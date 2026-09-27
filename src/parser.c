/* 構文解析: トークン列 -> AST（再帰下降）
 *
 * 文は改行か ';' で区切る。単文（代入・式・return など）の直後の
 * 同じ行に if / unless / while / until があれば後置修飾子として扱う。
 * 改行で区切られた次の行の if は修飾子にならない（字句解析で改行が
 * トークンとして挟まるため、自然にそうなる）。
 */
#include "laping.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

typedef struct {
    TokenList tl;
    int pos;
    const char *file;
} Parser;

static Parser P;
const char *cur_parse_file = NULL;

/* ---- ユーティリティ ---- */

static Token *peek(void) { return &P.tl.toks[P.pos]; }
static Token *peek_at(int n) {
    int i = P.pos + n;
    if (i >= P.tl.count) i = P.tl.count - 1;
    return &P.tl.toks[i];
}
static Token *advance(void) {
    Token *t = &P.tl.toks[P.pos];
    if (t->kind != T_EOF) P.pos++;
    return t;
}
static int check(TokKind k) { return peek()->kind == k; }
static int accept(TokKind k) {
    if (check(k)) {
        advance();
        return 1;
    }
    return 0;
}

int syntax_col = 0;

/* トークンの位置を指す構文エラー */
LP_NORETURN static void perr(Token *t, const char *fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    syntax_col = t->col;
    syntax_error(t->line, P.file, "%s", buf);
}

LP_NORETURN static void unexpected(const char *context) {
    Token *t = peek();
    syntax_col = t->col;
    if (t->kind == T_EOF) perr(t, "%s途中でファイルが終わりました（'}' や ')' が閉じられていない可能性があります）", context);
    if (t->kind == T_IDENT) perr(t, "%s予期しない識別子 '%s' があります", context, t->str);
    perr(t, "%s予期しない %s があります", context, tok_kind_name(t->kind));
}

static Token *expect(TokKind k, const char *where) {
    if (!check(k)) {
        Token *t = peek();
        syntax_col = t->col;
        if (k == T_RPAREN || k == T_RBRACKET || k == T_RBRACE)
            set_error_hint("手前の %s が閉じられているか確認してください",
                           k == T_RPAREN ? "'('" : k == T_RBRACKET ? "'['" : "'{'");
        if (t->kind == T_EOF)
            perr(t, "%s%s が必要ですが、ファイルが終わりました", where, tok_kind_name(k));
        perr(t, "%s%s が必要ですが、%s がありました", where, tok_kind_name(k), tok_kind_name(t->kind));
    }
    return advance();
}

static void skip_newlines(void) {
    while (check(T_NEWLINE)) advance();
}

/* 改行を読み飛ばした先が k なら、そこまで進めて 1 を返す */
static int peek_past_newlines(TokKind k) {
    int i = P.pos;
    while (P.tl.toks[i].kind == T_NEWLINE) i++;
    if (P.tl.toks[i].kind == k) {
        P.pos = i;
        return 1;
    }
    return 0;
}

static Node *new_node(NodeKind kind, int line) {
    Node *n = xcalloc(1, sizeof(Node));
    n->kind = kind;
    n->line = line;
    n->file = P.file;
    /* 列番号は、直前に読んだ同じ行のトークンから取る */
    for (int i = P.pos - 1, k = 0; i >= 0 && k < 8; i--, k++) {
        if (P.tl.toks[i].line == line && P.tl.toks[i].kind != T_NEWLINE) {
            n->col = P.tl.toks[i].col;
            break;
        }
    }
    return n;
}

static void nl_push(NodeList *l, Node *n) {
    if (l->count >= l->cap) {
        l->cap = l->cap ? l->cap * 2 : 4;
        l->items = xrealloc(l->items, sizeof(Node *) * l->cap);
    }
    l->items[l->count++] = n;
}

static Node *str_node(const char *s, size_t len, int line) {
    Node *n = new_node(N_STR, line);
    n->constant = v_obj(VAL_STR, str_new_perm(s, len));
    return n;
}

/* 構文解析が終わったトークン列を解放する（文字列定数は AST 側にコピー済み） */
static void free_tokens(TokenList *tl) {
    for (int i = 0; i < tl->count; i++) {
        Token *t = &tl->toks[i];
        if (t->kind == T_STR) free(t->str);
        for (int j = 0; j < t->nparts; j++) free(t->parts[j].text);
        free(t->parts);
    }
    free(tl->toks);
}

static Node *parse_expr(void);
static Node *parse_statement(void);
static Node *parse_match_expr(int line);
static Node *parse_ternary(void);
static Node *parse_block(void);

/* ===================================================================== */
/*  式                                                                    */
/* ===================================================================== */

