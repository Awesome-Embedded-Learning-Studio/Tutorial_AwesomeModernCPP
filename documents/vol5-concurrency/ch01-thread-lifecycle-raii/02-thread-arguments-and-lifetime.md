---
chapter: 1
cpp_standard:
- 11
- 14
- 17
- 20
description: 深入线程参数的传递机制，识别悬垂引用与对象析构顺序引发的并发 bug
difficulty: intermediate
order: 2
platform: host
prerequisites:
- std::thread 基础
reading_time_minutes: 18
related:
- 线程所有权与 RAII
- CPU cache 与 OS 线程
tags:
- host
- cpp-modern
- intermediate
- 内存管理
title: 线程参数与生命周期
---
# 线程参数与生命周期

上一篇我们学会了 `std::thread` 的基本操作——创建、join、detach、获取 ID。当时我们有意无意地绕过了一个非常重要的话题：传给线程的参数，到底是怎么到线程函数手里的？为什么有时候我明明传了一个引用进去，线程里改了外面的变量却没变？为什么用了 `std::ref` 之后，程序有时候又莫名其妙地崩了？

这一篇我们就来彻底拆解这些问题。`std::thread` 的参数传递机制有一个非常核心的设计决策——**decay-copy**（退化拷贝），它决定了所有参数在概念上都是按值传递的。理解了这个机制，你就能看懂一大类并发 bug 的根源。然后我们再往深处走：悬垂引用、`this` 指针捕获、对象析构顺序、lambda 的引用捕获陷阱——这些问题的本质都是同一个东西：**线程的生命周期超出了它所引用的对象的生命周期**。

## decay-copy：所有参数都是按值传递的

先说一个可能会让你感到意外的事实：不管你的线程函数签名怎么写，`std::thread` 的构造函数**总是**按值拷贝（或移动）所有传入的参数。这个行为叫做 decay-copy——参数的类型会经过跟函数模板参数推导一样的退化过程：引用被剥离、`const`/`volatile` 被丢弃、数组退化为指针、函数退化为函数指针。

我们用代码来看这个行为：

```cpp
#include <thread>
#include <iostream>

void update_value(int& x)
{
    x = 42;
    std::cout << "Thread: set x to " << x << "\n";
}

int main()
{
    int value = 0;
    // 编译错误！decay-copy 后 int& 变成了 int
    // std::thread t(update_value, value);
    // 错误信息大致是：std::thread 的参数需要能转换为 decay-copy 后的类型

    // 正确的做法：用 std::ref 显式包装引用
    std::thread t(update_value, std::ref(value));
    t.join();
    std::cout << "Main: value = " << value << "\n";
    return 0;
}
```

如果你把 `std::ref(value)` 改成直接传 `value`，编译器会报错——因为 `update_value` 的参数是 `int&`，但 `std::thread` 内部存的是 `int`（decay-copy 之后），一个右值 `int` 无法绑定到非 const 引用上。这个编译错误其实是标准库在保护你：如果你传了一个局部变量的引用给线程，而线程可能在那个变量销毁之后才去访问它，结果就是悬垂引用——比编译错误糟糕一万倍。

decay-copy 的设计动机非常明确：**让每个线程默认拥有自己的一份参数副本，避免隐式的共享状态**。共享状态是并发 bug 的温床，C++ 标准库选择了一种"默认安全"的策略——如果你想共享，必须显式地说出来（用 `std::ref`），这样至少在代码审查的时候，`std::ref` 这个词会像一个醒目的标记一样提醒你：这里有共享，需要检查生命周期。

### std::ref 与 std::cref：显式引用包装

`std::ref` 和 `std::cref` 是 `<functional>` 中定义的引用包装器。它们把一个引用"包装"成一个可以被拷贝的对象，内部持有原始对象的地址。当 `std::thread` 把这个包装器传给线程函数时，线程函数收到的是对原始对象的引用——而不是拷贝。

```cpp
#include <thread>
#include <iostream>
#include <functional>
#include <string>

void append_suffix(std::string& str, const std::string& suffix)
{
    str += suffix;
}

int main()
{
    std::string message = "Hello";
    std::string suffix = " World";

    std::thread t(append_suffix, std::ref(message), std::cref(suffix));
    t.join();

    std::cout << message << "\n";  // 输出 "Hello World"
    return 0;
}
```

`std::ref(message)` 让线程函数中的 `str` 参数绑定到 `main` 中的 `message` 变量；`std::cref(suffix)` 让 `suffix` 参数绑定到常量引用。这里 `join()` 保证了线程在 `message` 和 `suffix` 的作用域内完成，所以是安全的。

