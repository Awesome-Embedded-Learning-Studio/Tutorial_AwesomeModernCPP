---
title: "OnceCallback 实战（五）：then 链式组合"
description: "逐行拆解 then() 的所有权链设计——从管道思维到 void/非 void 分支处理，理解 OnceCallback 中最精巧的所有权管理"
chapter: 1
order: 5
tags:
  - host
  - cpp-modern
  - beginner
  - 回调机制
  - 函数对象
  - 模板
difficulty: beginner
platform: host
cpp_standard: [23]
reading_time_minutes: 9
prerequisites:
  - "OnceCallback 实战（二）：核心骨架搭建"
  - "OnceCallback 前置知识（二）：std::invoke 与统一调用协议"
  - "OnceCallback 前置知识（三）：Lambda 高级特性"
related:
  - "OnceCallback 实战（六）：测试与性能对比"
---

# OnceCallback 实战（五）：then 链式组合

## 引言

`then()` 允许我们把两个回调串联成一个管道——第一个回调的输出是第二个回调的输入。听起来简单，但它是 OnceCallback 四个功能中所有权设计最精巧的一个。因为 OnceCallback 是 move-only 的，`then()` 必须把原回调的所有权完整地转移到新回调中，不能有任何共享或泄露。

这一篇我们从管道思维出发，逐行拆解 `then()` 的实现，重点理解所有权链和 void/非 void 分支的处理。

> **学习目标**
>
> - 理解 `then()` 的管道语义和所有权链设计
> - 逐行理解 `then()` 的完整实现
> - 理解 void 前缀回调的特殊处理
> - 对比 `then()` 用 `&&` 限定和 `run()` 用 deducing this 的选择理由

---

## 管道思维：then() 的语义

如果你用过 Unix 管道，`then()` 的语义就很直觉了：

```bash
# Unix 管道：cmd1 的输出是 cmd2 的输入
echo "hello" | tr 'h' 'H' | wc -c
```

`then()` 做的是同样的事情——回调 A 的输出是回调 B 的输入。用代码表达：

```cpp
auto pipeline = OnceCallback<int(int, int)>([](int a, int b) {
    return a + b;          // 第一步：3 + 4 = 7
}).then([](int sum) {
    return sum * 2;        // 第二步：7 * 2 = 14
});

int result = std::move(pipeline).run(3, 4);  // result == 14
```

`then()` 把两个独立的回调串联成一个新的回调。调用新回调时，自动走完 A → B 的整个流程。

---

## 所有权是 then() 的核心挑战

串联后的新回调需要持有原回调和后续回调的**所有权**——否则原回调可能在外部被提前消费掉，管道就断了。而 OnceCallback 是 move-only 的，这意味着 `then()` 必须消费 `*this`（原回调）和 `next`（后续回调），把两者的所有权转移到一个新的 lambda 闭包里。

整个所有权链条是这样的：

```text
新 OnceCallback → move_only_function → lambda 闭包 → [原回调 + 后续回调]
```

每一层都通过移动语义传递所有权，没有任何共享或拷贝。这就是 move-only 语义在 `then()` 中的完整体现。

---

## then() 的完整实现逐行拆解

```cpp
template<typename ReturnType, typename... FuncArgs>
template<typename Next>
auto OnceCallback<ReturnType(FuncArgs...)>::then(Next&& next) && {
    using NextType = std::decay_t<Next>;

    if constexpr (std::is_void_v<ReturnType>) {
        using NextRet = std::invoke_result_t<NextType>;
        return OnceCallback<NextRet(FuncArgs...)>(
            [self = std::move(*this),
             cont = std::forward<Next>(next)]
            (FuncArgs... args) mutable -> NextRet {
                std::move(self).run(std::forward<FuncArgs>(args)...);
                return std::invoke(std::move(cont));
            });
    } else {
        using NextRet = std::invoke_result_t<NextType, ReturnType>;
        return OnceCallback<NextRet(FuncArgs...)>(
            [self = std::move(*this),
             cont = std::forward<Next>(next)]
            (FuncArgs... args) mutable -> NextRet {
                auto mid = std::move(self).run(std::forward<FuncArgs>(args)...);
                return std::invoke(std::move(cont), std::move(mid));
            });
    }
}
```

### 函数签名：右值限定

```cpp
auto then(Next&& next) &&
```

末尾的 `&&` 使其成为右值限定的成员函数——只能通过 `std::move(cb).then(next)` 或临时对象 `.then(next)` 调用。如果调用方写了 `cb.then(next)`（左值调用），编译器直接报"没有匹配的重载函数"。这是表达消费语义的另一种方式——和 `run()` 用 deducing this 不同，`then()` 不需要区分左值和右值给出不同的错误信息，直接用 ref-qualifier 更简洁。

### std::decay_t<Next>：退化去掉引用

```cpp
using NextType = std::decay_t<Next>;
```

`Next` 可能是 `SomeLambda&&`（右值引用）或 `SomeLambda&`（左值引用），`std::decay_t` 把引用去掉，得到裸的 lambda 类型。后续用 `NextType` 做类型查询。

### if constexpr 的两个分支

`then()` 的核心区别在于原回调的返回类型是不是 void。

**非 void 分支**：原回调返回一个值，这个值需要传给后续回调。

