#!/bin/sh
# tests/*.lp を実行し、標準出力+標準エラー出力を同名の .expected とバイト単位で比較する。
# 終了コードも確認する（既定は 0。NAME.exit があればその値を期待する）。
# 使い方: tests/run_tests.sh [lapingのパス]
# LAPING_GC_STRESS=1 を付けて実行すると、GC を毎文実行してルート漏れを検出できる。
LAPING=${1:-./laping}
case "$LAPING" in
    /*) ;;
    *) LAPING="$(pwd)/$LAPING" ;;
esac
DIR=$(cd "$(dirname "$0")" && pwd)
actual=$(mktemp)
trap 'rm -f "$actual"' EXIT
pass=0
fail=0
for t in "$DIR"/*.lp; do
    name=$(basename "$t" .lp)
    expected="$DIR/$name.expected"
    [ -f "$expected" ] || continue
    want_status=0
    [ -f "$DIR/$name.exit" ] && want_status=$(cat "$DIR/$name.exit")
    case "$name" in
        *_test)
            # test ブロックは laping test で実行する（所要時間は毎回変わるので消す）
            (cd "$DIR" && "$LAPING" test "$name.lp" 2>&1 < /dev/null) | sed 's/ ([0-9.]* 秒)$//' > "$actual"
            status=$(cd "$DIR" && "$LAPING" test "$name.lp" > /dev/null 2>&1 < /dev/null; echo $?)
            ;;
        *)
            (cd "$DIR" && "$LAPING" "$name.lp" > "$actual" 2>&1 < /dev/null)
            status=$?
            ;;
    esac
    if [ "$status" -ne "$want_status" ]; then
        fail=$((fail + 1))
        echo "FAIL: $name (終了コード $status、期待値 $want_status)"
    elif cmp -s "$expected" "$actual"; then
        pass=$((pass + 1))
    else
        fail=$((fail + 1))
        echo "FAIL: $name"
        diff "$expected" "$actual" | head -20
    fi
done
echo "テスト結果: $pass 件成功, $fail 件失敗"
[ "$fail" -eq 0 ] && [ $((pass + fail)) -gt 0 ]
