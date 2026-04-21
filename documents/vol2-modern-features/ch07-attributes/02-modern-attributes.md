---
title: "C++20-23 新属性：性能导向的编译器提示"
description: "[[likely]]/[[unlikely]]、[[no_unique_address]]、[[assume]] 等新属性"
chapter: 7
order: 2
tags:
  - host
  - cpp-modern
  - intermediate
difficulty: intermediate
platform: host
cpp_standard: [20, 23]
reading_time_minutes: 15
prerequisites:
  - "Chapter 7: 标准属性详解"
related:
  - "constexpr 构造函数与字面类型"
---

# C++20-23 新属性：性能导向的编译器提示

上一章我们看了 C++11-17 的标准属性，它们主要解决"代码正确性"的问题——强制检查返回值、消除警告、标记废弃 API。C++20 和 C++23 新增的属性则换了一个方向：它们更关注性能，给编译器提供优化提示。`[[likely]]` 和 `[[unlikely]]` 帮编译器做分支预测优化，`[[no_unique_address]]` 节省内存布局中的冗余空间，`[[assume]]` 让编译器基于假设做更激进的优化。

这些属性用好了能带来实打实的性能提升，但用错了也可能适得其反。我们来一个一个拆解。

> 一句话总结：**C++20-23 的新属性从"帮编译器找 bug"转向"帮编译器优化代码"。用对场景、验证效果，才是正道。**

------

## [[likely]] 和 [[unlikely]]（C++20）：分支预测提示

### 为什么需要手动提示

现代 CPU 都有动态分支预测器，会根据运行时的历史记录猜测分支走向。大部分情况下 CPU 的猜测已经足够聪明了。但在以下场景中，手动提示仍然有价值：第一，函数第一次被调用时分支预测器还没有历史数据；第二，在嵌入式系统中某些 CPU 的分支预测器比较简单；第三，编译器可以通过调整代码布局（把热路径放在一起）来提高指令缓存命中率。

`[[likely]]` 告诉编译器"这个分支更可能被执行"，`[[unlikely]]` 则表示"这个分支很少执行"。

### 语法与放置位置

这对属性可以放在 `if` 语句的分支体中，也可以放在 `switch` 的 `case` 标签上：

```cpp
// 放在 if 分支中
if (error == ErrorCode::Ok) [[likely]] {
    // 正常路径——大概率执行
    process_data();
} else {
    // 错误路径——小概率执行
    handle_error();
}

// 放在 switch case 上
switch (status) {
    [[likely]] case Status::Running:
        run_task();
        break;
    case Status::Error:
        recover();
        break;
    default:
        break;
}
```

⚠️ 注意属性的放置位置：`[[likely]]` 放在分支体的 `{` 前面，不是放在条件表达式上。这是 C++20 标准的规定。

### 实际效果分析

笔者写了一个简单的测试，看看 `[[likely]]` 到底能改变什么。以下是在 GCC 13 上用 `-O2` 编译的代码：

```cpp
int process(int value) {
    if (value > 0) [[likely]] {
        return value * 2;
    } else {
        return -value;
    }
}
```

编译器在不加 `[[likely]]` 时，可能会把 `else` 分支的代码放在前面（因为编译器也不知道哪个更可能）。加了 `[[likely]]` 后，编译器会把 `value > 0` 的路径放在前面，减少分支预测失败时的指令流水线刷新。

但从实际性能测试来看，这个优化的效果取决于具体场景。在热点循环中，效果可能比较明显；在偶发的条件判断中，几乎看不出差异。原因是现代 x86/ARM CPU 的动态分支预测器已经非常强大，静态提示的价值在递减。

### 什么时候值得用

笔者的建议是：在性能关键路径上，当你通过 profiling（比如 `perf`、`gprof`）确认某个分支预测失败率很高时，再考虑加 `[[likely]]`/`[[unlikely]]`。不要凭感觉到处加——过度使用不仅不会提升性能，还可能因为错误的提示让编译器做出更差的选择。

典型场景包括：错误检查分支（正常路径 likely）、边界条件处理（正常路径 likely）、中断处理中的常见事件类型（常见事件 likely）。

