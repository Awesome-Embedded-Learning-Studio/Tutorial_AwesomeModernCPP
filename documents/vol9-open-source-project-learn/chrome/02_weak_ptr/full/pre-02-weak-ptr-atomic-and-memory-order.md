---
chapter: 0
cpp_standard:
- 11
- 17
- 20
description: 从数据竞争出发,讲清 std::atomic 的六种 memory_order,重点拆开 acquire/release 配对,
  并落到 WeakPtr/AtomicFlag 的 release-Set 与 acquire-IsSet
difficulty: intermediate
order: 2
platform: host
prerequisites:
- WeakPtr 前置知识（一）：侵入式引用计数与 scoped_refptr
reading_time_minutes: 13
related:
- WeakPtr 实战（二）：核心骨架与控制块
- WeakPtr 前置知识（三）：序列、SEQUENCE_CHECKER 与 DCHECK/CHECK
tags:
- host
- cpp-modern
- intermediate
- atomic
- memory_order
- 并发
- weak_ptr
title: "WeakPtr 前置知识（二）：std::atomic 与 memory_order"
---
# WeakPtr 前置知识（二）：std::atomic 与 memory_order

上一篇咱们搓那个最小引用计数基类,`add_ref` 塞了 `memory_order_relaxed`,`release` 塞了 `memory_order_acq_rel`。当时笔者一笔带过没展开,留了个尾巴:为什么不是统一用一种?这几个 `memory_order_*` 到底在管什么?

这尾巴得收。WeakPtr 整套机制最妙的地方就在于它不拿锁,只靠一对 `release`/`acquire` 的原子操作,就做到了"一个序列把对象析构了,另一个序列手里的 WeakPtr 永远 deref 不到已析构对象"。您要吃透这条保证,就得先把 memory order 啃下来,它是后面整条 deref 链路的地基。笔者这篇就把它从数据竞争一路拆到 WeakPtr 的那一对配对。

---

## 为什么需要原子操作:数据竞争

先看一个会出事的最小场景,两个线程同时戳一个普通 `int`:

```cpp
int counter = 0;
// 线程 A
counter++;
// 线程 B(同时)
counter++;
```

`counter++` 长得像一条语句,真跑起来是三步:读 `counter`、加 1、写回。两个线程的三步一旦交错,A 读到 0、B 也读到 0、A 写 1、B 写 1,最后结果是 1 不是 2。这就是数据竞争(data race)。按 C++ 标准,只要撞上数据竞争,程序行为直接未定义,不只是结果错,编译器有权基于"程序无数据竞争"的假定随便发挥,您拿到的可能是一份完全意料之外的二进制。

`std::atomic` 管两件事。一是原子性,把那三步捏成不可分割的一步,谁都插不进来;二是可见性和顺序,管一个线程的写在什么条件下能被另一个线程看到,以及编译器和 CPU 能不能把读写重排。原子性这一半大家心里有数,真正难、也真正决定并发对不对的是后一半,也就是 `memory_order`。

---

## std::atomic 基础

`std::atomic<T>` 给出原子的 `load`/`store`/`exchange`/`compare_exchange`(CAS)以及 `fetch_add`/`fetch_sub` 等读-改-写操作:

```cpp
std::atomic<int> a{0};
a.store(1, std::memory_order_release);   // 原子写
int v = a.load(std::memory_order_acquire);   // 原子读
a.fetch_add(1, std::memory_order_relaxed);   // 原子 +1
```

每个操作都能挂一个 `memory_order` 参数,不写就默认 `std::memory_order_seq_cst`,最强也最贵。C++ 一共给了六种,从弱到强摆开:

---

## 六种 memory order

| memory_order | 适用操作 | 语义 |
|---|---|---|
| `relaxed` | load/store/RMW | 只保证原子,不建立同步,不限制重排 |
| `consume` | load | 数据依赖版 acquire(实践中近似 acquire,标准淡化,别用) |
| `acquire` | load | 阻止后续读写重排到本次 load 之前;配对 release 建立同步 |
| `release` | store | 阻止之前读写重排到本次 store 之后;配对 acquire 建立同步 |
| `acq_rel` | RMW(fetch_*) | 同时是 acquire 和 release |
| `seq_cst` | 所有 | 额外保证全局总序,最强,默认,最贵 |

笔者先劝您一句,别想着一次把六种全刻进脑子。真到工程里翻来覆去就三种组合:`relaxed`(只数数、不传信息)、`acquire`+`release`(单写多读的同步)、`seq_cst`(需要全局一致顺序时才上)。下面咱们把 `acquire`/`release` 重点拆开,因为这正是 WeakPtr 用的那一对。

