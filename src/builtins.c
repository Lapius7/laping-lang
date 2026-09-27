/* 組み込み関数
 *
 * どの関数も x.f(a) の形（メソッド呼び出し）でも f(x, a) の形でも呼べる。
 * ユーザー関数を呼び出す関数（map / filter など）は、途中で GC が
 * 走っても消えないように作成中の値を root_push しておく。
 */
#include "laping.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

#define B(name) static Value bi_##name(int argc, Value *argv)
#define UNUSED (void)argc; (void)argv

/* ===================================================================== */
/*  引数チェック                                                          */
/* ===================================================================== */

static const char *cur_fn = "";

LP_NORETURN static void arg_error(int i, const char *want, Value got) {
    rt_error("%s() の第%d引数には %s が必要です（%s が渡されました）", cur_fn, i + 1, want, type_name(got));
}

static double arg_num(Value *argv, int i) {
    if (!IS_NUM(argv[i])) arg_error(i, "数値", argv[i]);
    return argv[i].as.n;
}

static long arg_int(Value *argv, int i) {
    double d = arg_num(argv, i);
    if (d != floor(d)) rt_error("%s() の第%d引数には整数が必要です", cur_fn, i + 1);
    return (long)d;
}

static StrObj *arg_str(Value *argv, int i) {
    if (!IS_STR(argv[i])) arg_error(i, "文字列", argv[i]);
    return AS_STR(argv[i]);
}

static ListObj *arg_list(Value *argv, int i) {
    if (!IS_LIST(argv[i])) arg_error(i, "リスト", argv[i]);
    return AS_LIST(argv[i]);
}

static MapObj *arg_map(Value *argv, int i) {
    if (!IS_MAP(argv[i])) arg_error(i, "マップ", argv[i]);
    return AS_MAP(argv[i]);
}

static Value arg_fn(Value *argv, int i) {
    if (!IS_CALLABLE(argv[i])) arg_error(i, "関数", argv[i]);
    return argv[i];
}

/* リスト・範囲・文字列・マップ(キー) をリストに変換する。
 * 新しく作ったリストは呼び出し側で root すること */
static ListObj *to_list(Value v, int argi) {
    switch (v.type) {
        case VAL_LIST: return AS_LIST(v);
        case VAL_RANGE: {
            RangeObj *r = AS_RANGE(v);
            size_t n;
            if (!range_len(r, &n)) rt_error("範囲が大きすぎます");
            ListObj *l = list_new(n);
            for (size_t i = 0; i < n; i++) list_push(l, v_num(range_at(r, i)));
            return l;
        }
        case VAL_STR: {
            StrObj *s = AS_STR(v);
            ListObj *l = list_new(s->len);
            size_t off = 0;
            while (off < s->len) {
                size_t w = utf8_char_len((unsigned char)s->chars[off]);
                if (off + w > s->len) w = s->len - off;
                list_push(l, v_str(s->chars + off, w));
                off += w;
            }
            return l;
        }
        case VAL_MAP: {
            MapObj *m = AS_MAP(v);
            ListObj *l = list_new(m->live);
            for (size_t i = 0; i < m->count; i++)
                if (!m->entries[i].deleted) list_push(l, m->entries[i].key);
            return l;
        }
        default:
            arg_error(argi, "リスト・範囲・文字列・マップ", v);
    }
    return NULL;
}

/* 関数が2つ以上の引数を受け取れるなら (x, i) を、そうでなければ (x) を渡す */
static int wants_index(Value f) {
    if (f.type != VAL_FUNC) return 0;
    Node *d = AS_FUNC(f)->decl;
    return d->nparams >= 2 || d->variadic;
}

static Value call1(Value f, Value x) { return call_value(f, 1, &x); }

static Value call_xi(Value f, Value x, size_t i, int with_index) {
    Value args[2] = {x, v_num((double)i)};
    return call_value(f, with_index ? 2 : 1, args);
}

static int cmp_values(Value a, Value b) {
    if (IS_NUM(a) && IS_NUM(b)) return a.as.n < b.as.n ? -1 : a.as.n > b.as.n ? 1 : 0;
    if (IS_STR(a) && IS_STR(b)) {
        StrObj *x = AS_STR(a), *y = AS_STR(b);
        size_t n = x->len < y->len ? x->len : y->len;
        int c = memcmp(x->chars, y->chars, n);
        if (c) return c < 0 ? -1 : 1;
        return x->len < y->len ? -1 : x->len > y->len ? 1 : 0;
    }
    rt_error("%s と %s は大小比較できません", type_name(a), type_name(b));
    return 0;
}

/* ===================================================================== */
/*  入出力                                                                */
/* ===================================================================== */

static void write_values(int argc, Value *argv) {
    StrBuf sb;
    sb_init(&sb);
    for (int i = 0; i < argc; i++) {
        if (i) sb_append(&sb, " ", 1);
        value_to_sb(&sb, argv[i], 0);
    }
    if (sb.len) fwrite(sb.buf, 1, sb.len, stdout);
    sb_free(&sb);
}

B(print) {
    write_values(argc, argv);
    fputc('\n', stdout);
    return v_nil();
}

B(write) {
    write_values(argc, argv);
    fflush(stdout);
    return v_nil();
}

B(input) {
    if (argc > 0) {
        write_values(1, argv);
    }
    fflush(stdout);
    StrBuf sb;
    sb_init(&sb);
    int c;
    int got = 0;
    while ((c = fgetc(stdin)) != EOF) {
        got = 1;
        if (c == '\n') break;
        char ch = (char)c;
        sb_append(&sb, &ch, 1);
    }
    if (!got) {
        sb_free(&sb);
        return v_nil();
    }
    if (sb.len && sb.buf[sb.len - 1] == '\r') sb.len--;
    return sb_to_value(&sb);
}

B(read_file) {
    UNUSED;
    StrObj *p = arg_str(argv, 0);
    size_t len;
    char *s = read_whole_file(p->chars, &len);
    if (!s) rt_error("ファイルを開けません: %s", p->chars);
    Value v = v_str(s, len);
    free(s);
    return v;
}

static Value write_file_mode(Value *argv, const char *mode) {
    StrObj *p = arg_str(argv, 0);
    Value content = value_to_str(argv[1]);
    FILE *f = fopen(p->chars, mode);
    if (!f) rt_error("ファイルに書き込めません: %s", p->chars);
    fwrite(AS_STR(content)->chars, 1, AS_STR(content)->len, f);
    fclose(f);
    return v_nil();
}

B(write_file) { UNUSED; return write_file_mode(argv, "wb"); }
B(append_file) { UNUSED; return write_file_mode(argv, "ab"); }

B(file_exists) {
    UNUSED;
    FILE *f = fopen(arg_str(argv, 0)->chars, "rb");
    if (f) fclose(f);
    return v_bool(f != NULL);
}

/* ===================================================================== */
/*  型・変換                                                              */
/* ===================================================================== */

B(type) { UNUSED; return v_cstr(value_type_name(argv[0])); }
B(str) { UNUSED; return value_to_str(argv[0]); }

B(repr) {
    UNUSED;
    StrBuf sb;
    sb_init(&sb);
    value_to_sb(&sb, argv[0], 1);
    return sb_to_value(&sb);
}

B(bool) { UNUSED; return v_bool(is_truthy(argv[0])); }

static int parse_number(StrObj *s, double *out) {
    const char *p = s->chars;
    while (isspace((unsigned char)*p)) p++;
    if (!*p) return 0;
    char *end;
    double d;
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) d = (double)strtoll(p + 2, &end, 16);
    else if (p[0] == '0' && (p[1] == 'b' || p[1] == 'B')) d = (double)strtoll(p + 2, &end, 2);
    else d = strtod(p, &end);
    while (isspace((unsigned char)*end)) end++;
    if (*end) return 0;
    *out = d;
    return 1;
}

B(num) {
    UNUSED;
    Value v = argv[0];
    if (IS_NUM(v)) return v;
    if (IS_BOOL(v)) return v_num(v.as.b);
    if (IS_STR(v)) {
        double d;
        if (!parse_number(AS_STR(v), &d)) rt_error("\"%s\" は数値に変換できません", AS_STR(v)->chars);
        return v_num(d);
    }
    rt_error("%s は数値に変換できません", type_name(v));
    return v_nil();
}

B(int) {
    Value n = bi_num(argc, argv);
    return v_num(trunc(n.as.n));
}

B(is_num) {
    UNUSED;
    if (IS_NUM(argv[0])) return v_bool(1);
    double d;
    return v_bool(IS_STR(argv[0]) && parse_number(AS_STR(argv[0]), &d));
}

B(len) {
    UNUSED;
    Value v = argv[0];
    switch (v.type) {
        case VAL_STR: return v_num((double)utf8_len(AS_STR(v)->chars, AS_STR(v)->len));
        case VAL_LIST: return v_num((double)AS_LIST(v)->count);
        case VAL_MAP: return v_num((double)AS_MAP(v)->live);
        case VAL_RANGE: {
            size_t n = 0;
            range_len(AS_RANGE(v), &n);
            return v_num((double)n);
        }
        default: arg_error(0, "文字列・リスト・マップ・範囲", v);
    }
    return v_nil();
}

B(list) {
    if (argc == 0) return v_list(0);
    Value v = argv[0];
    ListObj *l = to_list(v, 0);
    if (l == AS_LIST(v) && IS_LIST(v)) { /* リストはコピーを返す */
        ListObj *c = list_new(l->count);
        for (size_t i = 0; i < l->count; i++) list_push(c, l->items[i]);
        return v_obj(VAL_LIST, c);
    }
    return v_obj(VAL_LIST, l);
}

B(range) {
    double start = 0, end, step = 1;
    if (argc == 1) end = arg_num(argv, 0);
    else {
        start = arg_num(argv, 0);
        end = arg_num(argv, 1);
        if (argc > 2) step = arg_num(argv, 2);
    }
    if (step == 0) rt_error("range() の step に 0 は指定できません");
    return v_obj(VAL_RANGE, range_new(start, end, step, 0));
}

static Value copy_value(Value v) {
    if (IS_LIST(v)) {
        ListObj *l = AS_LIST(v), *c = list_new(l->count);
        for (size_t i = 0; i < l->count; i++) list_push(c, l->items[i]);
        return v_obj(VAL_LIST, c);
    }
    if (IS_MAP(v)) {
        MapObj *m = AS_MAP(v), *c = map_new();
        c->tag = m->tag;
        for (size_t i = 0; i < m->count; i++)
            if (!m->entries[i].deleted) map_set(c, m->entries[i].key, m->entries[i].val);
        return v_obj(VAL_MAP, c);
    }
    return v;
}

B(copy) { UNUSED; return copy_value(argv[0]); }

/* ===================================================================== */
/*  リスト                                                                */
/* ===================================================================== */

B(push) {
    ListObj *l = arg_list(argv, 0);
    for (int i = 1; i < argc; i++) list_push(l, argv[i]);
    return argv[0];
}

static size_t norm_index(long idx, size_t count, int allow_end) {
    long n = (long)count;
    if (idx < 0) idx += n;
    if (idx < 0 || idx > n || (!allow_end && idx == n))
        rt_error("%s() の添字 %ld が範囲外です（長さ %zu）", cur_fn, idx < 0 ? idx - n : idx, count);
    return (size_t)idx;
}

