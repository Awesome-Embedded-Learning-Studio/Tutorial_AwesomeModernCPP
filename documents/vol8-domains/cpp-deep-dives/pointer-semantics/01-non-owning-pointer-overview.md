---
title: "非拥有指针全景：从 T* 到 Borrowed 到 ObserverPtr"
description: "理解 C++ 中借用、观察与非拥有指针的语义边界，手搓 Borrowed<T> 和 ObserverPtr<T>"
chapter: 1
order: 1
tags:
  - host
  - cpp-modern
  - intermediate
  - 智能指针
  - 内存管理
difficulty: intermediate
platform: host
reading_time_minutes: 20
prerequisites:
  - "卷二 · 第一章：RAII 深入理解"
  - "卷二 · 第一章：weak_ptr 与循环引用"
related:
  - "WeakPtr 反模式：T* + raw Flag* 的致命陷阱"
cpp_standard: [17, 20]
---

# 非拥有指针全景：从 T* 到 Borrowed 到 ObserverPtr

## 引言

我很好奇，不知道有没有人有过这样的经历：拿到一个项目，按照需要打开一个函数，看到参数列表里赫然写着 `T* ptr`，然后就开始犯嘀咕——这个指针到底是"拥有"这个对象呢，还是只是"借用"一下？调用者是不是需要检查 nullptr？函数返回后这个对象还活着吗？

裸指针 `T*` 什么都可能是，也什么都没承诺。它可能是拥有者（比如 `new` 出来还没交给智能指针的那个瞬间），可能是借用者（传给函数用一下），也可能是一个悬垂指针（对象早就没了，指针还留着）。编译器不会帮你区分，注释也不一定靠得住（没准注释是AI写的，代）

C++ Core Guidelines 里有一条 R.3 说得很直白：**裸指针（非 `owner<T>` 的 `T*`）应该只用来表示非拥有的观察或借用**。但在实际代码里，我们拿到一个 `T*`，根本分不清它到底想表达什么语义。

所以我们今天要做的事情很明确：梳理 C++ 中"不拥有对象"的各种指针表达方式，然后手搓两个语义明确的类型——`Borrowed<T>` 和 `ObserverPtr<T>`——让代码自己说话。

先把结论放在前面：非拥有不等于安全，可空不等于能判活，这些类型各有各的适用场景，用错了比用裸指针还坑。

## 核心概念：四层语义模型

在动手写代码之前，我们需要先理清一件事——C++ 里的"不拥有"到底有几种语义。这里我们把它分成四层：

**第一层：借用（Borrowing）。** `T*` 和 `T&` 是最原始的借用方式。你拿到一个指针或引用，用完就还回去，不管理对象的生命周期，也不关心对象什么时候销毁。适合函数参数这种"短暂同步使用"的场景，但千万别存下来以后再用。毕竟资源没有义务告诉你，咱们这资源炸了，请您另寻高就。

**第二层：显式观察（Observation）。** 从这里开始，我们就出现了更语义化的说明。我的意思是——当我们持有`ObserverPtr<T>`的适合，我们不过是想说——他虽然被持久化了，但是我们丝毫不拥有它，甚至我们没办法知道他是否失效。"我只是观察它，我知道有这个事情。但是我不拥有它，或者说，他到底能不能用，我一点保证不了"。和裸指针的区别在于**可读性**（听着有点鸡肋了哈哈）：看到 `ObserverPtr<T>` 就知道这是一个纯观察关系。但它和 `T*` 一样，不能判活——对象销毁了你还拿着 ObserverPtr，解引用就是 UB。

**第三层：非 owning 弱引用（Weak Reference）。** 这是 `WeakPtr<T>` 登场的层次。它和 ObserverPtr 的核心区别是：对象销毁后，你可以安全地检测到失效。为此它需要一个独立于对象的 control block 来记录"对象是否还活着"。但是你说。我还想lock一下，把他延长生命周期，额，做不到。

**第四层：shared ownership 弱引用。** 这就是 `std::weak_ptr<T>`，它和第三层的区别在于它依赖 `std::shared_ptr<T>` 的控制块，调用 `lock()` 会临时延长对象生命周期。

现在我们用一个表格来对比这四层：