/* 埋め込み式の中で、括弧や文字列の外にある最後の ':' の位置（なければ len） */
static size_t find_format_colon(const char *s, size_t len) {
    int depth = 0;
    size_t last = len;
    for (size_t i = 0; i < len; i++) {
        char c = s[i];
        if (c == '"' || c == '\'') {
            char q = c;
            for (i++; i < len && s[i] != q; i++)
                if (s[i] == '\\') i++;
            continue;
        }
        if (c == '(' || c == '[' || c == '{') depth++;
        else if (c == ')' || c == ']' || c == '}') depth--;
        else if (c == ':' && depth == 0) last = i;
    }
    return last;
}

/* [埋め文字][< > ^][幅][.桁数] の形か */
static int format_spec_ok(const char *s, size_t len) {
    if (len == 0) return 0;
    size_t i = 0;
    size_t fl = utf8_char_len((unsigned char)s[0]);
    if (fl < len && (s[fl] == '<' || s[fl] == '>' || s[fl] == '^')) i = fl + 1;
    else if (s[0] == '<' || s[0] == '>' || s[0] == '^') i = 1;
    while (i < len && s[i] >= '0' && s[i] <= '9') i++;
    if (i < len && s[i] == '.') {
        i++;
        if (i >= len) return 0;
        while (i < len && s[i] >= '0' && s[i] <= '9') i++;
    }
    return i == len;
}

static Node *parse_interp(Token *t) {
    Node *n = new_node(N_INTERP, t->line);
    n->col = t->col;
    for (int i = 0; i < t->nparts; i++) {
        InterpPart *p = &t->parts[i];
        if (!p->is_expr) {
            nl_push(&n->list, str_node(p->text, p->len, p->line));
            continue;
        }
        /* {式:書式} なら、最後の ':' の後ろを書式として取り出す */
        size_t elen = p->len;
        Node *spec = NULL;
        size_t colon = find_format_colon(p->text, p->len);
        if (colon < p->len && format_spec_ok(p->text + colon + 1, p->len - colon - 1)) {
            spec = str_node(p->text + colon + 1, p->len - colon - 1, p->line);
            elen = colon;
        }
        Parser saved = P;
        P.tl = lex(p->text, elen, p->line, p->col);
        P.pos = 0;
        skip_newlines();
        if (check(T_EOF)) {
            syntax_col = p->col;
            set_error_hint("{ } を文字として書くには \\{ \\} とするか、'...' を使ってください");
            syntax_error(p->line, P.file, "文字列中の {} の中に式がありません");
        }
        Node *e = parse_expr();
        skip_newlines();
        if (!check(T_EOF)) unexpected("文字列中の埋め込み式に");
        free_tokens(&P.tl);
        P = saved;
        nl_push(&n->list, e);
        while (n->list2.count < n->list.count - 1) nl_push(&n->list2, NULL);
        nl_push(&n->list2, spec);
    }
    return n;
}

static void add_param(Node *fn, int *cap, const char *name) {
    if (fn->nparams >= *cap) {
        *cap = *cap ? *cap * 2 : 4;
        fn->params = xrealloc(fn->params, sizeof(char *) * *cap);
        fn->defaults = xrealloc(fn->defaults, sizeof(Node *) * *cap);
    }
    fn->params[fn->nparams] = name;
    fn->defaults[fn->nparams] = NULL;
}

/* (a, b = 1, ...rest) を読んで fn に設定する */
static void parse_params(Node *fn, int allow_variadic) {
    expect(T_LPAREN, "引数リストの前に ");
    int cap = 0;
    while (!check(T_RPAREN)) {
        int variadic = 0;
        if (allow_variadic && accept(T_DOTDOTDOT)) variadic = 1;
        Token *id = expect(T_IDENT, "引数名として ");
        for (int i = 0; i < fn->nparams; i++)
            if (fn->params[i] == id->str) perr(id, "引数名 '%s' が重複しています", id->str);
        add_param(fn, &cap, id->str);
        if (!variadic && accept(T_ASSIGN)) fn->defaults[fn->nparams] = parse_expr();
        else if (fn->nparams > 0 && fn->defaults[fn->nparams - 1] && !variadic)
            perr(id, "デフォルト値のある引数の後にデフォルト値のない引数は置けません");
        fn->nparams++;
        if (variadic) {
            fn->variadic = 1;
            if (!check(T_RPAREN)) perr(id, "可変長引数 ...%s は最後の引数にしてください", id->str);
            break;
        }
        if (!accept(T_COMMA)) break;
    }
    expect(T_RPAREN, "引数リストの後に ");
}

