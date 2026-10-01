// main/flomo_websrv.h —— 局域网配置页:浏览器打开 http://<设备IP>/ 填写
// flomo webhook 与 ASR 凭证。Wi-Fi 已连接后启动;设备只接受同网段明文 HTTP,
// 凭证落 NVS(HTTPS 证书对动态 IP 不可行, LAN 内风险可接受并写入文档)。
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

typedef struct {
    // 配置保存成功后回调(通知 UI 刷新)。运行在 HTTP 服务任务,勿阻塞/碰 LVGL。
    void (*on_config_saved)(void *user);
    // 队列长度查询(用于 /status),可为 NULL。
    size_t (*queue_count)(void *user);
    void *user;
} flomo_websrv_hooks_t;

esp_err_t flomo_websrv_start(const flomo_websrv_hooks_t *hooks);
esp_err_t flomo_websrv_stop(void);
bool flomo_websrv_running(void);