---

## relaxed:只保证原子,不传递信息

`relaxed` 最省心,它只把这次操作变成原子的,对编译器和 CPU 的重排不做任何约束,也不保证这次操作前后的其它读写能被别的线程看到。

笔者的经验是,`relaxed` 最自然的归宿就是纯计数器,您只在乎数字加对了,不在乎它和别的内存操作谁先谁后:

```cpp
std::atomic<int> hits{0};
// 多个线程都执行:
hits.fetch_add(1, std::memory_order_relaxed);   // 只想数对总次数
```

坑也藏在这。`relaxed` 没法用来传递"数据准备好了"这种信号,您要是图省事这么写:

```cpp
// 线程 A
data = 42;                                   // 普通写
ready.store(true, std::memory_order_relaxed);

// 线程 B
while (!ready.load(std::memory_order_relaxed));
assert(data == 42);   // 不保证通过!可能读到 data 的旧值
```

`relaxed` 既不拦"先写 `ready` 再写 `data`"这种重排,也不保证 B 读到 `ready==true` 时 `data==42` 对它可见。assert 翻不翻车全看运气。要传这种信号,就得升级到 `release`/`acquire`。

---

## acquire / release:建立 happens-before

这是日常用得最多的一对,也是并发正确性的命门。规矩就两条,笔者给您念一遍。

线程做 `release` 的 store 时,本次 store 之前它做过的所有读写,都不许被重排到 store 之后;线程做 `acquire` 的 load 时,本次 load 之后要做的所有读写,都不许被重排到 load 之前。

把这两头配起来就精彩了:线程 A 做完一堆写,用 `release` 存一个标志;线程 B 用 `acquire` 读这个标志。一旦 B 的 acquire 读到了 A 的 release 写入的值,A 在 release 之前做的所有写,对 B 全都可见。这条边就叫 happens-before。

```cpp
// Platform: host | C++ Standard: C++17
#include <atomic>
#include <thread>
#include <cassert>

int data = 0;
std::atomic<bool> ready{false};

void producer() {
    data = 42;                                   // (1) 普通写
    ready.store(true, std::memory_order_release); // (2) release:把 (1) "发布"出去
}

void consumer() {
    while (!ready.load(std::memory_order_acquire)) { // (3) acquire:等 release
        // spin
    }
    assert(data == 42);                          // (4) 保证通过!
}

int main() {
    std::thread t1(producer);
    std::thread t2(consumer);
    t1.join(); t2.join();
    return 0;
}
```

为什么 `assert` 保证通过?因为 (3) 的 acquire 读到了 (2) 的 release 写入,于是 (2) 之前的 (1) 对 (3) 之后的 (4) 可见。release/acquire 配对就这么点事,它把"对象写完了"这条信号连同写进去的数据,原子又有序地递到了另一个线程手里。

### 回到 release() 的 acq_rel

[前置知识（一）](./pre-01-weak-ptr-intrusive-refcount-and-scoped-refptr.md) 里引用计数的 `release` 用了 `memory_order_acq_rel`,不是单纯的 `release`。原因很简单,`fetch_sub` 是个读-改-写操作,既读旧值又写新值。`acq_rel` 让它两头都顾,acquire 那一半能看到别的线程对此计数器的最新写,release 那一半把自己对对象的写发布给下一个接管 `delete` 的线程。引用计数归零时这套交接刚好够用。

---

## 回到 WeakPtr:AtomicFlag 的 release/acquire 配对

工具齐了,咱们终于可以精确地盯 WeakPtr 的 liveness 机制。Chromium 的 `base::AtomicFlag` 是 `std::atomic<uint_fast8_t>` 的一个语义收窄封装,它在 WeakPtr 里用得极其克制,`Set()` 用 release,`IsSet()` 用 acquire,就这么一对:

```cpp
// base::AtomicFlag 简化等价(我们教学版直接用 std::atomic)
class AtomicFlag {
public:
    void Set() {
        flag_.store(1, std::memory_order_release);
    }
    bool IsSet() const {
        return flag_.load(std::memory_order_acquire) != 0;
    }
private:
    std::atomic<uint_fast8_t> flag_{0};
};
```

