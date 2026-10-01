// main/flomo_websrv.c —— 设备配置页(esp_http_server,端口 80),见 flomo_websrv.h。
// 凭证只写 NVS:asr_key 永不回显(/config.json 直接省略),请求体也绝不写日志。
#include "flomo_websrv.h"

#include "esp_http_server.h"
#include "esp_log.h"
#include "flomo_config.h"
#include "flomo_json.h"
#include "flomo_wifi.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "flomo_websrv";

// POST 请求体上限 2 KB,超出直接按非法请求拒绝。
#define BODY_MAX 2048
// /config.json 输出缓冲:各字段全转义(\uXXXX)的最坏情况 + 固定开销。
#define JSON_BUF_SIZE 2400
// /scan.json 输出缓冲:SSID 全转义的最坏情况按条预留,不足即截断列表。
#define SCAN_JSON_SIZE 3072
#define STATUS_JSON_SIZE 128

// httpd 默认单线程串行执行 handler,静态缓冲既省任务栈又无重入问题。
static char s_body[BODY_MAX + 1];
static char s_json[JSON_BUF_SIZE];
static char s_scan_json[SCAN_JSON_SIZE];
static char s_status_json[STATUS_JSON_SIZE];

static httpd_handle_t s_server;
static flomo_websrv_hooks_t s_hooks;

// 配置页:属性一律单引号,整段 HTML 内不出现双引号,免去 C 字符串转义。
static const char PAGE[] =
    "<!DOCTYPE html>\n"
    "<html lang='zh'>\n"
    "<head>\n"
    "<meta charset='utf-8'>\n"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>\n"
    "<title>flomo 配置</title>\n"
    "<style>\n"
    "body{font-family:sans-serif;max-width:480px;margin:24px auto;padding:0 12px}\n"
    "h3{margin:16px 0 0}\n"
    "label{display:block;margin:12px 0 4px;font-size:14px}\n"
    "input,select{width:100%;box-sizing:border-box;padding:6px;font-size:14px}\n"
    "button{margin-top:16px;padding:8px 24px;font-size:15px}\n"
    "#msg{margin-top:12px;font-size:14px;white-space:pre-wrap}\n"
    "#st{font-size:14px;color:#555}\n"
    "</style>\n"
    "</head>\n"
    "<body>\n"
    "<h2>flomo 语音备忘 · 配置</h2>\n"
    "<p id='st'></p>\n"
    "<form id='cfg'>\n"
    "<div id='wifi' hidden>\n"
    "<h3>Wi-Fi</h3>\n"
    "<select id='pick'><option value=''>— 选择网络 —</option></select>\n"
    "<label>网络名(可手动输入)</label><input id='ssid'>\n"
    "<label>Wi-Fi 密码(开放网络留空)</label><input id='pass' type='password' autocomplete='off'>\n"
    "</div>\n"
    "<label>flomo Webhook</label><input id='webhook' placeholder='https://flomoapp.com/iwh/...'>\n"
    "<label>ASR 接口 URL</label><input id='asr_url'>\n"
    "<label>ASR Key</label><input id='asr_key' type='password' autocomplete='off'>\n"
    "<label>ASR 模型</label><input id='asr_model'>\n"
    "<label>语言(如 zh,留空自动检测)</label><input id='language'>\n"
    "<button id='save'>保存</button>\n"
    "</form>\n"
    "<pre id='msg'></pre>\n"
    "<script>\n"
    "var $=function(i){return document.getElementById(i)};\n"
    "var prov=false;\n"
    "function post(url,obj){\n"
    "  return fetch(url,{method:'POST',headers:{'Content-Type':'application/json'},\n"
    "    body:JSON.stringify(obj)}).then(function(r){return r.ok});\n"
    "}\n"
    "$('pick').onchange=function(){$('ssid').value=this.value};\n"
    "$('cfg').onsubmit=function(e){\n"
    "  e.preventDefault();\n"
    "  var cfg={webhook:$('webhook').value,asr_url:$('asr_url').value,\n"
    "    asr_model:$('asr_model').value,language:$('language').value};\n"
    "  if($('asr_key').value)cfg.asr_key=$('asr_key').value;\n"
    "  post('/config',cfg).then(function(ok){\n"
    "    if(!ok){$('msg').textContent='保存失败:配置字段非法(webhook/接口须 https:// 开头)';return}\n"
    "    if(!prov||!$('ssid').value){$('msg').textContent='保存成功';return}\n"
    "    $('msg').textContent='配置已保存,正在连接 Wi-Fi…';\n"
    "    post('/wifi',{ssid:$('ssid').value,password:$('pass').value}).then(function(ok){\n"
    "      if(!ok){$('msg').textContent='凭证提交失败:检查网络名后重试';return}\n"
    "      poll(0);\n"
    "    });\n"
    "  });\n"
    "};\n"
    "function poll(n){\n"
    "  fetch('/status.json').then(function(r){return r.json()}).then(function(s){\n"
    "    if(s.wifi=='connected'){$('msg').textContent='已连接 '+s.ip+';热点即将关闭,设备回到主页';return}\n"
    "    if(s.wifi=='failed'){$('msg').textContent='连接失败:检查密码后重新保存';return}\n"
    "    if(n>40){$('msg').textContent='连接超时,请靠近路由器重试';return}\n"
    "    setTimeout(function(){poll(n+1)},1000);\n"
    "  }).catch(function(){setTimeout(function(){poll(n+1)},1000)});\n"
    "}\n"
    "function refresh(){\n"
    "  fetch('/config.json').then(function(r){return r.json()}).then(function(c){\n"
    "    $('asr_url').value=c.asr_url||'';$('asr_model').value=c.asr_model||'';\n"
    "    $('language').value=c.language||'';$('webhook').value=c.webhook||'';\n"
    "    if(c.queue)$('st').textContent='待上传队列:'+c.queue;\n"
    "  }).catch(function(){});\n"
    "  fetch('/status.json').then(function(r){return r.json()}).then(function(s){\n"
    "    if(s.prov){\n"
    "      prov=true;$('wifi').hidden=false;$('save').textContent='保存并连接';\n"
    "      $('st').textContent='配网模式:选择 Wi-Fi,填好配置后点保存并连接';\n"
    "      fetch('/scan.json').then(function(r){return r.json()}).then(function(l){\n"
    "        for(var i=0;i<l.aps.length;i++){\n"
    "          var a=l.aps[i],o=document.createElement('option');\n"
    "          o.value=a.ssid;\n"
    "          o.textContent=a.ssid+' ('+a.rssi+(a.lock?',需密码':'')+')';\n"
    "          $('pick').appendChild(o);\n"
    "        }\n"
    "      }).catch(function(){});\n"
    "    }else if(s.ip){\n"
    "      $('st').textContent=($('st').textContent?$('st').textContent+' · ':'')+'设备IP:'+s.ip;\n"
    "    }\n"
    "  }).catch(function(){});\n"
    "}\n"
    "refresh();\n"
    "</script>\n"
    "</body>\n"
    "</html>\n";