B(pop) {
    ListObj *l = arg_list(argv, 0);
    if (l->count == 0) rt_error("空のリストから pop() できません");
    size_t i = argc > 1 ? norm_index(arg_int(argv, 1), l->count, 0) : l->count - 1;
    return list_remove(l, i);
}

B(shift) {
    UNUSED;
    ListObj *l = arg_list(argv, 0);
    if (l->count == 0) rt_error("空のリストから shift() できません");
    return list_remove(l, 0);
}

B(unshift) {
    ListObj *l = arg_list(argv, 0);
    for (int i = argc - 1; i >= 1; i--) list_insert(l, 0, argv[i]);
    return argv[0];
}

B(insert) {
    UNUSED;
    ListObj *l = arg_list(argv, 0);
    size_t i = norm_index(arg_int(argv, 1), l->count, 1);
    list_insert(l, i, argv[2]);
    return argv[0];
}

B(remove) {
    UNUSED;
    ListObj *l = arg_list(argv, 0);
    size_t i = norm_index(arg_int(argv, 1), l->count, 0);
    return list_remove(l, i);
}

B(clear) {
    UNUSED;
    if (IS_LIST(argv[0])) AS_LIST(argv[0])->count = 0;
    else {
        MapObj *m = arg_map(argv, 0);
        for (size_t i = 0; i < m->count; i++) m->entries[i].deleted = 1;
        for (size_t i = 0; i < m->index_cap; i++) m->index[i] = -1;
        m->count = m->live = 0;
    }
    return argv[0];
}

B(index_of) {
    UNUSED;
    if (IS_STR(argv[0])) {
        StrObj *h = AS_STR(argv[0]), *n = arg_str(argv, 1);
        if (n->len == 0) return v_num(0);
        for (size_t i = 0; i + n->len <= h->len; i++)
            if (memcmp(h->chars + i, n->chars, n->len) == 0) return v_num((double)utf8_len(h->chars, i));
        return v_num(-1);
    }
    ListObj *l = arg_list(argv, 0);
    for (size_t i = 0; i < l->count; i++)
        if (values_equal(l->items[i], argv[1])) return v_num((double)i);
    return v_num(-1);
}

B(contains) {
    UNUSED;
    Value c = argv[0], x = argv[1];
    if (IS_STR(c)) {
        StrObj *h = AS_STR(c), *n = arg_str(argv, 1);
        if (n->len == 0) return v_bool(1);
        for (size_t i = 0; i + n->len <= h->len; i++)
            if (memcmp(h->chars + i, n->chars, n->len) == 0) return v_bool(1);
        return v_bool(0);
    }
    if (IS_MAP(c)) return v_bool(is_hashable(x) && map_get(AS_MAP(c), x, NULL));
    ListObj *l = arg_list(argv, 0);
    for (size_t i = 0; i < l->count; i++)
        if (values_equal(l->items[i], x)) return v_bool(1);
    return v_bool(0);
}

static void slice_bounds(long *start, long *end, long n) {
    if (*start < 0) *start += n;
    if (*end < 0) *end += n;
    if (*start < 0) *start = 0;
    if (*end > n) *end = n;
    if (*start > *end) *start = *end;
}

B(slice) {
    Value v = argv[0];
    long start = arg_int(argv, 1);
    if (IS_STR(v)) {
        StrObj *s = AS_STR(v);
        long n = (long)utf8_len(s->chars, s->len);
        long end = argc > 2 ? arg_int(argv, 2) : n;
        slice_bounds(&start, &end, n);
        size_t a = utf8_offset(s->chars, s->len, (size_t)start);
        size_t b = utf8_offset(s->chars, s->len, (size_t)end);
        return v_str(s->chars + a, b - a);
    }
    ListObj *l = arg_list(argv, 0);
    long end = argc > 2 ? arg_int(argv, 2) : (long)l->count;
    slice_bounds(&start, &end, (long)l->count);
    ListObj *r = list_new((size_t)(end - start));
    for (long i = start; i < end; i++) list_push(r, l->items[i]);
    return v_obj(VAL_LIST, r);
}

B(reverse) {
    UNUSED;
    if (IS_STR(argv[0])) {
        StrObj *s = AS_STR(argv[0]);
        char *buf = xmalloc(s->len + 1);
        size_t off = 0;
        while (off < s->len) {
            size_t w = utf8_char_len((unsigned char)s->chars[off]);
            if (off + w > s->len) w = s->len - off;
            memcpy(buf + s->len - off - w, s->chars + off, w);
            off += w;
        }
        Value v = v_str(buf, s->len);
        free(buf);
        return v;
    }
    ListObj *l = arg_list(argv, 0);
    ListObj *r = list_new(l->count);
    for (size_t i = l->count; i > 0; i--) list_push(r, l->items[i - 1]);
    return v_obj(VAL_LIST, r);
}

/* 安定なマージソート。keys が NULL なら値そのもので比較 */
static void merge_sort(size_t *idx, size_t *tmp, size_t n, Value *vals) {
    if (n < 2) return;
    size_t mid = n / 2;
    merge_sort(idx, tmp, mid, vals);
    merge_sort(idx + mid, tmp, n - mid, vals);
    size_t i = 0, j = mid, k = 0;
    while (i < mid && j < n) {
        if (cmp_values(vals[idx[j]], vals[idx[i]]) < 0) tmp[k++] = idx[j++];
        else tmp[k++] = idx[i++];
    }
    while (i < mid) tmp[k++] = idx[i++];
    while (j < n) tmp[k++] = idx[j++];
    memcpy(idx, tmp, sizeof(size_t) * n);
}

static Value sorted_by_keys(ListObj *src, ListObj *keys) {
    size_t n = src->count;
    size_t *idx = xmalloc(sizeof(size_t) * (n ? n : 1));
    size_t *tmp = xmalloc(sizeof(size_t) * (n ? n : 1));
    for (size_t i = 0; i < n; i++) idx[i] = i;
    merge_sort(idx, tmp, n, keys->items);
    ListObj *r = list_new(n);
    for (size_t i = 0; i < n; i++) list_push(r, src->items[idx[i]]);
    free(idx);
    free(tmp);
    return v_obj(VAL_LIST, r);
}

B(sort) {
    ListObj *src = to_list(argv[0], 0);
    root_push(v_obj(VAL_LIST, src));
    Value r;
    if (argc > 1) {
        Value f = arg_fn(argv, 1);
        ListObj *keys = list_new(src->count);
        root_push(v_obj(VAL_LIST, keys));
        for (size_t i = 0; i < src->count; i++) list_push(keys, call1(f, src->items[i]));
        r = sorted_by_keys(src, keys);
        root_pop(1);
    } else {
        r = sorted_by_keys(src, src);
    }
    root_pop(1);
    return r;
}

B(map) {
    UNUSED;
    ListObj *src = to_list(argv[0], 0);
    root_push(v_obj(VAL_LIST, src));
    Value f = arg_fn(argv, 1);
    int wi = wants_index(f);
    ListObj *r = list_new(src->count);
    root_push(v_obj(VAL_LIST, r));
    for (size_t i = 0; i < src->count; i++) list_push(r, call_xi(f, src->items[i], i, wi));
    root_pop(2);
    return v_obj(VAL_LIST, r);
}

B(filter) {
    UNUSED;
    ListObj *src = to_list(argv[0], 0);
    root_push(v_obj(VAL_LIST, src));
    Value f = arg_fn(argv, 1);
    int wi = wants_index(f);
    ListObj *r = list_new(0);
    root_push(v_obj(VAL_LIST, r));
    for (size_t i = 0; i < src->count; i++) {
        Value x = src->items[i];
        if (is_truthy(call_xi(f, x, i, wi))) list_push(r, x);
    }
    root_pop(2);
    return v_obj(VAL_LIST, r);
}

B(each) {
    UNUSED;
    ListObj *src = to_list(argv[0], 0);
    root_push(v_obj(VAL_LIST, src));
    Value f = arg_fn(argv, 1);
    int wi = wants_index(f);
    for (size_t i = 0; i < src->count; i++) call_xi(f, src->items[i], i, wi);
    root_pop(1);
    return v_nil();
}

B(reduce) {
    ListObj *src = to_list(argv[0], 0);
    root_push(v_obj(VAL_LIST, src));
    Value f = arg_fn(argv, 1);
    size_t i = 0;
    Value acc;
    if (argc > 2) acc = argv[2];
    else {
        if (src->count == 0) rt_error("空のリストを初期値なしで reduce() できません");
        acc = src->items[i++];
    }
    root_push(acc);
    size_t slot = root_depth() - 1;
    for (; i < src->count; i++) {
        Value args[2] = {root_get(slot), src->items[i]};
        acc = call_value(f, 2, args);
        root_pop(1);
        root_push(acc);
    }
    root_pop(2);
    return acc;
}

B(find) {
    UNUSED;
    ListObj *src = to_list(argv[0], 0);
    root_push(v_obj(VAL_LIST, src));
    Value f = arg_fn(argv, 1);
    for (size_t i = 0; i < src->count; i++) {
        if (is_truthy(call1(f, src->items[i]))) {
            Value x = src->items[i];
            root_pop(1);
            return x;
        }
    }
    root_pop(1);
    return v_nil();
}

static Value any_all(int argc, Value *argv, int want_all) {
    ListObj *src = to_list(argv[0], 0);
    root_push(v_obj(VAL_LIST, src));
    Value f = argc > 1 ? arg_fn(argv, 1) : v_nil();
    for (size_t i = 0; i < src->count; i++) {
        int t = argc > 1 ? is_truthy(call1(f, src->items[i])) : is_truthy(src->items[i]);
        if (t != want_all) {
            root_pop(1);
            return v_bool(!want_all);
        }
    }
    root_pop(1);
    return v_bool(want_all);
}

B(any) { return any_all(argc, argv, 0); }
B(all) { return any_all(argc, argv, 1); }

B(count) {
    UNUSED;
    ListObj *src = to_list(argv[0], 0);
    root_push(v_obj(VAL_LIST, src));
    size_t n = 0;
    for (size_t i = 0; i < src->count; i++) {
        if (IS_CALLABLE(argv[1])) n += is_truthy(call1(argv[1], src->items[i]));
        else n += values_equal(src->items[i], argv[1]);
    }
    root_pop(1);
    return v_num((double)n);
}

B(sum) {
    UNUSED;
    ListObj *src = to_list(argv[0], 0);
    double s = 0;
    for (size_t i = 0; i < src->count; i++) {
        if (!IS_NUM(src->items[i])) rt_error("sum() は数値のリストにのみ使えます（%s が含まれています）", type_name(src->items[i]));
        s += src->items[i].as.n;
    }
    return v_num(s);
}

static Value min_max(int argc, Value *argv, int sign) {
    Value *items = argv;
    size_t n = (size_t)argc;
    if (argc == 1) {
        ListObj *l = to_list(argv[0], 0);
        items = l->items;
        n = l->count;
    }
    if (n == 0) rt_error("%s() に空のリストは渡せません", cur_fn);
    Value best = items[0];
    for (size_t i = 1; i < n; i++)
        if (cmp_values(items[i], best) * sign > 0) best = items[i];
    return best;
}

B(min) { return min_max(argc, argv, -1); }
B(max) { return min_max(argc, argv, 1); }

B(join) {
    ListObj *l = to_list(argv[0], 0);
    StrObj *sep = argc > 1 ? arg_str(argv, 1) : NULL;
    StrBuf sb;
    sb_init(&sb);
    for (size_t i = 0; i < l->count; i++) {
        if (i && sep) sb_append(&sb, sep->chars, sep->len);
        value_to_sb(&sb, l->items[i], 0);
    }
    return sb_to_value(&sb);
}

