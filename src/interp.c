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
    int line;       /* 呼び出し元の行 */
    const char *file;
} CallFrame;

int cur_line = 0;
const char *cur_file = NULL;
const char *main_file = NULL;
EnvObj *global_env = NULL;
EnvObj *builtin_env = NULL;
extern const char *cur_parse_file;

static EnvObj *cur_env = NULL;
static EnvObj **env_stack = NULL;
static int env_depth = 0, env_cap = 0;
static CallFrame *frames = NULL;
static int call_depth = 0, frames_cap = 0;

static TryFrame *try_top = NULL;
Value thrown_value;
int thrown_line = 0;
const char *thrown_file = NULL;
static Value ret_val;

static char **imported = NULL;
static int imported_count = 0;

static ExecStatus exec_stmt(Node *s);
static ExecStatus exec_block(Node *b);
static Value eval(Node *n);

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
}

static void print_location(FILE *out, int line, const char *file) {
    if (!file || file == main_file) fprintf(out, "%d行目", line);
    else fprintf(out, "%s:%d行目", file, line);
}

void report_error(Value v, int line, const char *file) {
    fflush(stdout);
    StrBuf sb;
    sb_init(&sb);
    value_to_sb(&sb, v, 0);
    fprintf(stderr, "Laping: %s (", sb.buf ? sb.buf : "");
    print_location(stderr, line, file);
    fprintf(stderr, ")\n");
    sb_free(&sb);
    /* 呼び出し履歴（新しいものから最大10件。同じ呼び出しの連続はまとめる） */
    int shown = 0;
    int i = call_depth - 1;
    while (i >= 0 && shown < 10) {
        int j = i;
        while (j > 0 && frames[j - 1].name == frames[i].name && frames[j - 1].line == frames[i].line &&
               frames[j - 1].file == frames[i].file)
            j--;
        fprintf(stderr, "    %s() の呼び出し元: ", frames[i].name ? frames[i].name : "<無名関数>");
        print_location(stderr, frames[i].line, frames[i].file);
        if (i - j > 0) fprintf(stderr, "（同じ呼び出しが %d 回続いています）", i - j + 1);
        fprintf(stderr, "\n");
        shown++;
        i = j - 1;
    }
    if (i >= 0) fprintf(stderr, "    ... (他 %d 件)\n", i + 1);
}

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
    thrown_file = cur_file;
    if (try_top) longjmp(try_top->buf, 1);
    report_error(v, thrown_line, thrown_file);
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
    cur_file = file ? file : cur_parse_file;
    int saved_depth = call_depth;
    call_depth = 0; /* 構文エラーに呼び出し履歴は不要 */
    if (!try_top) {
        report_error(v_cstr(buf), line, cur_file);
        exit(1);
    }
    call_depth = saved_depth;
    throw_value(v_cstr(buf));
}

/* ===================================================================== */
/*  変数                                                                  */
/* ===================================================================== */

static Value lookup(const char *name) {
    Value *v = env_find(cur_env, name);
    if (!v) rt_error("未定義の変数 '%s'", name);
    return *v;
}

/* 既存の変数（組み込みを除く）があれば更新、なければ現在のスコープに作る */
static void set_var(const char *name, Value v) {
    for (EnvObj *e = cur_env; e && e != builtin_env; e = e->parent) {
        Value *slot = env_find_local(e, name);
        if (slot) {
            *slot = v;
            return;
        }
    }
    env_define(cur_env, name, v);
}