| 特性 | T* | T& | Borrowed\<T\> | ObserverPtr\<T\> | WeakPtr\<T\> | std::weak_ptr\<T\> |
|------|----|----|---------------|-----------------|-------------|-------------------|
| 可空 | 是 | 否 | 否（设计上） | 是 | 是 | 是 |
| 拥有对象 | 否 | 否 | 否 | 否 | 否 | 否 |
| 延长生命周期 | 否 | 否 | 否 | 否 | 否 | lock() 临时延长 |
| 对象销毁后安全判空 | 否 | 否 | 否 | 否 | **是** | **是** |
| 适合函数参数 | 是 | 是 | **推荐** | 可以 | 过重 | 过重 |
| 适合类成员 | 可以但不明确 | 可以 | 不推荐 | **推荐** | 推荐 | 推荐 |
| 适合异步回调 | **危险** | **危险** | **危险** | **危险** | 是 | 是 |

⚠️ 注意看这一行——"对象销毁后安全判空"。前四种类型（T*、T&、Borrowed、ObserverPtr）全部做不到。只有真正拥有独立 control block 的 WeakPtr 才行。这一点我们第二篇会展开讲，先记住这个结论。

## 手搓 Borrowed\<T\>：让借用语义显式化

`Borrowed<T>` 想解决的问题很简单：函数参数里出现 `const T&` 或者 `T*` 的时候，调用者和阅读者无法一眼看出"这只是一个借用"。我们需要一个类型来把"非空、非拥有、短期使用"这个语义钉死在类型系统里。

C++ Core Guidelines 里的 `gsl::not_null<T>` 做了类似的事情——它约束指针不能为空，但不表达借用语义。我们的 `Borrowed<T>` 在此基础上更进一步：它是非空的，它是非拥有的，而且它**禁止从临时对象构造**——因为你不能"借用"一个马上就销毁的东西。

先看核心实现：

```cpp
// borrowed.h
// 教学版 Borrowed<T>：显式非空借用语义
// 注意：这不是生产级实现，用于教学演示

#pragma once

#include <type_traits>
#include <utility>

template <typename T>
class Borrowed {
public:
    // 从左值引用构造——这是最正常的用法
    explicit Borrowed(T& ref) noexcept : ptr_(&ref) {}

    // 禁止从临时对象构造
    Borrowed(T&&) = delete;

    // 禁止从 nullptr 构造（T* 重载只接受非空指针）
    Borrowed(std::nullptr_t) = delete;

    // 从裸指针构造，但调用者需保证非空
    explicit Borrowed(T* ptr) noexcept : ptr_(ptr)
    {
        assert(ptr != nullptr && "Borrowed<T> requires a non-null pointer");
    }

    // 默认拷贝和移动——借用是可以传递的
    Borrowed(const Borrowed&) = default;
    Borrowed& operator=(const Borrowed&) = default;
    Borrowed(Borrowed&&) = default;
    Borrowed& operator=(Borrowed&&) = default;

    // 访问接口
    T& get() const noexcept { return *ptr_; }
    T* operator->() const noexcept { return ptr_; }
    T& operator*() const noexcept { return *ptr_; }

private:
    T* ptr_;
};

// 辅助函数：从引用创建 Borrowed，省去写 explicit 构造
template <typename T>
Borrowed<T> borrow(T& ref) noexcept
{
    return Borrowed<T>(ref);
}
```

那很显然我们会有这些问题：

**为什么禁止从临时对象构造？** 这是 `Borrowed<T>` 和裸引用之间最关键的区别。看这个场景：

```cpp
std::string get_name();

// 如果允许从临时对象构造，就会出这种事：
// Borrowed<std::string> b(get_name());  // 临时对象在表达式结束时销毁
// 到这里，get_name返回的对象就被销毁掉了，这个时候访问持有的引用就是踩到地雷了
// b.get();  // 悬垂引用！
```

`T&&` 被标记为 `= delete` 之后，编译器会在编译期直接拒绝这种用法。这是 Rust 借用检查器在 C++ 里能做的最接近的模拟——虽然不像 Rust 那样全面，但至少堵住了最常见的坑。

**为什么构造函数是 explicit？** 防止隐式转换。你不会希望某个函数接受 `Borrowed<Foo>` 然后被隐式地从 `Foo&` 调用——借用的动作应该是有意识的。

**为什么有 `borrow()` 辅助函数？** 纯粹是为了方便。因为构造函数是 explicit 的，每次写 `Borrowed<Foo>(foo)` 有点啰嗦，`borrow(foo)` 更清爽。标准库也有类似的设计，比如 `std::make_pair`、`std::make_shared`。