B(zip) {
    ListObj *a = to_list(argv[0], 0);
    root_push(v_obj(VAL_LIST, a));
    ListObj *b = to_list(argv[1], 1);
    root_push(v_obj(VAL_LIST, b));
    (void)argc;
    size_t n = a->count < b->count ? a->count : b->count;
    ListObj *r = list_new(n);
    root_push(v_obj(VAL_LIST, r));
    for (size_t i = 0; i < n; i++) {
        ListObj *pair = list_new(2);
        list_push(pair, a->items[i]);
        list_push(pair, b->items[i]);
        list_push(r, v_obj(VAL_LIST, pair));
    }
    root_pop(3);
    return v_obj(VAL_LIST, r);
}

B(enumerate) {
    UNUSED;
    ListObj *a = to_list(argv[0], 0);
    ListObj *r = list_new(a->count);
    for (size_t i = 0; i < a->count; i++) {
        ListObj *pair = list_new(2);
        list_push(pair, v_num((double)i));
        list_push(pair, a->items[i]);
        list_push(r, v_obj(VAL_LIST, pair));
    }
    return v_obj(VAL_LIST, r);
}

B(flatten) {
    UNUSED;
    ListObj *a = arg_list(argv, 0);
    ListObj *r = list_new(a->count);
    for (size_t i = 0; i < a->count; i++) {
        if (IS_LIST(a->items[i])) {
            ListObj *in = AS_LIST(a->items[i]);
            for (size_t j = 0; j < in->count; j++) list_push(r, in->items[j]);
        } else {
            list_push(r, a->items[i]);
        }
    }
    return v_obj(VAL_LIST, r);
}

B(unique) {
    UNUSED;
    ListObj *a = to_list(argv[0], 0);
    ListObj *r = list_new(0);
    for (size_t i = 0; i < a->count; i++) {
        int dup = 0;
        for (size_t j = 0; j < r->count && !dup; j++) dup = values_equal(a->items[i], r->items[j]);
        if (!dup) list_push(r, a->items[i]);
    }
    return v_obj(VAL_LIST, r);
}

/* ===================================================================== */
/*  文字列                                                                */
/* ===================================================================== */

static Value map_ascii(StrObj *s, int (*f)(int)) {
    Value v = v_str(s->chars, s->len);
    StrObj *o = AS_STR(v);
    for (size_t i = 0; i < o->len; i++)
        if ((unsigned char)o->chars[i] < 0x80) o->chars[i] = (char)f((unsigned char)o->chars[i]);
    o->hash = hash_bytes(o->chars, o->len);
    return v;
}

B(upper) { UNUSED; return map_ascii(arg_str(argv, 0), toupper); }
B(lower) { UNUSED; return map_ascii(arg_str(argv, 0), tolower); }

static int is_space_at(const char *s, size_t len, size_t i, size_t *w) {
    unsigned char c = (unsigned char)s[i];
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f') { *w = 1; return 1; }
    if (c == 0xE3 && i + 2 < len + 0 && (unsigned char)s[i + 1] == 0x80 && (unsigned char)s[i + 2] == 0x80) { *w = 3; return 1; }
    return 0;
}

B(trim) {
    UNUSED;
    StrObj *s = arg_str(argv, 0);
    size_t a = 0, b = s->len, w;
    while (a < b && is_space_at(s->chars, s->len, a, &w)) a += w;
    for (;;) {
        if (b > a && is_space_at(s->chars, s->len, b - 1, &w) && w == 1) { b--; continue; }
        if (b >= a + 3 && is_space_at(s->chars, s->len, b - 3, &w) && w == 3) { b -= 3; continue; }
        break;
    }
    return v_str(s->chars + a, b - a);
}

B(split) {
    StrObj *s = arg_str(argv, 0);
    ListObj *r = list_new(0);
    root_push(v_obj(VAL_LIST, r));
    if (argc < 2) {
        /* 空白で区切る（連続する空白はまとめて1つ） */
        size_t i = 0, w;
        while (i < s->len) {
            while (i < s->len && is_space_at(s->chars, s->len, i, &w)) i += w;
            if (i >= s->len) break;
            size_t st = i;
            while (i < s->len && !is_space_at(s->chars, s->len, i, &w)) i++;
            list_push(r, v_str(s->chars + st, i - st));
        }
    } else {
        StrObj *sep = arg_str(argv, 1);
        if (sep->len == 0) {
            root_pop(1);
            return v_obj(VAL_LIST, to_list(argv[0], 0));
        }
        size_t st = 0;
        for (size_t i = 0; i + sep->len <= s->len;) {
            if (memcmp(s->chars + i, sep->chars, sep->len) == 0) {
                list_push(r, v_str(s->chars + st, i - st));
                i += sep->len;
                st = i;
            } else {
                i++;
            }
        }
        list_push(r, v_str(s->chars + st, s->len - st));
    }
    root_pop(1);
    return v_obj(VAL_LIST, r);
}

B(replace) {
    UNUSED;
    StrObj *s = arg_str(argv, 0), *from = arg_str(argv, 1), *to = arg_str(argv, 2);
    if (from->len == 0) return argv[0];
    StrBuf sb;
    sb_init(&sb);
    size_t i = 0;
    while (i < s->len) {
        if (i + from->len <= s->len && memcmp(s->chars + i, from->chars, from->len) == 0) {
            sb_append(&sb, to->chars, to->len);
            i += from->len;
        } else {
            sb_append(&sb, s->chars + i, 1);
            i++;
        }
    }
    return sb_to_value(&sb);
}

B(starts_with) {
    UNUSED;
    StrObj *s = arg_str(argv, 0), *p = arg_str(argv, 1);
    return v_bool(p->len <= s->len && memcmp(s->chars, p->chars, p->len) == 0);
}

B(ends_with) {
    UNUSED;
    StrObj *s = arg_str(argv, 0), *p = arg_str(argv, 1);
    return v_bool(p->len <= s->len && memcmp(s->chars + s->len - p->len, p->chars, p->len) == 0);
}

B(chars) { UNUSED; arg_str(argv, 0); return v_obj(VAL_LIST, to_list(argv[0], 0)); }

B(ord) {
    UNUSED;
    StrObj *s = arg_str(argv, 0);
    if (s->len == 0) rt_error("ord() に空文字列は渡せません");
    const unsigned char *p = (const unsigned char *)s->chars;
    size_t w = utf8_char_len(p[0]);
    unsigned long cp;
    if (w == 1) cp = p[0];
    else if (w == 2) cp = ((p[0] & 0x1Fu) << 6) | (p[1] & 0x3Fu);
    else if (w == 3) cp = ((p[0] & 0x0Fu) << 12) | ((p[1] & 0x3Fu) << 6) | (p[2] & 0x3Fu);
    else cp = ((p[0] & 0x07u) << 18) | ((p[1] & 0x3Fu) << 12) | ((p[2] & 0x3Fu) << 6) | (p[3] & 0x3Fu);
    return v_num((double)cp);
}

