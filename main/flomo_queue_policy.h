// main/flomo_queue_policy.h —— 离线备忘录队列的重试决策,纯逻辑、主机可测。
// 队列本体(小 FS 文件)在设备端;这里只回答"这次失败了怎么办"。
#pragma once

#include <stdbool.h>
#include <stdint.h>

// 一次上传尝试的失败分类。分类决定重试策略,与具体 HTTP 库解耦。
typedef enum {
    FLOMO_QERR_NETWORK = 0,   // 连接/超时/TLS:环境问题,可重试
    FLOMO_QERR_HTTP_429,      // 限流:重试但退避拉长(flomo webhook 有未公开的限额)
    FLOMO_QERR_HTTP_5XX,      // 服务端错误:可重试
    FLOMO_QERR_HTTP_4XX,      // 客户端错误(token 失效/请求构造错):重试无意义
    FLOMO_QERR_PARSE,         // 响应非预期(如 ASR 返回结构变了):重试大概率同样失败
} flomo_qerr_t;

// HTTP 状态码 → 失败分类。status<0 表示传输层失败(网络)。
flomo_qerr_t flomo_qerr_from_http(int32_t status);

#define FLOMO_QUEUE_MAX_ATTEMPTS 5

typedef enum {
    FLOMO_QACT_DROP = 0,       // 放弃该条(4XX/超次数):删除文件,UI 计数
    FLOMO_QACT_RETRY_LATER,    // 保留,按退避等待后重试
} flomo_qact_t;

// 失败后的决策。attempts 为【含本次】的尝试次数(1 起)。
// - 4XX:立即 DROP(重试同样的请求只会再失败一次)。
// - 429/5XX/网络/PARSE:attempts 达到 FLOMO_QUEUE_MAX_ATTEMPTS 后 DROP。
flomo_qact_t flomo_queue_on_failure(flomo_qerr_t err, uint8_t attempts);

// 下次重试前等待的毫秒数(相对本次失败时刻)。
// 网络/5XX/PARSE:15s 起、×2 封顶 120s;429:60s 起、×2 封顶 600s(限流更需要耐心)。
uint32_t flomo_queue_backoff_ms(flomo_qerr_t err, uint8_t attempts);