/* fn (a, b = 1, ...rest) => expr   /   fn (a) { ... }   /   fn x => expr */
static Node *parse_function_rest(int line, const char *name) {
    Node *fn = new_node(N_FUNC, line);
    fn->name = name;
    if (!name && check(T_IDENT) && peek_at(1)->kind == T_ARROW) {
        /* 引数が1つなら括弧を省略できる: fn x => x * 2 */
        int cap = 0;
        add_param(fn, &cap, advance()->str);
        fn->nparams = 1;
    } else {
        parse_params(fn, 1);
    }
    if (accept(T_ARROW)) {
        fn->op = 1; /* 式本体 */
        fn->a = parse_expr();
    } else if (check(T_LBRACE)) {
        fn->a = parse_block();
    } else {
        unexpected("関数の本体として '{' か '=>' が必要ですが、");
    }
    return fn;
}

/* for x in xs / for k, v in m の変数と対象、続く if 条件を読む */
static void parse_for_clause(Node *n, int in_brackets) {
    n->params = xmalloc(sizeof(char *) * 2);
    n->params[n->nparams++] = expect(T_IDENT, "for の後に変数名として ")->str;
    if (accept(T_COMMA)) n->params[n->nparams++] = expect(T_IDENT, "for の2つ目の変数名として ")->str;
    expect(T_IN, "for の変数の後に ");
    n->b = parse_expr();
    if (in_brackets) skip_newlines();
    if (accept(T_IF)) {
        n->c = parse_expr();
        if (in_brackets) skip_newlines();
    }
}

static Node *parse_list_literal(int line) {
    Node *n = new_node(N_LIST, line);
    skip_newlines();
    if (!check(T_RBRACKET)) {
        Node *first = parse_expr();
        skip_newlines();
        if (accept(T_FOR)) {
            /* リスト内包表記: [式 for x in xs if 条件] */
            Node *c = new_node(N_COMPREHENSION, line);
            c->a = first;
            parse_for_clause(c, 1);
            expect(T_RBRACKET, "リスト内包表記の終わりに ");
            return c;
        }
        nl_push(&n->list, first);
        if (!accept(T_COMMA)) {
            expect(T_RBRACKET, "リストの終わりに ");
            return n;
        }
        skip_newlines();
    }
    while (!check(T_RBRACKET)) {
        nl_push(&n->list, parse_expr());
        skip_newlines();
        if (!accept(T_COMMA)) break;
        skip_newlines();
    }
    skip_newlines();
    expect(T_RBRACKET, "リストの終わりに ");
    return n;
}

static Node *parse_map_literal(int line) {
    Node *n = new_node(N_MAP, line);
    skip_newlines();
    while (!check(T_RBRACE)) {
        Node *key;
        Node *val;
        Token *key_ident = NULL; /* {name: ...} の name（内包表記なら変数として読む） */
        if (check(T_IDENT) && peek_at(1)->kind == T_COLON) {
            Token *id = advance();
            key_ident = id;
            key = str_node(id->str, id->len, id->line);
            advance(); /* ':' */
            skip_newlines();
            val = parse_expr();
        } else if (check(T_IDENT) && (peek_at(1)->kind == T_COMMA || peek_at(1)->kind == T_RBRACE || peek_at(1)->kind == T_NEWLINE)) {
            /* {name} は {name: name} の省略形 */
            Token *id = advance();
            key = str_node(id->str, id->len, id->line);
            val = new_node(N_IDENT, id->line);
            val->name = id->str;
        } else {
            key = parse_expr();
            skip_newlines();
            expect(T_COLON, "マップのキーの後に ");
            skip_newlines();
            val = parse_expr();
        }
        skip_newlines();
        if (n->list.count == 0 && check(T_FOR)) {
            /* マップ内包表記: {キー: 値 for x in xs if 条件} */
            advance();
            Node *c = new_node(N_COMPREHENSION, line);
            c->op = 1;
            if (key_ident) {
                key = new_node(N_IDENT, key_ident->line);
                key->name = key_ident->str;
            }
            c->a = key;
            c->d = val;
            parse_for_clause(c, 1);
            expect(T_RBRACE, "マップ内包表記の終わりに ");
            return c;
        }
        nl_push(&n->list, key);
        nl_push(&n->list2, val);
        if (!accept(T_COMMA)) break;
        skip_newlines();
    }
    skip_newlines();
    expect(T_RBRACE, "マップの終わりに ");
    return n;
}

