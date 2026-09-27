#ifndef LAPING_UPDATER_H
#define LAPING_UPDATER_H

/* "laping update" / "laping --update" で呼ばれる自己更新ルーチン。
 * GitHub Releases の最新タグを確認し、ローカルより新しければ
 * 実行ファイルを差し替える。 */
void run_self_update(const char *current_version);

#endif
