# 嵌入式现代C++教程——智能指针

写嵌入式代码久了，你会发现一件哲学级问题：谁来负责释放内存？是我？是你？还是大家一起？C++ 标准库给了两位主角：`unique_ptr`（独占式，单身贵族）和 `shared_ptr`（合租式，分摊生活费）

> PS，其实还有个`weak_ptr`，但是那是循环引用的事情，咱们先不唠。

------

## 先简单回顾

行动之前，先快速的Review以下基本概念。

`unique_ptr<T>`：仅持有一个对象的所有权，支持移动但不拷贝。它就是 RAII 的小清新：构造即拥有，离开作用域即释放。内存开销极低——除了保存一个裸指针的大小（在目标平台上通常就是 `sizeof(void*)`），没有别的控制块。

`shared_ptr<T>`：允许多个 `shared_ptr` 共同拥有同一个对象。其核心是**控制块（control block）**，记录强引用计数和弱引用计数，并可能保存自定义删除器和分配器。对象释放发生在最后一个强引用消失时。代价：额外指针和控制块（内存）以及每次拷贝/析构时的引用计数操作（通常是原子递增/递减）。

------

## 再次强调——为什么要谨慎选择？

嵌入式常常意味着：

- 内存紧张：几 KB 到几 MB，不同于 PC/服务器那样“有的是 RAM”。
- CPU 资源宝贵：不能随便浪费周期在频繁的原子操作或缓存未命中上。
- 实时/中断环境：在 ISR（中断服务例程）里，你常常不能做阻塞或复杂的内存管理。
- 链接/库有限：标准库实现（和其对原子操作的实现）在某些平台上可能不是“理想的”。

因此，`unique_ptr` 的简洁（内存消耗小、操作代价低、行为确定）是很大的优点；而 `shared_ptr` 的“自动共享销毁”看起来舒服，但付出的代价在嵌入式环境下可能很快就“吃掉你的周末”和几百字节 RAM。

------

## 内存成本

谈数字前先说原则：**`unique_ptr` 几乎不占额外空间，`shared_ptr` 会占额外空间（对象 + 控制块）**。具体到平台，差别如下（示例说明，实际请以目标平台 `sizeof(void*)` 与你的标准库实现为准）：

- 在 32 位系统上：
  - `unique_ptr<T>`：通常占 4 字节（指针）。
  - `shared_ptr<T>`：对象指针 + 控制块指针（或其它实现），常见实现是 8 或 12 字节；控制块本身在堆上还会占至少几十个字节（记录引用计数、弱计数、对齐、可能的 deleter 指针等）。
- 在 64 位系统上：
  - `unique_ptr<T>`：通常占 8 字节。
  - `shared_ptr<T>`：通常占 16 字节（两个指针）；控制块通常更大。

控制块通常是额外的一次堆分配（除非使用 `std::allocate_shared`/`make_shared` 优化），也就是说使用 `shared_ptr` **通常会带来一次额外的分配**（或更多），这在堆受限时很糟糕。

**实战结论（内存）：**如果你的单个对象很小（比如一个 32 字节的结构体），控制块的开销会把总体内存成本推高很多倍。在 RAM 只有几十 KB 的 MCU 上，这不是可选项，而是要避免的坑。

------

## 控制周期

`shared_ptr` 的每一次拷贝/赋值/析构通常都会触发对引用计数的更新。如果多线程环境下引用计数是原子的，那就是原子加/减；原子操作不是“免费午餐”：

- 原子递增/递减可能导致内存屏障（memory fence），影响指令重排序，导致 CPU pipeline 冲突。
- 在 SMP 或含有外设 DMA 的系统上，原子操作可能比普通简单操作慢好几倍。
- 在一些嵌入式工具链/库中，`std::atomic` 可能不是 lock-free，会退化为禁用中断的互斥或进入临界区，这在 ISR 或高优先级任务里是致命的（会引起延迟超标）。

因此，若你的系统有严格的控制周期或在 ISR 中需要频繁传递所有权，`shared_ptr` 的原子计数可能会违反实时约束。所有不要在 ISR 或对延迟敏感的路径中使用 `shared_ptr`。如果必须用共享所有权，请在任务级别完成计数，并确保计数是非原子的（仅在单线程或在临界区内操作）或用更轻量的自定义方案。

------

## 几个场景

### 场景 A：外设句柄（GPIO、UART、DMA） —— `unique_ptr` 更合适

外设通常有明确的所有权边界（初始化 -> 运行 -> 释放），不需要共享销毁责任。使用 `unique_ptr` 或带自定义删除器的 `unique_ptr` 很自然：

```cpp
// gpio.h（伪代码）
struct GpioHandle { int id; /*资源描述*/ };

void gpio_release(GpioHandle* h) {
    // 关闭外设，注销中断等
    if (h) { /* ... */ }
    delete h;
}

// 使用：
auto gpio = std::unique_ptr<GpioHandle, void(*)(GpioHandle*)>(new GpioHandle{1}, gpio_release);
// or using alias
using UniqueGpio = std::unique_ptr<GpioHandle, void(*)(GpioHandle*)>;
UniqueGpio g(new GpioHandle{1}, gpio_release);
```

优点：确定、开销小、适合在构造/析构中执行实际释放逻辑。不在 ISR 中 delete，不在中断路径上做复杂操作。

------

### 场景 B：共享配置或共享资源（高层逻辑）——谨慎使用 `shared_ptr`

假如你有一个“系统配置对象”，多个任务读它但很少写，使用 `shared_ptr<const Config>` 在任务之间共享读取是可以的（但要注意内存）：

```cpp
auto cfg = std::make_shared<const Config>(/*...*/); // 优先使用 make_shared

// 多个任务持有 shared_ptr<const Config>
```

如果要在运行时频繁更换配置（拷贝次数多），`shared_ptr` 的引用计数更新会频繁发生，代价明显。

------

### 场景 C：当内存紧张且需要共享语义 —— 使用“侵入式引用计数（intrusive）”

侵入式引用计数是把计数器放到对象内部，这样你可以避免额外的控制块分配（因为对象本身就包含计数）。示例（单线程/单核环境下用非原子计数）：

```cpp
struct IntrusiveBase {
    uint16_t ref = 0;
    void inc_ref() { ++ref; }
    void dec_ref() { if (--ref == 0) delete this; }
};

struct MyThing : IntrusiveBase {
    // payload
};

template<typename T>
class intrusive_ptr {
    T* p = nullptr;
public:
    intrusive_ptr(T* t = nullptr): p(t) { if (p) p->inc_ref(); }
    intrusive_ptr(const intrusive_ptr& o): p(o.p) { if (p) p->inc_ref(); }
    intrusive_ptr(intrusive_ptr&& o): p(o.p) { o.p = nullptr; }
    ~intrusive_ptr() { if (p) p->dec_ref(); }
    // ... move/copy/assign ...
};
```

优点：没有额外 heap 分配，内存更可控。缺点：对象类型需要内嵌计数器（改变类设计），并且你要自己处理线程安全（原子 vs 非原子）。

------

## 最后给出几条容易复制的“工程准则”

- 确定好，咱们的资源到底是不是要共享的，可不可以提供访问点而非所属权——来选择到底用不用，选不选智能指针
- 必须共享时用 `make_shared` 优化并控制共享点，不要让 `shared_ptr` 成为设计偷懒的托词。
- 在 ISR 或硬实时路径里**绝对避免**引用计数的动态开销。
- 内存紧张时优先考虑侵入式计数、对象池或静态分配。
- 设计初期画出所有权边界图，比事后追踪泄漏要便宜得多。