但如果你把 `join()` 改成 `detach()` 呢？主线程可能在 `message` 销毁之后，后台线程还在修改它——这就是一个经典的 use-after-free。`std::ref` 打开了共享状态的大门，但也意味着你必须自己保证被引用对象的生命周期覆盖线程的整个执行期。标准库帮不了你。

## 移动语义：把 move-only 类型传入线程

并非所有类型都能拷贝。`std::unique_ptr`、`std::thread` 本身、以及很多自定义的资源管理类都是 move-only 的——它们只支持移动，不支持拷贝。`std::thread` 的构造函数接受右值引用参数，所以你可以直接移动这些对象到线程中：

```cpp
#include <thread>
#include <iostream>
#include <memory>

void process_data(std::unique_ptr<int[]> data, std::size_t size)
{
    for (std::size_t i = 0; i < size; ++i) {
        data[i] *= 2;
    }
    std::cout << "First element after processing: "
              << data[0] << "\n";
}

int main()
{
    constexpr std::size_t kSize = 10;
    auto data = std::make_unique<int[]>(kSize);
    for (std::size_t i = 0; i < kSize; ++i) {
        data[i] = static_cast<int>(i);
    }

    // 移动 unique_ptr 到线程中
    std::thread t(process_data, std::move(data), kSize);
    t.join();

    // data 在移动后为 nullptr
    std::cout << "data after move: "
              << (data ? "not null" : "null") << "\n";
    return 0;
}
```

`std::move(data)` 把 `unique_ptr` 的所有权转移给了线程的内部存储。线程启动后，`process_data` 收到的 `data` 参数拥有那块内存的唯一所有权——没有任何人能同时访问它，所以不存在 data race。线程执行完毕后，`unique_ptr` 在线程函数返回时自动释放内存。这是一个非常干净的所有权转移模式：谁拥有数据，谁负责释放，绝不共享。

同样的模式也适用于移动 `std::thread` 对象本身。你不能拷贝一个线程对象（`std::thread` 的拷贝构造函数是删除的），但你可以移动它，把线程的所有权从一个管理对象转移到另一个管理对象——这个话题我们会在下一篇"线程所有权与 RAII"中展开。

## 悬垂引用：detach 的头号杀手

接下来我们进入这一篇最核心的部分——悬垂引用。它是 `std::thread` 使用中最常见、也最阴险的 bug 来源。它的特征是：程序有时候能正常工作，有时候崩溃，有时候给出错误结果——完全取决于线程的执行速度和操作系统的调度策略。

### 场景 1：detach 后访问已销毁的局部变量

```cpp
#include <thread>
#include <iostream>
#include <chrono>

void faulty_function()
{
    int local_value = 42;

    std::thread t([&local_value]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        // local_value 可能已经被销毁了！
        std::cout << "Value: " << local_value << "\n";
    });
    t.detach();
    // faulty_function 返回后，local_value 被销毁
    // 但线程还在 100ms 后访问它 -> 未定义行为
}

int main()
{
    faulty_function();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    return 0;
}
```

`faulty_function` 返回后，`local_value` 作为栈变量被销毁了。但 detach 的线程还在后台跑着，100ms 后它会尝试读取 `local_value` 所在的内存——而那块内存已经被回收，可能被其他函数调用覆盖了。这就是经典的悬垂引用：引用还在，但它指向的内存已经不是原来的对象了。

这种 bug 最让人头疼的地方在于它**不是必现的**。如果 `faulty_function` 的调用者碰巧等了足够长的时间（比如在上面的 `main` 里 `sleep` 了 200ms，而线程只需要 100ms），程序就能正常跑。但如果调度延迟了那么一点点——比如系统负载高的时候——线程还没来得及读完数据，函数就返回了，bug 就触发了。在测试环境里可能跑了一万次都不出问题，上线后在客户环境凌晨三点崩一次，你根本无从复现。

### 场景 2：this 指针捕获

面向对象编程中，成员函数经常通过 lambda 捕获 `this` 来启动线程。但如果对象的寿命比线程短呢？

```cpp
#include <thread>
#include <iostream>
#include <chrono>
#include <atomic>

class BackgroundWorker {
public:
    BackgroundWorker() : running_(false) {}

    void start()
    {
        running_ = true;
        std::thread t([this]() {
            while (running_) {
                std::cout << "Working...\n";
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }
        });
        t.detach();
    }

    void stop()
    {
        running_ = false;
    }

    ~BackgroundWorker()
    {
        stop();
        // 问题：detach 的线程可能还在跑！
        // 它持有的 this 指针指向的对象正在被销毁
    }

private:
    std::atomic<bool> running_;
};

int main()
{
    {
        BackgroundWorker worker;
        worker.start();
        // worker 在这里析构，但 detach 的线程还在用 this
    }
    std::this_thread::sleep_for(std::chrono::seconds(2));
    // 线程还在访问已销毁的 worker 的成员 -> 未定义行为
    return 0;
}
```

