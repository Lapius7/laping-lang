# Laping 言語仕様（AI 向け）

このファイルは、AI（大規模言語モデル）が **Laping**（拡張子 `.lp`）のコードを正しく書くための仕様書です。
人間向けの説明は [README.md](../README.md) にあります。この文書は Laping v2.1 に対応しています。

> **AI へ**: Laping は Python・JavaScript・Ruby に似た見た目をしていますが、**別の言語です**。
> 他の言語の書き方を推測で使わず、この文書に書かれている構文と関数だけを使ってください。
> 特に「[よくある間違い](#よくある間違い)」の表は、コードを書く前に必ず確認してください。

## 目次

1. [基本](#基本)
2. [よくある間違い](#よくある間違い)
3. [構文の早見表](#構文の早見表)
4. [組み込み関数](#組み込み関数)
5. [書き方の手本](#書き方の手本)
6. [出力前の確認](#出力前の確認)

## 基本

- 実行: `laping ファイル.lp`（引数は `args` リストに入る）。構文だけ確認: `laping check ファイル.lp`。テスト: `laping test`
- 文は**改行**で区切る。`;` は1行に複数の文を書くときだけ使う。行末の `;` は不要
- ブロックは必ず `{ }`。`if` や `while` の条件に括弧もコロンも要らない（`if x > 0 { ... }`）
- コメントは `#`（行末まで）と `#[ ... ]#`（複数行）。`//` はコメントではなく切り捨て除算
- 値の型: `number`（整数も小数も double）・`string`・`bool`（`true` / `false`）・`nil`・`list`・`map`・`range`・`function`、および `record` で宣言した型
- 偽になる値: `false` `nil` `0` `""` `[]` `{}`。それ以外は真
- **暗黙の型変換はしない**。`"a" + 1` はエラー。文字列に数値を入れるときは埋め込み `"値は{x}"` か `str(x)` を使う
- 識別子に日本語を使える（`名前 = "太郎"`）。文字列の長さ・添字は**文字単位**（`len("日本") == 2`）

## よくある間違い

左の書き方は Laping では**エラーになるか、意図と違う動きをします**。右の書き方を使ってください。

| 他の言語の癖（使わない） | Laping での正しい書き方 |
|---|---|
| `def f(x):` / `function f(x) {}` | `fn f(x) { ... }` または `fn f(x) => 式` |
| `lambda x: x * 2` / `x => x * 2` / `(x) => x * 2` | `fn x => x * 2` / `fn(a, b) => a + b` |
| `if x > 0:` / `if (x > 0) {` | `if x > 0 { ... }`（括弧もコロンも不要。`{` は同じ行に書く） |
| `else if` / `elif` のどちらか迷う | どちらも使える: `} elif x { ... }` / `} else if x { ... }` |
| `for (i = 0; i < n; i++)` | `for i in 0...n { }`（`...` は終端を含まない。`..` は終端を含む） |
| `x++` / `x--` | `x += 1` / `x -= 1` |
| `None` / `null` / `undefined` | `nil` |
| `True` / `False` | `true` / `false` |
| `&&` `\|\|` `!` だけを使う | `and` `or` `not` を推奨（`&&` `\|\|` `!` も使える） |
| `a if cond else b` / `cond ? a : b` | `cond then a else b`（`?:` も使えるが `then`/`else` を推奨） |
| `x ?? default` / `x \|\| default` | `x else default`（`or` は `0` や `""` も置き換えるので既定値には使わない） |
| `m.get("k", 0)` / `m?.k` | `m.k else 0` / `m["k"] else 0`（キーが無いと `m.k` 単独はエラー） |
| `xs.append(x)` / `xs.push_back(x)` | `xs.push(x)` |
| `xs.length` / `len(xs)` の混同 | `len(xs)` または `xs.len()`（プロパティ `xs.length` は無い） |
| `"a" + 1` / `"合計: " + n` | `"合計: {n}"`（埋め込み）または `"合計: " + str(n)` |
| `f"{x}"` / `` `${x}` `` / `"#{x}"` | `"{x}"`（普通の `"..."` の中で `{式}` が埋め込まれる） |
| 文字列の中に `{` を書く | `"\{"` とエスケープするか、埋め込みをしない `'...'` を使う |
| `f"{x:.2f}"` | `"{x:.2}"`（書式は `[埋め文字][<>^][幅][.桁数]`。`f` は付けない） |
| `class Point:` / `class Point {}` | `record Point(x, y = 0)` と `fn Point.dist(p) => ...` |
| `self.x` | メソッドの第1引数を使う: `fn Point.dist(p) => sqrt(p.x ** 2 + p.y ** 2)` |
| `fn f() { x * 2 }`（最後の式が返ると思う） | `fn f() { return x * 2 }` または `fn f() => x * 2`（ブロックは暗黙に値を返さず `nil`） |
| 関数の中で `x = 1` をローカル変数のつもりで書く | `let x = 1`（`x = 1` は外側に同名の変数があればそれを書き換える） |
| 定数を `MAX = 1` → 後で `MAX = 2` | 全部大文字（2文字以上）の名前は定数で、再代入はエラー。変える値は小文字の名前にする |
| `import util` / `from x import y` / `require("x")` | `import "util" as util`（パスは文字列。`.lp` は省略可） |
| `print(a, b, sep="")` / `console.log` | `print(a, b)`（スペース区切り）/ 改行なしは `write(...)` |
| `switch` / `case` | `match 値 { 1, 2 => ...  _ => ... }` |
| `try: ... except E as e:` | `try { ... } catch e { ... } finally { ... }` |
| `raise` / `throw new Error("x")` | `throw "x"`（どんな値でも投げられる） |
| `assert x == 1` | `expect x == 1`（失敗時に左辺と右辺を表示）または `assert(条件, メッセージ)` |
| 文字列を `s[0] = "x"` で書き換える | 文字列は変更できない。`replace()` や `slice()` で新しい文字列を作る |
| `x.toUpperCase()` / `x.upper` | `x.upper()`（すべて関数呼び出し。`upper(x)` と同じ） |
| `str.split(",")` の結果を `len()` 以外で数える | `len(xs)` / `xs.len()` / `count(xs, 値)` |
| 変数名に `repeat` `is` `then` `match` などを使う | 予約語は変数名に使えない（下の一覧） |

予約語: `if` `elif` `else` `unless` `while` `until` `for` `in` `loop` `repeat` `break` `continue` `return` `fn` `let` `true` `false` `nil` `and` `or` `not` `is` `then` `match` `try` `catch` `finally` `throw` `import`

文の先頭の `record` `test` `expect`、`import` の後の `as` は、その位置でだけ特別な意味を持つ。

## 構文の早見表

### 変数・代入

```laping
x = 10                      # 変数は宣言不要
let y = 20                  # 現在のスコープに新しく作る（関数の中ではこちらを推奨）
a, b = 1, 2                 # 複数同時の代入
a, b = b, a                 # 入れ替え
first, second = [1, 2]      # リストの分割代入
x += 5                      # += -= *= /= %=
MAX_SIZE = 100              # 全部大文字は定数（再代入するとエラー）
```

### 演算子

```laping
print(7 / 2, 7 // 2, 7 % 3, 2 ** 10)   # 3.5 3 1 1024
print(1 < 5 < 10)                      # 連鎖比較: true
print("ab" * 3, [0] * 3)               # ababab [0, 0, 0]
print(3 in [1, 2, 3], "k" in {k: 1}, "ell" in "hello", 5 in 1..10)
print(5 is number, "a" is not list)    # 型の判定
print(nil or "既定", 0 and 1)          # and / or は値を返す
```

### 文字列

```laping
name = "世界"
score = 95.456
print("こんにちは、{name}！")             # 埋め込み
print("点数: {score:.1}、右寄せ: [{42:>5}]")   # 書式: 点数: 95.5、右寄せ: [   42]
print('埋め込まない {name}')              # シングルクオートはそのまま
print("改行\nタブ\t波かっこ\{\}")
print(len("日本語"), "日本語"[0], "abc"[-1])  # 3 日 c
```

### リスト・マップ・範囲

```laping
xs = [3, 1, 2]
xs.push(4)
print(xs[0], xs[-1], len(xs), sort(xs), xs.contains(2))
m = {name: "太郎", age: 20}     # 識別子のキーは文字列キーになる
m.age += 1
m["city"] = "東京"
print(m.name, m.email else "未登録", keys(m))
for k, v in m {
    print("{k} = {v}")
}
print(list(1..5), list(0...5), list(range(0, 10, 3)))
```

### 制御構文

```laping
n = 7
if n > 10 {
    print("大")
} elif n > 5 {
    print("中")
} else {
    print("小")
}
unless n == 0 {
    print("0 ではない")
}
i = 0
while i < 3 {
    i += 1
}
repeat 2 {
    print("2回")
}
for i in 1..3 {
    continue if i == 2
    print(i)
}
for i, x in ["a", "b"] {
    print(i, x)
}
loop {
    break
}
```

### 後置修飾子（単文の末尾・同じ行にだけ書ける）

```laping
xs = [1, -2, 3]
print("正") if xs[0] > 0
print(x) for x in xs if x > 0
write("★") repeat 3
print()
```

### 式としての選択・既定値・パイプライン

```laping
n = 4
kind = n % 2 == 0 then "偶数" else "奇数"
config = {server: {port: 8080}}
host = config.server.host else "localhost"   # 途中のキーが無くてもエラーにならない
total = [3, 1, 4, 1, 5] -> filter(fn x => x > 1) -> map(fn x => x * 10) -> sum()
print(kind, host, total)                      # 偶数 localhost 120
```

`x -> f(a, b)` は `f(x, a, b)` と同じ。`x -> f` は `f(x)`。

### 内包表記

```laping
squares = [i * i for i in 1..5]
evens = [x for x in [1, 2, 3, 4] if x % 2 == 0]
lengths = {w: len(w) for w in ["apple", "kiwi"]}
print(squares, evens, lengths)
```

### 関数

```laping
fn add(a, b) {
    return a + b                # ブロックでは return が必要
}
fn square(x) => x * x           # 式1つなら =>（値がそのまま返る）
fn greet(name, greeting = "こんにちは") => "{greeting}、{name}"
fn total(...nums) => sum(nums)  # 可変長引数
double = fn x => x * 2          # 無名関数（引数1つなら括弧を省略できる）
pair = fn(a, b) => [a, b]
fn make_counter() {
    let count = 0               # クロージャで状態を持つ
    return fn() {
        count += 1
        return count
    }
}
c = make_counter()
c()
print(add(1, 2), square(4), greet("花子"), total(1, 2, 3), double(5), pair(1, 2), c())
```

`x.f(a)` は、①`fn 型.f` で追加したメソッド ②マップのキー `f` に入った関数 ③`f(x, a)` の順に探して呼ぶ。組み込み関数もすべて `x.f(...)` の形で呼べる（`"a b".split()`、`xs.sort()`）。

### match

```laping
fn describe(x) {
    match x {
        0 => return "ゼロ"
        1, 2, 3 => return "小さい"
        4..9 => return "一桁"
        _ if x < 0 => return "負"
        _ => return "その他"
    }
}
season = match 4 { 3..5 => "春", 6..8 => "夏", _ => "その他" }   # 式としても使える
print(describe(2), describe(-1), season)
```

### record と型ごとのメソッド

```laping
record Point(x, y = 0)
fn Point.dist(p) => sqrt(p.x ** 2 + p.y ** 2)
fn Point.add(p, q) => Point(p.x + q.x, p.y + q.y)
p = Point(3, 4)
p.x = 6                          # 宣言したフィールドは変更できる（p.z = 1 はエラー）
print(p, p.dist(), p.add(Point(1)), type(p), p is Point)

fn string.shout(s) => upper(s) + "!"   # 組み込み型にもメソッドを追加できる
print("hello".shout())
```

### エラー処理

```laping
fn safe_div(a, b) {
    try {
        return a / b
    } catch e {                  # e にはエラーメッセージ（文字列）か throw した値が入る
        print("エラー:", e)
        return nil
    } finally {
        print("後片付け")
    }
}
print(safe_div(1, 0))
fn need(m) => m.name else throw "name がありません"   # 無ければエラーにする
```

### モジュールとテスト

```text
# lib/shapes.lp
fn area(w, h) => w * h
fn _helper() => 1          # _ で始まる名前は import ... as で公開されない

# main.lp
import "lib/shapes" as shapes
print(shapes.area(3, 4))

# main_test.lp（laping test で実行。通常の実行では test ブロックは飛ばされる）
test "面積を計算できる" {
    expect shapes.area(3, 4) == 12
}
```

## 組み込み関数

以下がすべてです（ソースの関数表から生成）。ここに無い関数（`append` `length` `toString` `parseInt` など）は存在しません。どの関数も `f(x, a)`・`x.f(a)`・`x -> f(a)` のどの形でも呼べます。定数として `PI` `E` `INF`、コマンドライン引数として `args` があります。

<!-- builtins:start -->
### 入出力

| 使い方 | 説明 |
|---|---|
| `print(値...)` | 値をスペース区切りで出力して改行する |
| `write(値...)` | 改行せずに出力する |
| `input(プロンプト?)` | 1行読み込む。入力が終わっていれば nil |
| `read_file(パス)` | ファイルの中身を文字列で返す |
| `write_file(パス, 値)` | ファイルに書き込む（上書き） |
| `append_file(パス, 値)` | ファイルの末尾に追記する |
| `file_exists(パス)` | ファイルが存在するか |

### 画面表示

| 使い方 | 説明 |
|---|---|
| `color(値, 色名)` | 文字に色を付ける（red green yellow blue magenta cyan white gray / 赤 緑 青 など） |
| `bold(値)` | 太字にする |
| `dim(値)` | 薄い文字にする |
| `underline(値)` | 下線を付ける |
| `table(行のリスト, 見出し?)` | 罫線付きの表を表示する（行はリストかマップ） |
| `box(文字列, タイトル?)` | 文字列を枠で囲んで表示する |
| `progress(現在, 全体, 幅?)` | 進捗バーの文字列を返す（例: [████░░░░]  50%） |
| `pp(値)` | 入れ子のリストやマップを見やすく整形して表示する |
| `clear_screen()` | 画面を消す |
| `confirm(質問)` | y/N で確認し、はいなら true を返す |
| `choose(質問, 選択肢のリスト)` | 番号付きの選択肢から1つ選ばせ、選ばれた値を返す |
| `width(値)` | 端末上の表示幅（全角は2） |

### 型・変換

| 使い方 | 説明 |
|---|---|
| `type(値)` | 型名（number string bool nil list map range function、record なら型名） |
| `str(値)` | 文字列に変換する（print と同じ表示） |
| `repr(値)` | 文字列なら引用符付きの表現を返す |
| `num(値)` | 数値に変換する（"3.5" や "0xFF" も可） |
| `int(値)` | 小数点以下を切り捨てた整数にする |
| `bool(値)` | 真偽値に変換する |
| `is_num(値)` | 数値、または数値に変換できる文字列か |
| `list(値?)` | 範囲・文字列・マップ(キー)からリストを作る |
| `range(終わり) / range(始め, 終わり, 増分?)` | 終わりを含まない範囲 |
| `len(値)` | 長さ（文字列は文字数） |
| `copy(値)` | リスト・マップの浅いコピー |
| `to_json(値, インデント?)` | JSON 文字列に変換する |
| `from_json(文字列)` | JSON を読んで値にする |

### リスト

| 使い方 | 説明 |
|---|---|
| `push(リスト, 値...)` | 末尾に追加する |
| `pop(リスト, 位置?)` | 末尾（または指定位置）を取り出す |
| `shift(リスト)` | 先頭を取り出す |
| `unshift(リスト, 値...)` | 先頭に追加する |
| `insert(リスト, 位置, 値)` | 指定位置に挿入する |
| `remove(リスト, 位置)` | 指定位置を削除して返す |
| `clear(リスト|マップ)` | 空にする |
| `slice(値, 始め, 終わり?)` | 部分リスト・部分文字列（負の値は後ろから） |
| `index_of(値, 探す値)` | 見つかった位置。なければ -1 |
| `contains(値, 探す値)` | 含まれているか（x in xs と同じ） |
| `reverse(値)` | 逆順にした新しいリスト・文字列 |
| `sort(リスト, キー関数?)` | 並べ替えた新しいリスト（安定ソート） |
| `map(リスト, 関数)` | 各要素に関数を適用したリスト |
| `filter(リスト, 関数)` | 条件を満たす要素だけのリスト |
| `each(リスト, 関数)` | 各要素に関数を実行する |
| `reduce(リスト, 関数, 初期値?)` | 畳み込み |
| `find(リスト, 関数)` | 条件を満たす最初の要素（なければ nil） |
| `any(リスト, 関数?)` | いずれかが条件を満たすか |
| `all(リスト, 関数?)` | すべてが条件を満たすか |
| `count(リスト, 値|関数)` | 等しい、または条件を満たす要素の数 |
| `sum(リスト)` | 合計 |
| `min(リスト) / min(値...)` | 最小値 |
| `max(リスト) / max(値...)` | 最大値 |
| `min_by(リスト, 関数)` | 関数の値が最小の要素 |
| `max_by(リスト, 関数)` | 関数の値が最大の要素 |
| `join(リスト, 区切り?)` | 要素を文字列にしてつなげる |
| `zip(a, b)` | [[a0, b0], [a1, b1], ...] |
| `enumerate(リスト)` | [[0, x0], [1, x1], ...] |
| `flatten(リスト)` | 1段平らにする |
| `unique(リスト)` | 重複を除く |
| `first(リスト)` | 最初の要素（空なら nil） |
| `last(リスト)` | 最後の要素（空なら nil） |
| `take(リスト, n)` | 先頭から n 個 |
| `drop(リスト, n)` | 先頭の n 個を除いた残り |
| `chunk(リスト, n)` | n 個ずつに区切る |
| `group_by(リスト, 関数)` | 関数の値ごとにまとめたマップ |
| `partition(リスト, 関数)` | [条件を満たすもの, 満たさないもの] |
| `tally(リスト)` | 値ごとの出現回数のマップ |
| `is_empty(値)` | 空か（nil も空とみなす） |

### 文字列

| 使い方 | 説明 |
|---|---|
| `upper(文字列)` | 大文字にする |
| `lower(文字列)` | 小文字にする |
| `capitalize(文字列)` | 先頭を大文字にする |
| `trim(文字列)` | 前後の空白（全角スペースを含む）を除く |
| `split(文字列, 区切り?)` | 分割する。省略すると空白で分割 |
| `lines(文字列)` | 行ごとに分割する |
| `replace(文字列, 古い, 新しい)` | すべて置換する |
| `starts_with(文字列, 先頭)` | 前方一致 |
| `ends_with(文字列, 末尾)` | 後方一致 |
| `chars(文字列)` | 1文字ずつのリスト |
| `ord(文字)` | Unicode コードポイント |
| `chr(番号)` | コードポイントから文字を作る |
| `pad_left(値, 幅, 埋め文字?)` | 左を埋めて幅を揃える |
| `pad_right(値, 幅, 埋め文字?)` | 右を埋めて幅を揃える |
| `center(値, 幅, 埋め文字?)` | 中央に揃える（全角は幅2で数える） |
| `fixed(数値, 桁数)` | 小数点以下の桁数を固定した文字列 |
| `format(書式, 値...)` | {} に値を埋め込む。{:>8} で右寄せ、{:.2} で小数2桁 |

### マップ

| 使い方 | 説明 |
|---|---|
| `keys(マップ)` | キーのリスト |
| `values(マップ)` | 値のリスト |
| `items(マップ)` | [キー, 値] のリスト |
| `has(マップ, キー)` | キーがあるか |
| `get(マップ|リスト, キー, 既定値?)` | 値を取得。なければ既定値 |
| `delete(マップ, キー)` | キーを削除する |
| `merge(マップ...)` | 結合した新しいマップ（後の値が優先） |

### 数学

| 使い方 | 説明 |
|---|---|
| `abs(x)` | 絶対値 |
| `floor(x)` | 切り捨て |
| `ceil(x)` | 切り上げ |
| `round(x, 桁数?)` | 四捨五入 |
| `sqrt(x)` | 平方根 |
| `pow(x, y)` | べき乗（x ** y と同じ） |
| `exp(x)` | 指数関数 |
| `log(x, 底?)` | 対数 |
| `sin(x)` | 正弦 |
| `cos(x)` | 余弦 |
| `tan(x)` | 正接 |
| `asin(x)` | 逆正弦 |
| `acos(x)` | 逆余弦 |
| `atan(x)` | 逆正接 |
| `atan2(y, x)` | 2引数の逆正接 |

### 乱数

| 使い方 | 説明 |
|---|---|
| `random()` | 0以上1未満の乱数 |
| `rand_int(a, b)` | a以上b以下の整数の乱数 |
| `choice(リスト)` | ランダムに1つ選ぶ |
| `shuffle(リスト)` | シャッフルした新しいリスト |
| `seed(n)` | 乱数の種を設定する |

### システム

| 使い方 | 説明 |
|---|---|
| `time()` | 現在時刻（UNIX 時間、秒） |
| `date(書式?, 時刻?)` | 日時を文字列にする（既定は "%Y-%m-%d %H:%M:%S"） |
| `clock()` | CPU 時間（秒） |
| `sleep(秒)` | 指定秒数待つ |
| `env(名前, 既定値?)` | 環境変数を読む |
| `exit(コード?)` | プログラムを終了する |
| `assert(条件, メッセージ?)` | 条件が偽ならエラーにする |
| `help(関数?)` | 組み込み関数の説明を表示する |
<!-- builtins:end -->

## 書き方の手本

Laping らしい書き方の例です。データは `->` で流し、無いかもしれない値は `else`、条件は後置で書きます。

```laping
record Sale(shop, price, qty)
fn Sale.total(s) => s.price * s.qty

SALES = [
    Sale("東京", 120, 30),
    Sale("大阪", 110, 45),
    Sale("東京", 400, 8),
]

fn summary(sales) {
    let by_shop = sales -> group_by(fn s => s.shop)
    return [[shop, list -> map(fn s => s.total()) -> sum()] for shop, list in by_shop]
}

rows = summary(SALES) -> sort(fn r => -r[1])
table(rows, ["店舗", "売上"])
best = SALES -> max_by(fn s => s.total())
print("最大の取引: {best.shop} {best.total():>6} 円")
print("警告: {s.shop} の数量が少ない") for s in SALES if s.qty < 10

test "売上の合計" {
    expect Sale("東京", 100, 3).total() == 300
}
```

## 出力前の確認

コードを出力する前に、次を確認してください。

1. 関数は `fn`、無名関数は `fn x => ...`、クラスではなく `record` を使っている
2. `if` / `for` / `while` の条件に括弧やコロンを付けていない。`{` は同じ行にある
3. ブロック形式の関数で、値を返すところに `return` を書いている
4. 文字列と数値を `+` で連結していない（埋め込み `"{x}"` を使う）
5. 存在しないかもしれないキーや添字には `else 既定値` を付けている
6. 関数内のローカル変数には `let` を使っている
7. 上の一覧に無い組み込み関数や、`.append` `.length` などのメソッドを使っていない
8. 予約語（`repeat` `is` `then` など）を変数名にしていない
9. 実行できる環境なら `laping check ファイル.lp` で構文を、`laping ファイル.lp` で動作を確認する
