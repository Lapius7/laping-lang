/* コマンドライン / 対話モード (REPL)
 *
 *   laping <ファイル.lp> [引数...]   実行
 *   laping                           対話モード
 *   laping run / check / test / new / doc / help / version / update
 */
#include "laping.h"
#include "updater.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#if defined(_WIN32)
#include <direct.h>
#include <windows.h>
#define lp_mkdir(p) _mkdir(p)
#else
#include <dirent.h>
#include <pthread.h>
#include <unistd.h>
#define lp_mkdir(p) mkdir(p, 0755)
#endif

#if defined(_WIN32)
#define PLATFORM "windows-x86_64"
#elif defined(__APPLE__)
#define PLATFORM "macos"
#else
#define PLATFORM "linux-x86_64"
#endif

static int g_argc;
static char **g_argv;
static int g_exit_code = 0;

#define C(code) ui_c(0, code)

static double now_seconds(void) {
#if defined(_WIN32)
    return (double)GetTickCount64() / 1000.0;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
#endif
}

/* ===================================================================== */
/*  help / version                                                        */
/* ===================================================================== */

static void print_help(void) {
    printf("%s%sLaping %s%s — 読んだ順に書けて、間違いにすぐ気づける言語\n\n", C(UI_BOLD), C(UI_CYAN), LAPING_VERSION, C(UI_RESET));
    printf("%s使い方:%s\n", C(UI_BOLD), C(UI_RESET));
    static const char *rows[][2] = {
        {"laping <ファイル.lp> [引数...]", "プログラムを実行する"},
        {"laping", "対話モード (REPL) を起動する"},
        {"laping run <ファイル> [引数...]", "実行する（laping <ファイル> と同じ）"},
        {"laping check <ファイル...>", "実行せずに構文だけを確認する"},
        {"laping test [ファイル|フォルダ...]", "test ブロックを実行する（省略時は *_test.lp を探す）"},
        {"laping new <名前>", "新しいプロジェクトのひな形を作る"},
        {"laping doc [関数名]", "組み込み関数の一覧・説明を表示する"},
        {"laping -e \"コード\"", "コードを直接実行する"},
        {"laping update", "最新版を確認して更新する"},
        {"laping version", "バージョンを表示する"},
    };
    for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); i++) {
        size_t w = display_width(rows[i][0], strlen(rows[i][0]));
        printf("  %s%s%s%*s%s\n", C(UI_GREEN), rows[i][0], C(UI_RESET), (int)(w < 36 ? 36 - w : 1), "", rows[i][1]);
    }
    printf("\n%s例:%s\n", C(UI_BOLD), C(UI_RESET));
    printf("  laping new hello && cd hello && laping main.lp\n");
    printf("  laping -e '[1, 2, 3] -> map(fn x => x * 2) -> print()'\n");
}

static void print_version(void) { printf("laping %s (%s)\n", LAPING_VERSION, PLATFORM); }

/* ===================================================================== */
/*  REPL                                                                  */
/* ===================================================================== */

/* 括弧や文字列が閉じていない、または行末が続きを求めているなら 1 */
static int input_is_incomplete(const char *s) {
    int depth = 0;
    char quote = 0;
    for (const char *p = s; *p; p++) {
        char c = *p;
        if (quote) {
            if (c == '\\' && p[1]) p++;
            else if (c == quote) quote = 0;
            continue;
        }
        if (c == '#') {
            while (*p && *p != '\n') p++;
            if (!*p) break;
            continue;
        }
        if (c == '"' || c == '\'') quote = c;
        else if (c == '(' || c == '[' || c == '{') depth++;
        else if (c == ')' || c == ']' || c == '}') depth--;
    }
    if (quote || depth > 0) return 1;
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' ' || s[n - 1] == '\t')) n--;
    if (n >= 2 && s[n - 2] == '-' && s[n - 1] == '>') return 1;
    if (n >= 1 && (s[n - 1] == ',' || s[n - 1] == '\\')) return 1;
    return 0;
}

static void repl_help(void) {
    static const char *rows[][2] = {
        {":help", "このヘルプ"},
        {":vars", "定義した変数と関数の一覧"},
        {":doc [関数名]", "組み込み関数の説明"},
        {":load <ファイル>", "ファイルを読み込んで実行する"},
        {":time <コード>", "コードを実行して時間を測る"},
        {":reset", "変数をすべて消す"},
        {":clear", "画面を消す"},
        {":q", "終了する（exit() や Ctrl+D でも終了）"},
    };
    for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); i++) {
        size_t w = display_width(rows[i][0], strlen(rows[i][0]));
        printf("  %s%s%s%*s%s\n", C(UI_CYAN), rows[i][0], C(UI_RESET), (int)(w < 18 ? 18 - w : 1), "", rows[i][1]);
    }
}

