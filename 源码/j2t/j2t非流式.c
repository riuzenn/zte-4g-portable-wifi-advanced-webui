/*
 * json2txt.c - JSON -> 对齐纯文本
 *   {"a":1,"b":{"c":[2,3]}}  变成:
 *   a: 1
 *   b:
 *     c:
 *       - 2
 *       - 3
 *
 * 编译:
 *   arm-buildroot-linux-uclibcgnueabi-gcc -static -O2 -s json2txt.c -o json2txt
 * 用法:
 *   curl -s http://x/api | ./json2txt
 *   ./json2txt data.json
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MAX_LINE   80
#define LINE_CAP   128
#define MAX_DEPTH  24

/* ---------- 输出行缓冲 ---------- */
static char line[LINE_CAP];
static int  line_len = 0;

static int truncated = 0;
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

/* 换新行 + 每层 2 空格缩进 */
static void output_indent(int depth) {
    int n, i;
    if (line_len > 0)
        flush_line();
    n = depth * 2;
    if (n > MAX_LINE / 2)
        n = MAX_LINE / 2;      /* 缩进封顶, 防异常深嵌套 */
    for (i = 0; i < n; i++)
        append_byte_nc(' ');
}

/* ---------- 输入 ---------- */
static char        *g_buf;
static size_t       g_len;
static size_t       p;      /* 解析游标 */

static int read_all(FILE *fp) {
    size_t chunk = 65536, cap = 131072, n;
    char *buf = malloc(cap), *nb;
    if (!buf) return -1;
    g_len = 0;
    while (1) {
        if (g_len + chunk + 1 > cap) {
            if (cap > ((size_t)~0) / 2) { free(buf); return -1; }
            cap *= 2;
            nb = realloc(buf, cap);
            if (!nb) { free(buf); return -1; }
            buf = nb;
        }
        n = fread(buf + g_len, 1, chunk, fp);
        g_len += n;
        if (n < chunk) {
            if (ferror(fp)) { free(buf); return -1; }
            break;
        }
    }
    buf[g_len] = '\0';
    g_buf = buf;
    return 0;
}

static void skip_ws(void) {
    while (p < g_len && isspace((unsigned char)g_buf[p]))
        p++;
}

static int match_lit(const char *s) {
    size_t n = strlen(s);
    if (p + n > g_len) return 0;
    if (strncmp(g_buf + p, s, n) != 0) return 0;
    if (p + n < g_len && isalnum((unsigned char)g_buf[p + n])) return 0;
    return 1;
}

static unsigned parse_hex4(const char *s) {
    unsigned v = 0;
    int i;
    for (i = 0; i < 4; i++) {
        unsigned char c = (unsigned char)s[i];
        v <<= 4;
        if (c >= '0' && c <= '9')       v |= c - '0';
        else if (c >= 'a' && c <= 'f')  v |= c - 'a' + 10;
        else if (c >= 'A' && c <= 'F')  v |= c - 'A' + 10;
        else return (unsigned)-1;
    }
    return v;
}

static int is_num_char(unsigned char c) {
    return (c >= '0' && c <= '9') || c == '-' || c == '+' ||
           c == '.' || c == 'e' || c == 'E';
}

/* 解码一个 JSON 字符串(不含两端的引号)并追加到当前行 */
static int emit_json_string(void) {
    unsigned char c;
    unsigned cp, lo;
    int n, i;

    p++;   /* 跳过开头的 '"' */
    while (p < g_len) {
        c = (unsigned char)g_buf[p];
        if (c == '"') { p++; return 0; }
        if (c == '\\') {
            p++;
            if (p >= g_len) return -1;
            c = (unsigned char)g_buf[p];
            switch (c) {
            case '"':  append_byte('"');  p++; break;
            case '\\': append_byte('\\'); p++; break;
            case '/':  append_byte('/');  p++; break;
            case 'b':  append_byte('\b'); p++; break;
            case 'f':  append_byte('\f'); p++; break;
            case 'n':  append_space();    p++; break;   /* 换行 -> 空格 */
            case 't':  append_space();    p++; break;   /* 制表 -> 空格 */
            case 'r':  p++; break;                       /* \r 忽略 */
            case 'u':
                if (p + 5 > g_len) return -1;
                cp = parse_hex4(g_buf + p + 1);
                if (cp == (unsigned)-1) return -1;
                p += 5;
                if (cp >= 0xD800 && cp <= 0xDBFF &&
                    p + 6 <= g_len && g_buf[p] == '\\' && g_buf[p + 1] == 'u') {
                    lo = parse_hex4(g_buf + p + 2);
                    if (lo != (unsigned)-1 && lo >= 0xDC00 && lo <= 0xDFFF) {
                        cp = 0x10000u + ((cp - 0xD800u) << 10) + (lo - 0xDC00u);
                        p += 6;
                    }
                }
                if (cp >= 0xD800 && cp <= 0xDFFF)
                    break;      /* 未配对代理: 丢弃, 不产出非法 UTF-8 */
                append_utf8(cp);
                break;
            default:
                append_byte(c); p++; break;   /* 未知转义: 宽容处理 */
            }
        } else if (c < 0x20) {
            append_space(); p++;
        } else if (c < 0x80) {
            append_byte(c); p++;
        } else {
            n = utf8_char_len(c);
            if (line_len + n > MAX_LINE)
                wrap_line();
            for (i = 0; i < n && p + i < g_len; i++)
                append_byte_nc((unsigned char)g_buf[p + i]);
            p += n;
        }
    }
    return -1;
}

