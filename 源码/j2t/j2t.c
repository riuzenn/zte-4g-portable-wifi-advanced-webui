/*
 * j2t.c - 流式版 JSON -> 对齐纯文本
 *   - 逐字节读入, 内存 ≈ 行缓冲
 *   {"a":1,"b":{"c":[2,3]}} 变成:
 *   a: 1
 *   b:
 *     c:
 *       - 2
 *       - 3
 * 编译:
 *   arm-buildroot-linux-uclibcgnueabi-gcc -std=gnu99 -Os -s -static j2t.c -o j2t
 * 用法:
 *   curl -s http://x/api | ./j2t
 *   ./j2t data.json
 */
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#define MAX_LINE   80
#define LINE_CAP   128
#define MAX_DEPTH  24

static char line[LINE_CAP];
static int  line_len = 0;
static int  truncated = 0;

static FILE *in_fp = NULL;

/* ---------- 单字节输入 ---------- */
#define RD_PB 8

static int    rd_pb[RD_PB];
static int    rd_pb_n = 0;
static size_t rd_pos = 0;

static int rd_get(void) {
    int c;
    if (rd_pb_n > 0)
        return rd_pb[--rd_pb_n];
    c = fgetc(in_fp);
    if (c != EOF)
        rd_pos++;
    return c;
}

static int rd_peek(void) {
    int c = rd_get();
    if (c != EOF && rd_pb_n < RD_PB)
        rd_pb[rd_pb_n++] = c;
    return c;
}

static void rd_unget(int c) {
    if (c != EOF && rd_pb_n < RD_PB)
        rd_pb[rd_pb_n++] = c;
}

/* ---------- 输出行缓冲 ---------- */
static void append_byte_nc(unsigned char c) {
    if (line_len + 1 >= LINE_CAP) {
        truncated = 1;
        return;
    }
    line[line_len++] = (char)c;
    line[line_len] = '\0';
}

static void wrap_line(void) {
    int i, sp = -1, cut, j, rest;
    for (i = 0; i < line_len; i++)
        if (line[i] == ' ') sp = i;
    if (sp < 0) {
        fwrite(line, 1, line_len, stdout);
        fputc('\n', stdout);
        line_len = 0;
        line[0] = '\0';
        return;
    }
    cut = sp;
    while (cut > 0 && line[cut - 1] == ' ') cut--;
    fwrite(line, 1, cut, stdout);
    fputc('\n', stdout);
    j = sp + 1;
    while (j < line_len && line[j] == ' ') j++;
    rest = line_len - j;
    memmove(line, line + j, rest);
    line_len = rest;
    line[line_len] = '\0';
}

static void append_byte(unsigned char c) {
    if (line_len >= MAX_LINE)
        wrap_line();
    append_byte_nc(c);
}

static int utf8_char_len(unsigned char c) {
    if (c >= 0xF0 && c <= 0xF4) return 4;
    if (c >= 0xE0 && c <= 0xEF) return 3;
    if (c >= 0xC2 && c <= 0xDF) return 2;
    return 1;
}

static void append_str(const char *s) {
    unsigned char c;
    int n, i;
    while (*s) {
        c = (unsigned char)*s;
        if (c < 0x80) {
            append_byte(c);
            s++;
        } else {
            n = utf8_char_len(c);
            if (line_len + n > MAX_LINE)
                wrap_line();
            for (i = 0; i < n && *s; i++)
                append_byte_nc((unsigned char)*s++);
        }
    }
}

static void append_space(void) {
    if (line_len > 0 && line[line_len - 1] != ' ')
        append_byte(' ');
}

static void flush_line(void) {
    while (line_len > 0 && line[line_len - 1] == ' ')
        line_len--;
    if (line_len <= 0) return;
    fwrite(line, 1, line_len, stdout);
    fputc('\n', stdout);
    line_len = 0;
    line[0] = '\0';
}

