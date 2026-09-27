/* 値・ヒープオブジェクト・GC
 *
 * GC は単純なマーク&スイープ。ルートは
 *   - シャドウスタック（評価途中の一時値を root_push で積む）
 *   - 実行中の環境（スコープ）スタック・グローバル環境
 *   - 例外値・戻り値
 * 回収は文の実行開始時（安全点）にのみ行うので、式評価の途中で
 * 保持している値は「ユーザー関数の呼び出しをまたぐ場合だけ」
 * root_push しておけばよい。
 */
#include "laping.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Obj *all_objects = NULL;
static size_t bytes_allocated = 0;
static size_t next_gc = 1 << 20;

static Value *roots = NULL;
static size_t roots_top = 0, roots_cap = 0;

static Obj **gray = NULL;
static size_t gray_count = 0, gray_cap = 0;

void *xmalloc(size_t n) {
    void *p = malloc(n ? n : 1);
    if (!p) {
        fprintf(stderr, "Laping: メモリが不足しました\n");
        exit(1);
    }
    return p;
}

void *xcalloc(size_t count, size_t size) {
    void *p = calloc(count ? count : 1, size ? size : 1);
    if (!p) {
        fprintf(stderr, "Laping: メモリが不足しました\n");
        exit(1);
    }
    return p;
}

void *xrealloc(void *p, size_t n) {
    void *q = realloc(p, n ? n : 1);
    if (!q) {
        fprintf(stderr, "Laping: メモリが不足しました\n");
        exit(1);
    }
    return q;
}

Value v_nil(void) { Value v; v.type = VAL_NIL; v.as.n = 0; return v; }
Value v_bool(int b) { Value v; v.type = VAL_BOOL; v.as.b = b ? 1 : 0; return v; }
Value v_num(double n) { Value v; v.type = VAL_NUM; v.as.n = n; return v; }
Value v_obj(ValueType t, void *o) { Value v; v.type = t; v.as.o = (Obj *)o; return v; }

/* ===================================================================== */
/*  GC                                                                    */
/* ===================================================================== */

static Obj *gc_alloc(size_t size, ObjType type) {
    Obj *o = xmalloc(size);
    o->type = (unsigned char)type;
    o->marked = 0;
    o->perm = 0;
    o->next = all_objects;
    all_objects = o;
    bytes_allocated += size;
    return o;
}

void gc_account(long bytes) {
    if (bytes < 0 && (size_t)(-bytes) > bytes_allocated) bytes_allocated = 0;
    else bytes_allocated += bytes;
}

void gc_init(void) {}

void root_push(Value v) {
    if (roots_top >= roots_cap) {
        roots_cap = roots_cap ? roots_cap * 2 : 256;
        roots = xrealloc(roots, sizeof(Value) * roots_cap);
    }
    roots[roots_top++] = v;
}

void root_pop(int n) { roots_top -= (size_t)n; }
size_t root_depth(void) { return roots_top; }
void root_restore(size_t depth) { roots_top = depth; }
Value root_get(size_t i) { return roots[i]; }

void gc_mark_obj(Obj *o) {
    if (!o || o->marked || o->perm) return;
    o->marked = 1;
    if (o->type == OBJ_STR || o->type == OBJ_RANGE) return; /* 子を持たない */
    if (gray_count >= gray_cap) {
        gray_cap = gray_cap ? gray_cap * 2 : 256;
        gray = xrealloc(gray, sizeof(Obj *) * gray_cap);
    }
    gray[gray_count++] = o;
}

void gc_mark_value(Value v) {
    switch (v.type) {
        case VAL_STR: case VAL_LIST: case VAL_MAP: case VAL_FUNC: case VAL_RANGE:
            gc_mark_obj(v.as.o);
            break;
        default:
            break;
    }
}

