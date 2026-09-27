# elif / else if
fn grade(score) {
    if score >= 90 {
        return "A"
    } elif score >= 70 {
        return "B"
    } else if score >= 50 {
        return "C"
    }
    else {
        return "D"
    }
}
print(grade(95), grade(75), grade(55), grade(10))

# unless / until / loop
unless false {
    print("unless ブロック")
} else {
    print("ここには来ない")
}
n = 0
until n >= 3 {
    n += 1
}
print(n)
loop {
    n -= 1
    break if n == 0
}
print(n)

# for
for i in 1..3 {
    write("{i} ")
}
print()
for i in 0...10 {
    continue if i % 3 != 0
    break if i > 6
    write("{i} ")
}
print()
for i, x in ["a", "b"] {
    print(i, x)
}
for x in range(10, 0, -4) {
    write("{x} ")
}
print()

# 後置 while / until
i = 0
i += 1 while i < 5
print(i)
i -= 2 until i < 0
print(i)

# 入れ子のループで break は内側だけ抜ける
for a in 1..3 {
    for b in 1..3 {
        break if b == 2
        write("{a}{b} ")
    }
}
print()

# match
fn describe(x) {
    match x {
        0 => return "ゼロ"
        1, 2, 3 => return "小さい"
        4..9 => return "一桁"
        "hello" => return "あいさつ"
        _ if type(x) == "list" => return "リスト"
        _ => return "その他"
    }
}
print(describe(0), describe(2), describe(7), describe("hello"), describe([1]), describe(100))
match 5 {
    5 => {
        print("ブロックも")
        print("書ける")
    }
}

# 三項演算子・論理演算子
x = 5
print(x > 3 ? "大" : "小", x > 10 ? "大" : x > 3 ? "中" : "小")
print(nil or "default", 0 or "", 1 and 2, false and crash(), not true, !false)
print(true && false, true || false)

# 1行に複数の文
a = 1; b = 2; print(a + b)

# 式は複数行に続けられる
total = 1 +
    2 +
    3
print(total)
result = [1, 2, 3, 4]
    .filter(fn(x) => x % 2 == 0)
    .map(fn(x) => x * 10)
print(result)
data = {
    name: "複数行",
    items: [
        1,
        2,
    ],
}
print(data)
