<div align="center">

<p align="center"><b>English</b> | <a href="README.ja.md">日本語</a></p>

<h1 align="center">Laping</h1>

<h3 align="center">Lap + Lang = Laping. A language you can read in order, and whose mistakes show up fast.</h3>

<p align="center">An interpreted programming language written in C (<code>.lp</code> files). It is a lexer, a parser and a tree-walking interpreter, with no parsing library. Only the auto-update feature uses libcurl.</p>

<p align="center">
  <a href="https://github.com/Lapius7/laping-lang/releases/latest"><img alt="Release" src="https://img.shields.io/github/v/release/Lapius7/laping-lang?style=flat-square&color=e8833a&logo=github"></a>
  <a href="https://www.npmjs.com/package/@lapius/laping-lang"><img alt="npm" src="https://img.shields.io/npm/v/@lapius/laping-lang?style=flat-square&color=cb3837&logo=npm"></a>
  <a href="https://github.com/Lapius7/laping-lang/actions/workflows/ci.yml"><img alt="CI" src="https://img.shields.io/github/actions/workflow/status/Lapius7/laping-lang/ci.yml?branch=main&label=CI&style=flat-square&logo=githubactions&logoColor=white"></a>
  <img alt="C" src="https://img.shields.io/badge/C-a8b9cc?style=flat-square&logo=c&logoColor=black">
  <a href="LICENSE"><img alt="License" src="https://img.shields.io/github/license/Lapius7/laping-lang?style=flat-square&color=97ca00"></a>
  <a href="https://github.com/Lapius7/laping-lang/stargazers"><img alt="Stars" src="https://img.shields.io/github/stars/Lapius7/laping-lang?style=flat-square&color=e3b341&logo=github"></a>
  <a href="https://github.com/Lapius7/laping-lang/commits/main"><img alt="Last commit" src="https://img.shields.io/github/last-commit/Lapius7/laping-lang?style=flat-square&color=3b82f6"></a>
</p>

<p align="center">
  <a href="#install">Install</a> ·
  <a href="#usage">Usage</a> ·
  <a href="#features">Features</a> ·
  <a href="#writing-laping-with-an-ai">AI</a> ·
  <a href="#development">Development</a>
</p>

</div>

---

```laping
record Sale(shop, price, qty)
fn Sale.total(s) => s.price * s.qty

sales = [Sale("東京", 120, 30), Sale("大阪", 110, 45), Sale("東京", 400, 8)]

# Data flows left to right with ->
tokyo = sales -> filter(fn s => s.shop == "東京") -> map(fn s => s.total()) -> sum()
print("東京の売上: {tokyo:>8} 円")

# A value that may be missing falls back to a default with else
config = {server: {host: "example.com"}}
port = config.server.port else 8080

# What to do comes first, the condition after (postfix modifiers)
print("{s.shop}: {s.total()}") for s in sales if s.total() > 3000

# Tests are written in the same language, right next to the code (run with laping test)
test "売上を計算できる" {
    expect Sale("東京", 100, 3).total() == 300
}
```

> [!NOTE]
> This English README covers install, usage, features and development. The full syntax reference, the built-in function reference and the EBNF grammar are only in the Japanese [README.ja.md](README.ja.md). `laping doc` lists the built-in functions in the terminal.

## Writing Laping with an AI

