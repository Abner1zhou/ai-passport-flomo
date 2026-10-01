// tests/test_flomo_json.c —— 最小 JSON 扫描器 + 配置解析:转义/截断/非法输入/部分更新。
#include <assert.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#include "flomo_config.h"
#include "flomo_json.h"

static void test_get_string_basics(void)
{
    char out[64];
    bool trunc = false;

    assert(flomo_json_get_string("{\"text\":\"hello\"}", "text", out, sizeof(out), &trunc));
    assert(strcmp(out, "hello") == 0 && !trunc);

    // 缺席键。
    assert(!flomo_json_get_string("{\"a\":\"b\"}", "text", out, sizeof(out), &trunc));
    assert(out[0] == '\0');

    // 目标键值不是字符串 → false,且不接受后置重复键补救。
    assert(!flomo_json_get_string("{\"text\":123,\"dup\":\"x\"}", "text",
                                   out, sizeof(out), &trunc));
    assert(!flomo_json_get_string("{\"text\":null}", "text", out, sizeof(out), &trunc));

    // 重复键取第一个。
    assert(flomo_json_get_string("{\"k\":\"1\",\"k\":\"2\"}", "k", out, sizeof(out), &trunc));
    assert(strcmp(out, "1") == 0);

    // 非对象顶层 / 截断 JSON / 尾随垃圾。
    assert(!flomo_json_get_string("[1,2]", "text", out, sizeof(out), &trunc));
    assert(!flomo_json_get_string("{\"text\":\"o", "text", out, sizeof(out), &trunc));
    assert(!flomo_json_get_string("{\"a\":\"b\"}x", "a", out, sizeof(out), &trunc));
}

static void test_get_string_escapes(void)
{
    char out[64];
    bool trunc = false;

    assert(flomo_json_get_string(
        "{\"t\":\"a\\\"b\\\\c\\nd\\te\"}", "t", out, sizeof(out), &trunc));
    assert(strcmp(out, "a\"b\\c\nd\te") == 0);

    // \uXXXX → UTF-8(中文"中"= U+4E2D → E4 B8 AD)。
    assert(flomo_json_get_string("{\"t\":\"\\u4e2d\"}", "t", out, sizeof(out), &trunc));
    assert((unsigned char)out[0] == 0xE4 && (unsigned char)out[1] == 0xB8 &&
           (unsigned char)out[2] == 0xAD && out[3] == '\0');

    // 代理区转义拒绝;裸控制字符拒绝;非法转义拒绝。
    assert(!flomo_json_get_string("{\"t\":\"\\ud800\"}", "t", out, sizeof(out), &trunc));
    assert(!flomo_json_get_string("{\"t\":\"a\nb\"}", "t", out, sizeof(out), &trunc));
    assert(!flomo_json_get_string("{\"t\":\"\\x41\"}", "t", out, sizeof(out), &trunc));

    // 未转义的 UTF-8 原样透传。
    assert(flomo_json_get_string("{\"t\":\"中文\"}", "t", out, sizeof(out), &trunc));
    assert(strcmp(out, "中文") == 0);
}

static void test_get_string_truncation(void)
{
    char small[8];
    bool trunc = false;
    assert(flomo_json_get_string("{\"t\":\"abcdefghijkl\"}", "t", small, sizeof(small), &trunc));
    assert(trunc);
    assert(strlen(small) == sizeof(small) - 1);   // NUL 安全
}

static void test_is_object(void)
{
    assert(flomo_json_is_object("{}"));
    assert(flomo_json_is_object("  {\"a\":1,\"b\":[1,{\"c\":null}],\"d\":true}  "));
    assert(!flomo_json_is_object("[]"));
    assert(!flomo_json_is_object("{\"a\":}"));
    assert(!flomo_json_is_object("{\"a\":1,}"));   // 尾随逗号非法
    assert(!flomo_json_is_object("{\"a\":1} {\"b\":2}"));
    assert(!flomo_json_is_object(NULL));
    // 深度保护:8 层以内可跳过,9 层拒绝。
    assert(flomo_json_is_object("{\"a\":{\"b\":{\"c\":{\"d\":{\"e\":{\"f\":{\"g\":1}}}}}}}"));
    assert(!flomo_json_is_object(
        "{\"a\":[[[[[[[[[1]]]]]]]]]}"));
}