static Node *parse_primary(void) {
    Token *t = peek();
    switch (t->kind) {
        case T_NUM: {
            advance();
            Node *n = new_node(N_NUM, t->line);
            n->col = t->col;
            n->num = t->num;
            return n;
        }
        case T_STR:
            advance();
            return str_node(t->str, t->len, t->line);
        case T_INTERP:
            advance();
            return parse_interp(t);
        case T_TRUE:
        case T_FALSE: {
            advance();
            Node *n = new_node(N_BOOL, t->line);
            n->col = t->col;
            n->op = t->kind == T_TRUE;
            return n;
        }
        case T_NIL:
            advance();
            return new_node(N_NIL, t->line);
        case T_IDENT: {
            advance();
            Node *n = new_node(N_IDENT, t->line);
            n->col = t->col;
            n->name = t->str;
            return n;
        }
        case T_LPAREN: {
            advance();
            Node *e = parse_expr();
            expect(T_RPAREN, "");
            return e;
        }
        case T_LBRACKET:
            advance();
            return parse_list_literal(t->line);
        case T_LBRACE:
            advance();
            return parse_map_literal(t->line);
        case T_FN:
            advance();
            if (check(T_IDENT) && peek_at(1)->kind != T_ARROW) {
                Token *id = advance();
                return parse_function_rest(t->line, id->str);
            }
            return parse_function_rest(t->line, NULL);
        case T_MATCH:
            advance();
            return parse_match_expr(t->line);
        case T_THROW: {
            /* 値 else throw "メッセージ" のように式の中でも使える */
            advance();
            Node *n = new_node(N_THROW, t->line);
            n->col = t->col;
            n->a = parse_ternary();
            return n;
        }
        default:
            unexpected("式が必要ですが、");
    }
    return NULL;
}

static void parse_call_args(Node *call) {
    while (!check(T_RPAREN)) {
        nl_push(&call->list, parse_expr());
        skip_newlines();
        if (!accept(T_COMMA)) break;
        skip_newlines();
    }
    expect(T_RPAREN, "関数呼び出しの終わりに ");
}

static Node *parse_postfix(void) {
    Node *e = parse_primary();
    for (;;) {
        Token *t = peek();
        if (t->kind == T_LPAREN) {
            advance();
            Node *call = new_node(N_CALL, t->line);
            call->a = e;
            parse_call_args(call);
            e = call;
        } else if (t->kind == T_LBRACKET) {
            advance();
            Node *idx = new_node(N_INDEX, t->line);
            idx->a = e;
            idx->b = parse_expr();
            expect(T_RBRACKET, "添字の後に ");
            e = idx;
        } else if (t->kind == T_DOT) {
            advance();
            Token *id = expect(T_IDENT, "'.' の後に ");
            if (check(T_LPAREN)) {
                advance();
                Node *m = new_node(N_METHOD, id->line);
                m->col = id->col;
                m->a = e;
                m->name = id->str;
                parse_call_args(m);
                e = m;
            } else {
                Node *f = new_node(N_FIELD, id->line);
                f->col = id->col;
                f->a = e;
                f->name = id->str;
                e = f;
            }
        } else {
            return e;
        }
    }
}

static Node *parse_unary(void);

static Node *parse_power(void) {
    Node *base = parse_postfix();
    if (check(T_POW)) {
        Token *t = advance();
        Node *n = new_node(N_BINARY, t->line);
        n->col = t->col;
        n->op = T_POW;
        n->a = base;
        n->b = parse_unary(); /* 右結合: 2 ** 3 ** 2 == 2 ** 9 */
        return n;
    }
    return base;
}

static Node *parse_unary(void) {
    Token *t = peek();
    if (t->kind == T_MINUS || t->kind == T_BANG || t->kind == T_PLUS) {
        advance();
        Node *operand = parse_unary();
        if (t->kind == T_PLUS) return operand;
        if (t->kind == T_MINUS && operand->kind == N_NUM) {
            operand->num = -operand->num;
            return operand;
        }
        Node *n = new_node(N_UNARY, t->line);
        n->col = t->col;
        n->op = t->kind == T_BANG ? T_NOT : T_MINUS;
        n->a = operand;
        return n;
    }
    return parse_power();
}

static Node *binary(Token *t, TokKind op, Node *a, Node *b) {
    Node *n = new_node(N_BINARY, t->line);
    n->col = t->col;
    n->col = t->col; /* エラー表示では演算子の位置を指す */
    n->op = op;
    n->a = a;
    n->b = b;
    return n;
}

static Node *parse_term(void) {
    Node *e = parse_unary();
    while (check(T_STAR) || check(T_SLASH) || check(T_SLASHSLASH) || check(T_PERCENT)) {
        Token *t = advance();
        e = binary(t, t->kind, e, parse_unary());
    }
    return e;
}

static Node *parse_additive(void) {
    Node *e = parse_term();
    while (check(T_PLUS) || check(T_MINUS)) {
        Token *t = advance();
        e = binary(t, t->kind, e, parse_term());
    }
    return e;
}

static Node *parse_range(void) {
    Node *e = parse_additive();
    if (check(T_DOTDOT) || check(T_DOTDOTDOT)) {
        Token *t = advance();
        Node *n = new_node(N_RANGE, t->line);
        n->col = t->col;
        n->op = t->kind == T_DOTDOT; /* 1 = 終端を含む */
        n->a = e;
        n->b = parse_additive();
        return n;
    }
    return e;
}

static int is_cmp_op(TokKind k) {
    return k == T_EQ || k == T_NE || k == T_LT || k == T_GT || k == T_LE || k == T_GE;
}

