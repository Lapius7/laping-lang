/* 評価器: AST を直接たどって実行する
 *
 * スコープ: 関数呼び出しごとに新しい環境を作る。if / while などの
 * ブロックは新しいスコープを作らない（旧バージョンと同じ感覚で書ける）。
 *   x = 1       既存の変数があればそれに代入、なければ現在のスコープに作る
 *   let x = 1   常に現在のスコープに新しく作る（外側の同名変数を隠す）
 *
 * 制御フロー: break / continue / return は exec の戻り値で伝える。
 * 例外（実行時エラーと throw）は setjmp / longjmp で try まで飛ぶ。
 */
#include "laping.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_CALL_DEPTH 10000

typedef enum { EX_NORMAL, EX_BREAK, EX_CONTINUE, EX_RETURN } ExecStatus;

typedef struct {
    const char *name;
    int line; /* 呼び出し元の行 */
    int col;
    const char *file;
} CallFrame;

int cur_line = 0;
int cur_col = 0;
const char *cur_file = NULL;
const char *main_file = NULL;
EnvObj *global_env = NULL;
EnvObj *builtin_env = NULL;

static EnvObj *cur_env = NULL;
static EnvObj **env_stack = NULL;
static int env_depth = 0, env_cap = 0;
static CallFrame *frames = NULL;
static int call_depth = 0, frames_cap = 0;

static TryFrame *try_top = NULL;
Value thrown_value;
int thrown_line = 0;
int thrown_col = 0;
const char *thrown_file = NULL;
char thrown_hint[256];
static char pending_hint[256];
static Value ret_val;

/* import したファイル: パスと、as で読み込んだ場合のモジュール（マップ） */
static char **imported = NULL;
static int imported_count = 0;
static MapObj *module_cache = NULL; /* パス -> モジュール（import ... as で作ったマップ） */

/* fn Type.method で追加したメソッド（型名ごと） */
typedef struct {
    const char *type;
    EnvObj *methods; /* メソッド名（インターン済み）-> 関数 */
} MethodSet;
static MethodSet *method_sets = NULL;
static int method_set_count = 0, method_set_cap = 0;

/* record で宣言した型名（インターン済み） */
static const char **record_names = NULL;
static int record_count = 0, record_cap = 0;

static const char *const builtin_type_names[] = {
    "nil", "bool", "number", "string", "list", "map", "function", "range", NULL
};

int test_mode = 0;
TestStats test_stats;

static ExecStatus exec_stmt(Node *s);
static ExecStatus exec_block(Node *b);
static Value eval(Node *n);
static Value eval_soft(Node *n);
static Value call_method(Node *n, Value obj);
static Value eval_comprehension(Node *n);
static int arm_matches(Node *arm, Value subj);

#define SET_POS(n) (cur_line = (n)->line, cur_col = (n)->col)

/* ===================================================================== */
/*  エラー処理                                                            */
/* ===================================================================== */

void interp_mark_roots(void) {
    gc_mark_obj((Obj *)builtin_env);
    gc_mark_obj((Obj *)global_env);
    gc_mark_obj((Obj *)cur_env);
    for (int i = 0; i < env_depth; i++) gc_mark_obj((Obj *)env_stack[i]);
    gc_mark_value(ret_val);
    gc_mark_value(thrown_value);
    gc_mark_obj((Obj *)module_cache);
    for (int i = 0; i < method_set_count; i++) gc_mark_obj((Obj *)method_sets[i].methods);
}

void set_error_hint(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(pending_hint, sizeof(pending_hint), fmt, ap);
    va_end(ap);
}

static void print_frame_location(FILE *out, int line, const char *file) {
    if (!file || file == main_file) fprintf(out, "%d行目", line);
    else fprintf(out, "%s:%d行目", file, line);
}

/* 該当行を表示し、col の位置に ^ を付ける */
static void print_source_snippet(int line, int col, const char *file) {
    size_t len;
    const char *text = source_line(file, line, &len);
    if (!text) return;
    const char *bl = ui_c(1, UI_BLUE), *rs = ui_c(1, UI_RESET), *rd = ui_c(1, UI_RED);
    char num[32];
    int w = snprintf(num, sizeof(num), "%d", line);
    fprintf(stderr, "%s%*s |%s\n", bl, w, "", rs);
    fprintf(stderr, "%s%s |%s ", bl, num, rs);
    /* タブは4つの空白として表示する（^ の位置を合わせるため） */
    for (size_t i = 0; i < len; i++) {
        if (text[i] == '\t') fputs("    ", stderr);
        else fputc(text[i], stderr);
    }
    fputc('\n', stderr);
    if (col > 0 && (size_t)(col - 1) <= len) {
        size_t pad = 0;
        for (size_t i = 0; i < (size_t)(col - 1);) {
            if (text[i] == '\t') {
                pad += 4;
                i++;
                continue;
            }
            size_t cl = utf8_char_len((unsigned char)text[i]);
            if (i + cl > len) cl = len - i;
            pad += display_width(text + i, cl);
            i += cl;
        }
        fprintf(stderr, "%s%*s |%s %*s%s^%s\n", bl, w, "", rs, (int)pad, "", rd, rs);
    }
}

void report_error_at(Value v, int line, int col, const char *file, const char *hint) {
    fflush(stdout);
    StrBuf sb;
    sb_init(&sb);
    value_to_sb(&sb, v, 0);
    const char *msg = sb.buf ? sb.buf : "";
    const char *label = "エラー";
    static const char syntax_prefix[] = "構文エラー: ";
    if (strncmp(msg, syntax_prefix, sizeof(syntax_prefix) - 1) == 0) {
        label = "構文エラー";
        msg += sizeof(syntax_prefix) - 1;
    }
    const char *rd = ui_c(1, UI_RED), *bd = ui_c(1, UI_BOLD), *rs = ui_c(1, UI_RESET);
    const char *bl = ui_c(1, UI_BLUE), *yl = ui_c(1, UI_YELLOW), *gy = ui_c(1, UI_GRAY);
    fprintf(stderr, "%s%s%s%s%s: %s%s\n", rd, bd, label, rs, bd, msg, rs);
    fprintf(stderr, "  %s-->%s %s:%d", bl, rs, file ? file : "<不明>", line);
    if (col > 0) fprintf(stderr, ":%d", col);
    fputc('\n', stderr);
    print_source_snippet(line, col, file);
    if (hint && hint[0]) fprintf(stderr, "  %sヒント:%s %s\n", yl, rs, hint);
    sb_free(&sb);

    /* 呼び出し履歴（新しいものから最大10件。同じ呼び出しの連続はまとめる） */
    if (call_depth == 0) return;
    fprintf(stderr, "  %s呼び出し履歴:%s\n", gy, rs);
    int shown = 0;
    int i = call_depth - 1;
    while (i >= 0 && shown < 10) {
        int j = i;
        while (j > 0 && frames[j - 1].name == frames[i].name && frames[j - 1].line == frames[i].line &&
               frames[j - 1].file == frames[i].file)
            j--;
        fprintf(stderr, "    %s%s()%s ← ", bd, frames[i].name ? frames[i].name : "<無名関数>", rs);
        print_frame_location(stderr, frames[i].line, frames[i].file);
        if (i - j > 0) fprintf(stderr, " %s（同じ呼び出しが %d 回続いています）%s", gy, i - j + 1, rs);
        fputc('\n', stderr);
        shown++;
        i = j - 1;
    }
    if (i >= 0) fprintf(stderr, "    %s... (他 %d 件)%s\n", gy, i + 1, rs);
}

void report_error(Value v, int line, const char *file) { report_error_at(v, line, 0, file, NULL); }

void try_push(TryFrame *tf) {
    tf->prev = try_top;
    tf->root_depth = root_depth();
    tf->env_depth = env_depth;
    tf->call_depth = call_depth;
    tf->env = cur_env;
    tf->file = cur_file;
    try_top = tf;
}

void try_pop(TryFrame *tf) { try_top = tf->prev; }

void try_restore(TryFrame *tf) {
    try_top = tf->prev;
    root_restore(tf->root_depth);
    env_depth = tf->env_depth;
    call_depth = tf->call_depth;
    cur_env = tf->env;
    cur_file = tf->file;
}

