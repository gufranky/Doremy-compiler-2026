# SysY 语言定义（2022 版）

---

## 1. SysY 语言概要

SysY 语言是编译系统设计赛要实现的编程语言，由 C 语言的一个子集扩展而成。每个 SysY 程序的源码存储在一个扩展名为 `.sy` 的文件中。

### 基本特性

- 该文件中有且仅有一个名为 `main` 的主函数定义，还可以包含若干全局变量声明、常量声明和其他函数定义。
- **支持类型**：`int`（32 位有符号整数）、`float`（32 位单精度浮点数），以及元素为 `int`/`float` 类型且按行优先存储的多维数组类型。
- `const` 修饰符用于声明常量。
- 支持 `int` 和 `float` 之间的**隐式类型转换**，但不支持显式强制类型转换。
- I/O 通过运行时库函数提供，不是语言构造。

### 函数

- 可以带参数或不带参数，参数类型可以是 `int`/`float` 或数组类型。
- 可以返回 `int`/`float` 类型的值，或不返回值（声明为 `void`）。
- `int`/`float` 参数按值传递；数组参数传递起始地址，形参只有第一维长度可以空缺。

### 变量/常量声明

- 可以在一个声明语句中声明多个变量或常量，声明时可以带初始化表达式。
- 所有变量/常量要求先定义再使用。
- 函数外声明的为全局变量/常量，函数内声明的为局部变量/常量。

### 语句类型

赋值语句、表达式语句（表达式可以为空）、语句块、`if` 语句、`while` 语句、`break`、`continue`、`return`。

### 表达式

- 算术运算：`+`、`-`、`*`、`/`、`%`
- 关系运算：`==`、`!=`、`<`、`>`、`<=`、`>=`
- 逻辑运算：`!`、`&&`、`||`
- 非 0 表示真，0 表示假；关系/逻辑运算结果：1 表示真，0 表示假。
- 算符的优先级、结合性和计算规则（含逻辑运算的短路计算）与 C 语言一致。

> SysY2022 版相比 2020 版，在基本类型上增加了 `float` 类型，支持元素类型为 `float` 的多维数组。

---

## 2. SysY 语言的文法

文法采用扩展的 Backus 范式（EBNF）表示：

- `[...]`：方括号内的内容为可选项
- `{...}`：花括号内的内容可重复 0 次或多次
- 终结符：单引号括起的串，或 `Ident`、`IntConst`、`floatConst` 这样的记号

### 文法规则（CompUnit 为开始符号）

```
编译单元    CompUnit     → [ CompUnit ] ( Decl | FuncDef )
声明        Decl         → ConstDecl | VarDecl
常量声明    ConstDecl    → 'const' BType ConstDef { ',' ConstDef } ';'
基本类型    BType        → 'int' | 'float'
常数定义    ConstDef     → Ident { '[' ConstExp ']' } '=' ConstInitVal
常量初值    ConstInitVal → ConstExp
                         | '{' [ ConstInitVal { ',' ConstInitVal } ] '}'
变量声明    VarDecl      → BType VarDef { ',' VarDef } ';'
变量定义    VarDef       → Ident { '[' ConstExp ']' }
                         | Ident { '[' ConstExp ']' } '=' InitVal
变量初值    InitVal      → Exp | '{' [ InitVal { ',' InitVal } ] '}'
函数定义    FuncDef      → FuncType Ident '(' [FuncFParams] ')' Block
函数类型    FuncType     → 'void' | 'int' | 'float'
函数形参表  FuncFParams  → FuncFParam { ',' FuncFParam }
函数形参    FuncFParam   → BType Ident ['[' ']' { '[' Exp ']' }]
语句块      Block        → '{' { BlockItem } '}'
语句块项    BlockItem    → Decl | Stmt
语句        Stmt         → LVal '=' Exp ';'
                         | [Exp] ';'
                         | Block
                         | 'if' '(' Cond ')' Stmt [ 'else' Stmt ]
                         | 'while' '(' Cond ')' Stmt
                         | 'break' ';'
                         | 'continue' ';'
                         | 'return' [Exp] ';'
表达式      Exp          → AddExp                         -- 注：SysY 表达式是 int/float 型
条件表达式  Cond         → LOrExp
左值表达式  LVal         → Ident { '[' Exp ']' }
基本表达式  PrimaryExp   → '(' Exp ')' | LVal | Number
数值        Number       → IntConst | floatConst
一元表达式  UnaryExp     → PrimaryExp
                         | Ident '(' [FuncRParams] ')'
                         | UnaryOp UnaryExp
单目运算符  UnaryOp      → '+' | '-' | '!'               -- 注：'!' 仅出现在条件表达式中
函数实参表  FuncRParams  → Exp { ',' Exp }
乘除模表达式 MulExp      → UnaryExp | MulExp ('*' | '/' | '%') UnaryExp
加减表达式  AddExp       → MulExp | AddExp ('+' | '-') MulExp
关系表达式  RelExp       → AddExp | RelExp ('<' | '>' | '<=' | '>=') AddExp
相等性表达式 EqExp       → RelExp | EqExp ('==' | '!=') RelExp
逻辑与表达式 LAndExp     → EqExp | LAndExp '&&' EqExp
逻辑或表达式 LOrExp      → LAndExp | LOrExp '||' LAndExp
常量表达式  ConstExp     → AddExp                         -- 注：使用的 Ident 必须是常量
```

