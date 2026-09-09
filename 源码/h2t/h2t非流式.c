/*
 * html2txt.c - 极简 HTML -> 纯文本(只取标签内容)
 *
 *   - 丢弃 <style>/<script>/<noscript>/<template>/<svg> 与注释 <!-- -->
 *   - 丢掉标签，只保留标签之间的可见文本
 *   - 解码常见实体: &nbsp; &amp; &lt; &gt; &quot; &#233; &#x4E2D; ...
 *   - 空白折叠; 块级标签(p/div/br/li/td/title...)换行
 *   - 每行超过 MAX_LINE 字节时按词换行(长词硬断)
 *
 * 用法:
 *   curl -s http://example/ | ./html2txt
 *   ./html2txt page.html
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>

#define MAX_LINE 80      /* 每行最大字节数, 超过就换行(中文按字节计) */
#define LINE_CAP 128     /* 行缓冲容量, 大于 MAX_LINE+16 即可 */

/* ---------- 输出行缓冲 ---------- */
static char line[LINE_CAP];
static int  line_len = 0;

/* ---------- 输入缓冲(整页读进内存) ---------- */
static char *html;
static size_t html_len;

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
        /* 没有空格可断: 硬断(缓冲区内的字符始终是完整的字) */
        fwrite(line, 1, line_len, stdout);
        fputc('\n', stdout);
        line_len = 0;
        line[0] = '\0';
        return;
    }
    cut = sp;                       /* 断在最后一个空格处 */
    while (cut > 0 && line[cut - 1] == ' ') cut--;
    fwrite(line, 1, cut, stdout);
    fputc('\n', stdout);
    j = sp + 1;                    /* 跳过空格, 剩余部分挪到行首 */
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