Laping is a new language, so an AI will mix in Python or JavaScript syntax unless you give it the specification. Paste the prompt in [README.ja.md](README.ja.md#ai-に-laping-のコードを書かせるとき), or point the AI at [docs/AI_GUIDE.md](docs/AI_GUIDE.md) (the spec is written in Japanese). [llms.txt](llms.txt) is a short summary for AI crawlers.

## Features

- **Reads in order**: `->` pipelines, postfix modifiers (`stmt if cond` / `for` / `repeat`), and `then` / `else` to choose a value
- **Words over symbols**: defaults are `value else default`, type checks are `x is list`, counted loops are `repeat 3 { }`
- **Mistakes show up fast**: a mistyped `record` field, an unknown type name, or reassigning a constant (an all-uppercase name) is an error, with "did you mean" suggestions
- **Easy to manage**: `import "x" as x` namespaces each module, names starting with `_` are private, and `test` blocks can live in the same file
- **A readable CLI**: colored errors that show the line and a `^`, a REPL with commands, and `laping test` / `check` / `new` / `doc`
- Functions, closures, `fn x => x * 2`, default and variadic arguments, lists, maps, ranges, comprehensions, `match` (usable as an expression), and `try` / `catch` / `finally`
- Expression interpolation and formatting in strings (`"合計 {a + b:>8} 円"`), and UTF-8 support (Japanese character counts, indexes, variable names, and full-width display width)
- Tables, boxes, progress bars and colors, plus JSON and data aggregation, in 120+ built-in functions (list them with `laping doc`)
- No implicit type conversion, a mark-and-sweep GC, a single binary, and self-update with `laping update`

## Design

Laping's syntax does not copy symbols from other languages. Each form has a reason: it is easier to write, or harder to get wrong.

| Syntax | Meaning | Why |
|---|---|---|
| `xs -> filter(f) -> sum()` | Pass the left value as the first argument of the right function | The data flow follows the arrow, so it reads in the order it was written. It avoids `\|`, which is hard to type on a Japanese keyboard |
| `config.port else 8080` | Default when the value is missing | Words instead of symbols. Unlike `or`, it does not replace `0` or `""`. A missing or nil key in the middle is not an error (a mistyped variable name still is) |
| `x > 0 then "pos" else "neg"` | Choose a value by condition | Reads like English, so there is no `?:` to learn |
| `print(x) for x in xs if x > 0` | Postfix loop | Same rule as the existing postfix `if` / `unless` / `while`: the action first, the condition after |
| `[x * 2 for x in xs if x > 0]` | Comprehension | Same word order as the postfix `for`, so nothing new to learn |
| `repeat 3 { }` / `hi() repeat 3` | Repeat a fixed number of times | No unused counter variable |
| `1 < x < 10` | Chained comparison | Written as in mathematics. Each term is evaluated once |
| `record Point(x, y)` | Type declaration | Assigning an undeclared field is an error, so typos show up immediately |
| `fn Point.len(p)` / `fn string.shout(s)` | Add a method to a type | Written like a function, with no classes or `self`. Works on built-in types too |
| `x is list` | Type check | Prevents typos such as `type(x) == "lsit"` with an "unknown type name" error |
| `MAX_SIZE = 10` | Constant | An all-uppercase name cannot be reassigned, so "a value that must not change" needs no extra keyword |
| `import "util" as util` | Module | Avoids name collisions. Names starting with `_` are hidden from outside |
| `test "name" { expect a == b }` | Test | On failure it prints the left and right values. Skipped in a normal run |
| `"{pts:>5}"` / `"{rate:.1}"` | Formatted interpolation | Set alignment and decimal places inside the string |

## Install

### pnpm (recommended; Linux / macOS / Windows)

> [!IMPORTANT]
> This package assumes **pnpm**. Installing with both npm and pnpm duplicates the command, so use only `pnpm add -g`.

```bash
pnpm add -g @lapius/laping-lang
```

This installs the prebuilt binary for your OS and CPU and gives you the `laping` command (Node.js 18 or later, no C compiler needed). Updating uses the same command. With the npm version, `laping update` does not self-update; it shows this command.

### From Releases

Download the file for your OS from [Releases](https://github.com/Lapius7/laping-lang/releases).

- Linux: `laping-linux-x86_64`
- Windows (installer, recommended): `laping-windows-x86_64-setup.exe`
- Windows (zip, advanced): `laping-windows-x86_64.zip` (includes `laping.exe` and `libcurl-x64.dll`)

On Linux, make it executable and put it on your `PATH`.

```sh
chmod +x laping-linux-x86_64
mv laping-linux-x86_64 ~/.local/bin/laping
```

On Windows, run `laping-windows-x86_64-setup.exe`. It is a standard setup wizard that adds the install folder to `PATH`, creates Start menu and desktop shortcuts, and registers an uninstaller in "Apps & features". Use the zip only if you want a portable copy: extract it and keep `laping.exe` and `libcurl-x64.dll` in the **same folder** (it does not start without the DLL).

### Build from source

You need the `libcurl` development headers (for the auto-update feature).

```sh
# Ubuntu/Debian
sudo apt-get install libcurl4-openssl-dev
make
make test      # run the tests
make install   # install to ~/.local/bin/laping
```

The source is in `src/`.

| File | Role |
|---|---|
| `src/lexer.c` | Lexer (source to tokens) |
| `src/parser.c` | Parser (tokens to AST) |
| `src/interp.c` | Evaluator (walks the AST directly; scopes, exceptions, calls) |
| `src/value.c` | Values, strings, lists, maps, GC |
| `src/builtins.c` | Built-in functions (their descriptions live here too) |
| `src/ui.c` | Terminal output (colors, full-width display width, source lines for errors) |
| `src/main.c` | Command line and REPL |
| `src/updater.c` | Auto update |

## Usage

```sh
laping main.lp              # run main.lp
laping main.lp a b c        # run with arguments (read them with args in the script)
laping                      # start the REPL
laping run main.lp          # run (same as laping main.lp)
laping check *.lp           # check syntax without running
laping test                 # run the test blocks in *_test.lp (folders and files can be given)
laping new myapp            # create a project skeleton (main.lp, lib.lp, main_test.lp)
laping doc                  # list built-in functions
laping doc format           # describe one function
laping -e 'print(1 + 2)'    # run code directly
laping update               # check for and install the latest version (npm version prints the update command)
laping version              # show the version
laping help                 # show usage
```

When writing to a terminal, errors and tables are colored. `NO_COLOR=1` turns the color off and `LAPING_COLOR=always` always turns it on.

### REPL

The value of each expression is printed in color. While a `{` or `(` is unclosed, or a line ends with `->`, you can keep typing the next line.

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

| Command | Description |
|---|---|
| `:help` | List commands |
| `:vars` | List defined variables and functions (type and value) |
| `:doc [name]` | Describe a built-in function |
| `:load <file>` | Load and run a file |
| `:time <code>` | Time the execution |
| `:reset` | Remove all variables |
| `:clear` | Clear the screen |
| `:q` | Quit (`exit()` or Ctrl+D also work) |

### Tests

Write `test` blocks in a file named `*_test.lp` and run them with `laping test`. When an `expect` fails, the left and right values of the comparison are printed.

```laping
# calc_test.lp
fn add(a, b) => a + b

test "足し算ができる" {
    expect add(1, 2) == 3
}
```

`test` blocks run only under `laping test`. A normal `laping calc_test.lp` skips them, so you can also put tests directly in the file that defines the functions.

## How auto update works

When you run `laping update` on a build from Releases, it does the following (the npm version does not self-update and points to `pnpm add -g @lapius/laping-lang` instead).

1. Requests `https://api.github.com/repos/Lapius7/laping-lang/releases/latest` to get the latest release tag
2. Compares it with the running version (`laping --version`)
3. If it is newer, downloads the build for your OS (`laping-linux-x86_64` on Linux, `laping-windows-x86_64.zip` on Windows)
4. On Windows, extracts the zip with the built-in `tar.exe` and takes out `laping.exe` and `libcurl-x64.dll` (Windows 10 1803 or later)
5. Backs up the current executable (and the DLL on Windows), then replaces it with the new one

When a maintainer pushes a `vX.Y.Z` tag, CI (GitHub Actions) builds for Linux and Windows and attaches the files to the Release. The npm package is published from the same tag.

## Examples

See [`examples/`](examples/): `tour.lp` (a tour of the main features), `todo.lp` (a TODO CLI app), `report.lp` (sales aggregation), `quicksort.lp`, `bank.lp`, `wordcount.lp`, `fizzbuzz.lp`, `primes.lp`, and `helloworld.lp`.

## Development

```sh
make test
```

This runs `tests/*.lp` and compares the output with the `.expected` file of the same name. `*_test.lp` files run with `laping test`. The exit code is 0 by default, or the value in `name.exit` if present. To hunt GC bugs, run with `LAPING_GC_STRESS=1`, which runs the GC before every statement.

```sh
LAPING_GC_STRESS=1 tests/run_tests.sh ./laping
```

Pushing a tag such as `git tag v2.1.0 && git push origin v2.1.0` starts GitHub Actions, which runs the tests and then builds and publishes the Linux and Windows files.

## License

See [LICENSE](LICENSE) (MIT).
