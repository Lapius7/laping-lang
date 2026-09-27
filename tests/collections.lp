xs = [3, 1, 4, 1, 5, 9, 2, 6]
print(xs, len(xs), xs[0], xs[-1])
xs[1] = 10
xs.push(7)
print(xs.pop(), xs)
print(sort(xs), sort(xs).reverse())
print(xs.map(fn(x) => x * 2))
print(xs.filter(fn(x) => x % 2 == 0))
print(xs.reduce(fn(a, b) => a + b), sum(xs), min(xs), max(xs))
print(xs.map(fn(x, i) => "{i}:{x}").join(","))
print(slice(xs, 2, 5), slice(xs, -2))
print([1, 2] + [3], [0] * 3, [1, [2, [3]]].flatten())
print(unique([1, 2, 1, 3, 2]), [1, 2, 3].contains(2), index_of([5, 6], 6))
print(zip([1, 2, 3], ["a", "b"]), enumerate(["x", "y"]))
print(any([0, 0, 1]), all([1, 1, 0]), count([1, 2, 1], 1), find([1, 5, 8], fn(x) => x > 3))
l = [1, 2, 3]
insert(l, 0, 0)
remove(l, -1)
unshift(l, -1)
print(l, shift(l), l)
print(sort(["banana", "fig", "apple"], fn(s) => len(s)))

# マップ（挿入順を保持）
m = {name: "太郎", age: 20, "好きな色": "青"}
print(m, len(m))
print(m.name, m["好きな色"])
m.age += 1
m["city"] = "東京"
print(keys(m), values(m))
print(has(m, "age"), "city" in m, get(m, "zzz", "なし"))
delete(m, "city")
print(m.items())
for k, v in m {
    print("{k} = {v}")
}
nested = {user: {tags: ["a", "b"]}}
nested.user.tags.push("c")
print(nested)
print(merge({a: 1, b: 2}, {b: 3, c: 4}))
name = "x"
print({name, n: 1})
print({1: "one", true: "yes", nil: "nil"})

# 構造的な等価性
print([1, [2, 3]] == [1, [2, 3]], {a: 1, b: 2} == {b: 2, a: 1}, [1] == [2])

# 範囲
r = 1..5
print(r, len(r), list(r), list(1...5), r[2])
print(list(range(5)), list(range(10, 0, -3)), 3 in 1..5, 5 in 1...5)

# コピー
a = [1, 2]
b = a
c = copy(a)
b.push(3)
print(a, c)

# 分割代入と入れ替え
p, q = 1, 2
p, q = q, p
print(p, q)
first, second = ["F", "S"]
print(first, second)
