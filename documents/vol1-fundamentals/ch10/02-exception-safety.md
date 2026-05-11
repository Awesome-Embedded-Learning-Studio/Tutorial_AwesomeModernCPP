---
title: "异常安全"
description: "理解异常安全的四个等级，掌握 RAII 守卫模式确保资源在异常发生时正确释放"
chapter: 10
order: 2
difficulty: intermediate
reading_time_minutes: 12
platform: host
prerequisites:
  - "异常基础"
tags:
  - cpp-modern
  - host
  - intermediate
  - 进阶
cpp_standard: [11, 14, 17, 20]
---

# 异常安全

抛出一个异常很容易——`throw std::runtime_error("oops")` 一行就够了。但真正让人头疼的问题是：异常飞过的时候，在此之前已经打开的文件、分配的内存、锁住的互斥量……这些东西谁来收拾？如果没人管，轻则内存泄漏，重则程序状态彻底崩坏。异常安全（exception safety）讨论的就是这件事——不是"怎么抛异常"，而是"异常发生后，程序的状态还能不能看"。

我们先明确一个大前提：异常安全不是二选一的"安全或不安全"，而是一个从差到好的**四个等级**。理解这四个等级，我们才能在设计函数和类的时候有意识地选择自己要达到的安全级别，并且知道为此需要付出什么代价。

## 异常安全的四个等级

### 无保证（No Guarantee）

这是最糟糕的情况——如果异常发生，对象可能处于不一致的状态，资源可能泄漏，程序的行为完全不可预测。听起来好像谁也不会故意写出这种代码，但实际上只要你在裸用 `new`/`delete` 而没有用任何 RAII 包装，你就已经处于这个级别了：

```cpp
void no_guarantee() {
    int* data = new int[100];
    fill_data(data, 100);     // 如果这里抛异常...
    process_data(data, 100);  // ...或者这里...
    delete[] data;            // 这行永远不会执行，内存泄漏
}
```

这段代码在正常路径下工作得很好——`data` 被分配、使用、然后释放。但一旦 `fill_data` 或 `process_data` 抛出异常，程序流程会直接跳转到最近的 `catch` 块，`delete[] data` 永远不会执行。更糟糕的是，如果 `no_guarantee` 本身没有 `catch`，调用者甚至不知道有资源泄漏了——异常被无声无息地传播出去，留下的只有一块没人管的堆内存。

### 基本保证（Basic Guarantee）

基本保证承诺两件事：第一，不会泄漏资源；第二，对象仍然处于一个**合法**的状态——你可以对它调用析构函数、可以给它赋新值，程序不会崩溃。但这个状态的具体内容是**不确定的**——你不能假设数据还是调用前的值，只知道它是一个"合理的、可用的"状态。

所有标准库容器都至少提供基本保证。比如 `std::vector::push_back` 如果在扩容时因为内存不足而抛出 `std::bad_alloc`，vector 本身仍然处于合法状态——你可以继续对它进行操作——但之前插入的元素是否还在、容量变成了多少，这些都不确定。

实现基本保证的核心手段就是 RAII：如果所有资源（内存、文件句柄、锁）都由 RAII 对象管理，那么当异常发生时，栈展开（stack unwinding）会自动调用所有局部对象的析构函数，资源一定会被正确释放。我们稍后会详细展开这一点。

### 强保证（Strong Guarantee）

强保证比基本保证更严格：操作要么**完全成功**，要么**完全回滚**——如果异常发生，对象的状态和调用前一模一样，就好像这个操作从来没执行过。这也就是所谓的"事务语义"。

典型的实现方式是 **copy-and-swap 惯用法**：先对副本进行修改，如果修改过程中没有异常，再把副本和原对象交换。因为交换操作（`std::swap`）本身承诺不抛异常，所以整个操作要么成功、要么原对象完全不变。后面我们会用一个简短的例子展示这种思路。

### 不抛异常保证（Nothrow Guarantee）

这是最高等级：函数承诺**永远不会**抛出异常。在 C++11 及以后，用 `noexcept` 关键字来标记这种函数。析构函数默认是 `noexcept` 的——这是一个非常重要的设计决策，因为栈展开过程中析构函数一定会被调用，如果析构函数本身抛出异常，程序会直接调用 `std::terminate` 终止。

