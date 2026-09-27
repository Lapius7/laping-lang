# finally の後で再送出された例外は、元の位置（3行目）を報告する
try {
    x = [1][5]
} finally {
    print("finally")
    y = 2
}