static void blacken(Obj *o) {
    switch (o->type) {
        case OBJ_LIST: {
            ListObj *l = (ListObj *)o;
            for (size_t i = 0; i < l->count; i++) gc_mark_value(l->items[i]);
            break;
        }
        case OBJ_MAP: {
            MapObj *m = (MapObj *)o;
            for (size_t i = 0; i < m->count; i++) {
                if (m->entries[i].deleted) continue;
                gc_mark_value(m->entries[i].key);
                gc_mark_value(m->entries[i].val);
            }
            break;
        }
        case OBJ_FUNC:
            gc_mark_obj((Obj *)((FuncObj *)o)->closure);
            break;
        case OBJ_ENV: {
            EnvObj *e = (EnvObj *)o;
            gc_mark_obj((Obj *)e->parent);
            for (size_t i = 0; i < e->cap; i++)
                if (e->names[i]) gc_mark_value(e->vals[i]);
            break;
        }
        default:
            break;
    }
}

static void free_obj(Obj *o) {
    switch (o->type) {
        case OBJ_STR:
            bytes_allocated -= sizeof(StrObj) + ((StrObj *)o)->len + 1;
            break;
        case OBJ_LIST: {
            ListObj *l = (ListObj *)o;
            bytes_allocated -= sizeof(ListObj) + l->cap * sizeof(Value);
            free(l->items);
            break;
        }
        case OBJ_MAP: {
            MapObj *m = (MapObj *)o;
            bytes_allocated -= sizeof(MapObj) + m->cap * sizeof(MapEntry) + m->index_cap * sizeof(int32_t);
            free(m->entries);
            free(m->index);
            break;
        }
        case OBJ_ENV: {
            EnvObj *e = (EnvObj *)o;
            bytes_allocated -= sizeof(EnvObj) + e->cap * (sizeof(Value) + sizeof(char *));
            free(e->names);
            free(e->vals);
            break;
        }
        case OBJ_FUNC:
            bytes_allocated -= sizeof(FuncObj);
            break;
        case OBJ_RANGE:
            bytes_allocated -= sizeof(RangeObj);
            break;
    }
    free(o);
}

void gc_collect(void) {
    for (size_t i = 0; i < roots_top; i++) gc_mark_value(roots[i]);
    interp_mark_roots();
    while (gray_count > 0) blacken(gray[--gray_count]);

    Obj **p = &all_objects;
    while (*p) {
        Obj *o = *p;
        if (o->marked) {
            o->marked = 0;
            p = &o->next;
        } else {
            *p = o->next;
            free_obj(o);
        }
    }
    next_gc = bytes_allocated * 2;
    if (next_gc < (1 << 20)) next_gc = 1 << 20;
}

void gc_maybe_collect(void) {
    /* LAPING_GC_STRESS=1 で毎回回収する（GC のルート漏れを見つけるためのデバッグ用） */
    static int stress = -1;
    if (stress < 0) stress = getenv("LAPING_GC_STRESS") != NULL;
    if (stress || bytes_allocated > next_gc) gc_collect();
}

/* ===================================================================== */
/*  文字列                                                                */
/* ===================================================================== */

uint32_t hash_bytes(const char *s, size_t len) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < len; i++) {
        h ^= (unsigned char)s[i];
        h *= 16777619u;
    }
    return h;
}

static StrObj *str_fill(StrObj *o, const char *s, size_t len) {
    o->len = len;
    if (len) memcpy(o->chars, s, len);
    o->chars[len] = '\0';
    o->hash = hash_bytes(o->chars, len);
    return o;
}

StrObj *str_new(const char *s, size_t len) {
    StrObj *o = (StrObj *)gc_alloc(sizeof(StrObj) + len + 1, OBJ_STR);
    return str_fill(o, s, len);
}

StrObj *str_new_perm(const char *s, size_t len) {
    StrObj *o = xmalloc(sizeof(StrObj) + len + 1);
    o->h.type = OBJ_STR;
    o->h.marked = 0;
    o->h.perm = 1;
    o->h.next = NULL;
    return str_fill(o, s, len);
}

Value v_str(const char *s, size_t len) { return v_obj(VAL_STR, str_new(s, len)); }
Value v_cstr(const char *s) { return v_str(s, strlen(s)); }

size_t utf8_char_len(unsigned char c) {
    if (c < 0x80) return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 1;
}

