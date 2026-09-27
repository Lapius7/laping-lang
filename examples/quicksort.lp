# クイックソート（再帰・可変長引数・リスト操作）

fn quicksort(xs) {
    return xs if len(xs) <= 1
    let pivot = xs[0]
    let rest = slice(xs, 1)
    let smaller = rest.filter(fn(x) => x < pivot)
    let larger = rest.filter(fn(x) => x >= pivot)
    return quicksort(smaller) + [pivot] + quicksort(larger)
}

data = []
seed(2024)
for _ in 1..15 {
    data.push(rand_int(1, 100))
}
print("元のデータ:  ", data)
print("ソート結果:  ", quicksort(data))
print("組み込みsort:", sort(data))
print("一致:", quicksort(data) == sort(data))

words = ["ぶどう", "りんご", "いちご", "かき", "バナナ"]
print(quicksort(words))
