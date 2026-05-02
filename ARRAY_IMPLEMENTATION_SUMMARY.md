# 数组功能实现总结

## 概述

本次更新实现了 SysY2022 编译器的数组参数传递功能，并修复了局部数组初始化的问题。

## 测试成绩

| 指标 | 基线 | 实现后 | 提升 |
|------|------|--------|------|
| 编译通过率 | 47.86% | **82.86% (116/140)** | +35.00% |
| 执行通过率 | 47.86% | **72.86% (102/140)** | +25.00% |

## 已实现的数组功能

| 功能 | 状态 | 说明 |
|------|------|------|
| 数组声明 `int a[10]` | ✅ | 支持局部和全局数组 |
| 多维数组 `int a[5][3]` | ✅ | 支持任意维度 |
| 数组初始化 | ✅ | 支持嵌套/扁平形式混合，支持非常量表达式 |
| 数组元素访问 `a[i]` | ✅ | 支持多维索引 |
| 数组参数传递 | ✅ | 新实现 |
| 数组参数访问 | ✅ | 新实现 |
| 全局数组传参 | ✅ | 新实现 |
| 局部数组传参 | ✅ | 新实现 |
| `getarray`/`putarray` | ✅ | 新声明内置函数 |
| `getfarray`/`putfarray` | ✅ | 新声明内置函数 |

## 主要修改文件

### 1. 语义分析器 (`include/semantic_analyzer.h`, `src/frontend/semantic_analyzer.cpp`)

#### 新增函数声明
```cpp
bool isParamCompatible(const Type& argType, const Type& paramType) const;
```

#### 修改 `canImplicitlyConvert` 函数
支持数组类型的匹配判断：
```cpp
bool SemanticAnalyzer::canImplicitlyConvert(const Type& from, const Type& to) const {
  // 数组类型：需要类型匹配
  if (from.isArray && to.isArray) {
    return from.base == to.base;
  }
  // 标量类型：可以隐式转换
  if (!isNumericType(from) || !isNumericType(to)) return false;
  return true;
}
```

#### 新增 `isParamCompatible` 函数
检查函数调用时参数类型是否兼容：
```cpp
bool SemanticAnalyzer::isParamCompatible(const Type& argType, const Type& paramType) const {
  if (paramType.isArray) {
    if (!argType.isArray) return false;
    return argType.base == paramType.base;
  }
  if (argType.isArray) return false;
  return isNumericType(argType) && isNumericType(paramType);
}
```

#### 修改 `visit(FunctionCallExpr*)` 函数
使用 `isParamCompatible` 进行参数检查，支持数组参数：
```cpp
for (size_t i = 0; i < node->args.size(); ++i) {
  node->args[i]->accept(this);
  if (i < func->paramTypes.size()) {
    if (!isParamCompatible(lastExprType, func->paramTypes[i])) {
      // 错误处理...
    }
  }
}
```

#### 新增内置函数声明
```cpp
void SemanticAnalyzer::declareBuiltinFunctions() {
  // ... 原有内置函数 ...

  // 数组相关内置函数
  Type intArrayParam = Type::Int();
  intArrayParam.isArray = true;
  intArrayParam.firstDimUnsized = true;

  Type floatArrayParam = Type::Float();
  floatArrayParam.isArray = true;
  floatArrayParam.firstDimUnsized = true;

  symbolTable.declareFunction("getarray", Type::Int(), {intArrayParam});
  symbolTable.declareFunction("getfarray", Type::Int(), {floatArrayParam});
  symbolTable.declareFunction("putarray", Type::Void(), {Type::Int(), intArrayParam});
  symbolTable.declareFunction("putfarray", Type::Void(), {Type::Int(), floatArrayParam});
}
```

### 2. IR 生成器 (`include/ir_generator.h`, `src/ir/ir_generator.cpp`)

#### `ValueBinding` 结构体新增字段
```cpp
struct ValueBinding {
  // ... 原有字段 ...
  bool isArrayParam = false;  // 标记是否为数组参数
};
```

#### 函数参数处理
区分数组参数和标量参数：
```cpp
for (auto& paramPtr : func->params) {
  int reg = newVReg();
  ValueBinding binding;
  binding.type = paramPtr->type;
  binding.isArray = paramPtr->type.isArray;
  binding.isArrayParam = paramPtr->type.isArray;  // 标记数组参数
  binding.arrayDimensions = paramPtr->type.arrayDimensions;
  // ...
}
```