**为什么不禁止作为类成员？** 技术上可以做到（比如通过 `static_assert` 加 SFINAE），但实际上过度工程化了。我们在文档和惯例中约定"Borrowed 不应该作为类成员保存"就足够了。编译器强制和团队规范之间，我们选择后者——因为 C++ 的类型系统本来就不擅长表达生命周期约束（要不然，为什么我们在这里坐下来谈这个，用蹩脚的方式来表达我们的意思呢），硬做反而容易引入不必要的复杂度。

一个典型的正确用法：

```cpp
void process_data(Borrowed<const std::vector<int>> data)
{
    // 调用者保证 data 非空，我们直接用
    for (const auto& item : data.get()) {
        // ...
    }
}

int main()
{
    std::vector<int> v{1, 2, 3};
    process_data(borrow(v));  // 清晰：我在借用 v
}
```

和直接用 `const std::vector<int>&` 相比，`Borrowed` 版本的优势不在运行时行为（它们生成的代码几乎一样），而在于**可读性**——函数签名直接告诉你"这是一个借用"。

## 手搓 ObserverPtr\<T\>：可空的非拥有观察

如果说 `Borrowed<T>` 是给函数参数用的，那 `ObserverPtr<T>` 就是给类成员用的。它的语义是"我观察这个对象，但我不拥有它，我也不负责它的生命周期"。

事实上，C++ 标准委员会曾经提出过一个非常类似的类型：`std::experimental::observer_ptr<W>`，收录在 Library Fundamentals TS v2 中。它的定义是：

> A non-owning pointer, or observer. The observer stores a pointer to a second object, known as the watched object. An observer_ptr may also have no watched object.

遗憾的是，截至 C++26（似乎是26，我没翻到新消息，如果我又搞错了，欢迎喷我），`observer_ptr` 仍未被正式纳入标准，仍停留在 TS 阶段。但它设计得非常清晰，值得参考。我们的教学版会在其基础上做简化：

```cpp
// observer_ptr.h
// 教学版 ObserverPtr<T>：可空非拥有观察指针
// 参考了 std::experimental::observer_ptr (Library Fundamentals TS v2)

#pragma once

#include <cstddef>

template <typename T>
class ObserverPtr {
public:
    // 默认构造：空观察
    ObserverPtr() noexcept : ptr_(nullptr) {}

    // 从 nullptr 构造：空观察
    ObserverPtr(std::nullptr_t) noexcept : ptr_(nullptr) {}

    // 从裸指针构造：开始观察
    explicit ObserverPtr(T* ptr) noexcept : ptr_(ptr) {}

    // 拷贝和移动
    ObserverPtr(const ObserverPtr&) = default;
    ObserverPtr& operator=(const ObserverPtr&) = default;
    ObserverPtr(ObserverPtr&&) = default;
    ObserverPtr& operator=(ObserverPtr&&) = default;

    // 重新绑定观察对象
    void reset(T* ptr = nullptr) noexcept { ptr_ = ptr; }

    // 释放观察关系，返回原指针
    T* release() noexcept
    {
        T* old = ptr_;
        ptr_ = nullptr;
        return old;
    }

    // 访问
    T* get() const noexcept { return ptr_; }
    T& operator*() const noexcept { return *ptr_; }
    T* operator->() const noexcept { return ptr_; }

    // 检查是否有观察对象
    explicit operator bool() const noexcept { return ptr_ != nullptr; }

    // 交换
    void swap(ObserverPtr& other) noexcept
    {
        T* tmp = ptr_;
        ptr_ = other.ptr_;
        other.ptr_ = tmp;
    }

private:
    T* ptr_;
};

// 相等比较
template <typename T, typename U>
bool operator==(const ObserverPtr<T>& a, const ObserverPtr<U>& b) noexcept
{
    return a.get() == b.get();
}

template <typename T>
bool operator==(const ObserverPtr<T>& a, std::nullptr_t) noexcept
{
    return !a;
}

// 辅助函数
template <typename T>
ObserverPtr<T> make_observer(T* ptr) noexcept
{
    return ObserverPtr<T>(ptr);
}
```