static void push_env(EnvObj *e) {
    if (env_depth >= env_cap) {
        env_cap = env_cap ? env_cap * 2 : 64;
        env_stack = realloc(env_stack, sizeof(EnvObj *) * env_cap);
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

static Value field_get(Node *n, Value obj) {
    if (!IS_MAP(obj)) rt_error("'.%s' でフィールドを参照できるのはマップだけです（%s が渡されました）", n->name, type_name(obj));
    Value out;
    if (!map_get(AS_MAP(obj), field_key(n), &out)) rt_error("キー \"%s\" が存在しません", n->name);
    return out;
}

static void field_set(Node *n, Value obj, Value v) {
    if (!IS_MAP(obj)) rt_error("'.%s' でフィールドに代入できるのはマップだけです（%s が渡されました）", n->name, type_name(obj));
    map_set(AS_MAP(obj), field_key(n), v);
}

/* ===================================================================== */
/*  関数呼び出し                                                          */
/* ===================================================================== */

static void push_frame(const char *name) {
    if (call_depth >= MAX_CALL_DEPTH) rt_error("関数呼び出しが深すぎます（再帰が止まっていない可能性があります）");
    if (call_depth >= frames_cap) {
        frames_cap = frames_cap ? frames_cap * 2 : 64;
        frames = realloc(frames, sizeof(CallFrame) * frames_cap);
    }
    frames[call_depth].name = name;
    frames[call_depth].line = cur_line;
    frames[call_depth].file = cur_file;
    call_depth++;
}

Value call_value(Value callee, int argc, Value *argv) {
    int saved_line = cur_line;
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
    if (d->op == 1) {
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
    Value *argv = argc <= 8 ? small : malloc(sizeof(Value) * (size_t)argc);
    if (self) argv[0] = *self;
    /* root スタックは再確保されうるので、評価後に値だけコピーする */
    for (int i = 0; i < call->list.count; i++) argv[i + extra] = root_get(base + 1 + (size_t)extra + (size_t)i);
    cur_line = call->line;
    cur_file = call->file;
    Value r = call_value(callee, argc, argv);
    if (argv != small) free(argv);
    root_restore(base);
    return r;
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
                cur_line = n->line;
                rt_error("未定義の変数 '%s'", n->name);
            }
            return *v;
        }
        case N_INTERP: {
            StrBuf sb;
            sb_init(&sb);
            for (int i = 0; i < n->list.count; i++) {
                Node *p = n->list.items[i];
                if (p->kind == N_STR) {
                    StrObj *s = AS_STR(p->constant);
                    sb_append(&sb, s->chars, s->len);
                } else {
                    value_to_sb(&sb, eval(p), 0);
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
                    cur_line = n->list.items[i]->line;
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
                cur_line = n->line;
                rt_error("単項 '-' には数値が必要です（%s が渡されました）", type_name(v));
            }
            return v_num(-v.as.n);
        }
        case N_BINARY: {
            Value l = eval(n->a);
            root_push(l);
            Value r = eval(n->b);
            root_pop(1);
            cur_line = n->line;
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
                cur_line = n->line;
                rt_error("範囲 '..' の両端には数値が必要です");
            }
            return v_obj(VAL_RANGE, range_new(a.as.n, b.as.n, 1, n->op));
        }
        case N_CALL: {
            Value callee = eval(n->a);
            return eval_call_args(n, callee, NULL);
        }
        case N_METHOD: {
            Value obj = eval(n->a);
            root_push(obj);
            Value callee;
            Value r;
            if (IS_MAP(obj) && map_get(AS_MAP(obj), field_key(n), &callee)) {
                /* マップのフィールドに入った関数を呼ぶ */
                r = eval_call_args(n, callee, NULL);
            } else {
                /* x.f(a) は f(x, a) と同じ（組み込み関数もユーザー関数も使える） */
                Value *fv = env_find(cur_env, n->name);
                if (!fv || !IS_CALLABLE(*fv)) {
                    cur_line = n->line;
                    if (IS_MAP(obj)) rt_error("マップにキー \"%s\" がなく、同名の関数もありません", n->name);
                    rt_error("%s に対するメソッド '%s' が見つかりません", type_name(obj), n->name);
                }
                r = eval_call_args(n, *fv, &obj);
            }
            root_pop(1);
            return r;
        }
        case N_INDEX: {
            Value obj = eval(n->a);
            root_push(obj);
            Value idx = eval(n->b);
            root_pop(1);
            cur_line = n->line;
            return index_get(obj, idx);
        }
        case N_FIELD: {
            Value obj = eval(n->a);
            cur_line = n->line;
            return field_get(n, obj);
        }
        default:
            cur_line = n->line;
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
            cur_line = t->line;
            index_set(obj, idx, v);
            root_pop(1);
            break;
        }
        case N_FIELD: {
            Value obj = eval(t->a);
            cur_line = t->line;
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
        cur_line = s->line;
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
        cur_line = s->line;
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
        cur_line = s->line;
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
        cur_line = s->line;
        Value res = binop(s->op, index_get(obj, idx), rhs);
        index_set(obj, idx, res);
        root_pop(3);
    } else {
        Value rhs = eval(s->b);
        root_push(rhs);
        cur_line = s->line;
        Value res = binop(s->op, field_get(t, obj), rhs);
        field_set(t, obj, res);
        root_pop(2);
    }
    return EX_NORMAL;
}

