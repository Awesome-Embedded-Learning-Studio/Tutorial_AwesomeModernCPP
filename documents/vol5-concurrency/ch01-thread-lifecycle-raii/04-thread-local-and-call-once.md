---
chapter: 1
cpp_standard:
- 11
- 14
- 17
- 20
description: 掌握线程局部存储与一次性初始化机制，写出线程安全的懒初始化与全局状态
difficulty: intermediate
order: 4
platform: host
prerequisites:
- 线程所有权与 RAII
reading_time_minutes: 19
related:
- 线程参数与生命周期
tags:
- host
- cpp-modern
- intermediate
- 内存管理
title: thread_local 与 call_once
---
# thread_local 与 call_once

上一篇我们用 RAII 解决了线程所有权和生命周期管理的问题。这一篇我们要看另一个维度的问题：当多个线程需要访问某些"全局状态"时，怎么既保证线程安全又不牺牲性能？

答案分两个方向。第一个方向是**完全避免共享**——给每个线程一份独立的副本，各用各的，自然就不存在竞争。这就是 `thread_local` 存储持续时间要做的事情。第二个方向是**共享但只初始化一次**——某个全局对象需要在第一次使用时被初始化，而且无论多少个线程同时触发初始化，它都只被执行一次。这就是 `std::call_once` 的职责。两个工具解决了两个不同的问题，但它们有一个共同的主题：让并发代码在"初始化"这个环节上是安全的。

## thread_local 存储持续时间

C++ 有几种存储持续时间（storage duration）：自动存储（栈上的局部变量）、静态存储（全局变量和 `static` 局部变量）、动态存储（`new`/`malloc` 分配的），以及线程存储。`thread_local` 就是线程存储持续时间的说明符——被它修饰的变量在每个线程中都有自己独立的实例，线程从创建到退出期间一直存在。

这意味着什么？假设你声明了一个 `thread_local int counter = 0;`，那么你的程序中有多少个线程，就有多少个独立的 `counter` 副本。线程 A 对自己副本的修改，线程 B 完全看不到——它们在内存中是不同的对象，地址也不一样。从线程的视角来看，`thread_local` 变量就像是一个"线程专属的全局变量"——生命周期跟线程一样长，但每个线程各有一份。

我们来看一个最直接的例子——线程安全的计数器，不需要任何锁：

```cpp
#include <thread>
#include <iostream>

thread_local int thread_counter = 0;

void increment_and_print(const char* name)
{
    for (int i = 0; i < 5; ++i) {
        ++thread_counter;
        std::cout << name << ": counter = " << thread_counter << "\n";
    }
}

int main()
{
    std::thread t1(increment_and_print, "Thread-A");
    std::thread t2(increment_and_print, "Thread-B");

    t1.join();
    t2.join();

    // 主线程也有自己的 thread_counter 副本
    std::cout << "Main: counter = " << thread_counter << "\n";
    return 0;
}
```

输出大概是这样的：

```text
Thread-A: counter = 1
Thread-A: counter = 2
Thread-B: counter = 1
Thread-A: counter = 3
Thread-B: counter = 2
...
Main: counter = 0
```

你会注意到，`Thread-A` 和 `Thread-B` 各自计数到 5，互不干扰。而主线程的 `thread_counter` 仍然是 0——它从来没被任何线程碰过。三个线程，三个独立的 `thread_counter` 实例。

### thread_local 的初始化时机

`thread_local` 变量的初始化发生在**每个线程首次使用（ODR-use）时**，而不是程序启动时。这个"首次使用时初始化"的行为非常重要——它保证了以下几点：第一，如果一个 `thread_local` 变量从来没有被某个线程访问过，那个线程就不会为它分配内存或执行初始化，所以不会有浪费。第二，初始化是线程安全的——标准保证即使多个线程同时首次访问同一个 `thread_local` 变量，每个线程的初始化也只会执行一次，而且不会互相干扰。第三，`thread_local` 变量的初始化顺序跟它的声明位置有关——同一编译单元内的 `thread_local` 变量按声明顺序初始化，不同编译单元之间的顺序是未指定的（跟静态变量的初始化顺序问题类似）。

这个"延迟初始化"的特性让 `thread_local` 非常适合实现一些"按需分配"的资源——比如每个线程的随机数生成器、内存池、日志缓冲区等。这些资源如果全局共享就需要加锁，而用 `thread_local` 之后就完全无锁了。

### thread_local 与全局/静态变量：各自的生命周期