#### 数组标识符处理 (`genExprResult` 中)
正确返回数组地址用于参数传递：
```cpp
if (binding->isArray) {
  ExprResult result;
  result.type = binding->type.withoutConst();
  result.isArrayAccess = true;
  if (binding->isGlobal) {
    result.operand = Operand::Global(binding->globalName, ValueType::I32);
    result.addrOperand = Operand::Global(binding->globalName, ValueType::I32);
  } else if (binding->isArrayParam) {
    // 数组参数：vreg 存储传入的数组地址
    result.operand = Operand::VReg(binding->vreg, ValueType::I32);
    result.addrOperand = Operand::VReg(binding->vreg, ValueType::I32);
  } else {
    // 局部数组：计算栈地址
    // ...
  }
  return result;
}
```

#### 函数调用参数处理
正确区分数组地址传递和标量值传递：
```cpp
for (size_t i = 0; i < call->args.size(); ++i) {
  ExprResult arg = genExprResult(call->args[i].get());

  bool paramIsArray = false;
  if (sigIt != functions_.end() && i < sigIt->second.paramTypes.size()) {
    paramIsArray = sigIt->second.paramTypes[i].isArray;
  }

  if (arg.isArrayAccess && paramIsArray) {
    // 传递数组地址
    args.emplace_back(arg.addrOperand);
    argTypes.push_back(ValueType::I32);
  } else {
    // 标量参数：使用加载后的值
    // ...
    args.emplace_back(arg.operand);
    argTypes.push_back(toIRValueType(arg.type));
  }
}
```

#### 左值表达式处理 (`genLValueExpr`)
正确处理数组参数的地址：
```cpp
if (binding->isArray) {
  result.isArrayAccess = true;
  if (binding->isGlobal) {
    result.addrOperand = Operand::Global(binding->globalName, ValueType::I32);
  } else if (binding->isArrayParam) {
    // 数组参数：vreg 存储传入的数组地址
    result.addrOperand = Operand::VReg(binding->vreg, ValueType::I32);
  } else {
    // 局部数组
    result.addrOperand = Operand::LocalVarAddr(binding->stackOffset);
  }
}
```

### 3. 局部数组初始化修复

#### 问题
局部数组只初始化了初始化列表中指定的元素，其余元素未初始化为 0。

#### 解决方案
首先将整个数组清零，然后存储初始化列表中的值：
```cpp
// 首先将整个数组初始化为 0
for (int i = 0; i < arraySize; ++i) {
  int offset = binding.stackOffset + i * 4;
  Operand addrOp = Operand::LocalVarAddr(offset);
  int valReg = newVReg();
  current_->append<CopyInst>(ValueType::I32, valReg, Operand::Imm(0));
  current_->append<StoreInst>(Operand::VReg(valReg, ValueType::I32), addrOp);
}

// 然后存储初始化列表中的值
if (def->hasInitList && def->initList) {
  // ...
}
```

## 示例代码

### 数组参数传递
```c
void print_arr(int x[]) {
  putint(x[0]);
  putch(10);
}

int main() {
  int a[3] = {1, 2, 3};
  print_arr(a);  // 输出: 1
  return 0;
}
```

### 数组元素修改
```c
void swap(int arr[], int i, int j) {
  int tmp = arr[i];
  arr[i] = arr[j];
  arr[j] = tmp;
}

int main() {
  int a[3] = {1, 2, 3};
  swap(a, 0, 2);
  // a 现在是 {3, 2, 1}
  return 0;
}
```

### 内置函数使用
```c
int main() {
  int n;
  int a[100];
  n = getarray(a);  // 读取数组，返回元素个数
  putarray(n, a);   // 输出数组
  return 0;
}
```

## 遗留问题

### 后端寄存器分配 Bug

**问题描述**：在某些复杂函数中（特别是有递归调用的函数），当一个变量在后续代码中仍被使用时，其寄存器可能被其他变量覆盖。

**影响用例**：
- `38_light2d`
- `39_fp_params`
- `57_sort_test3`
- `59_sort_test5`
- `66_exgcd`
- `69_expr_eval`
- `74_kmp`
- `85_long_code`
- `87_many_params`

**示例**：
```c
int quicksort(int arr[], int low, int high) {
  if (low < high) {
    int p = partition(arr, low, high);
    // high 的值被 p 覆盖，导致后续递归调用参数错误
    quicksort(arr, low, p - 1);
    quicksort(arr, p + 1, high);  // high 已经被修改
  }
  return 0;
}
```

**原因分析**：
后端寄存器分配器在分配 `s0` 寄存器时，没有正确分析变量的活跃范围，导致 `high` 和 `p` 被分配到同一寄存器。

**建议修复方向**：
1. 改进后端活跃变量分析
2. 在 IR 层面插入额外的复制指令
3. 使用更保守的寄存器分配策略

## 编译与测试

```bash
# 编译编译器
make clean && make

# 运行测试
python run_funct_tests.py --mode full

# 运行单个测试
python run_funct_tests.py --mode full --pattern "24_array*"
```
