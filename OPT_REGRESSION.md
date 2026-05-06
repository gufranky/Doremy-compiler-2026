# -opt 回归记录

## 最终状态

- 环境：WSL Ubuntu / host 模式
- 编译器：当前 `myrt1e` 分支本地构建产物
- 非 `-opt`：主线 funct 正常
- `-opt` 修复后结果：`141/141` 编译通过，`141/141` 执行通过
- `blockGVN` 修复后的全量摘要：`opt-full-summary-after-blockgvn-fix.json`

## 初始现象

- 初始 `-opt` 全量结果：`141/141` 编译通过，`125/141` 执行通过，`16` 个 `mismatch`
- 已确认本地 `CRLF` 输入导致的 WSL runner mismatch 已修复，不属于当前问题。
- 当前剩余问题属于 `-opt` 优化正确性问题，重点集中在数组、地址传播、内存别名、副作用调用。

## 初始失败用例

### 控制流/短路
- `51_short_circuit3`

### 图/排序/循环
- `09_BFS`
- `55_sort_test1`
- `56_sort_test2`
- `57_sort_test3`
- `58_sort_test4`
- `59_sort_test5`
- `60_sort_test6`

### 表达式/计算
- `63_big_int_mul`
- `64_calculator`
- `69_expr_eval`
- `85_long_code`

### 数组/矩阵/长数组
- `83_long_array`
- `96_matrix_add`
- `98_matrix_mul`
- `99_matrix_tran`

## 定位结论

1. `simplifyCallsInFunc`
   - 问题：按返回摘要直接把 `CallInst` 改写为 `CopyInst`，没有证明被调函数无副作用。
   - 修复：仅允许 `isPureReturnOnly` 的函数参与该替换，即函数内不能有 `CallInst`、`StoreInst`、`LoadInst`。

2. `globalVarConst`
   - 问题：对包含控制流或调用的函数做整函数扫描常量化，约束不足。
   - 修复：遇到 `Call` / `Branch` / `Jump` / `Label` 直接禁用该 pass，先保正确性。

3. `blockGVN`
   - 问题：未处理内存与调用副作用，对含 `Load` / `Store` / `Call` 的函数仍做值编号，导致错误复用。
   - 修复：对含内存访问或调用的函数直接禁用该 pass，先保正确性。

## 质量整理

- 抽出 `hasForbiddenInstKind()` 统一 `globalVarConst` 与 `blockGVN` 的保守前置判定。
- 抽出 `kAllOptPasses`，避免 `OptimizeConfig.passMask` 默认值内联位运算常量。
- 保持调试入口参数语义不变：`--pass-mask`、`--stop-after`、`--disable-ipa`。

## 定位矩阵

| 用例 | 类别 | 首个问题点 | 结果 | 备注 |
| --- | --- | --- | --- | --- |
| 51_short_circuit3 | 短路 | `simplifyCallsInFunc` | 已修复 | 调用被错误删掉 |
| 64_calculator | 表达式 | `globalVarConst` | 已修复 | 控制流函数中过度常量化 |
| 69_expr_eval | 表达式+输入 | `globalVarConst` | 已修复 | 与输入/流程相关 |
| 96_matrix_add | 数组/矩阵 | `blockGVN` | 已修复 | 内存读写被错误复用 |
| 98_matrix_mul | 数组/矩阵 | `blockGVN` | 已修复 | 数组循环场景 |
| 99_matrix_tran | 数组/矩阵 | `blockGVN` | 已修复 | 转置读写场景 |