为了更清楚地理解 `thread_local` 的位置，我们可以把它跟其他存储持续时间做一个对比。全局变量和 `static` 成员变量具有静态存储持续时间——程序启动时（或首次使用时，对于函数内的 `static` 局部变量）初始化，程序退出时销毁。所有线程共享同一个实例。`thread_local` 变量也具有跟线程一样长的生命周期，但每个线程有独立的副本——线程启动时（首次使用时）初始化，线程退出时销毁。普通的栈变量（自动存储持续时间）在函数调用时创建，函数返回时销毁，线程之间当然也是隔离的，但生命周期太短——函数返回就没了。

一个容易忽略的点是 `thread_local` 变量的销毁时机。当一个线程退出时，该线程的所有 `thread_local` 变量会按照与初始化相反的顺序被销毁。这意味着 `thread_local` 变量的析构函数是在线程的上下文中执行的——如果你在析构函数中访问了其他线程的状态，就要小心同步问题了。更棘手的是，如果 `thread_local` 变量的析构函数中又触发了另一个 `thread_local` 变量的访问（这个变量已经被销毁了），行为是未定义的。这种"析构中的交叉引用"问题是 `thread_local` 最隐蔽的陷阱之一。

## 用 thread_local 避免线程间共享

理解了基本概念之后，我们来看几个 `thread_local` 在实战中的典型应用场景。

### 线程安全的随机数生成器

随机数生成器是 `thread_local` 最经典的用例之一。`std::rand()` 的线程安全性是实现定义的（implementation-defined），并非所有平台都保证——而且即使某个实现碰巧是线程安全的，它的内部状态仍然被所有线程共享，多次调用的结果在多线程环境下可能缺乏你期望的随机性分布。而 `<random>` 中的随机数引擎（如 `std::mt19937`）不是线程安全的——你不能在多个线程中同时调用同一个引擎对象。解决方案就是给每个线程一个独立的引擎：

```cpp
#include <random>
#include <thread>
#include <iostream>
#include <vector>

int random_int(int min_val, int max_val)
{
    // 每个线程第一次调用时初始化，后续复用
    thread_local std::mt19937 generator{std::random_device{}()};
    std::uniform_int_distribution<int> dist(min_val, max_val);
    return dist(generator);
}

void generate_numbers(const char* name, int count)
{
    std::cout << name << ": ";
    for (int i = 0; i < count; ++i) {
        std::cout << random_int(1, 100) << " ";
    }
    std::cout << "\n";
}

int main()
{
    std::thread t1(generate_numbers, "Thread-A", 10);
    std::thread t2(generate_numbers, "Thread-B", 10);
    t1.join();
    t2.join();
    return 0;
}
```

`generator` 被声明为 `thread_local`，所以每个线程有自己的 `std::mt19937` 实例，各自维护自己的随机状态。`std::random_device{}()` 用于为每个线程的生成器提供不同的种子——注意这个种子是在线程首次调用 `random_int` 时获取的，不是程序启动时。所以即使两个线程几乎同时启动，它们也会拿到不同的种子（只要 `std::random_device` 本身的实现是非确定性的，这在大多数平台上都是成立的）。

### 线程本地内存池

在高性能场景中，频繁地调用 `new` 和 `delete` 会产生严重的锁竞争——因为标准库的内存分配器（通常是 `ptmalloc2` 或 `tcmalloc`）在内部需要加锁来保护空闲链表。一个常见的优化是给每个线程一个小的内存池，小对象的分配直接从线程本地池中取，不用跟其他线程竞争：

```cpp
#include <vector>
#include <cstddef>

class ThreadLocalPool {
public:
    static ThreadLocalPool& instance()
    {
        thread_local ThreadLocalPool pool;
        return pool;
    }

    void* allocate(std::size_t size)
    {
        if (size <= kBlockSize) {
            if (!free_list_.empty()) {
                void* ptr = free_list_.back();
                free_list_.pop_back();
                return ptr;
            }
            // 从大块中切出一块
            if (current_offset_ + size > kChunkSize) {
                chunks_.emplace_back(new char[kChunkSize]);
                current_offset_ = 0;
            }
            void* ptr = chunks_.back().get() + current_offset_;
            current_offset_ += size;
            return ptr;
        }
        // 超过块大小的分配，回退到全局分配器
        return ::operator new(size);
    }

    void deallocate(void* ptr, std::size_t size)
    {
        if (size <= kBlockSize) {
            free_list_.push_back(ptr);
        }
        else {
            ::operator delete(ptr);
        }
    }

private:
    ThreadLocalPool() = default;

    static constexpr std::size_t kBlockSize = 256;
    static constexpr std::size_t kChunkSize = 4096;

    std::vector<std::unique_ptr<char[]>> chunks_;
    std::vector<void*> free_list_;
    std::size_t current_offset_{kChunkSize};  // 初始值触发首次分配
};
```

