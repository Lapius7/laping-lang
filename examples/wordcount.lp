# 単語の出現回数を数える（マップ・文字列処理・sort のキー関数）

text = "the quick brown fox jumps over the lazy dog the fox"

counts = {}
for word in split(text) {
    counts[word] = get(counts, word, 0) + 1
}

ranking = sort(items(counts), fn(pair) => -pair[1])
print("出現回数ランキング:")
for pair in slice(ranking, 0, 3) {
    word, n = pair
    print("  {pad_right(word, 6)} {"*" * n} ({n})")
}
print("異なる単語の数:", len(counts))
