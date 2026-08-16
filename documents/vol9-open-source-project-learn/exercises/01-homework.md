---
title: "卷 9 课后练习（Homework）"
description: "卷 9（开源项目学习）的课后练习：四个主题（OnceCallback / WeakPtr / flat_map / NoDestructor）各出 2~3 题（基础+进阶），另加 2 道跨主题综合与 1 道 L5 挑战（无锁 MPSC 任务队列，改编自 Vyukov 经典算法）。难度覆盖 L1~L5，题目全部做了变式处理，参考答案独立成文件、逐步解答附知识点链接。"
chapter: 9
order: 1
tags:
  - host
  - intermediate
  - cpp-modern
  - 回调机制
  - weak_ptr
  - map
  - 内存管理
difficulty: intermediate
platform: host
cpp_standard: [17, 20, 23]
reading_time_minutes: 15
prerequisites:
  - "卷 9 chrome/ 四个主题全部章节"
related:
  - "卷 9 Lab：mini-chrome 基础库四件套"
  - "卷 9 Project：mini_chrome 任务投递系统"
---

# 卷 9 课后练习（Homework）

## 引言

本卷四个主题都手把手带您走了一遍 Chromium `//base` 的真东西，但"看懂"和"做得出来"之间隔着一段路，这段路只能靠您自己走。这里的题按主题组织，OnceCallback / WeakPtr / flat_map 各三道（基础 + 进阶），NoDestructor 两道，最后是两道跨主题综合和一道 L5 挑战。每题标注难度档位（L1~L5，口径见[练习总览](index.md)）和涉及章节，题目都做了"变式"——换场景、换数据、换推理方向，照抄教材例题是抄不出答案的。

所有题目都要真编译真跑，把输出贴下来才算完。工具链与教程一致：WSL Arch，g++ 16.1.1 / clang++ 22.1.8；默认 `-std=c++20`，仅 9.1-C（deducing this）与 9.C-3（`std::move_only_function`）挂 `-std=c++23`，统一 `-Wall -Wextra` 起步（个别题目会要求别的旗标，题面会写明）。答案在独立的[参考答案](02-homework-solutions.md)文件里，按题号对应，每步解答带知识点链接。建议一个主题做完再看答案，卡住先点回题目标注的章节链接补课，实在做不出来再翻答案。

## 9.1 OnceCallback：回调设计

### 9.1-A {#hw-9-1-a}

难度 **L1** · 涉及[OnceCallback 前置知识（一）：函数类型与模板偏特化](../chrome/01_once_callback/full/pre-01-once-callback-function-type-and-specialization.md)

教材里的 `FuncTraits` 拆的是普通函数签名，这次换个场景：您在写一个信号处理框架，需要把"信号槽签名"拆成返回类型与参数包。完成三件事，都要真编译真跑：

1. 写一个 `SlotTraits`，主模板不提供定义，偏特化 `SlotTraits<R(Args...)>` 暴露 `ReturnType`、`ArgsTuple` 和 `kArity`。用 `static_assert` 验证 `int(double, char)` 拆出 `ReturnType == int`、`kArity == 2`，`void()` 拆出 `void`、`kArity == 0`。
2. 再验证三条"是非题"，全用 `static_assert`：`std::is_function_v<void()>` 是真是假？`std::is_pointer_v<void()>` 呢？`std::is_pointer_v<void(*)()>` 呢？把这三条的实际真假先**动笔预测**再上机验证。
3. 在 `main` 里打印 `kArity` 的值（比如 `SlotTraits<int(int, int, int)>::kArity`），证明偏特化在运行期也拿到了正确的 3。