---

## 3. SysY 语言的终结符特征

### 3.1 标识符 Ident

```
identifier → identifier-nondigit
           | identifier identifier-nondigit
           | identifier digit
```

- `identifier-nondigit`：下划线 `_` 或大小写英文字母 `a-z`、`A-Z`
- `identifier-digit`：`0 1 2 3 4 5 6 7 8 9`

**同名标识符约定：**

- 全局变量和局部变量的作用域可以重叠，重叠部分局部变量优先；同名局部变量的作用域不能重叠。
- 变量名可以与函数名相同。

### 3.2 注释

与 C 语言一致：

- **单行注释**：以 `//` 开始，直到换行符结束（不包括换行符）。
- **多行注释**：以 `/*` 开始，直到第一次出现 `*/` 时结束（包括结束处的 `*/`）。

### 3.3 数值常量

#### 整型常量 IntConst

```
integer-const        → decimal-const | octal-const | hexadecimal-const
decimal-const        → nonzero-digit | decimal-const digit
octal-const          → 0 | octal-const octal-digit
hexadecimal-const    → hexadecimal-prefix hexadecimal-digit
                     | hexadecimal-const hexadecimal-digit
hexadecimal-prefix   → '0x' | '0X'
nonzero-digit        → 1 2 3 4 5 6 7 8 9
octal-digit          → 0 1 2 3 4 5 6 7
hexadecimal-digit    → 0-9  a-f  A-F
```

> 注：在 ISO/IEC 9899 整型常量定义的基础上，忽略所有后缀。

#### 浮点型常量 floatConst

参考 ISO/IEC 9899 第 57 页关于浮点常量的定义，在此基础上**忽略所有后缀**。
（详见 `C_Language_Floating_Constants_Standard.md`）

---

## 4. SysY 语言的语义约束

### CompUnit

```
编译单元  CompUnit → [ CompUnit ] ( Decl | FuncDef )
声明      Decl     → ConstDecl | VarDecl
```

1. 一个 SysY 程序由单个文件组成。在 CompUnit 中，必须存在且仅存在一个标识为 `'main'`、无参数、返回类型为 `int` 的函数定义。`main` 函数是程序的入口点，其返回结果需要输出。
2. CompUnit 的顶层变量/常量声明和函数定义都不可以重复定义同名标识符，即便类型不同也不允许。
3. CompUnit 的变量/常量/函数声明的作用域从该声明处开始到文件结尾。

### ConstDef

```
常数定义  ConstDef → Ident { '[' ConstExp ']' } '=' ConstInitVal
```

1. `ConstDef` 用于定义符号常量。`Ident` 后、`=` 之前是可选的数组维度和各维长度定义部分，`=` 之后是初始值。
2. 无数组维度部分时，表示定义单个变量，`=` 右边必须是单个初始数值。
3. 有数组维度部分时，表示定义数组，语义与 C 语言一致。各维长度的 `ConstExp` 都必须能在编译时求值到**非负整数**。注意：SysY 在声明数组时各维长度都需要显式给出，不允许未知。
4. 当 `ConstDef` 定义的是数组时，`=` 右边的 `ConstInitVal` 表示常量初始化器，其中 `ConstExp` 是能在编译时求值的 `int`/`float` 型表达式，可以引用已定义的符号常量。
5. `ConstInitVal` 初始化器必须是以下三种情况之一：
   - a) 一对花括号 `{}`，表示所有元素初始为 0。
   - b) 与多维数组维数和各维长度完全对应的初始值，例如 `{{1,2},{3,4},{5,6}}`、`{1,2,3,4,5,6}`、`{1,2,{3,4},5,6}` 均可作为 `a[3][2]` 的初始值。
   - c) 若花括号内初始值少于对应维的元素个数，则其余部分隐式初始化为 0。
   - d) 数组元素初值类型应与数组元素声明类型一致；但浮点型数组的初始化列表中可以出现整型常量或整型常量表达式。
   - e) 数组元素初值大小不能超出对应元素数据类型的表示范围。
   - f) 初始化列表中的元素个数不能超过数组声明时给出的总元素个数。

### VarDef

```
变量定义  VarDef → Ident { '[' ConstExp ']' }
                 | Ident { '[' ConstExp ']' } '=' InitVal
```

