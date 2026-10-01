// main/flomo_uploader.c —— 见 flomo_uploader.h。
//
// 内存预算(无 PSRAM):WAV 不整读,esp_http_client_open 后按 4KB 块从文件直写
// socket;ASR 响应限 4KB;两段 HTTPS 复用同一 8KB 栈外缓冲(任务栈 6KB)。
#include "flomo_uploader.h"

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "flomo_json.h"
#include "flomo_queue_policy.h"
#include "flomo_store.h"
#include "flomo_wifi.h"

#include "stdio.h"
#include "string.h"

static const char *TAG = "flomo_up";

#define UP_HTTP_BUF   4096
#define UP_TASK_STACK 6144
#define UP_ASR_RESP_MAX 4096
#define UP_TEXT_MAX   3072

typedef struct {
    flomo_config_t cfg;
    flomo_uploader_result_t cb;
    void *user;
    TaskHandle_t task;
    volatile bool busy;
} up_state_t;

static up_state_t s_up;

static void report(flomo_sync_result_t r, const char *detail)
{
    if (s_up.cb) s_up.cb(r, detail, s_up.user);
}

// ---- HTTP 基元 -----------------------------------------------------------

// 读完整响应体(≤cap-1 字节)。返回 ESP_OK 或传输错误。
static esp_err_t read_body(esp_http_client_handle_t client, char *body, size_t cap,
                           size_t *out_len)
{
    size_t total = 0;
    while (total + 1 < cap) {
        int n = esp_http_client_read(client, body + total, (int)(cap - 1 - total));
        if (n < 0) return ESP_FAIL;
        if (n == 0) break;                    // 对端关闭/读尽
        total += (size_t)n;
    }
    body[total] = '\0';
    *out_len = total;
    return ESP_OK;
}

// 上传 WAV 到 ASR 并取出转写文本。
// 失败分类写入 *err;text 缓冲由调用方提供(≥UP_TEXT_MAX)。
static bool asr_transcribe(const flomo_config_t *cfg, const flomo_memo_t *memo,
                           FILE *wav, char *text, size_t text_cap, flomo_qerr_t *err)
{
    static const char boundary[] = "----flomo7boundary";
    char head[512];
    int head_len = snprintf(head, sizeof(head),
        "--%s\r\n"
        "Content-Disposition: form-data; name=\"file\"; filename=\"memo.wav\"\r\n"
        "Content-Type: audio/wav\r\n\r\n", boundary);
    char fields[384];
    int fields_len = snprintf(fields, sizeof(fields),
        "\r\n--%s\r\n"
        "Content-Disposition: form-data; name=\"model\"\r\n\r\n%s\r\n"
        "--%s\r\n"
        "Content-Disposition: form-data; name=\"response_format\"\r\n\r\njson\r\n"
        "--%s\r\n"
        "Content-Disposition: form-data; name=\"language\"\r\n\r\n%s\r\n"
        "--%s--\r\n",
        boundary, cfg->asr_model, boundary, boundary, cfg->language, boundary);
    size_t content_len = (size_t)head_len + 44 + memo->data_bytes + (size_t)fields_len;

    char auth[192];
    snprintf(auth, sizeof(auth), "Bearer %s", cfg->asr_key);

    esp_http_client_config_t hc = {
        .url = cfg->asr_url,
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 30000,
        .buffer_size = UP_HTTP_BUF,
        .buffer_size_tx = UP_HTTP_BUF,
    };
    esp_http_client_handle_t client = esp_http_client_init(&hc);
    if (!client) { *err = FLOMO_QERR_NETWORK; return false; }

    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Authorization", auth);
    char ctype[64];
    snprintf(ctype, sizeof(ctype), "multipart/form-data; boundary=%s", boundary);
    esp_http_client_set_header(client, "Content-Type", ctype);

    bool ok = false;
    do {
        if (esp_http_client_open(client, (int64_t)content_len) != ESP_OK) break;
        if (esp_http_client_write(client, head, head_len) != head_len) break;
        // WAV:头 44 字节 + PCM 流式转发,不进堆。
        uint8_t wav_head[44];
        rewind(wav);
        if (fread(wav_head, 1, sizeof(wav_head), wav) != sizeof(wav_head)) break;
        if (esp_http_client_write(client, (const char *)wav_head, sizeof(wav_head)) !=
            (int)sizeof(wav_head)) break;
        static char chunk[4096];      // 静态:任务独占,避免挤压任务栈
        size_t left = memo->data_bytes;
        bool wfail = false;
        while (left > 0) {
            size_t want = left < sizeof(chunk) ? left : sizeof(chunk);
            size_t got = fread(chunk, 1, want, wav);
            if (got == 0) { wfail = true; break; }
            if (esp_http_client_write(client, chunk, (int)got) != (int)got) {
                wfail = true;
                break;
            }
            left -= got;
        }
        if (wfail) break;
        if (esp_http_client_write(client, fields, fields_len) != fields_len) break;

        int64_t rlen = esp_http_client_fetch_headers(client);
        (void)rlen;
        int status = esp_http_client_get_status_code(client);
        if (status != 200) {
            *err = flomo_qerr_from_http((int32_t)status);
            break;
        }
        char body[UP_ASR_RESP_MAX];
        size_t body_len = 0;
        if (read_body(client, body, sizeof(body), &body_len) != ESP_OK) {
            *err = FLOMO_QERR_NETWORK;
            break;
        }
        bool truncated = false;
        if (!flomo_json_get_string(body, "text", text, text_cap, &truncated) ||
            truncated || text[0] == '\0') {
            *err = FLOMO_QERR_PARSE;
            break;
        }
        ok = true;
    } while (0);

    esp_http_client_cleanup(client);
    if (!ok && *err != FLOMO_QERR_NETWORK && *err != FLOMO_QERR_PARSE &&
        *err != FLOMO_QERR_HTTP_429 && *err != FLOMO_QERR_HTTP_5XX &&
        *err != FLOMO_QERR_HTTP_4XX) {
        *err = FLOMO_QERR_NETWORK;    // write/open 失败等传输层问题
    }
    return ok;
}