B(chr) {
    UNUSED;
    long cp = arg_int(argv, 0);
    if (cp < 0 || cp > 0x10FFFF) rt_error("chr() の引数が範囲外です: %ld", cp);
    char b[4];
    size_t n;
    if (cp < 0x80) { b[0] = (char)cp; n = 1; }
    else if (cp < 0x800) { b[0] = (char)(0xC0 | (cp >> 6)); b[1] = (char)(0x80 | (cp & 0x3F)); n = 2; }
    else if (cp < 0x10000) {
        b[0] = (char)(0xE0 | (cp >> 12)); b[1] = (char)(0x80 | ((cp >> 6) & 0x3F)); b[2] = (char)(0x80 | (cp & 0x3F)); n = 3;
    } else {
        b[0] = (char)(0xF0 | (cp >> 18)); b[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        b[2] = (char)(0x80 | ((cp >> 6) & 0x3F)); b[3] = (char)(0x80 | (cp & 0x3F)); n = 4;
    }
    return v_str(b, n);
}

static Value pad(int argc, Value *argv, int left) {
    Value sv = value_to_str(argv[0]);
    StrObj *s = AS_STR(sv);
    long width = arg_int(argv, 1);
    StrObj *fill = argc > 2 ? arg_str(argv, 2) : NULL;
    if (fill && utf8_len(fill->chars, fill->len) != 1) rt_error("%s() の埋め文字は1文字にしてください", cur_fn);
    long cur = (long)utf8_len(s->chars, s->len);
    StrBuf sb;
    sb_init(&sb);
    if (!left) sb_append(&sb, s->chars, s->len);
    for (long i = cur; i < width; i++) {
        if (fill) sb_append(&sb, fill->chars, fill->len);
        else sb_append(&sb, " ", 1);
    }
    if (left) sb_append(&sb, s->chars, s->len);
    return sb_to_value(&sb);
}

B(pad_left) { return pad(argc, argv, 1); }
B(pad_right) { return pad(argc, argv, 0); }

B(fixed) {
    UNUSED;
    double d = arg_num(argv, 0);
    long digits = arg_int(argv, 1);
    if (digits < 0 || digits > 20) rt_error("fixed() の桁数は 0〜20 で指定してください");
    char buf[512];
    snprintf(buf, sizeof(buf), "%.*f", (int)digits, d);
    return v_cstr(buf);
}

/* ===================================================================== */
/*  マップ                                                                */
/* ===================================================================== */

B(keys) { UNUSED; arg_map(argv, 0); return v_obj(VAL_LIST, to_list(argv[0], 0)); }

B(values) {
    UNUSED;
    MapObj *m = arg_map(argv, 0);
    ListObj *l = list_new(m->live);
    for (size_t i = 0; i < m->count; i++)
        if (!m->entries[i].deleted) list_push(l, m->entries[i].val);
    return v_obj(VAL_LIST, l);
}

B(items) {
    UNUSED;
    MapObj *m = arg_map(argv, 0);
    ListObj *l = list_new(m->live);
    root_push(v_obj(VAL_LIST, l));
    for (size_t i = 0; i < m->count; i++) {
        if (m->entries[i].deleted) continue;
        ListObj *pair = list_new(2);
        list_push(pair, m->entries[i].key);
        list_push(pair, m->entries[i].val);
        list_push(l, v_obj(VAL_LIST, pair));
    }
    root_pop(1);
    return v_obj(VAL_LIST, l);
}

B(has) {
    UNUSED;
    MapObj *m = arg_map(argv, 0);
    return v_bool(is_hashable(argv[1]) && map_get(m, argv[1], NULL));
}

B(get) {
    Value out;
    if (IS_LIST(argv[0])) {
        ListObj *l = AS_LIST(argv[0]);
        long i = arg_int(argv, 1);
        if (i < 0) i += (long)l->count;
        if (i >= 0 && (size_t)i < l->count) return l->items[i];
        return argc > 2 ? argv[2] : v_nil();
    }
    MapObj *m = arg_map(argv, 0);
    if (is_hashable(argv[1]) && map_get(m, argv[1], &out)) return out;
    return argc > 2 ? argv[2] : v_nil();
}

B(delete) {
    UNUSED;
    MapObj *m = arg_map(argv, 0);
    return v_bool(is_hashable(argv[1]) && map_delete(m, argv[1]));
}

B(merge) {
    MapObj *r = map_new();
    for (int a = 0; a < argc; a++) {
        MapObj *m = arg_map(argv, a);
        for (size_t i = 0; i < m->count; i++)
            if (!m->entries[i].deleted) map_set(r, m->entries[i].key, m->entries[i].val);
    }
    return v_obj(VAL_MAP, r);
}

/* ===================================================================== */
/*  数学                                                                  */
/* ===================================================================== */

#define MATH1(name, expr) B(name) { UNUSED; double x = arg_num(argv, 0); return v_num(expr); }
MATH1(abs, fabs(x))
MATH1(floor, floor(x))
MATH1(ceil, ceil(x))
MATH1(sin, sin(x))
MATH1(cos, cos(x))
MATH1(tan, tan(x))
MATH1(asin, asin(x))
MATH1(acos, acos(x))
MATH1(atan, atan(x))
MATH1(exp, exp(x))

B(sqrt) {
    UNUSED;
    double x = arg_num(argv, 0);
    if (x < 0) rt_error("負の数の平方根は計算できません");
    return v_num(sqrt(x));
}

B(log) {
    double x = arg_num(argv, 0);
    if (x <= 0) rt_error("log() には正の数が必要です");
    if (argc > 1) return v_num(log(x) / log(arg_num(argv, 1)));
    return v_num(log(x));
}

B(pow) { UNUSED; return v_num(pow(arg_num(argv, 0), arg_num(argv, 1))); }
B(atan2) { UNUSED; return v_num(atan2(arg_num(argv, 0), arg_num(argv, 1))); }

B(round) {
    double x = arg_num(argv, 0);
    if (argc > 1) {
        double p = pow(10, (double)arg_int(argv, 1));
        return v_num(round(x * p) / p);
    }
    return v_num(round(x));
}

static uint64_t rng_state = 0x853c49e6748fea9bULL;

static uint64_t rng_next(void) {
    uint64_t x = rng_state;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    rng_state = x;
    return x * 0x2545F4914F6CDD1DULL;
}

B(random) { UNUSED; return v_num((double)(rng_next() >> 11) / 9007199254740992.0); }

B(rand_int) {
    UNUSED;
    long a = arg_int(argv, 0), b = arg_int(argv, 1);
    if (b < a) rt_error("rand_int(a, b) は a <= b で指定してください");
    return v_num((double)(a + (long)(rng_next() % (uint64_t)(b - a + 1))));
}

B(seed) {
    UNUSED;
    rng_state = (uint64_t)arg_int(argv, 0) * 0x9E3779B97F4A7C15ULL + 1;
    return v_nil();
}

B(choice) {
    UNUSED;
    ListObj *l = to_list(argv[0], 0);
    if (l->count == 0) rt_error("空のリストから choice() できません");
    return l->items[rng_next() % l->count];
}

B(shuffle) {
    UNUSED;
    Value v = copy_value(v_obj(VAL_LIST, to_list(argv[0], 0)));
    ListObj *l = AS_LIST(v);
    for (size_t i = l->count; i > 1; i--) {
        size_t j = rng_next() % i;
        Value t = l->items[i - 1];
        l->items[i - 1] = l->items[j];
        l->items[j] = t;
    }
    return v;
}

/* ===================================================================== */
/*  システム                                                              */
/* ===================================================================== */

B(time) {
    UNUSED;
#if defined(_WIN32)
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    uint64_t t = ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    return v_num((double)(t - 116444736000000000ULL) / 1e7);
#else
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return v_num((double)ts.tv_sec + ts.tv_nsec / 1e9);
#endif
}

B(clock) { UNUSED; return v_num((double)clock() / CLOCKS_PER_SEC); }

B(sleep) {
    UNUSED;
    double s = arg_num(argv, 0);
    if (s <= 0) return v_nil();
    fflush(stdout);
#if defined(_WIN32)
    Sleep((DWORD)(s * 1000));
#else
    struct timespec ts;
    ts.tv_sec = (time_t)s;
    ts.tv_nsec = (long)((s - (double)ts.tv_sec) * 1e9);
    nanosleep(&ts, NULL);
#endif
    return v_nil();
}

B(exit) {
    fflush(stdout);
    exit(argc > 0 ? (int)arg_int(argv, 0) : 0);
    return v_nil();
}

B(assert) {
    if (is_truthy(argv[0])) return v_nil();
    if (argc > 1) throw_value(value_to_str(argv[1]));
    rt_error("assert に失敗しました");
    return v_nil();
}

/* ===================================================================== */
/*  画面表示（CLI 向け）                                                   */
/* ===================================================================== */

static const struct { const char *name; const char *code; } color_names[] = {
    {"black", "30"}, {"red", "31"}, {"green", "32"}, {"yellow", "33"}, {"blue", "34"},
    {"magenta", "35"}, {"cyan", "36"}, {"white", "37"}, {"gray", "90"}, {"grey", "90"},
    {"黒", "30"}, {"赤", "31"}, {"緑", "32"}, {"黄", "33"}, {"青", "34"}, {"紫", "35"},
    {"水色", "36"}, {"白", "37"}, {"灰色", "90"},
    {NULL, NULL}
};

static Value styled(Value v, const char *code) {
    Value sv = value_to_str(v);
    if (!ui_color_out) return sv;
    StrBuf sb;
    sb_init(&sb);
    sb_printf(&sb, "\x1b[%sm", code);
    sb_append(&sb, AS_STR(sv)->chars, AS_STR(sv)->len);
    sb_appendc(&sb, "\x1b[0m");
    return sb_to_value(&sb);
}

B(color) {
    UNUSED;
    StrObj *name = arg_str(argv, 1);
    for (int i = 0; color_names[i].name; i++)
        if (strcmp(color_names[i].name, name->chars) == 0) return styled(argv[0], color_names[i].code);
    rt_error("color() の色名 '%s' は使えません（red green yellow blue magenta cyan white gray black）", name->chars);
    return v_nil();
}

B(bold) { UNUSED; return styled(argv[0], "1"); }
B(dim) { UNUSED; return styled(argv[0], "2"); }
B(underline) { UNUSED; return styled(argv[0], "4"); }

B(width) {
    UNUSED;
    Value sv = value_to_str(argv[0]);
    return v_num((double)display_width(AS_STR(sv)->chars, AS_STR(sv)->len));
}

static void sb_repeat(StrBuf *sb, const char *s, long n) {
    for (long i = 0; i < n; i++) sb_appendc(sb, s);
}

/* 表示幅 w の文字列を width に揃える。align: 'l' 'r' 'c' */
static void sb_aligned(StrBuf *sb, const char *s, size_t len, size_t width, char align) {
    size_t w = display_width(s, len);
    size_t pad = width > w ? width - w : 0;
    size_t left = align == 'r' ? pad : align == 'c' ? pad / 2 : 0;
    sb_repeat(sb, " ", (long)left);
    sb_append(sb, s, len);
    sb_repeat(sb, " ", (long)(pad - left));
}

/* 二重ループで使う: 各セルを文字列にして保持する */
typedef struct {
    Value *cells; /* rows * cols */
    int *is_num;
    size_t rows, cols;
} Grid;

B(table) {
    ListObj *rows = arg_list(argv, 0);
    ListObj *headers = NULL;
    ListObj *auto_headers = NULL;
    size_t base = root_depth();
    if (argc > 1) headers = arg_list(argv, 1);
    if (!headers && rows->count > 0 && IS_MAP(rows->items[0])) {
        auto_headers = to_list(rows->items[0], 0);
        root_push(v_obj(VAL_LIST, auto_headers));
        headers = auto_headers;
    }
    size_t cols = headers ? headers->count : 0;
    for (size_t r = 0; r < rows->count; r++) {
        Value row = rows->items[r];
        if (IS_LIST(row) && AS_LIST(row)->count > cols) cols = AS_LIST(row)->count;
        else if (!IS_LIST(row) && !IS_MAP(row)) rt_error("table() の各行はリストかマップにしてください（%s が含まれています）", type_name(row));
    }
    if (cols == 0) {
        root_restore(base);
        return v_nil();
    }
    size_t nrows = rows->count + (headers ? 1 : 0);
    ListObj *cells = list_new(nrows * cols);
    root_push(v_obj(VAL_LIST, cells));
    int *is_num = xcalloc(nrows * cols, sizeof(int));
    size_t *widths = xcalloc(cols, sizeof(size_t));
    for (size_t r = 0; r < nrows; r++) {
        for (size_t c = 0; c < cols; c++) {
            Value v = v_str("", 0);
            if (headers && r == 0) {
                if (c < headers->count) v = value_to_str(headers->items[c]);
            } else {
                Value row = rows->items[r - (headers ? 1 : 0)];
                Value cell = v_nil();
                int have = 0;
                if (IS_LIST(row) && c < AS_LIST(row)->count) {
                    cell = AS_LIST(row)->items[c];
                    have = 1;
                } else if (IS_MAP(row) && headers && c < headers->count && is_hashable(headers->items[c])) {
                    have = map_get(AS_MAP(row), headers->items[c], &cell);
                }
                if (have) {
                    is_num[r * cols + c] = IS_NUM(cell);
                    v = value_to_str(cell);
                }
            }
            list_push(cells, v);
            size_t w = display_width(AS_STR(v)->chars, AS_STR(v)->len);
            if (w > widths[c]) widths[c] = w;
        }
    }
    StrBuf sb;
    sb_init(&sb);
    const char *bd = ui_c(0, UI_BOLD), *gy = ui_c(0, UI_GRAY), *rs = ui_c(0, UI_RESET);
    const char *edges[3][3] = {{"┌", "┬", "┐"}, {"├", "┼", "┤"}, {"└", "┴", "┘"}};
#define RULE(kind)                                                        \
    do {                                                                  \
        sb_appendc(&sb, gy);                                              \
        sb_appendc(&sb, edges[kind][0]);                                  \
        for (size_t c = 0; c < cols; c++) {                               \
            sb_repeat(&sb, "─", (long)widths[c] + 2);                     \
            sb_appendc(&sb, c + 1 < cols ? edges[kind][1] : edges[kind][2]); \
        }                                                                 \
        sb_appendc(&sb, rs);                                              \
        sb_append(&sb, "\n", 1);                                          \
    } while (0)
    RULE(0);
    for (size_t r = 0; r < nrows; r++) {
        sb_appendc(&sb, gy);
        sb_appendc(&sb, "│");
        sb_appendc(&sb, rs);
        for (size_t c = 0; c < cols; c++) {
            StrObj *cs = AS_STR(cells->items[r * cols + c]);
            int head = headers && r == 0;
            sb_append(&sb, " ", 1);
            if (head) sb_appendc(&sb, bd);
            sb_aligned(&sb, cs->chars, cs->len, widths[c], head ? 'c' : is_num[r * cols + c] ? 'r' : 'l');
            if (head) sb_appendc(&sb, rs);
            sb_append(&sb, " ", 1);
            sb_appendc(&sb, gy);
            sb_appendc(&sb, "│");
            sb_appendc(&sb, rs);
        }
        sb_append(&sb, "\n", 1);
        if (headers && r == 0) RULE(1);
    }
    RULE(2);
#undef RULE
    fwrite(sb.buf, 1, sb.len, stdout);
    sb_free(&sb);
    free(is_num);
    free(widths);
    root_restore(base);
    return v_nil();
}

B(box) {
    Value sv = value_to_str(argv[0]);
    root_push(sv);
    ListObj *lines = list_new(0);
    root_push(v_obj(VAL_LIST, lines));
    StrObj *s = AS_STR(sv);
    size_t st = 0;
    for (size_t i = 0; i <= s->len; i++) {
        if (i == s->len || s->chars[i] == '\n') {
            list_push(lines, v_str(s->chars + st, i - st));
            st = i + 1;
        }
    }
    StrObj *title = argc > 1 ? AS_STR(value_to_str(argv[1])) : NULL;
    size_t w = title ? display_width(title->chars, title->len) + 2 : 0;
    for (size_t i = 0; i < lines->count; i++) {
        StrObj *l = AS_STR(lines->items[i]);
        size_t lw = display_width(l->chars, l->len);
        if (lw > w) w = lw;
    }
    const char *cy = ui_c(0, UI_CYAN), *bd = ui_c(0, UI_BOLD), *rs = ui_c(0, UI_RESET);
    StrBuf sb;
    sb_init(&sb);
    sb_appendc(&sb, cy);
    sb_appendc(&sb, "╭─");
    if (title) {
        sb_appendc(&sb, rs);
        sb_appendc(&sb, bd);
        sb_append(&sb, " ", 1);
        sb_append(&sb, title->chars, title->len);
        sb_append(&sb, " ", 1);
        sb_appendc(&sb, rs);
        sb_appendc(&sb, cy);
        sb_repeat(&sb, "─", (long)(w - display_width(title->chars, title->len) - 2 + 1));
    } else {
        sb_repeat(&sb, "─", (long)w + 1);
    }
    sb_appendc(&sb, "╮");
    sb_appendc(&sb, rs);
    sb_append(&sb, "\n", 1);
    for (size_t i = 0; i < lines->count; i++) {
        StrObj *l = AS_STR(lines->items[i]);
        sb_appendc(&sb, cy);
        sb_appendc(&sb, "│");
        sb_appendc(&sb, rs);
        sb_append(&sb, " ", 1);
        sb_aligned(&sb, l->chars, l->len, w, 'l');
        sb_append(&sb, " ", 1);
        sb_appendc(&sb, cy);
        sb_appendc(&sb, "│");
        sb_appendc(&sb, rs);
        sb_append(&sb, "\n", 1);
    }
    sb_appendc(&sb, cy);
    sb_appendc(&sb, "╰");
    sb_repeat(&sb, "─", (long)w + 2);
    sb_appendc(&sb, "╯");
    sb_appendc(&sb, rs);
    sb_append(&sb, "\n", 1);
    fwrite(sb.buf, 1, sb.len, stdout);
    sb_free(&sb);
    root_pop(2);
    return v_nil();
}

B(progress) {
    double cur = arg_num(argv, 0), total = arg_num(argv, 1);
    long width = argc > 2 ? arg_int(argv, 2) : 30;
    if (width < 1 || width > 500) rt_error("progress() の幅は 1〜500 で指定してください");
    double ratio = total > 0 ? cur / total : 0;
    if (ratio < 0) ratio = 0;
    if (ratio > 1) ratio = 1;
    long filled = (long)(ratio * (double)width + 0.5);
    StrBuf sb;
    sb_init(&sb);
    sb_append(&sb, "[", 1);
    sb_appendc(&sb, ui_c(0, UI_GREEN));
    sb_repeat(&sb, "█", filled);
    sb_appendc(&sb, ui_c(0, UI_GRAY));
    sb_repeat(&sb, "░", width - filled);
    sb_appendc(&sb, ui_c(0, UI_RESET));
    sb_printf(&sb, "] %3.0f%%", ratio * 100);
    return sb_to_value(&sb);
}

static void pp_value(StrBuf *sb, Value v, int indent, int depth) {
    if (depth > 50) {
        sb_appendc(sb, "...");
        return;
    }
    /* 1行に収まる（60桁以内）なら1行で表示する */
    StrBuf one;
    sb_init(&one);
    value_to_sb(&one, v, 1);
    size_t one_w = display_width(one.buf ? one.buf : "", one.len);
    sb_free(&one);
    if (one_w + (size_t)indent <= 60) {
        repr_colored(sb, v, depth);
        return;
    }
    if (IS_LIST(v) && AS_LIST(v)->count > 0) {
        ListObj *l = AS_LIST(v);
        sb_append(sb, "[\n", 2);
        for (size_t i = 0; i < l->count; i++) {
            sb_repeat(sb, " ", indent + 2);
            pp_value(sb, l->items[i], indent + 2, depth + 1);
            if (i + 1 < l->count) sb_append(sb, ",", 1);
            sb_append(sb, "\n", 1);
        }
        sb_repeat(sb, " ", indent);
        sb_append(sb, "]", 1);
        return;
    }
    if (IS_MAP(v) && AS_MAP(v)->live > 0) {
        MapObj *m = AS_MAP(v);
        if (m->tag) {
            sb_appendc(sb, ui_c(0, UI_BOLD));
            sb_appendc(sb, m->tag);
            sb_appendc(sb, ui_c(0, UI_RESET));
            sb_append(sb, " ", 1);
        }
        sb_append(sb, "{\n", 2);
        size_t seen = 0;
        for (size_t i = 0; i < m->count; i++) {
            if (m->entries[i].deleted) continue;
            sb_repeat(sb, " ", indent + 2);
            sb_appendc(sb, ui_c(0, UI_CYAN));
            value_to_sb(sb, m->entries[i].key, 0);
            sb_appendc(sb, ui_c(0, UI_RESET));
            sb_append(sb, ": ", 2);
            pp_value(sb, m->entries[i].val, indent + 2, depth + 1);
            if (++seen < m->live) sb_append(sb, ",", 1);
            sb_append(sb, "\n", 1);
        }
        sb_repeat(sb, " ", indent);
        sb_append(sb, "}", 1);
        return;
    }
    repr_colored(sb, v, depth);
}

B(pp) {
    UNUSED;
    StrBuf sb;
    sb_init(&sb);
    pp_value(&sb, argv[0], 0, 0);
    sb_append(&sb, "\n", 1);
    fwrite(sb.buf, 1, sb.len, stdout);
    sb_free(&sb);
    return v_nil();
}

B(clear_screen) {
    UNUSED;
    if (ui_color_out) fputs("\x1b[2J\x1b[H", stdout);
    fflush(stdout);
    return v_nil();
}

/* 1行読む（改行は含めない）。入力が終わっていれば 0 を返す */
static int read_line(StrBuf *sb) {
    int c, got = 0;
    fflush(stdout);
    while ((c = fgetc(stdin)) != EOF) {
        got = 1;
        if (c == '\n') break;
        char ch = (char)c;
        sb_append(sb, &ch, 1);
    }
    if (sb->len && sb->buf[sb->len - 1] == '\r') sb->buf[--sb->len] = '\0';
    return got;
}

B(confirm) {
    UNUSED;
    StrBuf in;
    sb_init(&in);
    printf("%s %s[y/N]%s ", AS_STR(value_to_str(argv[0]))->chars, ui_c(0, UI_GRAY), ui_c(0, UI_RESET));
    if (!read_line(&in)) {
        sb_free(&in);
        return v_bool(0);
    }
    const char *a = in.buf ? in.buf : "";
    int yes = strcmp(a, "y") == 0 || strcmp(a, "Y") == 0 || strcmp(a, "yes") == 0 || strcmp(a, "はい") == 0;
    sb_free(&in);
    return v_bool(yes);
}

B(choose) {
    UNUSED;
    ListObj *opts = arg_list(argv, 1);
    if (opts->count == 0) rt_error("choose() の選択肢が空です");
    printf("%s%s%s\n", ui_c(0, UI_BOLD), AS_STR(value_to_str(argv[0]))->chars, ui_c(0, UI_RESET));
    for (size_t i = 0; i < opts->count; i++)
        printf("  %s%zu)%s %s\n", ui_c(0, UI_CYAN), i + 1, ui_c(0, UI_RESET), AS_STR(value_to_str(opts->items[i]))->chars);
    for (;;) {
        printf("番号を入力 (1-%zu): ", opts->count);
        StrBuf in;
        sb_init(&in);
        if (!read_line(&in)) {
            sb_free(&in);
            return v_nil();
        }
        char *end;
        long n = strtol(in.buf ? in.buf : "", &end, 10);
        int ok = in.buf && end != in.buf && *end == '\0' && n >= 1 && (size_t)n <= opts->count;
        sb_free(&in);
        if (ok) return opts->items[n - 1];
        printf("%s1〜%zu の番号を入力してください%s\n", ui_c(0, UI_YELLOW), opts->count, ui_c(0, UI_RESET));
    }
}

/* ===================================================================== */
/*  書式・JSON                                                            */
/* ===================================================================== */

/* format("{} は {:>5} 点（{:.1}%）", name, score, rate)
 *   {}      次の引数をそのまま
 *   {:>8}   右寄せ（< 左寄せ、^ 中央）。> の前に埋め文字を書ける（{:0>5}）
 *   {:.2}   小数点以下の桁数
 *   {{ }}   波かっこそのもの */
/* 書式指定 [埋め文字][< > ^][幅][.桁数] を解釈して v を out に書く。
 * 書式として読めなければ 0 を返す */
int apply_format_spec(StrBuf *out, Value v, const char *spec, size_t speclen) {
    char fill[8] = " ";
    char align = IS_NUM(v) ? 'r' : 'l';
    long width = 0, prec = -1;
    const char *q = spec, *qe = spec + speclen;
    size_t fl = q < qe ? utf8_char_len((unsigned char)*q) : 0;
    if (fl < sizeof(fill) && q + fl < qe && (q[fl] == '<' || q[fl] == '>' || q[fl] == '^')) {
        memcpy(fill, q, fl);
        fill[fl] = '\0';
        align = q[fl] == '<' ? 'l' : q[fl] == '>' ? 'r' : 'c';
        q += fl + 1;
    } else if (q < qe && (*q == '<' || *q == '>' || *q == '^')) {
        align = *q == '<' ? 'l' : *q == '>' ? 'r' : 'c';
        q++;
    }
    while (q < qe && *q >= '0' && *q <= '9') width = width * 10 + (*q++ - '0');
    if (q < qe && *q == '.') {
        q++;
        if (q >= qe) return 0;
        prec = 0;
        while (q < qe && *q >= '0' && *q <= '9') prec = prec * 10 + (*q++ - '0');
    }
    if (q != qe || width > 1000 || prec > 20) return 0;
    StrBuf cell;
    sb_init(&cell);
    if (prec >= 0 && IS_NUM(v)) sb_printf(&cell, "%.*f", (int)prec, v.as.n);
    else value_to_sb(&cell, v, 0);
    size_t w = display_width(cell.buf ? cell.buf : "", cell.len);
    size_t pad = (size_t)width > w ? (size_t)width - w : 0;
    size_t left = align == 'r' ? pad : align == 'c' ? pad / 2 : 0;
    for (size_t i = 0; i < left; i++) sb_appendc(out, fill);
    if (cell.len) sb_append(out, cell.buf, cell.len);
    for (size_t i = left; i < pad; i++) sb_appendc(out, fill);
    sb_free(&cell);
    return 1;
}

/* format('{} は {:>5} 点', name, score)
 * 書式は "{式:>5}" と同じ。"..." の中では {} が埋め込みになるので '...' で書く */
B(format) {
    StrObj *fmt = arg_str(argv, 0);
    int next = 1;
    StrBuf sb;
    sb_init(&sb);
    const char *p = fmt->chars, *end = fmt->chars + fmt->len;
    while (p < end) {
        if (p[0] == '{' && p + 1 < end && p[1] == '{') { sb_append(&sb, "{", 1); p += 2; continue; }
        if (p[0] == '}' && p + 1 < end && p[1] == '}') { sb_append(&sb, "}", 1); p += 2; continue; }
        if (p[0] != '{') { sb_append(&sb, p, 1); p++; continue; }
        const char *close = memchr(p, '}', (size_t)(end - p));
        if (!close) { sb_free(&sb); rt_error("format() の '{' が閉じられていません"); }
        const char *spec = p + 1;
        size_t speclen = (size_t)(close - spec);
        if (next >= argc) { sb_free(&sb); rt_error("format() の {} の数に対して引数が足りません"); }
        Value v = argv[next++];
        if (speclen > 0 && (spec[0] != ':' || !apply_format_spec(&sb, v, spec + 1, speclen - 1))) {
            sb_free(&sb);
            rt_error("format() の書式 '{%.*s}' を読めません（{:>8} や {:.2} の形にしてください）", (int)speclen, spec);
        }
        if (speclen == 0) apply_format_spec(&sb, v, "", 0);
        p = close + 1;
    }
    return sb_to_value(&sb);
}

static void json_string(StrBuf *sb, const char *s, size_t len) {
    sb_append(sb, "\"", 1);
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        switch (c) {
            case '"': sb_appendc(sb, "\\\""); break;
            case '\\': sb_appendc(sb, "\\\\"); break;
            case '\n': sb_appendc(sb, "\\n"); break;
            case '\r': sb_appendc(sb, "\\r"); break;
            case '\t': sb_appendc(sb, "\\t"); break;
            default:
                if (c < 0x20) sb_printf(sb, "\\u%04x", c);
                else sb_append(sb, (const char *)&s[i], 1);
        }
    }
    sb_append(sb, "\"", 1);
}

