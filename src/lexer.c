/* 字句解析: ソース文字列 -> トークン列
 *
 * 改行は文の区切りとして T_NEWLINE トークンにする。ただし次の場合は出さない:
 *   - ( ) や [ ] の内側（複数行にまたがる引数やリストを書けるように）
 *   - 行末が演算子やカンマで終わっている（式が次の行に続いている）
 *   - 次の行が "." で始まる（メソッドチェーンの折り返し）
 * { } の内側はブロックでもマップでもありうるので改行を出し、パーサ側で扱う。
 */
#include "laping.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *src;
    size_t len;
    size_t i;
    int line;
    TokenList out;
    char *nest; /* 'p' = ( or [,  'b' = { */
    int nest_len;
    int nest_cap;
} Lexer;

static void push_tok(Lexer *lx, Token t) {
    if (lx->out.count >= lx->out.cap) {
        lx->out.cap = lx->out.cap ? lx->out.cap * 2 : 256;
        lx->out.toks = realloc(lx->out.toks, sizeof(Token) * lx->out.cap);
    }
    lx->out.toks[lx->out.count++] = t;
}

static Token make_tok(TokKind k, int line) {
    Token t;
    memset(&t, 0, sizeof(t));
    t.kind = k;
    t.line = line;
    return t;
}

static void add(Lexer *lx, TokKind k) { push_tok(lx, make_tok(k, lx->line)); }

static void nest_push(Lexer *lx, char c) {
    if (lx->nest_len >= lx->nest_cap) {
        lx->nest_cap = lx->nest_cap ? lx->nest_cap * 2 : 32;
        lx->nest = realloc(lx->nest, lx->nest_cap);
    }
    lx->nest[lx->nest_len++] = c;
}

static void nest_pop(Lexer *lx) {
    if (lx->nest_len > 0) lx->nest_len--;
}

/* この種類のトークンで行が終わっていたら、式は次の行へ続いているとみなす */
static int continues_line(TokKind k) {
    switch (k) {
        case T_PLUS: case T_MINUS: case T_STAR: case T_SLASH: case T_SLASHSLASH:
        case T_PERCENT: case T_POW: case T_EQ: case T_NE: case T_LT: case T_GT:
        case T_LE: case T_GE: case T_ANDAND: case T_OROR: case T_AND: case T_OR:
        case T_NOT: case T_BANG: case T_COMMA: case T_DOT: case T_ASSIGN:
        case T_PLUS_EQ: case T_MINUS_EQ: case T_STAR_EQ: case T_SLASH_EQ:
        case T_PERCENT_EQ: case T_ARROW: case T_QUESTION: case T_COLON:
        case T_DOTDOT: case T_DOTDOTDOT: case T_IN:
            return 1;
        default:
            return 0;
    }
}

static void emit_newline(Lexer *lx) {
    if (lx->nest_len > 0 && lx->nest[lx->nest_len - 1] == 'p') return;
    if (lx->out.count == 0) return;
    TokKind last = lx->out.toks[lx->out.count - 1].kind;
    if (last == T_NEWLINE || continues_line(last)) return;
    /* 次の行が ".foo" で始まるならメソッドチェーンの続き */
    size_t j = lx->i;
    while (j < lx->len) {
        char c = lx->src[j];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { j++; continue; }
        break;
    }
    if (j + 1 < lx->len && lx->src[j] == '.' && lx->src[j + 1] != '.') return;
    add(lx, T_NEWLINE);
}

static int is_ident_start(unsigned char c) { return isalpha(c) || c == '_' || c >= 0x80; }
static int is_ident_char(unsigned char c) { return isalnum(c) || c == '_' || c >= 0x80; }

/* quoted はエラーメッセージ用の表記 */
#define KW(w, k) {w, "'" w "'", k}
static const struct { const char *word; const char *quoted; TokKind kind; } keywords[] = {
    KW("if", T_IF), KW("elif", T_ELIF), KW("else", T_ELSE), KW("unless", T_UNLESS),
    KW("while", T_WHILE), KW("until", T_UNTIL), KW("for", T_FOR), KW("in", T_IN),
    KW("loop", T_LOOP), KW("break", T_BREAK), KW("continue", T_CONTINUE),
    KW("return", T_RETURN), KW("fn", T_FN), KW("let", T_LET), KW("true", T_TRUE),
    KW("false", T_FALSE), KW("nil", T_NIL), KW("and", T_AND), KW("or", T_OR),
    KW("not", T_NOT), KW("match", T_MATCH), KW("try", T_TRY), KW("catch", T_CATCH),
    KW("finally", T_FINALLY), KW("throw", T_THROW), KW("import", T_IMPORT),
    {NULL, NULL, T_EOF}
};
#undef KW