这个简化版的内存池展示了 `thread_local` 在性能优化中的典型用法：`thread_local ThreadLocalPool pool` 确保每个线程有自己独立的内存池，小对象的分配和释放完全在本地完成，不需要任何同步操作。当然，这只是一个教学示例——生产环境中你应该使用成熟的内存分配器（如 `jemalloc`、`tcmalloc`），它们内部已经用类似的思路实现了线程本地缓存。但理解 `thread_local` 在这里扮演的角色，对于写出高性能的并发代码是很有帮助的。

## std::call_once 与 std::once_flag

说完了"每个线程一份"的场景，我们再来看"所有线程共享一份但只初始化一次"的场景。

`std::call_once` 是 C++11 提供的一次性初始化机制。你给它一个 `std::once_flag` 和一个可调用对象，它保证无论有多少个线程同时调用 `call_once`，可调用对象只被执行一次——第一个到达的线程执行初始化，其余线程等待它完成。这个机制在实现单例模式、全局配置初始化、延迟加载等场景中非常有用。

### 基本用法

```cpp
#include <mutex>
#include <iostream>
#include <thread>

std::once_flag init_flag;
int* shared_resource = nullptr;

void ensure_initialized()
{
    std::call_once(init_flag, []() {
        std::cout << "Initializing shared resource...\n";
        shared_resource = new int(42);
    });
}

void use_resource(const char* thread_name)
{
    ensure_initialized();
    std::cout << thread_name << ": resource = " << *shared_resource << "\n";
}

int main()
{
    std::thread t1(use_resource, "Thread-A");
    std::thread t2(use_resource, "Thread-B");
    std::thread t3(use_resource, "Thread-C");

    t1.join();
    t2.join();
    t3.join();

    delete shared_resource;
    return 0;
}
```

输出中你会发现 "Initializing shared resource..." 只出现了一次——无论三个线程的调度顺序如何，初始化代码只执行了一次。`std::once_flag` 记录了初始化是否已经完成，`call_once` 在每次调用时检查这个标志位。如果初始化还没开始，第一个线程执行初始化；如果正在进行中，其他线程阻塞等待；如果已经完成，所有线程直接跳过。

### call_once 与异常重试

`std::call_once` 有一个很关键的行为：如果初始化函数（可调用对象）抛出了异常，`call_once` 不会把 `once_flag` 标记为"已完成"。这意味着下一次有线程调用 `call_once` 时，初始化会再次尝试。这个设计非常合理——如果初始化失败了（比如打开文件失败、网络连接超时），你不希望后续所有线程都认为"已经初始化过了"然后使用一个无效的状态。

```cpp
#include <mutex>
#include <iostream>
#include <stdexcept>

std::once_flag config_flag;
bool config_loaded = false;
int attempt_count = 0;

void load_config()
{
    ++attempt_count;
    std::cout << "Attempt " << attempt_count << ": loading config...\n";

    if (attempt_count < 3) {
        // 模拟前两次失败
        throw std::runtime_error("Config file not ready");
    }

    config_loaded = true;
    std::cout << "Config loaded successfully\n";
}

void worker(const char* name)
{
    try {
        std::call_once(config_flag, load_config);
        std::cout << name << ": using config\n";
    }
    catch (const std::exception& e) {
        std::cout << name << ": init failed - " << e.what() << "\n";
    }
}
```

在这个例子中，前两次调用 `call_once` 时 `load_config` 会抛异常，`once_flag` 不会被标记为已完成，所以下一次调用时初始化会重新尝试。直到第三次成功之后，后续所有调用都会直接跳过初始化。这个"异常后重试"的行为是 `call_once` 相比 Meyers singleton 的一个重要优势——后面我们会详细对比。

## Meyers singleton：函数作用域中的 static 局部

从 C++11 开始，函数作用域中的 `static` 局部变量有一个非常重要的保证：**它的初始化是线程安全的**。如果有多个线程同时首次执行到 `static` 变量的声明处，只有一个线程会执行初始化，其他线程会等待。这就是所谓的"Meyers singleton"（以 Scott Meyers 命名，他在《Effective C++》中推广了这种写法）：