static void json_newline(StrBuf *sb, long indent, int level) {
    if (indent <= 0) return;
    sb_append(sb, "\n", 1);
    sb_repeat(sb, " ", indent * level);
}

static void json_encode(StrBuf *sb, Value v, long indent, int level) {
    if (level > 100) rt_error("json() の入れ子が深すぎます");
    char num[64];
    switch (v.type) {
        case VAL_NIL: sb_appendc(sb, "null"); break;
        case VAL_BOOL: sb_appendc(sb, v.as.b ? "true" : "false"); break;
        case VAL_NUM:
            if (isnan(v.as.n) || isinf(v.as.n)) rt_error("json() に nan / inf は変換できません");
            format_number(num, sizeof(num), v.as.n);
            sb_appendc(sb, num);
            break;
        case VAL_STR: json_string(sb, AS_STR(v)->chars, AS_STR(v)->len); break;
        case VAL_LIST: {
            ListObj *l = AS_LIST(v);
            sb_append(sb, "[", 1);
            for (size_t i = 0; i < l->count; i++) {
                if (i) sb_append(sb, ",", 1);
                json_newline(sb, indent, level + 1);
                json_encode(sb, l->items[i], indent, level + 1);
            }
            if (l->count) json_newline(sb, indent, level);
            sb_append(sb, "]", 1);
            break;
        }
        case VAL_MAP: {
            MapObj *m = AS_MAP(v);
            size_t n = 0;
            sb_append(sb, "{", 1);
            for (size_t i = 0; i < m->count; i++) {
                if (m->entries[i].deleted) continue;
                if (n++) sb_append(sb, ",", 1);
                json_newline(sb, indent, level + 1);
                Value k = m->entries[i].key;
                StrBuf ks;
                sb_init(&ks);
                value_to_sb(&ks, k, 0);
                json_string(sb, ks.buf ? ks.buf : "", ks.len);
                sb_free(&ks);
                sb_append(sb, indent > 0 ? ": " : ":", indent > 0 ? 2 : 1);
                json_encode(sb, m->entries[i].val, indent, level + 1);
            }
            if (n) json_newline(sb, indent, level);
            sb_append(sb, "}", 1);
            break;
        }
        default:
            rt_error("json() に %s は変換できません", type_name(v));
    }
}