```cpp
using NextRet = std::invoke_result_t<NextType, ReturnType>;
```

`std::invoke_result_t<NextType, ReturnType>` 在编译期推导"把 `ReturnType` 类型的值传给 `NextType` 类型的可调用对象，返回什么类型"。这就是新回调的返回类型。

lambda 内部的执行流程：先调用原回调拿到中间结果 `mid`，再把 `mid` 传给后续回调。

```cpp
auto mid = std::move(self).run(std::forward<FuncArgs>(args)...);
return std::invoke(std::move(cont), std::move(mid));
```

**void 分支**：原回调没有返回值，后续回调不接受参数。

```cpp
using NextRet = std::invoke_result_t<NextType>;
```

`std::invoke_result_t<NextType>` 推导的是"不带参数调用 `NextType`，返回什么类型"。

lambda 内部的执行流程：先执行原回调（不拿返回值），再执行后续回调（不传参数）。

```cpp
std::move(self).run(std::forward<FuncArgs>(args)...);
return std::invoke(std::move(cont));
```

### lambda 捕获：所有权的核心

```cpp
[self = std::move(*this), cont = std::forward<Next>(next)]
```

`self = std::move(*this)` 是整个所有权链的关键——它把当前 OnceCallback 对象的**所有内容**（`func_`、`status_`、`token_`）移动到 lambda 的闭包对象里。移动之后，当前对象进入"被移走"的状态——`func_` 和 `token_` 已经被搬走了。

`cont = std::forward<Next>(next)` 把后续回调也搬进 lambda 闭包。`std::forward` 保持 `next` 的值类别——右值就移动，左值就拷贝。

这个 lambda 又被传给一个新的 `OnceCallback<NextRet(FuncArgs...)>` 构造函数，存入新回调的 `std::move_only_function` 里。`move_only_function` 的类型擦除能力保证了不管 lambda 的实际类型是什么，都能被统一存储。

---

## 多级管道

`then()` 可以链式调用，形成多级管道：

```cpp
using namespace tamcpp::chrome;
auto pipeline = OnceCallback<int(int)>([](int x) {
    return x * 2;
}).then([](int x) {
    return x + 10;
}).then([](int x) {
    return std::to_string(x);
});

std::string result = std::move(pipeline).run(5);
// 5 * 2 = 10, 10 + 10 = 20, to_string(20) = "20"
```

每次 `then()` 都会创建一个新的 OnceCallback，内部嵌套捕获了前一步的回调。调用最外层的 `run()` 时，执行过程是递归展开的：最外层回调被 `run()` → 执行其 lambda → lambda 内部对上一层调用 `std::move(self).run()` → 再对更上一层调用 → 直到底层。

性能上，每一层 `then()` 增加一次 `std::move_only_function` 的间接调用。对于 2-3 级的管道来说完全可接受。如果管道层级超过 10 级，可能需要考虑扁平化的管道结构来避免过深的嵌套——但这已经超出我们当前的讨论范围了。

---

## 几个容易踩坑的地方

### mutable 不可省略

lambda 内部需要调用 `std::move(self).run()`——这个操作会修改 `self` 的状态（把 status 从 kValid 改为 kConsumed）。如果 lambda 是 const 的（没加 `mutable`），`self` 在内部就是 const 引用，没法在 const 对象上调用修改状态的操作，编译直接失败。

### self = std::move(*this) 的状态

移动之后，当前 OnceCallback 对象的 `func_` 和 `token_` 都已经被 move 走了——它们处于"被移走"的状态。`status_` 没有被显式设为 kEmpty，而是保持原来的值。但因为 `func_` 已经被 move 走了，当前对象实际上已经不可用了——任何对它的操作都是未定义的。`then()` 的 `&&` 限定保证了调用方没法在调用 `then()` 之后继续使用原对象。

### 为什么用 std::invoke 而不是直接调用

`cont` 是一个普通可调用对象（通常是 lambda），直接 `cont(mid)` 也能工作。但 `std::invoke` 是防御性编程——如果有人传进来一个成员函数指针作为后续回调，直接调用语法会失败，`std::invoke` 不会。统一使用 `std::invoke` 保证了无论传什么可调用对象都能正确工作。

---

## 小结

这一篇我们拆解了 `then()` 的完整实现。它的核心挑战是所有权管理——通过 `self = std::move(*this)` 把整个原回调搬进 lambda 闭包，建立完整的所有权链。`if constexpr` 处理 void 和非 void 返回类型的不同语义——void 回调不传参数给后续回调，非 void 回调传递中间结果。`then()` 用 `&&` 限定表达消费语义（比 `run()` 的 deducing this 更简洁，因为不需要自定义错误信息），`mutable` 关键字不可省略（因为内部需要修改 `self` 的状态）。

下一篇是系列的最后一篇——我们用系统化的测试用例来验证整个实现，并对比与 Chromium 原版的性能差异。

## 参考资源

- [Chromium callback.h 源码](https://chromium.googlesource.com/chromium/src/+/HEAD/base/functional/callback.h)
- [cppreference: std::invoke](https://en.cppreference.com/w/cpp/utility/functional/invoke)
- [cppreference: if constexpr](https://en.cppreference.com/w/cpp/language/if)
