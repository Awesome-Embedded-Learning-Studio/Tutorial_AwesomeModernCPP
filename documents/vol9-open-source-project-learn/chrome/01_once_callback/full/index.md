# 新手完整教程

本目录包含 OnceCallback 组件的完整新手教程，共 13 篇文章，覆盖从 C++ 基础特性复习到组件实现、测试的完整学习路径。

## 前置知识

先掌握 OnceCallback 所需的 C++ 核心特性：

1. [OnceCallback 前置知识速查：C++11/14/17 核心特性回顾](pre-00-once-callback-cpp-basics-review.md)
2. [OnceCallback 前置知识（一）：函数类型与模板偏特化](pre-01-once-callback-function-type-and-specialization.md)
3. [OnceCallback 前置知识（二）：std::invoke 与统一调用协议](pre-02-once-callback-invoke-and-callable.md)
4. [OnceCallback 前置知识（三）：Lambda 高级特性](pre-03-once-callback-lambda-advanced.md)
5. [OnceCallback 前置知识（四）：Concepts 与 requires 约束](pre-04-once-callback-concepts-and-requires.md)
6. [OnceCallback 前置知识（五）：std::move_only_function (C++23)](pre-05-once-callback-move-only-function.md)
7. [OnceCallback 前置知识（六）：Deducing this (C++23)](pre-06-once-callback-deducing-this.md)

## 动手实践

学完前置知识后，开始实现 OnceCallback：

1. [OnceCallback 实战（一）：动机与接口设计](01-1-once-callback-motivation-and-api-design.md)
2. [OnceCallback 实战（二）：核心骨架搭建](01-2-once-callback-core-skeleton.md)
3. [OnceCallback 实战（三）：bind_once 实现](01-3-once-callback-bind-once.md)
4. [OnceCallback 实战（四）：取消令牌设计](01-4-once-callback-cancellation-token.md)
5. [OnceCallback 实战（五）：then 链式组合](01-5-once-callback-then-chaining.md)
6. [OnceCallback 实战（六）：测试与性能对比](01-6-once-callback-testing-and-perf.md)

## 配套代码

前置知识章节中涉及的 C++ 独立示例代码已提炼为可编译的最小工程，位于：

```
code/volumn_codes/vol9/full_tutorial_codes/chrome_design/
```

| 示例 | 主题 | 来源文章 | 最低 C++ 标准 |
|------|------|----------|-------------|
| `01_move_semantics.cpp` | 移动语义、完美转发、可变参数模板 | pre-00 | C++17 |
| `02_smart_pointers.cpp` | unique_ptr、shared_ptr | pre-00 | C++17 |
| `03_atomic_memory_order.cpp` | atomic、memory_order、enum class | pre-00 | C++17 |
| `04_lambda_basics.cpp` | 捕获模式、泛型 lambda、[[nodiscard]] | pre-00 | C++17 |
| `05_lambda_advanced.cpp` | mutable lambda、init capture、C++17/C++20 bind | pre-03 | C++20 |
| `06_type_traits.cpp` | type traits、if constexpr、decltype(auto)、ref-qualifier | pre-00 | C++17 |
| `07_function_type_specialization.cpp` | 函数类型、FuncTraits、主模板+偏特化 | pre-01 | C++17 |
| `08_invoke.cpp` | std::invoke、std::invoke_result_t | pre-02 | C++17 |
| `09_concepts_requires.cpp` | concept、requires、not_the_same_t、模板构造函数劫持 | pre-04 | C++20 |
| `10_move_only_function.cpp` | std::move_only_function 构造/移动/判空/SBO | pre-05 | C++23 |
| `11_deducing_this.cpp` | deducing this 推导规则、左值拦截 | pre-06 | C++23 |

构建方式：

```bash
cd code/volumn_codes/vol9/full_tutorial_codes/chrome_design
mkdir build && cd build
cmake ..
make -j$(nproc)
```