static void append_utf8(unsigned int cp) {
    unsigned char tmp[4];
    int n = 0, i;

    if (cp < 0x80) {
        tmp[n++] = (unsigned char)cp;
    } else if (cp < 0x800) {
        tmp[n++] = (unsigned char)(0xC0 | (cp >> 6));
        tmp[n++] = (unsigned char)(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        tmp[n++] = (unsigned char)(0xE0 | (cp >> 12));
        tmp[n++] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
        tmp[n++] = (unsigned char)(0x80 | (cp & 0x3F));
    } else {
        tmp[n++] = (unsigned char)(0xF0 | (cp >> 18));
        tmp[n++] = (unsigned char)(0x80 | ((cp >> 12) & 0x3F));
        tmp[n++] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
        tmp[n++] = (unsigned char)(0x80 | (cp & 0x3F));
    }
    if (line_len + n > MAX_LINE)
        wrap_line();
    for (i = 0; i < n; i++)
        append_byte_nc(tmp[i]);
}

static void output_indent(int depth) {
    int n, i;
    if (line_len > 0)
        flush_line();
    n = depth * 2;
    if (n > MAX_LINE / 2)
        n = MAX_LINE / 2;
    for (i = 0; i < n; i++)
        append_byte_nc(' ');
}

/* ---------- JSON 词法 ---------- */
static int hex_digit(unsigned char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int is_num_char(unsigned char c) {
    return (c >= '0' && c <= '9') || c == '-' || c == '+' ||
           c == '.' || c == 'e' || c == 'E';
}

static void skip_ws(void) {
    int c;
    while ((c = rd_peek()) != EOF && isspace((unsigned char)c))
        rd_get();
}

static int match_lit(const char *s) {
    size_t n = strlen(s), i;
    int c;
    for (i = 0; i < n; i++) {
        c = rd_peek();
        if (c == EOF || (unsigned char)c != (unsigned char)s[i])
            return 0;
        rd_get();
    }
    c = rd_peek();
    if (c != EOF && isalnum((unsigned char)c))
        return 0;                    /* 词边界: true/false/null 后不能接字母数字 */
    return 1;
}

/* 读取 \uXXXX; 调用时已读到 'u' */
static int parse_unicode_escape(void) {
    unsigned v = 0, lo;
    int i, c, d;
    int hex[4];

    for (i = 0; i < 4; i++) {
        c = rd_get();
        if (c == EOF) return -1;
        d = hex_digit((unsigned char)c);
        if (d < 0) return -1;
        hex[i] = c;
        v = v * 16 + d;
    }

    if (v >= 0xD800 && v <= 0xDBFF) {
        c = rd_peek();
        if (c != '\\')
            return 0;                      /* 未配对高代理: 丢弃 */

        rd_get();                          /* 吃掉 '\' */
        c = rd_peek();                     /* 再看下个字节, 不消费 */
        if (c != 'u') {
            rd_unget('\\');                /* 把 '\' 还回去, 让字符串解析器继续 */
            return 0;                      /* 只丢高代理 */
        }
        rd_get();                          /* 吃掉 'u' */

        lo = 0;
        for (i = 0; i < 4; i++) {
            c = rd_get();
            if (c == EOF) return -1;
            d = hex_digit((unsigned char)c);
            if (d < 0) return -1;
            hex[i] = c;                    /* 复用记录低代理 4 位 */
            lo = lo * 16 + d;
        }
        if (lo < 0xDC00 || lo > 0xDFFF) {
            /* 低代理无效: 只丢高代理, 把整个 \uXXXX 按反序推回 */
            rd_unget(hex[3]);
            rd_unget(hex[2]);
            rd_unget(hex[1]);
            rd_unget(hex[0]);
            rd_unget('u');
            rd_unget('\\');
            return 0;
        }
        v = 0x10000u + ((v - 0xD800u) << 10) + (lo - 0xDC00u);
    } else if (v >= 0xDC00 && v <= 0xDFFF) {
        return 0;                          /* 孤立低代理: 丢弃 */
    }

    append_utf8(v);
    return 0;
}

/* 解码 JSON 字符串; 调用时当前 peek 应为 '"' */
static int emit_json_string(void) {
    int c;

    if (rd_get() != '"') return -1;   /* 吃掉开头引号 */

    for (;;) {
        c = rd_get();
        if (c == EOF) return -1;
        if (c == '"') return 0;
        if (c == '\\') {
            c = rd_get();
            if (c == EOF) return -1;
            switch (c) {
            case '"':  append_byte('"');  break;
            case '\\': append_byte('\\'); break;
            case '/':  append_byte('/');  break;
            case 'b':  append_byte('\b'); break;
            case 'f':  append_byte('\f'); break;
            case 'n':  append_space();    break;   /* 换行 -> 空格 */
            case 't':  append_space();    break;   /* 制表 -> 空格 */
            case 'r':  break;
            case 'u':  if (parse_unicode_escape() != 0) return -1; break;
            default:   append_byte((unsigned char)c); break;
            }
            continue;
        }
        if ((unsigned char)c < 0x20) { append_space(); continue; }
        if ((unsigned char)c < 0x80) { append_byte((unsigned char)c); continue; }
        {
            int n = utf8_char_len((unsigned char)c), i;
            if (line_len + n > MAX_LINE)
                wrap_line();
            append_byte_nc((unsigned char)c);
            for (i = 1; i < n; i++) {
                int cc = rd_get();
                if (cc == EOF) return -1;
                append_byte_nc((unsigned char)cc);
            }
        }
    }
}

static int parse_value(int depth);
static int parse_object(int depth);
static int parse_array(int depth);

static int parse_number(void) {
    int c;
    while ((c = rd_peek()) != EOF && is_num_char((unsigned char)c)) {
        rd_get();
        append_byte((unsigned char)c);
    }
    return 0;
}

static int parse_value(int depth) {
    int c;

    skip_ws();
    c = rd_peek();
    if (c == EOF) return -1;

    if (c == '{') return parse_object(depth);
    if (c == '[') return parse_array(depth);
    if (c == '"') return emit_json_string();
    if (c == 't') { if (!match_lit("true"))  return -1; append_str("true");  return 0; }
    if (c == 'f') { if (!match_lit("false")) return -1; append_str("false"); return 0; }
    if (c == 'n') { if (!match_lit("null"))  return -1; append_str("null");  return 0; }
    if ((c >= '0' && c <= '9') || c == '-') return parse_number();
    return -1;
}

static int parse_object(int depth) {
    int c;

    if (depth > MAX_DEPTH) return -1;
    rd_get();                        /* 吃掉 '{' */
    skip_ws();
    c = rd_peek();
    if (c == '}') { rd_get(); return 0; }

    for (;;) {
        skip_ws();
        c = rd_peek();
        if (c != '"') return -1;

        output_indent(depth);
        if (emit_json_string() != 0) return -1;   /* key */
        skip_ws();
        c = rd_peek();
        if (c != ':') return -1;
        rd_get();
        skip_ws();

        append_str(": ");
        c = rd_peek();
        if (c == '{' || c == '[') {
            flush_line();                        /* key: 独占一行 */
            if (parse_value(depth + 1) != 0) return -1;
        } else {
            if (parse_value(depth) != 0) return -1;
            flush_line();
        }
        skip_ws();
        c = rd_peek();
        if (c == ',') { rd_get(); continue; }
        break;
    }
    skip_ws();
    c = rd_peek();
    if (c != '}') return -1;
    rd_get();
    return 0;
}

static int parse_array(int depth) {
    int c;

    if (depth > MAX_DEPTH) return -1;
    rd_get();                        /* 吃掉 '[' */
    skip_ws();
    c = rd_peek();
    if (c == ']') { rd_get(); return 0; }

    for (;;) {
        output_indent(depth);
        append_str("- ");
        c = rd_peek();
        if (c == '{' || c == '[') {
            flush_line();                        /* '-' 独占一行 */
            if (parse_value(depth + 1) != 0) return -1;
        } else {
            if (parse_value(depth) != 0) return -1;
            flush_line();
        }
        skip_ws();
        c = rd_peek();
        if (c == ',') { rd_get(); continue; }
        break;
    }
    skip_ws();
    c = rd_peek();
    if (c != ']') return -1;
    rd_get();
    return 0;
}

int main(int argc, char **argv) {
    int c;

    if (argc > 1) {
        in_fp = fopen(argv[1], "rb");
        if (!in_fp) { perror(argv[1]); return 1; }
    } else {
        in_fp = stdin;
    }

    skip_ws();
    c = rd_peek();
    if (c == EOF) {
        fprintf(stderr, "empty input\n");
        if (in_fp != stdin) fclose(in_fp);
        return 1;
    }
    if (parse_value(0) != 0) {
        fprintf(stderr, "json parse error at byte %lu\n", (unsigned long)rd_pos);
        if (in_fp != stdin) fclose(in_fp);
        return 1;
    }
    skip_ws();
    c = rd_peek();
    if (c != EOF) {
        fprintf(stderr, "trailing data at byte %lu\n", (unsigned long)rd_pos);
        if (in_fp != stdin) fclose(in_fp);
        return 1;
    }
    flush_line();
    if (truncated)
        fprintf(stderr, "warning: output truncated, increase LINE_CAP\n");
    if (in_fp != stdin)
        fclose(in_fp);
    return 0;
}
