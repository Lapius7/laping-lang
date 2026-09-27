# try / catch / finally と throw
try {
    print(1 / 0)
} catch e {
    print("捕捉:", e)
}

try {
    throw "自作のエラー"
} catch e {
    print("捕捉:", e)
} finally {
    print("finally は必ず実行")
}

try {
    throw {code: 404, message: "見つかりません"}
} catch err {
    print(err.code, err.message)
}

fn risky(n) {
    throw "負の数です" if n < 0
    return n * 2
}
fn safe(n) {
    try {
        return risky(n)
    } catch e {
        return "エラー: {e}"
    }
}
print(safe(5), safe(-1))

# finally は return があっても実行される
fn with_finally() {
    try {
        return "try の戻り値"
    } finally {
        print("後片付け")
    }
}
print(with_finally())

# 入れ子の try と再送出
try {
    try {
        undefined_var
    } catch e {
        print("内側:", e)
        throw "再送出: " + e
    }
} catch e {
    print("外側:", e)
}

# 実行時エラーの例
errors = [
    fn() => "a" + 1,
    fn() => "a" - 1,
    fn() => [1, 2][5],
    fn() => {a: 1}["b"],
    fn() => {a: 1}.b,
    fn() => nil(),
    fn() => len(5),
    fn() => fn(a) => a,
    fn() => (fn(a) => a)(),
    fn() { "abc"[0] = "x" },
    fn() => 1 < "a",
    fn() => num("abc"),
    fn() => [].pop(),
    fn() => 5 % 0,
    fn() => 5.nosuchmethod(),
    fn() => assert(1 == 2, "アサーション失敗"),
]
for f in errors {
    try {
        f()
        print("エラーなし")
    } catch e {
        print(e)
    }
}

# 捕捉されないエラーは行番号付きで表示されて終了する
fn outer() {
    inner()
}
fn inner() {
    x = [1, 2, 3]
    return x[10]
}
outer()
print("ここには来ない")
