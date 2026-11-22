---
title: "无锁入门：CAS 循环、ABA、内存回收"
description: "lock-free 与 wait-free 的进度保证差别，CAS 循环与无锁栈的实现，ABA 问题的成因与 tagged pointer、Hazard Pointer 两种对策，以及何时该用、何时不该用无锁。"
chapter: 5
order: 2
tags:
  - host
  - cpp-modern
  - advanced
  - atomic
  - 无锁
difficulty: advanced
platform: host
cpp_standard: [11, 20, 26]
current_status:
  title: 正在进行打磨
  detail: 现在是LLM写好的初级稿件，引入是为了初步公开 + 希望收集反馈
  variant: polishing
---

# 无锁入门：CAS 循环、ABA、内存回收

本篇是选读区的无锁入门。卷五第五单元把 `std::atomic` 讲到必修深度：[《原子操作解决到哪一步》](../../chapter05-atomic-memory-model/01-what-atomics-solve)给出操作面与 CAS 的重试语义，[《一个线程怎样观察另一个线程的修改》](../../chapter05-atomic-memory-model/03-happens-before)与[《内存序的四档：从顺序一致性到 relaxed》](../../chapter05-atomic-memory-model/04-choosing-memory-orders)一起，把六种内存序铺开。但写出无锁容器不算入门验收，复杂无锁数据结构不进必修。咱们把超出必修的这部分整篇平移进选读区。

读它之前，您需要掌握第五单元的全部内容：CAS 的 weak 与 strong 之别，acquire/release 建立的同步关系，relaxed 的适用范围。旧卷素材里与 SeqLock、自旋锁、双重检查锁定等模式相关的内容已分篇处理，归入[《SeqLock 与自旋锁》](05-seqlock-spinlock)与第五单元正文，本篇专心讲无锁。

平移的来路同样交代清楚。机制讲解、代码与练习原样保留，我改动的只有编号、frontmatter、指向正文的交叉引用，外加判死词表清理。文中的分析没有声称实测读数，判断都来自机制推演与文献，您读到哪一步拿不准，欢迎用第五单元的实验办法自己量。

咱们前几个单元用 mutex 加 condition_variable，搭出了线程安全队列和容器。那些内容回答的是“怎么做原子操作”的问题，但还没有碰过一个更深层的问题：**如果咱们完全不用锁，能不能写出正确的并发数据结构？**

我第一次听到“无锁编程”这个词的时候，脑子里冒出来的是一种“这不就是炫技吗”的直觉。后来看了几个无锁栈的实现，才意识到这不是炫技——这是一整套和基于锁的并发完全不同的思维方式。您不再用一把锁把临界区包起来让线程排队，而是让所有线程同时操作数据结构，用原子操作来协调冲突——谁冲突了谁就重来，但系统整体永远在前进。这套思路的代价是正确性推理的复杂度暴增，收益是在高争用场景下延迟更可控。

“无锁”这个词其实挺有误导性的，它不是说不使用任何锁。它说的是，系统的整体进度不会被任何一个线程的延迟或崩溃所阻塞。这个区分很重要，也很微妙。我刚开始接触这个领域的时候也被绕进去过好几次，所以本篇咱们从进度保证的精确定义开始，把 lock-free 和 wait-free 的区别讲解明白，然后进入 CAS 循环这个无锁编程的核心构建块，实现一个经典的无锁栈，再讨论 ABA 问题与内存回收，无锁编程里最难搞的两件事。最后咱们聊聊什么时候该用无锁，什么时候不该用，这个判断力比会写无锁代码本身更重要。

## Lock-free vs Wait-free：到底在保证什么

很多人把“lock-free”理解为“不用 mutex”，这个理解不算错，但不够精确——差得还挺远。在学术界，Herlihy 在 1991 年的论文里奠定了 wait-free 和 lock-free 的定义基础，后来 Herlihy、Luchangco 和 Moir 在 2003 年，又引入了 obstruction-free 这个更弱的概念。C++ 标准和工业界基本沿用了这套三级框架，所以咱们需要把三个层次的进度保证搞清楚。

最弱的是 **obstruction-free**（无阻碍）：如果一个线程在某个时间点被单独执行（也就是说，其他线程都暂停了），它能在有限步内完成操作。其实，就是“如果没有竞争，就能前进”。这个保证太弱了，几乎没有实用价值，咱们后面不会展开讨论它。