static int cmp_cstr(const void *a, const void *b) { return strcmp(*(const char *const *)a, *(const char *const *)b); }

static void repl_vars(void) {
    EnvObj *g = global_env;
    const char **names = xmalloc(sizeof(char *) * (g->count + 1));
    size_t n = 0;
    for (size_t i = 0; i < g->cap; i++)
        if (g->names[i]) names[n++] = g->names[i];
    if (n == 0) {
        printf("  %s（まだ何も定義されていません）%s\n", C(UI_GRAY), C(UI_RESET));
        free(names);
        return;
    }
    qsort(names, n, sizeof(char *), cmp_cstr);
    for (size_t i = 0; i < n; i++) {
        Value v = *env_find_local(g, names[i]);
        StrBuf sb;
        sb_init(&sb);
        repr_colored(&sb, v, 0);
        if (sb.len > 200) {
            sb.len = 200;
            sb.buf[200] = '\0';
            sb_appendc(&sb, C(UI_RESET));
            sb_appendc(&sb, " …");
        }
        printf("  %s%s%s %s: %s%s = %s\n", C(UI_BOLD), names[i], C(UI_RESET), C(UI_GRAY), value_type_name(v), C(UI_RESET), sb.buf);
        sb_free(&sb);
    }
    free(names);
}

static void doc_lookup(const char *name) {
    const Builtin *b = builtin_find(name);
    if (b) {
        print_builtin_doc(b);
        return;
    }
    printf("  組み込み関数 '%s' はありません。%slaping doc%s で一覧を見られます\n", name, C(UI_BOLD), C(UI_RESET));
}

/* src を REPL の続きとして実行し、式なら値を表示する */
static void repl_eval(const char *src, size_t len, const char *file) {
    TryFrame tf;
    try_push(&tf);
    if (setjmp(tf.buf) == 0) {
        Node *prog = parse_program(src, len, file);
        int has_value;
        Value v = exec_program_repl(prog, &has_value);
        try_pop(&tf);
        if (has_value && !IS_NIL(v)) {
            StrBuf out;
            sb_init(&out);
            repr_colored(&out, v, 0);
            printf("%s=>%s %s\n", C(UI_GRAY), C(UI_RESET), out.buf);
            sb_free(&out);
        }
    } else {
        try_restore(&tf);
        report_error_at(thrown_value, thrown_line, thrown_col, thrown_file, thrown_hint);
    }
    fflush(stdout);
}

static const char *skip_spaces(const char *p) {
    while (*p == ' ' || *p == '\t') p++;
    return p;
}

static void chomp(char *s) {
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' ')) s[--n] = '\0';
}

/* 1 を返したら REPL を終了する */
static int repl_command(char *line) {
    chomp(line);
    const char *arg = strchr(line, ' ');
    arg = arg ? skip_spaces(arg) : "";
    size_t cmdlen = strcspn(line, " ");
#define IS(word) (cmdlen == strlen(word) && strncmp(line, word, cmdlen) == 0)
    if (IS(":q") || IS(":quit") || IS(":exit")) return 1;
    if (IS(":help") || IS(":h") || IS(":?")) repl_help();
    else if (IS(":vars")) repl_vars();
    else if (IS(":doc")) {
        if (*arg) doc_lookup(arg);
        else print_builtin_list();
    } else if (IS(":clear")) {
        if (ui_color_out) fputs("\x1b[2J\x1b[H", stdout);
    } else if (IS(":reset")) {
        interp_reset_globals();
        printf("  %s変数をすべて消しました%s\n", C(UI_GRAY), C(UI_RESET));
    } else if (IS(":load")) {
        if (!*arg) {
            printf("  使い方: :load <ファイル>\n");
            return 0;
        }
        size_t len;
        char *src = read_whole_file(arg, &len);
        if (!src) {
            printf("  %sファイルを開けません: %s%s\n", C(UI_RED), arg, C(UI_RESET));
            return 0;
        }
        repl_eval(src, len, intern(arg, strlen(arg)));
        free(src);
    } else if (IS(":time")) {
        double t0 = now_seconds();
        repl_eval(arg, strlen(arg), "<repl>");
        printf("  %s%.3f 秒%s\n", C(UI_GRAY), now_seconds() - t0, C(UI_RESET));
    } else {
        printf("  不明なコマンド %s です。%s:help%s で一覧を表示します\n", line, C(UI_BOLD), C(UI_RESET));
    }
#undef IS
    return 0;
}