size_t utf8_len(const char *s, size_t len) {
    size_t n = 0;
    for (size_t i = 0; i < len; i++)
        if (((unsigned char)s[i] & 0xC0) != 0x80) n++;
    return n;
}

/* index 文字目のバイト位置。範囲外なら len を返す */
size_t utf8_offset(const char *s, size_t len, size_t index) {
    size_t i = 0;
    while (index > 0 && i < len) {
        i += utf8_char_len((unsigned char)s[i]);
        index--;
    }
    return i > len ? len : i;
}

void sb_init(StrBuf *sb) { sb->buf = NULL; sb->len = 0; sb->cap = 0; }

void sb_append(StrBuf *sb, const char *s, size_t len) {
    if (sb->len + len + 1 > sb->cap) {
        size_t nc = sb->cap ? sb->cap * 2 : 64;
        while (nc < sb->len + len + 1) nc *= 2;
        sb->buf = xrealloc(sb->buf, nc);
        sb->cap = nc;
    }
    if (len) memcpy(sb->buf + sb->len, s, len);
    sb->len += len;
    sb->buf[sb->len] = '\0';
}

void sb_appendc(StrBuf *sb, const char *s) { sb_append(sb, s, strlen(s)); }

void sb_printf(StrBuf *sb, const char *fmt, ...) {
    char tmp[512];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if ((size_t)n < sizeof(tmp)) {
        sb_append(sb, tmp, (size_t)n);
        return;
    }
    char *big = xmalloc((size_t)n + 1);
    va_start(ap, fmt);
    vsnprintf(big, (size_t)n + 1, fmt, ap);
    va_end(ap);
    sb_append(sb, big, (size_t)n);
    free(big);
}

void sb_free(StrBuf *sb) {
    free(sb->buf);
    sb_init(sb);
}

Value sb_to_value(StrBuf *sb) {
    Value v = v_str(sb->buf ? sb->buf : "", sb->len);
    sb_free(sb);
    return v;
}

/* ===================================================================== */
/*  インターン（識別子名）                                                */
/* ===================================================================== */

static const char **intern_tab = NULL;
static size_t intern_cap = 0, intern_count = 0;

const char *intern(const char *s, size_t len) {
    if (intern_count * 2 >= intern_cap) {
        size_t nc = intern_cap ? intern_cap * 2 : 512;
        const char **nt = xcalloc(nc, sizeof(char *));
        for (size_t i = 0; i < intern_cap; i++) {
            if (!intern_tab[i]) continue;
            size_t j = hash_bytes(intern_tab[i], strlen(intern_tab[i])) & (nc - 1);
            while (nt[j]) j = (j + 1) & (nc - 1);
            nt[j] = intern_tab[i];
        }
        free(intern_tab);
        intern_tab = nt;
        intern_cap = nc;
    }
    size_t j = hash_bytes(s, len) & (intern_cap - 1);
    while (intern_tab[j]) {
        if (strlen(intern_tab[j]) == len && memcmp(intern_tab[j], s, len) == 0) return intern_tab[j];
        j = (j + 1) & (intern_cap - 1);
    }
    char *copy = xmalloc(len + 1);
    memcpy(copy, s, len);
    copy[len] = '\0';
    intern_tab[j] = copy;
    intern_count++;
    return copy;
}

/* ===================================================================== */
/*  リスト                                                                */
/* ===================================================================== */

ListObj *list_new(size_t cap) {
    ListObj *l = (ListObj *)gc_alloc(sizeof(ListObj), OBJ_LIST);
    l->count = 0;
    l->cap = cap;
    l->items = cap ? xmalloc(sizeof(Value) * cap) : NULL;
    bytes_allocated += cap * sizeof(Value);
    return l;
}

Value v_list(size_t cap) { return v_obj(VAL_LIST, list_new(cap)); }

static void list_grow(ListObj *l, size_t need) {
    if (need <= l->cap) return;
    size_t nc = l->cap ? l->cap * 2 : 8;
    while (nc < need) nc *= 2;
    l->items = xrealloc(l->items, sizeof(Value) * nc);
    bytes_allocated += (nc - l->cap) * sizeof(Value);
    l->cap = nc;
}

