# Chapter 6.8: Scope Guard 作用域守卫模式

本目录包含作用域守卫（Scope Guard）的实现示例，展示如何确保清理代码在作用域结束时执行。

## 文件说明

| 文件 | 描述 |
|------|------|
| `scope_guard.cpp` | 基础 ScopeGuard 实现和应用 |
| `scope_success_fail.cpp` | 带 success/fail 分支的 ScopeGuard（需要 C++17） |

## 关键概念

- **ScopeExit**: 始终在作用域结束时执行
- **ScopeSuccess**: 仅在无异常时执行
- **ScopeFail**: 仅在有异常时执行
- **dismiss()**: 取消清理操作

## 嵌入式注意事项

1. **无堆分配**: ScopeGuard 完全在栈上，适合嵌入式
2. **异常禁用**: 如果使用 `-fno-exceptions`，ScopeSuccess/Fail 不可用
3. **ISR 安全**: 避免在 ISR 中使用复杂的 lambda
4. **确定性**: 析构函数必须快速且不阻塞

## 编译运行

```bash
mkdir build && cd build
cmake ..
make
./scope_guard
./scope_success_fail
```
