# record・型ごとのメソッド・定数・モジュール

record Point(x, y = 0)
fn Point.dist(p) => sqrt(p.x ** 2 + p.y ** 2)
fn Point.add(p, q) => Point(p.x + q.x, p.y + q.y)

p = Point(3, 4)
print(p, p.dist(), p.add(Point(1)), type(p), p is Point, p is map)
p.x = 10
print(p, p == Point(10, 4), p == Point(10, 5))
print(copy(p), to_json(p))
try {
    p.z = 1
} catch e {
    print("エラー:", e)
}

# 組み込み型にもメソッドを追加できる
fn string.shout(s) => upper(s) + "!"
fn list.second(xs) => xs[1]
fn number.times10(n) => n * 10
print("hello".shout(), [7, 8, 9].second(), 5.times10())

# 定数（全部大文字の名前）は変更できない
MAX_SIZE = 10
try {
    MAX_SIZE = 20
} catch e {
    print("エラー:", e)
}
print(MAX_SIZE)
i = 1
i = 2
print(i)

# import ... as: _ で始まる名前は非公開
import "lib/shapes" as shapes
print(shapes.area(3, 4), shapes.NAME, "_helper" in shapes, keys(shapes) -> sort())
import "lib/shapes" as again
print(again == shapes)