/* UTF-8 で1文字エンコードする */
static void encode_utf8(StrBuf *sb, unsigned long cp) {
    char b[4];
    if (cp < 0x80) { b[0] = (char)cp; sb_append(sb, b, 1); }
    else if (cp < 0x800) {
        b[0] = (char)(0xC0 | (cp >> 6)); b[1] = (char)(0x80 | (cp & 0x3F));
        sb_append(sb, b, 2);
    } else if (cp < 0x10000) {
        b[0] = (char)(0xE0 | (cp >> 12)); b[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        b[2] = (char)(0x80 | (cp & 0x3F));
        sb_append(sb, b, 3);
    } else {
        b[0] = (char)(0xF0 | (cp >> 18)); b[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        b[2] = (char)(0x80 | ((cp >> 6) & 0x3F)); b[3] = (char)(0x80 | (cp & 0x3F));
        sb_append(sb, b, 4);
    }
}

/* バックスラッシュの直後（lx->i は '\\' の次）を読んで sb に追加 */
static void read_escape(Lexer *lx, StrBuf *sb) {
    if (lx->i >= lx->len) syntax_error(lx->line, NULL, "文字列リテラルが閉じられていません");
    char c = lx->src[lx->i++];
    switch (c) {
        case 'n': sb_append(sb, "\n", 1); break;
        case 't': sb_append(sb, "\t", 1); break;
        case 'r': sb_append(sb, "\r", 1); break;
        case '0': sb_append(sb, "\0", 1); break;
        case 'e': sb_append(sb, "\x1b", 1); break;
        case '\\': sb_append(sb, "\\", 1); break;
        case '"': sb_append(sb, "\"", 1); break;
        case '\'': sb_append(sb, "'", 1); break;
        case '{': sb_append(sb, "{", 1); break;
        case '}': sb_append(sb, "}", 1); break;
        case '\n': lx->line++; break; /* 行継続 */
        case 'u': {
            if (lx->i >= lx->len || lx->src[lx->i] != '{')
                syntax_error(lx->line, NULL, "\\u の後には {16進数} が必要です");
            lx->i++;
            unsigned long cp = 0;
            int digits = 0;
            while (lx->i < lx->len && isxdigit((unsigned char)lx->src[lx->i])) {
                char h = lx->src[lx->i++];
                cp = cp * 16 + (unsigned long)(isdigit((unsigned char)h) ? h - '0' : (tolower((unsigned char)h) - 'a' + 10));
                digits++;
            }
            if (!digits || lx->i >= lx->len || lx->src[lx->i] != '}' || cp > 0x10FFFF)
                syntax_error(lx->line, NULL, "不正な \\u エスケープです");
            lx->i++;
            encode_utf8(sb, cp);
            break;
        }
        default:
            syntax_error(lx->line, NULL, "不明なエスケープシーケンス '\\%c'", c);
    }
}

static char *sb_take(StrBuf *sb, size_t *len) {
    if (!sb->buf) sb_append(sb, "", 0);
    *len = sb->len;
    return sb->buf; /* 所有権をトークンへ移す */
}

/* "..." を読む。{式} による埋め込みがあれば T_INTERP を作る */
static void lex_dq_string(Lexer *lx) {
    int start_line = lx->line;
    lx->i++; /* opening quote */
    InterpPart *parts = NULL;
    int nparts = 0, cap = 0;
    StrBuf cur;
    sb_init(&cur);
    for (;;) {
        if (lx->i >= lx->len) syntax_error(start_line, NULL, "文字列リテラルが閉じられていません");
        char c = lx->src[lx->i];
        if (c == '"') { lx->i++; break; }
        if (c == '\\') { lx->i++; read_escape(lx, &cur); continue; }
        if (c == '\n') lx->line++;
        if (c == '{') {
            /* 埋め込み式: 対応する } まで（入れ子の {} と文字列を考慮） */
            int expr_line = lx->line;
            size_t s = ++lx->i;
            int depth = 1;
            while (lx->i < lx->len && depth > 0) {
                char d = lx->src[lx->i];
                if (d == '{') depth++;
                else if (d == '}') { if (--depth == 0) break; }
                else if (d == '\n') lx->line++;
                else if (d == '"' || d == '\'') {
                    char q = d;
                    lx->i++;
                    while (lx->i < lx->len && lx->src[lx->i] != q) {
                        if (lx->src[lx->i] == '\\') lx->i++;
                        lx->i++;
                    }
                }
                lx->i++;
            }
            if (lx->i >= lx->len) syntax_error(expr_line, NULL, "文字列中の '{' が閉じられていません");
            size_t e = lx->i;
            lx->i++; /* '}' */
            if (nparts + 2 > cap) {
                cap = cap ? cap * 2 : 8;
                parts = realloc(parts, sizeof(InterpPart) * cap);
            }
            if (cur.len > 0) {
                InterpPart p = {0, NULL, 0, start_line};
                p.text = sb_take(&cur, &p.len);
                parts[nparts++] = p;
                sb_init(&cur);
            }
            InterpPart p = {1, NULL, e - s, expr_line};
            p.text = malloc(e - s + 1);
            memcpy(p.text, lx->src + s, e - s);
            p.text[e - s] = '\0';
            parts[nparts++] = p;
            continue;
        }
        sb_append(&cur, &c, 1);
        lx->i++;
    }
    if (nparts == 0) {
        Token t = make_tok(T_STR, start_line);
        t.str = sb_take(&cur, &t.len);
        push_tok(lx, t);
        return;
    }
    if (cur.len > 0) {
        if (nparts + 1 > cap) parts = realloc(parts, sizeof(InterpPart) * (cap + 1));
        InterpPart p = {0, NULL, 0, start_line};
        p.text = sb_take(&cur, &p.len);
        parts[nparts++] = p;
    } else {
        sb_free(&cur);
    }
    Token t = make_tok(T_INTERP, start_line);
    t.parts = parts;
    t.nparts = nparts;
    push_tok(lx, t);
}

/* '...' は埋め込みなし。エスケープは \\ と \' のみ */
static void lex_sq_string(Lexer *lx) {
    int start_line = lx->line;
    lx->i++;
    StrBuf sb;
    sb_init(&sb);
    for (;;) {
        if (lx->i >= lx->len) syntax_error(start_line, NULL, "文字列リテラルが閉じられていません");
        char c = lx->src[lx->i];
        if (c == '\'') { lx->i++; break; }
        if (c == '\\' && lx->i + 1 < lx->len && (lx->src[lx->i + 1] == '\'' || lx->src[lx->i + 1] == '\\')) {
            sb_append(&sb, &lx->src[lx->i + 1], 1);
            lx->i += 2;
            continue;
        }
        if (c == '\n') lx->line++;
        sb_append(&sb, &c, 1);
        lx->i++;
    }
    Token t = make_tok(T_STR, start_line);
    t.str = sb_take(&sb, &t.len);
    push_tok(lx, t);
}

/* 数値リテラル用のバッファに1文字追加する。長すぎる場合は切り捨てずにエラーにする */
static void num_put(Lexer *lx, char *buf, size_t size, size_t *b, char c) {
    if (*b >= size - 1) syntax_error(lx->line, NULL, "数値リテラルが長すぎます");
    buf[(*b)++] = c;
}

static void lex_number(Lexer *lx) {
    const char *s = lx->src;
    size_t i = lx->i;
    char buf[128];
    size_t b = 0;
    double val;
    if (s[i] == '0' && i + 1 < lx->len && (s[i + 1] == 'x' || s[i + 1] == 'X' || s[i + 1] == 'b' || s[i + 1] == 'B')) {
        int base = (s[i + 1] == 'x' || s[i + 1] == 'X') ? 16 : 2;
        i += 2;
        val = 0;
        int digits = 0;
        while (i < lx->len) {
            char c = s[i];
            int d;
            if (c == '_') { i++; continue; }
            if (isdigit((unsigned char)c)) d = c - '0';
            else if (base == 16 && isxdigit((unsigned char)c)) d = tolower((unsigned char)c) - 'a' + 10;
            else break;
            if (d >= base) break;
            val = val * base + d;
            digits++;
            i++;
        }
        if (!digits) syntax_error(lx->line, NULL, "不正な数値リテラルです");
    } else {
        while (i < lx->len && (isdigit((unsigned char)s[i]) || s[i] == '_')) {
            if (s[i] != '_') num_put(lx, buf, sizeof(buf), &b, s[i]);
            i++;
        }
        /* "1..5" の ".." は範囲演算子なので小数点として読まない */
        if (i + 1 < lx->len && s[i] == '.' && isdigit((unsigned char)s[i + 1])) {
            num_put(lx, buf, sizeof(buf), &b, s[i++]);
            while (i < lx->len && (isdigit((unsigned char)s[i]) || s[i] == '_')) {
                if (s[i] != '_') num_put(lx, buf, sizeof(buf), &b, s[i]);
                i++;
            }
        }
        if (i < lx->len && (s[i] == 'e' || s[i] == 'E')) {
            size_t j = i + 1;
            if (j < lx->len && (s[j] == '+' || s[j] == '-')) j++;
            if (j < lx->len && isdigit((unsigned char)s[j])) {
                while (i < j) num_put(lx, buf, sizeof(buf), &b, s[i++]);
                while (i < lx->len && isdigit((unsigned char)s[i])) num_put(lx, buf, sizeof(buf), &b, s[i++]);
            }
        }
        buf[b] = '\0';
        val = strtod(buf, NULL);
    }
    if (i < lx->len && is_ident_start((unsigned char)s[i]))
        syntax_error(lx->line, NULL, "数値の直後に識別子を続けることはできません");
    lx->i = i;
    Token t = make_tok(T_NUM, lx->line);
    t.num = val;
    push_tok(lx, t);
}

TokenList lex(const char *src, size_t len, int first_line) {
    Lexer lx;
    memset(&lx, 0, sizeof(lx));
    lx.src = src;
    lx.len = len;
    lx.line = first_line;

    while (lx.i < lx.len) {
        unsigned char c = (unsigned char)src[lx.i];
        if (c == '\n') {
            lx.i++;
            emit_newline(&lx);
            lx.line++;
            continue;
        }
        if (c == ' ' || c == '\t' || c == '\r') { lx.i++; continue; }
        /* 全角スペース (U+3000) も空白として扱う */
        if (c == 0xE3 && lx.i + 2 < lx.len && (unsigned char)src[lx.i + 1] == 0x80 && (unsigned char)src[lx.i + 2] == 0x80) {
            lx.i += 3;
            continue;
        }
        if (c == '#') {
            if (lx.i + 1 < lx.len && src[lx.i + 1] == '[') {
                /* ブロックコメント #[ ... ]# */
                int start_line = lx.line;
                lx.i += 2;
                while (lx.i + 1 < lx.len && !(src[lx.i] == ']' && src[lx.i + 1] == '#')) {
                    if (src[lx.i] == '\n') lx.line++;
                    lx.i++;
                }
                if (lx.i + 1 >= lx.len) syntax_error(start_line, NULL, "ブロックコメント #[ が閉じられていません");
                lx.i += 2;
                continue;
            }
            while (lx.i < lx.len && src[lx.i] != '\n') lx.i++;
            continue;
        }
        if (c == '\\' && lx.i + 1 < lx.len && (src[lx.i + 1] == '\n' || src[lx.i + 1] == '\r')) {
            /* 行末の \ で明示的に次の行へ続ける */
            lx.i++;
            if (src[lx.i] == '\r') lx.i++;
            if (lx.i < lx.len && src[lx.i] == '\n') { lx.i++; lx.line++; }
            continue;
        }
        if (isdigit(c)) { lex_number(&lx); continue; }
        if (c == '"') { lex_dq_string(&lx); continue; }
        if (c == '\'') { lex_sq_string(&lx); continue; }
        if (is_ident_start(c)) {
            size_t s = lx.i;
            while (lx.i < lx.len && is_ident_char((unsigned char)src[lx.i])) lx.i++;
            size_t n = lx.i - s;
            TokKind kind = T_IDENT;
            for (int k = 0; keywords[k].word; k++) {
                if (strlen(keywords[k].word) == n && memcmp(keywords[k].word, src + s, n) == 0) {
                    kind = keywords[k].kind;
                    break;
                }
            }
            Token t = make_tok(kind, lx.line);
            if (kind == T_IDENT) {
                t.str = (char *)intern(src + s, n);
                t.len = n;
            }
            push_tok(&lx, t);
            continue;
        }

        char c1 = lx.i + 1 < lx.len ? src[lx.i + 1] : '\0';
        char c2 = lx.i + 2 < lx.len ? src[lx.i + 2] : '\0';
        TokKind k = T_EOF;
        int w = 1;
        switch (c) {
            case '(': k = T_LPAREN; nest_push(&lx, 'p'); break;
            case ')': k = T_RPAREN; nest_pop(&lx); break;
            case '[': k = T_LBRACKET; nest_push(&lx, 'p'); break;
            case ']': k = T_RBRACKET; nest_pop(&lx); break;
            case '{': k = T_LBRACE; nest_push(&lx, 'b'); break;
            case '}': k = T_RBRACE; nest_pop(&lx); break;
            case ',': k = T_COMMA; break;
            case ':': k = T_COLON; break;
            case ';': k = T_SEMI; break;
            case '?': k = T_QUESTION; break;
            case '.':
                if (c1 == '.' && c2 == '.') { k = T_DOTDOTDOT; w = 3; }
                else if (c1 == '.') { k = T_DOTDOT; w = 2; }
                else k = T_DOT;
                break;
            case '=':
                if (c1 == '=') { k = T_EQ; w = 2; }
                else if (c1 == '>') { k = T_ARROW; w = 2; }
                else k = T_ASSIGN;
                break;
            case '!':
                if (c1 == '=') { k = T_NE; w = 2; } else k = T_BANG;
                break;
            case '<':
                if (c1 == '=') { k = T_LE; w = 2; } else k = T_LT;
                break;
            case '>':
                if (c1 == '=') { k = T_GE; w = 2; } else k = T_GT;
                break;
            case '+':
                if (c1 == '=') { k = T_PLUS_EQ; w = 2; } else k = T_PLUS;
                break;
            case '-':
                if (c1 == '=') { k = T_MINUS_EQ; w = 2; } else k = T_MINUS;
                break;
            case '*':
                if (c1 == '*') { k = T_POW; w = 2; }
                else if (c1 == '=') { k = T_STAR_EQ; w = 2; }
                else k = T_STAR;
                break;
            case '/':
                if (c1 == '/') { k = T_SLASHSLASH; w = 2; }
                else if (c1 == '=') { k = T_SLASH_EQ; w = 2; }
                else k = T_SLASH;
                break;
            case '%':
                if (c1 == '=') { k = T_PERCENT_EQ; w = 2; } else k = T_PERCENT;
                break;
            case '&':
                if (c1 == '&') { k = T_ANDAND; w = 2; }
                break;
            case '|':
                if (c1 == '|') { k = T_OROR; w = 2; }
                break;
            default:
                break;
        }
        if (k == T_EOF) {
            if (c >= 0x20 && c < 0x7f) syntax_error(lx.line, NULL, "不正な文字 '%c'", c);
            syntax_error(lx.line, NULL, "不正な文字 (0x%02X)", c);
        }
        add(&lx, k);
        lx.i += w;
    }
    emit_newline(&lx);
    add(&lx, T_EOF);
    free(lx.nest);
    return lx.out;
}

const char *tok_kind_name(TokKind k) {
    switch (k) {
        case T_EOF: return "ファイルの終わり";
        case T_NEWLINE: return "改行";
        case T_NUM: return "数値";
        case T_STR: case T_INTERP: return "文字列";
        case T_IDENT: return "識別子";
        case T_LPAREN: return "'('";
        case T_RPAREN: return "')'";
        case T_LBRACKET: return "'['";
        case T_RBRACKET: return "']'";
        case T_LBRACE: return "'{'";
        case T_RBRACE: return "'}'";
        case T_COMMA: return "','";
        case T_DOT: return "'.'";
        case T_COLON: return "':'";
        case T_SEMI: return "';'";
        case T_QUESTION: return "'?'";
        case T_ARROW: return "'=>'";
        case T_ASSIGN: return "'='";
        case T_PLUS_EQ: return "'+='";
        case T_MINUS_EQ: return "'-='";
        case T_STAR_EQ: return "'*='";
        case T_SLASH_EQ: return "'/='";
        case T_PERCENT_EQ: return "'%='";
        case T_PLUS: return "'+'";
        case T_MINUS: return "'-'";
        case T_STAR: return "'*'";
        case T_SLASH: return "'/'";
        case T_SLASHSLASH: return "'//'";
        case T_PERCENT: return "'%'";
        case T_POW: return "'**'";
        case T_EQ: return "'=='";
        case T_NE: return "'!='";
        case T_LT: return "'<'";
        case T_GT: return "'>'";
        case T_LE: return "'<='";
        case T_GE: return "'>='";
        case T_BANG: return "'!'";
        case T_ANDAND: return "'&&'";
        case T_OROR: return "'||'";
        case T_DOTDOT: return "'..'";
        case T_DOTDOTDOT: return "'...'";
        default: break;
    }
    for (int i = 0; keywords[i].word; i++)
        if (keywords[i].kind == k) return keywords[i].quoted;
    return "?";
}
