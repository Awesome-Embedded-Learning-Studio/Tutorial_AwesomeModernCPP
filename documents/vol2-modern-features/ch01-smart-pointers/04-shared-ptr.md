---
chapter: 1
cpp_standard:
- 11
- 14
- 17
description: 理解 shared_ptr 的控制块机制、线程安全与性能特征
difficulty: intermediate
order: 4
platform: host
prerequisites:
- 'Chapter 1: RAII 深入理解'
- 'Chapter 1: unique_ptr 详解'
reading_time_minutes: 25
related:
- weak_ptr 与循环引用
- 自定义删除器
tags:
- host
- cpp-modern
- intermediate
- shared_ptr
- 智能指针
- 引用计数
title: shared_ptr 详解：共享所有权与引用计数
contributors:
  - github: Charliechen114514
    roles: [author, maintain]
    note:
      zh: "文章撰写与长期维护"
      en: "Article writing and ongoing maintenance"
  - github: YukunJ
    roles: [revision]
    note:
      zh: "参与文章内容与示例修订 (PR #32)"
      en: "Contributed article and example revisions (PR #32)"
---
# shared_ptr 详解：共享所有权与引用计数

上一篇咱们聊了 `unique_ptr`——独占所有权的零开销智能指针。但现实世界中的资源并不总是"一主独占"的。有时候，一个对象确实需要被多个模块共同持有、共同管理——比如一个配置对象被多个子系统读取，一个网络连接被多个任务共享，一个缓存条目被多个消费者访问。这时候，`unique_ptr` 的"独占"语义就显得不够用了。

`std::shared_ptr` 就是为这种场景设计的。它的核心思想是**引用计数**：每多一个 `shared_ptr` 指向对象，计数就加一；每少一个，计数就减一；当计数归零时，对象被自动销毁。听起来简单优雅，但背后的实现细节——控制块、原子操作、内存分配策略——远比想象中复杂。

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

这份演示程序就在下面，点“动手试一试”直接跑：

<OnlineCompilerDemo
  title="动手验证：引用计数的涨落"
  source-path="code/examples/vol2/26_shared_ptr_refcount.cpp"
  description="在线观察 use_count 的变化：内层作用域拷贝一份变 2，离开作用域回落到 1，最后一份销毁时 Connection 才断开。"
  run-options="-std=c++17"
  allow-run
/>

这场计数涨落做成了动画，您可以播放、暂停，也可以按步进键单步看，把 use_count 的每一次加减都看个清楚：

<Anim id="shared-refcount" />

看起来很美好。但共享所有权不是免费的——每一个 `shared_ptr` 的拷贝和析构都需要更新引用计数，而引用计数必须是线程安全的（原子操作）。此外，`shared_ptr` 内部还需要维护一个控制块（control block）来存储引用计数和其他元信息。这些开销在频繁创建和销毁 `shared_ptr` 的场景下会变得非常明显。

笔者的建议是：能用 `unique_ptr` 就用 `unique_ptr`，只在真正需要共享所有权的场景下才使用 `shared_ptr`<RefLink :id="1" preview="C++ Core Guidelines R.21 — Prefer unique_ptr over shared_ptr unless you need to share ownership" />。`shared_ptr` 不应该成为"懒得思考所有权"的借口。

## 控制块：shared_ptr 的内部结构

理解 `shared_ptr` 的性能特征，必须先理解它的内部结构。一个 `shared_ptr` 实际上包含两个指针：一个指向被管理的对象，另一个**指向控制块（control block）**。

一些朋友可能会感到困惑。欸？`shared_ptr`这个小指针，凭啥找我收内存的租金呢？答案是他需要控制包含强引用计数（`shared_ptr` 的数量）、弱引用计数（`weak_ptr` 的数量）、自定义删除器（如果有的话）、自定义分配器（如果有的话）。这些内容往往需要持久的存在，伴随着一个shared_ptr的生命周期。自然，也就是一个堆数据结构（所以笔者之前在一个极端高频的路径下用这个差点被骂死= =。）

