// main/flomo_recorder.c —— 见 flomo_recorder.h。
#include "flomo_recorder.h"

#include "bsp_audio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "flomo_store.h"
#include "flomo_wav.h"

#include "stdio.h"
#include "string.h"
#include "stdlib.h"

static const char *TAG = "flomo_rec";

#define REC_CHUNK_MS   100
#define REC_CHUNK_BYTES ((FLOMO_RECORDER_SAMPLE_HZ * 2 * REC_CHUNK_MS) / 1000)
#define REC_TASK_STACK 4096

typedef enum {
    REC_CMD_STOP = 1,     // 结束并落盘
    REC_CMD_CANCEL,       // 丢弃
} rec_cmd_t;

typedef struct {
    flomo_recorder_done_t done;
    flomo_recorder_tick_t tick;
    void *user;
    FILE *file;
    char path[FLOMO_STORE_PATH_MAX];
    uint32_t bytes;               // 已写 PCM 字节数
    uint32_t sec;                 // 整秒数(用于 tick 节流)
    volatile bool active;
} rec_state_t;

static rec_state_t s_rec;
static TaskHandle_t s_task;
static SemaphoreHandle_t s_done_ack;    // 录音任务确认收尾后再接受下一次 start

static void rec_task(void *arg)
{
    (void)arg;
    uint8_t *chunk = malloc(REC_CHUNK_BYTES);
    if (!chunk) {
        ESP_LOGE(TAG, "录音缓冲分配失败(%d 字节)", REC_CHUNK_BYTES);
        s_rec.active = false;
        for (;;) vTaskSuspend(NULL);
    }
    for (;;) {
        uint32_t cmd = 0;
        // 等待录音请求:0=开始(来自 start 的 eNoAction),录音中为 STOP/CANCEL。
        if (xTaskNotifyWait(0, UINT32_MAX, &cmd, portMAX_DELAY) != pdTRUE) continue;
        if (!s_rec.active) continue;


        flomo_wav_fmt_t fmt = {
            .sample_rate = FLOMO_RECORDER_SAMPLE_HZ,
            .channels = 1,
            .bits = 16,
        };
        uint8_t header[44];
        flomo_wav_header(header, &fmt, 0);
        fwrite(header, 1, sizeof(header), s_rec.file);

        bool cancelled = false;
        while (s_rec.active) {
            if (bsp_audio_read(chunk, REC_CHUNK_BYTES) != ESP_OK) {
                ESP_LOGE(TAG, "I2S 读取失败,取消本段录音");
                cancelled = true;
                break;
            }
            if (fwrite(chunk, 1, REC_CHUNK_BYTES, s_rec.file) != REC_CHUNK_BYTES) {
                ESP_LOGE(TAG, "littlefs 写入失败(空间不足?),取消本段录音");
                cancelled = true;
                break;
            }
            s_rec.bytes += REC_CHUNK_BYTES;

            // 峰值电平:100ms 窗口内 |sample| 最大值 → 0..100。
            uint16_t peak = 0;
            const int16_t *samples = (const int16_t *)chunk;
            for (int i = 0; i < REC_CHUNK_BYTES / 2; i++) {
                int16_t v = samples[i] < 0 ? (int16_t)(~samples[i] + 1) : samples[i];
                if ((uint16_t)v > peak) peak = (uint16_t)v;
            }
            uint32_t sec = s_rec.bytes / (FLOMO_RECORDER_SAMPLE_HZ * 2);
            if (sec != s_rec.sec) {
                s_rec.sec = sec;
                if (s_rec.tick) s_rec.tick(sec, (uint8_t)(peak / 328), s_rec.user);
            }
            if (xTaskNotifyWait(0, UINT32_MAX, &cmd, 0) == pdTRUE) {
                if (cmd == REC_CMD_CANCEL) cancelled = true;
                break;                  // STOP:正常收尾
            }
            if (sec >= FLOMO_RECORDER_MAX_SEC) {
                ESP_LOGW(TAG, "达到最长 %d 秒,自动结束", FLOMO_RECORDER_MAX_SEC);
                break;
            }
        }

        s_rec.active = false;
        if (cancelled) {
            fclose(s_rec.file);
            remove(s_rec.path);
            ESP_LOGI(TAG, "录音已取消并删除");
        } else {
            flomo_wav_patch_sizes(header, s_rec.bytes);
            fseek(s_rec.file, 0, SEEK_SET);
            fwrite(header, 1, sizeof(header), s_rec.file);
            fclose(s_rec.file);
            ESP_LOGI(TAG, "录音完成:%s (%lu 字节 PCM)",
                     s_rec.path, (unsigned long)s_rec.bytes);
            if (s_rec.done) s_rec.done(s_rec.path, s_rec.user);
        }
        s_rec.file = NULL;
        xSemaphoreGive(s_done_ack);
    }
}

