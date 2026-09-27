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

static void unexpected(const char *context) {
    Token *t = peek();
    if (t->kind == T_EOF) syntax_error(t->line, P.file, "%s途中でファイルが終わりました（'}' や ')' が閉じられていない可能性があります）", context);
    if (t->kind == T_IDENT) syntax_error(t->line, P.file, "%s予期しない識別子 '%s' があります", context, t->str);
    syntax_error(t->line, P.file, "%s予期しない %s があります", context, tok_kind_name(t->kind));
}

static Token *expect(TokKind k, const char *where) {
    if (!check(k)) {
        Token *t = peek();
        if (t->kind == T_EOF)
            syntax_error(t->line, P.file, "%s%s が必要ですが、ファイルが終わりました", where, tok_kind_name(k));
        syntax_error(t->line, P.file, "%s%s が必要ですが、%s がありました", where, tok_kind_name(k), tok_kind_name(t->kind));
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
static Node *parse_block(void);

/* ===================================================================== */
/*  式                                                                    */
/* ===================================================================== */

static Node *parse_interp(Token *t) {
    Node *n = new_node(N_INTERP, t->line);
    for (int i = 0; i < t->nparts; i++) {
        InterpPart *p = &t->parts[i];
        if (!p->is_expr) {
            nl_push(&n->list, str_node(p->text, p->len, p->line));
            continue;
        }
        Parser saved = P;
        P.tl = lex(p->text, p->len, p->line);
        P.pos = 0;
        skip_newlines();
        if (check(T_EOF)) syntax_error(p->line, P.file, "文字列中の {} の中に式がありません");
        Node *e = parse_expr();
        skip_newlines();
        if (!check(T_EOF)) unexpected("文字列中の埋め込み式に");
        free_tokens(&P.tl);
        P = saved;
        nl_push(&n->list, e);
    }
    return n;
}

/* fn (a, b = 1, ...rest) => expr   /   fn (a) { ... } */
static Node *parse_function_rest(int line, const char *name) {
    Node *fn = new_node(N_FUNC, line);
    fn->name = name;
    expect(T_LPAREN, "関数の引数リストの前に ");
    int cap = 0;
    while (!check(T_RPAREN)) {
        int variadic = 0;
        if (accept(T_DOTDOTDOT)) variadic = 1;
        Token *id = expect(T_IDENT, "引数名として ");
        if (fn->nparams >= cap) {
            cap = cap ? cap * 2 : 4;
            fn->params = xrealloc(fn->params, sizeof(char *) * cap);
            fn->defaults = xrealloc(fn->defaults, sizeof(Node *) * cap);
        }
        for (int i = 0; i < fn->nparams; i++)
            if (fn->params[i] == id->str) syntax_error(id->line, P.file, "引数名 '%s' が重複しています", id->str);
        fn->params[fn->nparams] = id->str;
        fn->defaults[fn->nparams] = NULL;
        if (!variadic && accept(T_ASSIGN)) fn->defaults[fn->nparams] = parse_expr();
        else if (fn->nparams > 0 && fn->defaults[fn->nparams - 1] && !variadic)
            syntax_error(id->line, P.file, "デフォルト値のある引数の後にデフォルト値のない引数は置けません");
        fn->nparams++;
        if (variadic) {
            fn->variadic = 1;
            if (!check(T_RPAREN)) syntax_error(id->line, P.file, "可変長引数 ...%s は最後の引数にしてください", id->str);
            break;
        }
        if (!accept(T_COMMA)) break;
    }
    expect(T_RPAREN, "関数の引数リストの後に ");
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

static Node *parse_list_literal(int line) {
    Node *n = new_node(N_LIST, line);
    skip_newlines();
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
        if (check(T_IDENT) && peek_at(1)->kind == T_COLON) {
            Token *id = advance();
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
        nl_push(&n->list, key);
        nl_push(&n->list2, val);
        skip_newlines();
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
            n->op = t->kind == T_TRUE;
            return n;
        }
        case T_NIL:
            advance();
            return new_node(N_NIL, t->line);
        case T_IDENT: {
            advance();
            Node *n = new_node(N_IDENT, t->line);
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
            if (check(T_IDENT)) {
                Token *id = advance();
                return parse_function_rest(t->line, id->str);
            }
            return parse_function_rest(t->line, NULL);
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
                m->a = e;
                m->name = id->str;
                parse_call_args(m);
                e = m;
            } else {
                Node *f = new_node(N_FIELD, id->line);
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
        n->op = t->kind == T_BANG ? T_NOT : T_MINUS;
        n->a = operand;
        return n;
    }
    return parse_power();
}

static Node *binary(Token *t, TokKind op, Node *a, Node *b) {
    Node *n = new_node(N_BINARY, t->line);
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
        n->op = t->kind == T_DOTDOT; /* 1 = 終端を含む */
        n->a = e;
        n->b = parse_additive();
        return n;
    }
    return e;
}

static Node *parse_comparison(void) {
    Node *e = parse_range();
    for (;;) {
        Token *t = peek();
        TokKind k = t->kind;
        if (k == T_EQ || k == T_NE || k == T_LT || k == T_GT || k == T_LE || k == T_GE || k == T_IN) {
            advance();
            e = binary(t, k, e, parse_range());
        } else if (k == T_NOT && peek_at(1)->kind == T_IN) {
            advance();
            advance();
            Node *in = binary(t, T_IN, e, parse_range());
            Node *n = new_node(N_UNARY, t->line);
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
        n->a = e;
        n->b = parse_and();
        e = n;
    }
    return e;
}

static Node *parse_ternary(void) {
    Node *cond = parse_or();
    if (check(T_QUESTION)) {
        Token *t = advance();
        Node *n = new_node(N_TERNARY, t->line);
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

static int at_modifier(void) {
    TokKind k = peek()->kind;
    return k == T_IF || k == T_UNLESS || k == T_WHILE || k == T_UNTIL;
}

static void parse_statements_until(NodeList *out, TokKind end) {
    for (;;) {
        while (check(T_NEWLINE) || check(T_SEMI)) advance();
        if (check(end)) return;
        if (check(T_EOF)) syntax_error(peek()->line, P.file, "ブロックの { } が閉じられていません");
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
            n->a = parse_expr();
            return n;
        }
        default:
            break;
    }

    Node *e = parse_expr();
    if (check(T_ASSIGN) || check(T_COMMA)) {
        Node *n = new_node(N_ASSIGN, t->line);
        nl_push(&n->list, e);
        while (accept(T_COMMA)) nl_push(&n->list, parse_expr());
        Token *eq = expect(T_ASSIGN, "代入の ");
        n->line = eq->line;
        for (int i = 0; i < n->list.count; i++)
            if (!is_assignable(n->list.items[i]))
                syntax_error(eq->line, P.file, "代入の左辺には変数・添字・フィールドのみ書けます");
        do {
            nl_push(&n->list2, parse_expr());
        } while (accept(T_COMMA));
        if (n->list2.count != 1 && n->list2.count != n->list.count)
            syntax_error(eq->line, P.file, "代入の左辺 (%d個) と右辺 (%d個) の数が合いません", n->list.count, n->list2.count);
        return n;
    }
    TokKind k = peek()->kind;
    if (k == T_PLUS_EQ || k == T_MINUS_EQ || k == T_STAR_EQ || k == T_SLASH_EQ || k == T_PERCENT_EQ) {
        Token *op = advance();
        if (!is_assignable(e)) syntax_error(op->line, P.file, "複合代入の左辺には変数・添字・フィールドのみ書けます");
        Node *n = new_node(N_COMPOUND, op->line);
        n->op = k == T_PLUS_EQ ? T_PLUS : k == T_MINUS_EQ ? T_MINUS : k == T_STAR_EQ ? T_STAR : k == T_SLASH_EQ ? T_SLASH : T_PERCENT;
        n->a = e;
        n->b = parse_expr();
        return n;
    }
    Node *n = new_node(N_EXPR_STMT, t->line);
    n->a = e;
    return n;
}

static Node *parse_match(int line) {
    Node *n = new_node(N_MATCH, line);
    n->a = parse_expr();
    expect(T_LBRACE, "match の対象の後に ");
    for (;;) {
        while (check(T_NEWLINE) || check(T_SEMI)) advance();
        if (accept(T_RBRACE)) break;
        if (check(T_EOF)) syntax_error(peek()->line, P.file, "match の { } が閉じられていません");
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
            Node *cond = parse_expr();
            n->a = t->kind == T_UNTIL ? negate(cond) : cond;
            n->b = parse_block();
            return n;
        }
        case T_LOOP: {
            advance();
            Node *n = new_node(N_LOOP, t->line);
            n->b = parse_block();
            return n;
        }
        case T_FOR: {
            advance();
            Node *n = new_node(N_FOR, t->line);
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
        case T_TRY: {
            advance();
            Node *n = new_node(N_TRY, t->line);
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
            if (!n->b && !n->c) syntax_error(t->line, P.file, "try の後には catch か finally が必要です");
            return n;
        }
        case T_IMPORT: {
            advance();
            Node *n = new_node(N_IMPORT, t->line);
            Token *s = expect(T_STR, "import の後にファイル名の ");
            n->constant = v_obj(VAL_STR, str_new_perm(s->str, s->len));
            return n;
        }
        case T_FN:
            if (peek_at(1)->kind == T_IDENT) {
                advance();
                Token *id = advance();
                Node *n = new_node(N_FNDECL, t->line);
                n->name = id->str;
                n->a = parse_function_rest(t->line, id->str);
                return n;
            }
            break;
        case T_ELSE:
        case T_ELIF:
            syntax_error(t->line, P.file, "対応する if のない %s です", tok_kind_name(t->kind));
            break;
        case T_CATCH:
        case T_FINALLY:
            syntax_error(t->line, P.file, "対応する try のない %s です", tok_kind_name(t->kind));
            break;
        default:
            break;
    }

    Node *s = parse_simple();
    /* 後置修飾子: 同じ行に続く if / unless / while / until */
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
    P.tl = lex(src, len, 1);
    P.pos = 0;
    Node *prog = new_node(N_BLOCK, 1);
    parse_statements_until(&prog->list, T_EOF);
    free_tokens(&P.tl);
    P = saved;
    cur_parse_file = saved_file;
    return prog;
}