所以，当您用 `std::make_shared` 创建 `shared_ptr` 时，对象和控制块会被放在同一个内存块中（一次分配）；而用 `std::shared_ptr<T>(new T)` 创建时，对象和控制块是两次独立的分配<RefLink :id="2" preview="Raymond Chen, Inside STL: separate vs combined control block, The Old New Thing, 2023" />。

咱们用一个简化的示意图来理解：

![shared_ptr 内部结构示意](./04-shared-ptr-structure.drawio)

这样看，一个 `shared_ptr` 对象本身的大小是 `2 * sizeof(void*)`——两个指针。在 64 位系统上是 16 字节，比 `unique_ptr`（8 字节）大一倍。控制块本身的大小取决于实现（GNU libstdc++ 在 x86_64 上，控制块头部是 16 字节——一个 vptr 加两个 4 字节的计数，再存一个对象指针，`shared_ptr(new T)` 最朴素的控制块整块 24 字节，GCC 16.2 实测）。

## make_shared 的优势：单次分配

前面提到，`make_shared` 把对象和控制块放在一个连续的内存块里。这带来了三个显著的好处。

首先是**更少的堆分配次数**——从两次减为一次。在性能敏感的代码中，堆分配是昂贵操作（通常涉及锁、遍历空闲链表等），减少分配次数总是好的。写个小程序打印分配地址，就能看到 `make_shared` 只 `new` 一次<RefLink :id="3" preview="cppreference make_shared — typically performs only one allocation for object and control block" />。

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

`make_shared` 也有一个不太为人知的缺点：由于对象和控制块共享同一个内存块，当所有 `shared_ptr` 都被销毁时（强引用归零），对象会被析构，但控制块的内存不会立即释放——必须等到所有 `weak_ptr` 也都销毁（弱引用归零）后，整个内存块才会被回收。如果对象很大且有 `weak_ptr` 仍在使用，可能会造成内存占用比预期更高的现象。如果您预期会有 `weak_ptr` 长期存在，可以考虑使用 `std::shared_ptr<T>(new T)` 来让对象的内存独立于控制块，这样强引用归零时对象内存就能立即释放。

## 引用计数的原子操作与线程安全

`shared_ptr` 的引用计数使用原子操作来保证线程安全。这意味着在多线程环境下，您可以安全地拷贝和销毁 `shared_ptr` 本身（引用计数的增减是原子的），但**被管理对象的访问并不受保护**——如果您有多个线程同时读写对象本身，仍然需要自行加锁。

> 很多人以为 `shared_ptr` 提供了"对象的线程安全"，但实际上它只保证了"**引用计数的线程安全**"。人话版本就是：我可以保证我记录有多少人在用这个资源，在并发时，添加和减少引用这个事情本身，是**线程安全**的，但是你管理的东西，那就不是线程安全的，比如说并发的被托管的对象本身，照样炸！所以，访问对象仍然还需要您自己来上锁。
>
> 咱们可以用 cppreference 的描述来精确理解<RefLink :id="4" preview="cppreference std::shared_ptr — member functions safe on distinct instances; data race on the same instance via non-const access" />：`shared_ptr` 的控制块是线程安全的——多个线程可以同时操作不同的 `shared_ptr` 实例（即使它们指向同一个对象），不需要外部同步。但同一个 `shared_ptr` 实例不能被多个线程同时读写（需要加锁）。被管理对象的并发访问需要自行保证安全<RefLink :id="5" preview="Stack Overflow — To what degree does std::shared_ptr ensure thread-safety?" />。

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

从性能角度看，每次拷贝或析构 `shared_ptr` 都会产生一次原子操作（通常是 `fetch_add` 或 `fetch_sub`）。原子操作在单核系统上开销很小（可能只是一条特殊的 CPU 指令），但在多核系统上会引发缓存一致性协议的开销（cache line bouncing）。如果您的代码频繁创建和销毁 `shared_ptr`（比如在热循环中），这个开销可能会变得非常显著。

引用计数递减时的逻辑尤其值得关注。当 `fetch_sub` 返回 1（意味着这是最后一个 `shared_ptr`）时，需要销毁对象。主流实现（如 GNU libstdc++）使用 `memory_order_acq_rel` 来保证所有之前的写操作对销毁代码可见，并在销毁前插入一个 `acquire` fence。这些内存屏障在 x86 上开销不大（x86 本身就有强内存序），但在 ARM 等弱序架构上可能会导致流水线刷新。