static char *app_lit(char *p, char *end, const char *s)
{
    while (*s && p < end) *p++ = *s++;
    return p;
}

// 追加一个带引号、已转义的 JSON 字符串。预留 7 字节保证 \uXXXX + 收尾引号完整。
static char *app_json_str(char *p, char *end, const char *s)
{
    if (p >= end) return p;
    *p++ = '"';
    for (; *s && end - p >= 7; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') {
            *p++ = '\\';
            *p++ = (char)c;
        } else if (c < 0x20) {
            p += sprintf(p, "\\u%04x", c);
        } else {
            *p++ = (char)c;
        }
    }
    if (p < end) *p++ = '"';
    return p;
}

static esp_err_t send_status(httpd_req_t *req, bool ok)
{
    httpd_resp_set_type(req, "application/json");
    if (!ok) {
        // 出错路径可能没读完请求体,关闭连接避免残留字节被当成下一个请求。
        httpd_resp_set_hdr(req, "Connection", "close");
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_send(req, "{\"status\":\"error\"}", HTTPD_RESP_USE_STRLEN);
    }
    return httpd_resp_send(req, "{\"status\":\"ok\"}", HTTPD_RESP_USE_STRLEN);
}

// 收完 POST 请求体到 s_body(上限 BODY_MAX,超出返回 ESP_FAIL 并由调用方回 400)。
static esp_err_t read_body(httpd_req_t *req)
{
    if (req->content_len > BODY_MAX) {
        ESP_LOGW(TAG, "请求体过大: %u 字节", (unsigned)req->content_len);
        return ESP_ERR_INVALID_SIZE;
    }
    size_t received = 0;
    while (received < req->content_len) {
        int r = httpd_req_recv(req, s_body + received,
                               req->content_len - received);
        if (r <= 0) return ESP_FAIL;   // 接收中断,连接已由 httpd 处理
        received += (size_t)r;
    }
    s_body[received] = '\0';           // 请求体可能含 asr_key/Wi-Fi 密码:绝不写日志
    return ESP_OK;
}

