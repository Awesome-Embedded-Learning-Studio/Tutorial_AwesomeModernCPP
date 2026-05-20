# ch03: Atomic 操作与内存模型

本目录包含 4 个示例，分为两类。

## 可运行示例

| 文件 | 说明 | 运行预期 |
|------|------|---------|
| `atomic_default_ctor_verify.cpp` | 验证 `std::atomic<T>` 不要求 T 可默认构造 | 输出 `42`、`100`、`200` 三行 |

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/atomic_default_ctor_verify
```

## 汇编验证示例

以下 3 个文件**没有 `main()` 函数**，不能作为可执行文件运行。它们的设计目的是通过 `-S` 生成汇编文件，验证文章中关于 x86 指令的技术断言。

| 文件 | 验证内容 | 预期汇编结果 |
|------|---------|------------|
| `atomic_cas_weak_strong_asm.cpp` | weak/strong CAS 是否生成相同指令 | 均生成 `lock cmpxchgl` |
| `fence_x86_instructions.cpp` | 各级别 fence 的 x86 指令 | relaxed/acquire/release 无指令，seq_cst → `lock orq` 或 `mfence` |
| `memory_order_store_x86.cpp` | 各 memory_order store 的 x86 指令 | relaxed/release → `movl`，seq_cst → `xchgl` |

### 方式一：CMake 一键生成（推荐）

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
cmake --build build --target asm_check
# 汇编文件输出在 build/asm/ 目录下
ls build/asm/
```

### 方式二：手动编译

```bash
# CAS weak vs strong
g++ -std=c++20 -O2 -S -o /tmp/atomic_cas_weak_strong_asm.s atomic_cas_weak_strong_asm.cpp
grep -A3 "test_weak\|test_strong" /tmp/atomic_cas_weak_strong_asm.s

# Fence 指令
g++ -std=c++20 -O2 -S -o /tmp/fence_x86_instructions.s fence_x86_instructions.cpp
grep -E "mfence|lock" /tmp/fence_x86_instructions.s

# Store 指令
g++ -std=c++20 -O2 -S -o /tmp/memory_order_store_x86.s memory_order_store_x86.cpp
grep -E "store_|movl|xchg|mfence" /tmp/memory_order_store_x86.s
```

### 验证要点

- `atomic_cas_weak_strong_asm`：`test_weak` 和 `test_strong` 应生成完全相同的 `lock cmpxchgl` 指令
- `fence_x86_instructions`：仅 `seq_cst` 级别应产生 `mfence` 或等价屏障指令
- `memory_order_store_x86`：仅 `seq_cst` store 应使用 `xchgl`（隐含 LOCK），relaxed/release 均为普通 `movl`

> **注意**：汇编输出依赖于编译器版本（GCC 16.1.1 测试通过）和优化等级（必须 `-O2`），不同编译器可能生成不同指令序列。
