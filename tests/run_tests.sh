#!/bin/sh
# tests/*.lp を実行し、標準出力+標準エラー出力を同名の .expected と比較する。
# 使い方: tests/run_tests.sh [lapingのパス]
# LAPING_GC_STRESS=1 を付けて実行すると、GC を毎文実行してルート漏れを検出できる。
LAPING=${1:-./laping}
case "$LAPING" in
    /*) ;;
    *) LAPING="$(pwd)/$LAPING" ;;
esac
DIR=$(cd "$(dirname "$0")" && pwd)
pass=0
fail=0
for t in "$DIR"/*.lp; do
    name=$(basename "$t" .lp)
    expected="$DIR/$name.expected"
    [ -f "$expected" ] || continue
    actual=$(cd "$DIR" && "$LAPING" "$name.lp" 2>&1 < /dev/null)
    if [ "$actual" = "$(cat "$expected")" ]; then
        pass=$((pass + 1))
    else
        fail=$((fail + 1))
        echo "FAIL: $name"
        printf '%s\n' "$actual" | diff "$expected" - | head -20
    fi
done
echo "テスト結果: $pass 件成功, $fail 件失敗"
[ "$fail" -eq 0 ]
