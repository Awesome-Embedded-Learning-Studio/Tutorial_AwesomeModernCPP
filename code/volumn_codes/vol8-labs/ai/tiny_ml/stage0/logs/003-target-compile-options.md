# 003 · `target_compile_options` 与警告三件套 + `-g`

> 出处:`CMakeLists.txt` 第 27 行。
> 本机:g++ 16.1.1 / clang++ 22.1.6。

## 原命令

```cmake
target_compile_options(smoke_catch2 PRIVATE -Wall -Wextra -Wpedantic -g)
```

## ① 命令骨架

`target_compile_options(<target> <scope> <flags...>)` —— **现代 CMake(target-based)** 写法,把选项挂在指定 target 上,对应老式全局 `add_compile_options()`。哲学:属性挂 target,谁需要谁加,不污染全局。

- `smoke_catch2` —— 接收方,第 25 行 `add_executable(smoke_catch2 tests/smoke.cpp)` 建的测试可执行文件。
- `PRIVATE` —— 传递性关键字:

  | 关键字 | 编译自己 | 传给下游 |
  |--------|:-------:|:--------:|
  | PRIVATE | ✅ | ❌ |
  | INTERFACE | ❌ | ✅ |
  | PUBLIC | ✅ | ✅ |

  **警告标志几乎总是 PRIVATE**:① 下游不关心你怎么编译它;② 警告标志编译器相关(`-Wall` 是 GCC/Clang,MSVC 是 `/W4` 且 `/Wall` 含义不同会刷屏),PUBLIC 传下去换编译器就炸。

## ② 四个标志(2026-06-30 实证)

`-Wall -Wextra -Wpedantic` = 警告三件套,逐级加严。演示埋三雷各吃一个:

| 标志 | 命中警告 | 含义 |
|------|----------|------|
| `-Wall` | `[-Wunused-variable]` | 精选「有用、误报低」警告集。⚠️ 非「所有警告」 |
| `-Wextra` | `[-Wunused-parameter]` | 再补一批:未用参数、有符号/无符号比较 |
| `-Wpedantic` | `[-Wvla] ISO C++ forbids variable length array` | 按 ISO 标准查,对编译器扩展告警 → 更可移植 |

演示:档1 → 档2 → 档3 警告逐级递增。

`-g` —— 生成 DWARF 调试信息塞进产物。实证:
- 无 `-g`:`not stripped`,0 个 `.debug*` 段;
- 有 `-g`:`with debug_info`,6 个 `.debug*` 段。
有这些段 gdb/lldb 才能映射回源文件行号/变量名/类型。

## 易错点

1. **`-g` 不影响优化**。默认 `-O0`(不优化),`-g` 单加 = 可调试 + 不优化;发布再加 `-O2`。
2. **这套标志 GCC/Clang 专用,MSVC 一套都不认**(`-Wall`/`-Wextra`/`-Wpedantic`/`-g` 全是 GCC/Clang 私有)。MSVC 有自己的对应物,本 lab 的 `CMakeLists.txt` 已用生成器表达式按编译器 ID 分挂:

   | GCC/Clang | MSVC | 含义 |
   |----------|------|------|
   | `-Wall -Wextra` | `/W4` | 警告级(W4 是 MSVC 实用最高档;`/Wall` 含义不同会刷屏,不可用) |
   | `-Wpedantic` | `/permissive-` | 严格标准符合,禁用非标准扩展(精神接近,不完全等同) |
   | `-g` | `/Zi` | 生成调试信息(写进 PDB) |

   `CMakeLists.txt` 第 29–32 行的真实写法是「分两条、按编译器 ID 选」:

   ```cmake
   target_compile_options(smoke_catch2 PRIVATE
       $<$<CXX_COMPILER_ID:MSVC>:/W4;/permissive-;/Zi>
       $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall;-Wextra;-Wpedantic;-g>
   )
   ```

   关键取舍:第二条用 `$<NOT:$<CXX_COMPILER_ID:MSVC>>` 而非枚举 `$<CXX_COMPILER_ID:GNU,Clang>` —— 「非 MSVC 即走 GCC/Clang 那套」,自动覆盖 Intel/LLVM 等兼容 GCC 标志的编译器,不必每加一个编译器改一次清单。生成器表达式里多条标志用分号 `;` 分隔(CMake 列表分隔符)。