**Lock-free**（无锁）就进了一步：它保证在任何时刻，**系统中至少有一个线程**能在有限步内完成操作。您注意，这里是“至少一个”，不是“每一个”。也就是说，在 lock-free 系统里，系统整体是在前进的，但个别线程可能因为持续 CAS 失败而一直重试，理论上就存在饥饿的可能。[《SeqLock 与自旋锁》](05-seqlock-spinlock)里写的自旋锁就不是 lock-free 的：如果一个线程拿着锁不放手（比如被操作系统挂起了），其他所有线程都得干等，系统整体停滞。

**Wait-free**（无等待）是最强的保证：**每一个线程**都保证能在有限步内完成自己的操作，不管其他线程在做什么、以什么速度运行。咱们把它当理想情况记在心上，wait-free 意味着没有饥饿、没有重试循环，每个操作都有确定的上界步数。

从弱到强的层次是：blocking，然后 obstruction-free，然后 lock-free，最强是 wait-free。每往上一层，实现难度都大幅增加。咱们在实际工程中追求的通常是 lock-free，因为 wait-free 的实现代价太高，而且 lock-free 在大多数场景下已经足够好了，至少系统不会因为一个线程卡住而整体瘫痪。

这里有一个常见的误解，需要咱们提前澄清：**lock-free 不意味着“更快”**。lock-free 解决的是进度保证问题，不是性能问题。一个 lock-free 的数据结构在低争用场景下可能比 mutex 版本还慢，因为 CAS 重试的开销可能比直接拿锁还大。lock-free 的优势体现在高争用、对延迟敏感的场景，它不会因为某个线程被调度器暂停而导致整个临界区堵塞。这个区别咱们后面在“何时使用无锁”部分还会用具体数据来展开。

## CAS 循环：无锁编程的基石

好，进度保证的概念搞清楚了，现在咱们来动手。几乎所有的无锁算法，都建立在一个原子原语之上：Compare-And-Swap（CAS）。C++ 里对应的是 `std::atomic` 的 `compare_exchange_weak` 和 `compare_exchange_strong` 成员函数。[《原子操作解决到哪一步》](../../chapter05-atomic-memory-model/01-what-atomics-solve)已经介绍过两个函数的签名和语义，这里不重复基础内容，聚焦它们在无锁编程里的使用模式。

CAS 的核心语义，咱们用大白话讲就是：**“我觉得当前值应该是 X，如果是的话就把它换成 Y，否则告诉我现在到底是什么”**。用代码来说，`compare_exchange_weak/strong` 接受两个关键参数，`expected`（预期值）和 `desired`（新值）。如果当前值等于 `expected`，就改成 `desired` 并返回 `true`。如果不等，就把当前值写回 `expected` 并返回 `false`。整个操作是原子的，不会有其他线程的修改夹在“比较”和“交换”之间。

weak 和 strong 的区别咱们快速回顾一下。`compare_exchange_weak` 允许虚假失败（spurious failure），即使当前值确实等于 `expected`，它也可能返回 `false`。这在某些硬件架构（比如 ARM 的 LL/SC 指令对）上是不可避免的。`compare_exchange_strong` 保证不会虚假失败。在 x86 上，weak 和 strong 生成完全相同的机器码（都是 `lock cmpxchg`），但在 ARM 上，strong 版本需要内部加一个重试循环来消除虚假失败。

一条关键的经验法则——和第五单元说的一样：**在循环中使用 weak，在非循环的一次性判断中使用 strong**。原因很直接——如果您已经在循环里了，CAS 失败后反正要重试，多一次虚假失败只是多一轮循环而已。而如果在循环外用 weak，一次虚假失败就会导致您错误地认为值已经变了，可能走错分支。在 ARM 上，循环里用 strong 会导致嵌套的重试循环（外层是您的循环，内层是 strong 的循环），白白浪费指令。

咱们看一个最简单的 CAS 循环——原子加法的手动实现。这个例子在实际工程中没必要（`fetch_add` 就够了），但它清晰地展示了 CAS 循环的基本结构，是咱们后面写无锁栈的基础：

