// main/flomo_blufi.c —— BLUFI(over NimBLE)配网:手机小程序写入 Wi-Fi 凭证。
// 移植自 demo/blufi-provisioning 分支 main/demo_blufi.c:保留 Wi-Fi 扫描/连接、
// 凭证收发(WIFI_STORAGE_FLASH 持久化)与完整的启停生命周期;
// 去掉示例的 LVGL 界面与按键逻辑,状态改经 flomo_blufi_cb_t 回调上报。
// NVS/esp_netif/事件环的初始化策略照搬该分支 demo_radio.c(失败不擦除 NVS)。
#include "flomo_blufi.h"
#include "flomo_blufi_security.h"

#include "esp_blufi.h"
#include "esp_blufi_api.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "host/ble_hs.h"
#include "lwip/ip4_addr.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "nvs_flash.h"
#include "services/gap/ble_svc_gap.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "flomo_blufi";
// ESP Config 微信小程序默认只显示以 "BLUFI" 开头的设备。
static const char *DEVICE_NAME = "BLUFI_FoloPassport";

#define BLUFI_AP_LIST_COUNT 16

static esp_netif_t *s_sta_netif;
static esp_event_handler_instance_t s_wifi_handler;
static esp_event_handler_instance_t s_ip_handler;
static wifi_config_t s_sta_config;
static volatile flomo_blufi_state_t s_state;
static char s_ip[16];
static flomo_blufi_cb_t s_cb;
static void *s_user;
static bool s_nvs_ready;
static bool s_netif_ready;
static bool s_event_loop_ready;
static bool s_wifi_initialized;
static bool s_wifi_started;
static bool s_wifi_handler_registered;
static bool s_ip_handler_registered;
static bool s_host_initialized;
static bool s_host_running;
static bool s_gatt_initialized;
static bool s_btc_initialized;
static bool s_profile_initialized;
static bool s_ble_connected;
static bool s_wifi_connecting;
static bool s_wifi_got_ip;
static bool s_reconnect_after_disconnect;
static SemaphoreHandle_t s_host_stopped;

// 状态变化唯一出口:随时可安全重入。回调跑在 Wi-Fi/BLE 事件上下文,
// 只传快照(ssid 取自 esp_wifi_get_config,ip 为最近 DHCP 结果),不做重活。
static void set_state(flomo_blufi_state_t state)
{
    s_state = state;
    if (!s_cb) return;
    const char *ssid = NULL;
    const char *ip = NULL;
    if (state == FLOMO_BLUFI_WIFI_CONNECTING || state == FLOMO_BLUFI_WIFI_CONNECTED) {
        wifi_config_t cfg;
        if (s_wifi_initialized &&
            esp_wifi_get_config(WIFI_IF_STA, &cfg) == ESP_OK &&
            cfg.sta.ssid[0] != '\0') {
            s_sta_config = cfg;
            ssid = (const char *)s_sta_config.sta.ssid;
        }
    }
    if (state == FLOMO_BLUFI_WIFI_CONNECTED && s_ip[0] != '\0') {
        ip = s_ip;
    }
    s_cb(state, ssid, ip, s_user);
}

static void send_wifi_report(esp_blufi_sta_conn_state_t state)
{
    wifi_mode_t mode = WIFI_MODE_STA;
    esp_wifi_get_mode(&mode);
    esp_blufi_extra_info_t info = { 0 };
    size_t ssid_len = strnlen((const char *)s_sta_config.sta.ssid,
                              sizeof(s_sta_config.sta.ssid));
    if (ssid_len > 0) {
        info.sta_ssid = s_sta_config.sta.ssid;
        info.sta_ssid_len = ssid_len;
    }
    esp_blufi_send_wifi_conn_report(mode, state, 0, &info);
}

static void send_wifi_list(void)
{
    uint16_t count = BLUFI_AP_LIST_COUNT;
    wifi_ap_record_t records[BLUFI_AP_LIST_COUNT] = { 0 };
    esp_blufi_ap_record_t list[BLUFI_AP_LIST_COUNT] = { 0 };
    esp_err_t err = esp_wifi_scan_get_ap_records(&count, records);
    if (err != ESP_OK) {
        esp_blufi_send_error_info(ESP_BLUFI_WIFI_SCAN_FAIL);
        return;
    }
    for (uint16_t i = 0; i < count; i++) {
        list[i].rssi = records[i].rssi;
        memcpy(list[i].ssid, records[i].ssid, sizeof(list[i].ssid));
    }
    if (s_ble_connected) esp_blufi_send_wifi_list(count, list);
}