[参考答案 →](02-homework-solutions.md#hw-9-1-a)

### 9.1-B {#hw-9-1-b}

难度 **L2** · 涉及[OnceCallback 前置知识（二）：std::invoke 与统一调用协议](../chrome/01_once_callback/full/pre-02-once-callback-invoke-and-callable.md)

教材里 `std::invoke` 是散着演示的，这次把它集中成一个函数：写一个模板 `uniform_call(f, args...)`，内部只用 `std::invoke` 转发，然后让它依次调度**四类**可调用对象并打印结果：

1. 自由函数 `int add(int, int)`；
2. 一个捕获了变量的 lambda；
3. 成员函数指针 `&Account::deposit`——注意第一个实参分别用**引用**和**指针**两种姿势各调一次，体会 `std::invoke` 怎么替您解引用；
4. 指向数据成员的指针 `&Rect::width`（配 `std::invoke` 读出一个字段值）。

再回答一问：为什么第 3 类不能写成 `f(args...)` 直接调用？如果您在 `uniform_call` 里不用 `std::invoke` 而直接 `f(args...)`，编译会在哪一步、报什么性质的错？**先预测**再真实验证（把直接调用的版本也编译一遍，贴出真实报错的开头几行）。

[参考答案 →](02-homework-solutions.md#hw-9-1-b)

### 9.1-C {#hw-9-1-c}

难度 **L3** · 涉及[once_callback 设计指南（二）：逐步实现](../chrome/01_once_callback/hands_on/02-once-callback-implementation.md)、[OnceCallback 实战（二）：核心骨架搭建](../chrome/01_once_callback/full/01-2-once-callback-core-skeleton.md)、[OnceCallback 前置知识（六）：Deducing this (C++23)](../chrome/01_once_callback/full/pre-06-once-callback-deducing-this.md)

把 OnceCallback 的核心骨架自己撸一遍（**不要求** bind_once / 取消令牌 / then，那些留给 Lab）。要求，单文件、C++23、`-Wall -Wextra -Wpedantic` 零警告：

1. 主模板只声明不定义，偏特化 `OnceCallback<R(Args...)>` 落地实现；内部三态枚举 `Status{kEmpty, kValid, kConsumed}` + `std::move_only_function<R(Args...)>`。
2. 模板构造函数挂 `not_the_same_t` 约束；拷贝删除、移动保留且移动后源对象 `is_null()`。
3. `run()` 用 deducing this + `static_assert` 拦左值调用，`impl_run` 先取出 `func_`、置 `kConsumed`、再执行（异常安全顺序）；**消费后再调 run 必须触发断言**——用一个子进程跑死亡用例，贴出真实退出码和报错文本。
4. 验证四种基本场景：非 void 返回、void 返回、move-only 捕获（`unique_ptr`）、移动后源对象变空。

[参考答案 →](02-homework-solutions.md#hw-9-1-c)

## 9.2 WeakPtr：弱指针设计

### 9.2-A {#hw-9-2-a}

难度 **L1** · 涉及[WeakPtr 前置知识（零）：弱引用与生命周期难题](../chrome/02_weak_ptr/full/pre-00-weak-ptr-weak-reference-and-lifetime.md)

两道预测题，都要真跑。①对下面的程序，**先动笔预测**每一步的输出，再上机验证：

```cpp
#include <iostream>
#include <memory>

struct Foo {
    int x;
    Foo(int v) : x(v) {}
    ~Foo() { std::cout << "~Foo\n"; }
};

int main() {
    std::weak_ptr<Foo> wp;
    {
        auto sp = std::make_shared<Foo>(42);
        wp = sp;
        std::cout << "use_count=" << sp.use_count() << '\n';
    }
    std::cout << "expired=" << wp.expired() << '\n';
    auto locked = wp.lock();
    std::cout << "locked=" << (locked ? "yes" : "no") << '\n';
    return 0;
}
```

解释：`~Foo` 打印之后，对象占的那块内存还了吗？为什么（结合 `make_shared` 控制块与对象合并分配说）？

②`if (!wp.expired()) sp->use();` 这种写法教材说会踩 TOCTOU 竞态，换成 `lock()` 才安全。用你自己的话写一段不超过 60 字的注释，说明"lock() 把判活和延寿塞进同一个原子操作"这句话里，"延寿"两个字到底延的是什么命。

[参考答案 →](02-homework-solutions.md#hw-9-2-a)

### 9.2-B {#hw-9-2-b}

难度 **L2** · 涉及[WeakPtr 前置知识（一）：侵入式引用计数与 scoped_refptr](../chrome/02_weak_ptr/full/pre-01-weak-ptr-intrusive-refcount-and-scoped-refptr.md)

手写侵入式引用计数的最小闭环，单文件、C++17 即可。要求：

1. 写 `RefCountedThreadSafe` 基类：`add_ref` 用 `relaxed`，`release` 用 `acq_rel` 返回"是否该 delete"，`has_one_ref` 用 `acquire`。计数器 `mutable std::atomic<int>`。
2. 写 `scoped_refptr<T>` 外壳：拷贝增计数、移动不增不减、析构在 `release()` 返回真时 `delete ptr_`。
3. 写一个 `Flag` 继承它，析构 `private`（堵住外部 `delete`）。验证三件事，都要贴输出：①两个 `scoped_refptr` 共享时 `has_one_ref()` 为假，一个时是真；②一个计数器实例自始至终只有**一次**构造（在构造/析构里埋计数器打印，数一下次数）；③把"外部 `delete flag.get()`"那行打开编译，**贴出真实编译错误**，说清是 `private` 析构在起作用。

[参考答案 →](02-homework-solutions.md#hw-9-2-b)

### 9.2-C {#hw-9-2-c}

难度 **L3** · 涉及[WeakPtr 实战（二）：核心骨架与控制块](../chrome/02_weak_ptr/full/02-2-weak-ptr-core-skeleton-and-control-block.md)、[WeakPtr 实战（三）：WeakPtrFactory 与「最后成员」惯用法](../chrome/02_weak_ptr/full/02-3-weak-ptr-factory-and-last-member.md)

把 WeakPtr 三层骨架（Flag → WeakReference → WeakPtr）加 factory 撸出来，单文件、C++20。核心机制一个不能省：Flag 继承侵入式引用计数 + `std::atomic<uint_fast8_t>` 的失效位（`Set` 用 release、`IsSet` 用 acquire）；`WeakPtr::get()` 走"先判活再给指针"的守门链。验证四件事，都贴输出：

1. 对象活着：`wp` 判真、`wp->x` 取值正确、`wp.get()` 等于对象地址；
2. `invalidate_weak_ptrs()` 一次失效**所有**已铸 WeakPtr（铸两个，失效后两个 `get()` 都返回 `nullptr`）；
3. factory 析构（对象出作用域）后，手里存的 WeakPtr 自动失效——这就是"最后成员"惯用法，写一个 `Controller`（成员在前、factory 最后声明），出作用域后验证 `wp` 判假；
4. 挑战自测：把 factory 声明挪到成员**前面**（故意写错的 BadController），用 ASan（`-fsanitize=address`）跑一个"对象析构后再经 WeakPtr 摸成员"的程序，贴出 ASan 报告的抬头几行，说明您抓到了什么。

[参考答案 →](02-homework-solutions.md#hw-9-2-c)

## 9.3 flat_map：有序容器设计

### 9.3-A {#hw-9-3-a}

难度 **L2** · 涉及[flat_map 前置知识（三）：比较器、strict_weak_order 与透明查找](../chrome/03_flat_map/full/pre-03-flat-map-comparator-and-transparent.md)、[flat_map 前置知识（零）：有序关联容器与 std::map 的红黑树](../chrome/03_flat_map/full/pre-00-flat-map-ordered-assoc-container-intro.md)

写一个 `CountingString`：内部包一个 `std::string`，**每次构造**（含拷贝构造、也含从 `const char*` 的隐式转换构造）都把全局计数器加一。然后做一个对比实验：

1. 把若干 `CountingString` 键塞进一个 `std::map<CountingString, int, std::less<CountingString>>`（**不透明**比较器），用字符串字面量 `"alpha"` 查一次 `find`——先预测计数器会增加多少，真跑贴出增量；
2. 换成 `std::less<>`（透明比较器）再做一遍同样的查找，贴出增量；
3. 用一段话解释两次增量的差来自哪里——`const char*` 在两次查找里各变成了什么？这跟 flat_map 默认 `Compare = std::less<>` 的关系是什么？

[参考答案 →](02-homework-solutions.md#hw-9-3-a)

### 9.3-B {#hw-9-3-b}

难度 **L3** · 涉及[flat_map 实战（二）：flat_tree 核心骨架](../chrome/03_flat_map/full/03-2-flat-map-flattree-skeleton.md)、[flat_map 实战（三）：查找与插入](../chrome/03_flat_map/full/03-3-flat-map-lookup-and-insert.md)

按教材骨架复刻 `flat_tree<Key, GetKeyFromValue, KeyCompare, Container>` 的最小版，单文件、C++20。要求：模板签名四个参数齐备；`GetFirst` 与 `std::identity` 两个提取器；构造期 `sort_and_unique`（stable_sort + unique + erase）；`find` 用 `lower_bound` + 判等；`insert` 走 `lower_bound` + `emplace` 并拒绝重复 key。验证：

1. 无序带重复数据构造后，遍历打印 keys 是严格升序且无重复；
2. `find` 命中/未命中两种结果各打印一次；
3. 实测那条 O(n) shift 曲线：从空容器起步，头部 emplace 10 万次 vs 尾部 push_back 10 万次，贴出两边耗时（`-O2` 编译）和你的结论——为什么 flat_map 的插入**没有**摊还 O(1) 这回事。

[参考答案 →](02-homework-solutions.md#hw-9-3-b)

### 9.3-C {#hw-9-3-c}

难度 **L3** · 涉及[flat_map 前置知识（四）：tag dispatch 与 sorted_unique_t](../chrome/03_flat_map/full/pre-04-flat-map-tag-dispatch-and-sorted-unique.md)、[flat_map 实战（四）：sorted_unique 构造优化](../chrome/03_flat_map/full/03-4-flat-map-sorted-unique-construction.md)

给上一题的 `flat_tree` 补上 `sorted_unique_t` 构造（空 tag 类型 + `inline constexpr` 实例），普通构造排序去重、sorted_unique 构造跳过排序只做 debug 校验（用 `assert` 模拟 `DCHECK`）。验证三件事：

1. 有序数据走 sorted_unique 构造，能正确接管（打印 size 与首个 key）；
2. **撒谎**的后果：把乱序数据塞进 sorted_unique 构造，debug 构建（不带 `-DNDEBUG`）跑一次——**贴出真实退出码与 abort 消息**，说明断言在哪一行炸的；
3. 用 `-DNDEBUG` 重编译同一个撒谎版本，跑一遍贴出结果——这次为什么它"活"了？这个差异正是教材里"诚实契约"的含义，用你自己的话总结一句。

[参考答案 →](02-homework-solutions.md#hw-9-3-c)

## 9.4 NoDestructor：静态生命周期管理

### 9.4-A {#hw-9-4-a}

难度 **L1** · 涉及[NoDestructor 前置知识（一）：placement new 与对齐存储](../chrome/04_no_destructor/full/pre-01-placement-new-and-aligned-storage.md)

写一个 `MiniNoDestructor<T>`：`alignas(T) char storage_[sizeof(T)]` + placement new 构造 + `~MiniNoDestructor() = default` + `get()` 用 `reinterpret_cast` 返回。配一个构造和析构都打印的 `Noisy` 类型验证，要求：

1. 在函数里建一个 `static const MiniNoDestructor<Noisy>`，程序运行完贴出**完整输出**——您应该只看到一行 `Noisy()`，看不到 `~Noisy()`。解释：编译器生成的 `~MiniNoDestructor()` 析构的是谁？为什么它不会把 `~Noisy()` 排进析构链？
2. 声明一个 `alignas(1) char buf[sizeof(Noisy) + 4]`（把对齐强行降到 1 字节），打印 `buf` 起始地址及 +1/+2/+3 三个偏移对 `alignof(Noisy)` 的余数，**先预测**这四个余数里必有一个非 0，然后说清为什么 `alignas(T)` 这一笔不能省——不写它，placement new 撞上的是什么？

[参考答案 →](02-homework-solutions.md#hw-9-4-a)

### 9.4-B {#hw-9-4-b}

难度 **L2** · 涉及[NoDestructor 前置知识（零）：静态存储期、初始化与析构](../chrome/04_no_destructor/full/pre-00-static-storage-and-init.md)

写一个 `GetSharedCounter()`：函数局部 `static` 的对象，构造时对一个 `std::atomic<int>` 计数加一。然后开 **16 个线程**同时第一次调用它，全部 join 后打印构造计数。要求：

1. 贴出最终计数——它必须是 1。解释：这层保证是谁给的？是 NoDestructor 自己加锁了吗，还是另有其人？把机制名说全（中文名 + 编译器底下的实现手段，如 `__cxa_guard_acquire`）。
2. 变式自测：把函数里的局部静态换成"每次返回一个新的堆对象"，同样 16 线程首调，计数变成多少？贴出结果，说清这两种写法差在哪。

[参考答案 →](02-homework-solutions.md#hw-9-4-b)

## 9.C 跨主题综合与挑战

### 9.C-1 {#hw-9-c-1}

难度 **L3** · 涉及[WeakPtr 实战（五）：与回调集成——关闭 OnceCallback 的环](../chrome/02_weak_ptr/full/02-5-weak-ptr-bind-integration.md)、[once_callback 设计指南（二）：逐步实现](../chrome/01_once_callback/hands_on/02-once-callback-implementation.md)

把 01-4 手搓取消令牌的尾巴正式收掉：实现一个 `bind_weak_once(method, receiver, args...)`，把"成员方法 + WeakPtr receiver + 绑定参数"包成一个 `void()` 回调（内部用 `std::function<void()>` 即可，不必引入 OnceCallback 本体）。核心就一行：执行前 `if (!receiver) return;`。验证：

1. 对象活着时跑回调：方法被调用、参数正确（贴输出）；
2. 对象析构后再跑一个**持有其 WeakPtr** 的回调：静默 no-op，什么都不打印、什么都不崩（贴输出）；
3. 思考题：为什么这条取消检查走的是 `get()`（同序列准），而不是 `maybe_valid()`（跨序列乐观）？用自己的话写一句（可以翻 02-4/02-5 的对照表）。

[参考答案 →](02-homework-solutions.md#hw-9-c-1)

### 9.C-2 {#hw-9-c-2}

难度 **L4** · 涉及[WeakPtr 实战（五）：与回调集成——关闭 OnceCallback 的环](../chrome/02_weak_ptr/full/02-5-weak-ptr-bind-integration.md)

复刻 Chromium `bind_internal.h` 那套编译期 weak 分派的最小版（源码出处：`base/functional/bind_internal.h` 的 `kIsWeakMethod` / `IsWeakReceiver` / `WeakCallReturnsVoid`，本卷教材 02-5 全文引用过）。三块拼图：

1. 写 `IsWeakReceiver<T>` 特征：`T` 是不是 `WeakPtr<?>` 的实例化（写一个通用的 `is_instantiation_of<ToCheck, Template>` 特征，再用它判 `WeakPtr`）。用 `static_assert` 验证：`WeakPtr<Foo>` 是、`Foo*` 不是、`int` 不是。
2. 写 `bind_weak`：模板参数带上 `kIsWeakMethod` 判定（成员方法 + receiver 是 WeakPtr 才为真）；为真时走"执行前判活、失效静默 return"的分派，且**返回类型强制 void**——用 `static_assert` 表达"weak 调用必须返回 void"。验证：把返回 `int` 的成员方法绑成 weak 回调，**贴出真实编译错误**，说清是哪条断言拦的、为什么取消语义要求 void。
3. 把"先 Unwrap 再判活"的顺序守住：在分派函数里先取出 receiver、再 `if (!target) return;`，注释里写清楚为什么这个顺序不能反。

[参考答案 →](02-homework-solutions.md#hw-9-c-2)

### 9.C-3 {#hw-9-c-3}

难度 **L5** · 涉及[OnceCallback 实战（四）：取消令牌设计](../chrome/01_once_callback/full/01-4-once-callback-cancellation-token.md)、[WeakPtr 前置知识（二）：std::atomic 与 memory_order](../chrome/02_weak_ptr/full/pre-02-weak-ptr-atomic-and-memory-order.md)

挑战题（**无锁 MPSC 任务队列**，改编自 Dmitry Vyukov 的经典侵入式 MPSC 算法——1024cores.net「Intrusive MPSC node-based queue」，按本卷取消机制强化。早期阶段 L5＝「用该阶段知识可解的最难问题」，档位口径见[练习总览](index.md)）。任务系统不许用锁，只许用原子：

1. 实现一个多生产者单消费者队列：节点持有 move-only 的 `std::move_only_function<void()>`，生产端 `push` 用 `exchange`/`acquire`/`release` 组合入队（多线程并发入队），消费端用 Vyukov 经典的单节点 `pop`（stub 哨兵节点；节点一次性、不回收，无需断链）循环取出、逐个执行。
2. 每个任务执行前查一枚 `CancelableToken`（复用 01-4 的设计：`shared_ptr` 包 `atomic<bool>`，`invalidate` 用 release、`is_valid` 用 acquire），失效任务跳过不执行。
3. 压测：4 个生产线程共投 10 万任务（每个任务对一个 `atomic<int>` 自增），1 个消费线程 drain；预先给其中 10%（每 10 个挂 1 个）任务挂上已失效的令牌——确定性做法，计数可复现。验收两条硬杠：最终计数 == 未取消任务数（90000，一个不多一个不少）；`-fsanitize=thread` 构建**零报告**，`-fsanitize=address,undefined` 构建也零报告。

这题的关键不在写出能跑的代码——在于写出**能向 TSan 自证清白的代码**。每个原子的内存序都要能说清"为什么这个序够用"：哪一处必须 acquire、哪一处 release 就够、哪一处 relaxed 就能糊口。答案文件会给出逐行注释版，但建议您先自己憋几个小时。

[参考答案 →](02-homework-solutions.md#hw-9-c-3)