## shared_ptr 的性能开销分析

咱们来做一个直观的对比，把 `shared_ptr`、`unique_ptr` 和裸指针的开销放在一张表里：

| 维度       | 裸指针      | unique_ptr | shared_ptr                   |
| ---------- | ----------- | ---------- | ---------------------------- |
| 对象大小   | 8B (64-bit) | 8B         | 16B                          |
| 额外堆分配 | 无          | 无         | 控制块 (24-32B+)             |
| 拷贝开销   | 8B 复制     | 不可拷贝   | 原子 fetch_add               |
| 析构开销   | 无          | delete     | 原子 fetch_sub + 可能 delete |
| 线程安全   | 无          | 无         | 引用计数安全，对象不安全     |

从这张表可以清楚地看到，`shared_ptr` 在每一个维度上都比 `unique_ptr` 更重。这不是说 `shared_ptr` 不好——它在共享所有权的场景下是正确的设计选择——但您应该在确实需要共享所有权时才使用它，而不是"为了方便到处用 `shared_ptr`"。

在实际项目中，笔者见过不少代码库把几乎所有对象都用 `shared_ptr` 管理，结果就是引用计数到处飞、性能无法优化、循环引用问题频出。更好的做法是在设计阶段就明确所有权关系：大多数资源用 `unique_ptr` 管理，只在确实需要共享的少数地方使用 `shared_ptr`，并通过引用（`T&`）或裸指针（`T*`，不持有所有权）来传递非拥有访问。

## Aliasing Constructor：不为人知的强大特性

`shared_ptr` 有一个非常强大但不太为人知的构造函数，叫做 **aliasing constructor**。它的签名是：

```cpp
template <typename U>
shared_ptr(const shared_ptr<U>& r, T* ptr) noexcept;
```

这个构造函数创建一个新的 `shared_ptr`，它共享 `r` 的所有权（即引用计数与 `r` 共享），但 `get()` 返回的是 `ptr` 而不是 `r.get()`。简单说就是：**它让您持有同一个对象的"一部分"，而不需要单独管理那部分的生命周期**。

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

这个特性在实现"指向容器元素的智能指针"时特别有用——比如您想返回一个指向 `vector` 中某个元素的 `shared_ptr`，但又不想让调用者持有整个 `vector` 的 `shared_ptr`。通过 aliasing constructor，您可以返回一个只暴露元素类型的 `shared_ptr`，而底层仍然由容器的 `shared_ptr` 管理生命周期。

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

使用 `shared_from_this()` 有一个前提条件：对象必须已经被一个 `shared_ptr` 管理。如果您在栈上创建对象或用裸指针管理，调用 `shared_from_this()` 会导致未定义行为。此外，构造函数中不能调用 `shared_from_this()`——因为此时 `shared_ptr` 还没有完成构造。

## 常见误用与踩坑

在深入嵌入式权衡之前，咱们先盘点几个 `shared_ptr` 的常见误用模式。这些"坑"笔者自己踩过不止一次，也希望读者能提前绕开。

**误用一：用 `shared_ptr(this)` 创建第二个控制块**。这是最致命的错误。如果您在一个已经被 `shared_ptr` 管理的对象的成员函数中写 `return std::shared_ptr<Widget>(this)`，编译器会创建一个全新的控制块，引用计数从 1 开始。结果就是两个独立的控制块管理同一个对象——当两个 `shared_ptr` 都被销毁时，对象会被 delete 两次。正确做法是继承 `enable_shared_from_this` 并调用 `shared_from_this()`。

**误用二：在接口中暴露 `shared_ptr` 的所有权意图**。如果您写一个函数 `void process(std::shared_ptr<Widget> w)`，签名本身就暗示了"我要和您共享所有权"。但很多时候函数只是想使用对象，并不需要持有它。这种场景下传 `const Widget&` 或 `Widget*` 更合适——不暗示所有权，也没有引用计数的开销。

