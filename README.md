# Laping (`.lp`)

**Lap + Lang = Laping**

C言語で実装された、インタプリタ型のプログラミング言語です。
Lexer → Parser → Tree-walking Interpreter という構成で、外部の構文解析ライブラリには依存していません（HTTP通信用に自動更新機能のみ libcurl を使用）。

```laping
fn fizzbuzz(n) {
    match 0 {
        n % 15 => return "FizzBuzz"
        n % 3 => return "Fizz"
        n % 5 => return "Buzz"
        _ => return str(n)
    }
}

for i in 1..15 {
    print(fizzbuzz(i))
}

scores = {太郎: 82, 花子: 95, 次郎: 67}
for name, score in scores {
    print("{name}: {score}点") if score >= 80
}
```

## 特徴

- 記号自体は他言語と同じ一般的なもの（`=` `+` `if` `while` など）を採用しつつ、組み立て方（構文構造）が独自
- Ruby のような **後置修飾子構文**（`stmt if cond` / `unless` / `while` / `until`）
- 関数・クロージャ・アロー関数・デフォルト引数・可変長引数
- リスト・マップ・範囲（`1..10`）、`for ... in`、`match`、`try` / `catch` / `finally`
- 文字列への式の埋め込み（`"合計 {a + b} 円"`）、UTF-8 対応（日本語の文字数・添字・変数名）
- `x.f(a)` と書くと `f(x, a)` を呼ぶメソッド構文（組み込み関数にもユーザー関数にも使える）
- 型不一致・ゼロ除算・未定義変数などを実行時に検出してエラーにする（暗黙の型変換なし）
- 90以上の組み込み関数、`import` によるファイル分割、対話モード（REPL）
- マーク&スイープ GC によるメモリ管理
- 単一バイナリで動作し、`laping update` でGitHub Releasesから自動更新できる

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
| `src/builtins.c` | 組み込み関数 |
| `src/main.c` | コマンドライン・対話モード |
| `src/updater.c` | 自動更新 |

## 使い方

```sh
laping main.lp            # main.lp を実行
laping main.lp a b c      # 引数付きで実行（スクリプト内では args で受け取れる）
laping                    # 対話モード (REPL) を起動
laping -e 'print(1 + 2)'  # コードを直接実行
laping update             # GitHub Releasesの最新版を確認し、自動更新
laping --version          # バージョン表示
```

対話モードでは、入力した式の値がそのまま表示されます。`{` や `(` が閉じていない間は続きの行を入力できます。

```
> xs = [3, 1, 2]
> sort(xs)
[1, 2, 3]
> fn sq(x) {
...     return x * x
... }
> sq(12)
144
```

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
| `==` `!=` `<` `>` `<=` `>=` `in` `not in` | 比較・所属判定 |
| `not` | 否定 |
| `and` `&&` | 論理積（短絡評価） |
| `or` `\|\|` | 論理和（短絡評価） |
| `条件 ? a : b` | 三項演算子 |

演算子の型の規則（暗黙の型変換はしません）:

| 演算子 | 使える組み合わせ |
|---|---|
| `+` | 数値 + 数値、文字列 + 文字列（連結）、リスト + リスト（連結） |
| `*` | 数値 * 数値、文字列 * 整数（繰り返し）、リスト * 整数（繰り返し） |
| `-` `/` `//` `%` `**` | 数値のみ。0 での除算・剰余はエラー。`%` の結果は除数と同じ符号 |
| `<` `>` `<=` `>=` | 数値同士、または文字列同士（辞書順） |
| `==` `!=` | 何でも比較可能。リストとマップは中身で比較。型が違えば常に不一致 |
| `in` | 要素 `in` リスト、キー `in` マップ、部分文字列 `in` 文字列、数値 `in` 範囲 |

`and` / `or` は真偽値ではなく、評価したオペランドの値をそのまま返します。