1. `VarDef` 用于定义变量。不含 `=` 和初始值时，运行时实际初值未定义。
2. 无数组维度部分时，表示定义单个变量；有时表示定义多维数组（参见 ConstDef 第 2 点）。
3. 含有 `=` 和初始值时，`InitVal` 的结构要求与 `ConstInitVal` 相同，唯一区别是 `InitVal` 中的表达式是当前上下文合法的任意 `Exp`（而非必须是 `ConstExp`）。
4. `VarDef` 中表示各维长度的 `ConstExp` 必须求值到**非负整数**；`InitVal` 中的初始值可以引用变量。

### 初值

```
常量初值  ConstInitVal → ConstExp
                       | '{' [ ConstInitVal { ',' ConstInitVal } ] '}'
变量初值  InitVal      → Exp | '{' [ InitVal { ',' InitVal } ] '}'
```

1. 全局变量声明中指定的初值表达式必须是常量表达式。
2. 常量或变量声明中指定的初值要与该常量或变量的类型一致。以下形式不满足 SysY 语义约束：
   ```
   a[4] = 4
   a[2] = {{1,2}, 3}
   a = {1,2,3}
   ```
3. 未显式初始化的局部变量，其值是不确定的；未显式初始化的全局变量，其（元素）值均被初始化为 `0` 或 `0.0`。

### FuncFParam 与实参

```
函数形参  FuncFParam → BType Ident ['[' ']' { '[' Exp ']' }]
```

1. `FuncFParam` 定义函数的一个形式参数。当 `Ident` 后面的可选部分存在时，表示数组定义。
2. 当 `FuncFParam` 为数组定义时，第一维的长度省去（用 `[]` 表示），后面各维需用表达式指明长度（整型常量）。
3. 对于 `int`/`float` 类型的参数，遵循按值传递；对于数组类型的参数，形参接收实参数组的地址。
4. 对于多维数组，可以传递其中的一部分到形参数组中。例如，若 `int a[4][3]`，则 `a[1]` 是含三个元素的一维数组，可以作为参数传递给类型为 `int[]` 的形参。

### FuncDef

```
函数定义  FuncDef → FuncType Ident '(' [FuncFParams] ')' Block
```

1. `FuncDef` 表示函数定义，`FuncType` 指明返回类型：
   - 返回类型为 `int`/`float` 时，函数内所有分支都应含有带 `Exp` 的 `return` 语句；不含 `return` 的分支返回值未定义。
   - 返回值类型为 `void` 时，函数内只能出现不带返回值的 `return` 语句。
2. 形参列表中每个形参声明用于声明 `int`/`float` 类型参数，或元素类型为 `int`/`float` 的多维数组。

### Block

1. `Block` 表示语句块，会创建作用域，其内声明的变量的生存期在该语句块内。
2. 语句块内可以再次定义与语句块外同名的变量或常量，其作用域从定义处开始到该语句块尾结束，它隐藏语句块外的同名变量或常量。

### Stmt

1. `Stmt` 中的 `if` 类型语句遵循**就近匹配**原则。
2. 单个 `Exp` 可以作为 `Stmt`，`Exp` 会被求值，所求的值会被丢弃。

### LVal

1. `LVal` 表示具有左值的表达式，可以为变量或某个数组元素。
2. 当 `LVal` 表示数组时，方括号个数必须和数组变量的维数相同（即定位到元素）。
3. 当 `LVal` 表示单个变量时，不能出现后面的方括号。

### Exp 与 Cond

1. `Exp` 代表 `int`/`float` 型表达式（定义为 `AddExp`），单目运算符中不出现 `!`；`Cond` 代表条件表达式（定义为 `LOrExp`），可以出现 `!`。当 `Exp` 作为数组维度时，必须是非负整数。
2. `LVal` 必须是当前作用域内、该 `Exp` 语句之前有定义的变量或常量；赋值号左边的 `LVal` 必须是变量。
3. 函数调用形式是 `Ident '(' FuncRParams ')'`，实际参数的类型和个数必须与函数定义的形参完全匹配。
4. SysY 中算符的优先级与结合性与 C 语言一致。

---

## 5. 隐式类型转换

SysY 语言的 `int` 和 `float` 类型之间有以下隐式类型转换：

1. **`float` → `int`**：小数部分将被丢弃（截断）。如果整数部分的值不在整型的表示范围，则行为是未定义的。
   ```c
   int i = 4.0;  // i == 4
   ```
2. **`int` → `float`**：转换后的值保持不变。
   ```c
   float j = 3;  // j == 3.0
   ```

> 注：编译器在实现隐式类型转换时，需要结合硬件体系结构提供的类型转换指令或运行时 ABI。例如，对于 ARM 架构，可以调用运行时 ABI 函数 `float __aeabi_i2f(int)` 来将 `int` 转换为 `float`。
> 参见 https://developer.arm.com/documentation/ihi0043/latest