void throw_value(Value v) {
    thrown_value = v;
    thrown_line = cur_line;
    thrown_col = cur_col;
    thrown_file = cur_file;
    snprintf(thrown_hint, sizeof(thrown_hint), "%s", pending_hint);
    pending_hint[0] = '\0';
    if (try_top) longjmp(try_top->buf, 1);
    report_error_at(v, thrown_line, thrown_col, thrown_file, thrown_hint);
    exit(1);
}

void rt_error(const char *fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    throw_value(v_cstr(buf));
}

void syntax_error(int line, const char *file, const char *fmt, ...) {
    char buf[1024];
    int n = snprintf(buf, sizeof(buf), "構文エラー: ");
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf + n, sizeof(buf) - (size_t)n, fmt, ap);
    va_end(ap);
    cur_line = line;
    cur_col = syntax_col;
    syntax_col = 0;
    cur_file = file ? file : cur_parse_file;
    int saved_depth = call_depth;
    call_depth = 0; /* 構文エラーに呼び出し履歴は不要 */
    if (!try_top) {
        report_error_at(v_cstr(buf), line, cur_col, cur_file, pending_hint);
        exit(1);
    }
    call_depth = saved_depth;
    throw_value(v_cstr(buf));
}

/* ===================================================================== */
/*  「もしかして」候補                                                    */
/* ===================================================================== */

static size_t edit_distance(const char *a, const char *b) {
    size_t la = strlen(a), lb = strlen(b);
    if (la > 64 || lb > 64) return 99;
    size_t prev[65], cur[65];
    for (size_t j = 0; j <= lb; j++) prev[j] = j;
    for (size_t i = 1; i <= la; i++) {
        cur[0] = i;
        for (size_t j = 1; j <= lb; j++) {
            size_t cost = a[i - 1] == b[j - 1] ? 0 : 1;
            size_t best = prev[j] + 1;
            if (cur[j - 1] + 1 < best) best = cur[j - 1] + 1;
            if (prev[j - 1] + cost < best) best = prev[j - 1] + cost;
            cur[j] = best;
        }
        memcpy(prev, cur, sizeof(size_t) * (lb + 1));
    }
    return prev[lb];
}

typedef struct {
    const char *best;
    size_t dist;
    const char *target;
} Suggest;

static void suggest_consider(Suggest *sg, const char *name) {
    if (!name || strcmp(name, sg->target) == 0) return;
    size_t d = edit_distance(sg->target, name);
    size_t limit = strlen(sg->target) <= 3 ? 1 : strlen(sg->target) <= 6 ? 2 : 3;
    if (d <= limit && d < sg->dist) {
        sg->best = name;
        sg->dist = d;
    }
}

static void suggest_env(Suggest *sg, EnvObj *e) {
    for (; e; e = e->parent)
        for (size_t i = 0; i < e->cap; i++) suggest_consider(sg, e->names[i]);
}

static void suggest_map_keys(Suggest *sg, MapObj *m) {
    if (!m) return;
    for (size_t i = 0; i < m->count; i++)
        if (!m->entries[i].deleted && IS_STR(m->entries[i].key)) suggest_consider(sg, AS_STR(m->entries[i].key)->chars);
}

/* ===================================================================== */
/* ===================================================================== */
/*  変数                                                                  */
/* ===================================================================== */

LP_NORETURN static void undefined_var(const char *name) {
    Suggest sg = {NULL, 99, name};
    suggest_env(&sg, cur_env);
    if (sg.best) set_error_hint("もしかして '%s' ですか？", sg.best);
    rt_error("未定義の変数 '%s'", name);
}

static Value lookup(const char *name) {
    Value *v = env_find(cur_env, name);
    if (!v) undefined_var(name);
    return *v;
}

/* 全部大文字（2文字以上）の名前は定数として扱い、再代入を禁止する。
 * キーワードを増やさずに「変えてはいけない値」を見分けられる */
static int is_constant_name(const char *name) {
    int letters = 0;
    size_t n = strlen(name);
    if (n < 2) return 0;
    for (size_t i = 0; i < n; i++) {
        char c = name[i];
        if (c >= 'A' && c <= 'Z') letters++;
        else if (!(c == '_' || (c >= '0' && c <= '9'))) return 0;
    }
    return letters > 0;
}

/* 既存の変数（組み込みを除く）があれば更新、なければ現在のスコープに作る */
static void set_var(const char *name, Value v) {
    for (EnvObj *e = cur_env; e && e != builtin_env; e = e->parent) {
        Value *slot = env_find_local(e, name);
        if (slot) {
            if (is_constant_name(name)) {
                set_error_hint("全部大文字の名前は定数です。変更する値には小文字の名前を使ってください");
                rt_error("定数 %s は変更できません", name);
            }
            *slot = v;
            return;
        }
    }
    env_define(cur_env, name, v);
}

static void push_env(EnvObj *e) {
    if (env_depth >= env_cap) {
        env_cap = env_cap ? env_cap * 2 : 64;
        env_stack = xrealloc(env_stack, sizeof(EnvObj *) * env_cap);
    }
    env_stack[env_depth++] = cur_env;
    cur_env = e;
}

static void pop_env(void) { cur_env = env_stack[--env_depth]; }

/* ===================================================================== */
/*  演算                                                                  */
/* ===================================================================== */

static const char *op_name(int op) {
    switch (op) {
        case T_PLUS: return "+";
        case T_MINUS: return "-";
        case T_STAR: return "*";
        case T_SLASH: return "/";
        case T_SLASHSLASH: return "//";
        case T_PERCENT: return "%";
        case T_POW: return "**";
        case T_EQ: return "==";
        case T_NE: return "!=";
        case T_LT: return "<";
        case T_GT: return ">";
        case T_LE: return "<=";
        case T_GE: return ">=";
        case T_IN: return "in";
        default: return "?";
    }
}

static double to_index(Value v, const char *what) {
    if (!IS_NUM(v)) rt_error("%sの添字には数値が必要です（%s が渡されました）", what, type_name(v));
    if (v.as.n != floor(v.as.n)) rt_error("%sの添字は整数でなければなりません", what);
    return v.as.n;
}

static int str_contains(StrObj *hay, StrObj *needle) {
    if (needle->len == 0) return 1;
    if (needle->len > hay->len) return 0;
    for (size_t i = 0; i + needle->len <= hay->len; i++)
        if (memcmp(hay->chars + i, needle->chars, needle->len) == 0) return 1;
    return 0;
}

static int range_contains(RangeObj *r, double x) {
    if (r->step == 0) return 0;
    double k = (x - r->start) / r->step;
    if (k < 0 || k != floor(k)) return 0;
    size_t n;
    if (!range_len(r, &n)) return 0;
    return k < (double)n;
}

static int compare_values(int op, Value l, Value r) {
    int c;
    if (IS_NUM(l) && IS_NUM(r)) {
        double a = l.as.n, b = r.as.n;
        switch (op) {
            case T_LT: return a < b;
            case T_GT: return a > b;
            case T_LE: return a <= b;
            default: return a >= b;
        }
    }
    if (IS_STR(l) && IS_STR(r)) {
        StrObj *a = AS_STR(l), *b = AS_STR(r);
        size_t n = a->len < b->len ? a->len : b->len;
        c = memcmp(a->chars, b->chars, n);
        if (c == 0) c = a->len < b->len ? -1 : a->len > b->len ? 1 : 0;
    } else {
        rt_error("演算子 '%s' で %s と %s は比較できません", op_name(op), type_name(l), type_name(r));
        return 0;
    }
    switch (op) {
        case T_LT: return c < 0;
        case T_GT: return c > 0;
        case T_LE: return c <= 0;
        default: return c >= 0;
    }
}