static Node *parse_comparison(void) {
    Node *e = parse_range();
    for (;;) {
        Token *t = peek();
        TokKind k = t->kind;
        if (is_cmp_op(k)) {
            /* 1 < x < 10 のような連鎖比較。各項は1回だけ評価する */
            Node *chain = new_node(N_CMPCHAIN, t->line);
            nl_push(&chain->list, e);
            while (is_cmp_op(peek()->kind)) {
                Token *opt = advance();
                Node *opn = new_node(N_NUM, opt->line);
                opn->op = opt->kind;
                opn->col = opt->col;
                nl_push(&chain->list2, opn);
                nl_push(&chain->list, parse_range());
            }
            if (chain->list2.count == 1) {
                Node *b = binary(t, k, chain->list.items[0], chain->list.items[1]);
                b->col = t->col;
                e = b;
            } else {
                e = chain;
            }
        } else if (k == T_IN) {
            advance();
            e = binary(t, k, e, parse_range());
        } else if (k == T_IS) {
            /* x is list / x is not nil / p is Point */
            advance();
            Node *n = new_node(N_IS, t->line);
            n->col = t->col;
            n->a = e;
            n->op = accept(T_NOT);
            if (accept(T_NIL)) n->name = intern("nil", 3);
            else n->name = expect(T_IDENT, "'is' の後に型名として ")->str;
            e = n;
        } else if (k == T_NOT && peek_at(1)->kind == T_IN) {
            advance();
            advance();
            Node *in = binary(t, T_IN, e, parse_range());
            Node *n = new_node(N_UNARY, t->line);
            n->col = t->col;
            n->op = T_NOT;
            n->a = in;
            e = n;
        } else {
            return e;
        }
    }
}

static Node *parse_not(void) {
    if (check(T_NOT)) {
        Token *t = advance();
        Node *n = new_node(N_UNARY, t->line);
        n->col = t->col;
        n->op = T_NOT;
        n->a = parse_not();
        return n;
    }
    return parse_comparison();
}

static Node *parse_and(void) {
    Node *e = parse_not();
    while (check(T_AND) || check(T_ANDAND)) {
        Token *t = advance();
        Node *n = new_node(N_AND, t->line);
        n->col = t->col;
        n->a = e;
        n->b = parse_not();
        e = n;
    }
    return e;
}

static Node *parse_or(void) {
    Node *e = parse_and();
    while (check(T_OR) || check(T_OROR)) {
        Token *t = advance();
        Node *n = new_node(N_OR, t->line);
        n->col = t->col;
        n->a = e;
        n->b = parse_and();
        e = n;
    }
    return e;
}

/* x -> f(a) は f(x, a)、x -> g は g(x) になる（データが矢印の向きに流れる） */
static Node *parse_pipe(void) {
    Node *e = parse_or();
    while (check(T_PIPE)) {
        Token *t = advance();
        skip_newlines();
        Node *rhs = parse_or();
        if (rhs->kind == N_CALL || rhs->kind == N_METHOD) {
            nl_push(&rhs->list, NULL);
            memmove(&rhs->list.items[1], &rhs->list.items[0], sizeof(Node *) * (size_t)(rhs->list.count - 1));
            rhs->list.items[0] = e;
            e = rhs;
        } else {
            Node *call = new_node(N_CALL, t->line);
            call->a = rhs;
            nl_push(&call->list, e);
            e = call;
        }
    }
    return e;
}

static Node *parse_ternary(void) {
    Node *cond = parse_pipe();
    if (check(T_THEN)) {
        /* 条件 then 値1 else 値2 */
        Token *t = advance();
        Node *n = new_node(N_TERNARY, t->line);
        n->col = t->col;
        n->a = cond;
        n->b = parse_pipe();
        expect(T_ELSE, "then の値の後に ");
        n->c = parse_ternary();
        return n;
    }
    if (check(T_ELSE)) {
        /* 値 else 既定値: 値が nil、または途中のキー・添字が存在しなければ既定値 */
        Token *t = advance();
        Node *n = new_node(N_FALLBACK, t->line);
        n->col = t->col;
        n->a = cond;
        n->b = parse_ternary();
        return n;
    }
    if (check(T_QUESTION)) {
        Token *t = advance();
        Node *n = new_node(N_TERNARY, t->line);
        n->col = t->col;
        n->a = cond;
        n->b = parse_ternary();
        expect(T_COLON, "三項演算子 ?: の ");
        n->c = parse_ternary();
        return n;
    }
    return cond;
}

static Node *parse_expr(void) { return parse_ternary(); }

/* ===================================================================== */
/*  文                                                                    */
/* ===================================================================== */

static int at_terminator(void) {
    TokKind k = peek()->kind;
    return k == T_NEWLINE || k == T_SEMI || k == T_RBRACE || k == T_EOF;
}

