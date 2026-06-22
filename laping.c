/* Laping (.lp) インタプリタ — Lexer -> Parser -> Tree-walking Interpreter
 *
 * 構文（記号はC/Python等と同じ一般的なものを使うが、組み立て方が特殊）:
 *   x = 10                          代入
 *   print(expr)                     出力
 *   stmt if cond                    単文修飾子（条件成立時のみ実行）
 *   stmt unless cond                単文修飾子（条件不成立時のみ実行）
 *   if cond { ... } else { ... }    複文ブロック
 *   while cond { ... }              複文ブロックループ
 *
 * 修飾子(if/unless文末)は単文のみ対応。複数文を書きたい場合はブロック型を使う。
 * これにより「修飾子の後にどこまでが対象か」が常に1文に固定され、曖昧さが出ない。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include "updater.h"

#if defined(_WIN32)
#include <windows.h>
#endif

#define LAPING_VERSION "v1.0.0"

#define MAX_TOKENS   4096
#define MAX_VARS     256
#define MAX_IDENT    64
#define MAX_NODES    4096
#define MAX_CHILDREN 64

/* ===== Lexer ===== */

typedef enum {
    T_NUMBER, T_STRING, T_IDENT,
    T_IF, T_ELSE, T_UNLESS, T_WHILE, T_PRINT,
    T_OP, T_LPAREN, T_RPAREN, T_LBRACE, T_RBRACE,
    T_EOF
} TokenKind;

typedef struct {
    TokenKind kind;
    char text[MAX_IDENT];
    double num;
    int line;
} Token;

static Token tokens[MAX_TOKENS];
static int token_count = 0;

static void die(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "Laping: ");
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
    exit(1);
}

static void add_token(TokenKind kind, const char *text, double num, int line) {
    if (token_count >= MAX_TOKENS) die("トークン数が上限を超えました");
    Token *t = &tokens[token_count++];
    t->kind = kind;
    t->num = num;
    t->line = line;
    if (text) snprintf(t->text, MAX_IDENT, "%s", text);
    else t->text[0] = '\0';
}

static void tokenize(const char *src) {
    int i = 0, line = 1;
    int n = (int)strlen(src);
    while (i < n) {
        char c = src[i];
        if (c == '\n') { line++; i++; continue; }
        if (c == ' ' || c == '\t' || c == '\r') { i++; continue; }
        if (c == '#') { while (i < n && src[i] != '\n') i++; continue; }

        if (isdigit((unsigned char)c)) {
            int start = i;
            while (i < n && (isdigit((unsigned char)src[i]) || src[i] == '.')) i++;
            char buf[64] = {0};
            int len = i - start;
            if (len >= (int)sizeof(buf)) len = sizeof(buf) - 1;
            memcpy(buf, src + start, len);
            add_token(T_NUMBER, NULL, atof(buf), line);
            continue;
        }
        if (c == '"') {
            int start = ++i;
            while (i < n && src[i] != '"') i++;
            if (i >= n) die("文字列リテラルが閉じられていません (%d行目)", line);
            char buf[MAX_IDENT] = {0};
            int len = i - start;
            if (len >= (int)sizeof(buf)) len = sizeof(buf) - 1;
            memcpy(buf, src + start, len);
            i++; /* closing quote */
            add_token(T_STRING, buf, 0, line);
            continue;
        }
        if (isalpha((unsigned char)c) || c == '_') {
            int start = i;
            while (i < n && (isalnum((unsigned char)src[i]) || src[i] == '_')) i++;
            char buf[MAX_IDENT] = {0};
            int len = i - start;
            if (len >= (int)sizeof(buf)) len = sizeof(buf) - 1;
            memcpy(buf, src + start, len);
            if (strcmp(buf, "if") == 0) add_token(T_IF, buf, 0, line);
            else if (strcmp(buf, "else") == 0) add_token(T_ELSE, buf, 0, line);
            else if (strcmp(buf, "unless") == 0) add_token(T_UNLESS, buf, 0, line);
            else if (strcmp(buf, "while") == 0) add_token(T_WHILE, buf, 0, line);
            else if (strcmp(buf, "print") == 0) add_token(T_PRINT, buf, 0, line);
            else add_token(T_IDENT, buf, 0, line);
            continue;
        }
        if (c == '(') { add_token(T_LPAREN, "(", 0, line); i++; continue; }
        if (c == ')') { add_token(T_RPAREN, ")", 0, line); i++; continue; }
        if (c == '{') { add_token(T_LBRACE, "{", 0, line); i++; continue; }
        if (c == '}') { add_token(T_RBRACE, "}", 0, line); i++; continue; }

        /* 2文字演算子 */
        if (i + 1 < n) {
            char two[3] = { c, src[i + 1], 0 };
            if (strcmp(two, "==") == 0 || strcmp(two, "!=") == 0 ||
                strcmp(two, "<=") == 0 || strcmp(two, ">=") == 0) {
                add_token(T_OP, two, 0, line);
                i += 2;
                continue;
            }
        }
        if (strchr("+-*/%<>=", c)) {
            char one[2] = { c, 0 };
            add_token(T_OP, one, 0, line);
            i++;
            continue;
        }
        die("不正な文字 '%c' (%d行目)", c, line);
    }
    add_token(T_EOF, NULL, 0, line);
}

