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

B(type) { UNUSED; return v_cstr(type_name(argv[0])); }
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
/*  登録                                                                  */
/* ===================================================================== */

#define ENTRY(name, mn, mx) {#name, bi_##name, mn, mx}

static const Builtin table[] = {
    ENTRY(print, 0, -1), ENTRY(write, 0, -1), ENTRY(input, 0, 1),
    ENTRY(read_file, 1, 1), ENTRY(write_file, 2, 2), ENTRY(append_file, 2, 2), ENTRY(file_exists, 1, 1),
    ENTRY(type, 1, 1), ENTRY(str, 1, 1), ENTRY(repr, 1, 1), ENTRY(bool, 1, 1), ENTRY(num, 1, 1),
    ENTRY(int, 1, 1), ENTRY(is_num, 1, 1), ENTRY(len, 1, 1), ENTRY(list, 0, 1), ENTRY(range, 1, 3),
    ENTRY(copy, 1, 1),
    ENTRY(push, 1, -1), ENTRY(pop, 1, 2), ENTRY(shift, 1, 1), ENTRY(unshift, 1, -1), ENTRY(insert, 3, 3),
    ENTRY(remove, 2, 2), ENTRY(clear, 1, 1), ENTRY(index_of, 2, 2), ENTRY(contains, 2, 2),
    ENTRY(slice, 2, 3), ENTRY(reverse, 1, 1), ENTRY(sort, 1, 2), ENTRY(map, 2, 2), ENTRY(filter, 2, 2),
    ENTRY(each, 2, 2), ENTRY(reduce, 2, 3), ENTRY(find, 2, 2), ENTRY(any, 1, 2), ENTRY(all, 1, 2),
    ENTRY(count, 2, 2), ENTRY(sum, 1, 1), ENTRY(min, 1, -1), ENTRY(max, 1, -1), ENTRY(join, 1, 2),
    ENTRY(zip, 2, 2), ENTRY(enumerate, 1, 1), ENTRY(flatten, 1, 1), ENTRY(unique, 1, 1),
    ENTRY(upper, 1, 1), ENTRY(lower, 1, 1), ENTRY(trim, 1, 1), ENTRY(split, 1, 2), ENTRY(replace, 3, 3),
    ENTRY(starts_with, 2, 2), ENTRY(ends_with, 2, 2), ENTRY(chars, 1, 1), ENTRY(ord, 1, 1), ENTRY(chr, 1, 1),
    ENTRY(pad_left, 2, 3), ENTRY(pad_right, 2, 3), ENTRY(fixed, 2, 2),
    ENTRY(keys, 1, 1), ENTRY(values, 1, 1), ENTRY(items, 1, 1), ENTRY(has, 2, 2), ENTRY(get, 2, 3),
    ENTRY(delete, 2, 2), ENTRY(merge, 1, -1),
    ENTRY(abs, 1, 1), ENTRY(floor, 1, 1), ENTRY(ceil, 1, 1), ENTRY(round, 1, 2), ENTRY(sqrt, 1, 1),
    ENTRY(pow, 2, 2), ENTRY(sin, 1, 1), ENTRY(cos, 1, 1), ENTRY(tan, 1, 1), ENTRY(asin, 1, 1),
    ENTRY(acos, 1, 1), ENTRY(atan, 1, 1), ENTRY(atan2, 2, 2), ENTRY(log, 1, 2), ENTRY(exp, 1, 1),
    ENTRY(random, 0, 0), ENTRY(rand_int, 2, 2), ENTRY(seed, 1, 1), ENTRY(choice, 1, 1), ENTRY(shuffle, 1, 1),
    ENTRY(time, 0, 0), ENTRY(clock, 0, 0), ENTRY(sleep, 1, 1), ENTRY(exit, 0, 1), ENTRY(assert, 1, 2),
    {NULL, NULL, 0, 0}
};

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