static void request_wifi_connect(void)
{
    bool was_connected = s_wifi_got_ip;
    s_wifi_connecting = true;
    s_wifi_got_ip = false;
    set_state(FLOMO_BLUFI_WIFI_CONNECTING);
    if (was_connected) {
        s_reconnect_after_disconnect = true;
        if (esp_wifi_disconnect() == ESP_OK) return;
        s_reconnect_after_disconnect = false;
    }
    esp_err_t err = esp_wifi_connect();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_connect 失败: %s", esp_err_to_name(err));
        s_wifi_connecting = false;
        set_state(FLOMO_BLUFI_FAILED);
    }
}

static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    if (id == WIFI_EVENT_STA_START) {
        esp_wifi_get_config(WIFI_IF_STA, &s_sta_config);
        if (s_sta_config.sta.ssid[0] != '\0') {
            s_wifi_connecting = true;
            set_state(FLOMO_BLUFI_WIFI_CONNECTING);
            esp_wifi_connect();
        }
    } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *event = data;
        ESP_LOGW(TAG, "Wi-Fi 断开, reason=%u", event->reason);
        if (s_reconnect_after_disconnect) {
            s_reconnect_after_disconnect = false;
            esp_wifi_connect();
            return;
        }
        if (s_state == FLOMO_BLUFI_WIFI_CONNECTING) {
            send_wifi_report(ESP_BLUFI_STA_CONN_FAIL);
        }
        s_wifi_connecting = false;
        s_wifi_got_ip = false;
        s_ip[0] = '\0';
        set_state(s_ble_connected ? FLOMO_BLUFI_BLE_CONNECTED
                                  : FLOMO_BLUFI_ADVERTISING);
    } else if (id == WIFI_EVENT_SCAN_DONE) {
        send_wifi_list();
    }
}

static void ip_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    if (id != IP_EVENT_STA_GOT_IP) return;
    ip_event_got_ip_t *event = data;
    snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&event->ip_info.ip));
    s_wifi_connecting = false;
    s_wifi_got_ip = true;
    set_state(FLOMO_BLUFI_WIFI_CONNECTED);
    if (s_ble_connected) send_wifi_report(ESP_BLUFI_STA_CONN_SUCCESS);
}

static void blufi_reset(int reason)
{
    ESP_LOGE(TAG, "NimBLE 复位: %d", reason);
    set_state(FLOMO_BLUFI_FAILED);
}

static void blufi_sync(void)
{
    int rc = esp_blufi_profile_init();
    if (rc == 0) {
        s_profile_initialized = true;
    } else {
        ESP_LOGE(TAG, "esp_blufi_profile_init 失败: %d", rc);
        set_state(FLOMO_BLUFI_FAILED);
    }
}

static void host_task(void *arg)
{
    (void)arg;
    nimble_port_run();
    if (s_host_stopped) xSemaphoreGive(s_host_stopped);
    nimble_port_freertos_deinit();
}