/* 码点 -> UTF-8, 带换行预留 */
static void append_utf8(unsigned int cp) {
    unsigned char tmp[4];
    int n = 0;
    int i;
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

/* ---------- 大小写不敏感的子串查找 ---------- */
static size_t find_ci(size_t start, const char *needle) {
    size_t n = strlen(needle);
    size_t i, k;
    if (n == 0) return start;
    for (i = start; i + n <= html_len; i++) {
        for (k = 0; k < n; k++)
            if (tolower((unsigned char)html[i + k]) != tolower((unsigned char)needle[k]))
                break;
        if (k == n) return i;
    }
    return (size_t)-1;
}

/* 跳过一个"内容型"标签, 直到对应 </name> */
/* 已知限制: 若脚本字符串/注释内出现 "</script" 会提前结束跳过 */
static void skip_until_close(const char *name, size_t *pos) {
    char pat[40];
    size_t idx, k;
    snprintf(pat, sizeof(pat), "</%s", name);
    k = strlen(pat);
    idx = find_ci(*pos, pat);
    if (idx == (size_t)-1) { *pos = html_len; return; }
    idx += k;
    while (idx < html_len && html[idx] != '>') idx++;
    if (idx < html_len) idx++;
    *pos = idx;
}

/* ---------- 实体解码 ---------- */
static void decode_entity(size_t *pos) {
    size_t p = *pos, d0, nlen;

    if (p + 1 >= html_len) { append_byte('&'); (*pos)++; return; }

    /* 数字实体 &#123; 或 &#x1F; */
    if (html[p + 1] == '#') {
        size_t i = p + 2;
        unsigned int cp = 0;
        int base = 10;
        if (i < html_len && (html[i] == 'x' || html[i] == 'X')) { base = 16; i++; }
        d0 = i;
        while (i < html_len) {
            unsigned char dc = (unsigned char)html[i];
            int v = -1;
            if (dc >= '0' && dc <= '9') v = dc - '0';
            else if (dc >= 'a' && dc <= 'f') v = dc - 'a' + 10;
            else if (dc >= 'A' && dc <= 'F') v = dc - 'A' + 10;
            if (v < 0 || (base == 10 && v > 9)) break;
            cp = cp * base + v;
            i++;
        }
        if (i == d0 || i >= html_len || html[i] != ';') {
            append_byte('&');          /* 非法数字实体: 当裸 & 输出 */
            *pos = p + 1;
            return;
        }
        /* 丢弃非法码点 */
        if (cp == 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
            *pos = i + 1;
            return;
        }
        if (cp == ' ' || cp == '\t' || cp == '\n' || cp == '\r') append_space();
        else append_utf8(cp);
        *pos = i + 1;
        return;
    }

    /* 命名实体: 在 12 字节内找 ';' */
    size_t i = p + 1;
    size_t end = p + 12;
    if (end > html_len) end = html_len;
    while (i < end && html[i] != ';') i++;
    if (i >= end || i == p + 1) {     /* 裸 & */
        append_byte('&');
        *pos = p + 1;
        return;
    }
    nlen = i - (p + 1);

    /* 空白类实体走 append_space 以正确折叠 */
    if (nlen == 4 && strncasecmp(html + p + 1, "nbsp", 4) == 0) { append_space(); *pos = i + 1; return; }
    if (nlen == 4 && strncasecmp(html + p + 1, "ensp", 4) == 0) { append_space(); *pos = i + 1; return; }
    if (nlen == 4 && strncasecmp(html + p + 1, "emsp", 4) == 0) { append_space(); *pos = i + 1; return; }
    if (nlen == 6 && strncasecmp(html + p + 1, "thinsp", 6) == 0) { append_space(); *pos = i + 1; return; }

    #define NE(name, str) \
        if (nlen == sizeof(name) - 1 && strncasecmp(html + p + 1, name, nlen) == 0) { \
            append_str(str); *pos = i + 1; return; \
        }
    NE("amp",    "&");
    NE("lt",     "<");
    NE("gt",     ">");
    NE("quot",   "\"");
    NE("apos",   "'");
    NE("ndash",  "-");
    NE("mdash",  "-");
    NE("hellip", "...");
    NE("middot", "\xC2\xB7");
    NE("times",  "\xC3\x97");
    NE("deg",    "\xC2\xB0");
    NE("plusmn", "\xC2\xB1");
    NE("copy",   "\xC2\xA9");
    NE("reg",    "\xC2\xAE");
    #undef NE

    *pos = i + 1;   /* 未知实体: 整段丢弃 */
}

/* ---------- 块级标签判断 ---------- */
static int is_block(const char *t) {
    static const char *bl[] = {
        "address","article","aside","blockquote","body","br","caption","dd",
        "details","div","dl","dt","fieldset","figcaption","figure","footer",
        "form","h1","h2","h3","h4","h5","h6","head","header","html","hr",
        "legend","li","main","nav","ol","optgroup","option","p","pre",
        "section","summary","table","tbody","td","tfoot","th","thead",
        "title","tr","ul", NULL
    };
    int i;
    for (i = 0; bl[i]; i++)
        if (strcmp(t, bl[i]) == 0) return 1;
    return 0;
}

/* ---------- 读入整个输入 ---------- */
static int read_all(FILE *fp) {
    size_t chunk = 65536, cap = 131072, n;
    char *buf = malloc(cap);
    if (!buf) return -1;
    html_len = 0;
    while (1) {
        if (html_len + chunk + 1 > cap) {
            if (cap > ((size_t)~0) / 2) { free(buf); return -1; }
            cap *= 2;
            char *nb = realloc(buf, cap);
            if (!nb) { free(buf); return -1; }
            buf = nb;
        }
        n = fread(buf + html_len, 1, chunk, fp);
        html_len += n;
        if (n < chunk) {
            if (ferror(fp)) { free(buf); return -1; }
            break;
        }
    }
    buf[html_len] = '\0';
    html = buf;
    return 0;
}



int main(int argc, char **argv) {
    FILE *fp = stdin;
    size_t pos = 0;

    if (argc > 1) {
        fp = fopen(argv[1], "rb");
        if (!fp) { perror(argv[1]); return 1; }
    }
    if (read_all(fp) != 0) {
        fprintf(stderr, "read failed\n");
        if (fp != stdin) fclose(fp);
        return 1;
    }
    if (fp != stdin) fclose(fp);

    while (pos < html_len) {
        unsigned char c = (unsigned char)html[pos];

        if (c == '<') {
            /* 注释 <!-- ... --> */
            if (pos + 3 < html_len && html[pos + 1] == '!' &&
                html[pos + 2] == '-' && html[pos + 3] == '-') {
                size_t idx = find_ci(pos + 4, "-->");
                pos = (idx == (size_t)-1) ? html_len : idx + 3;
                continue;
            }
            /* <!DOCTYPE> / <?...?> 等声明 */
            if (pos + 1 < html_len && (html[pos + 1] == '!' || html[pos + 1] == '?')) {
                pos++;
                while (pos < html_len && html[pos] != '>') pos++;
                if (pos < html_len) pos++;
                continue;
            }
            /* 普通标签 */
            {
                size_t p = pos + 1;
                int closing = 0, self = 0;
                size_t ns, ne, tl, j;
                char tn[64];

                if (p < html_len && html[p] == '/') { closing = 1; p++; }
                ns = p;
                while (p < html_len &&
                       (isalnum((unsigned char)html[p]) || html[p] == '-' || html[p] == ':'))
                    p++;
                ne = p;
                /* 跳过属性, 正确处理引号内的 '>' */
                while (p < html_len && html[p] != '>') {
                    if (html[p] == '"' || html[p] == '\'') {
                        char q = html[p++];
                        while (p < html_len && html[p] != q) p++;
                        if (p < html_len) p++;
                    } else {
                        p++;
                    }
                }
                if (p < html_len && html[p - 1] == '/') self = 1;
                tl = ne - ns;
                if (tl >= sizeof(tn)) tl = sizeof(tn) - 1;
                for (j = 0; j < tl; j++)
                    tn[j] = (char)tolower((unsigned char)html[ns + j]);
                tn[tl] = '\0';
                if (p < html_len) p++;      /* 越过 '>' */
                pos = p;

                if (!closing && !self) {
                    if (!strcmp(tn, "script") || !strcmp(tn, "style") ||
                        !strcmp(tn, "noscript") || !strcmp(tn, "template") ||
                        !strcmp(tn, "svg")) {
                        skip_until_close(tn, &pos);
                        continue;
                    }
                }
                if (is_block(tn)) flush_line();
            }
            continue;
        }

        if (c == '&') { decode_entity(&pos); continue; }

        if (isspace(c)) {
            append_space();
            pos++;
            while (pos < html_len && isspace((unsigned char)html[pos])) pos++;
            continue;
        }

        if (c < 0x20) { pos++; continue; }   /* 其余控制字符丢弃 */

        /* 普通文本字节 */
        if (c < 0x80) {
            append_byte(c);
        } else if (c >= 0xC2 && c <= 0xDF) {          /* 2 字节 UTF-8 开头 */
            if (line_len + 2 > MAX_LINE) wrap_line();
            append_byte_nc(c);
        } else if (c >= 0xE0 && c <= 0xEF) {          /* 3 字节 UTF-8 开头 */
            if (line_len + 3 > MAX_LINE) wrap_line();
            append_byte_nc(c);
        } else if (c >= 0xF0 && c <= 0xF4) {          /* 4 字节 UTF-8 开头 */
            if (line_len + 4 > MAX_LINE) wrap_line();
            append_byte_nc(c);
        } else {                                       /* 续字节或非法字节 */
            append_byte_nc(c);
        }
        pos++;
    }

    flush_line();
    if (truncated)
        fprintf(stderr, "warning: output truncated, increase LINE_CAP\n");
    free(html);
    return 0;
}