### 与编译器内置函数的对比

在 `[[likely]]` 出现之前，GCC/Clang 用 `__builtin_expect` 来做分支预测提示：

```cpp
// 旧写法
if (__builtin_expect(error == ErrorCode::Ok, 1)) {
    process_data();
}

// 新写法
if (error == ErrorCode::Ok) [[likely]] {
    process_data();
}
```

`[[likely]]` 的可读性好得多，而且标准化的属性意味着它在所有支持 C++20 的编译器上都能工作。

------

## [[no_unique_address]]（C++20）：空基类优化

### 问题：空类也要占 1 字节

C++ 标准要求每个完整的对象都有唯一的地址，这意味着即使是没有任何数据成员的"空类"，`sizeof` 也至少是 1。当你把一个空类作为另一个类的成员时，它就白白占了一个字节：

```cpp
struct Empty {
    void foo() {}   // 只有成员函数，没有数据成员
};

struct Container {
    Empty e;        // sizeof(Empty) == 1，浪费
    int x;
};

static_assert(sizeof(Empty) == 1);
static_assert(sizeof(Container) == sizeof(int) + 1);  // 可能有 padding
```

对于大多数应用来说浪费 1 字节不算什么，但在泛型编程中，策略类（allocator、mutex policy 等）经常是空类。如果多个策略类同时作为成员，每个都占 1 字节，累积起来就不容忽视了。更关键的是，这会让 `sizeof` 的结果不符合预期，影响缓存行对齐等优化。

### 传统的 EBO 方案

传统的解决方案是空基类优化（Empty Base Optimization, EBO）——通过继承而不是成员来持有空类，这样编译器就不需要给它分配独立的空间：

```cpp
struct Empty {};

// 传统 EBO：通过继承
struct Container : private Empty {
    int x;
};

static_assert(sizeof(Container) == sizeof(int));  // Empty 不占空间
```

但 EBO 有几个缺点：你只能继承一个同类型的空基类（不能同时继承两个 `Empty`）；继承是一种很强的耦合关系，为了节省内存而修改继承关系是不合理的；有些编码规范禁止私有继承。

### [[no_unique_address]] 的方案

C++20 引入的 `[[no_unique_address]]` 让你可以通过成员变量（而不是继承）来实现同样的优化：

```cpp
struct Empty {
    void foo() {}
};

struct Container {
    [[no_unique_address]] Empty e;   // 如果 Empty 是空类，e 不占空间
    int x;
};

static_assert(sizeof(Container) == sizeof(int));  // e 被优化掉了
```

### 策略模式中的应用

`[[no_unique_address]]` 在策略模式中特别有用。假设你有一个容器类，它接受分配器策略和锁策略作为模板参数。在单线程场景下，锁策略是空类（所有方法都是空操作），你不想让它白白占空间：

```cpp
struct NullMutex {
    void lock() {}
    void unlock() {}
};

struct StdMutex {
    void lock()   { mtx_.lock(); }
    void unlock() { mtx_.unlock(); }
private:
    std::mutex mtx_;
};

template<typename T, typename Mutex = NullMutex>
class ThreadSafeBuffer {
public:
    void push(const T& item) {
        mutex_.lock();
        // ... 添加元素
        mutex_.unlock();
    }

private:
    [[no_unique_address]] Mutex mutex_;
    T* data_;
    std::size_t size_;
    std::size_t capacity_;
};

// 单线程版本：NullMutex 不占空间
ThreadSafeBuffer<int> single_thread_buf;
static_assert(sizeof(single_thread_buf) == sizeof(void*) + sizeof(std::size_t) * 2);

// 多线程版本：std::mutex 占实际空间
ThreadSafeBuffer<int, StdMutex> multi_thread_buf;
// sizeof 包含 std::mutex 的大小
```

这个设计让你在不牺牲内存效率的前提下，通过模板参数灵活切换策略。单线程场景下不浪费一个字节，多线程场景下使用真正的互斥锁。

### 注意事项