static void bind_loop_vars(Node *s, Value a, Value b) {
    if (s->nparams == 1) env_define(cur_env, s->params[0], b);
    else {
        env_define(cur_env, s->params[0], a);
        env_define(cur_env, s->params[1], b);
    }
}

#define LOOP_BODY(s)                                   \
    do {                                               \
        ExecStatus st_ = exec_stmt((s)->b);            \
        if (st_ == EX_BREAK) goto loop_end;            \
        if (st_ == EX_RETURN) { root_pop(1); return st_; } \
    } while (0)

static ExecStatus exec_for(Node *s) {
    Value it = eval(s->a);
    root_push(it);
    cur_line = s->line;
    switch (it.type) {
        case VAL_RANGE: {
            RangeObj *r = AS_RANGE(it);
            size_t n;
            if (!range_len(r, &n)) rt_error("範囲が大きすぎるか、step が 0 です");
            for (size_t i = 0; i < n; i++) {
                bind_loop_vars(s, v_num((double)i), v_num(range_at(r, i)));
                LOOP_BODY(s);
            }
            break;
        }
        case VAL_LIST: {
            ListObj *l = AS_LIST(it);
            for (size_t i = 0; i < l->count; i++) {
                bind_loop_vars(s, v_num((double)i), l->items[i]);
                LOOP_BODY(s);
            }
            break;
        }
        case VAL_STR: {
            StrObj *str = AS_STR(it);
            size_t off = 0, idx = 0;
            while (off < str->len) {
                size_t w = utf8_char_len((unsigned char)str->chars[off]);
                if (off + w > str->len) w = str->len - off;
                bind_loop_vars(s, v_num((double)idx), v_str(str->chars + off, w));
                off += w;
                idx++;
                LOOP_BODY(s);
            }
            break;
        }
        case VAL_MAP: {
            MapObj *m = AS_MAP(it);
            for (size_t i = 0; i < m->count; i++) {
                if (m->entries[i].deleted) continue;
                if (s->nparams == 1) env_define(cur_env, s->params[0], m->entries[i].key);
                else bind_loop_vars(s, m->entries[i].key, m->entries[i].val);
                LOOP_BODY(s);
            }
            break;
        }
        default:
            rt_error("for で繰り返せるのは リスト・範囲・文字列・マップ です（%s が渡されました）", type_name(it));
    }
loop_end:
    root_pop(1);
    return EX_NORMAL;
}

static ExecStatus exec_match(Node *s) {
    Value subj = eval(s->a);
    root_push(subj);
    for (int i = 0; i < s->list.count; i++) {
        Node *arm = s->list.items[i];
        int matched = arm->op;
        for (int j = 0; !matched && j < arm->list.count; j++) {
            Value pv = eval(arm->list.items[j]);
            if (IS_RANGE(pv) && IS_NUM(subj)) matched = range_contains(AS_RANGE(pv), subj.as.n);
            else matched = values_equal(subj, pv);
        }
        if (matched && arm->a) matched = is_truthy(eval(arm->a));
        if (matched) {
            ExecStatus st = exec_stmt(arm->b);
            root_pop(1);
            return st;
        }
    }
    root_pop(1);
    return EX_NORMAL;
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
    int exc_line = 0;
    const char *exc_file = NULL;
    size_t base = root_depth();

    if (run_protected(s->a, &st, &exc)) {
        root_push(exc);
        exc_line = thrown_line;
        exc_file = thrown_file;
        if (s->b) {
            if (s->name) env_define(cur_env, s->name, exc);
            if (s->c) {
                Value exc2;
                if (run_protected(s->b, &st, &exc2)) {
                    root_push(exc2);
                    exc = exc2;
                    exc_line = thrown_line;
                    exc_file = thrown_file;
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
        cur_file = exc_file;
        throw_value(exc);
    }
    return st;
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
    imported = realloc(imported, sizeof(char *) * (size_t)(imported_count + 1));
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
    cur_line = s->line;
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
                cur_line = s->line;
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
            cur_line = s->line;
            throw_value(v);
            return EX_NORMAL;
        }
        case N_TRY:
            return exec_try(s);
        case N_FNDECL:
            env_define(cur_env, s->name, v_obj(VAL_FUNC, func_new(s->a, cur_env)));
            return EX_NORMAL;
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
    global_env = env_new(builtin_env);
    cur_env = global_env;
    builtins_set_args(argc, argv, script_index);
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
    imported = realloc(imported, sizeof(char *) * (size_t)(imported_count + 1));
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
            cur_line = s->line;
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
