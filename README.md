# Laping (`.lp`)

**Lap + Lang = Laping**

C言語で実装された、最小構成のインタプリタ型プログラミング言語です。
Lexer → Parser → Tree-walking Interpreter という素朴な構成で、外部の構文解析ライブラリには依存していません（HTTP通信用に自動更新機能のみ libcurl を使用）。

## 特徴

- 記号自体は他言語と同じ一般的なもの（`=` `+` `if` `while` など）を採用しつつ、組み立て方（構文構造）が独自
- Ruby のような **後置修飾子構文**（`stmt if cond` / `stmt unless cond`）に対応
- 型不一致・ゼロ除算・未定義変数などを実行時に検出してエラーにする
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
make install   # ~/.local/bin/laping にインストール
```

## 使い方

```sh
laping main.lp        # main.lp を実行
laping update          # GitHub Releasesの最新版を確認し、自動更新
laping --version       # バージョン表示
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

`#` から行末まではコメントとして無視されます。

```laping
# これはコメントです
x = 1  # 行末コメントも可
```

### 値の種類

| 種類 | 例 | 補足 |
|---|---|---|
| 数値 | `10`, `3.14` | 内部的には`double`で保持。整数値は整数として表示される |
| 文字列 | `"hello"` | ダブルクオートのみ対応。エスケープシーケンス未対応 |

### 変数と代入

```laping
x = 10
name = "Laping"
```

- 代入は `変数名 = 式` の形式
- 変数は宣言不要（代入した時点で生成される）
- 未定義の変数を参照すると実行時エラーになり、行番号付きで表示される

```laping
print(z)
# => Laping: 未定義の変数 'z' (1行目)
```

### 演算子

| 演算子 | 意味 | 対応する型 |
|---|---|---|
| `+` | 加算 / 文字列連結 | 数値同士、または文字列同士のみ |
| `-` | 減算 | 数値のみ |
| `*` | 乗算 | 数値のみ |
| `/` | 除算 | 数値のみ（0除算はエラー） |
| `%` | 剰余 | 数値のみ（整数として計算） |
| `==` | 等しい | 数値・文字列どちらでも可（型が違えば常にfalse） |
| `!=` | 等しくない | 同上 |
| `<` `>` `<=` `>=` | 比較 | 数値のみ |

`+` は数値と文字列を混在させるとエラーになります。暗黙の型変換は行いません。

```laping
x = "abc"
y = x + 1
# => Laping: '+' で文字列と数値は連結できません (2行目)
```

演算子の優先順位は一般的なものと同じです（`*` `/` `%` > `+` `-` > 比較演算子）。括弧 `()` でグルーピングできます。

```laping
print((1 + 2) * 3)   # => 9
```

### 出力

```laping
print(式)
```

文字列はそのまま、数値は整数ならそのまま、小数なら`%g`形式で出力されます。

### 後置修飾子構文（独自構文）

**単文（代入・print）に限り**、文の末尾に `if` / `unless` を置くことで条件付き実行ができます。これは Ruby の修飾子構文に近いスタイルです。

```laping
print("正の数です") if x > 0
print("0以下です") unless x > 0
```

- `stmt if cond` — `cond` が真の時だけ `stmt` を実行
- `stmt unless cond` — `cond` が偽の時だけ `stmt` を実行（`if`の否定）
- **同じ行に書かれている場合のみ**修飾子として解釈されます。改行を挟んだ場合は別々の文として扱われ、誤って次の行の `if` ブロックを修飾子と誤認識することはありません

```laping
x = 1
if x > 0 {
    print("これは修飾子ではなく通常のifブロック")
}
```

- 修飾子の対象にできるのは **単文のみ**（代入・print）。複数文をまとめて修飾したい場合は、後述のブロック構文を使ってください。これにより「修飾子がどこまでを対象にしているか」が常に1文に固定され、構文上の曖昧さが生まれません。

### 条件分岐（ブロック構文）

複数の文をまとめて条件分岐させたい場合は、`{ }` を使ったブロック構文を使います。

```laping
if cond {
    文1
    文2
} else {
    文3
}
```

- `else` は省略可能
- 条件式に括弧は不要（`if (cond)` ではなく `if cond`）
- ブロックは必ず `{ }` で囲む

### 繰り返し（while）

```laping
i = 0
while i < 5 {
    print(i)
    i = i + 1
}
```

- `while cond { ... }` の形式のみ対応（後置修飾子としての`while`は未対応）
- 条件式に括弧は不要

### エラー検出

Lapingは曖昧な挙動を避けるため、以下を実行時エラーとして検出します。いずれも `Laping: <内容> (<行番号>行目)` の形式でメッセージを出し、終了コード1で終了します。

| エラー内容 | 例 |
|---|---|
| 未定義の変数を参照 | `print(z)` |
| 数値以外の値を数値演算子に渡す | `"a" - 1` |
| 文字列と数値の `+` 混在 | `"a" + 1` |
| ゼロ除算・ゼロ剰余 | `1 / 0` |
| 文字列リテラルが閉じられていない | `print("abc` |
| ブロックの `{` `}` が閉じられていない | `if x > 0 { print(x)` |
| 不正な文字 | 未対応の記号の使用 |

### サンプルコード

[examples/helloworld.lp](examples/helloworld.lp)、[examples/fizzbuzz.lp](examples/fizzbuzz.lp) を参照してください。

```laping
# examples/fizzbuzz.lp
print("=== FizzBuzz (1-20) ===")
i = 1
while i <= 20 {
    if i % 15 == 0 {
        print("FizzBuzz")
    } else {
        if i % 3 == 0 {
            print("Fizz")
        } else {
            if i % 5 == 0 {
                print("Buzz")
            } else {
                print(i)
            }
        }
    }
    i = i + 1
}
```

## 文法まとめ（EBNF風）

```
program    := statement*
statement  := if_stmt | while_stmt | simple_stmt modifier?
if_stmt    := "if" expr block ("else" block)?
while_stmt := "while" expr block
block      := "{" statement* "}"
simple_stmt:= assign | print_stmt
assign     := IDENT "=" expr
print_stmt := "print" "(" expr ")"
modifier   := ("if" | "unless") expr      # simple_stmt と同じ行のみ有効

expr       := additive (("==" | "!=" | "<" | ">" | "<=" | ">=") additive)*
additive   := term (("+" | "-") term)*
term       := factor (("*" | "/" | "%") factor)*
factor     := NUMBER | STRING | IDENT | "(" expr ")"
```

## 開発者向け: 新バージョンのリリース方法

```sh
git tag v1.1.0
git push origin v1.1.0
```

タグをプッシュすると GitHub Actions が起動し、Linux/Windows向けバイナリをビルドして自動的にReleaseへ添付します。各ユーザーは `laping update` を実行するだけで新しいバイナリに更新されます。

## ライセンス

(未設定)
