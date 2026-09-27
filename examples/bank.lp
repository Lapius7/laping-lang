# クロージャで状態を隠した「銀行口座」オブジェクトと例外処理

fn Account(owner, balance = 0) {
    let history = []

    fn record(kind, amount) {
        history.push({kind, amount, balance})
    }

    return {
        owner: owner,
        deposit: fn(amount) {
            throw "入金額は正の数にしてください" if amount <= 0
            balance += amount
            record("入金", amount)
        },
        withdraw: fn(amount) {
            throw "出金額は正の数にしてください" if amount <= 0
            throw "残高不足です（残高 {balance} 円、出金 {amount} 円）" if amount > balance
            balance -= amount
            record("出金", amount)
        },
        balance: fn() => balance,
        history: fn() => history,
    }
}

acct = Account("山田", 1000)
acct.deposit(500)
acct.withdraw(300)

for op in [fn() => acct.withdraw(5000), fn() => acct.deposit(-1)] {
    try {
        op()
    } catch e {
        print("失敗:", e)
    }
}

print("{acct.owner} さんの残高: {acct.balance()} 円")
print("取引履歴:")
for i, h in acct.history() {
    print("  {i + 1}. {h.kind} {pad_left(str(h.amount), 5)} 円 → 残高 {h.balance} 円")
}