static Value repeat_value(Value seq, Value count) {
    double n = count.as.n;
    if (n < 0 || n != floor(n)) rt_error("繰り返し回数は0以上の整数でなければなりません");
    if (IS_STR(seq)) {
        StrObj *s = AS_STR(seq);
        if (s->len * n > 1e9) rt_error("文字列が大きすぎます");
        StrBuf sb;
        sb_init(&sb);
        for (long i = 0; i < (long)n; i++) sb_append(&sb, s->chars, s->len);
        return sb_to_value(&sb);
    }
    ListObj *src = AS_LIST(seq);
    if (src->count * n > 1e8) rt_error("リストが大きすぎます");
    ListObj *l = list_new(src->count * (size_t)n);
    for (long i = 0; i < (long)n; i++)
        for (size_t j = 0; j < src->count; j++) list_push(l, src->items[j]);
    return v_obj(VAL_LIST, l);
}

static Value binop(int op, Value l, Value r) {
    switch (op) {
        case T_EQ: return v_bool(values_equal(l, r));
        case T_NE: return v_bool(!values_equal(l, r));
        case T_LT: case T_GT: case T_LE: case T_GE:
            return v_bool(compare_values(op, l, r));
        case T_IN:
            switch (r.type) {
                case VAL_LIST: {
                    ListObj *lst = AS_LIST(r);
                    for (size_t i = 0; i < lst->count; i++)
                        if (values_equal(l, lst->items[i])) return v_bool(1);
                    return v_bool(0);
                }
                case VAL_MAP:
                    return v_bool(is_hashable(l) && map_get(AS_MAP(r), l, NULL));
                case VAL_STR:
                    if (!IS_STR(l)) rt_error("文字列に対する 'in' の左辺には文字列が必要です");
                    return v_bool(str_contains(AS_STR(r), AS_STR(l)));
                case VAL_RANGE:
                    return v_bool(IS_NUM(l) && range_contains(AS_RANGE(r), l.as.n));
                default:
                    rt_error("'in' の右辺には リスト・マップ・文字列・範囲 が必要です（%s が渡されました）", type_name(r));
            }
            break;
        case T_PLUS:
            if (IS_NUM(l) && IS_NUM(r)) return v_num(l.as.n + r.as.n);
            if (IS_STR(l) && IS_STR(r)) {
                StrObj *a = AS_STR(l), *b = AS_STR(r);
                StrBuf sb;
                sb_init(&sb);
                sb_append(&sb, a->chars, a->len);
                sb_append(&sb, b->chars, b->len);
                return sb_to_value(&sb);
            }
            if (IS_LIST(l) && IS_LIST(r)) {
                ListObj *a = AS_LIST(l), *b = AS_LIST(r);
                ListObj *res = list_new(a->count + b->count);
                for (size_t i = 0; i < a->count; i++) list_push(res, a->items[i]);
                for (size_t i = 0; i < b->count; i++) list_push(res, b->items[i]);
                return v_obj(VAL_LIST, res);
            }
            if ((IS_STR(l) && IS_NUM(r)) || (IS_NUM(l) && IS_STR(r)))
                rt_error("'+' で文字列と数値は連結できません（str() で変換するか \"{x}\" の埋め込みを使ってください）");
            rt_error("'+' は %s と %s の間では使えません", type_name(l), type_name(r));
            break;
        case T_STAR:
            if (IS_NUM(l) && IS_NUM(r)) return v_num(l.as.n * r.as.n);
            if ((IS_STR(l) || IS_LIST(l)) && IS_NUM(r)) return repeat_value(l, r);
            if (IS_NUM(l) && (IS_STR(r) || IS_LIST(r))) return repeat_value(r, l);
            rt_error("'*' は %s と %s の間では使えません", type_name(l), type_name(r));
            break;
        default:
            break;
    }
    if (!IS_NUM(l) || !IS_NUM(r))
        rt_error("演算子 '%s' には数値が必要です（%s と %s が渡されました）", op_name(op), type_name(l), type_name(r));
    double a = l.as.n, b = r.as.n;
    switch (op) {
        case T_MINUS: return v_num(a - b);
        case T_SLASH:
            if (b == 0) rt_error("0で除算しました");
            return v_num(a / b);
        case T_SLASHSLASH:
            if (b == 0) rt_error("0で除算しました");
            return v_num(floor(a / b));
        case T_PERCENT: {
            if (b == 0) rt_error("0で剰余演算しました");
            double m = fmod(a, b);
            if (m != 0 && ((m < 0) != (b < 0))) m += b; /* 結果の符号は除数に合わせる */
            return v_num(m);
        }
        case T_POW: return v_num(pow(a, b));
    }
    rt_error("未知の演算子です");
    return v_nil();
}

/* ===================================================================== */
/*  添字・フィールド                                                      */
/* ===================================================================== */

static Value str_char_at(StrObj *s, long idx) {
    size_t clen = utf8_len(s->chars, s->len);
    if (idx < 0) idx += (long)clen;
    if (idx < 0 || (size_t)idx >= clen) rt_error("文字列の添字 %ld が範囲外です（長さ %zu）", idx, clen);
    size_t off = utf8_offset(s->chars, s->len, (size_t)idx);
    size_t w = utf8_char_len((unsigned char)s->chars[off]);
    if (off + w > s->len) w = s->len - off;
    return v_str(s->chars + off, w);
}

static Value index_get(Value obj, Value idx) {
    switch (obj.type) {
        case VAL_LIST: {
            ListObj *l = AS_LIST(obj);
            double d = to_index(idx, "リスト");
            if (d < 0) d += (double)l->count;
            if (d < 0 || d >= (double)l->count)
                rt_error("リストの添字 %g が範囲外です（長さ %zu）", idx.as.n, l->count);
            return l->items[(size_t)d];
        }
        case VAL_STR:
            return str_char_at(AS_STR(obj), (long)to_index(idx, "文字列"));
        case VAL_MAP: {
            Value out;
            if (!is_hashable(idx)) rt_error("マップのキーには 数値・文字列・真偽値・nil のみ使えます");
            if (!map_get(AS_MAP(obj), idx, &out)) {
                StrBuf sb;
                sb_init(&sb);
                value_to_sb(&sb, idx, 1);
                char msg[256];
                snprintf(msg, sizeof(msg), "キー %s が存在しません", sb.buf);
                sb_free(&sb);
                rt_error("%s", msg);
            }
            return out;
        }
        case VAL_RANGE: {
            RangeObj *r = AS_RANGE(obj);
            size_t n = 0;
            range_len(r, &n);
            double d = to_index(idx, "範囲");
            if (d < 0) d += (double)n;
            if (d < 0 || d >= (double)n) rt_error("範囲の添字 %g が範囲外です（長さ %zu）", idx.as.n, n);
            return v_num(range_at(r, (size_t)d));
        }
        default:
            rt_error("%s には添字 [] でアクセスできません", type_name(obj));
    }
    return v_nil();
}

static void index_set(Value obj, Value idx, Value v) {
    switch (obj.type) {
        case VAL_LIST: {
            ListObj *l = AS_LIST(obj);
            double d = to_index(idx, "リスト");
            if (d < 0) d += (double)l->count;
            if (d < 0 || d >= (double)l->count)
                rt_error("リストの添字 %g が範囲外です（長さ %zu）。要素の追加には push を使ってください", idx.as.n, l->count);
            l->items[(size_t)d] = v;
            return;
        }
        case VAL_MAP:
            if (!is_hashable(idx)) rt_error("マップのキーには 数値・文字列・真偽値・nil のみ使えます");
            map_set(AS_MAP(obj), idx, v);
            return;
        case VAL_STR:
            rt_error("文字列は変更できません");
            break;
        default:
            rt_error("%s には添字 [] で代入できません", type_name(obj));
    }
}

static Value field_key(Node *n) {
    if (n->constant.type != VAL_STR) n->constant = v_obj(VAL_STR, str_new_perm(n->name, strlen(n->name)));
    return n->constant;
}

static int is_record_name(const char *name) {
    for (int i = 0; i < record_count; i++)
        if (strcmp(record_names[i], name) == 0) return 1;
    return 0;
}

static int is_known_type(const char *name) {
    for (int i = 0; builtin_type_names[i]; i++)
        if (strcmp(builtin_type_names[i], name) == 0) return 1;
    return is_record_name(name);
}

