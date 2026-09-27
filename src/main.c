/* コマンドライン / 対話モード (REPL) */
#include "laping.h"
#include "updater.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <pthread.h>
#endif

static int g_argc;
static char **g_argv;
static int g_exit_code = 0;

static void usage(const char *prog) {
    fprintf(stderr,
            "使い方: %s [ファイル.lp] [引数...]\n"
            "       %s                 対話モード (REPL) を起動\n"
            "       %s -e \"コード\"     コードを直接実行\n"
            "       %s update          最新版を確認・更新\n"
            "       %s --version       バージョン表示\n",
            prog, prog, prog, prog, prog);
}

/* REPL: 括弧が閉じるまで複数行を読み込む */
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
    return quote || depth > 0;
}

static void repl(void) {
    printf("Laping %s 対話モード — exit() か Ctrl+D (Windows は Ctrl+Z → Enter) で終了\n", LAPING_VERSION);
    main_file = "<repl>";
    StrBuf buf;
    sb_init(&buf);
    char line[4096];
    for (;;) {
        printf(buf.len ? "... " : "> ");
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin)) {
            printf("\n");
            break;
        }
        sb_appendc(&buf, line);
        if (input_is_incomplete(buf.buf)) continue;

        TryFrame tf;
        try_push(&tf);
        if (setjmp(tf.buf) == 0) {
            Node *prog = parse_program(buf.buf, buf.len, "<repl>");
            int has_value;
            Value v = exec_program_repl(prog, &has_value);
            try_pop(&tf);
            if (has_value && !IS_NIL(v)) {
                StrBuf out;
                sb_init(&out);
                value_to_sb(&out, v, 1);
                printf("%s\n", out.buf);
                sb_free(&out);
            }
        } else {
            try_restore(&tf);
            report_error(thrown_value, thrown_line, thrown_file);
        }
        fflush(stdout);
        sb_free(&buf);
        sb_init(&buf);
    }
    sb_free(&buf);
}

static void real_main(void) {
    int argc = g_argc;
    char **argv = g_argv;

    if (argc >= 2 && (strcmp(argv[1], "update") == 0 || strcmp(argv[1], "--update") == 0)) {
        run_self_update(LAPING_VERSION);
        return;
    }
    if (argc >= 2 && (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-v") == 0)) {
        printf("laping %s\n", LAPING_VERSION);
        return;
    }
    if (argc >= 2 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        usage(argv[0]);
        return;
    }
    if (argc >= 2 && strcmp(argv[1], "-e") == 0) {
        if (argc < 3) {
            usage(argv[0]);
            g_exit_code = 1;
            return;
        }
        interp_init(argc, argv, 2);
        main_file = "<-e>";
        Node *prog = parse_program(argv[2], strlen(argv[2]), main_file);
        run_program(prog);
        return;
    }
    if (argc < 2) {
        interp_init(argc, argv, argc);
        repl();
        return;
    }
    interp_init(argc, argv, 1);
    g_exit_code = run_file(argv[1]);
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