static int parse_value(int depth);
static int parse_object(int depth);
static int parse_array(int depth);

static int parse_value(int depth) {
    unsigned char c;
    size_t start, i;

    skip_ws();
    if (p >= g_len) return -1;
    c = (unsigned char)g_buf[p];

    if (c == '{') return parse_object(depth);
    if (c == '[') return parse_array(depth);
    if (c == '"') return emit_json_string();

    if (c == 't' && match_lit("true"))  { p += 4; append_str("true");  return 0; }
    if (c == 'f' && match_lit("false")) { p += 5; append_str("false"); return 0; }
    if (c == 'n' && match_lit("null"))  { p += 4; append_str("null");  return 0; }

    if ((c >= '0' && c <= '9') || c == '-') {
        start = p;
        while (p < g_len && is_num_char((unsigned char)g_buf[p]))
            p++;
        for (i = start; i < p; i++)
            append_byte((unsigned char)g_buf[i]);
        return 0;
    }
    return -1;
}

static int parse_object(int depth) {
    if (depth > MAX_DEPTH) return -1;
    p++;   /* 跳过 '{' */
    skip_ws();
    if (p < g_len && g_buf[p] == '}') { p++; return 0; }

    for (;;) {
        skip_ws();
        if (p >= g_len || g_buf[p] != '"') return -1;

        output_indent(depth);
        if (emit_json_string() != 0) return -1;   /* key */
        skip_ws();
        if (p >= g_len || g_buf[p] != ':') return -1;
        p++;
        skip_ws();

        append_str(": ");
        if (p < g_len && (g_buf[p] == '{' || g_buf[p] == '[')) {
            flush_line();                          /* key: 独占一行 */
            if (parse_value(depth + 1) != 0) return -1;
        } else {
            if (parse_value(depth) != 0) return -1;
            flush_line();
        }
        skip_ws();
        if (p < g_len && g_buf[p] == ',') { p++; continue; }
        break;
    }
    skip_ws();
    if (p >= g_len || g_buf[p] != '}') return -1;
    p++;
    return 0;
}

static int parse_array(int depth) {
    if (depth > MAX_DEPTH) return -1;
    p++;   /* 跳过 '[' */
    skip_ws();
    if (p < g_len && g_buf[p] == ']') { p++; return 0; }

    for (;;) {
        output_indent(depth);
        append_str("- ");
        if (p < g_len && (g_buf[p] == '{' || g_buf[p] == '[')) {
            flush_line();                          /* '-' 独占一行 */
            if (parse_value(depth + 1) != 0) return -1;
        } else {
            if (parse_value(depth) != 0) return -1;
            flush_line();
        }
        skip_ws();
        if (p < g_len && g_buf[p] == ',') { p++; continue; }
        break;
    }
    skip_ws();
    if (p >= g_len || g_buf[p] != ']') return -1;
    p++;
    return 0;
}

int main(int argc, char **argv) {
    FILE *fp = stdin;

    if (argc > 1) {
        fp = fopen(argv[1], "rb");
        if (!fp) { perror(argv[1]); return 1; }
    }
    p = 0;
    if (read_all(fp) != 0) {
        fprintf(stderr, "read failed\n");
        if (fp != stdin) fclose(fp);
        return 1;
    }
    if (fp != stdin) fclose(fp);

    skip_ws();
    if (p >= g_len) {
        fprintf(stderr, "empty input\n");
        free(g_buf);
        return 1;
    }
    if (parse_value(0) != 0) {
        fprintf(stderr, "json parse error at offset %lu\n", (unsigned long)p);
        free(g_buf);
        return 1;
    }
    skip_ws();
    if (p < g_len) {
        fprintf(stderr, "trailing data at offset %lu\n", (unsigned long)p);
        free(g_buf);
        return 1;
    }
    flush_line();
    if (truncated)
        fprintf(stderr, "warning: output truncated, increase LINE_CAP\n");
    free(g_buf);
    return 0;
}