```cpp
std::atomic<int> value{0};

void atomic_add(int delta)
{
    int old = value.load(std::memory_order_relaxed);
    while (!value.compare_exchange_weak(
        old,
        old + delta,
        std::memory_order_relaxed,
        std::memory_order_relaxed))
    {
        // CAS 失败时 old 被自动更新为当前值
        // 重新计算 old + delta，然后重试
    }
}
```

咱们看这个循环在干什么：读取当前值，计算新值，然后尝试把当前值从 `old` 换成 `old + delta`。如果在这个过程中有其他线程修改了 `value`，CAS 会失败并告诉咱们最新值是什么（通过写回 `old` 参数），咱们只需要用最新值重新计算再试。这就是所谓的“乐观并发”：假设没有冲突，冲突了就重来。您会发现这个循环不可能死循环——每次失败后 `old` 都被更新为更新的值，系统整体在前进——这就是 lock-free 语义在微观层面的体现。

当然，对于加法这个操作，直接用 `fetch_add` 就行了，不需要手动写 CAS 循环。CAS 循环的威力体现在更复杂的操作上，比如更新链表指针、交换数据结构的头节点。这些操作无法用简单的 `fetch_add` 或 `exchange` 表达，必须用 CAS。接下来咱们就来写一个真正的无锁数据结构。

## 经典无锁栈：从 CAS 循环到真实数据结构

理解了 CAS 循环的基本模式，咱们就可以挑战一个真正的无锁数据结构了。无锁栈是无锁数据结构里最简单的一个，也是几乎所有无锁编程教材的起点，Treiber 在 1986 年就发表了它的设计。咱们把整体结构搭起来，然后逐步看 push 和 pop 的实现。

```cpp
#include <atomic>
#include <optional>

template <typename T>
class LockFreeStack {
public:
    LockFreeStack() : head_(nullptr) {}
    ~LockFreeStack();

    void push(const T& value);
    std::optional<T> pop();

private:
    struct Node {
        T data;
        Node* next;
        explicit Node(const T& val) : data(val), next(nullptr) {}
    };

    std::atomic<Node*> head_;
};
```

结构非常简单：一个单链表，`head_` 是原子指针，指向栈顶节点。您看，所有操作都在头部进行，要同步的只有 `head_`。

### push：往栈顶插入节点

```cpp
void push(const T& value)
{
    Node* new_node = new Node(value);
    Node* old_head = head_.load(std::memory_order_relaxed);

    do {
        new_node->next = old_head;
    } while (!head_.compare_exchange_weak(
        old_head,
        new_node,
        std::memory_order_release,
        std::memory_order_relaxed));
}
```

push 的逻辑分三步：创建新节点，把新节点的 `next` 指向当前栈顶，然后尝试用 CAS 把 `head_` 从 `old_head` 换成 `new_node`。如果 CAS 成功，新节点就成为了新的栈顶。如果 CAS 失败，说明有其他线程抢在前面修改了 `head_`，但 `compare_exchange_weak` 会把 `old_head` 更新为最新值，咱们只需要重新设置 `new_node->next` 再试。

咱们注意内存序的选择：CAS 成功时使用 `memory_order_release`，这保证了新节点中的 `data` 和 `next` 的写入在 CAS 成功之前完成，其他线程通过 `acquire` 读到 `head_` 的新值后一定能看到这些写入。CAS 失败时用 `relaxed` 就够了——失败了什么都没改，不需要任何同步。`head_.load()` 也用 `relaxed`，因为真正的同步是由 CAS 操作本身的内存序保证的。

### pop：从栈顶取出节点

```cpp
std::optional<T> pop()
{
    Node* old_head = head_.load(std::memory_order_acquire);

    while (old_head) {
        Node* next_node = old_head->next;
        if (head_.compare_exchange_weak(
                old_head,
                next_node,
                std::memory_order_acquire,
                std::memory_order_relaxed)) {
            // CAS 成功，old_head 已经从栈上摘下来了
            T value = std::move(old_head->data);
            // 这里有一个严重的问题：什么时候 delete old_head？
            return value;
        }
        // CAS 失败，old_head 已被更新为最新值，重试
    }

    return std::nullopt;  // 栈空
}
```

pop 的逻辑也很直观：读取当前栈顶，记下它的 `next`，然后尝试用 CAS 把 `head_` 从 `old_head` 换成 `next_node`。成功的话，`old_head` 就被从栈上摘下来了，咱们取出它的数据返回。

