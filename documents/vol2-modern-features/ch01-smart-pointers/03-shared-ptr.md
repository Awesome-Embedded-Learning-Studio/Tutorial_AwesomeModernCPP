---
title: "shared_ptr 详解：共享所有权与引用计数"
description: "理解 shared_ptr 的控制块机制、线程安全与性能特征"
chapter: 1
order: 3
tags:
  - host
  - cpp-modern
  - intermediate
  - shared_ptr
  - 智能指针
  - 引用计数
difficulty: intermediate
platform: host
cpp_standard: [11, 14, 17]
reading_time_minutes: 20
prerequisites:
  - "Chapter 1: RAII 深入理解"
  - "Chapter 1: unique_ptr 详解"
related:
  - "weak_ptr 与循环引用"
  - "自定义删除器"
---

# shared_ptr 详解：共享所有权与引用计数

上一篇我们聊了 `unique_ptr`——独占所有权的零开销智能指针。但现实世界中的资源并不总是"一主独占"的。有时候，一个对象确实需要被多个模块共同持有、共同管理——比如一个配置对象被多个子系统读取，一个网络连接被多个任务共享，一个缓存条目被多个消费者访问。这时候，`unique_ptr` 的"独占"语义就显得不够用了。

`std::shared_ptr` 就是为这种场景设计的。它的核心思想是**引用计数**：每多一个 `shared_ptr` 指向对象，计数就加一；每少一个，计数就减一；当计数归零时，对象被自动销毁。听起来简单优雅，但背后的实现细节——控制块、原子操作、内存分配策略——远比想象中复杂。今天我们就把这些全部拆开来看。

## 共享所有权：语义与代价

`shared_ptr` 表达的是"共享所有权"语义：多个 `shared_ptr` 可以指向同一个对象，它们共同决定对象的生命周期。只有当最后一个 `shared_ptr` 被销毁时，对象才会被 delete。

```cpp
#include <memory>
#include <iostream>

struct Connection {
    explicit Connection(const std::string& addr) : addr_(addr) {
        std::cout << "Connected to " << addr_ << "\n";
    }
    ~Connection() {
        std::cout << "Disconnected from " << addr_ << "\n";
    }
    void send(const std::string& msg) {
        std::cout << "Send to " << addr_ << ": " << msg << "\n";
    }
private:
    std::string addr_;
};

void demo_shared() {
    auto conn = std::make_shared<Connection>("192.168.1.1:8080");
    {
        auto conn2 = conn;  // 引用计数: 1 → 2
        conn2->send("hello from conn2");
        std::cout << "use_count: " << conn.use_count() << "\n";  // 2
    }   // conn2 离开作用域，引用计数: 2 → 1

    conn->send("hello from conn");
    std::cout << "use_count: " << conn.use_count() << "\n";  // 1
}   // conn 离开作用域，引用计数: 1 → 0，Connection 被销毁
```

运行结果：

```text
Connected to 192.168.1.1:8080
Send to 192.168.1.1:8080: hello from conn2
use_count: 2
Send to 192.168.1.1:8080: hello from conn
use_count: 1
Disconnected from 192.168.1.1:8080
```

看起来很美好。但共享所有权不是免费的——每一个 `shared_ptr` 的拷贝和析构都需要更新引用计数，而引用计数必须是线程安全的（原子操作）。此外，`shared_ptr` 内部还需要维护一个控制块（control block）来存储引用计数和其他元信息。这些开销在频繁创建和销毁 `shared_ptr` 的场景下会变得非常明显。

笔者的建议是：能用 `unique_ptr` 就用 `unique_ptr`，只在真正需要共享所有权的场景下才使用 `shared_ptr`。`shared_ptr` 不应该成为"懒得思考所有权"的借口。

## 控制块：shared_ptr 的内部结构

理解 `shared_ptr` 的性能特征，必须先理解它的内部结构。一个 `shared_ptr` 实际上包含两个指针：一个指向被管理的对象，另一个指向控制块（control block）。

