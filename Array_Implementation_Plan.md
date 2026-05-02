# SysY2022 数组功能实现方案

## 〇、测评环境说明

### 0.1 测试执行环境

测评使用 **WSL (Windows Subsystem for Linux)** 环境运行测试：

```python
# run_funct_tests.py 关键配置
DEFAULT_GCC_FLAGS = ["-march=rv64gc", "-mabi=lp64d", "-static"]
DEFAULT_WSL_DISTRO = "Ubuntu"
```

**关键点**：
- 目标架构：**RISC-V 64-bit** (`rv64gc`)
- ABI：`lp64d`（64位 long/pointer，双精度浮点 ABI）
- QEMU 模拟器：`qemu-riscv64`
- GCC 交叉编译器：`riscv64-linux-gnu-gcc`

### 0.2 测评流程

```
┌─────────────────────────────────────────────────────────────┐
│  1. 编译器读取 .sy 源文件                                    │
│     ./compiler < test.sy > test.s                           │
├─────────────────────────────────────────────────────────────┤
│  2. GCC 链接运行时库生成可执行文件                           │
│     riscv64-linux-gnu-gcc -march=rv64gc -mabi=lp64d \       │
│         -static test.s libsysy_riscv.a -o test              │
├─────────────────────────────────────────────────────────────┤
│  3. QEMU 运行程序                                           │
│     qemu-riscv64 ./test < test.in                           │
├─────────────────────────────────────────────────────────────┤
│  4. 对比输出                                                │
│     stdout + return_code vs test.out                        │
└─────────────────────────────────────────────────────────────┘
```

### 0.3 运行时库函数

| 函数 | 功能 | 数组相关 |
|------|------|----------|
| `getint()` | 读取整数 | - |
| `getch()` | 读取字符 | - |
| `getfloat()` | 读取浮点数 | - |
| `getarray(int a[])` | 读取整数数组 | ✅ |
| `getfarray(float a[])` | 读取浮点数组 | ✅ |
| `putint(int)` | 输出整数 | - |
| `putch(int)` | 输出字符 | - |
| `putfloat(float)` | 输出浮点数 | - |
| `putarray(int n, int a[])` | 输出整数数组 | ✅ |
| `putfarray(int n, float a[])` | 输出浮点数组 | ✅ |
| `putf(char*, ...)` | 格式化输出 | - |
| `starttime()` / `stoptime()` | 性能计时 | - |

### 0.4 现有后端架构问题

⚠️ **当前后端生成 RV32 指令，但测评环境需要 RV64！**

需要确认或修改：
1. 寄存器宽度：x0-x31 仍为 64 位
2. 指令区别：`lw`/`sw` vs `ld`/`sd`
3. 浮点指令：`flw`/`fsw` 仍可用于 32 位浮点
4. 地址计算：指针为 64 位

**测试命令**：
```bash
# 在 WSL 中运行
python run_funct_tests.py --mode full --compiler ./compiler
```

---

### 0.5 快速测试方法

```bash
# 编译编译器
make clean && make

# 运行单个测试（compile 模式，只检查编译通过）
python run_funct_tests.py --mode compile --limit 5

# 运行完整测试（full 模式，编译+链接+执行）
python run_funct_tests.py --mode full --limit 5

# 运行全部测试
python run_funct_tests.py --mode full

# 启用优化
python run_funct_tests.py --mode full --opt

# 静默模式，只显示失败用例
python run_funct_tests.py --mode full --quiet
```

### 0.6 测试用例格式

每个测试用例包含三个文件：
- `XX_name.sy` - SysY 源代码
- `XX_name.out` - 期望输出（stdout + 最后一行为 return code）
- `XX_name.in` - 输入数据（如有）

---

## 一、概述

本文档详细描述 SysY2022 编译器的数组功能实现方案，包括词法分析、语法分析、语义分析、中间表示和代码生成各阶段的设计与实现。

### 1.1 目标功能

- 一维数组声明与初始化：`int a[10];`, `int a[3] = {1, 2, 3};`
- 多维数组声明与初始化：`int a[5][3];`, `int a[2][3] = {{1,2,3}, {4,5,6}};`
- 数组元素访问：`a[i]`, `a[i][j]`
- 数组元素赋值：`a[i] = 5;`, `a[i][j] = 10;`
- 数组参数传递：`void func(int a[], int b[][3]);`
- float 类型数组：`float fa[10];`