```laping
name = input_name or "名無し"   # input_name が nil や "" なら "名無し"
```

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
- 文字列は改行を含めて複数行に書けます
- 長さ・添字は **文字単位**（UTF-8）で数えます: `len("こんにちは")` は `5`、`"こんにちは"[1]` は `"ん"`
- 負の添字は後ろから数えます: `"abc"[-1]` は `"c"`

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

**単文**（代入・式・`return`・`break`・`continue`・`throw`）の末尾に `if` / `unless` / `while` / `until` を置くと、条件付きで実行できます。

```laping
print("正の数です") if x > 0
print("0以下です") unless x > 0
return 0 if n < 1
continue unless i % 3 == 0
throw "不正な値" if value < 0
i += 1 while i < 10
i -= 1 until i == 0
```

- **同じ行に書かれている場合のみ**修飾子として解釈されます。改行を挟んだ場合は別々の文として扱われ、誤って次の行の `if` ブロックを修飾子と誤認識することはありません
- 修飾子の対象にできるのは **単文のみ** です。複数の文をまとめて条件付きにしたい場合はブロック構文を使ってください。これにより「修飾子がどこまでを対象にしているか」が常に1文に固定され、構文上の曖昧さが生まれません

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

### 関数

```laping
fn add(a, b) {
    return a + b
}

fn square(x) => x * x               # 式1つだけなら => で書ける

fn greet(name, greeting = "こんにちは") => "{greeting}、{name}さん"   # デフォルト引数
fn total(...nums) => sum(nums)      # 可変長引数（リストで受け取る）

double = fn(x) => x * 2             # 無名関数
[1, 2, 3].map(fn(x) => x + 1)       # 関数を引数に渡す
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
import "lib/util"      # lib/util.lp を読み込んで実行する（.lp は省略可）
```

- パスは `import` を書いたファイルのあるフォルダからの相対パスです
- 読み込んだファイルで定義した関数や変数は、そのまま使えます
- 同じファイルは2回目以降は読み込まれません

### エラー検出

Lapingは曖昧な挙動を避けるため、以下を実行時エラーとして検出します。いずれも `Laping: <内容> (<行番号>行目)` の形式でメッセージを出し、終了コード1で終了します（`try` で捕捉しない場合）。

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
| 文字列リテラルが閉じられていない（構文エラー） | `print("abc` |
| ブロックの `{` `}` が閉じられていない（構文エラー） | `if x > 0 { print(x)` |
| 不正な文字（構文エラー） | 未対応の記号の使用 |

構文エラーはプログラムの実行前に検出され、`Laping: 構文エラー: ...` と表示されます。

## 組み込み関数

どの関数も `f(x, a)` と `x.f(a)` のどちらの形でも呼べます。

### 入出力・ファイル

| 関数 | 説明 |
|---|---|
| `print(...)` | 引数をスペース区切りで出力して改行 |
| `write(...)` | 改行せずに出力 |
| `input(prompt?)` | 1行読み込む。入力が終わっていれば `nil` |
| `read_file(path)` | ファイルの中身を文字列で返す |
| `write_file(path, s)` / `append_file(path, s)` | ファイルに書き込む / 追記する |
| `file_exists(path)` | ファイルが存在するか |

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
| `clock()` | プログラム開始からの CPU 時間（秒） |
| `sleep(sec)` | 指定秒数待つ |
| `exit(code?)` | プログラムを終了する |
| `assert(cond, msg?)` | 条件が偽ならエラーにする |

## サンプルコード

| ファイル | 内容 |
|---|---|
| [examples/tour.lp](examples/tour.lp) | 主な機能をひととおり紹介 |
| [examples/quicksort.lp](examples/quicksort.lp) | 再帰とリスト操作によるクイックソート |
| [examples/bank.lp](examples/bank.lp) | クロージャでオブジェクトを作り、例外で入力を検証する |
| [examples/wordcount.lp](examples/wordcount.lp) | マップを使った単語の集計 |
| [examples/fizzbuzz.lp](examples/fizzbuzz.lp) / [examples/primes.lp](examples/primes.lp) / [examples/helloworld.lp](examples/helloworld.lp) | v1 の基本的なサンプル |