// 把转写文本 POST 到 flomo webhook。
static bool flomo_post(const flomo_config_t *cfg, const char *text, flomo_qerr_t *err)
{
    static char body[UP_TEXT_MAX + UP_TEXT_MAX];    // 转义最坏翻倍
    body[0] = '\0';
    strcpy(body, "{\"content\":\"");
    char *payload = body + strlen(body);
    size_t need = flomo_json_escape(text, payload, sizeof(body) - strlen(body) - 2);
    if (need >= sizeof(body) - strlen(body) - 2) {
        *err = FLOMO_QERR_PARSE;                    // 文本超长:放弃该条
        return false;
    }
    strcat(body, "\"}");

    esp_http_client_config_t hc = {
        .url = cfg->webhook,
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 15000,
        .buffer_size = 2048,
        .buffer_size_tx = 2048,
    };
    esp_http_client_handle_t client = esp_http_client_init(&hc);
    if (!client) { *err = FLOMO_QERR_NETWORK; return false; }
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    bool ok = false;
    if (esp_http_client_open(client, (int64_t)strlen(body)) == ESP_OK &&
        esp_http_client_write(client, body, (int)strlen(body)) == (int)strlen(body) &&
        esp_http_client_fetch_headers(client) >= 0) {
        int status = esp_http_client_get_status_code(client);
        if (status == 200) {
            ok = true;
        } else {
            *err = flomo_qerr_from_http((int32_t)status);
        }
    } else {
        *err = FLOMO_QERR_NETWORK;
    }
    esp_http_client_cleanup(client);
    return ok;
}

// ---- 队列泵 --------------------------------------------------------------

static void up_task(void *arg)
{
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (s_up.busy) continue;
        s_up.busy = true;

        for (;;) {
            if (!flomo_wifi_connected() || !flomo_config_ready(&s_up.cfg)) {
                report(FLOMO_SYNC_IDLE, NULL);
                break;
            }
            flomo_memo_t memo;
            if (!flomo_store_peek_oldest(&memo)) {
                report(FLOMO_SYNC_IDLE, NULL);
                break;
            }
            FILE *wav = fopen(memo.path, "rb");
            if (!wav) {
                flomo_store_remove(&memo);          // 文件丢失:清掉元数据残骸
                continue;
            }
            static char text[UP_TEXT_MAX];
            flomo_qerr_t err = FLOMO_QERR_NETWORK;
            bool ok = asr_transcribe(&s_up.cfg, &memo, wav, text, sizeof(text), &err);
            fclose(wav);
            if (ok) {
                err = FLOMO_QERR_NETWORK;
                ok = flomo_post(&s_up.cfg, text, &err);
            }
            if (ok) {
                flomo_store_remove(&memo);
                report(FLOMO_SYNC_OK, text);
                continue;                            // 成功:立刻处理下一条
            }
            uint8_t attempts = (uint8_t)(memo.attempts + 1);
            if (flomo_queue_on_failure(err, attempts) == FLOMO_QACT_DROP) {
                flomo_store_remove(&memo);
                report(FLOMO_SYNC_DROPPED, "HTTP 4xx / too many attempts");
            } else {
                flomo_store_set_attempts(&memo, attempts);
                // 退避在设备侧由调用方(周期 kick + 状态屏)驱动;此处不 sleep,
                // 避免占用任务;下一轮 kick 自然在退避窗口之后。
                report(FLOMO_SYNC_QUEUED_RETRY, NULL);
            }
            break;                                   // 失败:本轮结束
        }
        s_up.busy = false;
    }
}

esp_err_t flomo_uploader_init(flomo_uploader_result_t cb, void *user)
{
    if (s_up.task) {
        s_up.cb = cb;
        s_up.user = user;
        return ESP_OK;
    }
    s_up.cb = cb;
    s_up.user = user;
    if (xTaskCreate(up_task, "flomo_up", UP_TASK_STACK, NULL, 4, &s_up.task) != pdPASS) {
        s_up.task = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void flomo_uploader_set_config(const flomo_config_t *cfg)
{
    // 上传任务只在 kick 后的泵循环里读;写指针操作按字宽对齐,拷贝期间任务空闲
    // (busy=false 且无 notify),竞面可接受。
    s_up.cfg = *cfg;
}

void flomo_uploader_kick(void)
{
    if (s_up.task) {
        xTaskNotify(s_up.task, 1, eSetValueWithOverwrite);
    }
}

bool flomo_uploader_busy(void)
{
    return s_up.busy;
}