### 1.2 设计原则

1. **最小侵入性**：尽量复用现有架构，减少对已有代码的修改
2. **类型安全**：在语义分析阶段进行完整的类型检查
3. **可扩展性**：为后续优化留出空间

---

## 二、现有架构分析

### 2.1 当前类型系统

```cpp
// ast.h
struct Type {
  BaseType base = BaseType::INVALID;  // INT, FLOAT, VOID
  bool isConst = false;
  bool isArray = false;                // 已有字段，但未使用
  std::vector<int> arrayDimensions;    // 已有字段，存储各维长度
  bool firstDimUnsized = false;        // 用于函数参数
};
```

**结论**：类型系统已为数组预留字段，可直接使用。

### 2.2 当前 VarDef 类

```cpp
// ast.h
class VarDef {
 public:
  std::string name;
  std::unique_ptr<Expr> initExpr;
  bool hasInit = false;
  bool initIsConst = false;
  int constInitValue = 0;
  ScalarValue typedConstInitValue = ScalarValue::Int(0);
  bool isArray = false;                // 已有字段
  std::vector<int> arrayDimensions;    // 已有字段
  bool firstDimUnsized = false;        // 已有字段
};
```

**结论**：`VarDef` 已有数组相关字段，需要扩展以支持初始化列表。

### 2.3 需要新增的 AST 节点

| 节点 | 用途 | 优先级 |
|------|------|--------|
| `ArrayAccessExpr` | 数组元素访问表达式 `a[i][j]` | 高 |
| `InitList` | 初始化列表 `{1, 2, {3, 4}}` | 高 |
| `ArrayParam` | 数组参数（可选，可复用 Param） | 中 |

---

## 三、详细设计方案

### 3.1 AST 节点设计

#### 3.1.1 数组访问表达式

```cpp
// ast.h - 新增节点

class ArrayAccessExpr : public Expr {
 public:
  std::unique_ptr<Expr> array;     // 可以是 IdentifierExpr 或嵌套的 ArrayAccessExpr
  std::unique_ptr<Expr> index;     // 索引表达式

  ArrayAccessExpr(Expr* arr, Expr* idx) : array(arr), index(idx) {}

  void accept(ASTVisitor* visitor) override;
};
```

**设计说明**：
- 采用递归结构，`a[i][j]` 表示为 `ArrayAccessExpr(ArrayAccessExpr(a, i), j)`
- 便于处理任意维度的数组访问

#### 3.1.2 初始化列表

```cpp
// ast.h - 新增节点

class InitList : public ASTNode {
 public:
  std::vector<std::unique_ptr<ASTNode>> elements;  // 可以是 Expr 或 InitList

  bool isScalar = false;  // 标记是否为单一表达式（非列表）

  void accept(ASTVisitor* visitor) override;
};

// 修改 VarDef 类
class VarDef {
 public:
  std::string name;
  std::unique_ptr<Expr> initExpr;        // 标量初始化（保留兼容）
  std::unique_ptr<InitList> initList;    // 数组初始化列表（新增）
  bool hasInit = false;
  bool hasInitList = false;              // 新增：区分标量初始化和列表初始化
  // ... 其他字段
};
```

#### 3.1.3 修改 AssignStmt

```cpp
// ast.h - 修改赋值语句

class AssignStmt : public Stmt {
 public:
  std::unique_ptr<Expr> lvalue;    // 改为通用左值表达式
  std::unique_ptr<Expr> value;     // 右值表达式

  // 构造函数
  AssignStmt(Expr* lv, Expr* val) : lvalue(lv), value(val) {}
  void accept(ASTVisitor* visitor) override;
};
```

#### 3.1.4 修改 Param 类

```cpp
// ast.h - 修改参数类

class Param {
 public:
  Type type;
  std::string name;
  // type.isArray 和 type.arrayDimensions 已足够表达数组参数
  // type.firstDimUnsized 用于标记第一维是否省略
};
```

---

