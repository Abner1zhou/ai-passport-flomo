// main/flomo_config.h —— flomo 客户端运行配置:结构、默认值、JSON 解析。
// 纯逻辑部分(flomo_config.h + flomo_config_json.c)不依赖 ESP-IDF,可在主机测试;
// NVS 存取在 flomo_config.c(设备端)。
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

// 字段上限。webhook/ASR URL 走 HTTPS,128 字节覆盖常规长度;超长输入按截断处理,
// 由调用方校验语义(见 flomo_config_json.c 注释)。
#define FLOMO_CFG_WEBHOOK_MAX  160
#define FLOMO_CFG_ASR_URL_MAX  160
#define FLOMO_CFG_ASR_KEY_MAX  160
#define FLOMO_CFG_ASR_MODEL_MAX 48
#define FLOMO_CFG_LANG_MAX     8

typedef struct {
    char webhook[FLOMO_CFG_WEBHOOK_MAX];      // 完整 URL:https://flomoapp.com/iwh/<token>
    char asr_url[FLOMO_CFG_ASR_URL_MAX];      // OpenAI 兼容 /audio/transcriptions 端点
    char asr_key[FLOMO_CFG_ASR_KEY_MAX];      // Bearer key,仅存设备 NVS,永不写日志
    char asr_model[FLOMO_CFG_ASR_MODEL_MAX];  // 如 whisper-large-v3
    char language[FLOMO_CFG_LANG_MAX];        // ASR 语言提示,如 zh;空=自动检测
} flomo_config_t;

// 填入出厂默认(ASR 指向 Groq 的 OpenAI 兼容端点,whatsapp/key 留空待配)。
void flomo_config_defaults(flomo_config_t *cfg);

// 配置是否具备上传条件:webhook 与 asr_key 非空。asr_url/model/language 允许默认。
bool flomo_config_ready(const flomo_config_t *cfg);

typedef enum {
    FLOMO_CFG_PARSE_OK = 0,
    FLOMO_CFG_PARSE_BAD_JSON,      // 整体不是合法对象
    FLOMO_CFG_PARSE_BAD_VALUE,     // 字段值非法(见下)或目标键非字符串
} flomo_config_parse_err_t;

// 解析配置页提交的 JSON 对象;只覆盖出现的字段,缺席字段保持 *cfg 原值
// (部分更新语义:表单可以只改 webhook)。额外未知键忽略,便于前端向后兼容。
// 校验:webhook/asr_url 必须以 https:// 开头且未被截断;截断或非法返回 BAD_VALUE
// 且 *cfg 不被修改。key/model/language 仅要求未截断。

// ---- NVS 存取(设备端,flomo_config.c)----
// load:默认值 + 已存字段覆盖;从未配置时返回全默认。始终尽量成功。
esp_err_t flomo_config_load(flomo_config_t *cfg);
esp_err_t flomo_config_save(const flomo_config_t *cfg);
esp_err_t flomo_config_clear(void);

flomo_config_parse_err_t flomo_config_parse_json(flomo_config_t *cfg, const char *json);
