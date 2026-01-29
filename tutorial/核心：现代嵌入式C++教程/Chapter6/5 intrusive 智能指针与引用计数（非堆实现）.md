# 嵌入式现代C++教程——intrusive 智能指针与引用计数

## 前言

在讨论 intrusive 智能指针之前，我们必须先承认一个事实：现代 C++ 中最广为人知的智能指针设计，并不是为嵌入式系统准备的。`shared_ptr` 所提供的共享所有权语义，在桌面与服务器环境中非常自然，但它隐含的实现前提——动态分配、独立控制块、原子引用计数——在资源受限、时序敏感的系统里，往往会变成一种负担。嵌入式开发者对这种“我只是想安全地用个对象，结果却引入了额外堆行为”的设计，天然是警惕的。

但这并不意味着嵌入式系统不需要“共享对象”。相反，在驱动层、设备抽象层、通信栈或系统服务中，同一个对象被多个模块同时引用，是一种非常常见且合理的需求。问题的关键不在于“要不要共享”，而在于“共享的代价是否可控”。

## intrusive 智能指针

intrusive 智能指针正是在这个背景下显得格外有吸引力。它并不是一种新发明，而是一种非常朴素、甚至有些“反现代”的设计思路：**既然对象需要知道自己何时该被销毁，那么引用计数干脆就放在对象内部**。对象不再是被动地由外部控制块管理生死，而是明确地承担起“我还被多少人使用”的责任。

这种设计的第一个变化，是引用计数不再是一个独立分配的结构，而成为对象的一部分。对象的大小稍微增大了一点，但作为交换，系统中少了一次隐式的内存分配，也少了一层间接访问。这对于嵌入式系统来说，几乎永远是一个值得的交易。更重要的是，引用计数的存储位置变得明确、可预测，它不再隐藏在某个你看不见的控制块里。



```cpp
class intrusive_refcount {
public:
    void add_ref() noexcept {
        ++refcount_;
    }

    void release() noexcept {
        if (--refcount_ == 0) {
            destroy();
        }
    }

protected:
    intrusive_refcount() = default;
    virtual ~intrusive_refcount() = default;

private:
    void destroy() noexcept {
        this->~intrusive_refcount();
    }

    uint32_t refcount_{0};
};
```

在 intrusive 方案中，一个可被共享的对象通常会继承自一个简单的引用计数基类。这个基类提供增加与减少引用计数的接口，并在计数归零时触发对象的销毁行为。这里的“销毁”需要非常小心地理解：**在嵌入式语境下，销毁往往意味着析构，而不意味着释放内存**。这是 intrusive 设计与桌面 C++ 最大的分野之一。

在很多嵌入式系统中，对象的内存来源并不具备“自由释放”的语义。它可能位于静态存储区，可能来自一个固定大小的内存池，也可能是通过 placement new 构造在一块预先分配好的缓冲区中。在这些情况下，调用 `delete` 不仅是不合适的，甚至是错误的。intrusive 智能指针如果仍然坚持“引用计数归零就 delete”，反而会破坏系统的整体设计。因此，一个更符合嵌入式直觉的 intrusive 引用计数实现，通常只在计数归零时显式调用析构函数。析构函数负责释放对象所持有的资源，例如关闭外设、注销中断、停止 DMA 或归还系统句柄，而对象本身所占据的内存则交还给它最初的“所有者”——静态区、内存池或上层管理逻辑。这种分离让对象的生命周期语义变得清晰而诚实：**引用计数决定“对象是否还活着”，而不是“内存是否还能用”**。

```cpp
template<typename T> // 可惜了，如果更加现代的C++20+，可以试一试concept约束
// 这里可以static assert一下，要求是intrusive_refcount的派生
class intrusive_ptr {
public:
    intrusive_ptr() noexcept = default;

    explicit intrusive_ptr(T* p) noexcept : ptr_(p) {
        if (ptr_) ptr_->add_ref();
    }

    intrusive_ptr(const intrusive_ptr& other) noexcept : ptr_(other.ptr_) {
        if (ptr_) ptr_->add_ref();
    }

    intrusive_ptr& operator=(const intrusive_ptr& other) noexcept {
        if (this != &other) {
            reset();
            ptr_ = other.ptr_;
            if (ptr_) ptr_->add_ref();
        }
        return *this;
    }

    ~intrusive_ptr() {
        reset();
    }

    T* get() const noexcept { return ptr_; }
    T& operator*() const noexcept { return *ptr_; }
    T* operator->() const noexcept { return ptr_; }

    void reset() noexcept {
        if (ptr_) {
            ptr_->release();
            ptr_ = nullptr;
        }
    }

private:
    T* ptr_{nullptr};
};

```

围绕这种对象设计，一个 intrusive 智能指针本身可以非常简单。它只需要在构造、拷贝和销毁时，分别调用对象的引用计数接口。指针本身不需要知道对象是如何构造的，也不需要知道对象最终住在哪里。它只是一个纪律严明的访问者：使用前登记，离开时注销。当最后一个访问者离开，对象自行完成善后工作。

## so, 为什么要有呢？

这种设计在工程上的好处是显而易见的。对象的生命周期规则是显式的、可审计的，任何阅读代码的人都能立刻意识到：“这个类型是可共享的，它内部有引用计数”。这在嵌入式项目中尤为重要，因为对象往往代表着真实的硬件资源，而不是一段可以随意丢弃的内存。相比之下，`shared_ptr` 将生命周期逻辑隐藏在模板和控制块之后，反而容易让人忽略其真实成本。

另一个常被提及的问题是线程安全。intrusive 引用计数并不强制使用原子操作，这一点在桌面 C++ 看来似乎是缺点，但在嵌入式系统中却是一种优势。是否需要原子性，取决于系统是否存在真正的并发访问；是否需要内存序保证，取决于对象是否跨任务或跨中断共享。这些决策不应由通用库替你做出，而应由系统架构来决定。intrusive 的开放性，使得你可以在不改变整体设计的前提下，根据需要引入临界区、轻量锁或原子类型。

从更高的角度看，intrusive 智能指针体现了一种非常“嵌入式”的设计哲学：**对象并不追求完全的自治，而是与系统环境达成一种明确的契约**。它知道自己可能被共享，也知道自己不会被随意释放内存。它承担了必要的责任，但并不越权行事。这种克制，在资源有限的系统中，往往比“功能齐全”更重要。

如果说 `shared_ptr` 的设计目标是“让大多数程序员不必思考对象生命周期”，那么 intrusive 智能指针的目标恰恰相反：**它要求你在设计阶段就想清楚对象该如何被管理**。而在嵌入式系统中，这种提前思考，往往正是系统稳定性的来源。