## 文法まとめ（EBNF風）

```
program     := statement*
statement   := if_stmt | while_stmt | for_stmt | loop_stmt | match_stmt
             | try_stmt | fn_decl | import_stmt
             | simple_stmt modifier?                  # 改行 or ";" で終わる
if_stmt     := ("if" | "unless") expr block
               ("elif" expr block | "else" "if" expr block)* ("else" block)?
while_stmt  := ("while" | "until") expr block
for_stmt    := "for" IDENT ("," IDENT)? "in" expr block
loop_stmt   := "loop" block
match_stmt  := "match" expr "{" (pattern ("," pattern)* ("if" expr)? "=>" (block | simple_stmt))* "}"
pattern     := "_" | expr
try_stmt    := "try" block ("catch" IDENT? block)? ("finally" block)?
fn_decl     := "fn" IDENT fn_rest
import_stmt := "import" STRING
block       := "{" statement* "}"

simple_stmt := "let" IDENT ("," IDENT)* ("=" expr ("," expr)*)?
             | "return" expr? | "break" | "continue" | "throw" expr
             | target ("," target)* "=" expr ("," expr)*
             | target ("+=" | "-=" | "*=" | "/=" | "%=") expr
             | expr
target      := IDENT | postfix "[" expr "]" | postfix "." IDENT
modifier    := ("if" | "unless" | "while" | "until") expr   # simple_stmt と同じ行のみ有効

expr        := or_expr ("?" expr ":" expr)?
or_expr     := and_expr (("or" | "||") and_expr)*
and_expr    := not_expr (("and" | "&&") not_expr)*
not_expr    := "not" not_expr | comparison
comparison  := range (("==" | "!=" | "<" | ">" | "<=" | ">=" | "in" | "not" "in") range)*
range       := additive ((".." | "...") additive)?
additive    := term (("+" | "-") term)*
term        := unary (("*" | "/" | "//" | "%") unary)*
unary       := ("-" | "+" | "!") unary | power
power       := postfix ("**" unary)?
postfix     := primary ("(" args ")" | "[" expr "]" | "." IDENT ("(" args ")")?)*
primary     := NUMBER | STRING | "true" | "false" | "nil" | IDENT | "(" expr ")"
             | "[" (expr ("," expr)* ","?)? "]"
             | "{" (entry ("," entry)* ","?)? "}"
             | "fn" IDENT? fn_rest
entry       := IDENT ":" expr | IDENT | expr ":" expr
fn_rest     := "(" params? ")" ("=>" expr | block)
params      := param ("," param)*
param       := IDENT ("=" expr)? | "..." IDENT
```

予約語: `if` `elif` `else` `unless` `while` `until` `for` `in` `loop` `break` `continue` `return` `fn` `let` `true` `false` `nil` `and` `or` `not` `match` `try` `catch` `finally` `throw` `import`

## v1 からの変更点

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

`tests/*.lp` を実行し、出力を同名の `.expected` ファイルと比較します。GC の不具合を探すときは、すべての文の実行前に GC を走らせる `LAPING_GC_STRESS=1` を付けて実行してください。

```sh
LAPING_GC_STRESS=1 tests/run_tests.sh ./laping
```

### 新バージョンのリリース方法

```sh
git tag v2.0.0
git push origin v2.0.0
```

タグをプッシュすると GitHub Actions が起動し、テストを実行したうえで Linux/Windows向けバイナリをビルドして自動的にReleaseへ添付します。各ユーザーは `laping update` を実行するだけで新しいバイナリに更新されます。タグのバージョンは `src/laping.h` の `LAPING_VERSION` と合わせてください。

## ライセンス

(未設定)
