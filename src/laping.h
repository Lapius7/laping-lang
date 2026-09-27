/* Laping (.lp) — 共通ヘッダ
 *
 * 構成:
 *   lexer.c    ソース文字列 -> トークン列
 *   parser.c   トークン列 -> AST
 *   value.c    値・ヒープオブジェクト・GC・文字列/リスト/マップの実装
 *   interp.c   AST を直接たどって実行する評価器（例外・スコープ・関数呼び出し）
 *   builtins.c 組み込み関数
 *   main.c     コマンドライン / REPL
 */
#ifndef LAPING_H
#define LAPING_H

#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>

#define LAPING_VERSION "v2.0.0"

/* ===================================================================== */
/*  値                                                                    */
/* ===================================================================== */

typedef enum {
    VAL_NIL,
    VAL_BOOL,
    VAL_NUM,
    VAL_STR,
    VAL_LIST,
    VAL_MAP,
    VAL_FUNC,
    VAL_BUILTIN,
    VAL_RANGE
} ValueType;

typedef struct Obj Obj;
typedef struct StrObj StrObj;
typedef struct ListObj ListObj;
typedef struct MapObj MapObj;
typedef struct FuncObj FuncObj;
typedef struct RangeObj RangeObj;
typedef struct EnvObj EnvObj;
typedef struct Builtin Builtin;
typedef struct Node Node;

typedef struct {
    ValueType type;
    union {
        int b;
        double n;
        Obj *o;
        const Builtin *bi;
    } as;
} Value;

typedef enum { OBJ_STR, OBJ_LIST, OBJ_MAP, OBJ_FUNC, OBJ_RANGE, OBJ_ENV } ObjType;

struct Obj {
    Obj *next;
    unsigned char type;
    unsigned char marked;
    unsigned char perm; /* 1 ならGC対象外（ソース中の文字列定数など） */
};

struct StrObj {
    Obj h;
    size_t len;
    uint32_t hash;
    char chars[]; /* NUL終端 */
};

struct ListObj {
    Obj h;
    Value *items;
    size_t count;
    size_t cap;
};

typedef struct {
    Value key;
    Value val;
    uint32_t hash;
    int deleted;
} MapEntry;

/* 挿入順を保持するハッシュマップ */
struct MapObj {
    Obj h;
    MapEntry *entries;
    size_t count; /* 削除済みを含むエントリ数 */
    size_t live;  /* 有効なエントリ数 */
    size_t cap;
    int32_t *index; /* entries へのインデックス（オープンアドレス法） */
    size_t index_cap;
};

struct FuncObj {
    Obj h;
    Node *decl;      /* N_FUNC ノード */
    EnvObj *closure; /* 定義時の環境 */
};

struct RangeObj {
    Obj h;
    double start, end, step;
    int inclusive;
};

/* 変数スコープ。名前はインターン済み文字列なのでポインタ比較で引ける */
struct EnvObj {
    Obj h;
    EnvObj *parent;
    const char **names;
    Value *vals;
    size_t count;
    size_t cap;
};

typedef Value (*BuiltinFn)(int argc, Value *argv);

struct Builtin {
    const char *name;
    BuiltinFn fn;
    int min_args;
    int max_args; /* -1 で可変長 */
};

#define AS_STR(v)   ((StrObj *)(v).as.o)
#define AS_LIST(v)  ((ListObj *)(v).as.o)
#define AS_MAP(v)   ((MapObj *)(v).as.o)
#define AS_FUNC(v)  ((FuncObj *)(v).as.o)
#define AS_RANGE(v) ((RangeObj *)(v).as.o)

#define IS_NIL(v)   ((v).type == VAL_NIL)
#define IS_BOOL(v)  ((v).type == VAL_BOOL)
#define IS_NUM(v)   ((v).type == VAL_NUM)
#define IS_STR(v)   ((v).type == VAL_STR)
#define IS_LIST(v)  ((v).type == VAL_LIST)
#define IS_MAP(v)   ((v).type == VAL_MAP)
#define IS_RANGE(v) ((v).type == VAL_RANGE)
#define IS_CALLABLE(v) ((v).type == VAL_FUNC || (v).type == VAL_BUILTIN)