void list_push(ListObj *l, Value v) {
    list_grow(l, l->count + 1);
    l->items[l->count++] = v;
}

void list_insert(ListObj *l, size_t idx, Value v) {
    list_grow(l, l->count + 1);
    memmove(&l->items[idx + 1], &l->items[idx], sizeof(Value) * (l->count - idx));
    l->items[idx] = v;
    l->count++;
}

Value list_remove(ListObj *l, size_t idx) {
    Value v = l->items[idx];
    memmove(&l->items[idx], &l->items[idx + 1], sizeof(Value) * (l->count - idx - 1));
    l->count--;
    return v;
}

/* ===================================================================== */
/*  マップ                                                                */
/* ===================================================================== */

int is_hashable(Value v) {
    return v.type == VAL_STR || v.type == VAL_NUM || v.type == VAL_BOOL || v.type == VAL_NIL;
}

static uint32_t value_hash(Value v) {
    switch (v.type) {
        case VAL_STR: return AS_STR(v)->hash;
        case VAL_NUM: {
            double d = v.as.n;
            if (d == 0) d = 0; /* -0 と 0 を同一視 */
            uint64_t bits;
            memcpy(&bits, &d, sizeof(bits));
            bits ^= bits >> 33;
            bits *= 0xff51afd7ed558ccdULL;
            bits ^= bits >> 33;
            return (uint32_t)bits;
        }
        case VAL_BOOL: return v.as.b ? 0x9e3779b9u : 0x7f4a7c15u;
        default: return 0x12345u;
    }
}

static int key_equal(Value a, Value b) {
    if (a.type != b.type) return 0;
    switch (a.type) {
        case VAL_STR: {
            StrObj *x = AS_STR(a), *y = AS_STR(b);
            return x == y || (x->len == y->len && x->hash == y->hash && memcmp(x->chars, y->chars, x->len) == 0);
        }
        case VAL_NUM: return a.as.n == b.as.n;
        case VAL_BOOL: return a.as.b == b.as.b;
        case VAL_NIL: return 1;
        default: return 0;
    }
}

MapObj *map_new(void) {
    MapObj *m = (MapObj *)gc_alloc(sizeof(MapObj), OBJ_MAP);
    m->entries = NULL;
    m->count = m->live = m->cap = 0;
    m->index = NULL;
    m->index_cap = 0;
    m->tag = NULL;
    return m;
}

Value v_map(void) { return v_obj(VAL_MAP, map_new()); }

static long map_find_slot(MapObj *m, Value key, uint32_t h) {
    if (!m->index_cap) return -1;
    size_t mask = m->index_cap - 1;
    size_t j = h & mask;
    for (;;) {
        int32_t ei = m->index[j];
        if (ei < 0) return -1;
        MapEntry *e = &m->entries[ei];
        if (!e->deleted && e->hash == h && key_equal(e->key, key)) return ei;
        j = (j + 1) & mask;
    }
}

static void map_rebuild(MapObj *m, size_t min_entries) {
    /* 削除済みエントリを詰めてからインデックスを作り直す */
    size_t w = 0;
    for (size_t i = 0; i < m->count; i++)
        if (!m->entries[i].deleted) m->entries[w++] = m->entries[i];
    m->count = w;
    size_t need = min_entries > w ? min_entries : w;
    if (need + 1 > m->cap) {
        size_t nc = m->cap ? m->cap : 8;
        while (nc < need + 1) nc *= 2;
        m->entries = xrealloc(m->entries, sizeof(MapEntry) * nc);
        bytes_allocated += (nc - m->cap) * sizeof(MapEntry);
        m->cap = nc;
    }
    size_t ic = 16;
    while (ic < m->cap * 2) ic *= 2;
    if (ic != m->index_cap) {
        bytes_allocated += (ic - m->index_cap) * sizeof(int32_t);
        m->index = xrealloc(m->index, sizeof(int32_t) * ic);
        m->index_cap = ic;
    }
    for (size_t i = 0; i < ic; i++) m->index[i] = -1;
    for (size_t i = 0; i < m->count; i++) {
        size_t j = m->entries[i].hash & (ic - 1);
        while (m->index[j] >= 0) j = (j + 1) & (ic - 1);
        m->index[j] = (int32_t)i;
    }
}