但是——事情到这里还没完，代码里埋着一个大麻烦，我用注释标出来了。咱们拿到了 `old_head`，也知道它已经从栈上摘下来了，但**不能立刻 `delete` 它**。原因在于：在咱们执行 CAS 之前，可能还有其他线程也读到了同一个 `old_head`，正在操作它的 `next` 指针。如果咱们现在就把 `old_head` 的内存释放了，那些线程就是在访问已释放的内存——use-after-free，典型的未定义行为。这个问题不像 data race 那样加个 `std::atomic` 就能解决，它是**逻辑层面的生命周期问题**。

这个问题就是无锁编程里最棘手的**内存回收问题**。咱们暂且把它放一放，等讲完 ABA 问题之后一起讨论，ABA 和内存回收是纠缠在一起的，分开看不容易看清全貌。

## ABA 问题：CAS 的头号陷阱

接下来咱们碰面的是无锁编程里最臭名昭著的 bug 模式——ABA 问题。如果您在面试中被问过无锁编程，大概率也被问过这个。它之所以出名，理解门槛并不高，要害是它实践中真的会发生，而且一旦发生就极难调试——程序不会崩溃，只会默默地产生错误结果。

### ABA 是怎么发生的

咱们用一个具体的场景来演示。假设有两个线程在操作咱们的无锁栈，初始状态是 A -> B -> C，栈顶是 A。

咱们让线程 1 开始执行 `pop`：它读取 `head_ = A`，读到 `A->next = B`，准备执行 CAS 把 `head_` 从 A 换成 B。但就在 CAS 之前，线程 1 被调度器挂起了——这就是麻烦的起点。

咱们接着看线程 2 的工作：它完整地执行了两次 `pop`，弹出 A（栈变成 B -> C），再弹出 B（栈变成 C）。然后线程 2 `push` 了一个新值，恰好分配器复用了 A 的内存地址，所以新节点的地址和刚才的 A 一样。现在栈变成了 A' -> C，但这个 A' 的地址和之前的 A 完全相同。

线程 1 醒来了，执行 CAS：`head_.compare_exchange(A, B)`。您会看到，它发现 `head_` 确实是 A（地址相同），CAS 成功，`head_` 被设成了 B。

问题来了：B 已经被线程 2 弹出并释放了。线程 1 把 `head_` 指向了一个已经失效的节点。接下来任何对栈的操作都会访问已释放的内存——程序随时可能崩溃，或者更糟糕地，静默地产生错误结果，而您根本不知道从哪里开始查。

### 为什么 ABA 如此危险

ABA 之所以阴险，是因为 CAS 只关心“值是否等于预期”，不关心“值在这个期间有没有变过”。咱们把它的盲区摆在明处：在 ABA 的场景里，指针的值确实从 A 变到了 A（中间经过了 B），CAS 无法区分“一直都是 A”和“A -> B -> A”，对 CAS 来说，两种情况完全一样。这算不上 CAS 的设计缺陷，它本是“值比较”原语的固有局限。

您可能会问：这在实践中真的会发生吗？答案是肯定的。在高争用环境下，节点被频繁分配和释放，内存分配器很可能复用刚释放的地址，特别是 `malloc`/`new` 这类针对小对象优化的分配器，它们维护着按大小分桶的空闲链表，刚释放的内存马上就能被再次分配出去。加上多线程的调度时序，“线程 1 读取后被挂起，线程 2 做了一轮完整操作”的场面，完全可能出现。

### Tagged Pointer：给指针加版本号

好，问题清楚了，现在咱们来看解决方案。最常用的方案是 **tagged pointer**（带标签的指针）。思路很直接：把指针和一个递增的版本号打包在一起，每次修改指针时版本号加一。这样即使指针的值从 A -> B -> A，版本号也从 0 -> 1 -> 2，CAS 会因为版本号不匹配而正确地失败——版本号只增不减，不可能出现回环。

在 64 位系统上，咱们可以利用指针的高 16 位来存储版本号，因为大多数架构上，用户空间的指针只用低 48 位。下面是一个简化版的实现：