### 3.2 语法规则扩展

#### 3.2.1 文法修改

```
// 原文法
VarDef     → Ident { '[' ConstExp ']' } ['=' InitVal]

// 扩展后（与 SysY2022 一致）
VarDef     → Ident { '[' ConstExp ']' }
           | Ident { '[' ConstExp ']' } '=' InitVal

ConstDef   → Ident { '[' ConstExp ']' } '=' ConstInitVal

InitVal    → Exp | '{' [ InitVal { ',' InitVal } ] '}'
ConstInitVal → ConstExp | '{' [ ConstInitVal { ',' ConstInitVal } ] '}'

LVal       → Ident { '[' Exp ']' }

FuncFParam → BType Ident ['[' ']' { '[' Exp ']' }]
```

#### 3.2.2 Parser 修改要点

```yacc
// parser.y 修改片段

// 数组维度
ArrayDims
    : /* empty */
    | ArrayDims '[' ConstExpr ']'
    ;

// 初始化值
InitVal
    : Expr {
        $$ = new InitList();
        $$->isScalar = true;
        $$->elements.emplace_back($1);
    }
    | '{' '}' {
        $$ = new InitList();
        $$->isScalar = false;
    }
    | '{' InitValList '}' {
        $$ = $2;
    }
    ;

InitValList
    : InitVal {
        $$ = new InitList();
        $$->elements.emplace_back($1);
    }
    | InitValList ',' InitVal {
        $$ = $1;
        $$->elements.emplace_back($3);
    }
    ;

// 变量定义（支持数组）
VarDef
    : IDENTIFIER ArrayDims {
        $$ = new VarDef(*$1);
        // 解析 ArrayDims 设置 arrayDimensions
    }
    | IDENTIFIER ArrayDims '=' InitVal {
        $$ = new VarDef(*$1);
        // 设置维度和初始化
    }
    ;

// 左值（支持数组访问）
LVal
    : IDENTIFIER {
        $$ = new IdentifierExpr(*$1);
    }
    | LVal '[' Expr ']' {
        $$ = new ArrayAccessExpr($1, $3);
    }
    ;

// 赋值语句
AssignStmt
    : LVal '=' Expr ';' {
        $$ = new AssignStmt($1, $3);
    }
    ;

// 函数参数（支持数组参数）
FuncFParam
    : BType IDENTIFIER {
        $$ = new Param(*$1, *$2);
    }
    | BType IDENTIFIER '[' ']' {
        $$ = new Param(*$1, *$2);
        $$->type.isArray = true;
        $$->type.firstDimUnsized = true;
    }
    | BType IDENTIFIER '[' ']' ArrayDimsExplicit {
        $$ = new Param(*$1, *$2);
        $$->type.isArray = true;
        $$->type.firstDimUnsized = true;
        // 设置后续维度
    }
    ;

ArrayDimsExplicit
    : '[' Expr ']'
    | ArrayDimsExplicit '[' Expr ']'
    ;
```

---

### 3.3 语义分析设计

#### 3.3.1 数组类型检查

```cpp
// semantic_analyzer.cpp 新增函数

// 检查数组维度是否为常量且非负
bool SemanticAnalyzer::validateArrayDimensions(
    const std::vector<Expr*>& dims,
    std::vector<int>* dimValues);

// 检查初始化列表与数组类型是否匹配
bool SemanticAnalyzer::validateInitList(
    InitList* initList,
    const Type& arrayType,
    std::vector<ScalarValue>* flattenedValues);

// 扁平化初始化列表
void SemanticAnalyzer::flattenInitList(
    InitList* initList,
    const Type& arrayType,
    std::vector<ScalarValue>& result,
    int& index);
```

#### 3.3.2 数组访问类型推导

```cpp
// 访问 ArrayAccessExpr
void SemanticAnalyzer::visit(ArrayAccessExpr* node) {
  node->array->accept(this);

  Type arrayType = lastExprType;
  if (!arrayType.isArray) {
    addError("Subscript requires array type");
    setExprResult(Type::Invalid());
    return;
  }

  // 检查索引类型
  node->index->accept(this);
  if (!isIntType(lastExprType)) {
    addError("Array index must be integer");
  }

  // 计算结果类型（减少一个维度）
  Type resultType = arrayType;
  resultType.isArray = arrayType.arrayDimensions.size() > 1;
  if (resultType.isArray) {
    resultType.arrayDimensions.erase(resultType.arrayDimensions.begin());
  }

  setExprResult(resultType);
}
```