int map_get(MapObj *m, Value key, Value *out) {
    long i = map_find_slot(m, key, value_hash(key));
    if (i < 0) return 0;
    if (out) *out = m->entries[i].val;
    return 1;
}

void map_set(MapObj *m, Value key, Value val) {
    uint32_t h = value_hash(key);
    long i = map_find_slot(m, key, h);
    if (i >= 0) {
        m->entries[i].val = val;
        return;
    }
    if (m->count + 1 > m->cap || (m->count + 1) * 2 > m->index_cap) map_rebuild(m, m->live + 1);
    MapEntry *e = &m->entries[m->count];
    e->key = key;
    e->val = val;
    e->hash = h;
    e->deleted = 0;
    size_t j = h & (m->index_cap - 1);
    while (m->index[j] >= 0) j = (j + 1) & (m->index_cap - 1);
    m->index[j] = (int32_t)m->count;
    m->count++;
    m->live++;
}

int map_delete(MapObj *m, Value key) {
    long i = map_find_slot(m, key, value_hash(key));
    if (i < 0) return 0;
    m->entries[i].deleted = 1;
    m->entries[i].key = v_nil();
    m->entries[i].val = v_nil();
    m->live--;
    return 1;
}

/* ===================================================================== */
/*  関数・範囲・環境                                                      */
/* ===================================================================== */

FuncObj *func_new(Node *decl, EnvObj *closure) {
    FuncObj *f = (FuncObj *)gc_alloc(sizeof(FuncObj), OBJ_FUNC);
    f->decl = decl;
    f->closure = closure;
    return f;
}

RangeObj *range_new(double start, double end, double step, int inclusive) {
    RangeObj *r = (RangeObj *)gc_alloc(sizeof(RangeObj), OBJ_RANGE);
    r->start = start;
    r->end = end;
    r->step = step;
    r->inclusive = inclusive;
    return r;
}

int range_len(RangeObj *r, size_t *out) {
    if (r->step == 0) return 0;
    double span = (r->end - r->start) / r->step;
    double n;
    if (span < 0) n = 0;
    else {
        n = floor(span);
        if (r->inclusive || n != span) n += 1;
    }
    if (n > 1e9) return 0;
    *out = (size_t)n;
    return 1;
}

double range_at(RangeObj *r, size_t i) { return r->start + r->step * (double)i; }

EnvObj *env_new(EnvObj *parent) {
    EnvObj *e = (EnvObj *)gc_alloc(sizeof(EnvObj), OBJ_ENV);
    e->parent = parent;
    e->cap = 8;
    e->count = 0;
    e->names = xcalloc(e->cap, sizeof(char *));
    e->vals = xmalloc(sizeof(Value) * e->cap);
    bytes_allocated += e->cap * (sizeof(Value) + sizeof(char *));
    return e;
}

static size_t ptr_hash(const char *p) {
    uintptr_t x = (uintptr_t)p;
    x ^= x >> 17;
    x *= 0xed5ad4bbu;
    x ^= x >> 11;
    return (size_t)x;
}

Value *env_find_local(EnvObj *e, const char *name) {
    size_t mask = e->cap - 1;
    size_t j = ptr_hash(name) & mask;
    while (e->names[j]) {
        if (e->names[j] == name) return &e->vals[j];
        j = (j + 1) & mask;
    }
    return NULL;
}

Value *env_find(EnvObj *e, const char *name) {
    for (; e; e = e->parent) {
        Value *v = env_find_local(e, name);
        if (v) return v;
    }
    return NULL;
}