static void repl(void) {
    printf("%s%sLaping %s%s 対話モード\n", C(UI_BOLD), C(UI_CYAN), LAPING_VERSION, C(UI_RESET));
    printf("%s:help でコマンド一覧、:q で終了。式を入力するとその値を表示します%s\n", C(UI_GRAY), C(UI_RESET));
    main_file = "<repl>";
    StrBuf buf;
    sb_init(&buf);
    char line[4096];
    for (;;) {
        if (buf.len) printf("%s   … %s", C(UI_GRAY), C(UI_RESET));
        else printf("%s%slaping›%s ", C(UI_BOLD), C(UI_CYAN), C(UI_RESET));
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin)) {
            printf("\n");
            break;
        }
        if (!buf.len && line[0] == ':') {
            if (repl_command(line)) break;
            continue;
        }
        sb_appendc(&buf, line);
        if (input_is_incomplete(buf.buf)) continue;
        repl_eval(buf.buf, buf.len, "<repl>");
        sb_free(&buf);
        sb_init(&buf);
    }
    sb_free(&buf);
}

/* ===================================================================== */
/*  check / test / new                                                    */
/* ===================================================================== */

static int cmd_check(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "使い方: laping check <ファイル...>\n");
        return 1;
    }
    int failed = 0;
    for (int i = 2; i < argc; i++) {
        size_t len;
        char *src = read_whole_file(argv[i], &len);
        if (!src) {
            fprintf(stderr, "%s✗%s %s（ファイルを開けません）\n", ui_c(1, UI_RED), ui_c(1, UI_RESET), argv[i]);
            failed++;
            continue;
        }
        TryFrame tf;
        try_push(&tf);
        if (setjmp(tf.buf) == 0) {
            parse_program(src, len, argv[i]);
            try_pop(&tf);
            printf("%s✓%s %s\n", C(UI_GREEN), C(UI_RESET), argv[i]);
        } else {
            try_restore(&tf);
            printf("%s✗%s %s\n", C(UI_RED), C(UI_RESET), argv[i]);
            report_error_at(thrown_value, thrown_line, thrown_col, thrown_file, thrown_hint);
            failed++;
        }
        free(src);
    }
    return failed ? 1 : 0;
}

typedef struct {
    char **items;
    int count, cap;
} PathList;

static void path_push(PathList *l, const char *p) {
    if (l->count >= l->cap) {
        l->cap = l->cap ? l->cap * 2 : 16;
        l->items = xrealloc(l->items, sizeof(char *) * (size_t)l->cap);
    }
    size_t n = strlen(p);
    l->items[l->count] = xmalloc(n + 1);
    memcpy(l->items[l->count], p, n + 1);
    l->count++;
}

static int ends_with(const char *s, const char *suffix) {
    size_t a = strlen(s), b = strlen(suffix);
    return a >= b && strcmp(s + a - b, suffix) == 0;
}

static int is_dir(const char *p) {
    struct stat st;
    return stat(p, &st) == 0 && (st.st_mode & S_IFMT) == S_IFDIR;
}

/* フォルダ内の *_test.lp を再帰的に集める（. で始まるフォルダは飛ばす） */
static void find_tests(const char *dir, PathList *out, int depth) {
    if (depth > 16) return;
#if defined(_WIN32)
    char pattern[1024];
    snprintf(pattern, sizeof(pattern), "%s\\*", dir);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        const char *name = fd.cFileName;
        if (name[0] == '.') continue;
        char path[1024];
        snprintf(path, sizeof(path), "%s\\%s", dir, name);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) find_tests(path, out, depth + 1);
        else if (ends_with(name, "_test.lp")) path_push(out, path);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;
        char path[1024];
        if (strcmp(dir, ".") == 0) snprintf(path, sizeof(path), "%s", e->d_name);
        else snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
        if (is_dir(path)) find_tests(path, out, depth + 1);
        else if (ends_with(e->d_name, "_test.lp")) path_push(out, path);
    }
    closedir(d);
#endif
}

