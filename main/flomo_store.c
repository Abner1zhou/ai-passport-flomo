// main/flomo_store.c —— littlefs 队列存储实现,见 flomo_store.h。
#include "flomo_store.h"

#include "esp_littlefs.h"
#include "esp_log.h"

#include "dirent.h"
#include "stdio.h"
#include "string.h"
#include "sys/stat.h"
static const char *TAG = "flomo_store";

static bool s_mounted;

esp_err_t flomo_store_init(void)
{
    if (s_mounted) return ESP_OK;
    esp_vfs_littlefs_conf_t conf = {
        .partition_label = "storage",
        .base_path = FLOMO_STORE_MOUNT,
        .format_if_mount_failed = true,   // 队列是可再生产的数据,损坏即重建
        .dont_mount = false,
    };
    esp_err_t err = esp_vfs_littlefs_register(&conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "littlefs 挂载失败: %s", esp_err_to_name(err));
        return err;
    }
    s_mounted = true;
    mkdir(FLOMO_STORE_QUEUE, 0700);       // 已存在时忽略 EEXIST
    return ESP_OK;
}

// 文件名 → 序号。仅识别恰好 6 位数字 + ".wav"。
static bool memo_seq(const char *name, uint32_t *seq)
{
    size_t len = strlen(name);
    if (len != 10 || strncmp(name + 6, ".wav", 4) != 0) return false;
    uint32_t v = 0;
    for (int i = 0; i < 6; i++) {
        if (name[i] < '0' || name[i] > '9') return false;
        v = v * 10 + (uint32_t)(name[i] - '0');
    }
    *seq = v;
    return true;
}

static void meta_path(const flomo_memo_t *memo, char *out, size_t out_size)
{
    snprintf(out, out_size, "%.*s.meta", (int)(strlen(memo->path) - 4), memo->path);
}

// 扫描队列目录,输出计数与最老/最新序号;scan 为 NULL 时只做计数。
static void scan(size_t *count, uint32_t *oldest, uint32_t *newest)
{
    *count = 0;
    if (oldest) *oldest = UINT32_MAX;
    if (newest) *newest = 0;
    DIR *dir = opendir(FLOMO_STORE_QUEUE);
    if (!dir) return;
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        uint32_t seq;
        if (!memo_seq(ent->d_name, &seq)) continue;
        (*count)++;
        if (oldest && seq < *oldest) *oldest = seq;
        if (newest && seq > *newest) *newest = seq;
    }
    closedir(dir);
}

// 从 WAV 头读 data 长度;失败(文件残缺)按 0 处理,由调用方决定删除。
static uint32_t wav_data_bytes(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    uint8_t h[44];
    size_t got = fread(h, 1, sizeof(h), f);
    fclose(f);
    if (got != sizeof(h) || memcmp(h + 36, "data", 4) != 0) return 0;
    return (uint32_t)h[40] | ((uint32_t)h[41] << 8) |
           ((uint32_t)h[42] << 16) | ((uint32_t)h[43] << 24);
}

static uint8_t read_attempts(const char *wav_path)
{
    char mpath[FLOMO_STORE_PATH_MAX + 6];
    snprintf(mpath, sizeof(mpath), "%.*s.meta",
             (int)(strlen(wav_path) - 4), wav_path);
    FILE *f = fopen(mpath, "r");
    if (!f) return 0;
    int v = 0;
    int n = fscanf(f, "%d", &v);
    fclose(f);
    return (n == 1 && v >= 0 && v <= 255) ? (uint8_t)v : 0;
}

size_t flomo_store_count(void)
{
    size_t count;
    uint32_t oldest;
    scan(&count, &oldest, NULL);
    return count;
}

bool flomo_store_peek_oldest(flomo_memo_t *out)
{
    size_t count;
    uint32_t oldest;
    scan(&count, &oldest, NULL);
    if (count == 0) return false;
    snprintf(out->path, sizeof(out->path), FLOMO_STORE_QUEUE "/%06lu.wav",
             (unsigned long)oldest);
    out->data_bytes = wav_data_bytes(out->path);
    out->attempts = read_attempts(out->path);
    return true;
}

esp_err_t flomo_store_alloc(flomo_memo_t *out)
{
    size_t count;
    uint32_t newest;
    scan(&count, NULL, &newest);
    uint32_t seq = newest + 1;
    if (seq > 999999) return ESP_FAIL;   // 序号空间满:队列清空后自然复用
    snprintf(out->path, sizeof(out->path), FLOMO_STORE_QUEUE "/%06lu.wav",
             (unsigned long)seq);
    out->data_bytes = 0;
    out->attempts = 0;
    return ESP_OK;
}

esp_err_t flomo_store_set_attempts(const flomo_memo_t *memo, uint8_t attempts)
{
    char mpath[FLOMO_STORE_PATH_MAX + 6];
    meta_path(memo, mpath, sizeof(mpath));
    FILE *f = fopen(mpath, "w");
    if (!f) return ESP_FAIL;
    fprintf(f, "%u\n", (unsigned)attempts);
    fclose(f);
    return ESP_OK;
}

void flomo_store_remove(const flomo_memo_t *memo)
{
    char mpath[FLOMO_STORE_PATH_MAX + 6];
    meta_path(memo, mpath, sizeof(mpath));
    remove(mpath);
    remove(memo->path);
}