/* 文の先頭の expect の後に式が続くか（expect = 1 のような代入とは区別する） */
static int starts_expr(TokKind k) {
    switch (k) {
        case T_NUM: case T_STR: case T_INTERP: case T_IDENT: case T_TRUE: case T_FALSE:
        case T_NIL: case T_LBRACKET: case T_LBRACE: case T_MINUS: case T_BANG: case T_NOT:
        case T_FN: case T_MATCH: case T_LPAREN:
            return 1;
        default:
            return 0;
    }
}

static int at_modifier(void) {
    TokKind k = peek()->kind;
    return k == T_IF || k == T_UNLESS || k == T_WHILE || k == T_UNTIL || k == T_FOR || k == T_REPEAT;
}

static void parse_statements_until(NodeList *out, TokKind end) {
    for (;;) {
        while (check(T_NEWLINE) || check(T_SEMI)) advance();
        if (check(end)) return;
        if (check(T_EOF)) perr(peek(), "ブロックの { } が閉じられていません");
        nl_push(out, parse_statement());
    }
}

static Node *parse_block(void) {
    Token *t = expect(T_LBRACE, "ブロックの始まりに ");
    Node *b = new_node(N_BLOCK, t->line);
    parse_statements_until(&b->list, T_RBRACE);
    expect(T_RBRACE, "");
    return b;
}

static Node *negate(Node *cond) {
    Node *n = new_node(N_UNARY, cond->line);
    n->op = T_NOT;
    n->a = cond;
    return n;
}

static int is_assignable(Node *n) {
    return n->kind == N_IDENT || n->kind == N_INDEX || n->kind == N_FIELD;
}

static Node *parse_if(int line, int is_unless) {
    Node *n = new_node(N_IF, line);
    Node *cond = parse_expr();
    n->a = is_unless ? negate(cond) : cond;
    n->b = parse_block();
    if (peek_past_newlines(T_ELIF)) {
        Token *t = advance();
        n->c = parse_if(t->line, 0);
    } else if (peek_past_newlines(T_ELSE)) {
        advance();
        if (check(T_IF)) {
            Token *t = advance();
            n->c = parse_if(t->line, 0);
        } else {
            n->c = parse_block();
        }
    }
    return n;
}

static Node *parse_simple(void) {
    Token *t = peek();
    switch (t->kind) {
        case T_LET: {
            advance();
            Node *n = new_node(N_LET, t->line);
            n->col = t->col;
            int cap = 0;
            do {
                Token *id = expect(T_IDENT, "let の後に変数名として ");
                if (n->nparams >= cap) {
                    cap = cap ? cap * 2 : 4;
                    n->params = xrealloc(n->params, sizeof(char *) * cap);
                }
                n->params[n->nparams++] = id->str;
            } while (accept(T_COMMA));
            if (accept(T_ASSIGN)) {
                do {
                    nl_push(&n->list2, parse_expr());
                } while (accept(T_COMMA));
            }
            return n;
        }
        case T_RETURN: {
            advance();
            Node *n = new_node(N_RETURN, t->line);
            n->col = t->col;
            if (!at_terminator() && !at_modifier()) n->a = parse_expr();
            return n;
        }
        case T_BREAK:
            advance();
            return new_node(N_BREAK, t->line);
        case T_CONTINUE:
            advance();
            return new_node(N_CONTINUE, t->line);
        case T_THROW: {
            advance();
            Node *n = new_node(N_THROW, t->line);
            n->col = t->col;
            n->a = parse_expr();
            return n;
        }
        default:
            break;
    }

    Node *e = parse_expr();
    if (check(T_ASSIGN) || check(T_COMMA)) {
        Node *n = new_node(N_ASSIGN, t->line);
        n->col = t->col;
        nl_push(&n->list, e);
        while (accept(T_COMMA)) nl_push(&n->list, parse_expr());
        Token *eq = expect(T_ASSIGN, "代入の ");
        n->line = eq->line;
        for (int i = 0; i < n->list.count; i++)
            if (!is_assignable(n->list.items[i]))
                perr(eq, "代入の左辺には変数・添字・フィールドのみ書けます");
        do {
            nl_push(&n->list2, parse_expr());
        } while (accept(T_COMMA));
        if (n->list2.count != 1 && n->list2.count != n->list.count)
            perr(eq, "代入の左辺 (%d個) と右辺 (%d個) の数が合いません", n->list.count, n->list2.count);
        return n;
    }
    TokKind k = peek()->kind;
    if (k == T_PLUS_EQ || k == T_MINUS_EQ || k == T_STAR_EQ || k == T_SLASH_EQ || k == T_PERCENT_EQ) {
        Token *op = advance();
        if (!is_assignable(e)) perr(op, "複合代入の左辺には変数・添字・フィールドのみ書けます");
        Node *n = new_node(N_COMPOUND, op->line);
        n->op = k == T_PLUS_EQ ? T_PLUS : k == T_MINUS_EQ ? T_MINUS : k == T_STAR_EQ ? T_STAR : k == T_SLASH_EQ ? T_SLASH : T_PERCENT;
        n->a = e;
        n->b = parse_expr();
        return n;
    }
    Node *n = new_node(N_EXPR_STMT, t->line);
    n->col = t->col;
    n->a = e;
    return n;
}