#### 3.3.3 初始化列表验证

```cpp
// 验证初始化列表
bool SemanticAnalyzer::validateInitList(
    InitList* initList,
    const Type& arrayType,
    std::vector<ScalarValue>* flattenedValues) {

  int totalSize = 1;
  for (int dim : arrayType.arrayDimensions) {
    totalSize *= dim;
  }

  flattenedValues->clear();
  flattenedValues->reserve(totalSize);

  // 递归展开初始化列表
  int index = 0;
  return flattenInitListRecursive(
      initList, arrayType, 0,
      *flattenedValues, index, totalSize);
}

bool SemanticAnalyzer::flattenInitListRecursive(
    InitList* initList,
    const Type& arrayType,
    int currentDim,
    std::vector<ScalarValue>& result,
    int& index,
    int totalSize) {

  if (initList->isScalar) {
    // 单个表达式
    Expr* expr = dynamic_cast<Expr*>(initList->elements[0].get());
    expr->accept(this);

    ScalarValue value;
    if (!evalConstExpr(expr, &value)) {
      return false;  // 非常量
    }

    if (isFloatType(arrayType)) {
      value = castConstValue(value, arrayType);
    }

    result.push_back(value);
    index++;
    return true;
  }

  // 列表形式
  int expectedCount = arrayType.arrayDimensions[currentDim];

  if (initList->elements.empty()) {
    // 空列表 {}，填充 0
    int remaining = totalSize - index;
    for (int i = 0; i < remaining; ++i) {
      result.push_back(isFloatType(arrayType) ?
                       ScalarValue::Float(0.0f) : ScalarValue::Int(0));
    }
    index = totalSize;
    return true;
  }

  for (auto& elem : initList->elements) {
    if (index >= totalSize) {
      addError("Too many initializers");
      return false;
    }

    if (auto* subList = dynamic_cast<InitList*>(elem.get())) {
      if (!flattenInitListRecursive(
              subList, arrayType, currentDim + 1, result, index, totalSize)) {
        return false;
      }
    } else if (auto* expr = dynamic_cast<Expr*>(elem.get())) {
      expr->accept(this);
      ScalarValue value;
      bool isConst = evalConstExpr(expr, &value);
      if (isFloatType(arrayType)) {
        value = castConstValue(value, arrayType);
      }
      result.push_back(value);
      index++;
    }
  }

  return true;
}
```

#### 3.3.4 左值检查

```cpp
// 检查左值是否可赋值
bool SemanticAnalyzer::isAssignableLValue(Expr* expr) {
  if (auto* id = dynamic_cast<IdentifierExpr*>(expr)) {
    Symbol* sym = symbolTable.lookupValue(id->name);
    return sym && sym->kind != SymbolKind::CONSTANT;
  }
  if (auto* arr = dynamic_cast<ArrayAccessExpr*>(expr)) {
    return isAssignableLValue(arr->array.get());
  }
  return false;
}
```

---

### 3.4 中间表示设计

#### 3.4.1 IR 类型扩展

```cpp
// ir.h 扩展

struct Operand {
  // ... 现有字段

  // 新增：数组元素地址
  static Operand ArrayElement(int baseVReg, int offsetVReg,
                              ValueType type = ValueType::I32);
};

// 新增指令类型
enum class InstKind {
  // ... 现有类型
  ArrayAddr,    // 计算数组元素地址
  MemCopy,      // 内存拷贝（用于数组初始化）
};

// 数组地址计算指令
struct ArrayAddrInst : public Instruction {
  int dest;                  // 结果虚拟寄存器（存放地址）
  Operand base;              // 数组基址（全局变量名或虚拟寄存器）
  std::vector<Operand> indices;  // 各维索引
  std::vector<int> dimensions;   // 各维大小
  ValueType elementType;     // 元素类型

  ArrayAddrInst(int dest, Operand base, std::vector<Operand> indices,
                std::vector<int> dims, ValueType elemType)
      : Instruction(InstKind::ArrayAddr),
        dest(dest), base(std::move(base)), indices(std::move(indices)),
        dimensions(std::move(dims)), elementType(elemType) {}
};

// 全局数组定义
struct GlobalArray {
  std::string name;
  std::vector<int> dimensions;
  ValueType elementType;
  std::vector<ScalarValue> initialValues;  // 扁平化的初始值
  bool isConst;
};

// 修改 IRProgram
struct IRProgram {
  std::vector<GlobalVar> globals;       // 标量全局变量
  std::vector<GlobalArray> globalArrays; // 新增：全局数组
  std::vector<IRFunction> functions;
};
```

