// main/flomo_wifi.h —— Wi-Fi 管理:STA 自动连接 + SoftAP 网页配网(xiaozhi 风格)。
// 凭证由 ESP-IDF(WIFI_STORAGE_FLASH)存 NVS;配网热点为开放网络
// FoloPassport-XXXX,页面地址固定 http://192.168.4.1/。
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"
#include "esp_wifi.h"

typedef enum {
    FLOMO_WIFI_OFF = 0,
    FLOMO_WIFI_CONNECTING,
    FLOMO_WIFI_CONNECTED,     // ip 有效
    FLOMO_WIFI_FAILED,        // 连接失败(凭证错/超时);普通模式会自动重连
    FLOMO_WIFI_PROV_AP,       // 配网热点已开启,等待手机连入并提交配置
} flomo_wifi_state_t;

// 回调运行在 ESP 事件任务:只做入队/置标志,勿阻塞、勿碰 LVGL。
typedef void (*flomo_wifi_cb_t)(flomo_wifi_state_t state, const char *ip, void *user);

// 初始化 NVS/netif/事件循环/STA+AP netif 并启动;有已存凭证则自动连接。幂等。
esp_err_t flomo_wifi_start(flomo_wifi_cb_t cb, void *user);

bool flomo_wifi_connected(void);
const char *flomo_wifi_ip(void);        // 未连接返回 NULL
bool flomo_wifi_has_credentials(void);  // NVS 中是否已有 SSID
flomo_wifi_state_t flomo_wifi_state(void);  // 最近一次上报的状态快照

// ---- SoftAP 配网 ----

// 进入配网:切 APSTA、开开放热点 FoloPassport-XXXX(192.168.4.1),断开当前
// STA 连接并挂起自动重连(旧凭证不能在后台连上打断换网),并触发一次扫描。
esp_err_t flomo_wifi_begin_provisioning(void);

// 退出配网:关热点回 STA-only。未连接且有已存凭证则恢复连接。
esp_err_t flomo_wifi_end_provisioning(void);

bool flomo_wifi_provisioning(void);

// 网页提交的 Wi-Fi 凭证:写入 NVS(经 WIFI_STORAGE_FLASH)并发起连接。
// ssid 必填(≤32 字节),password 可空(开放网络,≤64 字节)。
esp_err_t flomo_wifi_apply_credentials(const char *ssid, const char *password);

// 配网热点名(含 MAC 后缀),供屏幕与文档展示。
void flomo_wifi_ap_ssid(char *buf, size_t cap);

// 最近一次扫描缓存快照(供网页下拉);返回写入的条数。
size_t flomo_wifi_scan_snapshot(wifi_ap_record_t *records, size_t max);

// 清除保存的 Wi-Fi 凭证并停止(设置页"清除配置"用)。之后需重新配网。
esp_err_t flomo_wifi_forget(void);
