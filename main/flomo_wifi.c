// main/flomo_wifi.c —— 见 flomo_wifi.h。
#include "flomo_wifi.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "nvs_flash.h"

#include "string.h"

static const char *TAG = "flomo_wifi";

#define AP_PREFIX    "FoloPassport-"
#define SCAN_CACHE_MAX 16

static esp_netif_t *s_sta;
static esp_netif_t *s_ap;
static esp_event_handler_instance_t s_wifi_handler;
static esp_event_handler_instance_t s_ip_handler;
static flomo_wifi_cb_t s_cb;
static void *s_user;
static volatile bool s_connected;
static volatile flomo_wifi_state_t s_state = FLOMO_WIFI_OFF;
static char s_ip[16];
static bool s_started;
// 配网模式:热点已开;期间 STA 断开不自动重连,由网页提交凭证后显式连接。
static volatile bool s_prov_mode;
static volatile bool s_prov_connecting;   // 一次网页提交发起的连接尝试在途
static char s_ap_ssid[sizeof(AP_PREFIX) + 4 + 1];
static wifi_ap_record_t s_scan_cache[SCAN_CACHE_MAX];
static volatile size_t s_scan_count;
// 扫描缓存:事件任务写(SCAN_DONE)、httpd 任务读(网页);短临界区隔开。
static portMUX_TYPE s_scan_lock = portMUX_INITIALIZER_UNLOCKED;

static void notify(flomo_wifi_state_t st, const char *ip)
{
    s_state = st;
    if (s_cb) s_cb(st, ip, s_user);
}

static void cache_scan_results(void)
{
    uint16_t count = SCAN_CACHE_MAX;
    esp_err_t err = esp_wifi_scan_get_ap_records(&count, s_scan_cache);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "扫描结果读取失败: %s", esp_err_to_name(err));
        return;
    }
    portENTER_CRITICAL(&s_scan_lock);
    s_scan_count = count;
    portEXIT_CRITICAL(&s_scan_lock);
}

static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    if (id == WIFI_EVENT_STA_START) {
        // 配网期间不会重触发;由 apply_credentials 显式连接。
        if (!s_prov_mode) {
            notify(FLOMO_WIFI_CONNECTING, NULL);
            esp_wifi_connect();
        }
    } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *event = data;
        s_connected = false;
        s_ip[0] = '\0';
        if (s_prov_mode) {
            // reason 8 = 本端主动断开(进入配网/换凭证),不是失败。
            if (s_prov_connecting && event->reason != WIFI_REASON_ASSOC_LEAVE) {
                s_prov_connecting = false;
                notify(FLOMO_WIFI_FAILED, NULL);
            }
            return;                     // 挂起重连:等网页再次提交或退出配网
        }
        // ESP-IDF 不自动重连;按参考 demo 的策略手动重连,失败状态交给 UI 呈现。
        notify(FLOMO_WIFI_FAILED, NULL);
        esp_wifi_connect();
    } else if (id == WIFI_EVENT_SCAN_DONE) {
        cache_scan_results();
    }
}

static void ip_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    if (id != IP_EVENT_STA_GOT_IP) return;
    ip_event_got_ip_t *event = data;
    snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&event->ip_info.ip));
    s_connected = true;
    s_prov_connecting = false;
    notify(FLOMO_WIFI_CONNECTED, s_ip);
}

esp_err_t flomo_wifi_start(flomo_wifi_cb_t cb, void *user)
{
    if (s_started) {
        if (cb) s_cb = cb;      // 幂等:仅更新回调
        s_user = user;
        return ESP_OK;
    }
    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK) {
        // 应用数据(含 flomo 配置)都在 NVS;不自动擦除。
        ESP_LOGE(TAG, "NVS 初始化失败: %s", esp_err_to_name(err));
        return err;
    }
    if ((err = esp_netif_init()) != ESP_OK) return err;
    if ((err = esp_event_loop_create_default()) != ESP_OK) return err;

    // STA 与 AP netif 都在 Wi-Fi 启动前创建;平时只跑 STA,配网时再切 APSTA。
    s_sta = esp_netif_create_default_wifi_sta();
    if (!s_sta) return ESP_ERR_NO_MEM;
    s_ap = esp_netif_create_default_wifi_ap();
    if (!s_ap) return ESP_ERR_NO_MEM;
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    if ((err = esp_wifi_init(&cfg)) != ESP_OK) return err;
    if ((err = esp_event_handler_instance_register(
             WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL, &s_wifi_handler)) != ESP_OK) {
        return err;
    }
    if ((err = esp_event_handler_instance_register(
             IP_EVENT, IP_EVENT_STA_GOT_IP, ip_event, NULL, &s_ip_handler)) != ESP_OK) {
        return err;
    }
    if ((err = esp_wifi_set_storage(WIFI_STORAGE_FLASH)) != ESP_OK) return err;
    if ((err = esp_wifi_set_mode(WIFI_MODE_STA)) != ESP_OK) return err;
    if ((err = esp_wifi_start()) != ESP_OK) return err;
    // 省电:MIN_MODEM 在 DTIM=1 网络保活较好;接受略高功耗换稳定连接。
    esp_wifi_set_ps(WIFI_PS_MIN_MODEM);

    s_cb = cb;
    s_user = user;
    s_started = true;
    return ESP_OK;
}

