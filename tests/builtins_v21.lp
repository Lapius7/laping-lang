# v2.1 で追加した組み込み関数（出力は端末ではないので色は付かない）
table([["りんご", 120, 3], ["バナナ", 98, 12]], ["品名", "価格", "個数"])
record Item(name, price)
table([Item("ペン", 150), Item("ノート", 300)])
box("Laping へようこそ\nデータは -> で流す", "案内")
pp({user: {name: "太郎", tags: ["a", "b"]}, scores: [1, 2]})
print(progress(3, 10, 10), progress(10, 10, 4))
print(color("赤", "red"), bold("太"), width("日本a"), center("日本", 8, "-"))

data = from_json('{"name": "花子", "tags": ["a", "b"], "age": 20, "ok": true, "x": null, "u": "あ"}')
print(data)
print(to_json({a: [1, 2, {b: nil}], s: "改\n行"}))
print(to_json([1, {k: "v"}], 2))
try { from_json("[1, 2") } catch e { print(e) }

print(group_by(1..6, fn x => x % 2 == 0 then "偶" else "奇"))
print(tally(split("a b a c a b")), chunk(1..7, 3), partition(1..6, fn x => x > 3))
print(take([1, 2, 3], 2), drop([1, 2, 3], 2), first([]), last("abc"))
print(max_by(["aa", "b", "cccc"], fn s => len(s)), min_by([3, -7, 5], abs))
print(is_empty([]), is_empty(nil), is_empty("x"), lines("a\nb\r\nc"), capitalize("laping"))
print(date("%Y", 0) in ["1970", "1969"], env("LAPING_NO_SUCH_VAR", "既定"))
print(type(help))