#### 3.4.2 数组访问 IR 生成

```cpp
// ir_generator.cpp 新增

Operand IRGenerator::genArrayAccess(ArrayAccessExpr* node) {
  // 获取数组基址
  Operand baseAddr = getArrayBaseAddress(node);

  // 收集所有索引
  std::vector<Operand> indices;
  std::vector<int> dimensions;
  collectArrayIndices(node, indices, dimensions);

  // 计算元素地址
  int addrReg = newVReg();
  current_->append<ArrayAddrInst>(
      addrReg, baseAddr, std::move(indices),
      std::move(dimensions), toIRValueType(elementType));

  // 加载元素值
  int valueReg = newVReg();
  current_->append<LoadInst>(valueReg, Operand::VReg(addrReg));

  return Operand::VReg(valueReg);
}

void IRGenerator::collectArrayIndices(
    ArrayAccessExpr* node,
    std::vector<Operand>& indices,
    std::vector<int>& dimensions) {

  // 递归收集索引和维度
  if (auto* nested = dynamic_cast<ArrayAccessExpr*>(node->array.get())) {
    collectArrayIndices(nested, indices, dimensions);
  }

  indices.push_back(genExpr(node->index.get()));

  // 从符号表获取维度信息
  // ...
}
```

#### 3.4.3 数组初始化 IR

```cpp
// 局部数组初始化
void IRGenerator::emitLocalArrayInit(
    const std::string& name,
    const Type& arrayType,
    const std::vector<ScalarValue>& initValues) {

  int totalSize = 1;
  for (int dim : arrayType.arrayDimensions) {
    totalSize *= dim;
  }

  // 在栈上分配空间
  int baseAddr = newVReg();
  // ... 分配栈空间

  // 存储初始值
  for (int i = 0; i < totalSize; ++i) {
    int addrReg = newVReg();
    current_->append<ArrayAddrInst>(...);

    int valueReg = newVReg();
    current_->append<CopyInst>(valueReg, Operand::Imm(initValues[i]));

    current_->append<StoreInst>(Operand::VReg(valueReg),
                                Operand::VReg(addrReg));
  }
}
```

---

### 3.5 后端代码生成设计

#### 3.5.1 数组地址计算

对于 `a[i][j]`，地址计算公式：
```
addr = base + ((i * dim2 + j) * element_size)
```

```cpp
// codegen.cpp 新增

void CodeGen::emitArrayAddrCalculation(
    const ArrayAddrInst* inst,
    const StackFrame& frame,
    std::vector<std::string>& out) {

  std::string baseReg = getOperandReg(inst->base, frame);

  // 计算线性偏移
  std::string offsetReg = "t0";
  emitLoadImmediate(offsetReg, 0, out);

  int multiplier = 1;
  for (int i = inst->dimensions.size() - 1; i >= 0; --i) {
    std::string idxReg = getOperandReg(inst->indices[i], frame);

    // offset = offset * dim[i] + index[i]
    if (multiplier > 1) {
      out.push_back("\tli t1, " + std::to_string(multiplier));
      out.push_back("\tmul " + offsetReg + ", " + offsetReg + ", t1");
    }
    out.push_back("\tadd " + offsetReg + ", " + offsetReg + ", " + idxReg);

    multiplier *= inst->dimensions[i];
  }

  // 乘以元素大小（4 字节）
  out.push_back("\tslli " + offsetReg + ", " + offsetReg + ", 2");

  // 加上基址
  std::string destReg = getPhysicalReg(inst->dest, frame);
  out.push_back("\tadd " + destReg + ", " + baseReg + ", " + offsetReg);
}
```