控制块是一个在堆上分配的数据结构，通常包含以下内容：强引用计数（`shared_ptr` 的数量）、弱引用计数（`weak_ptr` 的数量）、自定义删除器（如果有的话）、自定义分配器（如果有的话）。当你用 `std::make_shared` 创建 `shared_ptr` 时，对象和控制块会被放在同一个内存块中（一次分配）；而用 `std::shared_ptr<T>(new T)` 创建时，对象和控制块是两次独立的分配。

我们用一个简化的示意图来理解：

```text
shared_ptr 对象 (栈上)
┌─────────────────────┐
│ T* ptr ─────────────┼──→ T 对象
│ ControlBlock* cb ───┼──→ ControlBlock (堆上)
└─────────────────────┘     ┌──────────────────────┐
                            │ strong_count: 2      │
                            │ weak_count: 0        │
                            │ deleter (可选)       │
                            │ allocator (可选)     │
                            └──────────────────────┘
```

所以一个 `shared_ptr` 对象本身的大小通常是 `2 * sizeof(void*)`——两个指针。在 64 位系统上是 16 字节，比 `unique_ptr`（8 字节）大一倍。控制块本身的大小取决于实现，通常至少 24-32 字节（两个原子计数器 + 虚表指针 + 对齐填充）。

## make_shared 的优势：单次分配

前面提到，`make_shared` 把对象和控制块放在一个连续的内存块里。这带来了三个显著的好处。

首先是**更少的堆分配次数**——从两次减为一次。在性能敏感的代码中，堆分配是昂贵操作（通常涉及锁、遍历空闲链表等），减少分配次数总是好的。

其次是**更好的缓存局部性**。对象和控制块在同一个内存块里，CPU 缓存行可能同时命中两者。而两次独立分配的内存块可能在物理上相距很远，导致更多的缓存未命中。

第三是**更少的内存碎片**。一次分配意味着一次释放，而不是在两个不同的位置各释放一次。

```cpp
// 推荐：单次分配
auto p1 = std::make_shared<Connection>("10.0.0.1:9090");

// 不推荐：两次分配，且不如 make_shared 异常安全
auto p2 = std::shared_ptr<Connection>(new Connection("10.0.0.1:9090"));

// 大小对比
std::cout << "sizeof(shared_ptr): " << sizeof(p1) << "\n";  // 16 (64-bit)
std::cout << "sizeof(unique_ptr): " << sizeof(std::unique_ptr<Connection>) << "\n";  // 8
```

⚠️ `make_shared` 也有一个不太为人知的缺点：由于对象和控制块共享同一个内存块，当所有 `shared_ptr` 都被销毁时（强引用归零），对象会被析构，但控制块的内存不会立即释放——必须等到所有 `weak_ptr` 也都销毁（弱引用归零）后，整个内存块才会被回收。如果对象很大且有 `weak_ptr` 仍在使用，可能会造成内存占用比预期更高的现象。如果你预期会有 `weak_ptr` 长期存在，可以考虑使用 `std::shared_ptr<T>(new T)` 来让对象的内存独立于控制块，这样强引用归零时对象内存就能立即释放。

## 引用计数的原子操作与线程安全

`shared_ptr` 的引用计数使用原子操作来保证线程安全。这意味着在多线程环境下，你可以安全地拷贝和销毁 `shared_ptr` 本身（引用计数的增减是原子的），但**被管理对象的访问并不受保护**——如果你有多个线程同时读写对象本身，仍然需要自行加锁。

这是一个常见的误解：很多人以为 `shared_ptr` 提供了"对象的线程安全"，但实际上它只保证了"引用计数的线程安全"。我们可以用 cppreference 的描述来精确理解：`shared_ptr` 的控制块是线程安全的——多个线程可以同时操作不同的 `shared_ptr` 实例（即使它们指向同一个对象），不需要外部同步。但同一个 `shared_ptr` 实例不能被多个线程同时读写（需要加锁）。被管理对象的并发访问需要自行保证安全。