static esp_err_t get_index(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, PAGE, HTTPD_RESP_USE_STRLEN);
}

// 当前配置 + 队列长度。asr_key 有意不输出:密钥永不回显到页面。
static esp_err_t get_config_json(httpd_req_t *req)
{
    flomo_config_t cfg;
    if (flomo_config_load(&cfg) != ESP_OK) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_send(req, "{\"status\":\"error\"}", HTTPD_RESP_USE_STRLEN);
    }
    size_t queue = s_hooks.queue_count ? s_hooks.queue_count(s_hooks.user) : 0;

    char *p = s_json;
    char *end = s_json + sizeof(s_json) - 1;
    p = app_lit(p, end, "{\"webhook\":");
    p = app_json_str(p, end, cfg.webhook);
    p = app_lit(p, end, ",\"asr_url\":");
    p = app_json_str(p, end, cfg.asr_url);
    p = app_lit(p, end, ",\"asr_model\":");
    p = app_json_str(p, end, cfg.asr_model);
    p = app_lit(p, end, ",\"language\":");
    p = app_json_str(p, end, cfg.language);
    int n = snprintf(p, (size_t)(end - p) + 1, ",\"queue\":%u}", (unsigned)queue);
    if (n < 0 || p + n > end) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_send(req, "{\"status\":\"error\"}", HTTPD_RESP_USE_STRLEN);
    }
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, s_json, HTTPD_RESP_USE_STRLEN);
}

// 配网/连接状态,页面据此切换展示并轮询连接结果。
static esp_err_t get_status_json(httpd_req_t *req)
{
    flomo_wifi_state_t st = flomo_wifi_state();
    const char *wifi = "off";
    if (st == FLOMO_WIFI_CONNECTING) wifi = "connecting";
    if (st == FLOMO_WIFI_CONNECTED) wifi = "connected";
    if (st == FLOMO_WIFI_FAILED) wifi = "failed";
    if (st == FLOMO_WIFI_PROV_AP) wifi = "ap";
    const char *ip = flomo_wifi_connected() ? flomo_wifi_ip() : NULL;
    int n;
    if (ip) {
        n = snprintf(s_status_json, sizeof(s_status_json),
                     "{\"prov\":%s,\"wifi\":\"%s\",\"ip\":\"%s\",\"queue\":%u}",
                     flomo_wifi_provisioning() ? "true" : "false", wifi, ip,
                     (unsigned)(s_hooks.queue_count
                                    ? s_hooks.queue_count(s_hooks.user) : 0));
    } else {
        n = snprintf(s_status_json, sizeof(s_status_json),
                     "{\"prov\":%s,\"wifi\":\"%s\",\"ip\":null,\"queue\":%u}",
                     flomo_wifi_provisioning() ? "true" : "false", wifi,
                     (unsigned)(s_hooks.queue_count
                                    ? s_hooks.queue_count(s_hooks.user) : 0));
    }
    if (n < 0 || (size_t)n >= sizeof(s_status_json)) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_send(req, "{\"status\":\"error\"}", HTTPD_RESP_USE_STRLEN);
    }
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, s_status_json, HTTPD_RESP_USE_STRLEN);
}