```cpp
#include <atomic>
#include <cstdint>

template <typename T>
class TaggedPointer {
public:
    TaggedPointer() : atomic_(0) {}
    TaggedPointer(T* ptr, uint16_t tag)
    {
        uint64_t raw = (static_cast<uint64_t>(tag) << kTagShift)
                     | reinterpret_cast<uint64_t>(ptr);
        atomic_.store(raw, std::memory_order_relaxed);
    }

    T* get_ptr() const
    {
        return reinterpret_cast<T*>(atomic_.load(std::memory_order_relaxed) & kPtrMask);
    }

    uint16_t get_tag() const
    {
        return static_cast<uint16_t>(atomic_.load(std::memory_order_relaxed) >> kTagShift);
    }

    bool compare_exchange_weak(TaggedPointer& expected, TaggedPointer desired)
    {
        uint64_t exp_value = expected.atomic_.load(std::memory_order_relaxed);
        if (atomic_.compare_exchange_weak(exp_value,
                desired.atomic_.load(std::memory_order_relaxed))) {
            return true;
        }
        expected = TaggedPointer(exp_value);
        return false;
    }

    TaggedPointer load() const
    {
        return TaggedPointer(atomic_.load(std::memory_order_acquire));
    }

    void store(TaggedPointer tp)
    {
        atomic_.store(tp.atomic_.load(std::memory_order_relaxed),
                     std::memory_order_release);
    }

private:
    std::atomic<uint64_t> atomic_;
    static constexpr uint64_t kTagShift = 48;
    static constexpr uint64_t kPtrMask = (1ULL << kTagShift) - 1;

    explicit TaggedPointer(uint64_t raw) : atomic_(raw) {}
};
```

咱们用 tagged pointer 改写无锁栈的 `push`：

```cpp
void push(const T& value)
{
    Node* new_node = new Node(value);
    TaggedPointer<Node> old_head = head_.load();

    do {
        new_node->next = old_head.get_ptr();
    } while (!head_.compare_exchange_weak(
        old_head,
        TaggedPointer<Node>(new_node, old_head.get_tag() + 1)));

    // 每次成功 CAS 都伴随着 tag + 1
    // 即使指针地址被复用，tag 不会重复，ABA 不会发生
}
```

tagged pointer 的方案有一个前提：您用的架构上 CAS 能操作 64 位（或者 128 位，如果您想用更多的版本号位）。在 x86-64 上这没有问题，`lock cmpxchg` 天然支持 64 位操作。在某些 32 位嵌入式平台上，双字 CAS 可能不可用或代价很高，需要考虑其他方案。

### Hazard Pointer：更通用的内存保护

Tagged pointer 解决了 ABA 问题，但您会发现，它没解决咱们前面提到的内存回收问题，咱们还是不知道什么时候可以安全地 `delete` 一个节点。Hazard Pointer 是 Maged Michael 在 2004 年提出的一种更通用的方案，它同时解决了 ABA 和内存回收两个问题，而且不只适用于栈，也适用于队列、链表等各种无锁数据结构。C++26 已经将 Hazard Pointer 纳入标准（`std::hazard_pointer`）。

咱们把它的核心思想摆出来：每个线程持有一个或一组“危险指针”（hazard pointer），用来声明“我正在访问这个节点”。当一个线程想要释放一个节点时，它不能直接 `delete`，而是检查所有线程的危险指针——如果有人在用这个节点，就暂缓释放。只有确认没有任何线程的 hazard pointer 指向这个节点时，才能安全释放。

咱们看一份简化的伪代码：

```cpp
// 全局的 hazard pointer 表，每个线程一个槽位
constexpr int kMaxThreads = 64;
std::atomic<Node*> g_hazard_pointers[kMaxThreads];

// 线程在访问节点前，先"发布"自己的 hazard pointer
void publish_hazard(int slot, Node* node)
{
    g_hazard_pointers[slot].store(node, std::memory_order_release);
}

// 释放节点前，检查是否有线程在用
bool is_hazardous(Node* node)
{
    for (int i = 0; i < kMaxThreads; ++i) {
        if (g_hazard_pointers[i].load(std::memory_order_acquire) == node) {
            return true;
        }
    }
    return false;
}
```

放进无锁栈的 `pop` 里，咱们大致这样用它：线程发布 hazard pointer 指向 `old_head`，然后执行 CAS。如果 CAS 成功，线程清除自己的 hazard pointer，把 `old_head` 放入一个“待回收列表”。定期地（比如当待回收列表积攒到一定长度），线程扫描所有 hazard pointer，把没人用的节点真正释放掉。

