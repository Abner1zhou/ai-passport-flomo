// tests/test_flomo_wav.c —— WAV 头字节级校验:字段偏移/小端/尺寸回写。
#include <assert.h>
#include <string.h>

#include "flomo_wav.h"

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint32_t le16(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8);
}

int main(void)
{
    uint8_t h[44];
    flomo_wav_fmt_t fmt = { .sample_rate = 16000, .channels = 1, .bits = 16 };

    // 录音开始:数据长度未知,先写 0。
    flomo_wav_header(h, &fmt, 0);
    assert(memcmp(h, "RIFF", 4) == 0);
    assert(memcmp(h + 8, "WAVE", 4) == 0);
    assert(memcmp(h + 12, "fmt ", 4) == 0);
    assert(memcmp(h + 36, "data", 4) == 0);
    assert(le32(h + 4) == 36);
    assert(le32(h + 16) == 16);            // PCM fmt 块长
    assert(le16(h + 20) == 1);             // PCM
    assert(le16(h + 22) == 1);             // 单声道
    assert(le32(h + 24) == 16000);
    assert(le32(h + 28) == 32000);         // byte rate = 16000*1*2
    assert(le16(h + 32) == 2);             // block align
    assert(le16(h + 34) == 16);
    assert(le32(h + 40) == 0);

    // 录音结束:60 秒 = 16000*60 采样 *2 字节。
    uint32_t data = 16000u * 60u * 2u;
    uint32_t total = flomo_wav_patch_sizes(h, data);
    assert(total == 44 + data);
    assert(le32(h + 4) == 36 + data);
    assert(le32(h + 40) == data);

    // 其他格式参数也正确换算(8kHz 用于回归)。
    flomo_wav_fmt_t fmt8k = { .sample_rate = 8000, .channels = 1, .bits = 16 };
    flomo_wav_header(h, &fmt8k, 100);
    assert(le32(h + 24) == 8000);
    assert(le32(h + 28) == 16000);
    return 0;
}
