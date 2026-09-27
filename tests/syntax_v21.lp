# v2.1 で追加した Laping 独自の構文

# -> パイプライン: 左の値が右の関数の第1引数になる
[3, 1, 4, 1, 5, 9, 2, 6]
    -> filter(fn x => x > 2)
    -> map(fn x => x * 10)
    -> sort()
    -> print()
print((1..10) -> sum(), "abc" -> upper(), 16 -> sqrt)

# 値 else 既定値: nil・存在しないキー・範囲外の添字なら既定値
config = {server: {port: 8080}}
print(config.server.port else 80)
print(config.db.host else "localhost")
print(config["missing"] else "なし", [1, 2][5] else "範囲外", nil else "nil")
print(0 else "0 は置き換えない", "" else "空文字も置き換えない", false else "false も")
print(args[0] else "引数なし")
fn find_user(id) => id == 1 then {name: "太郎"} else nil
print(find_user(1).name else "不明", find_user(2).name else "不明")

# 条件 then 値 else 値
n = 7
print(n % 2 == 0 then "偶数" else "奇数")
print(n > 10 then "大" else n > 5 then "中" else "小")

# 連鎖比較（各項は1回だけ評価される）
x = 5
calls = 0
fn five() {
    calls += 1
    return 5
}
print(1 < x < 10, 1 < x < 3, 0 <= x <= 5 < 6, 1 < five() < 10, calls)

# is による型の判定
print(x is number, "a" is string, [1] is list, nil is nil, x is not string, {} is map, print is function)

# 内包表記（後置修飾子と同じ語順）
print([i * i for i in 1..5])
print([c for c in "日本語" if c != "本"])
print({w: len(w) for w in ["apple", "kiwi"]})
print([k + "=" + str(v) for k, v in {a: 1, b: 2}])

# 後置 for / repeat
print("-", i) for i in 1..3 if i != 2
write("★") repeat 3
print()
repeat 2 {
    write("ラップ ")
}
print()
count = 0
repeat 10 {
    count += 1
    break if count == 4
}
print(count)

# match 式
fn season(m) => match m { 3..5 => "春", 6..8 => "夏", 9..11 => "秋", _ => "冬" }
print(season(4), season(7), season(12))
label = match x {
    1 => "一"
    5 => "五"
    _ => "?"
}
print(label, match 99 { 1 => "a" })

# 引数1つの関数は括弧を省略できる
double = fn x => x * 2
print(double(21), [1, 2, 3] -> map(fn v => v + 1))

# 埋め込み式の書式
名前 = "花子"
点 = 95.5
print("[{名前:<6}] {点:>7.2} 点 [{"中央":*^8}] {3.14159:.3}")
print(format('{} は {:>5} 点（{:.1}%）', "太郎", 82, 91.25))

# 値 else throw: 無ければエラーにする
fn need(m) => m.name else throw "name がありません"
print(need({name: "有り"}))
try { need({}) } catch e { print("エラー:", e) }