/* match の1つの枝の「パターン, パターン if 条件 =>」まで */
static Node *parse_match_arm_head(void) {
    Node *arm = new_node(N_MATCH_ARM, peek()->line);
    do {
        if (check(T_IDENT) && strcmp(peek()->str, "_") == 0 &&
            (peek_at(1)->kind == T_ARROW || peek_at(1)->kind == T_COMMA || peek_at(1)->kind == T_IF)) {
            advance();
            arm->op = 1; /* ワイルドカード */
        } else {
            nl_push(&arm->list, parse_expr());
        }
    } while (accept(T_COMMA));
    if (accept(T_IF)) arm->a = parse_expr();
    expect(T_ARROW, "match のパターンの後に ");
    return arm;
}

/* 式としての match: 一致した枝の値になる（どれにも一致しなければ nil） */
static Node *parse_match_expr(int line) {
    Node *n = new_node(N_MATCH_EXPR, line);
    n->a = parse_expr();
    expect(T_LBRACE, "match の対象の後に ");
    for (;;) {
        while (check(T_NEWLINE) || check(T_SEMI) || check(T_COMMA)) advance();
        if (accept(T_RBRACE)) break;
        if (check(T_EOF)) perr(peek(), "match の { } が閉じられていません");
        Node *arm = parse_match_arm_head();
        arm->b = parse_expr();
        nl_push(&n->list, arm);
        if (!check(T_NEWLINE) && !check(T_COMMA) && !check(T_SEMI) && !check(T_RBRACE))
            unexpected("match の各枝の後に");
    }
    return n;
}

static Node *parse_match(int line) {
    Node *n = new_node(N_MATCH, line);
    n->a = parse_expr();
    expect(T_LBRACE, "match の対象の後に ");
    for (;;) {
        while (check(T_NEWLINE) || check(T_SEMI)) advance();
        if (accept(T_RBRACE)) break;
        if (check(T_EOF)) perr(peek(), "match の { } が閉じられていません");
        Node *arm = parse_match_arm_head();
        if (check(T_LBRACE)) {
            arm->b = parse_block();
        } else {
            arm->b = parse_simple();
            if (!at_terminator()) unexpected("match の各パターンの後に");
        }
        nl_push(&n->list, arm);
    }
    return n;
}