B(to_json) {
    long indent = argc > 1 ? arg_int(argv, 1) : 0;
    StrBuf sb;
    sb_init(&sb);
    json_encode(&sb, argv[0], indent, 0);
    return sb_to_value(&sb);
}

typedef struct {
    const char *s;
    size_t len, i;
} JsonP;

LP_NORETURN static void json_fail(JsonP *p, const char *what) {
    rt_error("JSON を読めません: %s（%zu 文字目）", what, p->i + 1);
}

static void json_ws(JsonP *p) {
    while (p->i < p->len && (p->s[p->i] == ' ' || p->s[p->i] == '\t' || p->s[p->i] == '\n' || p->s[p->i] == '\r')) p->i++;
}

static Value json_value(JsonP *p, int depth);

static Value json_str(JsonP *p) {
    p->i++; /* " */
    StrBuf sb;
    sb_init(&sb);
    while (p->i < p->len && p->s[p->i] != '"') {
        char c = p->s[p->i++];
        if (c != '\\') {
            sb_append(&sb, &c, 1);
            continue;
        }
        if (p->i >= p->len) break;
        char e = p->s[p->i++];
        switch (e) {
            case 'n': sb_append(&sb, "\n", 1); break;
            case 't': sb_append(&sb, "\t", 1); break;
            case 'r': sb_append(&sb, "\r", 1); break;
            case 'b': sb_append(&sb, "\b", 1); break;
            case 'f': sb_append(&sb, "\f", 1); break;
            case '/': sb_append(&sb, "/", 1); break;
            case '\\': sb_append(&sb, "\\", 1); break;
            case '"': sb_append(&sb, "\"", 1); break;
            case 'u': {
                unsigned long cp = 0;
                for (int k = 0; k < 4; k++) {
                    if (p->i >= p->len || !isxdigit((unsigned char)p->s[p->i])) {
                        sb_free(&sb);
                        json_fail(p, "\\u の後には16進数4桁が必要です");
                    }
                    char h = p->s[p->i++];
                    cp = cp * 16 + (unsigned long)(isdigit((unsigned char)h) ? h - '0' : tolower((unsigned char)h) - 'a' + 10);
                }
                char b[4];
                size_t n;
                if (cp < 0x80) { b[0] = (char)cp; n = 1; }
                else if (cp < 0x800) { b[0] = (char)(0xC0 | (cp >> 6)); b[1] = (char)(0x80 | (cp & 0x3F)); n = 2; }
                else { b[0] = (char)(0xE0 | (cp >> 12)); b[1] = (char)(0x80 | ((cp >> 6) & 0x3F)); b[2] = (char)(0x80 | (cp & 0x3F)); n = 3; }
                sb_append(&sb, b, n);
                break;
            }
            default:
                sb_free(&sb);
                json_fail(p, "不明なエスケープです");
        }
    }
    if (p->i >= p->len) {
        sb_free(&sb);
        json_fail(p, "文字列が閉じられていません");
    }
    p->i++;
    return sb_to_value(&sb);
}

static Value json_value(JsonP *p, int depth) {
    if (depth > 200) json_fail(p, "入れ子が深すぎます");
    json_ws(p);
    if (p->i >= p->len) json_fail(p, "値がありません");
    char c = p->s[p->i];
    if (c == '"') return json_str(p);
    if (c == '{') {
        p->i++;
        Value m = v_map();
        root_push(m);
        json_ws(p);
        if (p->i < p->len && p->s[p->i] == '}') {
            p->i++;
            root_pop(1);
            return m;
        }
        for (;;) {
            json_ws(p);
            if (p->i >= p->len || p->s[p->i] != '"') json_fail(p, "キーは文字列にしてください");
            Value k = json_str(p);
            root_push(k);
            json_ws(p);
            if (p->i >= p->len || p->s[p->i] != ':') json_fail(p, "':' が必要です");
            p->i++;
            Value v = json_value(p, depth + 1);
            map_set(AS_MAP(m), k, v);
            root_pop(1);
            json_ws(p);
            if (p->i < p->len && p->s[p->i] == ',') { p->i++; continue; }
            if (p->i < p->len && p->s[p->i] == '}') { p->i++; break; }
            json_fail(p, "',' か '}' が必要です");
        }
        root_pop(1);
        return m;
    }
    if (c == '[') {
        p->i++;
        Value l = v_list(0);
        root_push(l);
        json_ws(p);
        if (p->i < p->len && p->s[p->i] == ']') {
            p->i++;
            root_pop(1);
            return l;
        }
        for (;;) {
            list_push(AS_LIST(l), json_value(p, depth + 1));
            json_ws(p);
            if (p->i < p->len && p->s[p->i] == ',') { p->i++; continue; }
            if (p->i < p->len && p->s[p->i] == ']') { p->i++; break; }
            json_fail(p, "',' か ']' が必要です");
        }
        root_pop(1);
        return l;
    }
    if (strncmp(p->s + p->i, "true", 4) == 0) { p->i += 4; return v_bool(1); }
    if (strncmp(p->s + p->i, "false", 5) == 0) { p->i += 5; return v_bool(0); }
    if (strncmp(p->s + p->i, "null", 4) == 0) { p->i += 4; return v_nil(); }
    if (c == '-' || isdigit((unsigned char)c)) {
        char *end;
        double d = strtod(p->s + p->i, &end);
        if (end == p->s + p->i) json_fail(p, "数値を読めません");
        p->i = (size_t)(end - p->s);
        return v_num(d);
    }
    json_fail(p, "予期しない文字があります");
}

B(from_json) {
    UNUSED;
    StrObj *s = arg_str(argv, 0);
    JsonP p = {s->chars, s->len, 0};
    Value v = json_value(&p, 0);
    json_ws(&p);
    if (p.i != p.len) json_fail(&p, "値の後に余分な文字があります");
    return v;
}

/* ===================================================================== */
/*  データ処理                                                            */
/* ===================================================================== */

B(group_by) {
    UNUSED;
    ListObj *src = to_list(argv[0], 0);
    root_push(v_obj(VAL_LIST, src));
    Value f = arg_fn(argv, 1);
    Value res = v_map();
    root_push(res);
    for (size_t i = 0; i < src->count; i++) {
        Value k = call1(f, src->items[i]);
        if (!is_hashable(k)) rt_error("group_by() の関数は 数値・文字列・真偽値・nil を返してください");
        root_push(k);
        Value group;
        if (!map_get(AS_MAP(res), k, &group)) {
            group = v_list(0);
            map_set(AS_MAP(res), k, group);
        }
        list_push(AS_LIST(group), src->items[i]);
        root_pop(1);
    }
    root_pop(2);
    return res;
}

B(partition) {
    UNUSED;
    ListObj *src = to_list(argv[0], 0);
    root_push(v_obj(VAL_LIST, src));
    Value f = arg_fn(argv, 1);
    ListObj *yes = list_new(0), *no = list_new(0);
    Value pair = v_list(2);
    root_push(pair);
    list_push(AS_LIST(pair), v_obj(VAL_LIST, yes));
    list_push(AS_LIST(pair), v_obj(VAL_LIST, no));
    for (size_t i = 0; i < src->count; i++) {
        Value x = src->items[i];
        list_push(is_truthy(call1(f, x)) ? yes : no, x);
    }
    root_pop(2);
    return pair;
}

