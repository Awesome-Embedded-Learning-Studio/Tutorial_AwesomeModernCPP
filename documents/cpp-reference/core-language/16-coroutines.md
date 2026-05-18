---
title: "Coroutines（协程基础）"
description: "无栈协程的语言支持：函数可挂起执行并稍后恢复，实现惰性求值与异步流程"
chapter: 99
order: 16
tags:
  - host
  - cpp-modern
  - intermediate
  - coroutine
difficulty: intermediate
cpp_standard: [20, 23]
---

<!--
参考卡模板 (Reference Card Template)
用于 documents/cpp-reference/ 下的特性速查页。
与 article-template.md 不同，参考卡走精炼的结构化格式，不需要叙事风格。

标签使用规则：
1. 必须包含 1 个 platform 标签（参考卡统一用 host）
2. 必须包含 1 个 difficulty 标签
3. 至少包含 1 个 topic 标签
4. 从 scripts/validate_frontmatter.py 的 VALID_TAGS 集合中选取
-->

# Coroutines 协程基础（C++20）

## 一句话

让函数可以在中途挂起（suspend）并稍后恢复（resume）的语言机制——实现惰性生成器、异步 I/O、状态机等模式的基础设施。

## 头文件

`#include <coroutine>`（协程支持库）

## 核心 API 速查

| 操作 | 签名 | 说明 |
|------|------|------|
| 协程句柄 | `coroutine_handle<promise_type>` | 类型擦除的协程句柄，用于恢复/销毁 |
| 挂起 | `co_await expr;` | 挂起当前协程，等待 `expr` 完成 |
| 产值 | `co_yield expr;` | 挂起并向调用者返回一个值 |
| 返回 | `co_return expr;` | 协程最终返回 |
| Promise 类型 | `struct promise_type` | 定制协程行为的类型（必须定义在返回类型中） |
| 初始挂起点 | `suspend_always initial_suspend()` | 协程启动时是否立即挂起 |
| 最终挂起点 | `suspend_always final_suspend() noexcept` | 协程结束时是否挂起（`noexcept` 必须） |
| 返回对象 | `get_return_object()` | 创建返回给调用者的对象 |

## 最小示例

```cpp
// Standard: C++20
#include <coroutine>
#include <iostream>

struct Generator {
    struct promise_type {
        int current_value;
        auto get_return_object() { return Generator{handle::from_promise(*this)}; }
        auto initial_suspend() { return std::suspend_always{}; }
        auto final_suspend() noexcept { return std::suspend_always{}; }
        auto yield_value(int v) { current_value = v; return std::suspend_always{}; }
        void return_void() {}
        void unhandled_exception() {}
    };
    using handle = std::coroutine_handle<promise_type>;
    handle coro;
    ~Generator() { if (coro) coro.destroy(); }
    bool next() { coro.resume(); return !coro.done(); }
    int value() { return coro.promise().current_value; }
};

Generator counter() {
    for (int i = 0; i < 3; ++i)
        co_yield i;
}

int main() {
    auto gen = counter();
    while (gen.next())
        std::cout << gen.value() << " "; // 0 1 2
}
```

## 嵌入式适用性：中

- 无栈协程：挂起时状态存储在堆分配的协程帧中，内存开销可控
- 适合实现嵌入式异步 I/O、事件循环、状态机等模式，替代回调地狱
- 协程帧默认堆分配，可通过自定义 `operator new` 改为静态内存池
- C++20 仅提供语言机制和最小库支持，实用的高层抽象（如 `std::generator`）需 C++23
- 编译器支持仍存在已知 ICE（内部编译器错误），生产使用需充分测试

## 编译器支持

| GCC | Clang | MSVC |
|-----|-------|------|
| 12 | 14 | 19.28 |

## 另见

- [cppreference: Coroutines](https://en.cppreference.com/w/cpp/language/coroutines)
- [cppreference: std::coroutine_handle](https://en.cppreference.com/w/cpp/coroutine/coroutine_handle)

---

*部分内容参考自 [cppreference.com](https://en.cppreference.com/)，采用 [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/) 许可*
