# 测试脚本使用说明

## 1. 功能测试

脚本：`run_funct_tests.py`

常用命令：
```bash
python run_funct_tests.py --mode full --timeout 60 --workers 10
python run_funct_tests.py --mode compile --pattern "09_BFS.sy" --timeout 60
python run_funct_tests.py --mode full --pattern "86_long_code2.sy" --timeout 120
```

常用参数：
- `--mode compile|full`：只检查编译，或同时检查运行结果
- `--pattern`：过滤用例
- `--timeout`：单阶段超时
- `--workers`：WSL 内部并发数
- `--summary-json`：输出结果摘要

当前行为：
- Windows 侧只启动一次 WSL 驱动
- 测试并发在 WSL 内部完成
- `prime_search1/2/3` 缺源文件时会跳过

## 2. 性能测试

脚本：`run_performance_tests.py`

常用命令：
```bash
python run_performance_tests.py --timeout 60 --workers 10 --repeat 1
python run_performance_tests.py --pattern "01_mm1*" --timeout 60 --workers 10
```

常用参数：
- `--pattern`：过滤性能用例
- `--repeat`：每个用例重复次数
- `--no-opt`：关闭 `-O1`
- `--timeout`：单阶段超时
- `--workers`：WSL 内部并发数
- `--summary-json`：输出结果摘要

当前目录：
- 性能用例目录是 `performance`

## 3. 二分定位调试

用于定位某个优化 pass 是否引入错误。

建议流程：
1. 先跑单个用例确认回归是否稳定
2. 用 `--pattern` 缩小范围
3. 用 `--timeout` 临时放宽，排除纯超时
4. 结合开关逐步二分：
   - 先看默认编译
   - 再看关闭 MidIR 或只做 lowering 的路径
   - 再逐个缩小到具体 pass

示例：
```bash
python run_funct_tests.py --mode full --pattern "09_BFS.sy" --timeout 60
python run_funct_tests.py --mode full --pattern "55_sort_test1.sy" --timeout 60
python run_performance_tests.py --pattern "01_mm1*" --timeout 60 --repeat 1
```

## 4. 现状备注

- 功能测试当前可跑，已确认大部分用例通过
- 性能测试可跑，`prime_search1/2/3` 因缺源文件跳过
- `86_long_code2` 需要更长超时，建议单独放宽
