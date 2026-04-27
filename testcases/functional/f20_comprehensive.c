// f20: 综合测试 - 测试所有功能
int fibonacci(int n) {
    if (n <= 1) {
        return n;
    } else {
        return fibonacci(n - 1) + fibonacci(n - 2);
    }
}

int gcd(int a, int b) {
    if (b == 0) {
        return a;
    }
    return gcd(b, a % b);
}

int isPrime(int num) {
    if (num <= 1) {
        return 0;
    }
    if (num <= 3) {
        return 1;
    }
    if (num % 2 == 0 || num % 3 == 0) {
        return 0;
    }
    int i = 5;
    while (i * i <= num) {
        if (num % i == 0 || num % (i + 2) == 0) {
            return 0;
        }
        i = i + 6;
    }
    return 1;
}

int factorial(int n) {
    int result = 1;
    while (n > 0) {
        result = result * n;
        n = n - 1;
    }
    return result;
}

int combination(int n, int k) {
    if (k > n) {
        return 0;
    }
    if (k == 0 || k == n) {
        return 1;
    }
    return factorial(n) / (factorial(k) * factorial(n - k));
}

int power(int base, int exponent) {
    int result = 1;
    while (exponent > 0) {
        if (exponent % 2 == 1) {
            result = result * base;
        }
        base = base * base;
        exponent = exponent / 2;
    }
    return result;
}

int complexFunction(int a, int b, int c) {
    int result = 0;
    if (!(a > b && b > c)) {
        result = a * b - -(c + -1);
    } else if (!(a < c) || c < b) {
        result = a * (c - +b - -2);
    } else if (b >= a && a >= c || b <= c) {
        result = b * a - -(c + -3);
    } else {
        result = c * (b - -a - -6);
    }

    int i = 0;
    while (i < 10) {
        i = i + 1;
        {
            int i = 0;
        }
        if (i % 3 == 0) {
            result = result + i;
        } else if (i % 3 == 1) {
            result = result - i;
        } else {
            result = result * 2;
            if (result > 100) {
                break;
            }
        }
    }
    return result;
}

int shortCircuit(int a, int b) {
    int result = 0;
    if (a > 0 && b / a > 2) {
        result = result + 25;
    }
    if (a < 0 || b / (a - a + 1) < 0) {
        result = result + 30;
    }
    return result;
}

int nestedLoopsAndConditions(int n) {
    int sum = 0;
    int i = 0;
    while (i < n) {
        int j = 0;
        while (j < i) {
            if ((i + j) % 2 == 0) {
                sum = sum - i * j;
            } else {
                sum = sum + i * j;
                if (sum < 0) {
                    sum = 0;
                    j = j + 1;
                    continue;
                }
            }
            if (sum > 1000) {
                break;
            }
            j = j + 1;
        }
        if (sum > 1000) {
            break;
        }
        i = i + 1;
    }
    return sum;
}

int func1(int x, int y, int z) {
    if (z == 0) {
        return +x * y;
    } else {
        return func1(x, y - z, 0);
    }
}

int func2(int x, int y) {
    if (y) {
        return func2(x % +y, 0);
    } else {
        return x;
    }
}

int func3(int x, int y) {
    if (y == 0) {
        return x + 1;
    } else {
        return func3(x + y, 0);
    }
}

int func4(int x, int y, int z) {
    if (x) {
        return y;
    } else {
        return z;
    }
}

int func5(int x) {
    return -x;
}

int func6(int x, int y) {
    if (x && y) {
        return 1;
    } else {
        return 0;
    }
}

int func7(int x) {
    if (!x) {
        return 1;
    } else {
        return 0;
    }
}

int nestedCalls(int a0, int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8, int a9) {
    int i1 = 5;
    int i2 = 7;
    int i3 = 11;
    int i4 = 13;
    int a = func1(
        func2(
            func1(
                func3(func4(func5(func3(func2(func6(func7(i1), func5(i2)), i3), i4)),
                            a0,
                            func1(func2(func3(func4(func5(a1),
                                                    func6(a2, func7(a3)),
                                                    func2(a4, func7(a5))),
                                              a6),
                                        a7),
                                  func3(a8, func7(a9)), i1)),
                      func2(i2, func3(func7(i3), i4))),
                a0, a1),
            a2),
        a3,
        func3(func2(func1(func2(func3(a4, func5(a5)), func5(a6)),
                          a7, func7(a8)),
                    func5(a9)),
              i1));
    return a;
}

int main() {
    int result = 0;

    // 测试斐波那契数列
    int fib = fibonacci(12);

    // 测试最大公约数
    int gcd_result = gcd(48, 36);

    // 测试素数判断
    int prime_check = isPrime(17);

    // 测试阶乘
    int fact = factorial(6);

    // 测试组合数
    int comb = combination(8, 3);

    // 测试快速幂
    int pow_result = power(2, 10);

    // 测试复杂函数
    int complex_result = complexFunction(5, 3, 7);

    // 测试短路逻辑
    int short_circuit = shortCircuit(-5, 10);

    // 测试嵌套循环和条件
    int nested_loops_conds_result = nestedLoopsAndConditions(10);

    // 测试嵌套调用
    int nested_calls_result = nestedCalls(100, 200, 300, 400, 500, 600, 700, 800, 900, 1000);

    // 综合所有结果
    result = 1 + (fib % 37633 + gcd_result % 37633 - prime_check % 37633 + 
                  fact % 37633 - comb % 37633 + pow_result % 37633 - 
                  complex_result % 37633 + short_circuit % 37633 -
                  nested_loops_conds_result % 37633 + nested_calls_result % 37633) % 254;

    return result;
}