**误用三：用 `shared_ptr` 管理"不需要共享"的对象**。有些团队为了图省事，把所有堆对象都用 `shared_ptr` 管理——"反正 shared_ptr 什么都能管"。这会导致所有权语义模糊（谁都持有等于谁都不负责）、性能下降（到处是原子操作）、循环引用风险增加。笔者的经验是：**90% 的对象应该用 `unique_ptr` 管理，只有 10% 真正需要共享的用 `shared_ptr`**。

**误用四：忽视 `make_shared` 与 `new` 的区别**。`make_shared` 把对象和控制块合并在一次分配中，但这也意味着对象的析构和控制块的释放不在同一时刻——当所有 `shared_ptr` 被销毁时，对象析构，但如果还有 `weak_ptr` 存活，整个内存块（包括对象占用的空间）不会释放直到所有 `weak_ptr` 也被销毁。对于大型对象，这可能导致"明明没人用了但内存还不还回来"的现象。如果您预期会有长期存活的 `weak_ptr`，用 `shared_ptr<T>(new T)` 把对象和控制块分开分配可能更合适。

## shared_ptr 滥用的系统性后果

笔者这里单独开了一节，很简单，因为笔者自己曾经就是滥用的人。。。

咱们前面逐个盘点了 `shared_ptr` 的常见误用模式，但问题的严重性远不止"某个地方写错了"。当 `shared_ptr` 在代码库中被系统性滥用时，它带来的是**架构层面的慢性毒药**——不是那种编译不过的急性错误，而是让代码库逐渐变得不可维护、不可推理、不可优化的渐进式腐化。笔者见过不止一个项目因为"所有对象都用 `shared_ptr` 管理"而陷入这种泥潭，修复起来往往需要大规模重构。

### 所有权模型的崩塌

在一个健康的设计中，每个对象都应该有一个明确的所有者——"谁创建的、谁销毁的、生命周期由谁决定"——这些问题应该在设计阶段就回答清楚。但当您到处使用 `shared_ptr` 时，这些问题的答案变成了"谁知道呢，引用计数归零的时候自然就销毁了"。听起来很方便，但代价是您失去了对对象生命周期的控制力：您不能保证对象在任何特定时刻存活（因为其他持有者可能随时释放），也不能保证对象在任何特定时刻被销毁（因为可能有您不知道的持有者还在引用它）。这种"谁都不负责"的状态，和全局变量泛滥带来的问题如出一辙。

Sean Parent 在 2013 年 GoingNative 大会的《C++ Seasoning》演讲里一针见血地把滥用 `shared_ptr` 比作**隐式全局变量**——他的原话是 "A shared pointer is as good as a global variable"<RefLink :id="6" preview="Sean Parent, C++ Seasoning, GoingNative 2013, slides PDF" />——任何持有 `shared_ptr` 的代码都在参与对象的生命周期管理，这和全局变量"任何地方都能访问、任何地方都能延长其寿命"的特性惊人地相似。更实际的问题是，一旦您的公共接口返回了 `shared_ptr<T>`，所有调用者都被迫使用 `shared_ptr`，即使他们只是想临时借用一下对象。您剥夺了调用者选择所有权模型的权利——更好的做法是返回 `unique_ptr`（调用者可以自由 `std::move` 为 `shared_ptr`）或者裸指针/引用（非拥有访问）。

### 多线程下的缓存行争用

这个问题在单线程代码中完全不会出现，但在多线程场景下会变得非常刺眼。`shared_ptr` 的控制块中存储着强引用计数和弱引用计数，这两个原子计数器通常在同一个控制块中，很可能共享同一个缓存行（cache line，通常 64 字节）。当多个线程频繁拷贝和销毁指向**同一个对象**的 `shared_ptr` 时，每个线程对引用计数的原子修改都会导致该缓存行在不同核心之间来回 bouncing——即使这些线程操作的是各自独立的 `shared_ptr` 实例，只要它们指向同一个对象，就会竞争同一个控制块的缓存行。

光说不够，咱们来跑个测试。下面这个基准程序构建了一个生产者-消费者的线程安全队列，分别用裸指针和 `shared_ptr` 传递消息。测试环境为笔者的 Windows WSL2 Arch Linux，AMD Ryzen 7 5800H（14 线程），GCC 15.2，`-O2` Release 编译。结果如下：

