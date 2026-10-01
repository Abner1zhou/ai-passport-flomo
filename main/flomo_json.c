// main/flomo_json.c —— 见 flomo_json.h。
#include "flomo_json.h"

#include <string.h>

#define JSON_MAX_DEPTH 8

typedef struct {
    const char *p;      // 当前扫描位置
    int depth;          // skip_value 的嵌套深度保护
    bool failed;        // 置位后所有后续解析直接失败
} json_scan_t;

static void skip_ws(json_scan_t *s)
{
    while (*s->p == ' ' || *s->p == '\t' || *s->p == '\n' || *s->p == '\r') {
        s->p++;
    }
}

static bool consume(json_scan_t *s, char c)
{
    skip_ws(s);
    if (*s->p != c) {
        s->failed = true;
        return false;
    }
    s->p++;
    return true;
}

// 解析字符串字面量。capture 为 NULL 时仅跳过;否则解码写入 capture
// (flomo_json_get_string 通过两次调用完成"先跳 key 再取值",这里只服务值)。
static bool parse_string(json_scan_t *s, char *out, size_t out_size, bool *truncated)
{
    size_t used = 0;
    if (truncated) *truncated = false;
    if (!consume(s, '"')) return false;
    for (;;) {
        unsigned char c = (unsigned char)*s->p;
        if (c == '\0') {
            s->failed = true;
            return false;
        }
        if (c < 0x20) {
            // 规范要求未转义的控制字符非法,避免把换行当普通文本吞进配置。
            s->failed = true;
            return false;
        }
        if (c == '"') {
            s->p++;
            if (out) {
                out[used] = '\0';
            }
            return true;
        }
        unsigned int cp = c;    // UTF-8 原样透传(拷字节),仅 \uXXXX 单独处理
        if (c == '\\') {
            s->p++;
            char esc = *s->p;
            switch (esc) {
            case '"': case '\\': case '/': cp = (unsigned char)esc; s->p++; break;
            case 'b': cp = '\b'; s->p++; break;
            case 'f': cp = '\f'; s->p++; break;
            case 'n': cp = '\n'; s->p++; break;
            case 'r': cp = '\r'; s->p++; break;
            case 't': cp = '\t'; s->p++; break;
            case 'u': {
                s->p++;
                cp = 0;
                for (int i = 0; i < 4; i++) {
                    char h = s->p[i];
                    cp <<= 4;
                    if (h >= '0' && h <= '9') cp |= (unsigned)(h - '0');
                    else if (h >= 'a' && h <= 'f') cp |= (unsigned)(h - 'a' + 10);
                    else if (h >= 'A' && h <= 'F') cp |= (unsigned)(h - 'A' + 10);
                    else { s->failed = true; return false; }
                }
                s->p += 4;
                // 只处理 BMP 显式转义;代理对按原样输出两个 3 字节序列的场景
                // 不存在于本应用的受控输入,遇到代理区直接判非法,避免产出
                // 半个合法 UTF-8。
                if (cp >= 0xD800 && cp <= 0xDFFF) { s->failed = true; return false; }
                break;
            }
            default:
                s->failed = true;
                return false;
            }
        } else {
            s->p++;
        }
        if (out) {
            // \uXXXX 转 UTF-8(≤3 字节,BMP 非代理),其余按原字节宽度拷贝。
            char enc[3];
            unsigned len;
            if (c == '\\') {
                if (cp < 0x80) { enc[0] = (char)cp; len = 1; }
                else if (cp < 0x800) {
                    enc[0] = (char)(0xC0 | (cp >> 6));
                    enc[1] = (char)(0x80 | (cp & 0x3F));
                    len = 2;
                } else {
                    enc[0] = (char)(0xE0 | (cp >> 12));
                    enc[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
                    enc[2] = (char)(0x80 | (cp & 0x3F));
                    len = 3;
                }
            } else {
                enc[0] = (char)c;
                len = 1;
            }
            for (unsigned i = 0; i < len; i++) {
                if (used + 1 < out_size) {
                    out[used++] = enc[i];
                } else if (truncated) {
                    *truncated = true;
                }
            }
        }
    }
}

// 跳过任意类型的值(含嵌套),深度超限或结构非法置 failed。
static bool skip_value(json_scan_t *s)
{
    skip_ws(s);
    if (s->depth >= JSON_MAX_DEPTH) { s->failed = true; return false; }
    char c = *s->p;
    if (c == '"') {
        return parse_string(s, NULL, 0, NULL);
    }
    if (c == '{' || c == '[') {
        s->depth++;
        char open = c;
        char close = (c == '{') ? '}' : ']';
        s->p++;
        skip_ws(s);
        if (*s->p == close) {
            s->p++;
            s->depth--;
            return true;
        }
        for (;;) {
            if (open == '{') {
                if (!parse_string(s, NULL, 0, NULL)) return false;
                if (!consume(s, ':')) return false;
            }
            if (!skip_value(s)) return false;
            skip_ws(s);
            if (*s->p == ',') {
                s->p++;
            } else if (*s->p == close) {
                s->p++;
                s->depth--;
                return true;
            } else {
                s->failed = true;
                return false;
            }
        }
    }
    if (c == 't') {
        if (strncmp(s->p, "true", 4) == 0) { s->p += 4; return true; }
    } else if (c == 'f') {
        if (strncmp(s->p, "false", 5) == 0) { s->p += 5; return true; }
    } else if (c == 'n') {
        if (strncmp(s->p, "null", 4) == 0) { s->p += 4; return true; }
    } else if (c == '-' || (c >= '0' && c <= '9')) {
        const char *start = s->p;
        if (*s->p == '-') s->p++;
        while (*s->p >= '0' && *s->p <= '9') s->p++;
        if (*s->p == '.') {
            s->p++;
            while (*s->p >= '0' && *s->p <= '9') s->p++;
        }
        if (*s->p == 'e' || *s->p == 'E') {
            s->p++;
            if (*s->p == '+' || *s->p == '-') s->p++;
            while (*s->p >= '0' && *s->p <= '9') s->p++;
        }
        if (s->p != start) return true;
    }
    s->failed = true;
    return false;
}

static bool parse_object(json_scan_t *s,
                         const char *want_key,
                         char *out, size_t out_size, bool *truncated)
{
    if (!consume(s, '{')) return false;
    skip_ws(s);
    if (*s->p == '}') {
        s->p++;
        return false;      // 空对象:结构合法但没有目标键
    }
    bool captured = false;
    for (;;) {
        char key[64];
        bool key_trunc = false;
        if (!parse_string(s, key, sizeof(key), &key_trunc)) return false;
        if (!consume(s, ':')) return false;
        skip_ws(s);
        bool key_match = !captured && !key_trunc && strcmp(key, want_key) == 0;
        if (key_match) {
            // 目标键的值不是字符串:立即失败,与头文件契约一致。
            if (*s->p != '"') {
                s->failed = true;
                return false;
            }
            // 捕获后继续把对象走完,确保闭括号被消费、尾随内容能被上层发现。
            if (!parse_string(s, out, out_size, truncated)) return false;
            captured = true;
        } else {
            if (!skip_value(s)) return false;
        }
        skip_ws(s);
        if (*s->p == ',') {
            s->p++;
            skip_ws(s);
            continue;
        }
        if (*s->p == '}') {
            s->p++;
            return captured;      // 首个匹配生效,后置重复键只被跳过
        }
        s->failed = true;
        return false;
    }
}

bool flomo_json_get_string(const char *json, const char *key,
                           char *out, size_t out_size, bool *truncated)
{
    if (out && out_size > 0) out[0] = '\0';
    if (!json || !key || !out || out_size == 0) return false;
    json_scan_t s = { .p = json, .depth = 0, .failed = false };
    bool found = parse_object(&s, key, out, out_size, truncated);
    skip_ws(&s);
    if (s.failed || *s.p != '\0') {
        // 结构失败,或对象结束后还有非空白内容:整体非法。
        out[0] = '\0';
        if (truncated) *truncated = false;
        return false;
    }
    return found;
}


size_t flomo_json_escape(const char *in, char *out, size_t out_size)
{
    static const char HEX[] = "0123456789abcdef";
    size_t used = 0;       // 完整转义长度(供调用方判断截断/分配)
    size_t written = 0;    // 实际写入字节数(截断时用于 NUL 收尾)
    bool fitting = true;   // 首个放不下的块之后停止写入,避免内容乱序
    if (!in) return 0;
    for (const unsigned char *p = (const unsigned char *)in; *p; p++) {
        char piece[7];
        size_t len;
        switch (*p) {
        case '"':  piece[0] = '\\'; piece[1] = '"'; len = 2; break;
        case '\\': piece[0] = '\\'; piece[1] = '\\'; len = 2; break;
        case '\b': piece[0] = '\\'; piece[1] = 'b'; len = 2; break;
        case '\f': piece[0] = '\\'; piece[1] = 'f'; len = 2; break;
        case '\n': piece[0] = '\\'; piece[1] = 'n'; len = 2; break;
        case '\r': piece[0] = '\\'; piece[1] = 'r'; len = 2; break;
        case '\t': piece[0] = '\\'; piece[1] = 't'; len = 2; break;
        default:
            if (*p < 0x20) {
                // 其余控制字符按 \u00XX 输出,保证 JSON 合法。
                piece[0] = '\\'; piece[1] = 'u'; piece[2] = '0'; piece[3] = '0';
                piece[4] = (char)HEX[*p >> 4];
                piece[5] = (char)HEX[*p & 0xF];
                len = 6;
            } else {
                piece[0] = (char)*p;
                len = 1;
            }
            break;
        }
        if (out && fitting && written + len < out_size) {
            memcpy(out + written, piece, len);
            written += len;
        } else {
            fitting = false;
        }
        used += len;
    }
    if (out && out_size > 0) {
        out[written < out_size ? written : out_size - 1] = '\0';
    }
    return used;
}

bool flomo_json_is_object(const char *json)
{
    if (!json) return false;
    json_scan_t s = { .p = json, .depth = 0, .failed = false };
    if (!consume(&s, '{')) return false;
    skip_ws(&s);
    if (*s.p == '}') {
        s.p++;
    } else {
        // 完整走一遍对象结构,任何非法都置 failed。
        char key[8];
        for (;;) {
            bool trunc = false;
            if (!parse_string(&s, key, sizeof(key), &trunc)) break;
            if (!consume(&s, ':')) break;
            if (!skip_value(&s)) break;
            skip_ws(&s);
            if (*s.p == ',') {
                s.p++;
                skip_ws(&s);
                continue;
            }
            if (*s.p == '}') {
                s.p++;
                break;
            }
            s.failed = true;
            break;
        }
    }
    if (s.failed) return false;
    skip_ws(&s);
    return *s.p == '\0';    // 对象后只允许空白
}