`worker.start()` 启动了一个 detach 的线程，线程通过捕获的 `this` 指针访问 `running_` 成员变量。当 `worker` 在作用域结束时析构，`this` 指针变成了悬垂指针——它指向的内存已经被回收了。线程后续对 `running_` 的访问是未定义行为。

你可能觉得："那我在析构函数里 `stop()` 了呀，`running_` 被设成了 `false`，线程会自己退出的。" 问题在于 `detach` 之后你**没有任何机制等待线程真正退出**。`stop()` 把 `running_` 设成 `false` 就返回了，线程可能要等到下一个循环迭代才会检查这个标志——而此时 `worker` 已经析构完毕了。如果线程在 `running_` 被设成 `false` 之后、下次检查之前有一段 `sleep`，那这个时间窗口就更大了。

正确的做法是不要 detach，而是持有线程对象并在析构时 join——我们稍后会看到修复版本。

### 场景 3：引用捕获的 Lambda 陷阱

Lambda 的引用捕获 `[&]` 在单线程代码里用起来非常方便——不需要关心生命周期，因为 lambda 的执行和被捕获变量的生命周期是在同一个执行流里的。但在多线程中，这就变成了一个陷阱：

```cpp
#include <thread>
#include <iostream>
#include <vector>
#include <chrono>

void parallel_square_incorrect(const std::vector<int>& input,
                                std::vector<int>& output)
{
    std::vector<std::thread> threads;

    // 危险：[&] 捕获了 input 和 output 的引用
    // 以及 i 的引用！
    for (std::size_t i = 0; i < input.size(); ++i) {
        threads.emplace_back([&, i]() {
            // i 是值捕获，OK
            // 但 input 和 output 是引用捕获
            // 如果 parallel_square_incorrect 返回后线程还在跑...
            output[i] = input[i] * input[i];
        });
    }

    for (auto& t : threads) {
        t.join();
    }
    // 这里 join 了，所以在这个函数内部是安全的
    // 但如果把 join 改成 detach，就是灾难
}

int main()
{
    std::vector<int> data(100);
    std::vector<int> result(100);
    for (int i = 0; i < 100; ++i) {
        data[i] = i;
    }

    parallel_square_incorrect(data, result);

    std::cout << "result[5] = " << result[5] << "\n";  // 25
    return 0;
}
```

这段代码实际上是安全的——因为函数在返回之前 join 了所有线程。但它的"安全"是非常脆弱的：只要有人把 `join` 改成 `detach`（可能觉得"我不需要等结果"），立刻就变成了悬垂引用 bug。而且 `[&]` 是一个"一刀切"的捕获方式——它捕获了所有局部变量的引用，包括你本来不想捕获的那些。万一以后函数里加了一个临时变量，它也会被隐式地捕获进来。

相比之下，显式地写出捕获列表（`[&input, &output, i]` 或者干脆用参数传递）会让意图更清晰，也更容易审查。C++17 引入了 `[=, *this]` 来按值捕获整个对象的副本（而不是仅捕获 `this` 指针），而 C++20 则更进一步废弃了 `[=]` 对 `this` 的隐式捕获——现在必须显式写出 `[=, this]`。这些变化让捕获的语义更加明确。但不管语法怎么变，核心原则不变：**被引用的对象必须在引用者（线程）的整个生命周期内保持有效**。

## 修复模式：复制到线程，或用 shared_ptr 延长生命周期

知道了问题在哪里，修复的思路就很直白了。主要有两种策略。

### 策略 1：把数据复制到线程中

最简单也最安全的做法是让每个线程拥有一份自己的数据副本——这正好是 `std::thread` decay-copy 的默认行为。

```cpp
#include <thread>
#include <iostream>
#include <string>

void safe_version()
{
    std::string message = "Hello from parent";

    // 值捕获：拷贝 message 到线程中
    std::thread t([message]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        // 这里访问的是 message 的副本，跟外部的 message 无关
        std::cout << "Thread sees: " << message << "\n";
    });
    t.detach();

    // 现在即使 message 被销毁了也无所谓
    // 线程持有自己的副本
}
```

