/* 端末表示: 色付け・全角文字の表示幅・エラー表示用のソース保存
 *
 * 色は端末に出力しているときだけ付ける（ファイルやパイプに出すときは付けない）。
 * 環境変数で切り替えられる:
 *   NO_COLOR=1           色を付けない
 *   LAPING_COLOR=always  常に色を付ける / never で付けない
 */
#include "laping.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <io.h>
#include <windows.h>
#define lp_isatty _isatty
#define lp_fileno _fileno
#else
#include <unistd.h>
#define lp_isatty isatty
#define lp_fileno fileno
#endif

int ui_color_out = 0;
int ui_color_err = 0;

#if defined(_WIN32)
static int enable_vt(DWORD which) {
    HANDLE h = GetStdHandle(which);
    DWORD mode;
    if (h == INVALID_HANDLE_VALUE || !GetConsoleMode(h, &mode)) return 0;
    return SetConsoleMode(h, mode | 0x0004 /* ENABLE_VIRTUAL_TERMINAL_PROCESSING */) != 0;
}
#endif

void ui_init(void) {
    const char *force = getenv("LAPING_COLOR");
    if (force && strcmp(force, "always") == 0) {
        ui_color_out = ui_color_err = 1;
    } else if ((force && strcmp(force, "never") == 0) || getenv("NO_COLOR")) {
        ui_color_out = ui_color_err = 0;
    } else {
        ui_color_out = lp_isatty(lp_fileno(stdout));
        ui_color_err = lp_isatty(lp_fileno(stderr));
    }
#if defined(_WIN32)
    if (ui_color_out && !enable_vt(STD_OUTPUT_HANDLE)) ui_color_out = 0;
    if (ui_color_err && !enable_vt(STD_ERROR_HANDLE)) ui_color_err = 0;
#endif
}

const char *ui_c(int to_err, const char *code) {
    static char bufs[8][16];
    static int next = 0;
    if (!(to_err ? ui_color_err : ui_color_out)) return "";
    char *b = bufs[next];
    next = (next + 1) % 8;
    snprintf(b, sizeof(bufs[0]), "\x1b[%sm", code);
    return b;
}

/* ===================================================================== */
/*  表示幅                                                                */
/* ===================================================================== */

static int is_wide(unsigned long cp) {
    return (cp >= 0x1100 && cp <= 0x115F) || (cp >= 0x2E80 && cp <= 0x303E) ||
           (cp >= 0x3041 && cp <= 0x33FF) || (cp >= 0x3400 && cp <= 0x4DBF) ||
           (cp >= 0x4E00 && cp <= 0x9FFF) || (cp >= 0xA000 && cp <= 0xA4CF) ||
           (cp >= 0xAC00 && cp <= 0xD7A3) || (cp >= 0xF900 && cp <= 0xFAFF) ||
           (cp >= 0xFE30 && cp <= 0xFE4F) || (cp >= 0xFF00 && cp <= 0xFF60) ||
           (cp >= 0xFFE0 && cp <= 0xFFE6) || (cp >= 0x1F300 && cp <= 0x1F64F) ||
           (cp >= 0x1F900 && cp <= 0x1F9FF) || (cp >= 0x20000 && cp <= 0x3FFFD);
}

/* 端末上での幅。全角文字は2、ANSI の色指定は0として数える */
size_t display_width(const char *s, size_t len) {
    size_t w = 0;
    size_t i = 0;
    while (i < len) {
        unsigned char c = (unsigned char)s[i];
        if (c == 0x1b && i + 1 < len && s[i + 1] == '[') {
            i += 2;
            while (i < len && !((s[i] >= 'A' && s[i] <= 'Z') || (s[i] >= 'a' && s[i] <= 'z'))) i++;
            i++;
            continue;
        }
        size_t cl = utf8_char_len(c);
        if (i + cl > len) cl = len - i;
        unsigned long cp;
        if (cl == 1) cp = c;
        else if (cl == 2) cp = ((c & 0x1Fu) << 6) | ((unsigned char)s[i + 1] & 0x3Fu);
        else if (cl == 3) cp = ((c & 0x0Fu) << 12) | (((unsigned char)s[i + 1] & 0x3Fu) << 6) | ((unsigned char)s[i + 2] & 0x3Fu);
        else cp = ((c & 0x07u) << 18) | (((unsigned char)s[i + 1] & 0x3Fu) << 12) |
                  (((unsigned char)s[i + 2] & 0x3Fu) << 6) | ((unsigned char)s[i + 3] & 0x3Fu);
        if (cp >= 0x0300 && cp <= 0x036F) w += 0;      /* 結合文字 */
        else if (cp == 0x200D || (cp >= 0xFE00 && cp <= 0xFE0F)) w += 0; /* ZWJ・異体字セレクタ */
        else w += is_wide(cp) ? 2 : 1;
        i += cl;
    }
    return w;
}