/* ===== AST ===== */

typedef enum {
    N_NUM, N_STR, N_VAR, N_BINOP,
    N_ASSIGN, N_PRINT, N_IF, N_WHILE, N_BLOCK
} NodeKind;

typedef struct Node Node;

struct Node {
    NodeKind kind;
    double num;
    char text[MAX_IDENT];   /* 文字列値・変数名・演算子 */
    int line;
    Node *a, *b, *c;        /* 用途は種別ごとに異なる汎用ポインタ */
    Node *children[MAX_CHILDREN]; /* N_BLOCK用 */
    int child_count;
};

static Node node_pool[MAX_NODES];
static int node_count = 0;

static Node *new_node(NodeKind kind, int line) {
    if (node_count >= MAX_NODES) die("ASTノード数が上限を超えました");
    Node *node = &node_pool[node_count++];
    memset(node, 0, sizeof(Node));
    node->kind = kind;
    node->line = line;
    return node;
}

/* 単文(N_ASSIGN/N_PRINT)だけを if/unless 修飾子の対象として許可する。
 * if/while/blockは対象外にして、修飾子の入れ子で挙動が曖昧になるのを防ぐ。 */
static int is_modifiable(Node *node) {
    return node->kind == N_ASSIGN || node->kind == N_PRINT;
}

/* ===== Parser ===== */

static int pos = 0;

static Token *peek(void) { return &tokens[pos]; }
static Token *advance(void) { return &tokens[pos++]; }

static Token *expect(TokenKind kind, const char *what) {
    if (peek()->kind != kind) die("'%s' を期待したが違うものが来た (%d行目)", what, peek()->line);
    return advance();
}

static int peek_is_op(const char *op) {
    return peek()->kind == T_OP && strcmp(peek()->text, op) == 0;
}

static Node *parse_expr(void);

static Node *parse_factor(void) {
    Token *t = peek();
    if (t->kind == T_NUMBER) {
        advance();
        Node *node = new_node(N_NUM, t->line);
        node->num = t->num;
        return node;
    }
    if (t->kind == T_STRING) {
        advance();
        Node *node = new_node(N_STR, t->line);
        snprintf(node->text, MAX_IDENT, "%s", t->text);
        return node;
    }
    if (t->kind == T_IDENT) {
        advance();
        Node *node = new_node(N_VAR, t->line);
        snprintf(node->text, MAX_IDENT, "%s", t->text);
        return node;
    }
    if (t->kind == T_LPAREN) {
        advance();
        Node *node = parse_expr();
        expect(T_RPAREN, ")");
        return node;
    }
    die("式の構文エラー (%d行目)", t->line);
    return NULL;
}