LP_NORETURN static void unknown_type(const char *name) {
    Suggest sg = {NULL, 99, name};
    for (int i = 0; builtin_type_names[i]; i++) suggest_consider(&sg, builtin_type_names[i]);
    for (int i = 0; i < record_count; i++) suggest_consider(&sg, record_names[i]);
    if (sg.best) set_error_hint("もしかして '%s' ですか？", sg.best);
    else set_error_hint("使える型名: nil bool number string list map function range と、record で宣言した名前");
    rt_error("不明な型名 '%s'", name);
}

static EnvObj *methods_for(const char *type, int create) {
    for (int i = 0; i < method_set_count; i++)
        if (strcmp(method_sets[i].type, type) == 0) return method_sets[i].methods;
    if (!create) return NULL;
    if (method_set_count >= method_set_cap) {
        method_set_cap = method_set_cap ? method_set_cap * 2 : 8;
        method_sets = xrealloc(method_sets, sizeof(MethodSet) * (size_t)method_set_cap);
    }
    method_sets[method_set_count].type = intern(type, strlen(type));
    method_sets[method_set_count].methods = env_new(NULL);
    return method_sets[method_set_count++].methods;
}

LP_NORETURN static void missing_field(Node *n, MapObj *m) {
    Suggest sg = {NULL, 99, n->name};
    suggest_map_keys(&sg, m);
    if (sg.best) set_error_hint("もしかして '%s' ですか？", sg.best);
    if (m->tag) rt_error("%s にフィールド '%s' はありません", m->tag, n->name);
    rt_error("キー \"%s\" が存在しません", n->name);
}

static Value field_get(Node *n, Value obj) {
    if (!IS_MAP(obj)) rt_error("'.%s' でフィールドを参照できるのはマップだけです（%s が渡されました）", n->name, type_name(obj));
    Value out;
    if (!map_get(AS_MAP(obj), field_key(n), &out)) missing_field(n, AS_MAP(obj));
    return out;
}

static void field_set(Node *n, Value obj, Value v) {
    if (!IS_MAP(obj)) rt_error("'.%s' でフィールドに代入できるのはマップだけです（%s が渡されました）", n->name, type_name(obj));
    /* record の値には宣言したフィールドしか作れない（打ち間違いを防ぐ） */
    if (AS_MAP(obj)->tag && !map_get(AS_MAP(obj), field_key(n), NULL)) missing_field(n, AS_MAP(obj));
    map_set(AS_MAP(obj), field_key(n), v);
}

/* ===================================================================== */
/*  関数呼び出し                                                          */
/* ===================================================================== */

static void push_frame(const char *name) {
    if (call_depth >= MAX_CALL_DEPTH) rt_error("関数呼び出しが深すぎます（再帰が止まっていない可能性があります）");
    if (call_depth >= frames_cap) {
        frames_cap = frames_cap ? frames_cap * 2 : 64;
        frames = xrealloc(frames, sizeof(CallFrame) * frames_cap);
    }
    frames[call_depth].name = name;
    frames[call_depth].line = cur_line;
    frames[call_depth].col = cur_col;
    frames[call_depth].file = cur_file;
    call_depth++;
}

Value call_value(Value callee, int argc, Value *argv) {
    int saved_line = cur_line;
    int saved_col = cur_col;
    const char *saved_file = cur_file;

    if (callee.type == VAL_BUILTIN) {
        const Builtin *b = callee.as.bi;
        if (argc < b->min_args || (b->max_args >= 0 && argc > b->max_args)) {
            if (b->min_args == b->max_args)
                rt_error("%s() の引数は %d 個です（%d 個渡されました）", b->name, b->min_args, argc);
            else if (b->max_args < 0)
                rt_error("%s() の引数は %d 個以上です（%d 個渡されました）", b->name, b->min_args, argc);
            else
                rt_error("%s() の引数は %d〜%d 個です（%d 個渡されました）", b->name, b->min_args, b->max_args, argc);
        }
        const char *prev = builtin_set_current(b->name);
        Value r = b->fn(argc, argv);
        builtin_set_current(prev);
        cur_line = saved_line;
        cur_col = saved_col;
        cur_file = saved_file;
        return r;
    }
    if (callee.type != VAL_FUNC) rt_error("%s は関数ではないので呼び出せません", type_name(callee));

    FuncObj *f = AS_FUNC(callee);
    Node *d = f->decl;
    const char *fname = d->name ? d->name : "<無名関数>";
    int required = 0;
    for (int i = 0; i < d->nparams; i++)
        if (!d->defaults[i] && !(d->variadic && i == d->nparams - 1)) required++;
    int fixed = d->variadic ? d->nparams - 1 : d->nparams;
    if (argc < required)
        rt_error("%s() の引数が足りません（%d 個必要ですが %d 個渡されました）", fname, required, argc);
    if (!d->variadic && argc > d->nparams)
        rt_error("%s() の引数が多すぎます（最大 %d 個ですが %d 個渡されました）", fname, d->nparams, argc);

    push_frame(d->name);
    EnvObj *env = env_new(f->closure);
    push_env(env);
    for (int i = 0; i < fixed; i++) {
        Value v;
        if (i < argc) v = argv[i];
        else v = eval(d->defaults[i]); /* 既に束縛した引数を参照できる */
        env_define(env, d->params[i], v);
    }
    if (d->variadic) {
        ListObj *rest = list_new(argc > fixed ? (size_t)(argc - fixed) : 0);
        for (int i = fixed; i < argc; i++) list_push(rest, argv[i]);
        env_define(env, d->params[fixed], v_obj(VAL_LIST, rest));
    }

    /* 式本体（=>）の関数は exec_stmt を通らないので、ここも GC の安全点にする。
     * 引数は呼び出し側で root 済み、新しい環境は環境スタックに積んである */
    gc_maybe_collect();

    Value result;
    if (d->kind == N_RECORD) {
        /* record のコンストラクタ: 引数をフィールドにしたマップを作る */
        if (d->list.count == 0)
            for (int i = 0; i < d->nparams; i++) {
                Node *k = xcalloc(1, sizeof(Node));
                k->kind = N_STR;
                k->constant = v_obj(VAL_STR, str_new_perm(d->params[i], strlen(d->params[i])));
                d->list.items = xrealloc(d->list.items, sizeof(Node *) * (size_t)(i + 1));
                d->list.items[i] = k;
                d->list.count = d->list.cap = i + 1;
            }
        MapObj *m = map_new();
        m->tag = d->name;
        for (int i = 0; i < d->nparams; i++)
            map_set(m, d->list.items[i]->constant, *env_find_local(env, d->params[i]));
        result = v_obj(VAL_MAP, m);
    } else if (d->op == 1) {
        result = eval(d->a);
    } else {
        ExecStatus st = exec_block(d->a);
        if (st == EX_RETURN) result = ret_val;
        else if (st == EX_BREAK || st == EX_CONTINUE)
            rt_error("ループの外で %s は使えません", st == EX_BREAK ? "break" : "continue");
        else result = v_nil();
    }
    ret_val = v_nil();
    pop_env();
    call_depth--;
    cur_line = saved_line;
    cur_col = saved_col;
    cur_file = saved_file;
    return result;
}

static Value eval_call_args(Node *call, Value callee, Value *self) {
    int extra = self ? 1 : 0;
    int argc = call->list.count + extra;
    size_t base = root_depth();
    root_push(callee);
    if (self) root_push(*self);
    for (int i = 0; i < call->list.count; i++) root_push(eval(call->list.items[i]));

    Value small[8];
    Value *argv = argc <= 8 ? small : xmalloc(sizeof(Value) * (size_t)argc);
    if (self) argv[0] = *self;
    /* root スタックは再確保されうるので、評価後に値だけコピーする */
    for (int i = 0; i < call->list.count; i++) argv[i + extra] = root_get(base + 1 + (size_t)extra + (size_t)i);
    SET_POS(call);
    cur_file = call->file;
    Value r = call_value(callee, argc, argv);
    if (argv != small) free(argv);
    root_restore(base);
    return r;
}