对应到 WeakPtr 的两个动作,失效这条链子是 `WeakReferenceOwner::Invalidate()` 一路调到 `Flag::Invalidate()`,最后落到 `invalidated_.Set()`,一次 release-store。它发生在 factory 让对象进入不可用状态的那一刻,典型场景有两个:要么是 `WeakPtrFactory` 析构之初(对应"最后成员"惯用法,见 [02-3](./02-3-weak-ptr-factory-and-last-member.md)),要么是您显式调 `InvalidateWeakPtrs()`。这里有个顺序笔者特意提醒一句,是 factory 析构先调 Invalidate 把所有 WeakPtr 失效,然后其它成员才轮到析构,这正是"最后成员"守护成员析构期的机制,而不是反过来"析构完了才 Invalidate"。

判活这条链子反着走,`WeakPtr::get()` 调到 `WeakReference::IsValid()`,再调到 `Flag::IsValid()`,本质就是 `!invalidated_.IsSet()`,一次 acquire-load。

把这两端套进上一节的 producer/consumer 模型,您会发现它们就是同一对 pattern。线程 A 是 factory 所在序列,析构对象之前先 `Invalidate` 做 release-store,把"对象已不可用"这个状态连同之前的所有写一起发布出去;线程 B 拿着 WeakPtr,想 deref 之前先 `IsValid` 做 acquire-load。如果 B 读到失效,说明 A 那边的 release 已经生效,A 序列在 release 之前的所有写,包括让对象进入不可用状态那些操作,对 B 全都可见,B 心里有数不该 deref,`get()` 老老实实返回 `nullptr`。反过来,如果 B 读到的是未失效,说明 A 还没 release,对象此刻还喘着气,B deref 是安全的。

WeakPtr "不用锁也能安全 deref" 的全部秘密就在这一对 release/acquire。它不拦着谁也不等谁,只立起"看到失效位 ⇒ 看到对象被改过的所有状态"这条 happens-before 边,剩下的交给原子读本身。

### 那为什么不用 relaxed,也不用 seq_cst

`relaxed` 想都不用想,前面那个 `data`/`ready` 反例已经把它证死了,光原子不传同步,acquire 一侧可能读到 `ready==true` 却看不到对象被析构前的状态,WeakPtr 直接漏判。

`seq_cst` 倒是能工作,但代价笔者觉得划不来。它要求所有 `seq_cst` 操作凑成一个全局一致的总顺序,x86 上要更强的指令撑着,普通 acquire/release 在 x86 上几乎就是普通的 load/store,`seq_cst` 的 store 却得挂 `MFENCE` 或 `LOCK` 前缀。WeakPtr 的 deref 是热路径,每一次 `get()` 都要读一次 flag,换 `seq_cst` 等于把开销无意义地放大一截。acquire/release 在这儿是正好够用的选择,同步建得起来,又不替 `seq_cst` 的全局总序买单。Chromium 源码注释里特意强调"IsSet 必须 inline、对 WeakPtr 有可测性能影响",根源就在这。

### MaybeValid 为什么也是 acquire

到 02-4 咱们会碰上 `MaybeValid()`,它给跨序列提供一个"乐观判活"的查询。您看这名字 maybe,第一反应没准是"那用 `relaxed` 凑合一下呗"。真不行,它内部就是 `!invalidated_.IsSet()`,照样得 acquire 读。道理和上头一模一样,它一旦读到"已失效",得让调用方敢拍胸脯说"对方序列让对象进入失效状态的所有写,我这儿都看见了"。`MaybeValid` 的"maybe"不在 memory order 上,而在于它不检查序列绑定,您可以从任意序列调它,但它只保证负面结果可信,读到失效就是真失效;正面结果(读到未失效)不保证您后续操作还安全。这条区别咱们留到 02-4 展开。

---

memory order 这块儿算是拆透了。还有最后一块并发地基得补上,就是序列与 `SEQUENCE_CHECKER`,外加 `DCHECK` 与 `CHECK` 那道分界线。三块地基凑齐,02-2 咱们就能动手把 Flag、WeakReference、WeakPtr 一级级搭起来。

## 参考资源

- [cppreference: std::atomic](https://en.cppreference.com/w/cpp/atomic/atomic)
- [cppreference: std::memory_order](https://en.cppreference.com/w/cpp/atomic/memory_order)
- [Herb Sutter — `atomic<>` Weapons`](https://channel9.msdn.com/Shows/Going+Deep/Cpp-and-Beyond-2012-Herb-Sutter-atomic-Weapons-1-of-2)
- [Chromium `base/synchronization/atomic_flag.h`](https://source.chromium.org/chromium/chromium/src/+/main:base/synchronization/atomic_flag.h)