/* ===================================================================== */
/*  ソースの保存（エラー表示で該当行を見せるため）                        */
/* ===================================================================== */

typedef struct {
    const char *file;
    char *src;
    size_t len;
} Source;

static Source *sources = NULL;
static int source_count = 0, source_cap = 0;

void source_register(const char *file, const char *src, size_t len) {
    if (!file) return;
    for (int i = 0; i < source_count; i++) {
        if (sources[i].file == file || strcmp(sources[i].file, file) == 0) {
            free(sources[i].src);
            sources[i].file = file;
            sources[i].src = xmalloc(len + 1);
            memcpy(sources[i].src, src, len);
            sources[i].src[len] = '\0';
            sources[i].len = len;
            return;
        }
    }
    if (source_count >= source_cap) {
        source_cap = source_cap ? source_cap * 2 : 8;
        sources = xrealloc(sources, sizeof(Source) * (size_t)source_cap);
    }
    Source *s = &sources[source_count++];
    s->file = file;
    s->src = xmalloc(len + 1);
    memcpy(s->src, src, len);
    s->src[len] = '\0';
    s->len = len;
}

const char *source_line(const char *file, int line, size_t *len) {
    if (!file || line < 1) return NULL;
    for (int i = 0; i < source_count; i++) {
        if (sources[i].file != file && strcmp(sources[i].file, file) != 0) continue;
        const char *p = sources[i].src, *end = p + sources[i].len;
        for (int l = 1; l < line && p < end; l++) {
            const char *nl = memchr(p, '\n', (size_t)(end - p));
            if (!nl) return NULL;
            p = nl + 1;
        }
        if (p >= end && line > 1) return NULL;
        const char *e = memchr(p, '\n', (size_t)(end - p));
        size_t n = e ? (size_t)(e - p) : (size_t)(end - p);
        if (n > 0 && p[n - 1] == '\r') n--;
        *len = n;
        return p;
    }
    return NULL;
}

/* ===================================================================== */
/*  REPL 用の色付き表示                                                   */
/* ===================================================================== */

void repr_colored(StrBuf *sb, Value v, int depth) {
    const char *rs = ui_c(0, UI_RESET);
    if (depth > 50) {
        sb_appendc(sb, "...");
        return;
    }
    switch (v.type) {
        case VAL_NUM:
            sb_appendc(sb, ui_c(0, UI_YELLOW));
            value_to_sb(sb, v, 1);
            sb_appendc(sb, rs);
            break;
        case VAL_STR:
            sb_appendc(sb, ui_c(0, UI_GREEN));
            value_to_sb(sb, v, 1);
            sb_appendc(sb, rs);
            break;
        case VAL_BOOL:
        case VAL_NIL:
            sb_appendc(sb, ui_c(0, UI_MAGENTA));
            value_to_sb(sb, v, 1);
            sb_appendc(sb, rs);
            break;
        case VAL_FUNC:
        case VAL_BUILTIN:
        case VAL_RANGE:
            sb_appendc(sb, ui_c(0, UI_CYAN));
            value_to_sb(sb, v, 1);
            sb_appendc(sb, rs);
            break;
        case VAL_LIST: {
            ListObj *l = AS_LIST(v);
            sb_append(sb, "[", 1);
            for (size_t i = 0; i < l->count; i++) {
                if (i) sb_append(sb, ", ", 2);
                repr_colored(sb, l->items[i], depth + 1);
            }
            sb_append(sb, "]", 1);
            break;
        }
        case VAL_MAP: {
            MapObj *m = AS_MAP(v);
            int first = 1;
            if (m->tag) {
                sb_appendc(sb, ui_c(0, UI_BOLD));
                sb_appendc(sb, m->tag);
                sb_appendc(sb, rs);
                sb_append(sb, "(", 1);
            } else {
                sb_append(sb, "{", 1);
            }
            for (size_t i = 0; i < m->count; i++) {
                if (m->entries[i].deleted) continue;
                if (!first) sb_append(sb, ", ", 2);
                first = 0;
                if (m->tag) value_to_sb(sb, m->entries[i].key, 0);
                else repr_colored(sb, m->entries[i].key, depth + 1);
                sb_append(sb, ": ", 2);
                repr_colored(sb, m->entries[i].val, depth + 1);
            }
            sb_append(sb, m->tag ? ")" : "}", 1);
            break;
        }
    }
}
