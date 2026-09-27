# v1 の構文がそのまま動くことを確認する
x = 10
name = "Laping"
print(x)
print(name)
print((1 + 2) * 3)
print("正の数です") if x > 0
print("0以下です") unless x > 0
if x > 0 {
    print("これは修飾子ではなく通常のifブロック")
}
if x > 100 {
    print("big")
} else {
    print("small")
}
i = 0
while i < 3 {
    print(i)
    i = i + 1
}
print(7 % 3)
print(3.14)
print(10 / 4)
print("a" == "a")
print(1 == "1")
