// main/flomo_queue_policy.c —— 见 flomo_queue_policy.h。
#include "flomo_queue_policy.h"

flomo_qerr_t flomo_qerr_from_http(int32_t status)
{
    if (status < 0) return FLOMO_QERR_NETWORK;
    if (status == 429) return FLOMO_QERR_HTTP_429;
    if (status >= 500) return FLOMO_QERR_HTTP_5XX;
    if (status >= 400) return FLOMO_QERR_HTTP_4XX;
    // 2xx/3xx 不是失败,调用方不应把成功状态传进来;按 4XX 对待以免死循环。
    return FLOMO_QERR_HTTP_4XX;
}

flomo_qact_t flomo_queue_on_failure(flomo_qerr_t err, uint8_t attempts)
{
    if (err == FLOMO_QERR_HTTP_4XX) return FLOMO_QACT_DROP;
    if (attempts >= FLOMO_QUEUE_MAX_ATTEMPTS) return FLOMO_QACT_DROP;
    return FLOMO_QACT_RETRY_LATER;
}

uint32_t flomo_queue_backoff_ms(flomo_qerr_t err, uint8_t attempts)
{
    // attempts 为含本次的次数;首次失败(attempts=1)后等 base,逐次翻倍。
    const uint32_t base = (err == FLOMO_QERR_HTTP_429) ? 60u * 1000u : 15u * 1000u;
    const uint32_t cap = (err == FLOMO_QERR_HTTP_429) ? 600u * 1000u : 120u * 1000u;
    if (attempts == 0) attempts = 1;
    uint32_t wait = base;
    for (uint8_t i = 1; i < attempts; i++) {
        if (wait >= cap) break;          // 防止溢出:到顶后不再翻倍
        wait *= 2;
    }
    return wait > cap ? cap : wait;
}