**ObserverPtr 和 Borrowed 有什么区别？** 核心区别在两个字上：**可空**。Borrowed 表达的是"我保证非空的借用"，ObserverPtr 表达的是"我可能为空的观察"。前者适合函数参数（调用者保证非空），后者适合作为持久化的类成员或者存储成员（观察对象可能还没设置，或者被设置成空）。

**为什么 ObserverPtr 不是 WeakPtr？** 这是最常见的误解。ObserverPtr 和 WeakPtr 的区别不在于 API 长什么样（它们都有 `get()`、`operator->`、`operator bool()`），而在于**对象销毁后会发生什么**。ObserverPtr 内部就是一个裸指针，对象销毁了它什么都不知道，解引用就是 UB。真正的 WeakPtr 需要一个独立于对象的 control block 来记录存活状态——这是后面笔者计划投稿到其他问题和专栏的文章了！

典型的正确用法——类成员观察关系：

```cpp
class Logger;

class Service {
public:
    void set_logger(Logger* log) { logger_.reset(log); }

    void do_work()
    {
        if (logger_) {
            // 有 Logger 才记录，没有就算了
            // ...
        }
    }

private:
    ObserverPtr<Logger> logger_;  // 我观察 Logger，但不拥有它
};
```

典型的错误用法——异步回调：

```cpp
// 错误！ObserverPtr 不能保证对象还活着
void Service::async_task()
{
    // 如果 Service 在回调执行前被销毁，logger_ 就是悬垂的
    // 这个 callback 捕获了 logger_，执行时可能 UB
    auto callback = [this]() {
        if (logger_) { // 孩子们，这种东西很危险
            // logger_ 的 ptr_ 指向的 Logger 可能已经不存在了
            // operator bool 只检查 ptr_ 是否为 nullptr
            // 如果 Logger 被销毁但 ptr_ 没被 reset，这里就是 UB
        }
    };
    // post_callback(callback);  // 别这么做
}
```

## Borrowed、ObserverPtr 和裸指针的关系

现在我们回头看，把这三个类型和裸指针的关系说清楚。

`Borrowed<T>` 本质上是 `T&` 的一个类型安全的包装。它比 `T&` 多了"禁止从临时对象构造"的约束，比 `T*` 多了"非空"的保证。它的开销等于零——编译器优化后和裸引用完全一样。它的限制也和裸引用一样：**不能判活**。

`ObserverPtr<T>` 本质上是 `T*` 的一个语义标注。它和裸指针的运行时行为完全一致，区别只在于可读性——当你看到一个 `ObserverPtr<Logger>` 类型的成员变量时，你不需要猜测它是不是拥有那个 Logger，类型名字已经替你回答了。但同样的，**它不能判活**。

而裸指针 `T*` 的问题不在于它"不安全"，而在于它"不表态"——拿到一个 `T*`，你不知道它是拥有的还是非拥有的，是可空的还是保证非空的，是短期的还是长期的。`Borrowed` 和 `ObserverPtr` 解决的是这个"不表态"的问题。

## 小结

我们把这一篇的关键要点总结一下：

- **T\*** 和 **T&** 是 C++ 最原始的借用机制，本身不表达所有权语义
- **Borrowed\<T\>** 表达非空借用，适合函数参数，禁止从临时对象构造，不延长生命周期
- **ObserverPtr\<T\>** 表达可空非拥有观察，适合类成员，不提供判活能力
- **非拥有不等于安全**——Borrowed 和 ObserverPtr 都不能在对象销毁后安全检测失效
- 它们的核心价值是**语义表达**，不是运行时安全——让代码自己说话，减少歧义

到这里我们只解决了"借用"和"观察"两个语义层。真正麻烦的是"弱引用"——当你需要在一个对象可能随时销毁的世界里安全地持有它的引用时，光靠 Borrowed 和 ObserverPtr 是不够的。

下一篇我们就来拆解一个看起来很像 WeakPtr 但实际上不是的东西：`T* + raw Flag*`。

## 参考资源

- [C++ Core Guidelines - R.3: A raw pointer (a T\*) is non-owning](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#Rr-ptr)
- [std::experimental::observer_ptr - cppreference](https://en.cppreference.com/cpp/experimental/observer_ptr)
- [GSL: Guidelines Support Library (Microsoft)](https://github.com/microsoft/GSL) — `gsl::not_null` 和 `gsl::span`
- [C++ Core Guidelines - F.7: For general use, take T\* or T\& arguments rather than smart pointers](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#Rf-smartptrref)