B(tally) {
    UNUSED;
    ListObj *src = to_list(argv[0], 0);
    root_push(v_obj(VAL_LIST, src));
    Value res = v_map();
    root_push(res);
    for (size_t i = 0; i < src->count; i++) {
        Value x = src->items[i], cur;
        if (!is_hashable(x)) rt_error("tally() で数えられるのは 数値・文字列・真偽値・nil です");
        double n = map_get(AS_MAP(res), x, &cur) ? cur.as.n : 0;
        map_set(AS_MAP(res), x, v_num(n + 1));
    }
    root_pop(2);
    return res;
}

B(chunk) {
    UNUSED;
    ListObj *src = to_list(argv[0], 0);
    root_push(v_obj(VAL_LIST, src));
    long n = arg_int(argv, 1);
    if (n < 1) rt_error("chunk() の大きさは 1 以上にしてください");
    ListObj *res = list_new(0);
    root_push(v_obj(VAL_LIST, res));
    for (size_t i = 0; i < src->count; i += (size_t)n) {
        ListObj *part = list_new((size_t)n);
        list_push(res, v_obj(VAL_LIST, part));
        for (size_t j = i; j < src->count && j < i + (size_t)n; j++) list_push(part, src->items[j]);
    }
    root_pop(2);
    return v_obj(VAL_LIST, res);
}

static Value take_drop(Value *argv, int take) {
    ListObj *src = to_list(argv[0], 0);
    root_push(v_obj(VAL_LIST, src));
    long n = arg_int(argv, 1);
    if (n < 0) n = 0;
    size_t k = (size_t)n > src->count ? src->count : (size_t)n;
    size_t a = take ? 0 : k, b = take ? k : src->count;
    ListObj *res = list_new(b - a);
    for (size_t i = a; i < b; i++) list_push(res, src->items[i]);
    root_pop(1);
    return v_obj(VAL_LIST, res);
}

B(take) { UNUSED; return take_drop(argv, 1); }
B(drop) { UNUSED; return take_drop(argv, 0); }

B(first) {
    UNUSED;
    ListObj *src = to_list(argv[0], 0);
    return src->count ? src->items[0] : v_nil();
}

B(last) {
    UNUSED;
    ListObj *src = to_list(argv[0], 0);
    return src->count ? src->items[src->count - 1] : v_nil();
}

static Value best_by(Value *argv, int sign) {
    ListObj *src = to_list(argv[0], 0);
    root_push(v_obj(VAL_LIST, src));
    Value f = arg_fn(argv, 1);
    if (src->count == 0) {
        root_pop(1);
        return v_nil();
    }
    Value best = src->items[0];
    Value best_key = call1(f, best);
    root_push(best_key);
    size_t slot = root_depth() - 1;
    for (size_t i = 1; i < src->count; i++) {
        Value k = call1(f, src->items[i]);
        if (cmp_values(k, root_get(slot)) * sign > 0) {
            best = src->items[i];
            root_pop(1);
            root_push(k);
        }
    }
    root_pop(2);
    return best;
}

B(min_by) { UNUSED; return best_by(argv, -1); }
B(max_by) { UNUSED; return best_by(argv, 1); }

B(is_empty) {
    UNUSED;
    Value v = argv[0];
    switch (v.type) {
        case VAL_NIL: return v_bool(1);
        case VAL_STR: return v_bool(AS_STR(v)->len == 0);
        case VAL_LIST: return v_bool(AS_LIST(v)->count == 0);
        case VAL_MAP: return v_bool(AS_MAP(v)->live == 0);
        case VAL_RANGE: {
            size_t n = 0;
            range_len(AS_RANGE(v), &n);
            return v_bool(n == 0);
        }
        default: arg_error(0, "文字列・リスト・マップ・範囲・nil", v);
    }
}

B(lines) {
    UNUSED;
    StrObj *s = arg_str(argv, 0);
    ListObj *res = list_new(0);
    root_push(v_obj(VAL_LIST, res));
    size_t st = 0;
    for (size_t i = 0; i < s->len; i++) {
        if (s->chars[i] == '\n') {
            size_t e = i > st && s->chars[i - 1] == '\r' ? i - 1 : i;
            list_push(res, v_str(s->chars + st, e - st));
            st = i + 1;
        }
    }
    if (st < s->len) list_push(res, v_str(s->chars + st, s->len - st));
    root_pop(1);
    return v_obj(VAL_LIST, res);
}

B(capitalize) {
    UNUSED;
    Value v = v_str(arg_str(argv, 0)->chars, AS_STR(argv[0])->len);
    StrObj *o = AS_STR(v);
    if (o->len && (unsigned char)o->chars[0] < 0x80) o->chars[0] = (char)toupper((unsigned char)o->chars[0]);
    o->hash = hash_bytes(o->chars, o->len);
    return v;
}

B(center) {
    Value sv = value_to_str(argv[0]);
    long width = arg_int(argv, 1);
    StrObj *fill = argc > 2 ? arg_str(argv, 2) : NULL;
    StrObj *s = AS_STR(sv);
    size_t w = display_width(s->chars, s->len);
    size_t pad = (size_t)width > w ? (size_t)width - w : 0;
    StrBuf sb;
    sb_init(&sb);
    for (size_t i = 0; i < pad / 2; i++) sb_appendc(&sb, fill ? fill->chars : " ");
    sb_append(&sb, s->chars, s->len);
    for (size_t i = pad / 2; i < pad; i++) sb_appendc(&sb, fill ? fill->chars : " ");
    return sb_to_value(&sb);
}

/* ===================================================================== */
/*  日付・環境                                                            */
/* ===================================================================== */

B(date) {
    const char *fmt = argc > 0 ? arg_str(argv, 0)->chars : "%Y-%m-%d %H:%M:%S";
    time_t t = argc > 1 ? (time_t)arg_num(argv, 1) : time(NULL);
    struct tm tmv;
#if defined(_WIN32)
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    char buf[256];
    size_t n = strftime(buf, sizeof(buf), fmt, &tmv);
    return v_str(buf, n);
}

B(env) {
    const char *v = getenv(arg_str(argv, 0)->chars);
    if (!v) return argc > 1 ? argv[1] : v_nil();
    return v_cstr(v);
}

/* ===================================================================== */
/*  登録                                                                  */
/* ===================================================================== */

static Value bi_help(int argc, Value *argv);

#define D(name, mn, mx, cat, sig, desc) {#name, bi_##name, mn, mx, cat, sig, desc}

