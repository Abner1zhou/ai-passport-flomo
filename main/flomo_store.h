// main/flomo_store.h —— littlefs 备忘录队列存储:文件即队列项。
// 布局:/flomo/queue/NNNNNN.wav + NNNNNN.meta(ASCII:尝试次数)。
// NNNNNN 为 6 位十进制序号,单调递增;最老 = 序号最小 = 队首。
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define FLOMO_STORE_MOUNT   "/flomo"
#define FLOMO_STORE_QUEUE   "/flomo/queue"
#define FLOMO_STORE_PATH_MAX 40

typedef struct {
    char path[FLOMO_STORE_PATH_MAX];   // WAV 完整路径
    uint32_t data_bytes;               // PCM 字节数(读 WAV 头)
    uint8_t attempts;                  // 已尝试上传次数(读 .meta,缺省 0)
} flomo_memo_t;

// 挂载 littlefs(分区 label "storage");损坏则格式化一次。幂等。
esp_err_t flomo_store_init(void);

// 队列长度(目录内 .wav 计数)。失败返回 0。
size_t flomo_store_count(void);

// 取最老一条;队列空返回 false。
bool flomo_store_peek_oldest(flomo_memo_t *out);

// 为新录音分配路径(序号 = 当前最大 +1)。不创建文件,由录音者 fopen。
esp_err_t flomo_store_alloc(flomo_memo_t *out);

// 更新尝试次数(写 .meta)。
esp_err_t flomo_store_set_attempts(const flomo_memo_t *memo, uint8_t attempts);

// 删除 WAV 与 .meta。
void flomo_store_remove(const flomo_memo_t *memo);
