fn fib(n) {
    return n if n < 2
    return fib(n - 1) + fib(n - 2)
}
print(fib(20))

# クロージャ
fn make_counter() {
    let count = 0
    return fn() {
        count += 1
        return count
    }
}
c1 = make_counter()
c2 = make_counter()
c1()
c1()
print(c1(), c2())

# アロー関数・デフォルト引数・可変長引数
square = fn(x) => x * x
print(square(7))
fn greet(name, greeting = "こんにちは") => "{greeting}、{name}さん"
print(greet("太郎"))
print(greet("花子", "おはよう"))
fn total(...nums) => sum(nums)
print(total(), total(1, 2, 3))
fn head_rest(first, ...rest) => [first, rest]
print(head_rest(1, 2, 3))

# 関数は値
fn apply_twice(f, x) => f(f(x))
print(apply_twice(fn(x) => x + 3, 10))
fn compose(f, g) => fn(x) => f(g(x))
inc_then_double = compose(fn(x) => x * 2, fn(x) => x + 1)
print(inc_then_double(5))

# return だけ書くと nil
fn nothing() {
    return
}
print(nothing())
fn implicit() {
    x = 1
}
print(implicit())

# let はスコープを作る / 代入は外側の変数を書き換える
g = 1
fn modify() {
    g = 2
    let local = 3
    return local
}
modify()
print(g)
fn shadow() {
    let g = 100
    return g
}
print(shadow(), g)

# 相互再帰
fn is_even(n) => n == 0 ? true : is_odd(n - 1)
fn is_odd(n) => n == 0 ? false : is_even(n - 1)
print(is_even(10), is_odd(7))

# デフォルト値は前の引数を参照できる
fn area(w, h = w) => w * h
print(area(3), area(3, 4))

# メソッド構文: x.f(a) は f(x, a)
fn double(x) => x * 2
print(21.double())
