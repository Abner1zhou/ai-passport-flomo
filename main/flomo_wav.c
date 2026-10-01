// main/flomo_wav.c —— 见 flomo_wav.h。
#include "flomo_wav.h"

#include <string.h>

static void put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

static void put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
}

static uint32_t byte_rate(const flomo_wav_fmt_t *fmt)
{
    return fmt->sample_rate * fmt->channels * (fmt->bits / 8);
}

void flomo_wav_header(uint8_t header[44], const flomo_wav_fmt_t *fmt, uint32_t data_bytes)
{
    memcpy(header, "RIFF", 4);
    put_u32(header + 4, 36 + data_bytes);          // 文件总长 - 8
    memcpy(header + 8, "WAVE", 4);
    memcpy(header + 12, "fmt ", 4);
    put_u32(header + 16, 16);                       // PCM fmt 块固定 16 字节
    put_u16(header + 20, 1);                        // PCM
    put_u16(header + 22, fmt->channels);
    put_u32(header + 24, fmt->sample_rate);
    put_u32(header + 28, byte_rate(fmt));
    put_u16(header + 32, (uint16_t)(fmt->channels * (fmt->bits / 8)));
    put_u16(header + 34, fmt->bits);
    memcpy(header + 36, "data", 4);
    put_u32(header + 40, data_bytes);
}

uint32_t flomo_wav_patch_sizes(uint8_t header[44], uint32_t data_bytes)
{
    put_u32(header + 4, 36 + data_bytes);
    put_u32(header + 40, data_bytes);
    return 44 + data_bytes;
}