Value v_nil(void);
Value v_bool(int b);
Value v_num(double n);
Value v_obj(ValueType t, void *o);

/* ---- GC ---- */
void gc_init(void);
void gc_maybe_collect(void);
void gc_collect(void);
void gc_account(long bytes);
void root_push(Value v);
void root_pop(int n);
size_t root_depth(void);
void root_restore(size_t depth);
Value root_get(size_t i);

/* ---- 文字列 ---- */
StrObj *str_new(const char *s, size_t len);
StrObj *str_new_perm(const char *s, size_t len);
Value v_str(const char *s, size_t len);
Value v_cstr(const char *s);
uint32_t hash_bytes(const char *s, size_t len);
size_t utf8_len(const char *s, size_t len);
size_t utf8_offset(const char *s, size_t len, size_t index);
size_t utf8_char_len(unsigned char c);

/* 可変長の文字列バッファ（GC対象外、使い終わったら sb_free） */
typedef struct {
    char *buf;
    size_t len;
    size_t cap;
} StrBuf;
void sb_init(StrBuf *sb);
void sb_append(StrBuf *sb, const char *s, size_t len);
void sb_appendc(StrBuf *sb, const char *s);
void sb_printf(StrBuf *sb, const char *fmt, ...);
void sb_free(StrBuf *sb);
Value sb_to_value(StrBuf *sb); /* sb を解放して文字列値を返す */

/* ---- リスト ---- */
ListObj *list_new(size_t cap);
Value v_list(size_t cap);
void list_push(ListObj *l, Value v);
void list_insert(ListObj *l, size_t idx, Value v);
Value list_remove(ListObj *l, size_t idx);

/* ---- マップ ---- */
MapObj *map_new(void);
Value v_map(void);
int map_get(MapObj *m, Value key, Value *out);
void map_set(MapObj *m, Value key, Value val);
int map_delete(MapObj *m, Value key);
int is_hashable(Value v);

/* ---- その他のオブジェクト ---- */
FuncObj *func_new(Node *decl, EnvObj *closure);
RangeObj *range_new(double start, double end, double step, int inclusive);
EnvObj *env_new(EnvObj *parent);
Value *env_find_local(EnvObj *e, const char *name);
Value *env_find(EnvObj *e, const char *name);
void env_define(EnvObj *e, const char *name, Value v);

/* ---- 値の汎用操作 ---- */
int values_equal(Value a, Value b);
int is_truthy(Value v);
const char *type_name(Value v);
void value_to_sb(StrBuf *sb, Value v, int repr);
Value value_to_str(Value v); /* print と同じ表示形式の文字列 */
void format_number(char *buf, size_t size, double d);
int range_len(RangeObj *r, size_t *out);
double range_at(RangeObj *r, size_t i);

/* ---- インターン ---- */
const char *intern(const char *s, size_t len);

/* ===================================================================== */
/*  字句解析                                                              */
/* ===================================================================== */

typedef enum {
    T_EOF, T_NEWLINE, T_NUM, T_STR, T_INTERP, T_IDENT,
    /* キーワード */
    T_IF, T_ELIF, T_ELSE, T_UNLESS, T_WHILE, T_UNTIL, T_FOR, T_IN, T_LOOP,
    T_BREAK, T_CONTINUE, T_RETURN, T_FN, T_LET, T_TRUE, T_FALSE, T_NIL,
    T_AND, T_OR, T_NOT, T_MATCH, T_TRY, T_CATCH, T_FINALLY, T_THROW, T_IMPORT,
    /* 記号 */
    T_LPAREN, T_RPAREN, T_LBRACKET, T_RBRACKET, T_LBRACE, T_RBRACE,
    T_COMMA, T_DOT, T_COLON, T_SEMI, T_QUESTION, T_ARROW,
    T_ASSIGN, T_PLUS_EQ, T_MINUS_EQ, T_STAR_EQ, T_SLASH_EQ, T_PERCENT_EQ,
    T_PLUS, T_MINUS, T_STAR, T_SLASH, T_SLASHSLASH, T_PERCENT, T_POW,
    T_EQ, T_NE, T_LT, T_GT, T_LE, T_GE, T_BANG, T_ANDAND, T_OROR,
    T_DOTDOT, T_DOTDOTDOT
} TokKind;