static int cmd_test(int argc, char **argv) {
    PathList files = {NULL, 0, 0};
    if (argc < 3) find_tests(".", &files, 0);
    for (int i = 2; i < argc; i++) {
        if (is_dir(argv[i])) find_tests(argv[i], &files, 0);
        else path_push(&files, argv[i]);
    }
    if (files.count == 0) {
        printf("%sテストが見つかりません。%s*_test.lp というファイルに test \"名前\" { ... } を書いてください\n",
               C(UI_YELLOW), C(UI_RESET));
        return 1;
    }
    qsort(files.items, (size_t)files.count, sizeof(char *), cmp_cstr);
    test_mode = 1;
    double t0 = now_seconds();
    int file_errors = 0;
    for (int i = 0; i < files.count; i++) {
        const char *path = files.items[i];
        printf("%s%s▶ %s%s\n", C(UI_BOLD), C(UI_BLUE), path, C(UI_RESET));
        size_t len;
        char *src = read_whole_file(path, &len);
        if (!src) {
            printf("  %sファイルを開けません%s\n", C(UI_RED), C(UI_RESET));
            file_errors++;
            continue;
        }
        interp_reset_globals();
        main_file = path;
        TryFrame tf;
        try_push(&tf);
        if (setjmp(tf.buf) == 0) {
            run_program(parse_program(src, len, path));
            try_pop(&tf);
        } else {
            /* test ブロックの外で起きたエラー */
            try_restore(&tf);
            printf("  %s✗ test の外でエラーが起きました%s\n", C(UI_RED), C(UI_RESET));
            fflush(stdout);
            report_error_at(thrown_value, thrown_line, thrown_col, thrown_file, thrown_hint);
            file_errors++;
        }
        free(src);
    }
    double elapsed = now_seconds() - t0;
    int total = test_stats.passed + test_stats.failed;
    printf("\n");
    if (test_stats.failed == 0 && file_errors == 0) {
        printf("%s%s✓ %d 件のテストがすべて成功しました%s %s(%.2f 秒)%s\n", C(UI_BOLD), C(UI_GREEN), total, C(UI_RESET),
               C(UI_GRAY), elapsed, C(UI_RESET));
    } else {
        printf("%s%s✗ %d 件中 %d 件が失敗しました%s", C(UI_BOLD), C(UI_RED), total, test_stats.failed, C(UI_RESET));
        if (file_errors) printf("（ファイルのエラー %d 件）", file_errors);
        printf(" %s(%.2f 秒)%s\n", C(UI_GRAY), elapsed, C(UI_RESET));
    }
    if (total == 0 && file_errors == 0) {
        printf("%s（test ブロックが1つもありませんでした）%s\n", C(UI_YELLOW), C(UI_RESET));
        return 1;
    }
    return test_stats.failed || file_errors ? 1 : 0;
}

static int write_text(const char *path, const char *text) {
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    fputs(text, f);
    fclose(f);
    return 1;
}

static int cmd_new(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "使い方: laping new <プロジェクト名>\n");
        return 1;
    }
    const char *name = argv[2];
    if (is_dir(name)) {
        fprintf(stderr, "%s'%s' はすでに存在します%s\n", ui_c(1, UI_RED), name, ui_c(1, UI_RESET));
        return 1;
    }
    if (lp_mkdir(name) != 0) {
        fprintf(stderr, "フォルダ '%s' を作れません\n", name);
        return 1;
    }
    char path[1024];
    snprintf(path, sizeof(path), "%s/main.lp", name);
    int ok = write_text(path,
        "# Laping プログラム\n"
        "import \"lib\" as lib\n"
        "\n"
        "名前 = args[0] else \"世界\"\n"
        "print(lib.greet(名前))\n"
        "\n"
        "scores = [72, 95, 58, 81]\n"
        "passed = scores -> filter(fn x => x >= 60)\n"
        "print(\"合格: {len(passed)} 人 / 平均 {fixed(sum(scores) / len(scores), 1)} 点\")\n");
    snprintf(path, sizeof(path), "%s/lib.lp", name);
    ok = ok && write_text(path,
        "# import \"lib\" as lib で読み込まれるモジュール\n"
        "# _ で始まる名前は外から見えません\n"
        "\n"
        "fn greet(name) => \"こんにちは、{name}！\"\n"
        "\n"
        "fn _secret() => \"これは lib の中だけで使う関数\"\n");
    snprintf(path, sizeof(path), "%s/main_test.lp", name);
    ok = ok && write_text(path,
        "# laping test で実行されるテスト\n"
        "import \"lib\" as lib\n"
        "\n"
        "test \"あいさつ文を作れる\" {\n"
        "    expect lib.greet(\"太郎\") == \"こんにちは、太郎！\"\n"
        "}\n"
        "\n"
        "test \"非公開の関数は見えない\" {\n"
        "    expect not (\"_secret\" in lib)\n"
        "}\n");
    if (!ok) {
        fprintf(stderr, "ファイルを書き込めません\n");
        return 1;
    }
    printf("%s✓%s プロジェクト %s%s%s を作りました\n\n", C(UI_GREEN), C(UI_RESET), C(UI_BOLD), name, C(UI_RESET));
    printf("  %s/main.lp        プログラム本体\n", name);
    printf("  %s/lib.lp         モジュール\n", name);
    printf("  %s/main_test.lp   テスト\n\n", name);
    printf("次のコマンドで動かせます:\n  cd %s\n  laping main.lp\n  laping test\n", name);
    return 0;
}