/* laping doc / help() / REPL の :doc で表示する説明も、ここにまとめて書く */
static const Builtin table[] = {
    D(print, 0, -1, "入出力", "print(値...)", "値をスペース区切りで出力して改行する"),
    D(write, 0, -1, "入出力", "write(値...)", "改行せずに出力する"),
    D(input, 0, 1, "入出力", "input(プロンプト?)", "1行読み込む。入力が終わっていれば nil"),
    D(read_file, 1, 1, "入出力", "read_file(パス)", "ファイルの中身を文字列で返す"),
    D(write_file, 2, 2, "入出力", "write_file(パス, 値)", "ファイルに書き込む（上書き）"),
    D(append_file, 2, 2, "入出力", "append_file(パス, 値)", "ファイルの末尾に追記する"),
    D(file_exists, 1, 1, "入出力", "file_exists(パス)", "ファイルが存在するか"),
    D(color, 2, 2, "画面表示", "color(値, 色名)", "文字に色を付ける（red green yellow blue magenta cyan white gray / 赤 緑 青 など）"),
    D(bold, 1, 1, "画面表示", "bold(値)", "太字にする"),
    D(dim, 1, 1, "画面表示", "dim(値)", "薄い文字にする"),
    D(underline, 1, 1, "画面表示", "underline(値)", "下線を付ける"),
    D(table, 1, 2, "画面表示", "table(行のリスト, 見出し?)", "罫線付きの表を表示する（行はリストかマップ）"),
    D(box, 1, 2, "画面表示", "box(文字列, タイトル?)", "文字列を枠で囲んで表示する"),
    D(progress, 2, 3, "画面表示", "progress(現在, 全体, 幅?)", "進捗バーの文字列を返す（例: [████░░░░]  50%）"),
    D(pp, 1, 1, "画面表示", "pp(値)", "入れ子のリストやマップを見やすく整形して表示する"),
    D(clear_screen, 0, 0, "画面表示", "clear_screen()", "画面を消す"),
    D(confirm, 1, 1, "画面表示", "confirm(質問)", "y/N で確認し、はいなら true を返す"),
    D(choose, 2, 2, "画面表示", "choose(質問, 選択肢のリスト)", "番号付きの選択肢から1つ選ばせ、選ばれた値を返す"),
    D(width, 1, 1, "画面表示", "width(値)", "端末上の表示幅（全角は2）"),
    D(type, 1, 1, "型・変換", "type(値)", "型名（number string bool nil list map range function、record なら型名）"),
    D(str, 1, 1, "型・変換", "str(値)", "文字列に変換する（print と同じ表示）"),
    D(repr, 1, 1, "型・変換", "repr(値)", "文字列なら引用符付きの表現を返す"),
    D(num, 1, 1, "型・変換", "num(値)", "数値に変換する（\"3.5\" や \"0xFF\" も可）"),
    D(int, 1, 1, "型・変換", "int(値)", "小数点以下を切り捨てた整数にする"),
    D(bool, 1, 1, "型・変換", "bool(値)", "真偽値に変換する"),
    D(is_num, 1, 1, "型・変換", "is_num(値)", "数値、または数値に変換できる文字列か"),
    D(list, 0, 1, "型・変換", "list(値?)", "範囲・文字列・マップ(キー)からリストを作る"),
    D(range, 1, 3, "型・変換", "range(終わり) / range(始め, 終わり, 増分?)", "終わりを含まない範囲"),
    D(len, 1, 1, "型・変換", "len(値)", "長さ（文字列は文字数）"),
    D(copy, 1, 1, "型・変換", "copy(値)", "リスト・マップの浅いコピー"),
    D(to_json, 1, 2, "型・変換", "to_json(値, インデント?)", "JSON 文字列に変換する"),
    D(from_json, 1, 1, "型・変換", "from_json(文字列)", "JSON を読んで値にする"),
    D(push, 1, -1, "リスト", "push(リスト, 値...)", "末尾に追加する"),
    D(pop, 1, 2, "リスト", "pop(リスト, 位置?)", "末尾（または指定位置）を取り出す"),
    D(shift, 1, 1, "リスト", "shift(リスト)", "先頭を取り出す"),
    D(unshift, 1, -1, "リスト", "unshift(リスト, 値...)", "先頭に追加する"),
    D(insert, 3, 3, "リスト", "insert(リスト, 位置, 値)", "指定位置に挿入する"),
    D(remove, 2, 2, "リスト", "remove(リスト, 位置)", "指定位置を削除して返す"),
    D(clear, 1, 1, "リスト", "clear(リスト|マップ)", "空にする"),
    D(slice, 2, 3, "リスト", "slice(値, 始め, 終わり?)", "部分リスト・部分文字列（負の値は後ろから）"),
    D(index_of, 2, 2, "リスト", "index_of(値, 探す値)", "見つかった位置。なければ -1"),
    D(contains, 2, 2, "リスト", "contains(値, 探す値)", "含まれているか（x in xs と同じ）"),
    D(reverse, 1, 1, "リスト", "reverse(値)", "逆順にした新しいリスト・文字列"),
    D(sort, 1, 2, "リスト", "sort(リスト, キー関数?)", "並べ替えた新しいリスト（安定ソート）"),
    D(map, 2, 2, "リスト", "map(リスト, 関数)", "各要素に関数を適用したリスト"),
    D(filter, 2, 2, "リスト", "filter(リスト, 関数)", "条件を満たす要素だけのリスト"),
    D(each, 2, 2, "リスト", "each(リスト, 関数)", "各要素に関数を実行する"),
    D(reduce, 2, 3, "リスト", "reduce(リスト, 関数, 初期値?)", "畳み込み"),
    D(find, 2, 2, "リスト", "find(リスト, 関数)", "条件を満たす最初の要素（なければ nil）"),
    D(any, 1, 2, "リスト", "any(リスト, 関数?)", "いずれかが条件を満たすか"),
    D(all, 1, 2, "リスト", "all(リスト, 関数?)", "すべてが条件を満たすか"),
    D(count, 2, 2, "リスト", "count(リスト, 値|関数)", "等しい、または条件を満たす要素の数"),
    D(sum, 1, 1, "リスト", "sum(リスト)", "合計"),
    D(min, 1, -1, "リスト", "min(リスト) / min(値...)", "最小値"),
    D(max, 1, -1, "リスト", "max(リスト) / max(値...)", "最大値"),
    D(min_by, 2, 2, "リスト", "min_by(リスト, 関数)", "関数の値が最小の要素"),
    D(max_by, 2, 2, "リスト", "max_by(リスト, 関数)", "関数の値が最大の要素"),
    D(join, 1, 2, "リスト", "join(リスト, 区切り?)", "要素を文字列にしてつなげる"),
    D(zip, 2, 2, "リスト", "zip(a, b)", "[[a0, b0], [a1, b1], ...]"),
    D(enumerate, 1, 1, "リスト", "enumerate(リスト)", "[[0, x0], [1, x1], ...]"),
    D(flatten, 1, 1, "リスト", "flatten(リスト)", "1段平らにする"),
    D(unique, 1, 1, "リスト", "unique(リスト)", "重複を除く"),
    D(first, 1, 1, "リスト", "first(リスト)", "最初の要素（空なら nil）"),
    D(last, 1, 1, "リスト", "last(リスト)", "最後の要素（空なら nil）"),
    D(take, 2, 2, "リスト", "take(リスト, n)", "先頭から n 個"),
    D(drop, 2, 2, "リスト", "drop(リスト, n)", "先頭の n 個を除いた残り"),
    D(chunk, 2, 2, "リスト", "chunk(リスト, n)", "n 個ずつに区切る"),
    D(group_by, 2, 2, "リスト", "group_by(リスト, 関数)", "関数の値ごとにまとめたマップ"),
    D(partition, 2, 2, "リスト", "partition(リスト, 関数)", "[条件を満たすもの, 満たさないもの]"),
    D(tally, 1, 1, "リスト", "tally(リスト)", "値ごとの出現回数のマップ"),
    D(is_empty, 1, 1, "リスト", "is_empty(値)", "空か（nil も空とみなす）"),
    D(upper, 1, 1, "文字列", "upper(文字列)", "大文字にする"),
    D(lower, 1, 1, "文字列", "lower(文字列)", "小文字にする"),
    D(capitalize, 1, 1, "文字列", "capitalize(文字列)", "先頭を大文字にする"),
    D(trim, 1, 1, "文字列", "trim(文字列)", "前後の空白（全角スペースを含む）を除く"),
    D(split, 1, 2, "文字列", "split(文字列, 区切り?)", "分割する。省略すると空白で分割"),
    D(lines, 1, 1, "文字列", "lines(文字列)", "行ごとに分割する"),
    D(replace, 3, 3, "文字列", "replace(文字列, 古い, 新しい)", "すべて置換する"),
    D(starts_with, 2, 2, "文字列", "starts_with(文字列, 先頭)", "前方一致"),
    D(ends_with, 2, 2, "文字列", "ends_with(文字列, 末尾)", "後方一致"),
    D(chars, 1, 1, "文字列", "chars(文字列)", "1文字ずつのリスト"),
    D(ord, 1, 1, "文字列", "ord(文字)", "Unicode コードポイント"),
    D(chr, 1, 1, "文字列", "chr(番号)", "コードポイントから文字を作る"),
    D(pad_left, 2, 3, "文字列", "pad_left(値, 幅, 埋め文字?)", "左を埋めて幅を揃える"),
    D(pad_right, 2, 3, "文字列", "pad_right(値, 幅, 埋め文字?)", "右を埋めて幅を揃える"),
    D(center, 2, 3, "文字列", "center(値, 幅, 埋め文字?)", "中央に揃える（全角は幅2で数える）"),
    D(fixed, 2, 2, "文字列", "fixed(数値, 桁数)", "小数点以下の桁数を固定した文字列"),
    D(format, 1, -1, "文字列", "format(書式, 値...)", "{} に値を埋め込む。{:>8} で右寄せ、{:.2} で小数2桁"),
    D(keys, 1, 1, "マップ", "keys(マップ)", "キーのリスト"),
    D(values, 1, 1, "マップ", "values(マップ)", "値のリスト"),
    D(items, 1, 1, "マップ", "items(マップ)", "[キー, 値] のリスト"),
    D(has, 2, 2, "マップ", "has(マップ, キー)", "キーがあるか"),
    D(get, 2, 3, "マップ", "get(マップ|リスト, キー, 既定値?)", "値を取得。なければ既定値"),
    D(delete, 2, 2, "マップ", "delete(マップ, キー)", "キーを削除する"),
    D(merge, 1, -1, "マップ", "merge(マップ...)", "結合した新しいマップ（後の値が優先）"),
    D(abs, 1, 1, "数学", "abs(x)", "絶対値"),
    D(floor, 1, 1, "数学", "floor(x)", "切り捨て"),
    D(ceil, 1, 1, "数学", "ceil(x)", "切り上げ"),
    D(round, 1, 2, "数学", "round(x, 桁数?)", "四捨五入"),
    D(sqrt, 1, 1, "数学", "sqrt(x)", "平方根"),
    D(pow, 2, 2, "数学", "pow(x, y)", "べき乗（x ** y と同じ）"),
    D(exp, 1, 1, "数学", "exp(x)", "指数関数"),
    D(log, 1, 2, "数学", "log(x, 底?)", "対数"),
    D(sin, 1, 1, "数学", "sin(x)", "正弦"),
    D(cos, 1, 1, "数学", "cos(x)", "余弦"),
    D(tan, 1, 1, "数学", "tan(x)", "正接"),
    D(asin, 1, 1, "数学", "asin(x)", "逆正弦"),
    D(acos, 1, 1, "数学", "acos(x)", "逆余弦"),
    D(atan, 1, 1, "数学", "atan(x)", "逆正接"),
    D(atan2, 2, 2, "数学", "atan2(y, x)", "2引数の逆正接"),
    D(random, 0, 0, "乱数", "random()", "0以上1未満の乱数"),
    D(rand_int, 2, 2, "乱数", "rand_int(a, b)", "a以上b以下の整数の乱数"),
    D(choice, 1, 1, "乱数", "choice(リスト)", "ランダムに1つ選ぶ"),
    D(shuffle, 1, 1, "乱数", "shuffle(リスト)", "シャッフルした新しいリスト"),
    D(seed, 1, 1, "乱数", "seed(n)", "乱数の種を設定する"),
    D(time, 0, 0, "システム", "time()", "現在時刻（UNIX 時間、秒）"),
    D(date, 0, 2, "システム", "date(書式?, 時刻?)", "日時を文字列にする（既定は \"%Y-%m-%d %H:%M:%S\"）"),
    D(clock, 0, 0, "システム", "clock()", "CPU 時間（秒）"),
    D(sleep, 1, 1, "システム", "sleep(秒)", "指定秒数待つ"),
    D(env, 1, 2, "システム", "env(名前, 既定値?)", "環境変数を読む"),
    D(exit, 0, 1, "システム", "exit(コード?)", "プログラムを終了する"),
    D(assert, 1, 2, "システム", "assert(条件, メッセージ?)", "条件が偽ならエラーにする"),
    D(help, 0, 1, "システム", "help(関数?)", "組み込み関数の説明を表示する"),
    {NULL, NULL, 0, 0, NULL, NULL, NULL}
};

const Builtin *builtin_table(void) { return table; }

const Builtin *builtin_find(const char *name) {
    for (int i = 0; table[i].name; i++)
        if (strcmp(table[i].name, name) == 0) return &table[i];
    return NULL;
}

/* ===================================================================== */
/*  ヘルプ                                                                */
/* ===================================================================== */

void print_builtin_doc(const Builtin *b) {
    printf("  %s%s%s  %s%s%s\n", ui_c(0, UI_BOLD), b->sig, ui_c(0, UI_RESET), ui_c(0, UI_GRAY), b->category, ui_c(0, UI_RESET));
    printf("    %s\n", b->desc);
}

void print_builtin_list(void) {
    const char *last = NULL;
    for (int i = 0; table[i].name; i++) {
        if (!last || strcmp(last, table[i].category) != 0) {
            last = table[i].category;
            printf("\n%s■ %s%s\n", ui_c(0, UI_CYAN), last, ui_c(0, UI_RESET));
        }
        char pad[64];
        size_t w = display_width(table[i].sig, strlen(table[i].sig));
        size_t n = w < 34 ? 34 - w : 1;
        memset(pad, ' ', n);
        pad[n] = '\0';
        printf("  %s%s%s%s%s\n", ui_c(0, UI_BOLD), table[i].sig, ui_c(0, UI_RESET), pad, table[i].desc);
    }
}

B(help) {
    if (argc == 0) {
        print_builtin_list();
        return v_nil();
    }
    const char *name = argv[0].type == VAL_BUILTIN ? argv[0].as.bi->name : AS_STR(value_to_str(argv[0]))->chars;
    const Builtin *b = builtin_find(name);
    if (!b) rt_error("組み込み関数 '%s' はありません", name);
    print_builtin_doc(b);
    return v_nil();
}


void builtins_register(EnvObj *env) {
    for (int i = 0; table[i].name; i++) {
        Value v;
        v.type = VAL_BUILTIN;
        v.as.bi = &table[i];
        env_define(env, intern(table[i].name, strlen(table[i].name)), v);
    }
    env_define(env, intern("PI", 2), v_num(3.14159265358979323846));
    env_define(env, intern("E", 1), v_num(2.71828182845904523536));
    env_define(env, intern("INF", 3), v_num(HUGE_VAL));

    uint64_t s = (uint64_t)time(NULL);
#if !defined(_WIN32)
    s ^= (uint64_t)getpid() << 32;
#endif
    s ^= (uint64_t)clock();
    rng_state = s * 0x9E3779B97F4A7C15ULL + 0x1234567ULL;
    if (!rng_state) rng_state = 1;
}

/* エラーメッセージに関数名を出すため、呼び出し直前に設定する。前の値を返す */
const char *builtin_set_current(const char *name) {
    const char *prev = cur_fn;
    cur_fn = name;
    return prev;
}

void builtins_set_args(int argc, char **argv, int start) {
    ListObj *l = list_new(0);
    Value lv = v_obj(VAL_LIST, l);
    env_define(builtin_env, intern("args", 4), lv);
    for (int i = start + 1; i < argc; i++) list_push(l, v_cstr(argv[i]));
}
