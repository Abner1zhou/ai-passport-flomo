// main/flomo_websrv.c —— 局域网配置页(esp_http_server,端口 80),见 flomo_websrv.h。
// 凭证只写 NVS:asr_key 永不回显(/config.json 直接省略),请求体也绝不写日志。
#include "flomo_websrv.h"

#include "esp_http_server.h"
#include "esp_log.h"
#include "flomo_config.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "flomo_websrv";

// POST /config 请求体上限 2 KB,超出直接按非法请求拒绝。
#define BODY_MAX 2048
// /config.json 输出缓冲:各字段全转义(\uXXXX)的最坏情况 + 固定开销。
#define JSON_BUF_SIZE 2400

// httpd 默认单线程串行执行 handler,静态缓冲既省任务栈又无重入问题。
static char s_body[BODY_MAX + 1];
static char s_json[JSON_BUF_SIZE];

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
    "label{display:block;margin:12px 0 4px;font-size:14px}\n"
    "input{width:100%;box-sizing:border-box;padding:6px;font-size:14px}\n"
    "button{margin-top:16px;padding:8px 24px;font-size:15px}\n"
    "#msg{margin-top:12px;font-size:14px;white-space:pre-wrap}\n"
    "</style>\n"
    "</head>\n"
    "<body>\n"
    "<h2>flomo 语音备忘 · 配置</h2>\n"
    "<p id='queue'></p>\n"
    "<form id='cfg'>\n"
    "<label>flomo Webhook</label><input id='webhook' placeholder='https://flomoapp.com/iwh/...'>\n"
    "<label>ASR 接口 URL</label><input id='asr_url'>\n"
    "<label>ASR Key</label><input id='asr_key' type='password' autocomplete='off'>\n"
    "<label>ASR 模型</label><input id='asr_model'>\n"
    "<label>语言(如 zh,留空自动检测)</label><input id='language'>\n"
    "<button>保存</button>\n"
    "</form>\n"
    "<pre id='msg'></pre>\n"
    "<script>\n"
    "var $=function(i){return document.getElementById(i)};\n"
    "fetch('/config.json').then(function(r){return r.json()}).then(function(c){\n"
    "  $('asr_url').value=c.asr_url||'';\n"
    "  $('asr_model').value=c.asr_model||'';\n"
    "  $('language').value=c.language||'';\n"
    "  if(c.queue)$('queue').textContent='待上传队列:'+c.queue;\n"
    "}).catch(function(){});\n"
    "$('cfg').onsubmit=function(e){\n"
    "  e.preventDefault();\n"
    "  fetch('/config',{method:'POST',headers:{'Content-Type':'application/json'},\n"
    "    body:JSON.stringify({webhook:$('webhook').value,asr_url:$('asr_url').value,\n"
    "      asr_key:$('asr_key').value,asr_model:$('asr_model').value,\n"
    "      language:$('language').value})\n"
    "  }).then(function(r){\n"
    "    return r.text().then(function(t){$('msg').textContent=(r.ok?'保存成功 ':'保存失败 ')+t});\n"
    "  }).catch(function(){$('msg').textContent='请求失败'});\n"
    "};\n"
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

// 部分更新语义:先载入当前配置,parse 只覆盖出现的字段,再整体保存。
static esp_err_t post_config(httpd_req_t *req)
{
    if (req->content_len > BODY_MAX) {
        ESP_LOGW(TAG, "配置请求体过大: %u 字节", (unsigned)req->content_len);
        return send_status(req, false);
    }
    size_t received = 0;
    while (received < req->content_len) {
        int r = httpd_req_recv(req, s_body + received,
                               req->content_len - received);
        if (r <= 0) return ESP_FAIL;   // 接收中断,连接已由 httpd 处理
        received += (size_t)r;
    }
    s_body[received] = '\0';           // 请求体可能含 asr_key:绝不写日志

    flomo_config_t cfg;
    if (flomo_config_load(&cfg) != ESP_OK) {
        ESP_LOGE(TAG, "读取当前配置失败");
        return send_status(req, false);
    }
    flomo_config_parse_err_t perr = flomo_config_parse_json(&cfg, s_body);
    if (perr != FLOMO_CFG_PARSE_OK) {
        ESP_LOGW(TAG, "配置 JSON 非法: %d", perr);
        return send_status(req, false);
    }
    if (flomo_config_save(&cfg) != ESP_OK) {
        ESP_LOGE(TAG, "配置写入 NVS 失败");
        return send_status(req, false);
    }
    if (s_hooks.on_config_saved) s_hooks.on_config_saved(s_hooks.user);
    return send_status(req, true);
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
        { .uri = "/",           .method = HTTP_GET,  .handler = get_index },
        { .uri = "/config.json", .method = HTTP_GET,  .handler = get_config_json },
        { .uri = "/config",      .method = HTTP_POST, .handler = post_config },
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
    ESP_LOGI(TAG, "配置页就绪: http://<设备IP>/");
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