static int cmd_doc(int argc, char **argv) {
    if (argc < 3) {
        printf("%s組み込み関数の一覧%s（laping doc <関数名> で詳しく表示）\n", C(UI_BOLD), C(UI_RESET));
        print_builtin_list();
        return 0;
    }
    for (int i = 2; i < argc; i++) doc_lookup(argv[i]);
    return 0;
}

/* ===================================================================== */
/*  入口                                                                  */
/* ===================================================================== */

static int is_command(const char *a, const char *name) { return strcmp(a, name) == 0; }

static void real_main(void) {
    int argc = g_argc;
    char **argv = g_argv;
    ui_init();

    if (argc < 2) {
        interp_init(argc, argv, argc);
        repl();
        return;
    }
    const char *cmd = argv[1];
    if (is_command(cmd, "update") || is_command(cmd, "--update")) {
        run_self_update(LAPING_VERSION);
        return;
    }
    if (is_command(cmd, "version") || is_command(cmd, "--version") || is_command(cmd, "-v")) {
        print_version();
        return;
    }
    if (is_command(cmd, "help") || is_command(cmd, "--help") || is_command(cmd, "-h")) {
        print_help();
        return;
    }
    if (is_command(cmd, "check")) {
        interp_init(argc, argv, argc);
        g_exit_code = cmd_check(argc, argv);
        return;
    }
    if (is_command(cmd, "test")) {
        interp_init(argc, argv, argc);
        g_exit_code = cmd_test(argc, argv);
        return;
    }
    if (is_command(cmd, "new")) {
        g_exit_code = cmd_new(argc, argv);
        return;
    }
    if (is_command(cmd, "doc")) {
        interp_init(argc, argv, argc);
        g_exit_code = cmd_doc(argc, argv);
        return;
    }
    if (is_command(cmd, "-e")) {
        if (argc < 3) {
            fprintf(stderr, "使い方: laping -e \"コード\"\n");
            g_exit_code = 1;
            return;
        }
        interp_init(argc, argv, 2);
        main_file = "<-e>";
        run_program(parse_program(argv[2], strlen(argv[2]), main_file));
        return;
    }
    int script = 1;
    if (is_command(cmd, "run")) {
        if (argc < 3) {
            fprintf(stderr, "使い方: laping run <ファイル> [引数...]\n");
            g_exit_code = 1;
            return;
        }
        script = 2;
    } else if (cmd[0] == '-') {
        fprintf(stderr, "不明なオプション %s です。laping help で使い方を表示します\n", cmd);
        g_exit_code = 1;
        return;
    }
    interp_init(argc, argv, script);
    g_exit_code = run_file(argv[script]);
}

#if !defined(_WIN32)
static void *thread_main(void *arg) {
    (void)arg;
    real_main();
    return NULL;
}
#endif

int main(int argc, char **argv) {
#if defined(_WIN32)
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
    g_argc = argc;
    g_argv = argv;

#if defined(_WIN32)
    /* Windows ではリンク時にスタックサイズを大きくしている (-Wl,--stack) */
    real_main();
#else
    /* 深い再帰に耐えられるよう、大きなスタックを持つスレッドで実行する */
    pthread_attr_t attr;
    pthread_t th;
    if (pthread_attr_init(&attr) == 0 &&
        pthread_attr_setstacksize(&attr, (size_t)512 * 1024 * 1024) == 0 &&
        pthread_create(&th, &attr, thread_main, NULL) == 0) {
        pthread_join(th, NULL);
    } else {
        real_main();
    }
#endif
    fflush(stdout);
    return g_exit_code;
}
