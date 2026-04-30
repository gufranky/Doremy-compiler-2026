# C 语言标准：6.4.4.2 浮点常量（Floating Constants）

## 语法定义（Syntax）

```
floating-constant（浮点常量）:
    decimal-floating-constant
    hexadecimal-floating-constant

decimal-floating-constant（十进制浮点常量）:
    fractional-constant  [exponent-part]  [floating-suffix]
    digit-sequence  exponent-part  [floating-suffix]

hexadecimal-floating-constant（十六进制浮点常量）:
    hexadecimal-prefix  hexadecimal-fractional-constant  binary-exponent-part  [floating-suffix]
    hexadecimal-prefix  hexadecimal-digit-sequence  binary-exponent-part  [floating-suffix]

fractional-constant（小数常量）:
    [digit-sequence]  .  digit-sequence
    digit-sequence  .

exponent-part（指数部分）:
    e  [sign]  digit-sequence
    E  [sign]  digit-sequence

sign（符号）:  选其一
    +  -

digit-sequence（数字序列）:
    digit
    digit-sequence  digit

hexadecimal-fractional-constant（十六进制小数常量）:
    [hexadecimal-digit-sequence]  .  hexadecimal-digit-sequence
    hexadecimal-digit-sequence  .

binary-exponent-part（二进制指数部分）:
    p  [sign]  digit-sequence
    P  [sign]  digit-sequence

hexadecimal-digit-sequence（十六进制数字序列）:
    hexadecimal-digit
    hexadecimal-digit-sequence  hexadecimal-digit

floating-suffix（浮点后缀）:  选其一
    f  l  F  L
```

> 注：方括号 `[...]` 表示该部分是可选的（即标准中的 `opt`）。

---

## 专业术语说明

| 术语 | 含义 |
|------|------|
| `opt` | 表示该部分是可选的，可以省略 |
| `fractional-constant` | 指包含小数点的数字部分 |
| `exponent-part` | 十进制浮点数使用 `e` 或 `E` 表示以 10 为底的指数 |
| `binary-exponent-part` | 十六进制浮点数**必须**使用 `p` 或 `P` 表示以 2 为底的指数 |
| `floating-suffix` | `f`/`F` 表示 `float` 类型；`l`/`L` 表示 `long double` 类型；无后缀默认为 `double` |