`[[no_unique_address]]` 有一些需要注意的细节。同一类型的多个 `[[no_unique_address]]` 成员可能共享同一地址（因为它们都是空类，不需要区分）：

```cpp
struct A {
    [[no_unique_address]] Empty e1;
    [[no_unique_address]] Empty e2;
    int x;
};

A a;
// &a.e1 == &a.e2 可能为 true！
```

如果你需要取这些成员的地址或用引用指向它们，请格外小心——它们的地址可能相同。此外，这个属性只对空类有效。如果类有数据成员，加了也没效果：

```cpp
struct NotEmpty { int data; };

struct Test {
    [[no_unique_address]] NotEmpty e;   // e 仍然占 sizeof(int)
    int x;
};
static_assert(sizeof(Test) == 2 * sizeof(int));
```

另外，MSVC 在某些版本中对 `[[no_unique_address]]` 的支持存在 bug——即使空类也可能不被优化。这在跨平台项目中需要特别注意，建议在目标平台上验证 `sizeof` 的结果。

------

## [[assume]]（C++23）：编译器假设

### 语义

C++23 引入的 `[[assume(expression)]]` 告诉编译器"请假设 `expression` 为真"，编译器可以基于这个假设做更激进的优化。如果运行时 `expression` 实际为假，行为是未定义的。

这与 `assert` 不同。`assert` 在运行时检查条件，失败则终止程序；`[[assume]]` 完全不做运行时检查，只是让编译器放心大胆地优化。

### 示例

```cpp
int divide(int a, int b) {
    [[assume(b != 0)]];
    return a / b;
}
```

在这个例子中，编译器可以省去除零检查的代码路径，生成更快的除法指令。但如果你传入 `b == 0`，后果是未定义的——可能崩溃，可能返回垃圾值，可能看起来正常但暗中搞破坏。

### 与 __builtin_assume 的对比

在 `[[assume]]` 之前，MSVC 用 `__assume`，GCC 用 `__builtin_assume`（虽然 GCC 更常用的方式是 `if (cond) __builtin_unreachable()`）：

```cpp
// MSVC
__assume(b != 0);

// GCC
if (b == 0) __builtin_unreachable();

// C++23 标准写法
[[assume(b != 0)]];
```

### 使用场景

`[[assume]]` 的典型使用场景是：你对运行时的某些条件有确定性的知识，但编译器无法通过静态分析推断出来。比如你知道一个数组访问永远不会越界，或者你知道某个指针永远不为 null：

```cpp
void process_array(int* data, std::size_t size) {
    [[assume(data != nullptr)]];
    [[assume(size > 0)]];

    for (std::size_t i = 0; i < size; ++i) {
        // 编译器可以省略 null 检查和越界检查
        data[i] *= 2;
    }
}
```

⚠️ 警告：`[[assume]]` 是所有属性中最危险的。如果你的假设是错的，程序的行为完全不可预测。笔者建议只在经过充分 profiling、确认瓶颈、并且你能 100% 保证条件总是成立的情况下使用它。在 99% 的代码中，你不需要它。

------

## C++20 [[nodiscard]] 增强

上一章已经提到，C++20 为 `[[nodiscard]]` 添加了自定义消息的能力。这里做一点补充说明。

### 标准库中的 nodiscard 扩展

C++20 还扩展了标准库中 `[[nodiscard]]` 的应用范围。现在以下标准库类型和函数也标记了 `[[nodiscard]]`：

- `std::unique_ptr` 和 `std::shared_ptr`（C++20 起）
- `std::vector::empty()`（C++20 起）
- `std::string::empty()`（C++20 起）

这意味着如果你写了 `vec.empty();` 而不是 `if (vec.empty())`，C++20 编译器会发出警告。以前这是一个常见的 bug 来源——`empty()` 看起来像是"清空"，实际上是"判空"。加了 `[[nodiscard]]` 之后，误用的代码至少会有警告提醒。

```cpp
std::vector<int> vec = {1, 2, 3};

// C++20 之前：不检查返回值，静默通过
vec.empty();  // 看起来像是清空操作，实际上什么都没做

// C++20：编译器发出 nodiscard 警告
vec.empty();  // warning: ignoring return value of 'empty()'
```

