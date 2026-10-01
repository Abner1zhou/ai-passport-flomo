// tests/test_flomo_queue_policy.c —— 失败分类、重试/放弃决策、退避曲线与封顶。
#include <assert.h>

#include "flomo_queue_policy.h"

int main(void)
{
    // 状态码分类。
    assert(flomo_qerr_from_http(-1) == FLOMO_QERR_NETWORK);
    assert(flomo_qerr_from_http(429) == FLOMO_QERR_HTTP_429);
    assert(flomo_qerr_from_http(500) == FLOMO_QERR_HTTP_5XX);
    assert(flomo_qerr_from_http(503) == FLOMO_QERR_HTTP_5XX);
    assert(flomo_qerr_from_http(401) == FLOMO_QERR_HTTP_4XX);
    assert(flomo_qerr_from_http(404) == FLOMO_QERR_HTTP_4XX);
    assert(flomo_qerr_from_http(200) == FLOMO_QERR_HTTP_4XX);   // 成功码传入按防呆处理

    // 4XX 立即放弃(token 失效重试无意义)。
    assert(flomo_queue_on_failure(FLOMO_QERR_HTTP_4XX, 1) == FLOMO_QACT_DROP);

    // 可重试错误:1..4 次保留,第 5 次放弃。
    for (uint8_t a = 1; a < FLOMO_QUEUE_MAX_ATTEMPTS; a++) {
        assert(flomo_queue_on_failure(FLOMO_QERR_NETWORK, a) == FLOMO_QACT_RETRY_LATER);
        assert(flomo_queue_on_failure(FLOMO_QERR_HTTP_5XX, a) == FLOMO_QACT_RETRY_LATER);
        assert(flomo_queue_on_failure(FLOMO_QERR_HTTP_429, a) == FLOMO_QACT_RETRY_LATER);
        assert(flomo_queue_on_failure(FLOMO_QERR_PARSE, a) == FLOMO_QACT_RETRY_LATER);
    }
    assert(flomo_queue_on_failure(FLOMO_QERR_NETWORK, FLOMO_QUEUE_MAX_ATTEMPTS) ==
           FLOMO_QACT_DROP);
    assert(flomo_queue_on_failure(FLOMO_QERR_HTTP_429, FLOMO_QUEUE_MAX_ATTEMPTS) ==
           FLOMO_QACT_DROP);

    // 退避:15s/30s/60s/120s/120s...;429 走 60s/120s/240s/480s/600s 封顶。
    assert(flomo_queue_backoff_ms(FLOMO_QERR_NETWORK, 1) == 15000u);
    assert(flomo_queue_backoff_ms(FLOMO_QERR_NETWORK, 2) == 30000u);
    assert(flomo_queue_backoff_ms(FLOMO_QERR_NETWORK, 3) == 60000u);
    assert(flomo_queue_backoff_ms(FLOMO_QERR_NETWORK, 4) == 120000u);
    assert(flomo_queue_backoff_ms(FLOMO_QERR_NETWORK, 5) == 120000u);
    assert(flomo_queue_backoff_ms(FLOMO_QERR_NETWORK, 200) == 120000u);   // 不溢出
    assert(flomo_queue_backoff_ms(FLOMO_QERR_HTTP_429, 1) == 60000u);
    assert(flomo_queue_backoff_ms(FLOMO_QERR_HTTP_429, 3) == 240000u);
    assert(flomo_queue_backoff_ms(FLOMO_QERR_HTTP_429, 5) == 600000u);
    assert(flomo_queue_backoff_ms(FLOMO_QERR_HTTP_429, 250) == 600000u);  // 大次数不溢出
    return 0;
}