Hazard Pointer 的优点是通用性好，适用于各种无锁数据结构。缺点是性能开销，您每次 `pop` 都要发布和清除 hazard pointer，扫描待回收列表也需要遍历所有线程的槽位。在高争用场景下，这个开销可能很显著。

## 内存回收：无锁编程里最难的问题

前面咱们反复碰到这个问题，每次都暂时搁下了。现在到了正面讨论它的时候。如果您觉得 ABA 问题已经够折腾了，那内存回收会让您更头疼——它是无锁编程中被公认最难搞的问题，也是阻碍无锁数据结构在实际项目中广泛使用的最大障碍之一。

在基于锁的数据结构里，内存回收很简单：拿锁、操作、释放内存、解锁。锁保证了同一时刻只有一个线程在操作数据结构，咱们不会遇到“一个线程还在用节点、另一个线程把它释放了”的问题。

但在无锁数据结构里，多个线程可以同时读取同一个节点。咱们看这个场面：线程 A 刚读完 `old_head->next`，准备执行 CAS，此时线程 B 可能已经把 `old_head` 弹出来并 `delete` 了。线程 A 的 CAS 还没执行，它手里的 `old_head` 已经是悬空指针。这个问题不像 data race 那样可以通过 `std::atomic` 消除，它是一个**逻辑层面的生命周期问题**。

目前主流的方案有几路，咱们一路一路看。除了前面提到的 Hazard Pointer，还有 **Epoch-based Reclamation**（基于时代的回收），以及**引用计数**。

Epoch-based Reclamation 的思路，咱们这样理解：把时间分成若干个“时代”（epoch），全局维护一个当前时代号。每个线程进入临界区时记录自己所在的时代。回收时，只有在所有线程都已经离开了某个时代之后，那个时代的节点才能被安全释放。这个方案比 Hazard Pointer 的扫描开销小，但实现更复杂，而且在某些极端情况下可能延迟回收很久，如果某个线程卡在旧时代里不出来，所有旧时代的节点都积压着无法释放。Facebook 的 Folly 库里有生产级的实现，`folly/concurrency/UnboundedQueue.h` 中的 `WeakRef` 机制就用到了类似的思路。

引用计数听起来最直观：给每个节点加一个原子引用计数，`pop` 时减一，为零时释放。但咱们细看就有问题：引用计数的增减本身也需要原子操作，而且“加载指针”和“增加引用计数”之间有一个窗口，这个窗口内节点可能被其他线程释放。要解决这个“加载-增加”的原子性问题，引用计数方案往往退化为某种形式的 Hazard Pointer 或者需要双字 CAS（double-word CAS），实现复杂度并没有真正降低。`std::atomic<std::shared_ptr>` 在 C++20 里可以用，但它的性能开销不小（通常内部用一把自旋锁实现），咱们拿它做真正的 lock-free 场景并不合适。

## 何时使用无锁——何时不使用

讲了这么多问题和解决方案，您可能会问：既然无锁编程这么复杂，为什么还要用它？答案是：在特定的场景下，无锁确实能带来 mutex 无法提供的性能优势。但这个“特定场景”比您想象的要窄得多。我见过不少案例，花了很大精力把一个 mutex 保护的数据结构改成无锁的，结果 benchmark 跑出来反而更慢了——然后对着数据发呆。

### 适合使用无锁的场景

最典型的场景，咱们放在头一个讲：**高争用、低延迟**。当大量线程频繁竞争同一个数据结构时，mutex 会导致频繁的上下文切换（每次切换都是一次内核态往返，代价在微秒级别）。无锁算法把竞争从“排队等锁”变成了“CAS 重试”，虽然重试也有开销，但重试发生在用户态、不涉及内核调度，延迟更可控、尾部延迟更小。高频交易系统、实时信号处理、网络游戏服务器的主循环，在这些场景下，几微秒的延迟差异可能就是可接受和不可接受的分界线。

**单生产者-单消费者（SPSC）队列**，是咱们要特别留意的另一个无锁场景。因为只有一个生产者和一个消费者，不需要 CAS 循环，只需要 `acquire/release` 语义的原子变量就能实现正确的同步。实现简单、性能极高、几乎没有争用，此时无锁几乎是默认选择。[《SPSC 与 MPMC 无锁队列》](03-lock-free-queues)专门展开 SPSC 队列的设计。