### 在自己的代码中使用 nodiscard 消息

对于库作者来说，`[[nodiscard("reason")]]` 非常实用。你可以在消息中解释为什么不应该忽略返回值，以及正确的使用方式：

```cpp
// 告诉调用方为什么需要检查返回值
[[nodiscard("Memory leak: returned pointer must be freed")]]
void* allocate_buffer(std::size_t size);

// 告诉调用方应该怎么用
[[nodiscard("Store the lock_guard to keep the mutex locked")]]
std::unique_lock<std::mutex> acquire_lock();
```

------

## 与 C++11-17 属性的对比

把 C++11-17 的属性和 C++20-23 的新属性放在一起对比，能看到一条清晰的发展脉络：早期属性关注代码正确性和可维护性，后期属性更关注性能优化。

| 属性 | 版本 | 关注点 | 风险 |
|------|------|--------|------|
| `[[noreturn]]` | C++11 | 正确性 | 低 |
| `[[carries_dependency]]` | C++11 | 性能 | 低 |
| `[[deprecated]]` | C++14 | 可维护性 | 低 |
| `[[nodiscard]]` | C++17 | 正确性 | 低 |
| `[[fallthrough]]` | C++17 | 正确性 | 低 |
| `[[maybe_unused]]` | C++17 | 可读性 | 低 |
| `[[likely]]/[[unlikely]]` | C++20 | 性能 | 低 |
| `[[no_unique_address]]` | C++20 | 性能 | 低 |
| `[[assume]]` | C++23 | 性能 | **高** |

其中只有 `[[assume]]` 是真正"危险"的属性——如果假设错误，后果是未定义行为。其他属性即使"提示"错了，最坏情况也只是性能略差，不会导致程序崩溃。

------

## 性能影响实测建议

对于 `[[likely]]`/`[[unlikely]]` 和 `[[assume]]` 这类性能导向的属性，笔者的建议是：加了之后一定要实测。优化效果高度依赖具体的硬件、编译器和代码上下文。有些场景收益明显，有些场景完全看不出差异。

测试方法可以是简单的：用 `perf stat` 或 `valgrind --tool=cachegrind` 对比加属性前后的指令数、分支预测失败率和缓存命中率。如果数据没有显著改善，就不值得加——因为属性会增加代码的"信息密度"，让读者多理解一个概念。

对于 `[[no_unique_address]]`，验证更直接——直接看 `sizeof` 的结果就好。如果空策略类确实不占空间，说明属性生效了。

------

## 小结

C++20-23 新增的属性把编译器提示的能力从"找 bug"扩展到了"做优化"。`[[likely]]` 和 `[[unlikely]]` 帮编译器做分支预测，`[[no_unique_address]]` 消除空类成员的内存浪费，`[[assume]]` 让编译器基于确定性假设做更激进的优化。

三个属性的风险不同。`[[no_unique_address]]` 基本无害——最坏情况就是优化没生效，`sizeof` 不变。`[[likely]]`/`[[unlikely]]` 风险也不高——最坏情况是分支预测提示错误，性能略差。`[[assume]]` 是唯一真正危险的属性——假设错误会导致未定义行为，必须谨慎使用。

在实践中，`[[no_unique_address]]` 在泛型代码中几乎可以无脑用（策略类模式），`[[likely]]`/`[[unlikely]]` 建议在 profiling 确认热点后再加，`[[assume]]` 只在极端性能敏感的场景中使用，并且一定要有对应的断言或测试来保证假设总是成立。

## 参考资源

- [cppreference: assume (C++23)](https://en.cppreference.com/w/cpp/language/attributes/assume)
- [cppreference: likely/unlikely (C++20)](https://en.cppreference.com/w/cpp/language/attributes/likely)
- [cppreference: no_unique_address (C++20)](https://en.cppreference.com/w/cpp/language/attributes/no_unique_address)
- [Don't use [[likely]] or [[unlikely]] - Aaron Ballman](https://blog.aaronballman.com/2020/08/dont-use-the-likely-or-unlikely-attributes/)