```cpp
#include <memory>
#include <thread>
#include <vector>
#include <iostream>

void demo_thread_safety() {
    auto data = std::make_shared<int>(0);

    // 多个线程各自持有 shared_ptr 的拷贝——安全
    std::vector<std::thread> threads;
    for (int i = 0; i < 8; ++i) {
        threads.emplace_back([data]() {  // 拷贝 shared_ptr，引用计数原子递增
            // 读取 *data 是安全的（只读）
            std::cout << "value: " << *data << "\n";

            // 但如果多个线程同时写 *data，就是数据竞争——需要加锁！
        });
    }

    for (auto& t : threads) t.join();
    std::cout << "final use_count: " << data.use_count() << "\n";  // 应该是 1
}
```

从性能角度看，每次拷贝或析构 `shared_ptr` 都会产生一次原子操作（通常是 `fetch_add` 或 `fetch_sub`）。原子操作在单核系统上开销很小（可能只是一条特殊的 CPU 指令），但在多核系统上会引发缓存一致性协议的开销（cache line bouncing）。如果你的代码频繁创建和销毁 `shared_ptr`（比如在热循环中），这个开销可能会变得非常显著。

引用计数递减时的逻辑尤其值得关注。当 `fetch_sub` 返回 1（意味着这是最后一个 `shared_ptr`）时，需要销毁对象。标准的实现通常使用 `memory_order_acq_rel` 来保证所有之前的写操作对销毁代码可见，并在销毁前插入一个 `acquire` fence。这些内存屏障在 x86 上开销不大（x86 本身就有强内存序），但在 ARM 等弱序架构上可能会导致流水线刷新。

## shared_ptr 的性能开销分析

我们来做一个直观的对比，把 `shared_ptr`、`unique_ptr` 和裸指针的开销放在一张表里：

| 维度 | 裸指针 | unique_ptr | shared_ptr |
|------|--------|------------|------------|
| 对象大小 | 8B (64-bit) | 8B | 16B |
| 额外堆分配 | 无 | 无 | 控制块 (24-32B+) |
| 拷贝开销 | 8B 复制 | 不可拷贝 | 原子 fetch_add |
| 析构开销 | 无 | delete | 原子 fetch_sub + 可能 delete |
| 线程安全 | 无 | 无 | 引用计数安全，对象不安全 |

从这张表可以清楚地看到，`shared_ptr` 在每一个维度上都比 `unique_ptr` 更重。这不是说 `shared_ptr` 不好——它在共享所有权的场景下是正确的设计选择——但你应该在确实需要共享所有权时才使用它，而不是"为了方便到处用 `shared_ptr`"。

在实际项目中，笔者见过不少代码库把几乎所有对象都用 `shared_ptr` 管理，结果就是引用计数到处飞、性能无法优化、循环引用问题频出。更好的做法是在设计阶段就明确所有权关系：大多数资源用 `unique_ptr` 管理，只在确实需要共享的少数地方使用 `shared_ptr`，并通过引用（`T&`）或裸指针（`T*`，不持有所有权）来传递非拥有访问。

## Aliasing Constructor：不为人知的强大特性

`shared_ptr` 有一个非常强大但不太为人知的构造函数，叫做 **aliasing constructor**。它的签名是：

```cpp
template <typename U>
shared_ptr(const shared_ptr<U>& r, T* ptr) noexcept;
```

这个构造函数创建一个新的 `shared_ptr`，它共享 `r` 的所有权（即引用计数与 `r` 共享），但 `get()` 返回的是 `ptr` 而不是 `r.get()`。简单说就是：**它让你持有同一个对象的"一部分"，而不需要单独管理那部分的生命周期**。

最常见的用途是访问对象的成员：