把 `[&message]` 改成 `[message]`（值捕获），lambda 会拷贝一份 `message` 到自己的闭包对象中。`std::thread` 又会把这个闭包对象 decay-copy 到线程的内部存储。这样，线程持有的就完全是自己的数据了，跟外部的 `message` 没有任何关联。detach 之后也不存在悬垂引用的问题。

这个策略的代价是额外的内存拷贝。对于小对象（`int`、指针）来说无所谓，但对于大对象（一个大 vector、一个巨大的字符串）来说可能有性能影响。不过在并发编程中，正确性永远排在性能前面——先保证正确，再优化性能。如果拷贝的开销真的不可接受，那就用下面这个策略。

### 策略 2：用 shared_ptr 延长生命周期

当数据不能拷贝（或者拷贝代价太大），又需要在线程之间共享时，`std::shared_ptr` 是一个很好的折中方案：它通过引用计数自动管理共享数据的生命周期，只要还有 `shared_ptr` 指向它，数据就不会被销毁。

```cpp
#include <thread>
#include <iostream>
#include <memory>
#include <chrono>

class BackgroundWorker {
public:
    BackgroundWorker() : running_(std::make_shared<std::atomic<bool>>(true)) {}

    void start()
    {
        // 捕获 shared_ptr（值捕获），引用计数 +1
        auto running = running_;
        std::thread t([running]() {
            while (running->load()) {
                std::cout << "Working...\n";
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(500));
            }
            std::cout << "Worker exiting cleanly\n";
        });
        t.detach();
        // 线程持有 running 的副本，shared_ptr 引用计数为 2
        // 即使 BackgroundWorker 析构，running 指向的对象仍然存活
    }

    void stop()
    {
        running_->store(false);
    }

    ~BackgroundWorker()
    {
        stop();
        // running_ 析构时引用计数 -1
        // 但线程还持有一个副本，所以 running 指向的对象不会销毁
        // 线程最终退出时，它持有的 shared_ptr 也析构，引用计数归零
        // 此时对象才真正被销毁
    }

private:
    std::shared_ptr<std::atomic<bool>> running_;
};

int main()
{
    {
        BackgroundWorker worker;
        worker.start();
    }
    // worker 已析构，但线程还在安全地运行
    std::this_thread::sleep_for(std::chrono::seconds(2));
    return 0;
}
```

这个版本的关键改动是把 `running_` 从 `std::atomic<bool>` 改成了 `std::shared_ptr<std::atomic<bool>>`，然后 lambda 通过值捕获拿到了 `shared_ptr` 的一个副本。这样就有两个 `shared_ptr` 指向同一个 `atomic<bool>` 对象：一个在 `BackgroundWorker` 里，一个在 detach 的线程里。

当 `BackgroundWorker` 析构时，它调用 `stop()` 把 `running` 设成 `false`，然后 `running_` 这个 `shared_ptr` 析构，引用计数从 2 变成 1。但 `atomic<bool>` 对象不会被销毁——因为线程还持有一个 `shared_ptr` 副本。线程最终检测到 `running` 为 `false`，退出循环，lambda 返回，它持有的 `shared_ptr` 析构，引用计数归零，`atomic<bool>` 对象这才被安全地销毁。

这个模式非常实用，但它也有一个需要注意的地方：`shared_ptr` 本身的引用计数操作是原子的（线程安全的），但它指向的对象的访问是否安全仍然需要你自己保证。在上面的例子中，`atomic<bool>` 本身就是线程安全的，所以没问题。但如果你用 `shared_ptr<std::vector<int>>` 在多个线程之间共享一个 vector，你对 vector 的并发访问仍然需要同步——`shared_ptr` 只保证了对象不会被过早销毁，不保证对象内部的线程安全。

### 更好的选择：不要 detach

说了这么多修复方案，笔者个人的建议是：**在绝大多数场景下，不要使用 detach**。用 join 配合 RAII（在线程对象的析构函数中自动 join）可以避免几乎所有的悬垂引用问题——因为 join 保证了线程在作用域退出之前完成，而被引用的对象至少活到作用域结束。

上面的 `BackgroundWorker` 用 join 模式写出来是这样的：

```cpp
#include <thread>
#include <iostream>
#include <memory>
#include <atomic>
#include <chrono>

class BackgroundWorker {
public:
    BackgroundWorker() : running_(false) {}

    void start()
    {
        running_ = true;
        thread_ = std::thread([this]() {
            while (running_) {
                std::cout << "Working...\n";
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(500));
            }
            std::cout << "Worker exiting cleanly\n";
        });
    }

    void stop()
    {
        running_ = false;
    }

    ~BackgroundWorker()
    {
        stop();
        if (thread_.joinable()) {
            thread_.join();
        }
        // join 保证了线程在析构完成之前退出
        // 不存在 this 指针悬垂的问题
    }

private:
    std::atomic<bool> running_;
    std::thread thread_;
};

int main()
{
    {
        BackgroundWorker worker;
        worker.start();
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
    // worker 析构时先 stop 再 join
    // 线程干净地退出，没有悬垂引用
    return 0;
}
```