void env_define(EnvObj *e, const char *name, Value v) {
    Value *slot = env_find_local(e, name);
    if (slot) {
        *slot = v;
        return;
    }
    if ((e->count + 1) * 2 > e->cap) {
        size_t nc = e->cap ? e->cap * 2 : 8;
        const char **nn = xcalloc(nc, sizeof(char *));
        Value *nv = xmalloc(sizeof(Value) * nc);
        for (size_t i = 0; i < e->cap; i++) {
            if (!e->names[i]) continue;
            size_t j = ptr_hash(e->names[i]) & (nc - 1);
            while (nn[j]) j = (j + 1) & (nc - 1);
            nn[j] = e->names[i];
            nv[j] = e->vals[i];
        }
        free(e->names);
        free(e->vals);
        bytes_allocated += (nc - e->cap) * (sizeof(Value) + sizeof(char *));
        e->names = nn;
        e->vals = nv;
        e->cap = nc;
    }
    size_t j = ptr_hash(name) & (e->cap - 1);
    while (e->names[j]) j = (j + 1) & (e->cap - 1);
    e->names[j] = name;
    e->vals[j] = v;
    e->count++;
}

/* ===================================================================== */
/*  汎用操作                                                              */
/* ===================================================================== */

static int equal_depth(Value a, Value b, int depth) {
    if (depth > 200) return 0;
    if (a.type != b.type) return 0;
    switch (a.type) {
        case VAL_NIL: return 1;
        case VAL_BOOL: return a.as.b == b.as.b;
        case VAL_NUM: return a.as.n == b.as.n;
        case VAL_STR: return key_equal(a, b);
        case VAL_BUILTIN: return a.as.bi == b.as.bi;
        case VAL_FUNC: return a.as.o == b.as.o;
        case VAL_RANGE: {
            RangeObj *x = AS_RANGE(a), *y = AS_RANGE(b);
            return x->start == y->start && x->end == y->end && x->step == y->step && x->inclusive == y->inclusive;
        }
        case VAL_LIST: {
            ListObj *x = AS_LIST(a), *y = AS_LIST(b);
            if (x == y) return 1;
            if (x->count != y->count) return 0;
            for (size_t i = 0; i < x->count; i++)
                if (!equal_depth(x->items[i], y->items[i], depth + 1)) return 0;
            return 1;
        }
        case VAL_MAP: {
            MapObj *x = AS_MAP(a), *y = AS_MAP(b);
            if (x == y) return 1;
            if (x->tag != y->tag) return 0;
            if (x->live != y->live) return 0;
            for (size_t i = 0; i < x->count; i++) {
                if (x->entries[i].deleted) continue;
                Value other;
                if (!map_get(y, x->entries[i].key, &other)) return 0;
                if (!equal_depth(x->entries[i].val, other, depth + 1)) return 0;
            }
            return 1;
        }
    }
    return 0;
}

int values_equal(Value a, Value b) { return equal_depth(a, b, 0); }

int is_truthy(Value v) {
    switch (v.type) {
        case VAL_NIL: return 0;
        case VAL_BOOL: return v.as.b;
        case VAL_NUM: return v.as.n != 0;
        case VAL_STR: return AS_STR(v)->len > 0;
        case VAL_LIST: return AS_LIST(v)->count > 0;
        case VAL_MAP: return AS_MAP(v)->live > 0;
        default: return 1;
    }
}

const char *value_type_name(Value v) {
    if (v.type == VAL_MAP && AS_MAP(v)->tag) return AS_MAP(v)->tag;
    return type_name(v);
}

const char *type_name(Value v) {
    switch (v.type) {
        case VAL_NIL: return "nil";
        case VAL_BOOL: return "bool";
        case VAL_NUM: return "number";
        case VAL_STR: return "string";
        case VAL_LIST: return "list";
        case VAL_MAP: return "map";
        case VAL_FUNC: case VAL_BUILTIN: return "function";
        case VAL_RANGE: return "range";
    }
    return "?";
}

void format_number(char *buf, size_t size, double d) {
    if (isnan(d)) { snprintf(buf, size, "nan"); return; }
    if (isinf(d)) { snprintf(buf, size, d > 0 ? "inf" : "-inf"); return; }
    if (d == floor(d) && fabs(d) < 1e15) {
        if (d == 0) d = 0; /* -0 を 0 と表示 */
        snprintf(buf, size, "%.0f", d);
        return;
    }
    snprintf(buf, size, "%.14g", d);
}