esp_err_t flomo_recorder_init(void)
{
    if (s_task) return ESP_OK;
    memset(&s_rec, 0, sizeof(s_rec));
    s_done_ack = xSemaphoreCreateBinary();
    if (!s_done_ack) return ESP_ERR_NO_MEM;
    xSemaphoreGive(s_done_ack);         // 初态视为"上一段已收尾"
    if (xTaskCreate(rec_task, "flomo_rec", REC_TASK_STACK, NULL, 5, &s_task) != pdPASS) {
        vSemaphoreDelete(s_done_ack);
        s_done_ack = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t flomo_recorder_start(flomo_recorder_done_t done,
                               flomo_recorder_tick_t tick, void *user)
{
    if (!s_task || !s_done_ack) return ESP_ERR_INVALID_STATE;
    if (s_rec.active) return ESP_ERR_INVALID_STATE;
    // 等上一段完全收尾(done 回调返回、文件关闭)再复用任务,避免新旧文件竞争。
    if (xSemaphoreTake(s_done_ack, pdMS_TO_TICKS(2000)) != pdTRUE) {
        return ESP_ERR_INVALID_STATE;
    }

    flomo_memo_t memo;
    if (flomo_store_alloc(&memo) != ESP_OK) return ESP_FAIL;
    if (s_rec.active) return ESP_ERR_INVALID_STATE;
    // 等上一段完全收尾(done 回调返回、文件关闭)再复用任务,避免新旧文件竞争。
    if (xSemaphoreTake(s_done_ack, pdMS_TO_TICKS(2000)) != pdTRUE) {
        return ESP_ERR_INVALID_STATE;
    }

    // 录音规格与 ASR 输入一致;bsp_audio_set_format 切换时先停读写,任务串行满足。
    esp_err_t err = bsp_audio_set_format(FLOMO_RECORDER_SAMPLE_HZ, 16, 1);
    if (err != ESP_OK) return err;

    FILE *f = fopen(memo.path, "wb");
    if (!f) return ESP_FAIL;
    setvbuf(f, NULL, _IOFBF, 8192);      // 8KB 落盘缓冲,抑制 littlefs 小写放大

    s_rec.done = done;
    s_rec.tick = tick;
    s_rec.user = user;
    s_rec.file = f;
    strcpy(s_rec.path, memo.path);
    s_rec.bytes = 0;
    s_rec.sec = 0;
    s_rec.active = true;
    xTaskNotify(s_task, 0, eNoAction);   // 唤醒任务进入录音循环
    return ESP_OK;
}

void flomo_recorder_stop(void)
{
    if (s_task && s_rec.active) {
        xTaskNotify(s_task, REC_CMD_STOP, eSetValueWithOverwrite);
    }
}

void flomo_recorder_cancel(void)
{
    if (s_task && s_rec.active) {
        xTaskNotify(s_task, REC_CMD_CANCEL, eSetValueWithOverwrite);
    }
}

bool flomo_recorder_active(void)
{
    return s_rec.active;
}
