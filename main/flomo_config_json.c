// main/flomo_config_json.c —— flomo_config_parse_json 的纯逻辑实现,见 flomo_config.h。
#include "flomo_config.h"

#include "flomo_json.h"

#include <string.h>

void flomo_config_defaults(flomo_config_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    strcpy(cfg->asr_url, "https://api.groq.com/openai/v1/audio/transcriptions");
    strcpy(cfg->asr_model, "whisper-large-v3");
    strcpy(cfg->language, "zh");
}

bool flomo_config_ready(const flomo_config_t *cfg)
{
    return cfg->webhook[0] != '\0' && cfg->asr_key[0] != '\0';
}

// 读一个字符串字段到临时缓冲,统一处理"未提供/截断/非字符串"三种情况。
// 返回:false = 键存在但值非法(截断或非字符串);true = 键不存在或读取成功。
static bool read_field(const char *json, const char *key, char *dst, size_t dst_size)
{
    char tmp[256];
    bool truncated = false;
    bool found = flomo_json_get_string(json, key, tmp, sizeof(tmp), &truncated);
    if (!found) return true;          // 键不存在:保持原值
    // 值超出【目标字段】宽度即整体拒绝:半截 URL 或 key 保存下去必然失效。
    if (truncated || strlen(tmp) >= dst_size) return false;
    strcpy(dst, tmp);
    return true;
}

static bool valid_https(const char *url)
{
    // 设备端只做 TLS;http 明文会把 webhook token 暴露在局域网,不接受。
    return strncmp(url, "https://", 8) == 0;
}

flomo_config_parse_err_t flomo_config_parse_json(flomo_config_t *cfg, const char *json)
{
    if (!flomo_json_is_object(json)) return FLOMO_CFG_PARSE_BAD_JSON;

    flomo_config_t next = *cfg;
    if (!read_field(json, "webhook", next.webhook, sizeof(next.webhook)) ||
        !read_field(json, "asr_url", next.asr_url, sizeof(next.asr_url)) ||
        !read_field(json, "asr_key", next.asr_key, sizeof(next.asr_key)) ||
        !read_field(json, "asr_model", next.asr_model, sizeof(next.asr_model)) ||
        !read_field(json, "language", next.language, sizeof(next.language))) {
        return FLOMO_CFG_PARSE_BAD_VALUE;
    }
    if ((next.webhook[0] != '\0' && !valid_https(next.webhook)) ||
        (next.asr_url[0] != '\0' && !valid_https(next.asr_url))) {
        return FLOMO_CFG_PARSE_BAD_VALUE;
    }
    *cfg = next;
    return FLOMO_CFG_PARSE_OK;
}