static void repr_string(StrBuf *sb, StrObj *s) {
    sb_append(sb, "\"", 1);
    for (size_t i = 0; i < s->len; i++) {
        char c = s->chars[i];
        switch (c) {
            case '"': sb_append(sb, "\\\"", 2); break;
            case '\\': sb_append(sb, "\\\\", 2); break;
            case '\n': sb_append(sb, "\\n", 2); break;
            case '\t': sb_append(sb, "\\t", 2); break;
            case '\r': sb_append(sb, "\\r", 2); break;
            case '{': sb_append(sb, "\\{", 2); break;
            case '}': sb_append(sb, "\\}", 2); break;
            default: sb_append(sb, &c, 1);
        }
    }
    sb_append(sb, "\"", 1);
}

static void to_sb_depth(StrBuf *sb, Value v, int repr, int depth) {
    char num[64];
    if (depth > 50) {
        sb_appendc(sb, "...");
        return;
    }
    switch (v.type) {
        case VAL_NIL: sb_appendc(sb, "nil"); break;
        case VAL_BOOL: sb_appendc(sb, v.as.b ? "true" : "false"); break;
        case VAL_NUM:
            format_number(num, sizeof(num), v.as.n);
            sb_appendc(sb, num);
            break;
        case VAL_STR:
            if (repr) repr_string(sb, AS_STR(v));
            else sb_append(sb, AS_STR(v)->chars, AS_STR(v)->len);
            break;
        case VAL_LIST: {
            ListObj *l = AS_LIST(v);
            sb_append(sb, "[", 1);
            for (size_t i = 0; i < l->count; i++) {
                if (i) sb_append(sb, ", ", 2);
                to_sb_depth(sb, l->items[i], 1, depth + 1);
            }
            sb_append(sb, "]", 1);
            break;
        }
        case VAL_MAP: {
            MapObj *m = AS_MAP(v);
            int first = 1;
            if (m->tag) {
                /* record の値は Point(x: 1, y: 2) と表示する */
                sb_appendc(sb, m->tag);
                sb_append(sb, "(", 1);
                for (size_t i = 0; i < m->count; i++) {
                    if (m->entries[i].deleted) continue;
                    if (!first) sb_append(sb, ", ", 2);
                    first = 0;
                    value_to_sb(sb, m->entries[i].key, 0);
                    sb_append(sb, ": ", 2);
                    to_sb_depth(sb, m->entries[i].val, 1, depth + 1);
                }
                sb_append(sb, ")", 1);
                break;
            }
            sb_append(sb, "{", 1);
            for (size_t i = 0; i < m->count; i++) {
                if (m->entries[i].deleted) continue;
                if (!first) sb_append(sb, ", ", 2);
                first = 0;
                to_sb_depth(sb, m->entries[i].key, 1, depth + 1);
                sb_append(sb, ": ", 2);
                to_sb_depth(sb, m->entries[i].val, 1, depth + 1);
            }
            sb_append(sb, "}", 1);
            break;
        }
        case VAL_FUNC: {
            Node *d = AS_FUNC(v)->decl;
            if (d->name) sb_printf(sb, "<fn %s>", d->name);
            else sb_appendc(sb, "<fn>");
            break;
        }
        case VAL_BUILTIN:
            sb_printf(sb, "<builtin %s>", v.as.bi->name);
            break;
        case VAL_RANGE: {
            RangeObj *r = AS_RANGE(v);
            char a[64], b[64];
            format_number(a, sizeof(a), r->start);
            format_number(b, sizeof(b), r->end);
            sb_printf(sb, "%s%s%s", a, r->inclusive ? ".." : "...", b);
            if (r->step != 1) {
                format_number(a, sizeof(a), r->step);
                sb_printf(sb, " (step %s)", a);
            }
            break;
        }
    }
}

void value_to_sb(StrBuf *sb, Value v, int repr) { to_sb_depth(sb, v, repr, 0); }

Value value_to_str(Value v) {
    if (v.type == VAL_STR) return v;
    StrBuf sb;
    sb_init(&sb);
    value_to_sb(&sb, v, 0);
    return sb_to_value(&sb);
}
