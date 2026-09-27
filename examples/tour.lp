# Laping v2 の主な機能をひととおり紹介するサンプル

print("=== 変数と文字列の埋め込み ===")
name = "Laping"
version = 2
print("{name} v{version} へようこそ！")

print("=== リストと関数型スタイル ===")
nums = [5, 3, 8, 1, 9, 2]
evens_squared = nums
    .filter(fn(n) => n % 2 == 0)
    .map(fn(n) => n ** 2)
print("偶数の2乗:", evens_squared)
print("合計:", sum(nums), " 最大:", max(nums), " 並べ替え:", sort(nums))

print("=== マップ ===")
person = {name: "花子", age: 28, hobbies: ["読書", "旅行"]}
person.age += 1
for key, value in person {
    print("  {key}: {value}")
}

print("=== 関数とクロージャ ===")
fn make_multiplier(factor) => fn(x) => x * factor
triple = make_multiplier(3)
print("triple(7) =", triple(7))

fn factorial(n) {
    return 1 if n <= 1
    return n * factorial(n - 1)
}
print("10! =", factorial(10))

print("=== 後置修飾子（Laping 独自のスタイル） ===")
for i in 1..10 {
    continue unless i % 3 == 0
    print("  {i} は3の倍数")
}

print("=== match ===")
fn season(month) {
    match month {
        3..5 => return "春"
        6..8 => return "夏"
        9..11 => return "秋"
        12, 1, 2 => return "冬"
        _ => return "不明"
    }
}
print("4月は{season(4)}、10月は{season(10)}、1月は{season(1)}")

print("=== エラー処理 ===")
fn safe_divide(a, b) {
    try {
        return a / b
    } catch e {
        print("  エラーを捕捉しました: {e}")
        return nil
    }
}
print("10 / 4 =", safe_divide(10, 4))
print("10 / 0 =", safe_divide(10, 0))