```cpp
struct Config {
    std::string host;
    int port;
    std::string db_name;
};

auto config = std::make_shared<Config>();

// 获取一个指向 config->host 的 shared_ptr
// 它共享 config 的引用计数——只要有人持有 host_ptr，config 就不会被销毁
std::shared_ptr<std::string> host_ptr(config, &config->host);

// 在另一个组件中使用 host_ptr，不需要知道 Config 的存在
void connect(const std::shared_ptr<std::string>& host) {
    std::cout << "Connecting to " << *host << "\n";
}
```

这个特性在实现"指向容器元素的智能指针"时特别有用——比如你想返回一个指向 `vector` 中某个元素的 `shared_ptr`，但又不想让调用者持有整个 `vector` 的 `shared_ptr`。通过 aliasing constructor，你可以返回一个只暴露元素类型的 `shared_ptr`，而底层仍然由容器的 `shared_ptr` 管理生命周期。

## enable_shared_from_this：在成员函数中获取 shared_ptr

有时候，对象的成员函数需要返回一个指向自身的 `shared_ptr`。最直觉的写法 `shared_ptr(this)` 是致命错误——它会创建一个新的控制块，导致对象被 delete 两次。正确的做法是继承 `std::enable_shared_from_this` 并调用 `shared_from_this()`：

```cpp
#include <memory>
#include <iostream>
#include <functional>

class TcpSession : public std::enable_shared_from_this<TcpSession> {
public:
    explicit TcpSession(int fd) : fd_(fd) {
        std::cout << "Session created (fd=" << fd_ << ")\n";
    }
    ~TcpSession() {
        std::cout << "Session destroyed (fd=" << fd_ << ")\n";
    }

    void start_read() {
        // 异步读取通常需要持有自身的 shared_ptr，防止在读完成前被销毁
        auto self = shared_from_this();
        // async_read(socket_, buffer_, [self](error_code ec, size_t n) {
        //     self->on_read_complete(ec, n);
        // });
        std::cout << "Start reading (use_count="
                  << self.use_count() << ")\n";
    }

private:
    int fd_;
};

// 正确用法：必须通过 shared_ptr 持有
void session_demo() {
    auto session = std::make_shared<TcpSession>(3);
    session->start_read();
}
```

⚠️ 使用 `enable_shared_from_this` 有一个前提条件：对象必须已经被一个 `shared_ptr` 管理。如果你在栈上创建对象或用裸指针管理，调用 `shared_from_this()` 会导致未定义行为。此外，构造函数中不能调用 `shared_from_this()`——因为此时 `shared_ptr` 还没有完成构造。

## 常见误用与踩坑

在深入嵌入式权衡之前，我们先盘点几个 `shared_ptr` 的常见误用模式。这些"坑"笔者自己踩过不止一次，也希望读者能提前绕开。

**误用一：用 `shared_ptr(this)` 创建第二个控制块**。这是最致命的错误。如果你在一个已经被 `shared_ptr` 管理的对象的成员函数中写 `return std::shared_ptr<Widget>(this)`，编译器会创建一个全新的控制块，引用计数从 1 开始。结果就是两个独立的控制块管理同一个对象——当两个 `shared_ptr` 都被销毁时，对象会被 delete 两次。正确做法是继承 `enable_shared_from_this` 并调用 `shared_from_this()`。

**误用二：在接口中暴露 `shared_ptr` 的所有权意图**。如果你写一个函数 `void process(std::shared_ptr<Widget> w)`，签名本身就暗示了"我要和你共享所有权"。但很多时候函数只是想使用对象，并不需要持有它。这种场景下传 `const Widget&` 或 `Widget*` 更合适——不暗示所有权，也没有引用计数的开销。

