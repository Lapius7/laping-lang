#!/usr/bin/env python3
"""docs/AI_GUIDE.md の組み込み関数の一覧を src/builtins.c の関数表から作り直す。

  python3 tools/update_ai_guide.py          一覧を更新する
  python3 tools/update_ai_guide.py --check  一覧が最新か確認する（CI 用。古ければ終了コード 1）
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "src" / "builtins.c"
GUIDE = ROOT / "docs" / "AI_GUIDE.md"
START = "<!-- builtins:start -->"
END = "<!-- builtins:end -->"

ENTRY = re.compile(
    r'D\((\w+), (-?\d+), (-?\d+), "((?:[^"\\]|\\.)*)", "((?:[^"\\]|\\.)*)", "((?:[^"\\]|\\.)*)"\)'
)


def render() -> str:
    lines = []
    category = None
    for _name, _mn, _mx, cat, sig, desc in ENTRY.findall(SOURCE.read_text(encoding="utf-8")):
        sig = sig.replace('\\"', '"')
        desc = desc.replace('\\"', '"')
        if cat != category:
            category = cat
            lines += ["", f"### {cat}", "", "| 使い方 | 説明 |", "|---|---|"]
        lines.append(f"| `{sig}` | {desc} |")
    return "\n".join(lines).strip() + "\n"


def main() -> int:
    guide = GUIDE.read_text(encoding="utf-8")
    if START not in guide or END not in guide:
        print(f"{GUIDE} に {START} と {END} がありません", file=sys.stderr)
        return 1
    head, rest = guide.split(START, 1)
    _, tail = rest.split(END, 1)
    updated = f"{head}{START}\n{render()}{END}{tail}"
    if "--check" in sys.argv:
        if updated != guide:
            print("docs/AI_GUIDE.md の組み込み関数の一覧が古くなっています。"
                  "python3 tools/update_ai_guide.py を実行してください", file=sys.stderr)
            return 1
        print("docs/AI_GUIDE.md の組み込み関数の一覧は最新です")
        return 0
    GUIDE.write_text(updated, encoding="utf-8")
    print("docs/AI_GUIDE.md を更新しました")
    return 0


if __name__ == "__main__":
    sys.exit(main())
