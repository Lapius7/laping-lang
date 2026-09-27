/* GitHub Releases を使った自己更新ロジック。
 * フルJSONパーサは持たず、レスポンス中の "tag_name" と
 * 該当OS向けアセットの "browser_download_url" を文字列検索で抜き出す。
 * Releasesのレスポンスは安定したキー名のJSONなので、この簡易抽出で十分。 */
#include "updater.h"
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#include <sys/stat.h>
#endif

#define GITHUB_REPO "Lapius7/laping-lang"
#define API_URL "https://api.github.com/repos/" GITHUB_REPO "/releases/latest"

#if defined(_WIN32)
#define ASSET_NAME "laping-windows-x86_64.zip"
#define EXE_SUFFIX ".exe"
#else
#define ASSET_NAME "laping-linux-x86_64"
#define EXE_SUFFIX ""
#endif

typedef struct {
    char *data;
    size_t size;
} Buffer;

static size_t write_cb(void *ptr, size_t size, size_t nmemb, void *userdata) {
    Buffer *buf = (Buffer *)userdata;
    size_t add = size * nmemb;
    char *resized = realloc(buf->data, buf->size + add + 1);
    if (!resized) return 0;
    buf->data = resized;
    memcpy(buf->data + buf->size, ptr, add);
    buf->size += add;
    buf->data[buf->size] = '\0';
    return add;
}

/* "key": "value" の value 部分を抜き出す。見つからなければ NULL。
 * 呼び出し側が free すること。 */
static char *extract_json_string(const char *json, const char *key) {
    char pattern[128];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *p = strstr(json, pattern);
    if (!p) return NULL;
    p = strchr(p + strlen(pattern), ':');
    if (!p) return NULL;
    p = strchr(p, '"');
    if (!p) return NULL;
    p++;
    const char *end = strchr(p, '"');
    if (!end) return NULL;
    size_t len = (size_t)(end - p);
    char *out = malloc(len + 1);
    memcpy(out, p, len);
    out[len] = '\0';
    return out;
}

/* 指定したアセット名を含むブロック内の browser_download_url を探す。
 * "name": "<asset_name>" ... "browser_download_url": "..." の並びを前提にする。 */
static char *extract_asset_url(const char *json, const char *asset_name) {
    char pattern[128];
    snprintf(pattern, sizeof(pattern), "\"%s\"", asset_name);
    const char *name_pos = strstr(json, pattern);
    if (!name_pos) return NULL;
    const char *url_key = strstr(name_pos, "\"browser_download_url\"");
    if (!url_key) return NULL;
    return extract_json_string(url_key, "browser_download_url");
}

/* "v1.2.3" 形式のバージョン文字列を比較する。a > b なら正、a < b なら負、等しければ0。 */
static int compare_versions(const char *a, const char *b) {
    if (a[0] == 'v' || a[0] == 'V') a++;
    if (b[0] == 'v' || b[0] == 'V') b++;
    int an, bn;
    while (1) {
        an = atoi(a);
        bn = atoi(b);
        if (an != bn) return an - bn;
        while (*a && *a != '.') a++;
        while (*b && *b != '.') b++;
        if (*a == '.') a++;
        if (*b == '.') b++;
        if (!*a && !*b) return 0;
        if (!*a) return -1;
        if (!*b) return 1;
    }
}

static int download_to_file(const char *url, const char *path) {
    CURL *curl = curl_easy_init();
    if (!curl) return -1;
    FILE *f = fopen(path, "wb");
    if (!f) { curl_easy_cleanup(curl); return -1; }
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, f);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "laping-updater/1.0");
    CURLcode res = curl_easy_perform(curl);
    fclose(f);
    curl_easy_cleanup(curl);
    return (res == CURLE_OK) ? 0 : -1;
}