/* x.f(a) の呼び出し先を決める。
 *   1. fn Type.f で x の型に追加したメソッド
 *   2. x がマップで、キー f に関数が入っていればそれ
 *   3. どちらでもなければ f(x, a)（組み込み関数もユーザー関数も使える） */
static Value call_method(Node *n, Value obj) {
    root_push(obj);
    Value r;
    Value callee;
    EnvObj *ms = methods_for(value_type_name(obj), 0);
    Value *mv = ms ? env_find_local(ms, n->name) : NULL;
    if (mv) {
        r = eval_call_args(n, *mv, &obj);
    } else if (IS_MAP(obj) && map_get(AS_MAP(obj), field_key(n), &callee)) {
        r = eval_call_args(n, callee, NULL);
    } else {
        Value *fv = env_find(cur_env, n->name);
        if (!fv || !IS_CALLABLE(*fv)) {
            SET_POS(n);
            Suggest sg = {NULL, 99, n->name};
            suggest_env(&sg, cur_env);
            if (ms)
                for (size_t i = 0; i < ms->cap; i++) suggest_consider(&sg, ms->names[i]);
            if (IS_MAP(obj)) suggest_map_keys(&sg, AS_MAP(obj));
            if (sg.best) set_error_hint("もしかして '%s' ですか？", sg.best);
            if (IS_MAP(obj) && !AS_MAP(obj)->tag) rt_error("マップにキー \"%s\" がなく、同名の関数もありません", n->name);
            rt_error("%s に対するメソッド '%s' が見つかりません", value_type_name(obj), n->name);
        }
        r = eval_call_args(n, *fv, &obj);
    }
    root_pop(1);
    return r;
}

/* 「値 else 既定値」の左辺用。途中が nil、キーや添字が存在しない場合は
 * エラーにせず nil を返す。未定義の変数（書き間違い）は通常どおりエラー */
static Value eval_soft(Node *n) {
    switch (n->kind) {
        case N_FIELD: {
            Value obj = eval_soft(n->a);
            if (IS_NIL(obj)) return v_nil();
            if (IS_MAP(obj)) {
                Value out;
                if (map_get(AS_MAP(obj), field_key(n), &out)) return out;
                return v_nil();
            }
            SET_POS(n);
            return field_get(n, obj);
        }
        case N_INDEX: {
            Value obj = eval_soft(n->a);
            root_push(obj);
            Value idx = eval(n->b);
            root_pop(1);
            if (IS_NIL(obj)) return v_nil();
            SET_POS(n);
            if (IS_MAP(obj)) {
                Value out;
                if (is_hashable(idx) && map_get(AS_MAP(obj), idx, &out)) return out;
                return v_nil();
            }
            if ((IS_LIST(obj) || IS_STR(obj)) && IS_NUM(idx) && idx.as.n == floor(idx.as.n)) {
                double len = IS_LIST(obj) ? (double)AS_LIST(obj)->count : (double)utf8_len(AS_STR(obj)->chars, AS_STR(obj)->len);
                double d = idx.as.n < 0 ? idx.as.n + len : idx.as.n;
                if (d < 0 || d >= len) return v_nil();
            }
            return index_get(obj, idx);
        }
        case N_METHOD: {
            Value obj = eval_soft(n->a);
            if (IS_NIL(obj)) return v_nil();
            return call_method(n, obj);
        }
        default:
            return eval(n);
    }
}

/* for / 内包表記で使う反復子。
 * a は添字（マップならキー）、b は要素（マップなら値） */
typedef struct {
    Value it;
    size_t i;
    size_t off;
} Iter;

static void iter_check(Value it) {
    if (!IS_RANGE(it) && !IS_LIST(it) && !IS_STR(it) && !IS_MAP(it))
        rt_error("for で繰り返せるのは リスト・範囲・文字列・マップ です（%s が渡されました）", type_name(it));
    if (IS_RANGE(it)) {
        size_t n;
        if (!range_len(AS_RANGE(it), &n)) rt_error("範囲が大きすぎるか、step が 0 です");
    }
}

static int iter_next(Iter *t, Value *a, Value *b) {
    switch (t->it.type) {
        case VAL_RANGE: {
            size_t n = 0;
            range_len(AS_RANGE(t->it), &n);
            if (t->i >= n) return 0;
            *a = v_num((double)t->i);
            *b = v_num(range_at(AS_RANGE(t->it), t->i));
            t->i++;
            return 1;
        }
        case VAL_LIST: {
            ListObj *l = AS_LIST(t->it);
            if (t->i >= l->count) return 0;
            *a = v_num((double)t->i);
            *b = l->items[t->i];
            t->i++;
            return 1;
        }
        case VAL_STR: {
            StrObj *str = AS_STR(t->it);
            if (t->off >= str->len) return 0;
            size_t w = utf8_char_len((unsigned char)str->chars[t->off]);
            if (t->off + w > str->len) w = str->len - t->off;
            *a = v_num((double)t->i);
            *b = v_str(str->chars + t->off, w);
            t->off += w;
            t->i++;
            return 1;
        }
        case VAL_MAP: {
            MapObj *m = AS_MAP(t->it);
            while (t->i < m->count && m->entries[t->i].deleted) t->i++;
            if (t->i >= m->count) return 0;
            *a = m->entries[t->i].key;
            *b = m->entries[t->i].val;
            t->i++;
            return 1;
        }
        default:
            return 0;
    }
}

static void bind_iter_vars(Node *s, Value it, Value a, Value b) {
    if (s->nparams == 1) env_define(cur_env, s->params[0], IS_MAP(it) ? a : b);
    else {
        env_define(cur_env, s->params[0], a);
        env_define(cur_env, s->params[1], b);
    }
}

/* [式 for x in xs if 条件] / {キー: 値 for x in xs} */
static Value eval_comprehension(Node *n) {
    Value it = eval(n->b);
    root_push(it);
    SET_POS(n);
    iter_check(it);
    Value res = n->op ? v_map() : v_list(0);
    root_push(res);
    push_env(env_new(cur_env)); /* ループ変数を外に漏らさない */
    Iter t = {it, 0, 0};
    Value a, b;
    while (iter_next(&t, &a, &b)) {
        bind_iter_vars(n, it, a, b);
        if (n->c && !is_truthy(eval(n->c))) continue;
        if (n->op) {
            Value k = eval(n->a);
            if (!is_hashable(k)) {
                SET_POS(n->a);
                rt_error("マップのキーには 数値・文字列・真偽値・nil のみ使えます");
            }
            root_push(k);
            Value v = eval(n->d);
            root_pop(1);
            map_set(AS_MAP(res), k, v);
        } else {
            list_push(AS_LIST(res), eval(n->a));
        }
    }
    pop_env();
    root_pop(2);
    return res;
}

static int arm_matches(Node *arm, Value subj) {
    int matched = arm->op;
    for (int j = 0; !matched && j < arm->list.count; j++) {
        Value pv = eval(arm->list.items[j]);
        if (IS_RANGE(pv) && IS_NUM(subj)) matched = range_contains(AS_RANGE(pv), subj.as.n);
        else matched = values_equal(subj, pv);
    }
    if (matched && arm->a) matched = is_truthy(eval(arm->a));
    return matched;
}

/* ===================================================================== */
/*  式の評価                                                              */
/* ===================================================================== */

