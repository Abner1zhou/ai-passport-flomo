// main/flomo_wav.h —— PCM WAV(RIFF)头封装,纯逻辑、无 ESP-IDF 依赖,便于主机测试。
// 设备与主流 ASR(OpenAI/Groq 兼容接口)均为小端,WAV 规范亦为小端,直接按字节写。
#pragma once

#include <stdint.h>

typedef struct {
    uint32_t sample_rate;  // Hz,如 16000
    uint16_t channels;     // 声道数,录音固定 1
    uint16_t bits;         // 位深,录音固定 16
} flomo_wav_fmt_t;

// 生成 44 字节标准 PCM WAV 头。data_bytes 为 PCM 数据字节数(录音开始时未知则先传 0)。
// 字节序、字段偏移遵循 RIFF/PCM 规范;不写 LIST 等可选块。
void flomo_wav_header(uint8_t header[44], const flomo_wav_fmt_t *fmt, uint32_t data_bytes);

// 录音结束后回写 RIFF 总长与 data 长度。返回整文件字节数(44 + data_bytes)。
uint32_t flomo_wav_patch_sizes(uint8_t header[44], uint32_t data_bytes);