**中断上下文与主循环之间的通信**在嵌入式系统中也很常见。您想在中断处理函数里干活，就不能调用可能阻塞的函数（包括 `mutex::lock`），无锁队列是几乎唯一的选择。

### 不适合使用无锁的场景

请您别急着把项目里的 mutex 全部换掉，下面这些场景里，无锁往往是赔本买卖。

**低争用场景**下无锁往往比 mutex 还慢。原因很简单：mutex 在没有竞争时的加锁/解锁开销其实很低（一条原子指令加一个分支预测），而 CAS 循环即使在成功路径上也至少需要一次原子操作和一次条件判断。您遇到平均每 1000 次访问才竞争一次的数据结构时，mutex 的总开销很可能低于无锁。

**复杂的临界区**不适合无锁。您若想用 CAS 表达“从 map 里删一个元素同时更新 size 计数器”这样的复合修改，会发现极其困难，代码难以正确实现，更难以维护。mutex 天然支持任意复杂的临界区，这个优势在复杂逻辑面前是不可替代的。

**团队维护成本**也是一个不能忽视的考量。无锁代码的阅读、审查和调试难度远高于 mutex 版本。一个 CAS 循环的 bug 可能在百万次运行中只触发一次，ThreadSanitizer 对无锁代码的误报率也不低。如果您的团队还没有足够的无锁编程经验，咱们劝您用 mutex 写出正确的代码，它比用 CAS 写出的快但不可靠的代码更有价值，正确的代码永远优于快速的错误代码。

### Benchmark：别猜，量一下

任何关于“无锁更快”或“mutex 更快”的断言，在没有具体 benchmark 数据的情况下都是空谈。我见过太多“理论上无锁更快”，实际上却因为缓存一致性开销、CAS 重试风暴、伪共享这类原因，跑得反而更慢的案例。并发性能的瓶颈，往往在您意想不到的地方。量测设计的完整方法，[《量之前，先把条件立住：串行基线与测量的陷阱》](../../chapter06-validating-designs/03-measuring-benefits)整篇都在讲。

您组织一个基本的 benchmark 框架，应该包括：不同线程数（1、2、4、8、16）下的吞吐量测试，不同操作比例（纯 push、纯 pop、混合）下的延迟分布（p50、p99、p999），以及不同硬件上的结果对比。[《SPSC 与 MPMC 无锁队列》](03-lock-free-queues)里有一组完整的 benchmark 对比，涵盖 SPSC、mutex 队列和 MPMC。

咱们可以拿这个简单但有效的 benchmark 模板起步：

```cpp
#include <atomic>
#include <thread>
#include <chrono>
#include <iostream>
#include <vector>

/// 测量 N 次 push + N 次 pop 的总耗时
template <typename Queue, typename T>
void benchmark_queue(Queue& q, int num_items, int num_producers, int num_consumers)
{
    auto start = std::chrono::high_resolution_clock::now();

    std::vector<std::thread> producers;
    std::vector<std::thread> consumers;

    std::atomic<int> consumed_count{0};

    for (int i = 0; i < num_producers; ++i) {
        producers.emplace_back([&q, num_items, num_producers] {
            int per_producer = num_items / num_producers;
            for (int j = 0; j < per_producer; ++j) {
                while (!q.push(T(j))) {
                    // 队列满，重试
                }
            }
        });
    }

    for (int i = 0; i < num_consumers; ++i) {
        consumers.emplace_back([&q, &consumed_count, num_items] {
            T value;
            while (consumed_count.load(std::memory_order_relaxed) < num_items) {
                if (q.pop(value)) {
                    consumed_count.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }

    for (auto& t : producers) t.join();
    for (auto& t : consumers) t.join();

    auto end = std::chrono::high_resolution_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    std::cout << "Items: " << num_items
              << " | Producers: " << num_producers
              << " | Consumers: " << num_consumers
              << " | Time: " << ms << " ms"
              << " | Throughput: " << (num_items * 1000.0 / ms) << " ops/s"
              << "\n";
}
```