```cpp
#include <iostream>
#include <thread>

class Singleton {
public:
    static Singleton& instance()
    {
        static Singleton inst;  // 线程安全的初始化
        return inst;
    }

    void do_work()
    {
        std::cout << "Singleton working\n";
    }

private:
    Singleton()
    {
        std::cout << "Singleton constructed\n";
    }

    // 禁止复制和移动
    Singleton(const Singleton&) = delete;
    Singleton& operator=(const Singleton&) = delete;
};

void use_singleton(const char* name)
{
    std::cout << name << ": accessing singleton\n";
    Singleton::instance().do_work();
}

int main()
{
    std::thread t1(use_singleton, "Thread-A");
    std::thread t2(use_singleton, "Thread-B");
    t1.join();
    t2.join();
    return 0;
}
```

"Singleton constructed" 只会输出一次，无论多少个线程同时调用 `instance()`。C++11 标准（[stmt.dcl] 第 4 段）明确规定：如果控制流进入 `static` 局部变量的声明同时有多个线程，其中一个会执行初始化，其他线程会阻塞等待。这个保证是由编译器和运行时库共同实现的——在 GCC 和 Clang 上，它通常是通过 `__cxa_guard_acquire` / `__cxa_guard_release` 这两个 ABI 函数来实现的，底层使用了类似于 `call_once` 的机制。

Meyers singleton 是实现单例模式最简洁、最安全的方式。不需要手动加锁，不需要 `std::call_once`，不需要 `std::atomic`——编译器帮你搞定了一切。如果你的单例初始化不会失败（不会抛异常），用 Meyers singleton 是最好的选择。

## call_once 何时优于 Meyers singleton

既然 Meyers singleton 这么好用，为什么还需要 `std::call_once`？关键区别在于**控制粒度**和**异常处理**。

Meyers singleton 的初始化是跟变量的声明绑定在一起的——你没法在初始化之前做一些准备工作，也没法在初始化失败后选择不同的策略。而 `call_once` 给了你完全的控制权：初始化函数可以是一个普通的函数或 lambda，你可以自由决定它的内容；初始化可以访问外部状态（比如读取配置文件路径、连接数据库）；如果初始化失败（抛异常），后续调用可以重试。

一个更微妙的区别是初始化的"位置"。Meyers singleton 的初始化发生在第一次调用 `instance()` 函数时——这个时机可能不是你想要的。也许你希望在程序启动后显式地初始化所有全局资源，而不是在某个请求处理的中间突然触发一次耗时的初始化。`call_once` 让你可以把这个初始化逻辑放在任何地方——可以在 `main()` 开头主动调用，也可以在真正需要时懒加载，完全由你控制。

还有一个实际场景：如果你的"单例"不是一个对象，而是一组初始化步骤（比如初始化日志系统、配置管理器、数据库连接池等），`call_once` 可以把所有这些步骤打包在一个函数里。而 Meyers singleton 只能初始化一个对象——要初始化多个东西就得为每个东西写一个 `static` 局部变量，不够灵活。

总结一下选择策略：如果你的初始化逻辑很简单、不会失败、只需要初始化一个对象，Meyers singleton 是最好的选择——简洁、安全、零开销。如果你需要更灵活的控制——初始化可能失败、需要重试、需要访问外部状态、或者需要初始化一组资源而不是单个对象——`call_once` 是更合适的工具。

## thread_local 与动态加载库

`thread_local` 在正常使用中是很可靠的，但在涉及动态链接库（shared library / DLL）的场景中会有一些需要注意的问题。

问题的根源在于 `thread_local` 变量的生命周期管理。每个线程的 `thread_local` 变量需要在线程退出时被销毁，这就需要注册一个析构回调。在主程序中，这个注册是由 C++ 运行时在 `thread_local` 变量首次被访问时完成的。但在动态加载的库中，情况变得更复杂——库可能在任何时候被加载或卸载，而 `thread_local` 变量的析构回调需要在库卸载之前被清理掉。

在 Linux 上（glibc + GCC/Clang），`thread_local` 变量在动态库中的支持通常工作正常——`__cxa_thread_atexit` 函数负责注册线程退出时的析构回调，它会正确处理库卸载的情况。但在 Windows 的 DLL 模型中，`thread_local` 在 DLL 中的行为长期以来都存在问题——DLL 卸载时，已经退出的线程的 `thread_local` 变量析构回调会指向已经无效的代码段，导致崩溃。直到较新的 MSVC 版本（VS 2017 及之后）才较为完善地支持了 `thread_local` 在 DLL 中的使用。