这个版本简洁得多——不需要 `shared_ptr`，不需要担心引用计数，析构函数里 `stop()` + `join()` 就是全部的逻辑。`join()` 是同步点，它保证了线程在 `join` 返回时已经完全执行完毕，之后 `worker` 的成员变量才被销毁。时间顺序是确定的，不存在竞态。

所以，修复 lifetime bug 的终极策略其实是回归到 `std::thread` 设计的原始意图：**用 join 来同步线程的退出，用 RAII 来保证 join 一定被执行**。detach 是一个有明确语义的工具（"我真的不在乎它什么时候结束"），但在实践中，"不在乎"往往是"没想清楚"的代名词。

> 💡 完整示例代码在 [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP)，访问 `code/volumn_codes/vol5/ch01-thread-lifecycle-raii/`。

## 练习

### 练习 1：识别 lifetime bug

下面三段代码各有一个 lifetime bug，请逐一指出问题所在并修复。

**代码片段 A：**

```cpp
void spawn_printer()
{
    std::string msg = "Hello from detach!";
    std::thread t([&msg]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        std::cout << msg << "\n";
    });
    t.detach();
}
```

**代码片段 B：**

```cpp
class TaskRunner {
public:
    void run(int iterations)
    {
        for (int i = 0; i < iterations; ++i) {
            threads_.emplace_back([this, i]() {
                results_[i] = compute(i);
            });
        }
    }

    ~TaskRunner()
    {
        for (auto& t : threads_) {
            t.join();
        }
    }

    const std::vector<int>& results() const { return results_; }

private:
    int compute(int n) { return n * n; }
    std::vector<std::thread> threads_;
    std::vector<int> results_;
};
```

**代码片段 C：**

```cpp
void process(std::vector<int>& output)
{
    int counter = 0;
    std::thread t([&output, &counter]() {
        for (int i = 0; i < 100; ++i) {
            output.push_back(counter++);
        }
    });
    // 程序员忘了 join 或 detach
}
```

提示：代码片段 A 的问题是 detach + 引用捕获；代码片段 B 的问题不在线程管理本身，而在 `results_` 的大小和并发访问；代码片段 C 的问题最直接——忘了 join/detach 会导致 `std::terminate`。

### 练习 2：用 shared_ptr 修复 this 指针捕获

把上面的"代码片段 B"改用 `std::shared_ptr` 的模式，让 `TaskRunner` 可以安全地 detach 线程。确保 `results_` 在所有线程都完成之前不会被销毁。

### 练习 3：编写一个线程安全的 RAII 包装器

写一个简单的类 `ScopedThread`，它在构造时接受一个 `std::thread` 对象，在析构时自动 `join()`。确保它正确处理以下情况：

1. 传入的线程已经 join 过（`joinable() == false`）
2. 传入一个默认构造的线程对象
3. `ScopedThread` 对象被移动（移动后原对象不应该在析构时 join）

测试代码：

```cpp
int main()
{
    {
        ScopedThread st(std::thread([]() {
            std::cout << "Hello from scoped thread\n";
        }));
        // st 析构时自动 join
    }
    std::cout << "ScopedThread destroyed, thread joined\n";
    return 0;
}
```

这个练习是下一篇"线程所有权与 RAII"的预习——你将亲手实现最基本的线程 RAII 包装器。

## 参考资源

- [std::thread constructor — cppreference](https://en.cppreference.com/w/cpp/thread/thread/thread)
- [std::ref, std::cref — cppreference](https://en.cppreference.com/w/cpp/utility/functional/ref)
- [C++ Core Guidelines: CP.24 — Think of a thread as a global container](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#cp24-think-of-a-thread-as-a-global-container)
- [C++ Core Guidelines: CP.25 — Prefer gsl::joining_thread over std::thread](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#cp25-prefer-gsljoining_thread-over-stdthread)
- [Top 20 C++ Multithreading Mistakes and How to Avoid Them — A Coder's Journey](https://acodersjourney.com/top-20-cplusplus-multithreading-mistakes/)
- [Abseil Tip of the Week #180: Avoiding Dangling References](https://abseil.io/tips/180)