static Node *parse_term(void) {
    Node *left = parse_factor();
    while (peek_is_op("*") || peek_is_op("/") || peek_is_op("%")) {
        Token *op = advance();
        Node *right = parse_factor();
        Node *node = new_node(N_BINOP, op->line);
        snprintf(node->text, MAX_IDENT, "%s", op->text);
        node->a = left;
        node->b = right;
        left = node;
    }
    return left;
}

static Node *parse_additive(void) {
    Node *left = parse_term();
    while (peek_is_op("+") || peek_is_op("-")) {
        Token *op = advance();
        Node *right = parse_term();
        Node *node = new_node(N_BINOP, op->line);
        snprintf(node->text, MAX_IDENT, "%s", op->text);
        node->a = left;
        node->b = right;
        left = node;
    }
    return left;
}

static Node *parse_expr(void) {
    Node *left = parse_additive();
    while (peek_is_op("==") || peek_is_op("!=") || peek_is_op("<") ||
           peek_is_op(">") || peek_is_op("<=") || peek_is_op(">=")) {
        Token *op = advance();
        Node *right = parse_additive();
        Node *node = new_node(N_BINOP, op->line);
        snprintf(node->text, MAX_IDENT, "%s", op->text);
        node->a = left;
        node->b = right;
        left = node;
    }
    return left;
}

static Node *parse_statement(void);
static Node *parse_simple_statement(void);

static Node *parse_block(void) {
    expect(T_LBRACE, "{");
    Node *block = new_node(N_BLOCK, peek()->line);
    while (peek()->kind != T_RBRACE) {
        if (peek()->kind == T_EOF) die("'}' が見つからないままファイルが終わりました");
        if (block->child_count >= MAX_CHILDREN) die("ブロック内の文が多すぎます (%d行目)", peek()->line);
        block->children[block->child_count++] = parse_statement();
    }
    expect(T_RBRACE, "}");
    return block;
}

/* 修飾子の対象になりうる「単文」のみ。if/while/blockはここに来ない。 */
static Node *parse_simple_statement(void) {
    Token *t = peek();
    if (t->kind == T_PRINT) {
        advance();
        expect(T_LPAREN, "(");
        Node *expr = parse_expr();
        expect(T_RPAREN, ")");
        Node *node = new_node(N_PRINT, t->line);
        node->a = expr;
        return node;
    }
    if (t->kind == T_IDENT) {
        advance();
        char name[MAX_IDENT];
        snprintf(name, MAX_IDENT, "%s", t->text);
        expect(T_OP, "=");
        Node *expr = parse_expr();
        Node *node = new_node(N_ASSIGN, t->line);
        snprintf(node->text, MAX_IDENT, "%s", name);
        node->a = expr;
        return node;
    }
    die("単文を期待したが予期しないトークンが来た (%d行目)", t->line);
    return NULL;
}

static Node *parse_statement(void) {
    Token *t = peek();
    if (t->kind == T_IF) {
        advance();
        Node *cond = parse_expr();
        Node *then_body = parse_block();
        Node *else_body = NULL;
        if (peek()->kind == T_ELSE) {
            advance();
            else_body = parse_block();
        }
        Node *node = new_node(N_IF, t->line);
        node->a = cond;
        node->b = then_body;
        node->c = else_body;
        return node;
    }
    if (t->kind == T_WHILE) {
        advance();
        Node *cond = parse_expr();
        Node *body = parse_block();
        Node *node = new_node(N_WHILE, t->line);
        node->a = cond;
        node->b = body;
        return node;
    }

    /* それ以外は「単文 (+ 行末修飾子)」 */
    Node *stmt = parse_simple_statement();
    int stmt_line = tokens[pos - 1].line; /* 単文の最後に消費したトークンの行番号 */
    /* if/unless が次の文の先頭である(=改行を挟んでいる)場合は修飾子として
     * 取り込まない。同じ行に書かれている時だけ修飾子とみなす。 */
    if (peek()->kind == T_IF && peek()->line == stmt_line) {
        advance();
        Node *cond = parse_expr();
        Node *node = new_node(N_IF, t->line);
        node->a = cond;
        node->b = stmt; /* 単文をそのまま実行対象にする(ブロック化しない) */
        node->c = NULL;
        return node;
    }
    if (peek()->kind == T_UNLESS && peek()->line == stmt_line) {
        advance();
        Node *cond = parse_expr();
        Node *node = new_node(N_IF, t->line);
        /* unless cond は if !cond と等価。BINOP "==" 0 で否定を表現せず、
         * 専用フラグの代わりに text に "unless" を積んで実行側で分岐する。 */
        snprintf(node->text, MAX_IDENT, "unless");
        node->a = cond;
        node->b = stmt;
        node->c = NULL;
        return node;
    }
    return stmt;
}

