import "lib/mathutil"
import "lib/mathutil.lp"
print(square(4), cube(3), MATH_UTIL_LOADED)

# マップと関数でモジュール風のオブジェクトを作る
fn Stack() {
    let items = []
    return {
        push: fn(x) => items.push(x),
        pop: fn() => items.pop(),
        size: fn() => len(items),
    }
}
s = Stack()
s.push(1)
s.push(2)
print(s.size(), s.pop(), s.size())

# 標準ライブラリ
print(abs(-3), floor(2.7), ceil(2.1), round(2.5), round(3.14159, 3), sqrt(16), pow(2, 8))
print(fixed(PI, 5), fixed(E, 3), log(100, 10), INF > 1e308)
print(7 // 2, -7 // 2, -7 % 3, 2 ** 3 ** 2, -2 ** 2, 0.1 + 0.2)
print(type(1), type(true), type(nil), type(print), type(square), type(1..2))
seed(42)
r = random()
print(r >= 0 and r < 1, rand_int(1, 1), choice([7]), len(shuffle([1, 2, 3])))
print(bool(0), bool("x"), bool([]), bool({}), str(nil), str([1, "a"]))
print(1_000_000, 0xFF, 0b1010, 1.5e3)
x = 1
x *= 5
x -= 1
x /= 2
x %= 3
print(x)