static Value eval(Node *n) {
    switch (n->kind) {
        case N_NUM: return v_num(n->num);
        case N_STR: return n->constant;
        case N_BOOL: return v_bool(n->op);
        case N_NIL: return v_nil();
        case N_IDENT: {
            Value *v = env_find(cur_env, n->name);
            if (!v) {
                SET_POS(n);
                undefined_var(n->name);
            }
            return *v;
        }
        case N_FALLBACK: {
            Value v = eval_soft(n->a);
            if (!IS_NIL(v)) return v;
            return eval(n->b);
        }
        case N_CMPCHAIN: {
            size_t base = root_depth();
            Value l = eval(n->list.items[0]);
            root_push(l);
            for (int i = 0; i < n->list2.count; i++) {
                Value r = eval(n->list.items[i + 1]);
                root_push(r);
                SET_POS(n->list2.items[i]);
                if (!is_truthy(binop(n->list2.items[i]->op, l, r))) {
                    root_restore(base);
                    return v_bool(0);
                }
                l = r;
            }
            root_restore(base);
            return v_bool(1);
        }
        case N_IS: {
            Value v = eval(n->a);
            SET_POS(n);
            if (!is_known_type(n->name)) unknown_type(n->name);
            int ok = strcmp(type_name(v), n->name) == 0 || strcmp(value_type_name(v), n->name) == 0;
            return v_bool(n->op ? !ok : ok);
        }
        case N_COMPREHENSION:
            return eval_comprehension(n);
        case N_THROW: {
            Value v = eval(n->a);
            SET_POS(n);
            throw_value(v);
        }
        case N_MATCH_EXPR: {
            Value subj = eval(n->a);
            root_push(subj);
            for (int i = 0; i < n->list.count; i++) {
                Node *arm = n->list.items[i];
                if (arm_matches(arm, subj)) {
                    Value r = eval(arm->b);
                    root_pop(1);
                    return r;
                }
            }
            root_pop(1);
            return v_nil();
        }
        case N_INTERP: {
            StrBuf sb;
            sb_init(&sb);
            for (int i = 0; i < n->list.count; i++) {
                Node *p = n->list.items[i];
                Node *spec0 = i < n->list2.count ? n->list2.items[i] : NULL;
                if (p->kind == N_STR && !spec0) {
                    StrObj *s = AS_STR(p->constant);
                    sb_append(&sb, s->chars, s->len);
                } else {
                    Value v = eval(p);
                    Node *spec = i < n->list2.count ? n->list2.items[i] : NULL;
                    if (spec) {
                        StrObj *sp = AS_STR(spec->constant);
                        apply_format_spec(&sb, v, sp->chars, sp->len);
                    } else {
                        value_to_sb(&sb, v, 0);
                    }
                }
            }
            return sb_to_value(&sb);
        }
        case N_LIST: {
            ListObj *l = list_new((size_t)n->list.count);
            root_push(v_obj(VAL_LIST, l));
            for (int i = 0; i < n->list.count; i++) list_push(l, eval(n->list.items[i]));
            root_pop(1);
            return v_obj(VAL_LIST, l);
        }
        case N_MAP: {
            MapObj *m = map_new();
            root_push(v_obj(VAL_MAP, m));
            for (int i = 0; i < n->list.count; i++) {
                Value k = eval(n->list.items[i]);
                if (!is_hashable(k)) {
                    SET_POS(n->list.items[i]);
                    rt_error("マップのキーには 数値・文字列・真偽値・nil のみ使えます");
                }
                root_push(k);
                Value v = eval(n->list2.items[i]);
                root_pop(1);
                map_set(m, k, v);
            }
            root_pop(1);
            return v_obj(VAL_MAP, m);
        }
        case N_FUNC:
            return v_obj(VAL_FUNC, func_new(n, cur_env));
        case N_UNARY: {
            Value v = eval(n->a);
            if (n->op == T_NOT) return v_bool(!is_truthy(v));
            if (!IS_NUM(v)) {
                SET_POS(n);
                rt_error("単項 '-' には数値が必要です（%s が渡されました）", type_name(v));
            }
            return v_num(-v.as.n);
        }
        case N_BINARY: {
            Value l = eval(n->a);
            root_push(l);
            Value r = eval(n->b);
            root_pop(1);
            SET_POS(n);
            return binop(n->op, l, r);
        }
        case N_AND: {
            Value l = eval(n->a);
            if (!is_truthy(l)) return l;
            return eval(n->b);
        }
        case N_OR: {
            Value l = eval(n->a);
            if (is_truthy(l)) return l;
            return eval(n->b);
        }
        case N_TERNARY:
            return is_truthy(eval(n->a)) ? eval(n->b) : eval(n->c);
        case N_RANGE: {
            Value a = eval(n->a);
            Value b = eval(n->b);
            if (!IS_NUM(a) || !IS_NUM(b)) {
                SET_POS(n);
                rt_error("範囲 '..' の両端には数値が必要です");
            }
            return v_obj(VAL_RANGE, range_new(a.as.n, b.as.n, 1, n->op));
        }
        case N_CALL: {
            Value callee = eval(n->a);
            return eval_call_args(n, callee, NULL);
        }
        case N_METHOD:
            return call_method(n, eval(n->a));
        case N_INDEX: {
            Value obj = eval(n->a);
            root_push(obj);
            Value idx = eval(n->b);
            root_pop(1);
            SET_POS(n);
            return index_get(obj, idx);
        }
        case N_FIELD: {
            Value obj = eval(n->a);
            SET_POS(n);
            return field_get(n, obj);
        }
        default:
            SET_POS(n);
            rt_error("ここに文は書けません");
    }
    return v_nil();
}

/* ===================================================================== */
/*  文の実行                                                              */
/* ===================================================================== */

/* v はこの関数の中で root される */
static void assign_to(Node *t, Value v) {
    root_push(v);
    switch (t->kind) {
        case N_IDENT:
            set_var(t->name, v);
            break;
        case N_INDEX: {
            Value obj = eval(t->a);
            root_push(obj);
            Value idx = eval(t->b);
            SET_POS(t);
            index_set(obj, idx, v);
            root_pop(1);
            break;
        }
        case N_FIELD: {
            Value obj = eval(t->a);
            SET_POS(t);
            field_set(t, obj, v);
            break;
        }
        default:
            break;
    }
    root_pop(1);
}

static void destructure_check(Value v, int n) {
    if (!IS_LIST(v)) rt_error("%d 個の変数に分割代入するにはリストが必要です（%s が渡されました）", n, type_name(v));
    if (AS_LIST(v)->count != (size_t)n)
        rt_error("分割代入の数が合いません（変数 %d 個、要素 %zu 個）", n, AS_LIST(v)->count);
}

static ExecStatus exec_assign(Node *s) {
    int nt = s->list.count, nv = s->list2.count;
    if (nt == 1) {
        assign_to(s->list.items[0], eval(s->list2.items[0]));
        return EX_NORMAL;
    }
    size_t base = root_depth();
    if (nv == 1) {
        Value v = eval(s->list2.items[0]);
        SET_POS(s);
        destructure_check(v, nt);
        root_push(v);
        for (int i = 0; i < nt; i++) assign_to(s->list.items[i], AS_LIST(v)->items[i]);
    } else {
        /* 右辺をすべて評価してから代入するので a, b = b, a で入れ替えられる */
        for (int i = 0; i < nv; i++) root_push(eval(s->list2.items[i]));
        for (int i = 0; i < nt; i++) assign_to(s->list.items[i], root_get(base + (size_t)i));
    }
    root_restore(base);
    return EX_NORMAL;
}

static ExecStatus exec_let(Node *s) {
    int nn = s->nparams, nv = s->list2.count;
    if (nv == 0) {
        for (int i = 0; i < nn; i++) env_define(cur_env, s->params[i], v_nil());
        return EX_NORMAL;
    }
    size_t base = root_depth();
    if (nv == 1 && nn > 1) {
        Value v = eval(s->list2.items[0]);
        SET_POS(s);
        destructure_check(v, nn);
        for (int i = 0; i < nn; i++) env_define(cur_env, s->params[i], AS_LIST(v)->items[i]);
        return EX_NORMAL;
    }
    if (nv != nn) rt_error("let の変数 (%d 個) と値 (%d 個) の数が合いません", nn, nv);
    for (int i = 0; i < nv; i++) root_push(eval(s->list2.items[i]));
    for (int i = 0; i < nn; i++) env_define(cur_env, s->params[i], root_get(base + (size_t)i));
    root_restore(base);
    return EX_NORMAL;
}

