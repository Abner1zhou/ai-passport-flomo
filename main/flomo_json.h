// main/flomo_json.h —— 只读、最小、无依赖的 JSON 扫描器(纯逻辑,主机可测)。
//
// 为什么不用 cJSON:设备端只做两件事——读配置页提交的扁平对象、读 ASR 响应里的
// 一个 "text" 字段。自带的严格扫描器约两百行,主机测试无需拖入 ESP-IDF 组件,
// 且输入面小、便于穷举边界(转义、截断、非法 UTF-8 控制字符)。
//
// 支持面:单个顶层对象;字符串/数字/布尔/null 值(非目标键的值只跳过,
// 含嵌套对象/数组,深度上限 8);字符串转义 \" \\ \/ \b \f \n \r \t \uXXXX。
// 不支持:注释、顶层数组、数字后再跟垃圾(视为解析失败)。
#pragma once

#include <stdbool.h>
#include <stddef.h>

// 在 json 文本的对象里查找 key 的字符串值并解码拷贝到 out(始终 NUL 结尾)。
// - 找到且完整放下:返回 true,*truncated(若非 NULL)为 false。
// - 找到但 out 装不下:拷贝前 out_size-1 字节,*truncated 为 true,仍返回 true
//   (调用方按字段语义决定截断是否可接受,如 webhook URL 截断即无效)。
// - 未找到 / json 结构非法 / 该键的值不是字符串:返回 false,out 写为空串。
// 对重复键:返回第一个匹配。
bool flomo_json_get_string(const char *json, const char *key,
                           char *out, size_t out_size, bool *truncated);

// 把 in 按 JSON 字符串字面量规则转义写入 out(" \ 控制字符;非 ASCII 原样透传),
// 始终 NUL 结尾。返回【不含 NUL】的完整转义长度;若返回值 >= out_size 则发生截断,
// 调用方应视为缓冲不足(可用返回值分配后重试)。out 可为 NULL:仅计算长度。
size_t flomo_json_escape(const char *in, char *out, size_t out_size);
// 整体结构是否为合法的顶层对象(用于配置提交前的快速校验)。
bool flomo_json_is_object(const char *json);