**误用三：用 `shared_ptr` 管理"不需要共享"的对象**。有些团队为了图省事，把所有堆对象都用 `shared_ptr` 管理——"反正 shared_ptr 什么都能管"。这会导致所有权语义模糊（谁都持有等于谁都不负责）、性能下降（到处是原子操作）、循环引用风险增加。笔者的经验是：**90% 的对象应该用 `unique_ptr` 管理，只有 10% 真正需要共享的用 `shared_ptr`**。

**误用四：忽视 `make_shared` 与 `new` 的区别**。`make_shared` 把对象和控制块合并在一次分配中，但这也意味着对象的析构和控制块的释放不在同一时刻——当所有 `shared_ptr` 被销毁时，对象析构，但如果还有 `weak_ptr` 存活，整个内存块（包括对象占用的空间）不会释放直到所有 `weak_ptr` 也被销毁。对于大型对象，这可能导致"明明没人用了但内存还不还回来"的现象。如果你预期会有长期存活的 `weak_ptr`，用 `shared_ptr<T>(new T)` 把对象和控制块分开分配可能更合适。

## 实战选择指南：什么时候该用 shared_ptr

在讲嵌入式权衡之前，我们先来做一个实战导向的选择分析。很多人在 `unique_ptr` 和 `shared_ptr` 之间犹豫不决，其实判断标准很简单——问自己一个问题：**这个对象是否需要被多个独立的模块共同拥有？**

如果答案是"不"——对象的生命周期由一个明确的"主人"决定，其他模块只是临时借用——那就用 `unique_ptr` + 裸指针/引用传递。这是绝大多数场景。

如果答案是"是"——多个模块确实需要独立地决定"我还在用这个对象"，而且没有一个模块能声称"我是唯一的主人"——那就用 `shared_ptr`。

典型的 `shared_ptr` 适用场景包括：插件系统中的共享模块（多个组件可能同时依赖同一个插件实例，谁也不能提前卸载它）、异步回调链中的共享状态（多个 future/callback 需要保持状态活着直到自己完成）、树或图中的共享节点（多个父节点引用同一个子节点）。

典型的不应该用 `shared_ptr` 的场景包括：函数参数传递（传引用就够了）、对象的唯一所有者（用 `unique_ptr`）、简单的缓存（用 `weak_ptr` 观察，`shared_ptr` 持有）。

我们来看一个具体的设计决策示例——实现一个简单的任务调度器：

```cpp
#include <memory>
#include <vector>
#include <functional>
#include <iostream>

class Task {
public:
    virtual ~Task() = default;
    virtual void execute() = 0;
    virtual std::string name() const = 0;
};

class PrintTask : public Task {
public:
    explicit PrintTask(std::string msg) : msg_(std::move(msg)) {}
    void execute() override { std::cout << msg_ << "\n"; }
    std::string name() const override { return "PrintTask"; }
private:
    std::string msg_;
};

class TaskScheduler {
public:
    // 调度器持有任务的所有权——用 unique_ptr 足够
    void submit(std::unique_ptr<Task> task) {
        std::cout << "提交任务: " << task->name() << "\n";
        tasks_.push_back(std::move(task));
    }

    void run_all() {
        for (auto& task : tasks_) {
            task->execute();
        }
        tasks_.clear();
    }

private:
    std::vector<std::unique_ptr<Task>> tasks_;
};

// 如果任务需要被多个调度器共享——这时才需要 shared_ptr
class SharedTaskScheduler {
public:
    void submit(std::shared_ptr<Task> task) {
        tasks_.push_back(std::move(task));
    }

    std::shared_ptr<Task> get_task(size_t index) {
        if (index < tasks_.size()) return tasks_[index];
        return nullptr;
    }

private:
    std::vector<std::shared_ptr<Task>> tasks_;
};
```

第一个版本用 `unique_ptr`——任务提交后所有权归调度器，简单明确。第二个版本用 `shared_ptr`——允许多个调度器或外部代码持有同一个任务的引用，任务在最后一个持有者离开时才被销毁。选哪个取决于你的设计需求，而不是"哪个更方便"。