static ExecStatus exec_compound(Node *s) {
    Node *t = s->a;
    if (t->kind == N_IDENT) {
        Value rhs = eval(s->b);
        root_push(rhs);
        Value cur = lookup(t->name);
        SET_POS(s);
        Value res = binop(s->op, cur, rhs);
        root_pop(1);
        set_var(t->name, res);
        return EX_NORMAL;
    }
    Value obj = eval(t->a);
    root_push(obj);
    if (t->kind == N_INDEX) {
        Value idx = eval(t->b);
        root_push(idx);
        Value rhs = eval(s->b);
        root_push(rhs);
        SET_POS(s);
        Value res = binop(s->op, index_get(obj, idx), rhs);
        index_set(obj, idx, res);
        root_pop(3);
    } else {
        Value rhs = eval(s->b);
        root_push(rhs);
        SET_POS(s);
        Value res = binop(s->op, field_get(t, obj), rhs);
        field_set(t, obj, res);
        root_pop(2);
    }
    return EX_NORMAL;
}

static ExecStatus exec_for(Node *s) {
    Value it = eval(s->a);
    root_push(it);
    SET_POS(s);
    iter_check(it);
    Iter t = {it, 0, 0};
    Value a, b;
    while (iter_next(&t, &a, &b)) {
        bind_iter_vars(s, it, a, b);
        ExecStatus st = exec_stmt(s->b);
        if (st == EX_BREAK) break;
        if (st == EX_RETURN) {
            root_pop(1);
            return st;
        }
    }
    root_pop(1);
    return EX_NORMAL;
}

static ExecStatus exec_match(Node *s) {
    Value subj = eval(s->a);
    root_push(subj);
    for (int i = 0; i < s->list.count; i++) {
        Node *arm = s->list.items[i];
        if (arm_matches(arm, subj)) {
            ExecStatus st = exec_stmt(arm->b);
            root_pop(1);
            return st;
        }
    }
    root_pop(1);
    return EX_NORMAL;
}

/* repeat 回数 { ... } */
static ExecStatus exec_repeat(Node *s) {
    Value c = eval(s->a);
    SET_POS(s);
    if (!IS_NUM(c) || c.as.n < 0 || c.as.n != floor(c.as.n))
        rt_error("repeat の回数には 0 以上の整数が必要です（%s が渡されました）", type_name(c));
    if (c.as.n > 9007199254740992.0) rt_error("repeat の回数が大きすぎます");
    long long times = (long long)c.as.n;
    for (long long i = 0; i < times; i++) {
        ExecStatus st = exec_stmt(s->b);
        if (st == EX_BREAK) break;
        if (st == EX_RETURN) return st;
    }
    return EX_NORMAL;
}

static int is_compare_node(Node *e) {
    if (e->kind != N_BINARY) return 0;
    switch (e->op) {
        case T_EQ: case T_NE: case T_LT: case T_GT: case T_LE: case T_GE: case T_IN:
            return 1;
        default:
            return 0;
    }
}

/* expect 式: 失敗すると、比較の左辺と右辺の値を見せる */
static ExecStatus exec_expect(Node *s) {
    Node *e = s->a;
    StrBuf sb;
    if (is_compare_node(e)) {
        Value l = eval(e->a);
        root_push(l);
        Value r = eval(e->b);
        root_push(r);
        SET_POS(e);
        int ok = is_truthy(binop(e->op, l, r));
        root_pop(2);
        if (ok) return EX_NORMAL;
        sb_init(&sb);
        sb_appendc(&sb, "expect が失敗しました: 左辺は ");
        value_to_sb(&sb, l, 1);
        sb_appendc(&sb, "、右辺は ");
        value_to_sb(&sb, r, 1);
        set_error_hint("条件 '%s' が成り立ちませんでした", op_name(e->op));
    } else {
        Value v = eval(e);
        SET_POS(e);
        if (is_truthy(v)) return EX_NORMAL;
        sb_init(&sb);
        sb_appendc(&sb, "expect が失敗しました: 式の値は ");
        value_to_sb(&sb, v, 1);
    }
    throw_value(sb_to_value(&sb));
}

/* block を実行し、例外が起きたら 1 を返して *exc に例外値を入れる */
static int run_protected(Node *block, ExecStatus *status, Value *exc) {
    TryFrame tf;
    try_push(&tf);
    if (setjmp(tf.buf) == 0) {
        *status = exec_block(block);
        try_pop(&tf);
        return 0;
    }
    try_restore(&tf);
    *exc = thrown_value;
    return 1;
}

static ExecStatus exec_try(Node *s) {
    ExecStatus st = EX_NORMAL;
    Value exc;
    int pending = 0; /* finally の後で再送出する例外があるか */
    int exc_line = 0, exc_col = 0;
    const char *exc_file = NULL;
    char exc_hint[sizeof(thrown_hint)] = "";
    size_t base = root_depth();

    if (run_protected(s->a, &st, &exc)) {
        root_push(exc);
        exc_line = thrown_line;
        exc_col = thrown_col;
        exc_file = thrown_file;
        memcpy(exc_hint, thrown_hint, sizeof(exc_hint));
        if (s->b) {
            if (s->name) env_define(cur_env, s->name, exc);
            if (s->c) {
                Value exc2;
                if (run_protected(s->b, &st, &exc2)) {
                    root_push(exc2);
                    exc = exc2;
                    exc_line = thrown_line;
                    exc_col = thrown_col;
                    exc_file = thrown_file;
                    memcpy(exc_hint, thrown_hint, sizeof(exc_hint));
                    pending = 1;
                }
            } else {
                st = exec_block(s->b);
            }
        } else {
            pending = 1;
        }
    }
    if (s->c) {
        Value saved_ret = ret_val;
        root_push(saved_ret);
        ExecStatus fs = exec_block(s->c);
        if (fs != EX_NORMAL) {
            root_restore(base);
            return fs; /* finally 内の return / break が優先 */
        }
        ret_val = saved_ret;
    }
    root_restore(base);
    if (pending) {
        /* 例外が起きた元の位置で再送出する（finally の最後の行ではなく） */
        cur_line = exc_line;
        cur_col = exc_col;
        cur_file = exc_file;
        if (exc_hint[0]) set_error_hint("%s", exc_hint);
        throw_value(exc);
    }
    return st;
}

/* import "path" as 名前: ファイルを独立したスコープで実行し、
 * 名前が _ で始まらない変数・関数をまとめたマップとして受け取る */
static ExecStatus import_as_module(Node *s, StrBuf *path) {
    Value key = v_str(path->buf, path->len);
    root_push(key);
    Value mod;
    if (map_get(module_cache, key, &mod)) {
        env_define(cur_env, s->name, mod);
        root_pop(1);
        sb_free(path);
        return EX_NORMAL;
    }
    size_t len;
    char *src = read_whole_file(path->buf, &len);
    if (!src) {
        char msg[512];
        snprintf(msg, sizeof(msg), "import するファイル '%s' を開けません", path->buf);
        sb_free(path);
        rt_error("%s", msg);
    }
    char *file = path->buf; /* ファイル名として使い続けるので解放しない */
    Node *prog = parse_program(src, len, file);
    free(src);
    EnvObj *menv = env_new(builtin_env);
    push_env(menv);
    exec_block(prog);
    pop_env();
    MapObj *m = map_new();
    for (size_t i = 0; i < menv->cap; i++) {
        const char *name = menv->names[i];
        if (!name || name[0] == '_') continue; /* _ で始まる名前は非公開 */
        map_set(m, v_cstr(name), menv->vals[i]);
    }
    mod = v_obj(VAL_MAP, m);
    map_set(module_cache, key, mod);
    env_define(cur_env, s->name, mod);
    root_pop(1);
    return EX_NORMAL;
}

/* test "名前" { ... }  laping test で実行したときだけ動く */
static ExecStatus exec_test(Node *s) {
    if (!test_mode) return EX_NORMAL;
    StrObj *name = AS_STR(s->constant);
    ExecStatus st;
    Value exc;
    push_env(env_new(cur_env));
    int failed = run_protected(s->b, &st, &exc);
    pop_env();
    const char *gr = ui_c(0, UI_GREEN), *rd = ui_c(0, UI_RED), *rs = ui_c(0, UI_RESET);
    const char *gy = ui_c(0, UI_GRAY), *yl = ui_c(0, UI_YELLOW);
    if (!failed) {
        test_stats.passed++;
        printf("  %s✓%s %s\n", gr, rs, name->chars);
        return EX_NORMAL;
    }
    test_stats.failed++;
    StrBuf sb;
    sb_init(&sb);
    value_to_sb(&sb, exc, 0);
    printf("  %s✗ %s%s\n", rd, name->chars, rs);
    printf("      %s%s%s\n", rd, sb.buf ? sb.buf : "", rs);
    printf("      %s場所: %s:%d%s\n", gy, thrown_file ? thrown_file : "?", thrown_line, rs);
    if (thrown_hint[0]) printf("      %sヒント:%s %s\n", yl, rs, thrown_hint);
    sb_free(&sb);
    return EX_NORMAL;
}

