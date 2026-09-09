/*
 * h2t.c - 流式版 HTML -> 纯文本(只取标签内容)
 *   - 逐字节读入, 不整页读内存; 内存 ≈ 行缓冲
 *   - 丢 <style>/<script>/<noscript>/<template>/<svg> 与注释
 *   - 丢标签, 只留标签内文本; 块级标签换行; 空白折叠
 *   - 解码实体(&nbsp; &amp; &#233; &#x4E2D; ...)
 *   - 每行超过 MAX_LINE 字节按词换行
 * 编译:
 *   arm-buildroot-linux-uclibcgnueabi-gcc -std=gnu99 -Os -s -static h2t.c -o h2t
 * 用法:
 *   curl -s http://example/ | ./h2t
 *   ./h2t page.html
 */
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#define MAX_LINE 80
#define LINE_CAP 128

static char line[LINE_CAP];
static int  line_len = 0;
static int  truncated = 0;

static FILE *in_fp = NULL;

/* ---------- 单字节输入 ---------- */
static int rd_get(void) {
    return fgetc(in_fp);
}
static int rd_peek(void) {
    int c = fgetc(in_fp);
    if (c != EOF) ungetc(c, in_fp);
    return c;
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

/* 普通文本字节: 多字节字符整体读入, 避免折行拆坏 UTF-8 */
static void append_text_byte(unsigned char c) {
    int n, i;
    if (c < 0x80) {
        append_byte(c);
        return;
    }
    n = utf8_char_len(c);
    if (n == 1) {               /* 续字节/非法字节 */
        append_byte_nc(c);
        return;
    }
    if (line_len + n > MAX_LINE)
        wrap_line();
    append_byte_nc(c);
    for (i = 1; i < n; i++) {
        int cc = rd_get();
        if (cc == EOF) break;
        append_byte_nc((unsigned char)cc);
    }
}

/* ---------- 注释 / 声明 / 跳过 ---------- */
static void skip_comment(void) {
    int c, d1 = 0, d2 = 0;
    while ((c = rd_get()) != EOF) {
        if (d2 == '-' && d1 == '-' && c == '>')
            return;
        d2 = d1;
        d1 = c;
    }
}

static void skip_decl(void) {
    int c;
    while ((c = rd_get()) != EOF && c != '>')
        ;
}

/* 流式找 </name(不区分大小写, KMP), 找到后跳到该标签末尾 '>'。
 * 已知限制: 不识别 JS/CSS 字符串与注释里出现的 "</name"。 */
static void skip_until_close(const char *name) {
    char pat[40];
    size_t k, i, j;
    size_t fail[40];
    int c;

    snprintf(pat, sizeof(pat), "</%s", name);
    k = strlen(pat);

    fail[0] = 0;
    for (i = 1, j = 0; i < k; i++) {
        while (j > 0 &&
               tolower((unsigned char)pat[i]) != tolower((unsigned char)pat[j]))
            j = fail[j - 1];
        if (tolower((unsigned char)pat[i]) == tolower((unsigned char)pat[j]))
            j++;
        fail[i] = j;
    }

    j = 0;
    while ((c = rd_get()) != EOF) {
        unsigned char lc = (unsigned char)tolower((unsigned char)c);
        while (j > 0 && lc != tolower((unsigned char)pat[j]))
            j = fail[j - 1];
        if (lc == tolower((unsigned char)pat[j]))
            j++;
        if (j == k) {
            /* 跳到闭合标签的 '>', 引号内的 '>' 不计 */
            for (;;) {
                c = rd_peek();
                if (c == EOF) return;
                if (c == '>') { rd_get(); return; }
                if (c == '"' || c == '\'') {
                    rd_get();
                    {
                        char q = (char)c;
                        while ((c = rd_get()) != EOF && c != q)
                            ;
                    }
                    continue;
                }
                rd_get();
            }
        }
    }
}

/* ---------- 块级标签 ---------- */
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

/* 在读到 '<' 之后调用 */
static void handle_tag(void) {
    int c, tl, closing = 0, self = 0;
    char tn[64];

    tn[0] = '\0';

    c = rd_peek();
    if (c == '!') {
        rd_get();
        if (rd_peek() == '-') {
            rd_get();
            if (rd_peek() == '-') {
                rd_get();
                skip_comment();
                return;
            }
            skip_decl();
            return;
        }
        skip_decl();
        return;
    }
    if (c == '?') {
        rd_get();
        skip_decl();
        return;
    }

    c = rd_peek();
    if (c == '/') { closing = 1; rd_get(); }

    tl = 0;
    while ((c = rd_peek()) != EOF &&
           (isalnum((unsigned char)c) || c == '-' || c == ':')) {
        rd_get();
        if (tl < (int)sizeof(tn) - 1)
            tn[tl++] = (char)tolower((unsigned char)c);
    }
    tn[tl] = '\0';

    for (;;) {
        c = rd_peek();
        if (c == EOF) break;
        if (c == '>') { rd_get(); break; }
        if (c == '"' || c == '\'') {
            rd_get();
            {
                char q = (char)c;
                while ((c = rd_get()) != EOF && c != q)
                    ;
            }
            continue;
        }
        if (c == '/') {
            rd_get();
            if (rd_peek() == '>') { self = 1; rd_get(); break; }
            continue;
        }
        rd_get();
    }

    if (!closing && !self) {
        if (!strcmp(tn, "script") || !strcmp(tn, "style") ||
            !strcmp(tn, "noscript") || !strcmp(tn, "template") ||
            !strcmp(tn, "svg")) {
            skip_until_close(tn);
            return;
        }
    }
    if (is_block(tn)) flush_line();
}

/* ---------- 实体解码 ---------- */
static int hex_digit(unsigned char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* 已知命名实体 -> 替换; 返回 1=命中 0=未命中 */
static int emit_known_entity(const char *name) {
    if (strcmp(name, "nbsp") == 0 || strcmp(name, "ensp") == 0 ||
        strcmp(name, "emsp") == 0 || strcmp(name, "thinsp") == 0) {
        append_space();
        return 1;
    }
    #define E(nm, str) if (strcmp(name, nm) == 0) { append_str(str); return 1; }
    E("amp",    "&");
    E("lt",     "<");
    E("gt",     ">");
    E("quot",   "\"");
    E("apos",   "'");
    E("ndash",  "-");
    E("mdash",  "-");
    E("hellip", "...");
    E("middot", "\xC2\xB7");
    E("times",  "\xC3\x97");
    E("deg",    "\xC2\xB0");
    E("plusmn", "\xC2\xB1");
    E("copy",   "\xC2\xA9");
    E("reg",    "\xC2\xAE");
    #undef E
    return 0;
}

/* 调用时已读到 '&' */
static void decode_entity(void) {
    int c, any, base, d, k;
    unsigned cp;
    unsigned char tmp[40];
    size_t n;
    char name[40];

    c = rd_peek();
    if (c == EOF) { append_byte('&'); return; }

    /* 数字实体 */
    if (c == '#') {
        rd_get();                    /* 吃掉 '#' */
        base = 10;
        any = 0;
        cp = 0;
        n = 0;
        tmp[n++] = '#';

        c = rd_peek();
        if (c == 'x' || c == 'X') {
            base = 16;
            rd_get();
            tmp[n++] = (unsigned char)c;
            c = rd_peek();
        }
        while (n + 1 < sizeof(tmp) && (c = rd_peek()) != EOF) {
            d = hex_digit((unsigned char)c);
            if (d < 0) break;
            if (base == 10 && d > 9) break;
            if (cp > (0x10FFFFu - (unsigned int)d) / (unsigned int)base)
                break;      /* 再累加会超码点范围, 停下走"残次重发"路径 */
            rd_get();
            tmp[n++] = (unsigned char)c;
            cp = cp * base + d;
            any = 1;
        }
        if (any && c == ';') {
            rd_get();                /* 吃掉 ';' */
            if (cp == 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
                return;              /* 丢弃非法码点 */
            if (cp == ' ' || cp == '\t' || cp == '\n' || cp == '\r')
                append_space();
            else
                append_utf8(cp);
            return;
        }
        /* 残次数字实体: '&' + 已读字符当文本重发; 未读字符留在流里 */
        append_byte('&');
        for (k = 0; k < (int)n; k++)
            append_byte((unsigned char)tmp[k]);
        return;
    }

    /* 命名实体: 只收集字母/数字 */
    n = 0;
    while (n + 1 < sizeof(tmp) &&
           (c = rd_peek()) != EOF && isalnum((unsigned char)c)) {
        rd_get();
        tmp[n++] = (unsigned char)c;
    }
    if (n == 0) {                    /* 裸 '&' */
        append_byte('&');
        return;
    }

    if (c == ';') {
        rd_get();                    /* 吃掉 ';' */
        for (k = 0; k < (int)n; k++)
            name[k] = (char)tolower(tmp[k]);
        name[n] = '\0';
        if (emit_known_entity(name)) return;
        return;                      /* 未知实体: 整段丢弃(与整块版一致) */
    }

    /* 无 ';': '&' + 名字当普通文本重发(等效回退) */
    append_byte('&');
    for (k = 0; k < (int)n; k++)
        append_byte((unsigned char)tmp[k]);
}

int main(int argc, char **argv) {
    int c;

    if (argc > 1) {
        in_fp = fopen(argv[1], "rb");
        if (!in_fp) { perror(argv[1]); return 1; }
    } else {
        in_fp = stdin;
    }

    while ((c = rd_get()) != EOF) {
        if (c == '<') { handle_tag(); continue; }
        if (c == '&') { decode_entity(); continue; }
        if (isspace((unsigned char)c)) {
            append_space();
            while ((c = rd_peek()) != EOF && isspace((unsigned char)c))
                rd_get();
            continue;
        }
        if ((unsigned char)c < 0x20) continue;
        append_text_byte((unsigned char)c);
    }

    flush_line();
    if (truncated)
        fprintf(stderr, "warning: output truncated, increase LINE_CAP\n");
    if (in_fp != stdin)
        fclose(in_fp);
    return 0;
}
