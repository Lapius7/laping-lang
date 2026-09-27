# laping test で実行する（通常の実行では test ブロックは飛ばされる）
fn add(a, b) => a + b

test "足し算ができる" {
    expect add(1, 2) == 3
    expect add("a", "b") == "ab"
}

test "失敗すると左辺と右辺を表示する" {
    expect add(2, 2) == 5
}

test "エラーも失敗として報告する" {
    user = {name: "太郎"}
    expect user.nmae == "太郎"
}

test "test の中の変数は外に漏れない" {
    secret = 1
    expect secret == 1
}

test "漏れていないことを確認" {
    leaked = true
    try {
        secret
    } catch e {
        leaked = false
    }
    expect (leaked) == false
}
