// main/flomo_wifi.h —— Wi-Fi STA 管理:自动连接已保存凭证、状态与 IP 上报。
// 凭证由 ESP-IDF(WIFI_STORAGE_FLASH)存 NVS,配网写入走 BluFi。
#pragma once

#include <stdbool.h>

#include "esp_err.h"

typedef enum {
    FLOMO_WIFI_OFF = 0,
    FLOMO_WIFI_CONNECTING,
    FLOMO_WIFI_CONNECTED,     // ip 有效
    FLOMO_WIFI_FAILED,        // 连接失败(凭证错/超时);会按 ESP-IDF 默认重连
} flomo_wifi_state_t;

// 回调运行在 ESP 事件任务:只做入队/置标志,勿阻塞、勿碰 LVGL。
typedef void (*flomo_wifi_cb_t)(flomo_wifi_state_t state, const char *ip, void *user);

// 初始化 NVS/netif/事件循环/STA 并启动;有已存凭证则自动连接。幂等。
esp_err_t flomo_wifi_start(flomo_wifi_cb_t cb, void *user);

bool flomo_wifi_connected(void);
const char *flomo_wifi_ip(void);        // 未连接返回 NULL
bool flomo_wifi_has_credentials(void);  // NVS 中是否已有 SSID

// 清除保存的 Wi-Fi 凭证并停止(设置页"清除配置"用)。之后需重新配网。
esp_err_t flomo_wifi_forget(void);