## 嵌入式权衡：内存开销与 ISR 注意事项

在嵌入式场景下使用 `shared_ptr` 需要格外谨慎，原因我们逐一分析。

首先是**内存开销**。在 32 位 MCU 上，一个 `shared_ptr` 对象占 8 字节（两个指针），控制块至少 16-20 字节。如果你用 `make_shared`，对象和控制块一共可能占用 `sizeof(T) + 20+` 字节。对于只有几十 KB RAM 的 MCU 来说，这种开销在对象数量较多时会非常明显。我们来算一笔具体的账：假设你的 MCU 有 64KB RAM，你需要管理 50 个外设句柄，每个句柄对象本身 16 字节。用 `unique_ptr` 管理，总开销是 `50 * (8 + 16) = 1200` 字节；用 `shared_ptr` + `make_shared` 管理，总开销是 `50 * (16 + 16 + 24) = 2800` 字节——多出 1600 字节，占总 RAM 的 2.4%。在内存更紧张的 MCU 上（比如 STM32F103 只有 20KB RAM），这个数字会变得更加刺眼。

其次是**堆分配**。控制块需要在堆上分配，而很多嵌入式系统要么禁用了堆，要么堆空间非常有限。频繁的堆分配会导致内存碎片，最终分配失败。如果你的系统运行时间长（嵌入式设备通常常年运行），碎片化问题会越来越严重。一个可能的缓解方案是使用 `std::allocate_shared` 配合自定义分配器（比如内存池分配器），把控制块的分配从系统堆转移到预分配的内存池中。

第三是**原子操作**。引用计数的原子递增/递减在单核 MCU 上可能退化为关中断操作（取决于工具链对 `std::atomic` 的实现），这会影响中断响应时间。在 ISR 中使用 `shared_ptr` 是一个糟糕的主意——不仅因为堆操作，还因为原子操作可能关中断。如果你的系统有严格的实时性要求（比如控制环路必须在 100us 内完成），ISR 中的任何不确定延迟都是不可接受的。

笔者的建议是：在嵌入式系统中优先使用 `unique_ptr` 或者直接使用 RAII 封装类。如果确实需要共享语义，考虑侵入式引用计数（intrusive reference counting）——把引用计数放在对象内部，避免额外的堆分配。在单线程环境下，侵入式方案的引用计数可以用普通的 `uint32_t`，不需要原子操作，开销极低。这个话题我们会在"自定义删除器与侵入式引用计数"那篇中详细讨论。

## 小结

`shared_ptr` 通过引用计数实现了共享所有权语义，是 `unique_ptr` 独占语义的互补。理解它的关键在于控制块机制——每个 `shared_ptr` 实例持有两个指针（对象和控制块），控制块中的原子引用计数保证了多线程下的安全性，但也带来了不可忽视的性能开销。

`make_shared` 通过单次分配优化了性能和内存局部性，应该是创建 `shared_ptr` 的首选方式。aliasing constructor 和 `enable_shared_from_this` 是两个不太知名但非常有用的高级特性。在嵌入式场景下，`shared_ptr` 的内存开销、堆分配和原子操作成本需要仔细权衡——大多数情况下，`unique_ptr` 或侵入式方案是更好的选择。

下一篇我们将讨论 `weak_ptr`——`shared_ptr` 的搭档，专门用来解决循环引用这个棘手问题。

## 参考资源

- [cppreference: std::shared_ptr](https://en.cppreference.com/w/cpp/memory/shared_ptr)
- [cppreference: std::make_shared](https://en.cppreference.com/w/cpp/memory/shared_ptr/make_shared)
- [Inside STL: The different types of shared pointer control blocks](https://devblogs.microsoft.com/oldnewthing/20230821-00/?p=108626)
- [std::shared_ptr thread safety](https://stackoverflow.com/questions/9127816/stdshared-ptr-thread-safety)
- [C++ Core Guidelines: R.20-24](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#Rr-smart)
