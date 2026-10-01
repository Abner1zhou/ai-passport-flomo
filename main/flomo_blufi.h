// main/flomo_blufi.h —— BluFi(over NimBLE)配网:手机小程序写入 Wi-Fi 凭证。
// 移植自 demo/blufi-provisioning 分支;本应用只保留 STA 凭证交换,不带自定义数据。
// 凭证由 ESP-IDF 以 WIFI_STORAGE_FLASH 持久化到 NVS。
#pragma once

#include "esp_err.h"

typedef enum {
    FLOMO_BLUFI_OFF = 0,        // 未启动
    FLOMO_BLUFI_ADVERTISING,    // 广播中,等待小程序连接
    FLOMO_BLUFI_BLE_CONNECTED,  // 小程序已连 BLE,等待凭证
    FLOMO_BLUFI_WIFI_CONNECTING,
    FLOMO_BLUFI_WIFI_CONNECTED, // ssid/ip 有效
    FLOMO_BLUFI_FAILED,
} flomo_blufi_state_t;

// 状态回调运行在 Wi-Fi/BLE 事件上下文:只做入队或简短状态记录,勿阻塞/碰 LVGL。
// ssid/ip 可为 NULL(对应状态无值)。
typedef void (*flomo_blufi_cb_t)(flomo_blufi_state_t state,
                                 const char *ssid, const char *ip, void *user);

// 启动:初始化 Wi-Fi STA + NimBLE/BLUFI 并开始广播 "BLUFI_FoloPassport"。
// 成功后必须调用 flomo_blufi_stop() 释放(页面退出/配网完成)。
esp_err_t flomo_blufi_start(flomo_blufi_cb_t cb, void *user);

// 停止并释放 BLUFI/NimBLE/Wi-Fi 全部资源(可从任意状态调用,幂等)。
esp_err_t flomo_blufi_stop(void);
