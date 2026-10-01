// main/flomo_recorder.h —— 按键语音录音:I2S(ES8311)→ littlefs WAV 落盘。
// 无 PSRAM:PCM 不进堆,边录边写文件;16kHz/16bit/单声道 = 32KB/s。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#define FLOMO_RECORDER_SAMPLE_HZ 16000
#define FLOMO_RECORDER_MAX_SEC   120

// 录音结束事件(on_done path 为落盘的 WAV 路径;取消时无回调)。
// 回调运行在录音任务上下文:UI 更新须自持 bsp_lvgl_lock(),勿做重活。
typedef void (*flomo_recorder_done_t)(const char *path, void *user);
// 每秒进度:sec 为已录秒数,level 为 0..100 电平(峰值)。
typedef void (*flomo_recorder_tick_t)(uint32_t sec, uint8_t level, void *user);

// 常驻任务,app 启动时调用一次;失败后可重试。
esp_err_t flomo_recorder_init(void);

// 开始录音。正在录时返回 ESP_ERR_INVALID_STATE。
// done/tick 可为 NULL。flomo_store_init() 必须已成功。
esp_err_t flomo_recorder_start(flomo_recorder_done_t done,
                               flomo_recorder_tick_t tick, void *user);

// 请求结束:落盘 + done 回调(异步,立即返回)。
void flomo_recorder_stop(void);

// 请求取消:丢弃文件,无 done 回调。
void flomo_recorder_cancel(void);

bool flomo_recorder_active(void);