运行 benchmark 时，建议关闭 CPU 频率缩放（`cpupower frequency-set -g performance`），绑定 CPU 核心（`taskset` 或 `pthread_setaffinity_np`），多次运行取中位数。这些控制变量的手段对并发 benchmark 的结果影响很大——不控制的话，您可能今天跑出一个数据、明天又跑出完全不同的数据，然后对着两组数据发呆。

## 往哪里走

本篇咱们建立了无锁编程的基本认知框架：lock-free 和 wait-free 不是一回事（前者保证系统整体前进，后者保证每个线程都前进），CAS 循环是无锁算法的核心构建块（“乐观并发”——冲突了就重来），无锁栈是最经典的入门案例，但它已经暴露了 ABA 问题与内存回收两道核心难题。tagged pointer 用版本号解决了 ABA 问题，Hazard Pointer 提供了更通用的内存保护，但两者都有各自的性能代价和实现复杂度。最后咱们讨论了何时该用无锁、何时不该用，这个工程判断力比会写无锁代码本身更重要。

不过无锁栈只是一个起点。[《SPSC 与 MPMC 无锁队列》](03-lock-free-queues)接着带咱们面对更实用的数据结构：SPSC 队列因为生产者和消费者各只有一个，不需要 CAS 循环，实现简洁且性能极高，是嵌入式和网络编程里的常见选择。MPMC 队列则需要处理多生产者多消费者的竞争，复杂度又上一个台阶，那边还有一组完整的 benchmark 对比——数据说话，不靠猜测。

> 完整示例代码在 [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP)，您可以到 `code/volumn_codes/vol5/ch04-concurrent-data-structures/` 取用。

## 练习

### 练习 1：实现无锁栈并观察 CAS 重试

请您使用本篇给出的 `LockFreeStack` 代码，完成以下任务：

1. 实现完整的 `push` 和 `pop`（暂时不处理内存回收，测试时让程序短时间运行即可）。
2. 启动 4 个线程并发 push 共 1000000 个整数，然后用 4 个线程并发 pop。
3. 在 CAS 循环中添加一个计数器，统计 CAS 重试的总次数。在高争用下这个数字会很大。
4. 对比 `std::mutex` + `std::stack` 的性能。别急着比快慢——试试不同的线程数和操作数。

### 练习 2：复现 ABA 问题

ABA 问题在正常情况下很难复现，因为需要精确的调度时序。但咱们可以用 `std::this_thread::sleep_for` 人为制造延迟来放大窗口：

1. 在 `pop` 的 CAS 之前加一个 `sleep_for(std::chrono::milliseconds(100))`。
2. 让线程 1 开始 `pop`（会在 CAS 前 sleep），线程 2 在这 100ms 内把栈上的元素全部弹出再 push 回一个新节点。
3. 观察线程 1 醒来后 CAS 是否成功、数据是否正确。如果分配器碰巧复用了地址，您就看到了 ABA。

### 练习 3：Tagged Pointer 改造

1. 使用本篇提供的 `TaggedPointer` 模板改写 `LockFreeStack`，让 `head_` 变成 `TaggedPointer<Node>` 类型。
2. 重新运行练习 2 的测试，确认 ABA 不再发生。
3. 思考：tagged pointer 方案在 32 位平台上会遇到什么问题？如果指针占 32 位，您怎么在剩余空间里编码版本号？

> 完整示例代码在 [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP)，您可以到 `code/volumn_codes/vol5/ch04-concurrent-data-structures/` 取用。

## 参考资源

- [Wait-Free Synchronization — Maurice Herlihy (1991)](https://cs.brown.edu/people/mph/Herlihy91/p124-herlihy.pdf)
- [Hazard Pointers: Safe Memory Reclamation for Lock-Free Objects — Maged Michael](https://www.cs.otago.ac.nz/cosc440/readings/hazard-pointers.pdf)
- [compare_exchange_weak / compare_exchange_strong — cppreference](https://en.cppreference.com/w/cpp/atomic/atomic/compare_exchange)
- [C++ Atomic Operations: The Performance Cost — Fedor Pikus, CppCon 2024](https://www.youtube.com/watch?v=ZQFzMfHIxng)
- [Non-blocking algorithm — Wikipedia](https://en.wikipedia.org/wiki/Non-blocking_algorithm)
- [Lock-Free Programming — cppreference](https://en.cppreference.com/w/cpp/atomic#Lock-free_property)
