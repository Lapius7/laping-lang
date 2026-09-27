name = "世界"
print("こんにちは、{name}！")
print("計算: {1 + 2 * 3}, リスト: {[1, 2]}, 入れ子: {"内側{name}"}")
print("波かっこ: \{ \}", 'シングルクオートは {展開しない}')
print("改行\nタブ\t終わり")
print("\u{1F600} \u{3042}")
s = "こんにちは"
print(len(s), s[0], s[-1], slice(s, 1, 3))
for ch in "あいう" {
    write(ch, "-")
}
print()
print("Hello".upper(), "Hello".lower(), "  空白  ".trim())
print(split("a,b,,c", ","), split("  the quick  fox "), split("abc", ""))
print(join(["x", "y", "z"], " / "), replace("banana", "an", "AN"))
print("hello".starts_with("he"), "hello".ends_with("lo"), "ell" in "hello")
print(reverse("abc"), reverse("日本語"), chars("日本"))
print(ord("A"), chr(97), ord("あ"), chr(12354))
print(str(42) + "!", num("3.5") + 1, int("12"), num(" 0x1F "), is_num("12a"), is_num("1e3"))
print("ab" * 3, "-" * 10)
print(pad_left("7", 3, "0"), pad_right("ab", 5) + "|", fixed(3.14159, 2))
print("apple" < "banana", "a" + "b" == "ab")
print(repr("引用\"符\n"), type("x"))
long = "1行目
2行目"
print(long)
print(index_of("こんにちは", "に"), "abc".contains("d"))