| 方案         | 消息数 | 平均耗时 | 相对开销    |
| ------------ | ------ | -------- | ----------- |
| 裸指针       | 10,000 | ~30 ms   | 基准        |
| `shared_ptr` | 10,000 | ~35 ms   | **+15-20%** |

15-20% 的开销在实际应用中可能更显著，因为咱们的测试使用了 mutex 保护的队列，mutex 的开销会掩盖部分 `shared_ptr` 的开销。在无锁队列或更高并发场景下（如原始测试中的 8 线程），`shared_ptr` 的开销会更加明显。这个开销的来源很明确：每次 `shared_ptr` 拷贝都要原子递增引用计数，每次销毁都要原子递减——在多线程同时操作同一个控制块的场景下，这些原子操作会引发缓存行争用。低并发、低吞吐的场景可以忽略，高并发热路径上务必慎重。

### 循环引用：静默的内存泄漏

当对象因为循环引用而泄漏时，您不会得到任何错误提示——`shared_ptr` 的引用计数永远不会归零，对象就静静地躺在堆上占用内存。没有崩溃、没有断言失败、没有任何日志告诉您"嘿，这个对象泄漏了"。您只能在内存使用持续增长时才可能察觉到问题，然后用 Valgrind 或 AddressSanitizer 才能定位到泄漏点。更糟糕的是，循环引用往往不是两个对象之间的简单环路，而是涉及多个对象的复杂依赖图——A 持有 B，B 持有 C，C 又持有 A——这种情况下追踪引用链本身就是一件非常痛苦的事情。

相比之下，`unique_ptr` 的独占所有权模型让循环引用在编译期就不可能发生（您无法构造一个合法的独占所有权环），这是它在设计层面的巨大优势。如果您发现自己需要大量使用 `weak_ptr` 来打破循环引用，这本身就是一个强烈的信号：您的所有权模型设计有问题，应该重新审视对象之间的依赖关系，而不是用 `weak_ptr` 到处打补丁。

### 所有权反转：回调中的定时炸弹

这个问题在异步编程中特别常见，而且出了 bug 极难排查。假设对象 A 持有一个 Timer，Timer 的回调通过 `shared_from_this()` 捕获了 A 的 `shared_ptr`。当 A 在主线程被 reset 之后，Timer 线程反而成了 A 的唯一持有者——A 的生命周期被"反转"到了 Timer 线程上。如果 Timer 的析构函数需要 join 自身所在的线程（`std::jthread` 就会这么做），就会触发 `std::system_error`：一个线程尝试 join 自己，这是未定义行为。这类 bug 的根源在于 `shared_ptr` 让您"懒得思考所有权"——您以为释放了 A，但回调还在暗处拽着它。正确做法是在设计阶段就明确生命周期约束：如果 A 的析构依赖 Timer 线程结束，那 A 必须在 Timer 之前销毁，用 `unique_ptr` 的独占语义来表达这个约束。

### 析构时机的不确定性与实时隐患

当您终于决定析构掉一个 `shared_ptr` 所持有的对象的时候时，您无法确定这是否是最后一个——对象可能在这次 drop 中被销毁，也可能因为还有其他持有者而继续存活。这意味着析构函数的调用时机是**不可预测的**，析构顺序也是**未定义的**。

其实蛮危险的，在实时系统中，比如说经典的就是音频回调的case、对于嵌入式则是ISR中断服务例程。或者是任何有实时性要求的代码路径，而恰好这是最后一个持有者，触发的析构函数可能带来不可接受的延迟——堆释放、文件 IO、日志写入，这些都是非确定性的耗时操作。

> Timur Doumler 在讨论 C++ 音频开发的讲座里介绍过一个巧妙的 `ReleasePool` 方案：在低优先级线程上定期清理那些可能需要析构的 `shared_ptr`，确保实时线程上永远不会触发析构<RefLink :id="7" preview="Timur Doumler — Talks on C++ Audio: Sharing Data Across Threads, JUCE Forum, 2018" />。但说到底，如果您在设计阶段就用了 `unique_ptr` 加显式的生命周期管理，根本就不需要这种 workaround。

