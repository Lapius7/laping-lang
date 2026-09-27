# Laping (`.lp`)

**Lap + Lang = Laping**

**読んだ順に理解できて、間違いにすぐ気づける**ことを目指した、C言語製のインタプリタ型プログラミング言語です。
Lexer → Parser → Tree-walking Interpreter という構成で、外部の構文解析ライブラリには依存していません（HTTP通信用に自動更新機能のみ libcurl を使用）。

```laping
record Sale(shop, price, qty)
fn Sale.total(s) => s.price * s.qty

sales = [Sale("東京", 120, 30), Sale("大阪", 110, 45), Sale("東京", 400, 8)]

# データは -> で左から右へ流す
tokyo = sales -> filter(fn s => s.shop == "東京") -> map(fn s => s.total()) -> sum()
print("東京の売上: {tokyo:>8} 円")

# 無いかもしれない値は else で既定値に
config = {server: {host: "example.com"}}
port = config.server.port else 8080

# 何をするかを先に、条件は後ろに（後置修飾子）
print("{s.shop}: {s.total()}") for s in sales if s.total() > 3000

# テストは同じ言語で、すぐ横に書ける（laping test で実行）
test "売上を計算できる" {
    expect Sale("東京", 100, 3).total() == 300
}
```

## 特徴

- **読んだ順に理解できる**: `->` パイプライン、後置修飾子（`stmt if cond` / `for` / `repeat`）、`then` / `else` による値の選択
- **記号より言葉**: 既定値は `値 else 既定値`、型の判定は `x is list`、回数の繰り返しは `repeat 3 { }`
- **間違いにすぐ気づける**: `record` の打ち間違いフィールド、不明な型名、定数（全部大文字の名前）の書き換えをエラーにし、「もしかして」の候補を出す
- **管理しやすい**: `import "x" as x` でモジュールごとに名前を分け、`_` で始まる名前は非公開。テストは `test` ブロックで同じファイルにも書ける
- **見やすい CLI**: 該当行と `^` を示す色付きのエラー表示、コマンド付きの対話モード、`laping test` / `check` / `new` / `doc`
- 関数・クロージャ・`fn x => x * 2`・デフォルト引数・可変長引数、リスト・マップ・範囲・内包表記、`match`（式としても使える）、`try` / `catch` / `finally`
- 文字列への式の埋め込みと書式（`"合計 {a + b:>8} 円"`）、UTF-8 対応（日本語の文字数・添字・変数名、全角文字の表示幅）
- 表・枠・進捗バー・色などの画面表示、JSON、データ集計を含む 130 以上の組み込み関数（`laping doc` で一覧）
- 暗黙の型変換なし、マーク&スイープ GC、単一バイナリ、`laping update` で自動更新

## 設計方針 — なぜこの書き方なのか

Laping の構文は、他の言語の記号をそのまま借りるのではなく、それぞれに「こう書くと楽になる」「こう書くと間違えにくい」という理由があるものにしています。

| 書き方 | 意味 | 理由 |
|---|---|---|
| `xs -> filter(f) -> sum()` | 左の値を右の関数の第1引数に渡す | データの流れがそのまま矢印の向きになり、書いた順に読める。日本語キーボードで打ちにくい `\|` を使わない |
| `config.port else 8080` | 値が無いときの既定値 | 記号ではなく言葉で書ける。`or` と違い `0` や `""` を置き換えない。途中のキーが無い・nil でもエラーにならない（変数名の打ち間違いはエラーのまま） |
| `x > 0 then "正" else "負"` | 条件で値を選ぶ | 英文のように読める。`?:` を覚えなくていい |
| `print(x) for x in xs if x > 0` | 後置の繰り返し | 既存の後置修飾子（`if` / `unless` / `while`）と同じ「何をするかが先、条件は後」の1ルール |
| `[x * 2 for x in xs if x > 0]` | 内包表記 | 上の後置 `for` と同じ語順なので、覚えることが増えない |
| `repeat 3 { }` / `hi() repeat 3` | 回数だけ繰り返す | 使わないカウンタ変数を書かなくていい |
| `1 < x < 10` | 連鎖比較 | 数学と同じ書き方。各項は1回だけ評価される |
| `record Point(x, y)` | 型の宣言 | 宣言していないフィールドへの代入をエラーにして、打ち間違いにすぐ気づける |
| `fn Point.len(p)` / `fn string.shout(s)` | 型にメソッドを追加 | クラスや `self` なしで、関数と同じ書き方。組み込み型にも追加できる |
| `x is list` | 型の判定 | `type(x) == "lsit"` のような打ち間違いを「不明な型名」エラーで防ぐ |
| `MAX_SIZE = 10` | 定数 | 全部大文字の名前は再代入できない。キーワードを増やさずに「変えてはいけない値」を表せる |
| `import "util" as util` | モジュール | 名前の衝突を防ぐ。`_` で始まる名前は外から見えない |
| `test "名前" { expect a == b }` | テスト | 失敗すると左辺と右辺の値を自動で表示する。通常の実行では飛ばされる |
| `"{点:>5}"` / `"{率:.1}"` | 書式付きの埋め込み | 桁揃えや小数の桁数を、文字列の中でそのまま指定できる |

## インストール

### Releaseから取得（推奨）

