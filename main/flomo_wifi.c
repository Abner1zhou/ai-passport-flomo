// main/flomo_wifi.c —— 见 flomo_wifi.h。
#include "flomo_wifi.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#include "string.h"

static const char *TAG = "flomo_wifi";

static esp_netif_t *s_sta;
static esp_event_handler_instance_t s_wifi_handler;
static esp_event_handler_instance_t s_ip_handler;
static flomo_wifi_cb_t s_cb;
static void *s_user;
static volatile bool s_connected;
static char s_ip[16];
static bool s_started;

static void notify(flomo_wifi_state_t st, const char *ip)
{
    if (s_cb) s_cb(st, ip, s_user);
}

static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    if (id == WIFI_EVENT_STA_START) {
        notify(FLOMO_WIFI_CONNECTING, NULL);
        esp_wifi_connect();
    } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
        // ESP-IDF 不自动重连;按参考 demo 的策略手动重连,失败状态交给 UI 呈现。
        s_connected = false;
        s_ip[0] = '\0';
        notify(FLOMO_WIFI_FAILED, NULL);
        esp_wifi_connect();
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

    s_sta = esp_netif_create_default_wifi_sta();
    if (!s_sta) return ESP_ERR_NO_MEM;
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