static Node *parse_statement(void) {
    Token *t = peek();
    switch (t->kind) {
        case T_IF:
            advance();
            return parse_if(t->line, 0);
        case T_UNLESS:
            advance();
            return parse_if(t->line, 1);
        case T_WHILE:
        case T_UNTIL: {
            advance();
            Node *n = new_node(N_WHILE, t->line);
            n->col = t->col;
            Node *cond = parse_expr();
            n->a = t->kind == T_UNTIL ? negate(cond) : cond;
            n->b = parse_block();
            return n;
        }
        case T_LOOP: {
            advance();
            Node *n = new_node(N_LOOP, t->line);
            n->col = t->col;
            n->b = parse_block();
            return n;
        }
        case T_FOR: {
            advance();
            Node *n = new_node(N_FOR, t->line);
            n->col = t->col;
            n->params = xmalloc(sizeof(char *) * 2);
            n->params[n->nparams++] = expect(T_IDENT, "for の後に変数名として ")->str;
            if (accept(T_COMMA)) n->params[n->nparams++] = expect(T_IDENT, "for の2つ目の変数名として ")->str;
            expect(T_IN, "for の変数の後に ");
            n->a = parse_expr();
            n->b = parse_block();
            return n;
        }
        case T_MATCH:
            advance();
            return parse_match(t->line);
        case T_REPEAT: {
            /* repeat 回数 { ... } */
            advance();
            Node *n = new_node(N_REPEAT, t->line);
            n->col = t->col;
            n->a = parse_expr();
            n->b = parse_block();
            return n;
        }
        case T_IDENT:
            if (strcmp(t->str, "record") == 0 && peek_at(1)->kind == T_IDENT && peek_at(2)->kind == T_LPAREN) {
                /* record Point(x, y = 0) */
                advance();
                Token *id = advance();
                Node *n = new_node(N_RECORD, t->line);
                n->col = t->col;
                n->name = id->str;
                parse_params(n, 0);
                return n;
            }
            if (strcmp(t->str, "expect") == 0 && starts_expr(peek_at(1)->kind)) {
                /* expect 1 + 1 == 2   失敗すると左辺と右辺の値を表示する */
                advance();
                Node *n = new_node(N_EXPECT, t->line);
                n->col = t->col;
                n->a = parse_expr();
                if (!at_terminator()) unexpected("expect の式の後に");
                return n;
            }
            if (strcmp(t->str, "test") == 0 && peek_at(1)->kind == T_STR && peek_at(2)->kind == T_LBRACE) {
                /* test "名前" { ... } */
                advance();
                Token *name = advance();
                Node *n = new_node(N_TEST, t->line);
                n->col = t->col;
                n->constant = v_obj(VAL_STR, str_new_perm(name->str, name->len));
                n->b = parse_block();
                return n;
            }
            break;
        case T_TRY: {
            advance();
            Node *n = new_node(N_TRY, t->line);
            n->col = t->col;
            n->a = parse_block();
            if (peek_past_newlines(T_CATCH)) {
                advance();
                if (check(T_IDENT)) n->name = advance()->str;
                n->b = parse_block();
            }
            if (peek_past_newlines(T_FINALLY)) {
                advance();
                n->c = parse_block();
            }
            if (!n->b && !n->c) perr(t, "try の後には catch か finally が必要です");
            return n;
        }
        case T_IMPORT: {
            advance();
            Node *n = new_node(N_IMPORT, t->line);
            n->col = t->col;
            Token *s = expect(T_STR, "import の後にファイル名の ");
            n->constant = v_obj(VAL_STR, str_new_perm(s->str, s->len));
            if (check(T_IDENT) && strcmp(peek()->str, "as") == 0) {
                /* import "lib/util" as util */
                advance();
                n->name = expect(T_IDENT, "as の後に名前として ")->str;
            }
            return n;
        }
        case T_FN:
            if (peek_at(1)->kind == T_IDENT && peek_at(2)->kind == T_DOT && peek_at(3)->kind == T_IDENT) {
                /* fn Point.len(p) { ... }  型にメソッドを追加する */
                advance();
                Token *owner = advance();
                advance();
                Token *id = advance();
                Node *n = new_node(N_FNDECL, t->line);
                n->col = t->col;
                n->owner = owner->str;
                n->name = id->str;
                n->a = parse_function_rest(t->line, id->str);
                return n;
            }
            if (peek_at(1)->kind == T_IDENT && peek_at(2)->kind != T_ARROW) {
                advance();
                Token *id = advance();
                Node *n = new_node(N_FNDECL, t->line);
                n->col = t->col;
                n->name = id->str;
                n->a = parse_function_rest(t->line, id->str);
                return n;
            }
            break;
        case T_ELSE:
        case T_ELIF:
            perr(t, "対応する if のない %s です", tok_kind_name(t->kind));
            break;
        case T_CATCH:
        case T_FINALLY:
            perr(t, "対応する try のない %s です", tok_kind_name(t->kind));
            break;
        default:
            break;
    }

    Node *s = parse_simple();
    /* 後置修飾子: 同じ行に続く if / unless / while / until / for / repeat */
    if (check(T_FOR)) {
        /* print(x) for x in xs if x > 0 */
        Token *m = advance();
        Node *n = new_node(N_FOR, m->line);
        parse_for_clause(n, 0);
        if (n->c) {
            Node *guard = new_node(N_IF, m->line);
            guard->a = n->c;
            guard->b = s;
            n->c = NULL;
            s = guard;
        }
        n->a = n->b;
        n->b = s;
        if (!at_terminator()) unexpected("文の終わりに");
        return n;
    }
    if (check(T_REPEAT)) {
        /* print("やあ") repeat 3 */
        Token *m = advance();
        Node *n = new_node(N_REPEAT, m->line);
        n->a = parse_expr();
        n->b = s;
        if (!at_terminator()) unexpected("文の終わりに");
        return n;
    }
    if (at_modifier()) {
        Token *m = advance();
        Node *cond = parse_expr();
        Node *n = new_node(m->kind == T_IF || m->kind == T_UNLESS ? N_IF : N_WHILE, m->line);
        n->a = (m->kind == T_UNLESS || m->kind == T_UNTIL) ? negate(cond) : cond;
        n->b = s;
        s = n;
    }
    if (!at_terminator()) unexpected("文の終わりに");
    return s;
}

Node *parse_program(const char *src, size_t len, const char *file) {
    Parser saved = P;
    const char *saved_file = cur_parse_file;
    P.file = file;
    cur_parse_file = file;
    source_register(file, src, len);
    P.tl = lex(src, len, 1, 1);
    P.pos = 0;
    Node *prog = new_node(N_BLOCK, 1);
    parse_statements_until(&prog->list, T_EOF);
    free_tokens(&P.tl);
    P = saved;
    cur_parse_file = saved_file;
    return prog;
}