#### 3.5.2 全局数组存储

```cpp
// codegen.cpp 全局数组输出

void CodeGen::emitGlobalArrays(const IRProgram& program,
                                std::vector<std::string>& out) {
  for (const auto& arr : program.globalArrays) {
    out.push_back(".globl " + arr.name);
    out.push_back(arr.name + ":");

    // 计算总大小
    int totalSize = 1;
    for (int dim : arr.dimensions) {
      totalSize *= dim;
    }

    // 输出初始化数据
    if (arr.initialValues.empty()) {
      // 零初始化
      out.push_back("\t.zero " + std::to_string(totalSize * 4));
    } else {
      // 逐元素输出
      for (const auto& val : arr.initialValues) {
        if (arr.elementType == ValueType::F32) {
          out.push_back("\t.word " + std::to_string(floatBits(val.floatValue)));
        } else {
          out.push_back("\t.word " + std::to_string(val.intValue));
        }
      }
    }
    out.push_back("");
  }
}
```

#### 3.5.3 数组参数传递

```cpp
// 数组参数传递（传递地址）

void CodeGen::emitArrayParamPassing(
    const CallInst* call,
    int paramIndex,
    const Type& paramType,
    std::vector<std::string>& out) {

  if (paramType.isArray) {
    // 数组参数：传递基地址
    Operand arrayArg = call->args[paramIndex];
    std::string addrReg = getOperandReg(arrayArg, frame);

    // 放入对应的参数寄存器或栈
    if (paramIndex < 8) {
      std::string argReg = "a" + std::to_string(paramIndex);
      if (addrReg != argReg) {
        out.push_back("\taddi " + argReg + ", " + addrReg + ", 0");
      }
    } else {
      // 存入栈
      emitStackStore(addrReg, stackArgOffset, out);
    }
  }
}
```

---

### 3.6 符号表扩展

```cpp
// symbol_table.h 扩展

struct Symbol {
  std::string name;
  SymbolKind kind;
  Type type;

  // 数组相关信息
  bool isArray = false;
  std::vector<int> arrayDimensions;

  // 存储位置
  bool isGlobal = false;
  bool isParameter = false;

  // 栈偏移（局部数组）
  int stackOffset = 0;      // 数组在栈上的起始偏移
  int stackSize = 0;        // 数组占用的栈空间大小

  // 全局数组名
  std::string globalName;

  // 常量值（常量数组初始化值）
  bool hasConstValues = false;
  std::vector<ScalarValue> constValues;
};
```

---

## 四、实现计划

### 4.1 阶段一：基础数组支持（高优先级）

| 任务 | 文件 | 预计工作量 |
|------|------|------------|
| 1.1 添加 `ArrayAccessExpr` AST 节点 | `ast.h`, `ast.cpp` | 0.5 天 |
| 1.2 添加 `InitList` AST 节点 | `ast.h`, `ast.cpp` | 0.5 天 |
| 1.3 修改词法/语法分析器支持数组维度 | `lexer.l`, `parser.y` | 1 天 |
| 1.4 修改语法分析器支持初始化列表 | `parser.y` | 1 天 |
| 1.5 修改语义分析器处理数组声明 | `semantic_analyzer.cpp` | 1 天 |
| 1.6 修改语义分析器处理数组访问 | `semantic_analyzer.cpp` | 0.5 天 |

**阶段一目标**：能编译 `int a[10]; a[5] = 3; return a[5];`

### 4.2 阶段二：数组初始化（高优先级）

| 任务 | 文件 | 预计工作量 |
|------|------|------------|
| 2.1 实现初始化列表语义检查 | `semantic_analyzer.cpp` | 1 天 |
| 2.2 实现初始化列表扁平化 | `semantic_analyzer.cpp` | 0.5 天 |
| 2.3 添加 IR 全局数组支持 | `ir.h`, `ir_generator.cpp` | 1 天 |
| 2.4 添加 IR 数组初始化指令 | `ir.h`, `ir_generator.cpp` | 1 天 |