一些简单的操作天然就是不抛异常的：内置类型的赋值、指针的拷贝、`std::swap` 对内置类型和大多数标准容器的特化版本。在设计类的时候，如果析构函数、`swap` 函数和移动赋值运算符能做到 `noexcept`，会给调用者带来很大的便利——标准库的很多操作（比如 `std::vector::push_back`）会根据元素类型是否 `noexcept` 来选择更高效的实现路径。

## RAII 与异常安全

我们现在回过头来看 RAII 为什么是实现基本保证的**核心机制**。原理其实很简单：C++ 的异常处理机制保证，在栈展开过程中，所有局部对象的析构函数都会被调用。那只要我们把资源的获取放在构造函数里、释放放在析构函数里，异常发生的时候资源就一定会被正确清理——不用写任何额外的 `try-catch`。

我们来看一个改造前后的对比。首先是"危险"版本：

```cpp
// 危险：裸指针 + 异常 = 泄漏
void unsafe_process() {
    int* buffer = new int[1024];
    double* temp  = new double[512];

    do_work(buffer, temp);  // 如果这里抛异常呢？

    delete[] temp;
    delete[] buffer;
}
```

如果 `do_work` 抛出异常，`buffer` 和 `temp` 全部泄漏。你可能想用 `try-catch` 包一下，但如果有三个、四个资源呢？代码会迅速膨胀成意大利面条。现在我们用 RAII 改造：

```cpp
// 安全：RAII 守卫，异常发生时自动清理
void safe_process() {
    auto buffer = std::make_unique<int[]>(1024);
    auto temp   = std::make_unique<double[]>(512);

    do_work(buffer.get(), temp.get());

    // 不管 do_work 是否抛异常，buffer 和 temp 都会在
    // 离开作用域时被自动释放
}
```

`std::unique_ptr` 的析构函数会调用 `delete[]`，而栈展开保证析构函数一定被执行。不需要任何 `try-catch`，不需要任何手动的清理逻辑——这就是 RAII 的威力。事实上，RAII 的核心理念可以浓缩成一句话：**资源的生命周期应该和某个对象的生命周期绑定**。只要做到这一点，异常安全就是自然而然的副产品。

> **踩坑预警**：RAII 的前提是"所有资源都被 RAII 对象管理"。如果你在函数里混用了 RAII 和裸指针——比如用 `std::unique_ptr` 管理了一块内存，但同时又 `fopen` 了一个文件句柄裸放着——那个文件句柄仍然会在异常时泄漏。**要 RAII 就 RAII 到底，不要半吊子**。对于文件句柄，标准库没有直接的 RAII 包装（C++ 没有 `std::file_ptr`），但我们可以自己写一个简单的守卫类——后面的练习会让你动手做这件事。

## lock_guard：一个具体的 RAII 守卫

`std::lock_guard<std::mutex>` 是 RAII 在并发编程中最经典的落地案例。它的实现原理简洁得令人赞叹：构造函数里调用 `mutex.lock()`，析构函数里调用 `mutex.unlock()`。仅此而已。

```cpp
#include <mutex>

std::mutex g_mutex;
int g_counter = 0;

void increment_unsafe() {
    g_mutex.lock();
    ++g_counter;
    // 如果 do_something() 抛异常...
    do_something();
    // ...这行 unlock 永远不会执行
    g_mutex.unlock();
    // 结果：互斥量永远被锁住，所有后续线程死锁
}
```

如果 `do_something()` 抛出了异常，`unlock()` 不会被执行，互斥量永远处于锁定状态——所有试图获取这个互斥量的线程都会被永久阻塞。这就是经典的死锁场景。用 `lock_guard` 改造后：

```cpp
#include <mutex>

void increment_safe() {
    std::lock_guard<std::mutex> lock(g_mutex);  // 构造时 lock()
    ++g_counter;
    do_something();  // 即使抛异常...
    // 析构时 unlock()，无论如何都会执行
}
```

