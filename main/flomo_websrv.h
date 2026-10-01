// main/flomo_websrv.h —— 设备配置页(esp_http_server,端口 80),见 flomo_websrv.h 同名实现。
// 同一页面两用:配网模式(手机连设备热点 FoloPassport-XXXX 后访问
// http://192.168.4.1/)提交 Wi-Fi 凭证与 flomo/ASR 配置;普通模式(设备 IP)
// 维护 flomo/ASR 配置。设备只接受明文 HTTP:AP 与局域网内风险已写入文档。
// 配置应用逻辑不在本模块:POST /config、POST /wifi 的载荷原样交给钩子处理。
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

typedef struct {
    // 应用配置 JSON(载入→解析→保存→刷新上传);返回是否接受。
    bool (*apply_config)(const char *json, void *user);
    // 应用 Wi-Fi 凭证并发起连接(仅配网模式有效);返回是否接受。
    bool (*apply_wifi)(const char *ssid, const char *password, void *user);
    // 队列长度查询(用于状态接口),可为 NULL。
    size_t (*queue_count)(void *user);
    void *user;
} flomo_websrv_hooks_t;

esp_err_t flomo_websrv_start(const flomo_websrv_hooks_t *hooks);
esp_err_t flomo_websrv_stop(void);
bool flomo_websrv_running(void);