**阶段二目标**：能编译 `int a[3] = {1, 2, 3};`

### 4.3 阶段三：多维数组（中优先级）

| 任务 | 文件 | 预计工作量 |
|------|------|------------|
| 3.1 多维数组声明支持 | `semantic_analyzer.cpp` | 0.5 天 |
| 3.2 多维数组访问 IR 生成 | `ir_generator.cpp` | 1 天 |
| 3.3 多维数组地址计算代码生成 | `codegen.cpp` | 1 天 |
| 3.4 嵌套初始化列表处理 | `semantic_analyzer.cpp` | 1 天 |

**阶段三目标**：能编译 `int a[2][3] = {{1,2,3}, {4,5,6}}; return a[1][2];`

### 4.4 阶段四：数组参数（中优先级）

| 任务 | 文件 | 预计工作量 |
|------|------|------------|
| 4.1 语法分析器支持数组参数 | `parser.y` | 0.5 天 |
| 4.2 语义分析器处理数组参数 | `semantic_analyzer.cpp` | 0.5 天 |
| 4.3 IR 数组参数传递 | `ir_generator.cpp` | 1 天 |
| 4.4 后端数组参数处理 | `codegen.cpp` | 1 天 |

**阶段四目标**：能编译 `void f(int a[]) { ... }`

### 4.5 阶段五：完善与测试（必须）

| 任务 | 文件 | 预计工作量 |
|------|------|------------|
| 5.1 单元测试编写 | `tests/` | 1 天 |
| 5.2 集成测试 | - | 1 天 |
| 5.3 Bug 修复 | - | 1 天 |

---

## 五、测试用例

### 5.1 基础测试

```c
// test_array_basic.sy
int main() {
    int a[10];
    a[5] = 42;
    return a[5];  // 期望返回 42
}
```

### 5.2 初始化测试

```c
// test_array_init.sy
int main() {
    int a[5] = {1, 2, 3, 4, 5};
    return a[2];  // 期望返回 3
}
```

### 5.3 多维数组测试

```c
// test_array_2d.sy
int main() {
    int a[2][3] = {{1, 2, 3}, {4, 5, 6}};
    a[1][2] = 10;
    return a[1][2];  // 期望返回 10
}
```

### 5.4 数组参数测试

```c
// test_array_param.sy
int sum(int a[], int n) {
    int s = 0;
    int i = 0;
    while (i < n) {
        s = s + a[i];
        i = i + 1;
    }
    return s;
}

int main() {
    int a[5] = {1, 2, 3, 4, 5};
    return sum(a, 5);  // 期望返回 15
}
```

### 5.5 全局数组测试

```c
// test_global_array.sy
int g[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};

int main() {
    return g[5];  // 期望返回 6
}
```

### 5.6 float 数组测试

```c
// test_float_array.sy
int main() {
    float fa[3] = {1.5, 2.5, 3.5};
    return fa[1];  // 期望返回 2（截断）
}
```

---

## 六、风险与注意事项

### 6.1 技术风险

| 风险 | 影响 | 缓解措施 |
|------|------|----------|
| 栈空间不足（大数组） | 运行时错误 | 限制数组大小或实现堆分配 |
| 数组越界访问 | 结果错误 | 文档说明不检查越界 |
| 初始化列表复杂嵌套 | 实现复杂度 | 分阶段实现，先支持简单情况 |

### 6.2 兼容性考虑

- 保持与现有代码的兼容性
- 标量变量声明逻辑不变
- 非数组代码路径不受影响

### 6.3 性能考虑

- 数组访问避免重复计算地址
- 常量索引可优化为直接偏移
- 初始化列表可在编译期完成

---

## 七、参考资料

1. SysY2022 语言定义 (`SysY2022_Language_Definition.md`)
2. 现有 AST 设计 (`include/ast.h`)
3. 现有 IR 设计 (`include/ir.h`)
4. 现有后端实现 (`src/backend/codegen.cpp`)
5. GCC/Clang 数组实现参考

---

## 八、修订历史

| 版本 | 日期 | 修改内容 |
|------|------|----------|
| 1.0 | 2026-05-02 | 初始版本 |