static void blufi_event(esp_blufi_cb_event_t event, esp_blufi_cb_param_t *param)
{
    switch (event) {
    case ESP_BLUFI_EVENT_INIT_FINISH:
        esp_blufi_adv_start_with_name(DEVICE_NAME);
        if (s_wifi_got_ip) {
            set_state(FLOMO_BLUFI_WIFI_CONNECTED);
        } else if (s_wifi_connecting) {
            set_state(FLOMO_BLUFI_WIFI_CONNECTING);
        } else {
            set_state(FLOMO_BLUFI_ADVERTISING);
        }
        break;
    case ESP_BLUFI_EVENT_BLE_CONNECT:
        s_ble_connected = true;
        esp_blufi_adv_stop();
        if (flomo_blufi_security_init() != 0) {
            ESP_LOGE(TAG, "BLUFI 安全上下文分配失败");
            set_state(FLOMO_BLUFI_FAILED);
        } else if (!s_wifi_got_ip) {
            set_state(FLOMO_BLUFI_BLE_CONNECTED);
        }
        break;
    case ESP_BLUFI_EVENT_BLE_DISCONNECT:
        s_ble_connected = false;
        flomo_blufi_security_deinit();
        esp_blufi_adv_start_with_name(DEVICE_NAME);
        set_state(s_wifi_got_ip ? FLOMO_BLUFI_WIFI_CONNECTED
                                : FLOMO_BLUFI_ADVERTISING);
        break;
    case ESP_BLUFI_EVENT_SET_WIFI_OPMODE:
        esp_wifi_set_mode(WIFI_MODE_STA);
        break;
    case ESP_BLUFI_EVENT_RECV_STA_BSSID:
        memcpy(s_sta_config.sta.bssid, param->sta_bssid.bssid, 6);
        s_sta_config.sta.bssid_set = true;
        esp_wifi_set_config(WIFI_IF_STA, &s_sta_config);
        break;
    case ESP_BLUFI_EVENT_RECV_STA_SSID:
        if (param->sta_ssid.ssid_len >= sizeof(s_sta_config.sta.ssid)) {
            esp_blufi_send_error_info(ESP_BLUFI_DATA_FORMAT_ERROR);
            break;
        }
        memset(s_sta_config.sta.ssid, 0, sizeof(s_sta_config.sta.ssid));
        memset(s_sta_config.sta.password, 0, sizeof(s_sta_config.sta.password));
        memset(s_sta_config.sta.bssid, 0, sizeof(s_sta_config.sta.bssid));
        s_sta_config.sta.bssid_set = false;
        s_sta_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
        memcpy(s_sta_config.sta.ssid, param->sta_ssid.ssid,
               param->sta_ssid.ssid_len);
        esp_wifi_set_config(WIFI_IF_STA, &s_sta_config);
        break;
    case ESP_BLUFI_EVENT_RECV_STA_PASSWD:
        if (param->sta_passwd.passwd_len >= sizeof(s_sta_config.sta.password)) {
            esp_blufi_send_error_info(ESP_BLUFI_DATA_FORMAT_ERROR);
            break;
        }
        memset(s_sta_config.sta.password, 0, sizeof(s_sta_config.sta.password));
        memcpy(s_sta_config.sta.password, param->sta_passwd.passwd,
               param->sta_passwd.passwd_len);
        esp_wifi_set_config(WIFI_IF_STA, &s_sta_config);
        break;
    case ESP_BLUFI_EVENT_REQ_CONNECT_TO_AP:
        request_wifi_connect();
        break;
    case ESP_BLUFI_EVENT_REQ_DISCONNECT_FROM_AP:
        esp_wifi_disconnect();
        break;
    case ESP_BLUFI_EVENT_GET_WIFI_STATUS:
        if (s_wifi_got_ip) {
            send_wifi_report(ESP_BLUFI_STA_CONN_SUCCESS);
        } else if (s_wifi_connecting) {
            send_wifi_report(ESP_BLUFI_STA_CONNECTING);
        } else {
            send_wifi_report(ESP_BLUFI_STA_CONN_FAIL);
        }
        break;
    case ESP_BLUFI_EVENT_GET_WIFI_LIST: {
        wifi_scan_config_t config = { 0 };
        if (esp_wifi_scan_start(&config, false) != ESP_OK) {
            esp_blufi_send_error_info(ESP_BLUFI_WIFI_SCAN_FAIL);
        }
        break;
    }
    case ESP_BLUFI_EVENT_RECV_SLAVE_DISCONNECT_BLE:
        esp_blufi_disconnect();
        break;
    case ESP_BLUFI_EVENT_DEAUTHENTICATE_STA:
        esp_wifi_disconnect();
        break;
    case ESP_BLUFI_EVENT_REPORT_ERROR:
        esp_blufi_send_error_info(param->report_error.state);
        break;
    default:
        break;
    }
}

static esp_blufi_callbacks_t s_callbacks = {
    .event_cb = blufi_event,
    .negotiate_data_handler = flomo_blufi_security_negotiate,
    .encrypt_func = flomo_blufi_security_encrypt,
    .decrypt_func = flomo_blufi_security_decrypt,
    .checksum_func = flomo_blufi_security_checksum,
};

// NVS/网络底座。策略照搬 ref demo_radio.c:初始化失败不自动擦除分区——
// 本应用的队列索引与 flomo 配置都在 NVS 里,不能为配网丢数据。
static esp_err_t radio_prepare(void)
{
    if (!s_nvs_ready) {
        esp_err_t err = nvs_flash_init();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "NVS 初始化失败: %s;未自动擦除分区",
                     esp_err_to_name(err));
            return err;
        }
        s_nvs_ready = true;
    }
    if (!s_netif_ready) {
        esp_err_t err = esp_netif_init();
        if (err != ESP_OK) return err;
        s_netif_ready = true;
    }
    if (!s_event_loop_ready) {
        esp_err_t err = esp_event_loop_create_default();
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
        s_event_loop_ready = true;
    }
    return ESP_OK;
}

static esp_err_t wifi_start(void)
{
    esp_err_t err = radio_prepare();
    if (err != ESP_OK) return err;

    s_sta_netif = esp_netif_create_default_wifi_sta();
    if (!s_sta_netif) return ESP_ERR_NO_MEM;
    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&config);
    if (err != ESP_OK) return err;
    s_wifi_initialized = true;
    err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                              wifi_event, NULL, &s_wifi_handler);
    if (err != ESP_OK) return err;
    s_wifi_handler_registered = true;
    err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                              ip_event, NULL, &s_ip_handler);
    if (err != ESP_OK) return err;
    s_ip_handler_registered = true;
    err = esp_wifi_set_storage(WIFI_STORAGE_FLASH);
    if (err == ESP_OK) err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err == ESP_OK) err = esp_wifi_start();
    if (err == ESP_OK) s_wifi_started = true;
    return err;
}