## 实战选择指南：什么时候该用 shared_ptr

在讲嵌入式权衡之前，咱们先来做一个实战导向的选择分析。很多人在 `unique_ptr` 和 `shared_ptr` 之间犹豫不决，其实判断标准很简单——问自己一个问题：**这个对象是否需要被多个独立的模块共同拥有？**

如果答案是"不"——对象的生命周期由一个明确的"主人"决定，其他模块只是临时借用——那就用 `unique_ptr` + 裸指针/引用传递。这是绝大多数场景。

如果答案是"是"——多个模块确实需要独立地决定"我还在用这个对象"，而且没有一个模块能声称"我是唯一的主人"——那就用 `shared_ptr`。

典型的 `shared_ptr` 适用场景包括：插件系统中的共享模块（多个组件可能同时依赖同一个插件实例，谁也不能提前卸载它）、异步回调链中的共享状态（多个 future/callback 需要保持状态活着直到自己完成）、树或图中的共享节点（多个父节点引用同一个子节点）。

典型的不应该用 `shared_ptr` 的场景包括：函数参数传递（传引用就够了）、对象的唯一所有者（用 `unique_ptr`）、简单的缓存（用 `weak_ptr` 观察，`shared_ptr` 持有）。

咱们来看一个具体的设计决策示例——实现一个简单的任务调度器：

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

第一个版本用 `unique_ptr`——任务提交后所有权归调度器，简单明确。第二个版本用 `shared_ptr`——允许多个调度器或外部代码持有同一个任务的引用，任务在最后一个持有者离开时才被销毁。选哪个取决于您的设计需求，而不是"哪个更方便"。

咱们下一篇，就准备好好聊聊 `weak_ptr`——`shared_ptr` 的搭档，专门解决**循环引用**这个棘手问题。

<ReferenceCard title="参考文献">
  <ReferenceItem
    :id="1"
    author="Bjarne Stroustrup / Herb Sutter (eds.)"
    title="C++ Core Guidelines — R.20-R.24: Smart Pointer Rules"
    publisher="isocpp.org"
    chapter="R.21: Prefer unique_ptr over shared_ptr"
    url="https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#rr-owner"
  />
  <ReferenceItem
    :id="2"
    author="Raymond Chen"
    title="Inside STL: The Different Types of Shared Pointer Control Blocks"
    publisher="The Old New Thing, Microsoft DevBlogs"
    :year="2023"
    url="https://devblogs.microsoft.com/oldnewthing/20230821-00/?p=108626"
  />
  <ReferenceItem
    :id="3"
    author="cppreference.com"
    title="std::shared_ptr — make_shared"
    chapter="Notes: single allocation; memory retention with weak_ptr"
    url="https://en.cppreference.com/w/cpp/memory/shared_ptr/make_shared"
  />
  <ReferenceItem
    :id="4"
    author="cppreference.com"
    title="std::shared_ptr"
    chapter="Thread safety; Implementation notes"
    url="https://en.cppreference.com/w/cpp/memory/shared_ptr"
  />
  <ReferenceItem
    :id="5"
    title="To What Degree Does std::shared_ptr Ensure Thread-Safety?"
    publisher="Stack Overflow"
    url="https://stackoverflow.com/questions/9127816/to-what-degree-does-stdshared-ptr-ensure-thread-safety"
  />
  <ReferenceItem
    :id="6"
    author="Sean Parent"
    title="C++ Seasoning (Slides)"
    publisher="GoingNative 2013"
    :year="2013"
    url="https://sean-parent.stlab.cc/presentations/2013-09-11-cpp-seasoning/cpp-seasoning.pdf"
  />
  <ReferenceItem
    :id="7"
    author="Timur Doumler"
    title="Timur Doumler Talks on C++ Audio: Sharing Data Across Threads (Discussion)"
    publisher="JUCE Forum"
    :year="2018"
    url="https://forum.juce.com/t/timur-doumler-talks-on-c-audio-sharing-data-across-threads/26311"
  />
</ReferenceCard>