[Releases](https://github.com/Lapius7/laping-lang/releases) から OS に対応するファイルをダウンロードしてください。

- Linux: `laping-linux-x86_64`
- Windows（インストーラー、推奨）: `laping-windows-x86_64-setup.exe`
- Windows（zip展開、上級者向け）: `laping-windows-x86_64.zip`（`laping.exe` と `libcurl-x64.dll` が同梱）

Linuxの場合は実行権限を付与してパスの通った場所に置きます。

```sh
chmod +x laping-linux-x86_64
mv laping-linux-x86_64 ~/.local/bin/laping
```

Windowsの場合は `laping-windows-x86_64-setup.exe` を実行すると、一般的なセットアップウィザード（インストール先選択 → インストール中のプログレスバー → 完了画面）でインストールされます。インストール先は自動でPATHに追加されるため、インストール後はコマンドプロンプトやPowerShellからどのフォルダでも `laping` コマンドが使えます。スタートメニュー・デスクトップへのショートカット作成、「アプリと機能」からのアンインストールにも対応しています。

`laping-windows-x86_64.zip` は、インストーラーを使わずにポータブルな形で使いたい場合のみ展開して `laping.exe` と `libcurl-x64.dll` を**同じフォルダ**に置いて使います（DLLが無いと起動できません）。

### ソースからビルド

`libcurl` の開発用ヘッダが必要です（自動更新機能のため）。

```sh
# Ubuntu/Debian
sudo apt-get install libcurl4-openssl-dev

make
make test      # テストを実行
make install   # ~/.local/bin/laping にインストール
```

ソースは `src/` にあります。

| ファイル | 役割 |
|---|---|
| `src/lexer.c` | 字句解析（ソース → トークン列） |
| `src/parser.c` | 構文解析（トークン列 → AST） |
| `src/interp.c` | 評価器（AST を直接たどって実行、スコープ・例外・関数呼び出し） |
| `src/value.c` | 値・文字列・リスト・マップ・GC |
| `src/builtins.c` | 組み込み関数（説明文もここにまとめて書く） |
| `src/ui.c` | 端末表示（色・全角文字の表示幅・エラー表示用のソース保存） |
| `src/main.c` | コマンドライン・対話モード |
| `src/updater.c` | 自動更新 |

## 使い方

```sh
laping main.lp              # main.lp を実行
laping main.lp a b c        # 引数付きで実行（スクリプト内では args で受け取れる）
laping                      # 対話モード (REPL) を起動
laping run main.lp          # 実行（laping main.lp と同じ）
laping check *.lp           # 実行せずに構文だけを確認
laping test                 # *_test.lp の test ブロックを実行（フォルダやファイルも指定できる）
laping new myapp            # プロジェクトのひな形（main.lp・lib.lp・main_test.lp）を作る
laping doc                  # 組み込み関数の一覧
laping doc format           # 関数の説明
laping -e 'print(1 + 2)'    # コードを直接実行
laping update               # GitHub Releasesの最新版を確認し、自動更新
laping version              # バージョン表示
laping help                 # 使い方
```

端末に出力しているときは、エラーや表が色付きで表示されます。`NO_COLOR=1` で色を消し、`LAPING_COLOR=always` で常に色を付けられます。

### 対話モード

入力した式の値が色付きで表示されます。`{` や `(` が閉じていない間や、行末が `->` の間は続きの行を入力できます。

```
laping› xs = [3, 1, 2]
laping› xs -> sort()
=> [1, 2, 3]
laping› fn sq(x) {
   …     return x * x
   … }
laping› sq(12)
=> 144
laping› :time (1..100000) -> sum()
=> 5000050000
  0.004 秒
```

| コマンド | 説明 |
|---|---|
| `:help` | コマンド一覧 |
| `:vars` | 定義した変数と関数の一覧（型と値） |
| `:doc [関数名]` | 組み込み関数の説明 |
| `:load <ファイル>` | ファイルを読み込んで実行する |
| `:time <コード>` | 実行時間を測る |
| `:reset` | 変数をすべて消す |
| `:clear` | 画面を消す |
| `:q` | 終了（`exit()` や Ctrl+D でも終了） |

### テスト

`*_test.lp` というファイルに `test` ブロックを書き、`laping test` で実行します。`expect` が失敗すると、比較の左辺と右辺の値が表示されます。

```laping
# calc_test.lp
fn add(a, b) => a + b

test "足し算ができる" {
    expect add(1, 2) == 3
}
```

```
▶ calc_test.lp
  ✓ 足し算ができる
  ✗ わざと失敗
      expect が失敗しました: 左辺は 4、右辺は 5
      場所: calc_test.lp:6

✗ 2 件中 1 件が失敗しました (0.00 秒)
```

`test` ブロックは `laping test` のときだけ実行され、普通に `laping calc_test.lp` と実行したときは飛ばされます。そのため、関数を定義したファイルに直接テストを書いておくこともできます。

## 自動更新の仕組み

`laping update` を実行すると、以下の流れで動作します。

1. `https://api.github.com/repos/Lapius7/laping-lang/releases/latest` にアクセスし、最新リリースのタグを取得
2. 実行中のバージョン（`laping --version`）と比較
3. ローカルより新しければ、OSに対応する配布物をダウンロード（Linuxは`laping-linux-x86_64`、Windowsは`laping-windows-x86_64.zip`）
4. Windowsの場合は標準搭載の`tar.exe`でzipを展開し、中の`laping.exe`と`libcurl-x64.dll`を取り出す（Windows 10 1803以降が必要）
5. 現在の実行ファイル（とWindowsの場合はDLL）をバックアップしてから新しいものに置き換える

開発者がGitHubに新しいバージョンをタグ付き（`vX.Y.Z`）でプッシュすると、CI（GitHub Actions）がLinux/Windows向けにビルドしてReleaseへ自動添付します。各ユーザーは `laping update` を実行するだけで最新版に追従できます。

## 構文リファレンス

### コメント

```laping
# 行コメント
x = 1  # 行末コメントも可

#[
  ブロックコメント
  複数行にわたって書ける
]#
```

### 文の区切り

文は改行で区切ります。1行に複数の文を書く場合は `;` で区切ります。

```laping
a = 1; b = 2; print(a + b)
```

次の場合は改行しても文が続いているとみなされます。

- `(` `)` や `[` `]` の内側
- 行末が演算子・`,`・`=` などで終わっている
- 次の行が `.` で始まる（メソッドチェーン）
- 行末に `\` を書いた

```laping
total = 1 +
    2 +
    3

result = [1, 2, 3, 4]
    .filter(fn(x) => x % 2 == 0)
    .map(fn(x) => x * 10)
```

### 値の種類

| 型 (`type()`) | 例 | 補足 |
|---|---|---|
| `number` | `10`, `3.14`, `-2`, `1_000_000`, `0xFF`, `0b1010`, `1.5e3` | 内部的には `double`。整数値は整数として表示される |
| `string` | `"hello"`, `'raw'` | 変更不可。UTF-8 |
| `bool` | `true`, `false` | |
| `nil` | `nil` | 「値がない」ことを表す |
| `list` | `[1, "a", [2, 3]]` | 可変長の配列 |
| `map` | `{name: "太郎", "age": 20}` | キーと値の組。挿入順を保持する |
| `range` | `1..5`, `0...10` | 数の範囲 |
| `function` | `fn(x) => x * 2`, `print` | 関数も値として扱える |

**真偽の判定**: `false`・`nil`・`0`・空文字列 `""`・空リスト `[]`・空マップ `{}` は偽、それ以外は真です。

### 変数と代入

```laping
x = 10
name = "Laping"
名前 = "日本語の変数名も使えます"

a, b = 1, 2        # 複数同時に代入
a, b = b, a        # 入れ替え
first, second = ["F", "S"]   # リストを分割して代入

x += 5             # 複合代入: += -= *= /= %=
```

- 代入は `変数名 = 式`。変数は宣言不要（代入した時点で生成される）
- 未定義の変数を参照すると実行時エラーになり、行番号付きで表示される
- `let x = 値` は「現在のスコープに新しい変数を作る」宣言です（下記「スコープ」参照）

### 演算子

優先順位の高い順:

| 演算子 | 意味 |
|---|---|
| `x(...)` `x[i]` `x.name` | 呼び出し・添字・フィールド |
| `**` | べき乗（右結合。`-2 ** 2` は `-4`） |
| `-x` `!x` | 符号反転・否定 |
| `*` `/` `//` `%` | 乗算・除算・切り捨て除算・剰余 |
| `+` `-` | 加算・減算 |
| `..` `...` | 範囲（`..` は終端を含む、`...` は含まない） |
| `==` `!=` `<` `>` `<=` `>=` `in` `not in` `is` `is not` | 比較・所属判定・型の判定（`1 < x < 10` のように連鎖できる） |
| `not` | 否定 |
| `and` `&&` | 論理積（短絡評価） |
| `or` `\|\|` | 論理和（短絡評価） |
| `x -> f(a)` | パイプライン（`f(x, a)` と同じ） |
| `条件 then a else b` / `値 else 既定値` | 値の選択 / 既定値（`条件 ? a : b` も使える） |

演算子の型の規則（暗黙の型変換はしません）:

| 演算子 | 使える組み合わせ |
|---|---|
| `+` | 数値 + 数値、文字列 + 文字列（連結）、リスト + リスト（連結） |
| `*` | 数値 * 数値、文字列 * 整数（繰り返し）、リスト * 整数（繰り返し） |
| `-` `/` `//` `%` `**` | 数値のみ。0 での除算・剰余はエラー。`%` の結果は除数と同じ符号 |
| `<` `>` `<=` `>=` | 数値同士、または文字列同士（辞書順） |
| `==` `!=` | 何でも比較可能。リストとマップは中身で比較。型が違えば常に不一致 |
| `in` | 要素 `in` リスト、キー `in` マップ、部分文字列 `in` 文字列、数値 `in` 範囲 |

`and` / `or` は真偽値ではなく、評価したオペランドの値をそのまま返します。`0` や `""` も置き換えたくない場合は、次の `else` を使ってください。

### パイプライン `->`

`x -> f(a, b)` は `f(x, a, b)` と同じ意味です。データを左から右へ流すように書けるので、処理を書いた順に読めます。右側が関数そのもの（`x -> sqrt`）なら `sqrt(x)` になります。行頭に `->` を書けば、次の行へ続けられます。

```laping
[3, 1, 4, 1, 5, 9]
    -> filter(fn x => x > 2)
    -> map(fn x => x * 10)
    -> sort()
    -> print()                 # [30, 40, 50, 90]
```

### 既定値 `else` と、値を選ぶ `then` / `else`

`値 else 既定値` は、値が `nil` のときに既定値を使います。さらに左側の `a.b.c` や `a[i]` の途中で、nil になった・キーが存在しない・添字が範囲外だった場合も、エラーにせず既定値を使います。変数名の打ち間違い（未定義の変数）は、これまでどおりエラーになります。

```laping
config = {server: {port: 8080}}
port = config.server.port else 80           # 8080
host = config.server.host else "localhost"  # キーが無いので "localhost"
first = args[0] else "引数なし"               # 範囲外なので "引数なし"
count = 0 else 10                           # 0（or と違い 0 や "" は置き換えない）
name = user.name else throw "name がありません"  # 無ければエラーにする
```

`条件 then 値1 else 値2` は、条件が真なら値1、偽なら値2 になります。

```laping
print(n % 2 == 0 then "偶数" else "奇数")
size = n > 100 then "大" else n > 10 then "中" else "小"
```

### 連鎖比較と `is`

`1 < x < 10` は `1 < x and x < 10` と同じ意味です（`x` は1回だけ評価されます）。`is` は値の型を調べます。

```laping
print(0 <= score <= 100)
print(x is number, xs is list, p is Point, v is not nil)
```

使える型名は `nil` `bool` `number` `string` `list` `map` `function` `range` と、`record` で宣言した名前です。それ以外の名前はエラーになるので、打ち間違いにすぐ気づけます（`x is lsit` → 「不明な型名 'lsit'／もしかして 'list' ですか？」）。

### 文字列

```laping
name = "世界"
print("こんにちは、{name}！")          # {式} で埋め込み
print("合計: {1 + 2}、一覧: {[1, 2]}")
print("波かっこそのもの: \{ \}")
print('シングルクオートは {埋め込みしない}')
print("改行\nタブ\t引用符\" 絵文字\u{1F600}")
```

- `"..."` の中では `{式}` がその値の文字列表現に置き換わります
- エスケープ: `\n` `\t` `\r` `\0` `\e` `\\` `\"` `\'` `\{` `\}` `\u{16進数}`
- `'...'` は埋め込みを行わず、エスケープも `\\` と `\'` のみです
- `{式:書式}` で桁揃えや小数の桁数を指定できます（下の表）
- 文字列は改行を含めて複数行に書けます
- 長さ・添字は **文字単位**（UTF-8）で数えます: `len("こんにちは")` は `5`、`"こんにちは"[1]` は `"ん"`
- 負の添字は後ろから数えます: `"abc"[-1]` は `"c"`

埋め込みの書式 `{式:書式}` は `[埋め文字][< > ^][幅][.桁数]` の形です。全角文字は幅2として揃えます。

```laping
名前 = "花子"
点 = 95.5
print("[{名前:<6}] {点:>7.2} 点")   # [花子  ]   95.50 点
print("[{"中央":*^8}]")             # [**中央**]
print("{3.14159:.3}")               # 3.142
```

同じ書式は `format()` でも使えます。`"..."` の中の `{}` は埋め込みになるため、書式は `'...'` で書きます: `format('{} は {:>5} 点', name, score)`

### リスト

```laping
xs = [3, 1, 4]
print(xs[0], xs[-1], len(xs))
xs[0] = 10
xs.push(5)              # 末尾に追加（push(xs, 5) と同じ）
last = xs.pop()         # 末尾を取り出す
print(xs + [9], [0] * 3)
print(slice(xs, 1, 3))  # 部分リスト
```

範囲外の添字はエラーになります。要素を増やすときは `push` / `insert` を使います。

### マップ

```laping
person = {name: "太郎", age: 20, "好きな色": "青"}
print(person.name, person["好きな色"])
person.age += 1
person["city"] = "東京"
print(keys(person), "age" in person)
print(get(person, "email", "未登録"))   # キーがなければ既定値
delete(person, "city")
x = 5
point = {x, y: 10}                       # {x: x, y: 10} の省略形
```

- キーには 文字列・数値・真偽値・`nil` が使えます。`{name: ...}` のように書いた識別子は文字列キーになります
- 存在しないキーを `m.key` や `m["key"]` で読むとエラーになります。無いかもしれないキーは `get(m, key, 既定値)` か `key in m` で確認してください
- `for` などで列挙すると、キーを追加した順に並びます

### 範囲

```laping
1..5          # 1, 2, 3, 4, 5（終端を含む）
0...5         # 0, 1, 2, 3, 4（終端を含まない）
range(10)          # 0〜9
range(10, 0, -2)   # 10, 8, 6, 4, 2（増分を指定）
list(1..3)    # [1, 2, 3]
```

範囲は必要になるまで要素を作らないので、`for i in 1..1000000` でも大きなリストは作られません。

### 出力と入力

```laping
print("a", 1, [2])      # 引数をスペース区切りで出力し、改行する
write("改行なし")        # 改行せずに出力
name = input("名前: ")   # 1行読み込む（入力が終わっていれば nil）
```

### 条件分岐

```laping
if score >= 90 {
    print("A")
} elif score >= 70 {        # else if と書いてもよい
    print("B")
} else {
    print("C")
}

unless logged_in {          # if not と同じ
    print("ログインしてください")
}
```

- 条件式に括弧は不要（`if (cond)` ではなく `if cond`）
- ブロックは必ず `{ }` で囲む
- `else` / `elif` は `}` の次の行に書いてもかまいません

### 繰り返し

```laping
while i < 5 { i += 1 }       # 条件が真の間
until i == 0 { i -= 1 }      # 条件が偽の間
loop {                       # 無限ループ（break で抜ける）
    break if done()
}

for x in [10, 20, 30] { print(x) }        # リスト
for i in 1..10 { print(i) }                # 範囲
for ch in "あいう" { print(ch) }           # 文字列（1文字ずつ）
for key in {a: 1, b: 2} { print(key) }     # マップ（キー）
for i, x in ["a", "b"] { print(i, x) }     # 添字と要素
for key, value in {a: 1} { print(key, value) }  # キーと値
```

`break` で最も内側のループを抜け、`continue` で次の繰り返しに進みます。

### 後置修飾子構文（Laping 独自のスタイル）

**単文**（代入・式・`return`・`break`・`continue`・`throw`）の末尾に `if` / `unless` / `while` / `until` / `for` / `repeat` を置くと、条件付きで、または繰り返して実行できます。「何をするか」を先に書き、条件や繰り返しは後ろに書く、という1つのルールです。

```laping
print("正の数です") if x > 0
print("0以下です") unless x > 0
return 0 if n < 1
continue unless i % 3 == 0
throw "不正な値" if value < 0
i += 1 while i < 10
i -= 1 until i == 0
print(x) for x in xs                  # 繰り返し
print(x) for x in xs if x > 0         # 条件付きの繰り返し
write("★") repeat 3                   # 回数だけ繰り返し
```

- **同じ行に書かれている場合のみ**修飾子として解釈されます。改行を挟んだ場合は別々の文として扱われ、誤って次の行の `if` ブロックを修飾子と誤認識することはありません
- 修飾子の対象にできるのは **単文のみ** です。複数の文をまとめて条件付きにしたい場合はブロック構文を使ってください。これにより「修飾子がどこまでを対象にしているか」が常に1文に固定され、構文上の曖昧さが生まれません

### 内包表記

後置の `for` と同じ語順で、リストやマップを作れます。ループ変数は外に漏れません。

```laping
squares = [i * i for i in 1..5]                  # [1, 4, 9, 16, 25]
evens = [x for x in xs if x % 2 == 0]
lengths = {w: len(w) for w in ["apple", "kiwi"]}  # {"apple": 5, "kiwi": 4}
pairs = ["{k}={v}" for k, v in {a: 1, b: 2}]      # ["a=1", "b=2"]
```

### repeat

回数だけ繰り返します。カウンタ変数が要らないときに使います。`break` / `continue` も使えます。

```laping
repeat 3 {
    print("やあ")
}
```

### match

値によって処理を振り分けます。上から順に調べ、最初に一致した1つだけを実行します。

```laping
match value {
    0 => print("ゼロ")
    1, 2, 3 => print("小さい")           # カンマで複数のパターン
    4..9 => print("一桁")                # 範囲に含まれるか
    "hello" => print("あいさつ")
    _ if value < 0 => print("負の数")    # _ は何にでも一致。if で追加条件
    _ => {
        print("その他")                   # ブロックも書ける
    }
}
```

`match` は式としても使え、一致した枝の値になります（どれにも一致しなければ `nil`）。枝は改行かカンマで区切ります。

```laping
fn season(m) => match m { 3..5 => "春", 6..8 => "夏", 9..11 => "秋", _ => "冬" }
label = match code {
    200 => "OK"
    404 => "見つかりません"
    _ => "エラー"
}
```

### 関数

```laping
fn add(a, b) {
    return a + b
}

fn square(x) => x * x               # 式1つだけなら => で書ける

fn greet(name, greeting = "こんにちは") => "{greeting}、{name}さん"   # デフォルト引数
fn total(...nums) => sum(nums)      # 可変長引数（リストで受け取る）

double = fn(x) => x * 2             # 無名関数
triple = fn x => x * 3              # 引数が1つなら括弧を省略できる
[1, 2, 3] -> map(fn x => x + 1)     # 関数を引数に渡す
```

- `return` の無い関数、または値を書かない `return` は `nil` を返します
- 引数の数が合わないとエラーになります
- 関数は定義された場所の変数を覚えています（クロージャ）

```laping
fn make_counter() {
    let count = 0
    return fn() {
        count += 1
        return count
    }
}
c = make_counter()
c()
print(c())   # => 2
```

### メソッド構文

`x.f(a, b)` と書くと `f(x, a, b)` が呼ばれます。組み込み関数にも自分で定義した関数にも使えるため、処理を左から右へつなげて書けます。

```laping
fn double(x) => x * 2
print(21.double())                       # double(21)
print("  hi  ".trim().upper())           # upper(trim("  hi  "))
words = "b a c".split().sort().join(",") # "a,b,c"
```

ただし `x` がマップで、`f` という名前のキーに関数が入っている場合はその関数を呼びます。これを使うとオブジェクトのように書けます。

```laping
fn Counter() {
    let n = 0
    return {
        inc: fn() { n += 1 },
        value: fn() => n,
    }
}
c = Counter()
c.inc()
print(c.value())   # => 1
```

### record（型の宣言）と型ごとのメソッド

`record` で、決まったフィールドを持つ型を宣言できます。宣言していないフィールドへの代入はエラーになるので、打ち間違いにすぐ気づけます。

```laping
record Point(x, y = 0)             # y は省略すると 0
p = Point(3, 4)
print(p)                           # Point(x: 3, y: 4)
p.x = 10                           # OK
p.z = 1                            # エラー: Point にフィールド 'z' はありません
print(type(p), p is Point)         # Point true
```

`fn 型名.メソッド名(値, ...)` で、型にメソッドを追加できます。第1引数がその値です（`self` は不要）。`string` `list` `number` などの組み込み型にも追加できます。

```laping
fn Point.dist(p) => sqrt(p.x ** 2 + p.y ** 2)
print(Point(3, 4).dist())          # 5

fn string.shout(s) => upper(s) + "!"
print("hello".shout())             # HELLO!
```

`x.f(a)` は、①型に追加したメソッド ②マップのキー `f` に入った関数 ③`f(x, a)` の順に探して呼びます。

### 定数

全部大文字の名前（2文字以上。`MAX_SIZE` や `API_URL` など）は定数になり、2回目の代入はエラーになります。キーワードを増やさずに「変えてはいけない値」を表せます。

```laping
MAX_SIZE = 10
MAX_SIZE = 20    # エラー: 定数 MAX_SIZE は変更できません
```

### スコープ

- 関数を呼び出すたびに新しいスコープが作られます。`if` や `for` などのブロックは新しいスコープを作りません
- `x = 値` は、外側のスコープに `x` があればそれを書き換え、なければ現在のスコープに作ります
- `let x = 値` は常に現在のスコープに新しい変数を作ります。関数の中で外側と同じ名前を使いたいときは `let` を使ってください

```laping
count = 0
fn increment() {
    count += 1        # 外側の count を書き換える
}
fn local_only() {
    let count = 100   # 外側とは別の変数
    return count
}
```

### エラー処理

```laping
try {
    risky()
} catch e {             # e にエラーの内容が入る（変数名は省略可）
    print("失敗:", e)
} finally {             # 省略可。成功しても失敗しても必ず実行される
    print("後片付け")
}

throw "値が不正です"             # 任意の値を投げられる
throw {code: 404, message: "Not Found"}
```

実行時エラー（ゼロ除算・未定義変数など）も `catch` で捕捉でき、`e` にはエラーメッセージの文字列が入ります。捕捉されなかったエラーは、行番号と呼び出し履歴を表示して終了コード1で終了します。

```
Laping: リストの添字 10 が範囲外です（長さ 3） (7行目)
    inner() の呼び出し元: 3行目
    outer() の呼び出し元: 9行目
```

### ファイルの分割（import）

```laping
import "lib/util"              # lib/util.lp を読み込んで実行する（.lp は省略可）
import "lib/shapes" as shapes  # モジュールとして読み込む
print(shapes.area(3, 4))
```

- パスは `import` を書いたファイルのあるフォルダからの相対パスです
- `as` を付けない場合、読み込んだファイルで定義した関数や変数は、そのまま使えます。同じファイルは2回目以降は読み込まれません
- `as 名前` を付けると、ファイルは独立したスコープで実行され、その変数と関数をまとめたマップが `名前` に入ります。名前の衝突を防げます
- `as` で読み込んだとき、`_` で始まる名前（`_helper` など）は外から見えません。公開する名前を一目で区別できます

### エラー検出

Lapingは曖昧な挙動を避けるため、以下を実行時エラーとして検出します。捕捉されなかったエラーは、場所・該当行・原因の位置（`^`）・ヒント・呼び出し履歴を表示して、終了コード1で終了します。

```
エラー: 未定義の変数 'numz'
  --> report.lp:3:28
  |
3 |     return sum(nums) / len(numz)
  |                            ^
  ヒント: もしかして 'nums' ですか？
  呼び出し履歴:
    average() ← 5行目
```

| エラー内容 | 例 |
|---|---|
| 未定義の変数を参照 | `print(z)` |
| 数値以外の値を数値演算子に渡す | `"a" - 1` |
| 文字列と数値の `+` 混在 | `"a" + 1` |
| ゼロ除算・ゼロ剰余 | `1 / 0` |
| 範囲外の添字・存在しないキー | `[1, 2][5]`、`{a: 1}.b` |
| 関数でない値の呼び出し・引数の数の間違い | `nil()`、`len()` |
| 大小比較できない型の比較 | `1 < "a"` |
| 止まらない再帰 | `fn f() => f()` |
| record に無いフィールドへの代入 | `Point(1, 2).z = 3` |
| 不明な型名 | `x is lsit` |
| 定数の書き換え | `MAX = 1` の後に `MAX = 2` |
| 文字列リテラルが閉じられていない（構文エラー） | `print("abc` |
| ブロックの `{` `}` が閉じられていない（構文エラー） | `if x > 0 { print(x)` |
| 不正な文字（構文エラー） | 未対応の記号の使用 |

構文エラーはプログラムの実行前に検出され、`構文エラー: ...` と表示されます。`laping check` を使うと、実行せずに構文だけを確認できます。

## 組み込み関数

どの関数も `f(x, a)`・`x.f(a)`・`x -> f(a)` のどの形でも呼べます。一覧と説明は `laping doc`（対話モードでは `:doc`、プログラム中では `help()`）でも表示できます。

### 入出力・ファイル

| 関数 | 説明 |
|---|---|
| `print(...)` | 引数をスペース区切りで出力して改行 |
| `write(...)` | 改行せずに出力 |
| `input(prompt?)` | 1行読み込む。入力が終わっていれば `nil` |
| `read_file(path)` | ファイルの中身を文字列で返す |
| `write_file(path, s)` / `append_file(path, s)` | ファイルに書き込む / 追記する |
| `file_exists(path)` | ファイルが存在するか |

### 画面表示

端末に出力しているときだけ色や太字が付き、ファイルやパイプに出すときは文字だけになります。全角文字は幅2として桁を揃えます。

| 関数 | 説明 |
|---|---|
| `table(行のリスト, 見出し?)` | 罫線付きの表を表示する。行はリストかマップ（record）。数値は右寄せ |
| `box(文字列, タイトル?)` | 文字列を枠で囲んで表示する |
| `progress(現在, 全体, 幅?)` | 進捗バーの文字列（`[██████░░░░]  60%`） |
| `pp(値)` | 入れ子のリストやマップを整形して表示する |
| `color(値, 色名)` | 色を付ける（`red` `green` `yellow` `blue` `magenta` `cyan` `white` `gray` `black`、`赤` `緑` `青` なども可） |
| `bold(値)` / `dim(値)` / `underline(値)` | 太字 / 薄字 / 下線 |
| `confirm(質問)` | `[y/N]` で確認し、はいなら `true` |
| `choose(質問, 選択肢)` | 番号付きの選択肢から選ばせ、選ばれた値を返す |
| `clear_screen()` | 画面を消す |
| `width(値)` | 端末上の表示幅 |

```laping
table([["りんご", 120], ["バナナ", 98]], ["品名", "価格"])
```

```
┌────────┬──────┐
│  品名  │ 価格 │
├────────┼──────┤
│ りんご │  120 │
│ バナナ │   98 │
└────────┴──────┘
```

### 型・変換

| 関数 | 説明 |
|---|---|
| `type(x)` | 型名（`"number"` `"string"` `"bool"` `"nil"` `"list"` `"map"` `"range"` `"function"`） |
| `str(x)` | 文字列に変換（`print` と同じ表示） |
| `repr(x)` | 文字列なら引用符付きの表現を返す |
| `num(x)` | 数値に変換（`"3.5"`・`"0xFF"` も可。変換できなければエラー） |
| `int(x)` | 小数点以下を切り捨てた整数に変換 |
| `bool(x)` | 真偽値に変換 |
| `is_num(x)` | 数値、または数値に変換できる文字列か |
| `list(x)` | 範囲・文字列・マップ(キー)からリストを作る。リストならコピー |
| `range(end)` / `range(start, end, step?)` | `end` を含まない範囲 |
| `len(x)` | 文字列(文字数)・リスト・マップ・範囲の長さ |
| `copy(x)` | リスト・マップの浅いコピー |
| `to_json(値, インデント?)` / `from_json(文字列)` | JSON との変換 |

### リスト

| 関数 | 説明 |
|---|---|
| `push(list, x...)` | 末尾に追加（リスト自身を返す） |
| `pop(list, i?)` | 末尾（または i 番目）を取り出す |
| `shift(list)` / `unshift(list, x...)` | 先頭を取り出す / 先頭に追加 |
| `insert(list, i, x)` / `remove(list, i)` | i 番目に挿入 / i 番目を削除して返す |
| `clear(x)` | リスト・マップを空にする |
| `slice(x, start, end?)` | 部分リスト・部分文字列（負の値は後ろから） |
| `index_of(x, v)` | 見つかった位置。なければ `-1` |
| `contains(x, v)` | 含まれているか（`v in x` と同じ） |
| `reverse(x)` | 逆順にした新しいリスト・文字列 |
| `sort(x, key_fn?)` | 並べ替えた新しいリスト（安定ソート）。`key_fn` の値で比較することもできる |
| `map(x, f)` / `filter(x, f)` / `each(x, f)` | 各要素に関数を適用。`f` が2引数なら `(要素, 添字)` を渡す |
| `reduce(x, f, init?)` | 畳み込み |
| `find(x, f)` | 条件を満たす最初の要素（なければ `nil`） |
| `any(x, f?)` / `all(x, f?)` | いずれか / すべての要素が条件を満たすか |
| `count(x, v_or_f)` | 値に等しい、または条件を満たす要素の数 |
| `sum(x)` / `min(...)` / `max(...)` | 合計・最小・最大（リスト1つか、複数の引数） |
| `join(x, sep?)` | 要素を文字列にしてつなげる |
| `zip(a, b)` / `enumerate(x)` | `[[a0, b0], ...]` / `[[0, x0], ...]` |
| `flatten(x)` / `unique(x)` | 1段平らにする / 重複を除く |
| `first(x)` / `last(x)` | 最初 / 最後の要素（空なら `nil`） |
| `take(x, n)` / `drop(x, n)` | 先頭の n 個 / 先頭の n 個を除いた残り |
| `chunk(x, n)` | n 個ずつに区切る |
| `group_by(x, f)` | 関数の値ごとにまとめたマップ |
| `partition(x, f)` | `[条件を満たすもの, 満たさないもの]` |
| `tally(x)` | 値ごとの出現回数のマップ |
| `min_by(x, f)` / `max_by(x, f)` | 関数の値が最小 / 最大の要素 |
| `is_empty(x)` | 空か（`nil` も空とみなす） |

### 文字列

| 関数 | 説明 |
|---|---|
| `upper(s)` / `lower(s)` | 大文字 / 小文字に変換 |
| `trim(s)` | 前後の空白（全角スペースを含む）を除く |
| `split(s, sep?)` | 区切り文字で分割。省略すると空白で分割、`""` なら1文字ずつ |
| `replace(s, old, new)` | すべて置換 |
| `starts_with(s, p)` / `ends_with(s, p)` | 前方一致 / 後方一致 |
| `chars(s)` | 1文字ずつのリスト |
| `ord(s)` / `chr(n)` | 文字 ↔ Unicode コードポイント |
| `pad_left(x, width, ch?)` / `pad_right(...)` | 指定の幅になるまで詰める |
| `fixed(n, digits)` | 小数点以下の桁数を固定した文字列（`fixed(3.14159, 2)` は `"3.14"`） |
| `format(書式, 値...)` | `{}` に値を埋め込む。`{:>8}` で右寄せ、`{:.2}` で小数2桁 |
| `center(x, width, ch?)` | 中央に揃える |
| `capitalize(s)` / `lines(s)` | 先頭を大文字に / 行ごとに分割 |

### マップ

| 関数 | 説明 |
|---|---|
| `keys(m)` / `values(m)` / `items(m)` | キー / 値 / `[キー, 値]` のリスト |
| `has(m, k)` | キーがあるか（`k in m` と同じ） |
| `get(m, k, default?)` | 値を取得。なければ `default`（省略時 `nil`）。リストにも使える |
| `delete(m, k)` | キーを削除。削除したら `true` |
| `merge(m1, m2, ...)` | 結合した新しいマップ（後の値が優先） |

### 数学

| 関数・定数 | 説明 |
|---|---|
| `abs` `floor` `ceil` `sqrt` `exp` | 絶対値・切り捨て・切り上げ・平方根・指数関数 |
| `round(x, digits?)` | 四捨五入（桁数指定も可） |
| `pow(x, y)` | べき乗（`x ** y` と同じ） |
| `log(x, base?)` | 自然対数（`base` 指定でその底の対数） |
| `sin` `cos` `tan` `asin` `acos` `atan` `atan2(y, x)` | 三角関数 |
| `random()` | 0以上1未満の乱数 |
| `rand_int(a, b)` | a以上b以下の整数の乱数 |
| `choice(x)` / `shuffle(x)` | ランダムに1つ選ぶ / シャッフルした新しいリスト |
| `seed(n)` | 乱数の種を設定（毎回同じ乱数列にしたいとき） |
| `PI` `E` `INF` | 円周率・ネイピア数・無限大 |

### システム

| 関数・変数 | 説明 |
|---|---|
| `args` | コマンドライン引数のリスト（`laping main.lp a b` なら `["a", "b"]`） |
| `time()` | 現在時刻（UNIX 時間、秒） |
| `date(書式?, 時刻?)` | 日時の文字列（既定は `"%Y-%m-%d %H:%M:%S"`） |
| `env(名前, 既定値?)` | 環境変数を読む |
| `help(関数?)` | 組み込み関数の説明を表示する |
| `clock()` | プログラム開始からの CPU 時間（秒） |
| `sleep(sec)` | 指定秒数待つ |
| `exit(code?)` | プログラムを終了する |
| `assert(cond, msg?)` | 条件が偽ならエラーにする |

## サンプルコード

| ファイル | 内容 |
|---|---|
| [examples/tour.lp](examples/tour.lp) | 主な機能をひととおり紹介 |
| [examples/todo.lp](examples/todo.lp) | TODO 管理の CLI アプリ（record・JSON 保存・table・進捗バー） |
| [examples/report.lp](examples/report.lp) | 売上データの集計レポート（`->`・group_by・内包表記・書式付き埋め込み） |
| [examples/quicksort.lp](examples/quicksort.lp) | 再帰とリスト操作によるクイックソート |
| [examples/bank.lp](examples/bank.lp) | クロージャでオブジェクトを作り、例外で入力を検証する |
| [examples/wordcount.lp](examples/wordcount.lp) | マップを使った単語の集計 |
| [examples/fizzbuzz.lp](examples/fizzbuzz.lp) / [examples/primes.lp](examples/primes.lp) / [examples/helloworld.lp](examples/helloworld.lp) | v1 の基本的なサンプル |

## 文法まとめ（EBNF風）

```
program     := statement*
statement   := if_stmt | while_stmt | for_stmt | loop_stmt | repeat_stmt | match_stmt
             | try_stmt | fn_decl | record_decl | test_block | import_stmt
             | simple_stmt modifier?                  # 改行 or ";" で終わる
if_stmt     := ("if" | "unless") expr block
               ("elif" expr block | "else" "if" expr block)* ("else" block)?
while_stmt  := ("while" | "until") expr block
for_stmt    := "for" IDENT ("," IDENT)? "in" expr block
loop_stmt   := "loop" block
repeat_stmt := "repeat" expr block
match_stmt  := "match" expr "{" (arm_head (block | simple_stmt))* "}"
arm_head    := pattern ("," pattern)* ("if" expr)? "=>"
pattern     := "_" | expr
try_stmt    := "try" block ("catch" IDENT? block)? ("finally" block)?
fn_decl     := "fn" (IDENT ".")? IDENT fn_rest            # fn Type.method(...)
record_decl := "record" IDENT "(" params ")"
test_block  := "test" STRING block
import_stmt := "import" STRING ("as" IDENT)?
block       := "{" statement* "}"

simple_stmt := "let" IDENT ("," IDENT)* ("=" expr ("," expr)*)?
             | "return" expr? | "break" | "continue" | "throw" expr | "expect" expr
             | target ("," target)* "=" expr ("," expr)*
             | target ("+=" | "-=" | "*=" | "/=" | "%=") expr
             | expr
target      := IDENT | postfix "[" expr "]" | postfix "." IDENT
modifier    := ("if" | "unless" | "while" | "until") expr   # simple_stmt と同じ行のみ有効
             | "for" IDENT ("," IDENT)? "in" expr ("if" expr)?
             | "repeat" expr

expr        := pipe ("then" expr "else" expr | "else" expr | "?" expr ":" expr)?
pipe        := or_expr ("->" or_expr)*                   # x -> f(a) は f(x, a)
or_expr     := and_expr (("or" | "||") and_expr)*
and_expr    := not_expr (("and" | "&&") not_expr)*
not_expr    := "not" not_expr | comparison
comparison  := range ((cmp_op range)+ | ("in" | "not" "in") range | "is" "not"? TYPE)*
cmp_op      := "==" | "!=" | "<" | ">" | "<=" | ">="     # 連鎖できる: 1 < x < 10
range       := additive ((".." | "...") additive)?
additive    := term (("+" | "-") term)*
term        := unary (("*" | "/" | "//" | "%") unary)*
unary       := ("-" | "+" | "!") unary | power
power       := postfix ("**" unary)?
postfix     := primary ("(" args ")" | "[" expr "]" | "." IDENT ("(" args ")")?)*
primary     := NUMBER | STRING | "true" | "false" | "nil" | IDENT | "(" expr ")"
             | "[" (expr ("," expr)* ","? | expr "for" for_clause)? "]"
             | "{" (entry ("," entry)* ","? | expr ":" expr "for" for_clause)? "}"
             | "fn" IDENT? fn_rest | "match" expr "{" (arm_head expr)* "}"
             | "throw" expr
for_clause  := IDENT ("," IDENT)? "in" expr ("if" expr)?
entry       := IDENT ":" expr | IDENT | expr ":" expr
fn_rest     := "(" params? ")" ("=>" expr | block) | IDENT "=>" expr
params      := param ("," param)*
param       := IDENT ("=" expr)? | "..." IDENT
```

予約語: `if` `elif` `else` `unless` `while` `until` `for` `in` `loop` `repeat` `break` `continue` `return` `fn` `let` `true` `false` `nil` `and` `or` `not` `is` `then` `match` `try` `catch` `finally` `throw` `import`

文の先頭の `record` `test` `expect` と、`import` の後の `as` は、その位置でだけ特別な意味を持ちます（変数名としても使えます）。

## v2.0 からの変更点（v2.1）

v2.0 のプログラムはほぼそのまま動きますが、次の点が変わっています。

- `repeat` `is` `then` が予約語になり、変数名に使えなくなりました
- 式の直後の `else`（`値 else 既定値`）は既定値の指定になりました
- 文の先頭の `expect` は `expect` 文になりました（`expect(...)` という名前の関数を文の先頭で呼べなくなりました）
- 全部大文字（2文字以上）の変数は定数になり、再代入するとエラーになります
- `"{式:書式}"` のように、埋め込み式の最後の `:` の後ろが書式として読める場合は書式として扱われます
- エラーメッセージの形式が変わりました（`Laping: 内容 (N行目)` → 場所・該当行・ヒントを含む複数行の表示）
- `laping test` `check` `new` `doc` `run` `help` `version` がコマンドになりました。これらと同じ名前の拡張子なしファイルを実行するには `laping run test` のようにします

## v1 からの変更点（v2.0）

v1 のプログラム（`examples/fizzbuzz.lp` など）はそのまま動きますが、次の点が変わっています。

- `"..."` の中の `{` `}` は式の埋め込みになりました。文字として書くには `\{` `\}` を使うか、`'...'` を使ってください
- 比較演算子の結果は `1` / `0` ではなく `true` / `false` になりました
- `%` の結果の符号は除数に合わせるようになりました（`-7 % 3` は `2`）。また小数の剰余も計算できます
- 上記の予約語は変数名に使えなくなりました
- `print` は予約語ではなく組み込み関数になり、複数の引数を受け取れます
- 文字列の長さやリスト・変数の数などの上限がなくなりました

## 開発者向け

### テスト

```sh
make test
```

`tests/*.lp` を実行し、出力を同名の `.expected` ファイルと比較します（`*_test.lp` は `laping test` で実行します。終了コードは既定で 0、`名前.exit` があればその値を期待します）。GC の不具合を探すときは、すべての文の実行前に GC を走らせる `LAPING_GC_STRESS=1` を付けて実行してください。

```sh
LAPING_GC_STRESS=1 tests/run_tests.sh ./laping
```

### 新バージョンのリリース方法

```sh
git tag v2.1.0
git push origin v2.1.0
```

タグをプッシュすると GitHub Actions が起動し、テストを実行したうえで Linux/Windows向けバイナリをビルドして自動的にReleaseへ添付します。各ユーザーは `laping update` を実行するだけで新しいバイナリに更新されます。タグのバージョンは `src/laping.h` の `LAPING_VERSION` と合わせてください。

## ライセンス

(未設定)