static esp_err_t host_start(void)
{
    esp_err_t err = esp_blufi_register_callbacks(&s_callbacks);
    if (err != ESP_OK) return err;
    err = nimble_port_init();
    if (err != ESP_OK) return err;
    s_host_initialized = true;
    s_host_stopped = xSemaphoreCreateBinary();
    if (!s_host_stopped) return ESP_ERR_NO_MEM;
    ble_hs_cfg.reset_cb = blufi_reset;
    ble_hs_cfg.sync_cb = blufi_sync;
    ble_hs_cfg.gatts_register_cb = esp_blufi_gatt_svr_register_cb;
    int rc = esp_blufi_gatt_svr_init();
    if (rc != 0) return ESP_FAIL;
    s_gatt_initialized = true;
    rc = ble_svc_gap_device_name_set(DEVICE_NAME);
    if (rc != 0) return ESP_FAIL;
    esp_blufi_btc_init();
    s_btc_initialized = true;
    err = esp_nimble_enable(host_task);
    if (err == ESP_OK) s_host_running = true;
    return err;
}

esp_err_t flomo_blufi_start(flomo_blufi_cb_t cb, void *user)
{
    if (s_wifi_initialized || s_host_initialized) {
        return ESP_ERR_INVALID_STATE;   // 已启动:先 flomo_blufi_stop()
    }
    s_cb = cb;
    s_user = user;
    s_ip[0] = '\0';
    s_ble_connected = false;
    s_wifi_connecting = false;
    s_wifi_got_ip = false;
    s_reconnect_after_disconnect = false;
    memset(&s_sta_config, 0, sizeof(s_sta_config));
    s_state = FLOMO_BLUFI_OFF;

    esp_err_t err = wifi_start();
    if (err == ESP_OK) err = host_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "BLUFI 启动失败: %s", esp_err_to_name(err));
        set_state(FLOMO_BLUFI_FAILED);  // 资源由调用方 flomo_blufi_stop() 清理
        return err;
    }
    return ESP_OK;
}

esp_err_t flomo_blufi_stop(void)
{
    // 先摘回调:拆除过程中 Wi-Fi/BLE 事件仍会触发,不能再打到已退出的调用方。
    s_cb = NULL;
    s_user = NULL;
    s_ble_connected = false;
    flomo_blufi_security_deinit();
    if (s_host_initialized) {
        if (s_profile_initialized) esp_blufi_adv_stop();
        if (s_gatt_initialized) {
            esp_blufi_gatt_svr_deinit();
            s_gatt_initialized = false;
        }
        bool host_stopped = !s_host_running;
        if (s_host_running) {
            int rc = nimble_port_stop();
            if (rc == 0) {
                xSemaphoreTake(s_host_stopped, portMAX_DELAY);
                host_stopped = true;
            } else {
                ESP_LOGE(TAG, "nimble_port_stop 失败: %d", rc);
            }
        }
        if (host_stopped) nimble_port_deinit();
        s_host_running = false;
        if (s_profile_initialized) {
            esp_blufi_profile_deinit();
            s_profile_initialized = false;
        }
        if (s_btc_initialized) {
            esp_blufi_btc_deinit();
            s_btc_initialized = false;
        }
        s_host_initialized = false;
    }
    if (s_host_stopped) {
        vSemaphoreDelete(s_host_stopped);
        s_host_stopped = NULL;
    }
    if (s_wifi_started) {
        esp_wifi_scan_stop();
        esp_wifi_disconnect();
        esp_wifi_stop();
        s_wifi_started = false;
    }
    if (s_ip_handler_registered) {
        esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                              s_ip_handler);
        s_ip_handler_registered = false;
    }
    if (s_wifi_handler_registered) {
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                              s_wifi_handler);
        s_wifi_handler_registered = false;
    }
    if (s_wifi_initialized) {
        esp_wifi_deinit();
        s_wifi_initialized = false;
    }
    if (s_sta_netif) {
        esp_netif_destroy_default_wifi(s_sta_netif);
        s_sta_netif = NULL;
    }
    s_wifi_connecting = false;
    s_wifi_got_ip = false;
    s_reconnect_after_disconnect = false;
    s_ip[0] = '\0';
    s_state = FLOMO_BLUFI_OFF;
    return ESP_OK;
}