// 扫描缓存 → 下拉列表数据。缓冲不足时截断条目,保证 JSON 完整。
static esp_err_t get_scan_json(httpd_req_t *req)
{
    wifi_ap_record_t aps[16];
    size_t count = flomo_wifi_scan_snapshot(aps, sizeof(aps) / sizeof(aps[0]));
    char *p = s_scan_json;
    char *end = s_scan_json + sizeof(s_scan_json) - 1;
    p = app_lit(p, end, "{\"aps\":[");
    bool first = true;
    for (size_t i = 0; i < count; i++) {
        if (aps[i].ssid[0] == '\0') continue;          // 隐藏网络不列,走手动输入
        // 一条全转义 SSID 的最坏开销 ≈ 33×6 + 概览字段,不足即止。
        if (end - p < 280) break;
        p = app_lit(p, end, first ? "{\"ssid\":" : ",{\"ssid\":");
        first = false;
        p = app_json_str(p, end, (const char *)aps[i].ssid);
        int n = snprintf(p, (size_t)(end - p) + 1, ",\"rssi\":%d,\"lock\":%s}",
                         (int)aps[i].rssi,
                         aps[i].authmode == WIFI_AUTH_OPEN ? "false" : "true");
        if (n < 0 || p + n > end) break;
        p += n;
    }
    p = app_lit(p, end, "]}");
    *p = '\0';
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, s_scan_json, HTTPD_RESP_USE_STRLEN);
}

// flomo/ASR 配置提交:解析与保存在 apply_config 钩子里(与配网共用同一入口)。
static esp_err_t post_config(httpd_req_t *req)
{
    if (read_body(req) != ESP_OK) return send_status(req, false);
    bool ok = s_hooks.apply_config && s_hooks.apply_config(s_body, s_hooks.user);
    return send_status(req, ok);
}

// Wi-Fi 凭证提交(配网模式):应用后由页面轮询 /status.json 等结果。
static esp_err_t post_wifi(httpd_req_t *req)
{
    if (read_body(req) != ESP_OK) return send_status(req, false);
    char ssid[33] = { 0 };
    char password[65] = { 0 };
    bool truncated = false;
    if (!flomo_json_get_string(s_body, "ssid", ssid, sizeof(ssid), &truncated) ||
        truncated || ssid[0] == '\0') {
        return send_status(req, false);
    }
    // 键缺席 = 空密码(开放网络);找到但超长才拒绝。
    flomo_json_get_string(s_body, "password", password, sizeof(password), &truncated);
    if (truncated) return send_status(req, false);
    bool ok = s_hooks.apply_wifi && s_hooks.apply_wifi(ssid, password, s_hooks.user);
    return send_status(req, ok);
}

esp_err_t flomo_websrv_start(const flomo_websrv_hooks_t *hooks)
{
    if (s_server) return ESP_OK;       // 幂等:已在运行,保持首次注册的 hooks

    memset(&s_hooks, 0, sizeof(s_hooks));
    if (hooks) s_hooks = *hooks;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();   // 默认端口 80
    esp_err_t err = httpd_start(&s_server, &config);
    if (err != ESP_OK) {
        s_server = NULL;
        ESP_LOGE(TAG, "httpd 启动失败: %s", esp_err_to_name(err));
        return err;
    }

    static const httpd_uri_t routes[] = {
        { .uri = "/",            .method = HTTP_GET,  .handler = get_index },
        { .uri = "/config.json", .method = HTTP_GET,  .handler = get_config_json },
        { .uri = "/status.json", .method = HTTP_GET,  .handler = get_status_json },
        { .uri = "/scan.json",   .method = HTTP_GET,  .handler = get_scan_json },
        { .uri = "/config",      .method = HTTP_POST, .handler = post_config },
        { .uri = "/wifi",        .method = HTTP_POST, .handler = post_wifi },
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        err = httpd_register_uri_handler(s_server, &routes[i]);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "注册 %s 失败: %s", routes[i].uri,
                     esp_err_to_name(err));
            flomo_websrv_stop();
            return err;
        }
    }
    ESP_LOGI(TAG, "配置页就绪: 配网模式 http://192.168.4.1/ 或设备 IP");
    return ESP_OK;
}

esp_err_t flomo_websrv_stop(void)
{
    if (!s_server) return ESP_OK;      // 幂等
    httpd_stop(s_server);              // 断开连接并注销全部 URI 处理器
    s_server = NULL;
    memset(&s_hooks, 0, sizeof(s_hooks));
    return ESP_OK;
}

bool flomo_websrv_running(void)
{
    return s_server != NULL;
}