不管 `do_something()` 是否抛出异常、不管函数从哪个 `return` 语句退出，`lock_guard` 的析构函数都会被调用，互斥量一定会被释放。这就是为什么我们说 RAII 守卫把"资源管理的正确性"从"程序员别忘记"变成了"语言机制保证"——前者依赖人的记忆力，后者依赖编译器的行为规范，显然后者靠谱得多。

> **踩坑预警**：`lock_guard` 的生命周期是从声明处到所在作用域结束。如果你在函数开头就锁住了互斥量，到函数末尾才释放，那持锁时间可能远超实际需要——这在多线程程序中会成为严重的性能瓶颈。如果只需要保护一小段操作，可以用一对大括号创建一个子作用域来精确控制 `lock_guard` 的生命周期。更灵活的选择是 `std::unique_lock`，它允许你手动 `lock()` 和 `unlock()`，同时仍然保证析构时一定释放——但灵活性的代价是更重的对象和稍大的运行时开销。

## copy-and-swap：通往强保证的路径

基本保证告诉我们"不会泄漏、状态合法"，但有时候我们需要更强的承诺——"要么成功，要么什么都没发生过"。这就是强保证，而实现它最常用的手法就是 copy-and-swap。

思路是这样的：我们不直接修改原对象，而是先做一份拷贝，在拷贝上进行修改。如果修改过程中出了问题（抛了异常），原对象完全不受影响——因为改的只是拷贝。如果修改顺利完成，我们把修改好的拷贝和原对象交换——交换操作本身是 `noexcept` 的，不可能失败。

```cpp
class ConfigManager {
private:
    std::vector<std::string> entries_;

public:
    // 强异常保证：要么全部更新，要么完全不变
    void update_entries(const std::vector<std::string>& new_entries) {
        std::vector<std::string> temp = new_entries;  // 拷贝，可能抛异常

        // 在 temp 上做各种校验和修改
        validate_and_normalize(temp);  // 可能抛异常

        // 到这里说明一切正常，交换——noexcept，不会失败
        using std::swap;
        swap(entries_, temp);
    }  // temp（原来的 entries_）在作用域结束时自动销毁
};
```

如果在 `validate_and_normalize` 中抛了异常，`entries_` 的内容完全没被碰过；如果一切顺利，`swap` 把新数据装进去、把旧数据交给 `temp`，然后 `temp` 析构时自动清理。整个过程不需要任何 `try-catch`。

copy-and-swap 是一个非常值得掌握的惯用法，不过在资源受限的嵌入式场景下，做完整拷贝的内存开销可能不可接受。我们这里只是先建立概念，后续在卷 2 深入讲解 RAII 和资源管理的时候会专门展开讨论它的各种变体和权衡。

## 实战：异常安全对比

现在我们把前面的知识串起来，写一段完整的对比代码——同样的功能，一个用裸指针（不安全），一个用 RAII（安全），看看异常发生时的行为差异。

```cpp
// safety.cpp
// 演示异常安全与不安全代码的行为对比

#include <cstdio>
#include <memory>
#include <stdexcept>

void might_throw(bool should_fail) {
    if (should_fail) {
        throw std::runtime_error("Something went wrong!");
    }
    std::puts("  Operation succeeded.");
}

// ---- 不安全版本 ----
void unsafe_version() {
    std::puts("[Unsafe] Allocating resources...");
    int* data = new int[100];
    double* temp = new double[50];
    std::puts("[Unsafe] Resources allocated. Starting work...");

    might_throw(true);  // 故意触发异常

    delete[] temp;
    delete[] data;
    std::puts("[Unsafe] Resources released.");
}

// ---- 安全版本 ----
void safe_version() {
    std::puts("[Safe] Allocating resources...");
    auto data = std::make_unique<int[]>(100);
    auto temp = std::make_unique<double[]>(50);
    std::puts("[Safe] Resources allocated. Starting work...");

    might_throw(true);  // 同样触发异常

    std::puts("[Safe] Resources released.");
}

int main() {
    // 测试不安全版本
    std::puts("=== Testing unsafe version ===");
    try {
        unsafe_version();
    } catch (const std::exception& e) {
        std::printf("  Caught: %s\n", e.what());
    }
    std::puts("  Note: memory leaked! data and temp were never freed.\n");

    // 测试安全版本
    std::puts("=== Testing safe version ===");
    try {
        safe_version();
    } catch (const std::exception& e) {
        std::printf("  Caught: %s\n", e.what());
    }
    std::puts("  Note: no leak! unique_ptr destructors cleaned up.\n");

    return 0;
}
```