void run_self_update(const char *current_version) {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    CURL *curl = curl_easy_init();
    if (!curl) {
        fprintf(stderr, "Laping: curl の初期化に失敗しました\n");
        curl_global_cleanup();
        return;
    }

    Buffer buf = { NULL, 0 };
    curl_easy_setopt(curl, CURLOPT_URL, API_URL);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "laping-updater/1.0");

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        fprintf(stderr, "Laping: 更新確認に失敗しました (%s)\n", curl_easy_strerror(res));
        free(buf.data);
        curl_global_cleanup();
        return;
    }

    char *latest_tag = extract_json_string(buf.data, "tag_name");
    if (!latest_tag) {
        fprintf(stderr, "Laping: リリース情報を解析できませんでした\n");
        free(buf.data);
        curl_global_cleanup();
        return;
    }

    if (compare_versions(latest_tag, current_version) <= 0) {
        printf("Laping は最新版です (現在: %s)\n", current_version);
        free(latest_tag);
        free(buf.data);
        curl_global_cleanup();
        return;
    }

    printf("新しいバージョンが見つかりました: %s -> %s\n", current_version, latest_tag);

    char *asset_url = extract_asset_url(buf.data, ASSET_NAME);
    if (!asset_url) {
        fprintf(stderr, "Laping: このOS向けの配布物が見つかりませんでした (%s)\n", ASSET_NAME);
        free(latest_tag);
        free(buf.data);
        curl_global_cleanup();
        return;
    }

#if defined(_WIN32)
    char download_path[] = "laping_update_tmp.zip";
#else
    char download_path[] = "laping_update_tmp";
#endif
    printf("ダウンロード中: %s\n", asset_url);
    if (download_to_file(asset_url, download_path) != 0) {
        fprintf(stderr, "Laping: ダウンロードに失敗しました\n");
        free(asset_url);
        free(latest_tag);
        free(buf.data);
        curl_global_cleanup();
        return;
    }

    char tmp_path[] = "laping_update_tmp" EXE_SUFFIX;
#if defined(_WIN32)
    /* Windowsはzip配布なので、標準搭載のtar.exeで展開して
     * laping.exe / libcurl-x64.dll を取り出す。 */
    if (system("tar -xf laping_update_tmp.zip") != 0) {
        fprintf(stderr, "Laping: zipの展開に失敗しました（tar.exeが必要です）\n");
        free(asset_url);
        free(latest_tag);
        free(buf.data);
        curl_global_cleanup();
        return;
    }
    remove(download_path);
    rename("laping.exe", tmp_path);
#else
    chmod(tmp_path, 0755);
#endif

    char self_path[4096];
#if defined(_WIN32)
    GetModuleFileNameA(NULL, self_path, sizeof(self_path));
#else
    ssize_t n = readlink("/proc/self/exe", self_path, sizeof(self_path) - 1);
    if (n < 0) {
        fprintf(stderr, "Laping: 自分自身のパスを取得できませんでした\n");
        free(asset_url);
        free(latest_tag);
        free(buf.data);
        curl_global_cleanup();
        return;
    }
    self_path[n] = '\0';
#endif

    char backup_path[4160];
    snprintf(backup_path, sizeof(backup_path), "%s.old", self_path);
    remove(backup_path);
    rename(self_path, backup_path);

    if (rename(tmp_path, self_path) != 0) {
        fprintf(stderr, "Laping: 実行ファイルの置き換えに失敗しました。手動で %s を %s に配置してください\n",
                tmp_path, self_path);
        rename(backup_path, self_path);
        free(asset_url);
        free(latest_tag);
        free(buf.data);
        curl_global_cleanup();
        return;
    }
    remove(backup_path);

#if defined(_WIN32)
    /* libcurl-x64.dll を実行ファイルと同じフォルダにコピーする。 */
    char dll_dest[4096];
    snprintf(dll_dest, sizeof(dll_dest), "%s", self_path);
    char *last_sep = strrchr(dll_dest, '\\');
    if (!last_sep) last_sep = strrchr(dll_dest, '/');
    if (last_sep) {
        snprintf(last_sep + 1, sizeof(dll_dest) - (size_t)(last_sep + 1 - dll_dest),
                  "libcurl-x64.dll");
        remove(dll_dest);
        rename("libcurl-x64.dll", dll_dest);
    }
#endif

    printf("更新が完了しました: %s\n", latest_tag);

    free(asset_url);
    free(latest_tag);
    free(buf.data);
    curl_global_cleanup();
}