static Node *parse_program(void) {
    Node *block = new_node(N_BLOCK, 1);
    while (peek()->kind != T_EOF) {
        if (block->child_count >= MAX_CHILDREN) die("文が多すぎます (%d行目)", peek()->line);
        block->children[block->child_count++] = parse_statement();
    }
    return block;
}

/* ===== Value / Interpreter ===== */

typedef enum { V_NUM, V_STR } ValueKind;

typedef struct {
    ValueKind kind;
    double num;
    char str[MAX_IDENT];
} Value;

typedef struct {
    char name[MAX_IDENT];
    Value value;
} Var;

static Var vars[MAX_VARS];
static int var_count = 0;

static Value *find_var(const char *name) {
    for (int i = 0; i < var_count; i++) {
        if (strcmp(vars[i].name, name) == 0) return &vars[i].value;
    }
    return NULL;
}

static void set_var(const char *name, Value value) {
    Value *existing = find_var(name);
    if (existing) { *existing = value; return; }
    if (var_count >= MAX_VARS) die("変数数が上限を超えました");
    snprintf(vars[var_count].name, MAX_IDENT, "%s", name);
    vars[var_count].value = value;
    var_count++;
}

static Value make_num(double n) { Value v; v.kind = V_NUM; v.num = n; v.str[0] = '\0'; return v; }
static Value make_str(const char *s) { Value v; v.kind = V_STR; v.num = 0; snprintf(v.str, MAX_IDENT, "%s", s); return v; }

static Value eval(Node *node);

static int truthy(Value v) {
    if (v.kind == V_NUM) return v.num != 0;
    return v.str[0] != '\0';
}

/* 数値専用演算子に文字列が来たら、その場で行番号付きで止める。
 * Pythonのように暗黙変換しないことで型ミスを早期に検出する。 */
static void require_num(const char *op, Value v, int line) {
    if (v.kind != V_NUM) die("演算子 '%s' には数値が必要です (%d行目)", op, line);
}

static Value apply_op(const char *op, Value left, Value right, int line) {
    if (strcmp(op, "+") == 0) {
        if (left.kind == V_STR || right.kind == V_STR) {
            if (left.kind != V_STR || right.kind != V_STR)
                die("'+' で文字列と数値は連結できません (%d行目)", line);
            char buf[MAX_IDENT];
            snprintf(buf, sizeof(buf), "%s%s", left.str, right.str);
            return make_str(buf);
        }
        return make_num(left.num + right.num);
    }
    if (strcmp(op, "==") == 0) {
        if (left.kind != right.kind) return make_num(0);
        if (left.kind == V_STR) return make_num(strcmp(left.str, right.str) == 0);
        return make_num(left.num == right.num);
    }
    if (strcmp(op, "!=") == 0) {
        if (left.kind != right.kind) return make_num(1);
        if (left.kind == V_STR) return make_num(strcmp(left.str, right.str) != 0);
        return make_num(left.num != right.num);
    }

    require_num(op, left, line);
    require_num(op, right, line);
    if (strcmp(op, "-") == 0) return make_num(left.num - right.num);
    if (strcmp(op, "*") == 0) return make_num(left.num * right.num);
    if (strcmp(op, "/") == 0) {
        if (right.num == 0) die("0で除算しました (%d行目)", line);
        return make_num(left.num / right.num);
    }
    if (strcmp(op, "%") == 0) {
        if ((long)right.num == 0) die("0で剰余演算しました (%d行目)", line);
        return make_num((double)((long)left.num % (long)right.num));
    }
    if (strcmp(op, "<") == 0) return make_num(left.num < right.num);
    if (strcmp(op, ">") == 0) return make_num(left.num > right.num);
    if (strcmp(op, "<=") == 0) return make_num(left.num <= right.num);
    if (strcmp(op, ">=") == 0) return make_num(left.num >= right.num);
    die("未知の演算子 %s (%d行目)", op, line);
    return make_num(0);
}

