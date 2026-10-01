// main/flomo_uploader.h —— 队列→ASR(OpenAI 兼容)→flomo webhook 的上传任务。
// 一次 kick 处理整个队列:逐条上传,遇可重试失败即停(按策略退避),成功继续。
#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "flomo_config.h"

typedef enum {
    FLOMO_SYNC_OK = 0,          // 一条备忘录已写入 flomo
    FLOMO_SYNC_QUEUED_RETRY,    // 失败但按策略保留待重试
    FLOMO_SYNC_DROPPED,         // 放弃(4XX/超次数),文件已删
    FLOMO_SYNC_IDLE,            // 无可上传项(队列空/未联网/未配置)
} flomo_sync_result_t;

// 结果回调运行在上传任务:UI 更新须自持 bsp_lvgl_lock();detail 为简述,可为 NULL。
typedef void (*flomo_uploader_result_t)(flomo_sync_result_t result,
                                        const char *detail, void *user);

esp_err_t flomo_uploader_init(flomo_uploader_result_t cb, void *user);

// 配置变更(启动/网页保存后)时刷新工作副本。cfg 指针会被拷贝,可立即释放。
void flomo_uploader_set_config(const flomo_config_t *cfg);

// 触发一轮队列处理;忙时忽略(当前轮结束后自然继续)。录音落盘、联网成功、
// 周期定时器都调它。
void flomo_uploader_kick(void);

bool flomo_uploader_busy(void);
