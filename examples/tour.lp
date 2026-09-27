# Laping v2.1 の主な機能をひととおり紹介するサンプル

print(bold("=== データは -> で流す ==="))
scores = [72, 95, 58, 81, 66]
[s for s in scores if s >= 60]
    -> map(fn s => s + 5)
    -> sort()
    -> print()
print("平均: {sum(scores) / len(scores):.1} 点")

print(bold("=== 無いかもしれない値は else で ==="))
config = {server: {port: 8080}}
print("port = {config.server.port else 80}, host = {config.server.host else "localhost"}")

print(bold("=== 条件で値を選ぶ then / else ==="))
for n in 1..4 {
    print("{n} は", n % 2 == 0 then "偶数" else "奇数")
}

print(bold("=== 後置修飾子（何をするかを先に書く） ==="))
print("  {i} は 3 の倍数") for i in 1..10 if i % 3 == 0
write("🏁") repeat 5
print()

print(bold("=== record と型ごとのメソッド ==="))
record Point(x, y = 0)
fn Point.dist(p) => sqrt(p.x ** 2 + p.y ** 2)
p = Point(3, 4)
print(p, "原点からの距離:", p.dist())
fn string.shout(s) => upper(s) + "!"
print("laping".shout())

print(bold("=== match は式としても使える ==="))
fn season(month) => match month { 3..5 => "春", 6..8 => "夏", 9..11 => "秋", _ => "冬" }
print("4月は{season(4)}、10月は{season(10)}、1月は{season(1)}")

print(bold("=== 間違いに早く気づける ==="))
MAX_USERS = 100
try { MAX_USERS = 200 } catch e { print("  ", e) }
try { p.z = 1 } catch e { print("  ", e) }
print("  1 < 5 < 10 は", 1 < 5 < 10, "/ [] is list は", [] is list)

print(bold("=== 画面表示 ==="))
table([["東京", 1400], ["大阪", 880]], ["都市", "人口(万人)"])
box("Laping へようこそ", "案内")