如果你需要编写跨平台的、可能被动态加载的库代码，使用 `thread_local` 时要注意以下几点。首先，确保你的目标平台上编译器对 `thread_local` 的动态库支持是完善的。其次，如果 `thread_local` 变量的析构函数有副作用（比如释放锁、写入文件、通知其他线程），要特别小心——库卸载时这些析构可能不会按你期望的顺序执行。最后，在一些嵌入式或特殊环境中（比如 WebAssembly、某些 RTOS），`thread_local` 的支持可能是不完整的或者完全没有——如果你的代码需要在这些平台上运行，最好用其他方式实现线程本地存储。

## 小结

这一篇我们讨论了两种处理并发环境中"初始化"问题的机制。`thread_local` 给每个线程提供独立的变量副本，从根本上消除了数据共享——适合随机数生成器、内存池、日志缓冲区等"每个线程各有一份"的场景。它的初始化是延迟的（首次使用时），线程安全的，销毁发生在对应线程退出时。

`std::call_once` 配合 `std::once_flag` 提供了"所有线程共享一份，但只初始化一次"的保证。它比 Meyers singleton 更灵活——支持异常重试、可以初始化非对象资源（比如一组函数调用）、可以在任意位置触发初始化。如果你的初始化逻辑简单且不会失败，Meyers singleton 仍然是首选——它更简洁，不需要额外的 `once_flag` 变量。两者不是替代关系，而是互补的工具，选择哪个取决于你的具体需求。

到这里，ch01 的四篇文章就全部结束了。我们从 `std::thread` 的基本用法出发，经历了参数传递、生命周期管理、RAII 包装、线程所有权、以及线程本地存储和一次性初始化。这些都是后续内容的基础——后面讨论 mutex、原子操作、无锁编程时，我们会频繁用到这一章建立的概念和工具。

> 💡 完整示例代码在 [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP)，访问 `code/volumn_codes/vol5/ch01-thread-lifecycle-raii/`。

## 练习

### 练习 1：线程安全的配置初始化器

实现一个 `ConfigManager` 类，它从文件中读取配置（可以用 `std::getline` 模拟），使用 `std::call_once` 保证只初始化一次。要求：(1) 如果文件读取失败，应该抛出异常并允许重试；(2) 提供一个 `get(key)` 方法返回配置值；(3) 多个线程可以同时调用 `get()`，但只有第一次会触发文件读取。

```cpp
// 骨架代码
#include <mutex>
#include <string>
#include <unordered_map>

class ConfigManager {
public:
    static ConfigManager& instance();

    std::string get(const std::string& key) const;

private:
    ConfigManager() = default;
    void load_from_file();

    std::once_flag init_flag_;
    std::unordered_map<std::string, std::string> config_;
};
```

### 练习 2：thread_local 日志器

实现一个简单的线程本地日志器，每个线程有自己的日志缓冲区（`std::stringstream`），日志写入时不加锁。提供两个方法：`log(message)` 写入日志，`flush()` 将缓冲区内容输出到 `std::cout` 并清空。在 `main()` 中启动 4 个线程，每个线程写入 10 条日志后 flush，观察输出是否线程安全。

### 练习 3：对比 call_once 和 Meyers singleton

用两种方式实现同一个单例——一种用 `std::call_once`，一种用 Meyers singleton。然后在单例的构造函数中模拟一个耗时的初始化（`std::this_thread::sleep_for(std::chrono::milliseconds(100))`），用 8 个线程同时访问单例，测量两种实现的性能差异。思考：为什么两者性能可能不同？提示：Meyers singleton 的初始化锁是在 `static` 变量上的，而 `call_once` 的锁是在 `once_flag` 上的——如果多个线程同时访问，等待的机制是一样的，但实现细节可能有差异。

## 参考资源

- [thread_local storage — cppreference](https://en.cppreference.com/w/cpp/language/storage_duration#thread_local_storage)
- [std::call_once — cppreference](https://en.cppreference.com/w/cpp/thread/call_once)
- [Magic Statics (C++11 thread-safe statics) — cppreference](https://en.cppreference.com/w/cpp/language/static#Static_local_variables)
- [Effective C++, Item 4: Make sure that objects are initialized before they're used — Scott Meyers](https://www.oreilly.com/library/view/effective-c/0321334876/)
- [Thread-local storage — Wikipedia](https://en.wikipedia.org/wiki/Thread-local_storage)
- [Dynamic Initialization and Destruction in C++ (Itanium C++ ABI)](https://itanium-cxx-abi.github.io/cxx-abi/abi.html#once-ctor)
