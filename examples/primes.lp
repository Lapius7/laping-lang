# 2から50までの素数を判定して表示する

n = 2
while n <= 50 {
    is_prime = 1
    i = 2
    while i * i <= n {
        is_prime = 0 if n % i == 0
        i = i + 1
    }
    print(n) if is_prime == 1
    n = n + 1
}

print("---")

count = 0
n = 2
while n <= 50 {
    is_prime = 1
    i = 2
    while i * i <= n {
        is_prime = 0 if n % i == 0
        i = i + 1
    }
    count = count + 1 if is_prime == 1
    n = n + 1
}

print("見つかった素数の個数:")
print(count)
