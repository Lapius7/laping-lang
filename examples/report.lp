# 売上データの集計レポート
#   -> パイプライン・group_by・内包表記・書式付きの埋め込み・table

record Sale(shop, item, price, qty)

sales = [
    Sale("東京", "りんご", 120, 30),
    Sale("大阪", "りんご", 110, 45),
    Sale("東京", "みかん", 80, 60),
    Sale("福岡", "ぶどう", 400, 12),
    Sale("大阪", "ぶどう", 380, 20),
    Sale("東京", "ぶどう", 420, 8),
]

fn Sale.total(s) => s.price * s.qty

by_shop = sales -> group_by(fn s => s.shop)
rows = [[shop, len(list), list -> map(fn s => s.total()) -> sum()] for shop, list in by_shop]
rows = rows -> sort(fn r => -r[2])

print(bold("■ 店舗別の売上"))
table(rows, ["店舗", "件数", "売上(円)"])

best = sales -> max_by(fn s => s.total())
total = sales -> map(fn s => s.total()) -> sum()
print("総売上: {total:>8} 円")
print("最大の取引: {best.shop} の {best.item}（{best.total()} 円、全体の {best.total() / total * 100:.1}%）")

print()
print(bold("■ 商品ごとの構成比"))
for item, list in sales -> group_by(fn s => s.item) {
    amount = list -> map(fn s => s.total()) -> sum()
    print("{item:<6} {progress(amount, total, 20)}")
}