static int path_is_absolute(const char *p) {
    if (p[0] == '/' || p[0] == '\\') return 1;
    if (p[0] && p[1] == ':') return 1; /* C:\... */
    return 0;
}

static ExecStatus exec_import(Node *s) {
    StrObj *rel = AS_STR(s->constant);
    StrBuf path;
    sb_init(&path);
    if (!path_is_absolute(rel->chars) && s->file) {
        const char *slash = strrchr(s->file, '/');
        const char *bslash = strrchr(s->file, '\\');
        if (bslash && (!slash || bslash > slash)) slash = bslash;
        if (slash) sb_append(&path, s->file, (size_t)(slash - s->file + 1));
    }
    sb_append(&path, rel->chars, rel->len);
    if (!strstr(rel->chars, ".lp")) sb_appendc(&path, ".lp");

    if (s->name) return import_as_module(s, &path);
    for (int i = 0; i < imported_count; i++)
        if (strcmp(imported[i], path.buf) == 0) {
            sb_free(&path);
            return EX_NORMAL;
        }
    size_t len;
    char *src = read_whole_file(path.buf, &len);
    if (!src) {
        char msg[512];
        snprintf(msg, sizeof(msg), "import するファイル '%s' を開けません", path.buf);
        sb_free(&path);
        rt_error("%s", msg);
    }
    imported = xrealloc(imported, sizeof(char *) * (size_t)(imported_count + 1));
    imported[imported_count++] = path.buf; /* 所有権を移す（ファイル名として使い続ける） */

    Node *prog = parse_program(src, len, path.buf);
    free(src);
    push_env(global_env);
    exec_block(prog);
    pop_env();
    return EX_NORMAL;
}

static ExecStatus exec_stmt(Node *s) {
    gc_maybe_collect();
    SET_POS(s);
    cur_file = s->file;
    switch (s->kind) {
        case N_BLOCK:
            return exec_block(s);
        case N_EXPR_STMT:
            eval(s->a);
            return EX_NORMAL;
        case N_ASSIGN:
            return exec_assign(s);
        case N_LET:
            return exec_let(s);
        case N_COMPOUND:
            return exec_compound(s);
        case N_IF:
            if (is_truthy(eval(s->a))) return exec_stmt(s->b);
            if (s->c) return exec_stmt(s->c);
            return EX_NORMAL;
        case N_WHILE:
            while (is_truthy(eval(s->a))) {
                ExecStatus st = exec_stmt(s->b);
                if (st == EX_BREAK) break;
                if (st == EX_RETURN) return st;
                SET_POS(s);
            }
            return EX_NORMAL;
        case N_LOOP:
            for (;;) {
                ExecStatus st = exec_stmt(s->b);
                if (st == EX_BREAK) break;
                if (st == EX_RETURN) return st;
            }
            return EX_NORMAL;
        case N_FOR:
            return exec_for(s);
        case N_MATCH:
            return exec_match(s);
        case N_RETURN:
            ret_val = s->a ? eval(s->a) : v_nil();
            return EX_RETURN;
        case N_BREAK:
            return EX_BREAK;
        case N_CONTINUE:
            return EX_CONTINUE;
        case N_THROW: {
            Value v = eval(s->a);
            SET_POS(s);
            throw_value(v);
            return EX_NORMAL;
        }
        case N_TRY:
            return exec_try(s);
        case N_FNDECL:
            if (s->owner) {
                /* fn Point.len(p) / fn string.shout(s): 型にメソッドを追加する */
                if (!is_known_type(s->owner)) unknown_type(s->owner);
                env_define(methods_for(s->owner, 1), s->name, v_obj(VAL_FUNC, func_new(s->a, cur_env)));
                return EX_NORMAL;
            }
            env_define(cur_env, s->name, v_obj(VAL_FUNC, func_new(s->a, cur_env)));
            return EX_NORMAL;
        case N_RECORD:
            if (!is_record_name(s->name)) {
                if (record_count >= record_cap) {
                    record_cap = record_cap ? record_cap * 2 : 8;
                    record_names = xrealloc(record_names, sizeof(char *) * (size_t)record_cap);
                }
                record_names[record_count++] = s->name;
            }
            env_define(cur_env, s->name, v_obj(VAL_FUNC, func_new(s, cur_env)));
            return EX_NORMAL;
        case N_REPEAT:
            return exec_repeat(s);
        case N_TEST:
            return exec_test(s);
        case N_EXPECT:
            return exec_expect(s);
        case N_IMPORT:
            return exec_import(s);
        default:
            eval(s);
            return EX_NORMAL;
    }
}

static ExecStatus exec_block(Node *b) {
    for (int i = 0; i < b->list.count; i++) {
        ExecStatus st = exec_stmt(b->list.items[i]);
        if (st != EX_NORMAL) return st;
    }
    return EX_NORMAL;
}

/* ===================================================================== */
/*  入口                                                                  */
/* ===================================================================== */

char *read_whole_file(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    StrBuf sb;
    sb_init(&sb);
    char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) sb_append(&sb, buf, n);
    fclose(f);
    if (!sb.buf) sb_append(&sb, "", 0);
    /* UTF-8 BOM を読み飛ばす */
    if (sb.len >= 3 && (unsigned char)sb.buf[0] == 0xEF && (unsigned char)sb.buf[1] == 0xBB && (unsigned char)sb.buf[2] == 0xBF) {
        memmove(sb.buf, sb.buf + 3, sb.len - 2);
        sb.len -= 3;
    }
    *out_len = sb.len;
    return sb.buf;
}

void interp_init(int argc, char **argv, int script_index) {
    gc_init();
    ret_val = v_nil();
    thrown_value = v_nil();
    builtin_env = env_new(NULL);
    builtins_register(builtin_env);
    builtins_set_args(argc, argv, script_index);
    interp_reset_globals();
}

void interp_reset_globals(void) {
    global_env = env_new(builtin_env);
    cur_env = global_env;
    env_depth = 0;
    call_depth = 0;
    imported_count = 0;
    module_cache = map_new();
    method_set_count = 0;
    record_count = 0;
}

void run_program(Node *program) {
    ExecStatus st = exec_block(program);
    if (st == EX_BREAK || st == EX_CONTINUE)
        rt_error("ループの外で %s は使えません", st == EX_BREAK ? "break" : "continue");
}

int run_file(const char *path) {
    size_t len;
    char *src = read_whole_file(path, &len);
    if (!src) {
        fprintf(stderr, "Laping: ファイルを開けません: %s\n", path);
        return 1;
    }
    main_file = path;
    imported = xrealloc(imported, sizeof(char *) * (size_t)(imported_count + 1));
    imported[imported_count++] = (char *)path;
    Node *prog = parse_program(src, len, path);
    free(src);
    run_program(prog);
    return 0;
}

/* REPL 用: 最後の文が式なら、その値を返す */
Value exec_program_repl(Node *program, int *has_value) {
    *has_value = 0;
    int n = program->list.count;
    for (int i = 0; i < n; i++) {
        Node *s = program->list.items[i];
        if (i == n - 1 && s->kind == N_EXPR_STMT) {
            SET_POS(s);
            cur_file = s->file;
            Value v = eval(s->a);
            *has_value = 1;
            return v;
        }
        ExecStatus st = exec_stmt(s);
        if (st == EX_BREAK || st == EX_CONTINUE)
            rt_error("ループの外で %s は使えません", st == EX_BREAK ? "break" : "continue");
        if (st == EX_RETURN) break;
    }
    return v_nil();
}