static void test_config_parse(void)
{
    flomo_config_t cfg;
    flomo_config_defaults(&cfg);

    assert(strcmp(cfg.asr_model, "whisper-large-v3") == 0);
    assert(!flomo_config_ready(&cfg));

    // 非法输入不改 cfg。
    flomo_config_t before = cfg;
    assert(flomo_config_parse_json(&cfg, "not json") == FLOMO_CFG_PARSE_BAD_JSON);
    assert(flomo_config_parse_json(&cfg, "{\"webhook\":\"http://x\"}") ==
           FLOMO_CFG_PARSE_BAD_VALUE);
    assert(memcmp(&before, &cfg, sizeof(cfg)) == 0);

    // 部分更新:只给 webhook,其余保持默认。
    assert(flomo_config_parse_json(&cfg,
           "{\"webhook\":\"https://flomoapp.com/iwh/abc123\"}") == FLOMO_CFG_PARSE_OK);
    assert(strcmp(cfg.webhook, "https://flomoapp.com/iwh/abc123") == 0);
    assert(strcmp(cfg.asr_model, "whisper-large-v3") == 0);
    assert(!flomo_config_ready(&cfg));            // 还差 asr_key

    // 补上 key 后 ready;未知键被忽略。
    assert(flomo_config_parse_json(&cfg,
           "{\"asr_key\":\"gsk_x\",\"future_field\":{\"nested\":[1,2]}}") ==
           FLOMO_CFG_PARSE_OK);
    assert(flomo_config_ready(&cfg));
    assert(strcmp(cfg.asr_key, "gsk_x") == 0);

    // 值超长(>159)整体拒绝且不落盘。
    char big[256];
    memset(big, 'a', sizeof(big) - 1);
    big[sizeof(big) - 1] = '\0';
    char json[600];
    snprintf(json, sizeof(json), "{\"asr_key\":\"%s\"}", big);
    before = cfg;
    assert(flomo_config_parse_json(&cfg, json) == FLOMO_CFG_PARSE_BAD_VALUE);
    assert(memcmp(&before, &cfg, sizeof(cfg)) == 0);

    // 空字符串:显式清空字段(例:换号时清 webhook)。
    assert(flomo_config_parse_json(&cfg, "{\"webhook\":\"\"}") == FLOMO_CFG_PARSE_OK);
    assert(cfg.webhook[0] == '\0');
    assert(!flomo_config_ready(&cfg));
}

static void test_escape(void)
{
    char out[64];
    // 常规转义与中文透传。
    assert(flomo_json_escape("a\"b\\c\nd", out, sizeof(out)) == 10);
    assert(strcmp(out, "a\\\"b\\\\c\\nd") == 0);
    assert(flomo_json_escape("中文", out, sizeof(out)) == 6);
    assert(strcmp(out, "中文") == 0);
    // 控制字符(无短转义)走 \u00XX。
    assert(flomo_json_escape("\x01", out, sizeof(out)) == 6);
    assert(strcmp(out, "\\u0001") == 0);
    // 长度计算(NULL out)与截断判定。
    assert(flomo_json_escape("ab\"cd", NULL, 0) == 6);
    assert(flomo_json_escape("ab\"cd", out, 4) == 6);      // 截断发生
    assert(out[2] == '\0' && strncmp(out, "ab", 2) == 0);
    // 空串与 NULL。
    assert(flomo_json_escape("", out, sizeof(out)) == 0 && out[0] == '\0');
    assert(flomo_json_escape(NULL, out, sizeof(out)) == 0);
}

int main(void)
{
    test_get_string_basics();
    test_get_string_escapes();
    test_get_string_truncation();
    test_is_object();
    test_config_parse();
    test_escape();
    return 0;
}