编译运行：

```bash
g++ -std=c++17 -Wall -Wextra safety.cpp -o safety && ./safety
```

预期输出：

```text
=== Testing unsafe version ===
[Unsafe] Allocating resources...
[Unsafe] Resources allocated. Starting work...
  Caught: Something went wrong!
  Note: memory leaked! data and temp were never freed.

=== Testing safe version ===
[Safe] Allocating resources...
[Safe] Resources allocated. Starting work...
  Caught: Something went wrong!
  Note: no leak! unique_ptr destructors cleaned up.
```

两个版本的执行路径几乎一模一样——都是在资源分配后、释放前触发了异常。区别在于：不安全版本的两块内存（`data` 和 `temp`）永远不会被释放，而安全版本的 `std::unique_ptr` 在栈展开时自动调用了 `delete[]`，没有任何泄漏。这就是 RAII 带来的实质性区别——代码甚至比裸指针版本更短，因为不需要手写 `delete`。

> **踩坑预警**：在实际项目中，内存泄漏不会像这个例子这样"安静"——它可能在长时间运行后慢慢蚕食可用内存，最终导致系统崩溃，而且崩溃的位置和泄漏的位置往往毫无关系。Valgrind 和 AddressSanitizer 是检测这类问题的利器，编译时加上 `-fsanitize=address` 就能启用 ASan，它会在泄漏发生的第一时间报告，远比事后排查要高效。之后也许笔者会好好介绍这几个好用的小工具！

## 练习

### 练习一：改造不安全代码

下面这段代码存在多个异常安全问题，试着找出所有问题并改造成异常安全的版本：

```cpp
void process_file(const char* path) {
    FILE* f = std::fopen(path, "r");
    char* buffer = new char[4096];

    read_and_process(f, buffer);  // 可能抛异常

    delete[] buffer;
    std::fclose(f);
}
```

提示：思考一下，如果 `read_and_process` 抛异常，哪些资源会泄漏？用 RAII 思路重写，`FILE*` 可以用一个自定义的守卫类来管理。

### 练习二：实现 ScopedFile

自己动手写一个 `ScopedFile` 类——构造函数接受文件路径和模式，调用 `std::fopen`；析构函数调用 `std::fclose`。要求禁用拷贝（因为拷贝会导致同一个 `FILE*` 被 `fclose` 两次），但支持移动语义。参考接口：

```cpp
class ScopedFile {
public:
    explicit ScopedFile(const char* path, const char* mode);
    ~ScopedFile();

    ScopedFile(const ScopedFile&) = delete;
    ScopedFile& operator=(const ScopedFile&) = delete;

    ScopedFile(ScopedFile&& other) noexcept;
    ScopedFile& operator=(ScopedFile&& other) noexcept;

    FILE* get() const noexcept;
    explicit operator bool() const noexcept;
};
```

## 小结

这一篇我们集中讨论了异常安全这个主题。异常安全的四个等级构成了一个从弱到强的阶梯：无保证（什么都不管）、基本保证（不泄漏、状态合法）、强保证（要么成功要么回滚）、不抛异常保证（永远不会抛出异常）。在这四个等级中，RAII 是实现基本保证的核心机制——只要把所有资源的生命周期绑定到对象上，栈展开就会替你完成所有的清理工作。`std::lock_guard` 是 RAII 在并发场景中的经典落地，而 copy-and-swap 惯用法则提供了通往强保证的路径。

一个实用的设计原则是：**默认追求基本保证，在关键操作上追求强保证，让析构函数和移动操作做到不抛异常**。不需要每行代码都追求最高等级——那既不现实也不必要——但要确保你的代码至少不会在异常飞过的时候留下一地碎片。

下一篇我们会跳出异常的框架，从更高的视角对比 C++ 中几种主要的错误处理方式：异常、返回值/错误码、`std::optional`、`std::expected`，看看它们各自适合什么场景，以及在实际项目中如何选择。