typedef struct {
    int is_expr;
    char *text;
    size_t len;
    int line;
} InterpPart;

typedef struct {
    TokKind kind;
    int line;
    double num;
    char *str; /* 識別子名 / 文字列の中身（エスケープ処理済み） */
    size_t len;
    InterpPart *parts; /* T_INTERP のみ */
    int nparts;
} Token;

typedef struct {
    Token *toks;
    int count;
    int cap;
} TokenList;

TokenList lex(const char *src, size_t len, int first_line);
const char *tok_kind_name(TokKind k);

/* ===================================================================== */
/*  AST                                                                   */
/* ===================================================================== */

typedef enum {
    /* 式 */
    N_NUM, N_STR, N_INTERP, N_BOOL, N_NIL, N_IDENT, N_LIST, N_MAP, N_FUNC,
    N_UNARY, N_BINARY, N_AND, N_OR, N_TERNARY, N_CALL, N_METHOD, N_INDEX,
    N_FIELD, N_RANGE,
    /* 文 */
    N_BLOCK, N_EXPR_STMT, N_ASSIGN, N_LET, N_COMPOUND, N_IF, N_WHILE, N_FOR,
    N_LOOP, N_MATCH, N_MATCH_ARM, N_RETURN, N_BREAK, N_CONTINUE, N_THROW,
    N_TRY, N_FNDECL, N_IMPORT
} NodeKind;

typedef struct {
    Node **items;
    int count;
    int cap;
} NodeList;

struct Node {
    NodeKind kind;
    int line;
    const char *file;
    int op;             /* 演算子のトークン種別 / 各種フラグ */
    double num;
    const char *name;   /* 識別子（インターン済み） */
    Value constant;     /* 文字列定数 */
    Node *a, *b, *c, *d;
    NodeList list;      /* 子ノード列 */
    NodeList list2;     /* 2つ目の子ノード列（代入の右辺など） */
    const char **params;
    Node **defaults;
    int nparams;
    int variadic;       /* 最後の引数が ...rest */
};

Node *parse_program(const char *src, size_t len, const char *file);

/* ===================================================================== */
/*  インタプリタ                                                          */
/* ===================================================================== */

extern int cur_line;
extern const char *cur_file;
extern const char *main_file;
extern EnvObj *global_env;
extern EnvObj *builtin_env;

void interp_init(int argc, char **argv, int script_index);
void run_program(Node *program);
int run_file(const char *path);
Value call_value(Value callee, int argc, Value *argv);
Value exec_program_repl(Node *program, int *has_value);

/* 例外 */
typedef struct TryFrame {
    jmp_buf buf;
    struct TryFrame *prev;
    size_t root_depth;
    int env_depth;
    int call_depth;
    EnvObj *env;
    const char *file;
} TryFrame;

void try_push(TryFrame *tf);
void try_pop(TryFrame *tf);
void try_restore(TryFrame *tf);
extern Value thrown_value;
extern int thrown_line;
extern const char *thrown_file;

void rt_error(const char *fmt, ...);
void syntax_error(int line, const char *file, const char *fmt, ...);
void throw_value(Value v);
void report_error(Value v, int line, const char *file);
char *read_whole_file(const char *path, size_t *out_len);

void gc_mark_value(Value v);
void gc_mark_obj(Obj *o);
void interp_mark_roots(void);

/* 組み込み */
void builtins_register(EnvObj *env);
void builtins_set_args(int argc, char **argv, int start);
const char *builtin_set_current(const char *name);

#endif