static void print_value(Value v) {
    if (v.kind == V_STR) {
        printf("%s\n", v.str);
    } else if (v.num == (long)v.num) {
        printf("%ld\n", (long)v.num);
    } else {
        printf("%g\n", v.num);
    }
}

static void exec_stmt(Node *stmt);

static void exec_block(Node *block) {
    for (int i = 0; i < block->child_count; i++) {
        exec_stmt(block->children[i]);
    }
}

static void exec_stmt(Node *stmt) {
    switch (stmt->kind) {
        case N_ASSIGN:
            set_var(stmt->text, eval(stmt->a));
            break;
        case N_PRINT:
            print_value(eval(stmt->a));
            break;
        case N_IF: {
            int cond = truthy(eval(stmt->a));
            int is_unless = strcmp(stmt->text, "unless") == 0;
            if (is_unless) cond = !cond;
            if (cond) {
                /* b が単文(N_ASSIGN/N_PRINT)なら直接実行、ブロックなら exec_block */
                if (is_modifiable(stmt->b)) exec_stmt(stmt->b);
                else exec_block(stmt->b);
            } else if (stmt->c) {
                exec_block(stmt->c);
            }
            break;
        }
        case N_WHILE:
            while (truthy(eval(stmt->a))) exec_block(stmt->b);
            break;
        case N_BLOCK:
            exec_block(stmt);
            break;
        default:
            die("未知の文 (%d行目)", stmt->line);
    }
}

static Value eval(Node *node) {
    switch (node->kind) {
        case N_NUM: return make_num(node->num);
        case N_STR: return make_str(node->text);
        case N_VAR: {
            Value *v = find_var(node->text);
            if (!v) die("未定義の変数 '%s' (%d行目)", node->text, node->line);
            return *v;
        }
        case N_BINOP: {
            Value left = eval(node->a);
            Value right = eval(node->b);
            return apply_op(node->text, left, right, node->line);
        }
        default:
            die("未知の式 (%d行目)", node->line);
    }
    return make_num(0);
}

/* ===== Main ===== */

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) die("ファイルを開けません: %s", path);
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc(size + 1);
    size_t read = fread(buf, 1, size, f);
    buf[read] = '\0';
    fclose(f);
    return buf;
}

int main(int argc, char **argv) {
#if defined(_WIN32)
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
    if (argc != 2) {
        fprintf(stderr, "使い方: %s <file.lp>\n", argv[0]);
        fprintf(stderr, "       %s update      (最新版を確認・更新)\n", argv[0]);
        fprintf(stderr, "       %s --version\n", argv[0]);
        return 1;
    }
    if (strcmp(argv[1], "update") == 0 || strcmp(argv[1], "--update") == 0) {
        run_self_update(LAPING_VERSION);
        return 0;
    }
    if (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-v") == 0) {
        printf("laping %s\n", LAPING_VERSION);
        return 0;
    }
    char *src = read_file(argv[1]);
    tokenize(src);
    Node *program = parse_program();
    exec_block(program);
    free(src);
    return 0;
}