bool flomo_wifi_connected(void)
{
    return s_connected;
}

flomo_wifi_state_t flomo_wifi_state(void)
{
    return s_state;
}

const char *flomo_wifi_ip(void)
{
    return s_connected ? s_ip : NULL;
}

bool flomo_wifi_has_credentials(void)
{
    wifi_config_t cfg = { 0 };
    if (!s_started) return false;
    if (esp_wifi_get_config(WIFI_IF_STA, &cfg) != ESP_OK) return false;
    return cfg.sta.ssid[0] != '\0';
}

esp_err_t flomo_wifi_begin_provisioning(void)
{
    if (!s_started) return ESP_ERR_INVALID_STATE;
    if (s_prov_mode) return ESP_OK;

    // 热点名:FoloPassport-<STA MAC 末 2 字节>,避免多台设备同名。
    uint8_t mac[6] = { 0 };
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(s_ap_ssid, sizeof(s_ap_ssid), "%s%02X%02X",
             AP_PREFIX, mac[4], mac[5]);

    s_prov_mode = true;
    s_prov_connecting = false;
    s_scan_count = 0;
    esp_wifi_disconnect();              // 旧连接主动断开:事件里按 reason 8 静默

    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (err == ESP_OK) {
        wifi_config_t ap = { 0 };
        strcpy((char *)ap.ap.ssid, s_ap_ssid);
        ap.ap.ssid_len = (uint8_t)strlen(s_ap_ssid);
        ap.ap.channel = 1;
        ap.ap.authmode = WIFI_AUTH_OPEN;   // 开放热点:入口简单,风险见文档
        ap.ap.max_connection = 2;
        err = esp_wifi_set_config(WIFI_IF_AP, &ap);
    }
    if (err == ESP_OK) {
        wifi_scan_config_t scan = { 0 };
        esp_wifi_scan_start(&scan, false);    // 异步;SCAN_DONE 后入缓存
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "进入配网失败: %s", esp_err_to_name(err));
        s_prov_mode = false;
        return err;
    }
    notify(FLOMO_WIFI_PROV_AP, NULL);
    return ESP_OK;
}

esp_err_t flomo_wifi_end_provisioning(void)
{
    if (!s_started || !s_prov_mode) return ESP_OK;
    s_prov_mode = false;
    s_prov_connecting = false;
    esp_wifi_scan_stop();
    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_STA);  // 关热点,DHCP 随 netif 停
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "退出配网失败: %s", esp_err_to_name(err));
        return err;
    }
    if (!s_connected && flomo_wifi_has_credentials()) {
        // 仅改配置/中途退出:恢复旧凭证的连接。
        notify(FLOMO_WIFI_CONNECTING, NULL);
        esp_wifi_connect();
    }
    return ESP_OK;
}

bool flomo_wifi_provisioning(void)
{
    return s_prov_mode;
}

esp_err_t flomo_wifi_apply_credentials(const char *ssid, const char *password)
{
    if (!s_started || !s_prov_mode) return ESP_ERR_INVALID_STATE;
    if (!ssid || !ssid[0]) return ESP_ERR_INVALID_ARG;
    size_t ssid_len = strlen(ssid);
    size_t pass_len = password ? strlen(password) : 0;
    if (ssid_len > sizeof(((wifi_sta_config_t *)0)->ssid) ||
        pass_len > sizeof(((wifi_sta_config_t *)0)->password)) {
        return ESP_ERR_INVALID_SIZE;
    }

    wifi_config_t cfg = { 0 };
    memcpy(cfg.sta.ssid, ssid, ssid_len);
    if (pass_len > 0) memcpy(cfg.sta.password, password, pass_len);
    cfg.sta.threshold.authmode = WIFI_AUTH_OPEN;   // 由路由器实际加密决定

    s_prov_connecting = true;
    esp_wifi_disconnect();      // 换凭证时先断;事件按 reason 8 静默
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &cfg);
    if (err == ESP_OK) err = esp_wifi_connect();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "凭证应用失败: %s", esp_err_to_name(err));
        s_prov_connecting = false;
        return err;
    }
    notify(FLOMO_WIFI_CONNECTING, NULL);
    return ESP_OK;
}

void flomo_wifi_ap_ssid(char *buf, size_t cap)
{
    if (!buf || cap == 0) return;
    snprintf(buf, cap, "%s", s_ap_ssid[0] ? s_ap_ssid : AP_PREFIX);
}

size_t flomo_wifi_scan_snapshot(wifi_ap_record_t *records, size_t max)
{
    portENTER_CRITICAL(&s_scan_lock);
    size_t count = s_scan_count;
    portEXIT_CRITICAL(&s_scan_lock);
    if (count > max) count = max;
    if (count > 0 && records) {
        portENTER_CRITICAL(&s_scan_lock);
        memcpy(records, s_scan_cache, count * sizeof(records[0]));
        portEXIT_CRITICAL(&s_scan_lock);
    }
    return count;
}

esp_err_t flomo_wifi_forget(void)
{
    if (!s_started) return ESP_ERR_INVALID_STATE;
    wifi_config_t empty = { 0 };
    esp_err_t err = esp_wifi_disconnect();
    if (err == ESP_OK) err = esp_wifi_set_config(WIFI_IF_STA, &empty);
    s_connected = false;
    s_ip[0] = '\0';
    return err;
}
