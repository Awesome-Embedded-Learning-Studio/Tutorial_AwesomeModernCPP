---
title: "卷 9 Lab 实验参考"
description: "卷 9 Lab（mini-chrome 基础库四件套）的实验参考：六个步骤加 L5 挑战的逐步解答，每步标注知识点链接，所有输出在 WSL Arch（g++ 16.1.1 / clang++ 22.1.8）真实运行得到，含 LSan 实验的实测结论与 TSan 验证。"
chapter: 9
order: 4
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
reading_time_minutes: 40
prerequisites:
  - "卷 9 Lab 题面"
related:
  - "卷 9 chrome/ 四个主题"
---

# 卷 9 Lab 实验参考

> 所有输出在 WSL Arch（g++ 16.1.1 / clang++ 22.1.8）真实运行得到。建议卡住时先看「思路」逐步对照。实验在 `/tmp/mini_base` 下做：`include/` 放四件套头文件，每步一个 `src/stepN.cpp` 驱动。头文件逐步攒出，步骤 2 起可复用前一步的成果。

## 步骤 1：工具链与函数类型侦察 {#lab-1}

**思路**：两件事——确认 C++23 的两个硬特性在，以及用 `FuncTraits` 把"函数类型"这个概念从抽象符号变成可打印的数字。

1. `static_assert(__cpp_lib_move_only_function >= 202110L)` 编译过即说明 `std::move_only_function` 可用；deducing this 的最小类编过即说明 C++23 显式对象参数可用。→ 知识点：[OnceCallback 实战（一）：动机与接口设计](../chrome/01_once_callback/full/01-1-once-callback-motivation-and-api-design.md)「环境搭建」一节
2. `FuncTraits` 主模板不定义、偏特化拆 `R(Args...)`，arity 打印 3 和 0。→ 知识点：[OnceCallback 前置知识（一）：函数类型与模板偏特化](../chrome/01_once_callback/full/pre-01-once-callback-function-type-and-specialization.md)「动手实践：撸一个 FuncTraits」一节

**验证输出**：

```text
$ g++ -std=c++23 -Wall -Wextra src/step1_env.cpp -o step1_env.out && ./step1_env.out
(无输出,退出码 0 = 两个 C++23 特性就位)

$ g++ -std=c++20 -Wall -Wextra src/step1_functraits.cpp -o step1_functraits.out && ./step1_functraits.out
FuncTraits<int(int,int,int)>::kArity = 3
FuncTraits<void()>::kArity         = 0
```

一句话：`int(int,int)` 在模板参数位置是一个货真价实的**函数类型**，描述"收两个 int、返回 int 的函数"——不是声明残骸，而是偏特化能拆包的模式匹配对象。

## 步骤 2：OnceCallback 核心骨架 {#lab-2}

**思路**：把 Lab 的四件套第一件立起来。这一版的 `include/mini_once_callback.hpp` 会一直用到步骤 4 和 L5，所以一步到位：三态 + `not_the_same_t` 约束 + deducing this 的 `run()` + 先取出再执行，外加 `bind_once`。

1. 骨架与消费顺序和 Homework 9.1-C 同一套，这里多补了 `bind_once`：C++20 capture pack expansion（`...bound = std::forward<BoundArgs>(args)`）把绑定参数逐一分发进闭包，lambda 内 `std::move(bound)...` 以右值喂给 `std::invoke`（mutable lambda 里捕获变量是左值，必须 `std::move` 才能触发移动）。→ 知识点：[once_callback 设计指南（二）：逐步实现](../chrome/01_once_callback/hands_on/02-once-callback-implementation.md)「第二步：参数绑定」一节、[OnceCallback 前置知识（三）：Lambda 高级特性](../chrome/01_once_callback/full/pre-03-once-callback-lambda-advanced.md)「为什么用 std::move 而不是 std::forward」一节
2. 四个场景把四条编译期分支都压一遍：非 void 与 void 的 `if constexpr`、move-only 捕获（证明底层真是 `move_only_function`）、绑定参数与运行时参数的顺序（绑定的在前、运行时的在后）。→ 知识点：[once_callback 设计指南（三）：测试策略与性能对比](../chrome/01_once_callback/hands_on/03-once-callback-testing.md)「按『不变量』切测试」一节

**验证输出**：

```text
$ g++ -std=c++23 -Wall -Wextra -Wpedantic -Iinclude src/step2.cpp -o step2.out && ./step2.out
non-void  : 7
void      : called=1
move-only : 42
bind_once : 60
```

`bind_once : 60` = 预绑 10、20 加运行时 30，绑定参数在前、运行时参数在后这条顺序在这里兑现。

## 步骤 3：取消令牌 {#lab-3}

**思路**：`CancelableToken` 只有 18 行，但三个场景把"取消的三分支"全部走通：有效正常跑、void 取消静默、非 void 取消抛异常。

1. `shared_ptr` 包 `struct Flag { atomic<bool> valid; }`：拷贝共享同一份状态、`atomic` 保多线程安全，`invalidate` 用 release、`is_valid` 用 acquire 配对。→ 知识点：[OnceCallback 实战（四）：取消令牌设计](../chrome/01_once_callback/full/01-4-once-callback-cancellation-token.md)「为什么非得套个嵌套结构体 Flag」「acquire/release 这对配子」两节
2. `is_cancelled()` 查两道关：先看 `status_ != kValid`（空/已消费都算取消），再看令牌失效。→ 知识点：同上「is_cancelled() 看的是两个地方」一节
3. `impl_run` 执行前查令牌，命中就"消费但不执行"：void 静默 return（调用方不期待返回值，透明跳过）；非 void 抛 `std::bad_function_call`（调用方在等一个值，给不出就响亮地失败，绝不返回一个假值骗它）。→ 知识点：同上「void 和非 void 回调,取消时为啥不一样」一节

**验证输出**：

```text
$ g++ -std=c++23 -Wall -Wextra -Wpedantic -Iinclude src/step3.cpp -o step3.out && ./step3.out
valid : is_cancelled=0 executed=1
void  : is_cancelled=1 executed=0 (no throw)
int   : caught std::bad_function_call
```

说清两个问题：取消的 void 回调**执行了吗**？没有——`executed=0`，但它确实"消费"了回调（状态已翻转）；取消的非 void 回调**返回了什么**？什么都没返回，它抛了 `std::bad_function_call`，因为取消态下不存在一个有意义的返回值。

## 步骤 4：WeakPtr 三层 + 回调集成 {#lab-4}

**思路**：把 01-4 手搓令牌的工业正解做出来。`include/mini_weak_ptr.hpp` 是 Lab 最厚的头文件：Flag（`RefCountedThreadSafe` + `AtomicFlag`）→ WeakReference（`scoped_refptr` 壳）→ WeakPtr（+允许悬垂的 `T*`）→ WeakPtrFactory 铸币。

1. `get()` 守门链一次 acquire-load 判活；factory 析构自动 `Invalidate`——这就是"最后成员"惯用法的根。→ 知识点：[WeakPtr 实战（二）：核心骨架与控制块](../chrome/02_weak_ptr/full/02-2-weak-ptr-core-skeleton-and-control-block.md)「get() 的守门链」一节、[WeakPtr 实战（三）：WeakPtrFactory 与「最后成员」惯用法](../chrome/02_weak_ptr/full/02-3-weak-ptr-factory-and-last-member.md)「两条叠起来:为什么 factory 非得放最后」一节
2. `bind_weak_once` 把成员方法 + WeakPtr 绑成 `OnceCallback<void()>`，取消点一行 `if (!receiver) return;`——与工业版 `InvokeHelper<true>::MakeItSo` 同构。→ 知识点：[WeakPtr 实战（五）：与回调集成——关闭 OnceCallback 的环](../chrome/02_weak_ptr/full/02-5-weak-ptr-bind-integration.md)「简版实现:在 01 的 OnceCallback 上接 WeakPtr」一节
3. 完整时间线 + ASan：活着跑一次 → 出作用域 → 再跑静默 no-op；`-fsanitize=address,undefined` 构建零报告，证明这条线上没有悬垂解引用。→ 知识点：同上「闭环:01-4 手搓令牌 vs 工业 WeakPtr」一节

**验证输出**：

```text
$ g++ -std=c++23 -Wall -Wextra -Wpedantic -Iinclude src/step4.cpp -o step4.out && ./step4.out
got 42
(done: only one "got" line above)

$ g++ -std=c++23 -Wall -Wextra -fsanitize=address,undefined -g -O1 -Iinclude src/step4.cpp -o step4_asan.out && ./step4_asan.out
got 42
(done: only one "got" line above)   ← 零报告
```

因果链一句话：factory 最后声明 → 最先析构 → 析构时自动失效所有 WeakPtr → 回调里的判活短路 → 悬垂解引用从根上不存在。

## 步骤 5：flat_map 骨架与诚实契约 {#lab-5}

**思路**：第三件套 `include/mini_flat_map.hpp`：`flat_tree` 四参数 + `GetFirst` 提取器 + `sort_and_unique` + `lower_bound` 查找 + `sorted_unique_t` 标签。诚实契约用 debug/release 两次运行看得最清楚。

1. `flat_tree<Key, GetKeyFromValue, KeyCompare, Container>` 是通用底座，`flat_map` 填 `GetFirst`、`flat_set` 填 `std::identity`——同一份代码两副面孔。→ 知识点：[flat_map 实战（二）：flat_tree 核心骨架](../chrome/03_flat_map/full/03-2-flat-map-flattree-skeleton.md)「flat_tree 模板签名」「key 提取器」两节
2. 有序不变量两处守：构造期 `sort_and_unique`（stable_sort + unique + erase），插入期 `lower_bound` + 拒绝重复。→ 知识点：同上「有序不变量:每次 mutation 后保持有序 + 唯一」一节
3. `sorted_unique_t` 空标签在重载决议期分流：普通构造排序、sorted_unique 构造跳过排序只做 debug `assert`；乱序数据塞 sorted_unique 在 debug 下 abort（退出码 134），`-DNDEBUG` 下照单全收——这就是诚实契约：容器给 O(N) 构造，调用方给"数据确实有序"的承诺。→ 知识点：[flat_map 前置知识（四）：tag dispatch 与 sorted_unique_t](../chrome/03_flat_map/full/pre-04-flat-map-tag-dispatch-and-sorted-unique.md)「DCHECK(is_sorted_and_unique):debug 抓撒谎的人」「零成本:release 完全不付费」两节

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra -Iinclude src/step5.cpp -o step5_debug.out && ./step5_debug.out
step5_debug.out: include/mini_flat_map.hpp:38: ...: Assertion `is_sorted_and_unique()' failed.
Aborted
debug run exit=134

$ g++ -std=c++20 -Wall -Wextra -DNDEBUG -Iinclude src/step5.cpp -o step5_rel.out && ./step5_rel.out
normal: size=3 first_key=1
sorted_unique(honest): size=4 first_key=1
sorted_unique(lying): size=3
```

## 步骤 6：NoDestructor + 三把 sanitizer 验货 {#lab-6}

**思路**：第四件套 `include/mini_no_destructor.hpp` 落地后，用 sanitizer 给全部家当验货。这步最有含金量的是 LSan 实验——**实测结果曾与旧版教材 04-4 的描述不一致**（这一发现推动了教材修订，现行版已按保守字节扫描的口径改写），我们如实记录当初的实测。

1. `NoDestructor` 三件套：placement new 构造、`= default` 析构跳过 `~T()`、两条 static_assert 把关。`Noisy` 只打印 `Noisy()`，`~Noisy()` 永不出现。→ 知识点：[NoDestructor 实战（二）：核心实现](../chrome/04_no_destructor/full/04-2-no-destructor-core-impl.md)「不析构:=default 这个动作藏着玄机」「static_assert 把关」两节
2. 配置表装进函数局部静态的 NoDestructor：查两回构造计数仍为 1，`port=8080` 正常命中。→ 知识点：[NoDestructor 实战（三）：何时用、何时不用](../chrome/04_no_destructor/full/04-3-no-destructor-when-to-use.md)「该用的场景:函数局部静态 + 非平凡析构 T」一节
3. LSan 实验的实测结论（**当初与旧版教材不一致、如实报告，现行教材已按此修订**）：真泄漏（`new int[100]` 后指针置空）被 g++ 16 的 ASan 内置 LSan 点名（400 字节），NoDestructor 的 vector **没有**被误报；clang 22 独立 LSan 同样只报真泄漏；clang ASan 集成 LSan（`-fsanitize=address` 内置）也只报真泄漏、放过 NoDestructor——注意 Linux 下 clang ASan **不会**打印 `checking for leaks` 那行（那是 macOS 的行为），别拿它当启动标志。旧版教材 04-4 引用的 crbug/40562930 场景（LSan 把 `char storage_` 当原始字节、可达链断开）在本工具链上**不复现**——因为现代 LSan 做的是**保守字节扫描**：它扫静态区里每一个指针形状的字节，`storage_` 里 vector 三指针的字节值被认了出来，可达链没断。这是"工具行为随版本变化"的活样本，也提醒我们：文档的每条结论都值得亲手复现一遍——旧版 04-4 的机制表述在现代工具链不复现（现行版已修订），读任何版本的同学都以本机实测为准。两个实验坑也记下：本工具链 LSan 的 `detect_leaks` 默认就是 1，不设 `ASAN_OPTIONS` 照样报真泄漏（命令里写上无害且自解释）；真正必须的是 `-O0`——`-O1`/`-O2` 下编译器可能把对照组分配直接优化掉，泄漏实验就白做了。→ 知识点：[NoDestructor 实战（四）：LSan 泄漏权衡与 reachability hack](../chrome/04_no_destructor/full/04-4-no-destructor-lsan-and-leak.md)「LeakSanitizer 怎么工作」一节（可达性分析 + 本节实测修正）
4. TSan 压 magic statics：16 线程首调，`ctor_count=1` 零报告。→ 知识点：[NoDestructor 前置知识（零）：静态存储期、初始化与析构](../chrome/04_no_destructor/full/pre-00-static-storage-and-init.md)「magic statics:C++11 的线程安全保证」一节

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra -Iinclude src/step6.cpp -o step6.out && ./step6.out
Noisy()
value=42
(exiting: ~Noisy was never printed)

$ g++ -std=c++20 -Wall -Wextra -Iinclude src/step6_config.cpp -o step6_config.out && ./step6_config.out
ctor=0
ctor=1 port=8080

$ g++ -std=c++20 -Wall -Wextra -fsanitize=address -g -O0 -Iinclude src/step6_lsan.cpp -o step6_lsan_gcc.out
$ ASAN_OPTIONS=detect_leaks=1 ./step6_lsan_gcc.out
sum=6
=================================================================
==293==ERROR: LeakSanitizer: detected memory leaks

Direct leak of 400 byte(s) in 1 object(s) allocated from:
    #0 0x727f20d2d431 in operator new[](unsigned long) (/usr/lib/libasan.so.8+0x12d431) (BuildId: 7f2845989b820f536270e19ec47df085ae89a675)
    #1 0x55ca903165a4 in main src/step6_lsan.cpp:15
SUMMARY: AddressSanitizer: 400 byte(s) leaked in 1 allocation(s).
    ← 只报了真泄漏;NoDestructor 的 vector 不在报告里

$ ./step6_lsan_gcc.out    ← 不写 ASAN_OPTIONS 直接跑,报告一模一样:本工具链 detect_leaks 默认就是 1
sum=6
=================================================================
==296==ERROR: LeakSanitizer: detected memory leaks
Direct leak of 400 byte(s) in 1 object(s) allocated from:
SUMMARY: AddressSanitizer: 400 byte(s) leaked in 1 allocation(s).

$ g++ -std=c++20 -Wall -Wextra -fsanitize=address -g -O1 -Iinclude src/step6_lsan.cpp -o step6_lsan_gcc_o1.out && ./step6_lsan_gcc_o1.out
sum=6    ← -O1 下对照组 new int[100] 被整段优化掉,LSan 无泄漏可报:泄漏实验必须 -O0

$ clang++ -std=c++20 -Wall -Wextra -fsanitize=leak -g -O0 -Iinclude src/step6_lsan_only.cpp -o step6_lsan_only_clang.out
$ ./step6_lsan_only_clang.out
sum=6    ← 独立 LSan:只有 NoDestructor 时同样零报告

$ clang++ -std=c++20 -Wall -Wextra -fsanitize=address -g -O0 -Iinclude src/step6_lsan.cpp -o step6_lsan_clang_asan.out
$ ./step6_lsan_clang_asan.out
sum=6
=================================================================
==319==ERROR: LeakSanitizer: detected memory leaks
Direct leak of 400 byte(s) in 1 object(s) allocated from:
SUMMARY: AddressSanitizer: 400 byte(s) leaked in 1 allocation(s).
    ← clang ASan 集成 LSan 同样只报真泄漏;整段输出里没有 "checking for leaks"——Linux 下 clang 不打印它(那是 macOS 的行为)

$ g++ -std=c++20 -Wall -Wextra -fsanitize=thread -O1 -g src/step6_tsan.cpp -o step6_tsan.out && ./step6_tsan.out
ctor_count=1    ← TSan 零报告
```

`src/step6_lsan.cpp` 全文（`step6_lsan_only.cpp` 就是它去掉三行真泄漏对照组）：

```cpp
// 真泄漏对照组 + NoDestructor 版配置表放进同一个程序
#include "mini_no_destructor.hpp"
#include <cstdio>
#include <vector>

using tamcpp::chrome::NoDestructor;

// NoDestructor 版配置表(函数局部静态):LSan 不应误报它
const std::vector<int>& GetConfig() {
    static const NoDestructor<std::vector<int>> config(std::vector<int>{1, 2, 3});
    return *config;
}

int main() {
    int* p = new int[100];   // ← 真泄漏对照组(指针随后置空)
    p[0] = 1;
    p = nullptr;

    int sum = 0;
    for (int v : GetConfig()) sum += v;
    std::printf("sum=%d\n", sum);
    return 0;
}
```

## 附加挑战（L5）：侵入式零分配 MPSC 队列 {#lab-l5}

**思路**：节点**内嵌**进任务对象，`push`/`pop` 只搬节点不碰任务，per-post 零堆分配；队列仍是 Vyukov stub 哨兵那套（节点一次性、不回收，无需断链），任务执行前查取消令牌。基准拿"互斥锁 + deque"当陪练。

1. 零分配的落点：所有任务在压测前一次性 `make_unique` 造好，`push` 路径上只有原子操作——没有 `new`、没有 `malloc`。→ 知识点：[OnceCallback 实战（四）](../chrome/01_once_callback/full/01-4-once-callback-cancellation-token.md)（令牌）与本解答 9.C-3 的队列算法
2. 内存序的账（每个都有一句话）：`next.store(relaxed)` 未发布不传信号；`head.exchange(acq_rel)` 同时读旧顶/发新顶；`prev->next.store(release)` 把载荷发布给消费端；`pop` 的 acquire 读接住 release，head 比对识别在途 push。→ 知识点：[WeakPtr 前置知识（二）：std::atomic 与 memory_order](../chrome/02_weak_ptr/full/pre-02-weak-ptr-atomic-and-memory-order.md)（六种序与 acquire/release 配对）
3. 基准结论（本机一次代表性运行）：无锁 MPSC 5 ms vs 互斥队列 7 ms（-O2）；TSan 构建下差距拉大到 38 vs 165 ms。无锁换的是"短临界区下的常数因子"，付的是"代码正确性要拿 TSan 自证"——4 生产 10 万任务、中途取消 10%，计数 90000 一分不差，TSan 零报告。→ 知识点：[WeakPtr 设计指南（三）：测试策略与性能对比](../chrome/02_weak_ptr/hands_on/03-weak-ptr-testing.md)（不变量驱动 + 数据说话）

**验证输出**：

```text
$ g++ -std=c++23 -Wall -Wextra -O2 -Iinclude src/step_l5.cpp -o step_l5.out && ./step_l5.out
mpsc  : executed=90000  5 ms
mutex : executed=90000  7 ms

$ g++ -std=c++23 -Wall -Wextra -fsanitize=thread -O1 -g -Iinclude src/step_l5.cpp -o step_l5_tsan.out && ./step_l5_tsan.out
mpsc  : executed=90000  38 ms
mutex : executed=90000  165 ms    ← 全程无 TSan 报告
```

核心代码（节点内嵌版队列；任务构造与压测同 9.C-3，只是节点换成了任务对象自身）：

```cpp
// ---- 侵入式节点:任务对象自带原子 next(每次投递零堆分配)----
struct Task {
    std::atomic<Task*> next{nullptr};
    std::move_only_function<void()> fn;
    std::shared_ptr<CancelableToken> token;
};

// ---- Vyukov 经典侵入式 MPSC(stub 哨兵;节点一次性、不回收,无需断链)----
class IntrusiveMpsc {
public:
    IntrusiveMpsc() : head_(&stub_), tail_(&stub_) {
        stub_.next.store(nullptr, std::memory_order_relaxed);
    }

    // 多生产者并发:exchange 摘旧栈顶 + release 接链
    void push(Task* n) {
        n->next.store(nullptr, std::memory_order_relaxed);
        Task* prev = head_.exchange(n, std::memory_order_acq_rel);
        prev->next.store(n, std::memory_order_release);
    }

    // 仅消费线程:nullptr = 空,或 push 在途,稍后重试
    Task* pop() {
        Task* t = tail_;
        Task* next = t->next.load(std::memory_order_acquire);
        if (t == &stub_) {
            if (next == nullptr) return nullptr;
            tail_ = next;
            t = next;
            next = t->next.load(std::memory_order_acquire);
        }
        if (next != nullptr) {
            tail_ = next;
            return t;
        }
        Task* h = head_.load(std::memory_order_acquire);
        if (t != h) return nullptr;   // push 在途
        push(&stub_);
        next = t->next.load(std::memory_order_acquire);
        if (next != nullptr) {
            tail_ = next;
            return t;
        }
        return nullptr;
    }

private:
    std::atomic<Task*> head_;
    Task* tail_ = nullptr;   // 消费端私有
    Task stub_;
};
```

与 Homework 9.C-3 的堆节点版相比，这个版本把 `std::atomic<Task*> next` 直接做进任务结构体：`push` 拿到的指针就是任务对象本身，队列只搬指针、绝不碰任务的存储，所以投递路径上的堆分配次数是 0。基准程序里两个队列各跑一遍同样的 10 万任务负载，`executed` 对账 90000 是取消 10% 后的精确值。

## 共享头文件四件套（完整源码）

以下四个头文件是 Lab 与 Project 共用的完整参考实现，全部在 WSL（g++ 16.1.1）下以 `-Wall -Wextra -Wpedantic` 编译通过、并在本参考的每一步真跑过。Project 的 `include/` 直接复用这四份，不再重复贴出。

### include/mini_once_callback.hpp（C++23）

```cpp
#pragma once
#include <atomic>
#include <cassert>
#include <concepts>
#include <cstdint>
#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

namespace tamcpp::chrome {

// ---- 取消令牌(01-4 设计):shared_ptr 包 atomic<bool>,release/acquire ----
class CancelableToken {
    struct Flag {
        std::atomic<bool> valid{true};
    };
    std::shared_ptr<Flag> flag_;
public:
    CancelableToken() : flag_(std::make_shared<Flag>()) {}
    void invalidate() { flag_->valid.store(false, std::memory_order_release); }
    bool is_valid() const { return flag_->valid.load(std::memory_order_acquire); }
};

// ---- OnceCallback 核心骨架 ----
template <typename F, typename T>
concept not_the_same_t = !std::is_same_v<std::decay_t<F>, T>;

template <typename Signature>
class OnceCallback;

template <typename R, typename... Args>
class OnceCallback<R(Args...)> {
    enum class Status : std::uint8_t { kEmpty, kValid, kConsumed };

public:
    using FuncSig = R(Args...);

    OnceCallback() = default;

    template <typename F>
        requires not_the_same_t<F, OnceCallback>
    explicit OnceCallback(F&& f) : status_(Status::kValid), func_(std::move(f)) {}

    OnceCallback(const OnceCallback&) = delete;
    OnceCallback& operator=(const OnceCallback&) = delete;

    OnceCallback(OnceCallback&& other) noexcept
        : status_(other.status_), func_(std::move(other.func_)), token_(std::move(other.token_)) {
        other.status_ = Status::kEmpty;
    }
    OnceCallback& operator=(OnceCallback&& other) noexcept {
        if (this != &other) {
            status_ = other.status_;
            func_ = std::move(other.func_);
            token_ = std::move(other.token_);
            other.status_ = Status::kEmpty;
        }
        return *this;
    }

    template <typename Self>
    auto run(this Self&& self, Args&&... args) -> R {
        static_assert(!std::is_lvalue_reference_v<Self>,
                      "OnceCallback::run() must be called on an rvalue. "
                      "Use std::move(cb).run(...) instead.");
        return std::forward<Self>(self).impl_run(std::forward<Args>(args)...);
    }

    [[nodiscard]] bool is_null() const noexcept { return status_ == Status::kEmpty; }
    [[nodiscard]] bool is_cancelled() const noexcept {
        if (status_ != Status::kValid) return true;
        if (token_ && !token_->is_valid()) return true;
        return false;
    }
    [[nodiscard]] bool maybe_valid() const noexcept { return !is_cancelled(); }
    explicit operator bool() const noexcept { return !is_null() && !is_cancelled(); }

    void set_token(std::shared_ptr<CancelableToken> token) { token_ = std::move(token); }

private:
    R impl_run(Args... args) {
        assert(status_ == Status::kValid);
        // 取消检查在执行前:消费但不执行
        if (token_ && !token_->is_valid()) {
            status_ = Status::kConsumed;
            func_ = nullptr;
            if constexpr (std::is_void_v<R>) {
                return;
            } else {
                throw std::bad_function_call{};
            }
        }
        // 消费:先取出、再置空、状态先行,最后执行(异常安全顺序)
        auto f = std::move(func_);
        func_ = nullptr;
        status_ = Status::kConsumed;
        if constexpr (std::is_void_v<R>) {
            f(std::forward<Args>(args)...);
        } else {
            return f(std::forward<Args>(args)...);
        }
    }

    Status status_ = Status::kEmpty;
    std::move_only_function<FuncSig> func_;
    std::shared_ptr<CancelableToken> token_;
};

// ---- bind_once:C++20 capture pack expansion + std::invoke ----
template <typename Signature, typename F, typename... BoundArgs>
auto bind_once(F&& functor, BoundArgs&&... args) {
    return OnceCallback<Signature>(
        [f = std::forward<F>(functor),
         ...bound = std::forward<BoundArgs>(args)]
        (auto&&... call_args) mutable -> decltype(auto) {
            return std::invoke(
                std::move(f),
                std::move(bound)...,
                std::forward<decltype(call_args)>(call_args)...);
        });
}

}  // namespace tamcpp::chrome
```

### include/mini_weak_ptr.hpp（C++20）

```cpp
#pragma once
#include <atomic>
#include <cassert>
#include <concepts>
#include <cstdint>
#include <type_traits>

namespace tamcpp::chrome {

namespace internal {

class RefCountedThreadSafe {
public:
    void add_ref() const noexcept { ref_count_.fetch_add(1, std::memory_order_relaxed); }
    bool release() const noexcept {
        return ref_count_.fetch_sub(1, std::memory_order_acq_rel) == 1;
    }
    bool has_one_ref() const noexcept {
        return ref_count_.load(std::memory_order_acquire) == 1;
    }
protected:
    RefCountedThreadSafe() = default;
    ~RefCountedThreadSafe() = default;
private:
    mutable std::atomic<int> ref_count_{0};
};

class AtomicFlag {
public:
    void Set() noexcept { flag_.store(1, std::memory_order_release); }
    bool IsSet() const noexcept { return flag_.load(std::memory_order_acquire) != 0; }
private:
    std::atomic<uint_fast8_t> flag_{0};
};

template <typename T>
class scoped_refptr {
public:
    scoped_refptr() noexcept = default;
    explicit scoped_refptr(T* p) noexcept : ptr_(p) { if (ptr_) ptr_->add_ref(); }
    scoped_refptr(const scoped_refptr& o) noexcept : ptr_(o.ptr_) { if (ptr_) ptr_->add_ref(); }
    scoped_refptr(scoped_refptr&& o) noexcept : ptr_(o.ptr_) { o.ptr_ = nullptr; }
    ~scoped_refptr() { if (ptr_ && ptr_->release()) delete ptr_; }
    scoped_refptr& operator=(scoped_refptr r) noexcept {
        T* t = ptr_; ptr_ = r.ptr_; r.ptr_ = t;
        return *this;
    }
    T* get() const noexcept { return ptr_; }
    T* operator->() const noexcept { return ptr_; }
    explicit operator bool() const noexcept { return ptr_ != nullptr; }
    scoped_refptr& operator=(std::nullptr_t) noexcept {
        if (ptr_ && ptr_->release()) delete ptr_;
        ptr_ = nullptr;
        return *this;
    }
private:
    T* ptr_ = nullptr;
};

class Flag : public RefCountedThreadSafe {
public:
    Flag() = default;
    void Invalidate() noexcept { invalidated_.Set(); }
    bool IsValid() const noexcept { return !invalidated_.IsSet(); }
    bool MaybeValid() const noexcept { return !invalidated_.IsSet(); }
private:
    template <typename> friend class scoped_refptr;
    ~Flag() = default;
    AtomicFlag invalidated_;
};

class WeakReference {
public:
    WeakReference() = default;
    explicit WeakReference(const scoped_refptr<Flag>& flag) : flag_(flag) {}
    bool IsValid() const noexcept { return flag_ && flag_->IsValid(); }
    bool MaybeValid() const noexcept { return flag_ && flag_->MaybeValid(); }
    void Reset() noexcept { flag_ = nullptr; }
private:
    scoped_refptr<Flag> flag_;
};

class WeakReferenceOwner {
public:
    WeakReferenceOwner() : flag_(new Flag()) {}
    ~WeakReferenceOwner() { if (flag_) flag_->Invalidate(); }
    WeakReference GetRef() const { return WeakReference(flag_); }
    void Invalidate() {
        flag_->Invalidate();
        flag_ = scoped_refptr<Flag>(new Flag());
    }
    bool HasRefs() const { return !flag_->has_one_ref(); }
private:
    scoped_refptr<Flag> flag_;
};

}  // namespace internal

template <typename T> class WeakPtrFactory;

template <typename T>
class WeakPtr {
public:
    WeakPtr() = default;
    WeakPtr(std::nullptr_t) noexcept {}

    template <typename U>
        requires(std::convertible_to<U*, T*>)
    WeakPtr(const WeakPtr<U>& o) noexcept : ref_(o.ref_), ptr_(o.ptr_) {}
    template <typename U>
        requires(std::convertible_to<U*, T*>)
    WeakPtr(WeakPtr<U>&& o) noexcept : ref_(std::move(o.ref_)), ptr_(o.ptr_) {}

    T* get() const noexcept { return ref_.IsValid() ? ptr_ : nullptr; }
    T& operator*() const { assert(ref_.IsValid()); return *ptr_; }
    T* operator->() const { assert(ref_.IsValid()); return ptr_; }
    explicit operator bool() const noexcept { return get() != nullptr; }
    void reset() noexcept { ref_.Reset(); ptr_ = nullptr; }
    bool maybe_valid() const noexcept { return ref_.MaybeValid(); }
    bool was_invalidated() const noexcept { return ptr_ && !ref_.IsValid(); }

private:
    template <typename U> friend class WeakPtr;
    friend class WeakPtrFactory<T>;
    WeakPtr(internal::WeakReference&& ref, T* ptr) noexcept
        : ref_(std::move(ref)), ptr_(ptr) {
        assert(ptr);
    }
    internal::WeakReference ref_;
    T* ptr_ = nullptr;
};

template <typename T>
class WeakPtrFactory {
public:
    WeakPtrFactory() = delete;
    explicit WeakPtrFactory(T* ptr) : ptr_(reinterpret_cast<uintptr_t>(ptr)) { assert(ptr); }
    WeakPtrFactory(const WeakPtrFactory&) = delete;
    WeakPtrFactory& operator=(const WeakPtrFactory&) = delete;

    WeakPtr<const T> get_weak_ptr() const {
        return WeakPtr<const T>(owner_.GetRef(), reinterpret_cast<const T*>(ptr_));
    }
    WeakPtr<T> get_weak_ptr() requires(!std::is_const_v<T>) {
        return WeakPtr<T>(owner_.GetRef(), reinterpret_cast<T*>(ptr_));
    }
    void invalidate_weak_ptrs() { assert(ptr_); owner_.Invalidate(); }
    bool has_weak_ptrs() const { return ptr_ != 0 && owner_.HasRefs(); }

private:
    internal::WeakReferenceOwner owner_;
    uintptr_t ptr_;
};

}  // namespace tamcpp::chrome
```

### include/mini_flat_map.hpp（C++20）

```cpp
#pragma once
#include <algorithm>
#include <cassert>
#include <functional>
#include <utility>
#include <vector>

namespace tamcpp::chrome {

struct sorted_unique_t {};
inline constexpr sorted_unique_t sorted_unique{};

namespace internal {

template <class Key, class GetKeyFromValue, class KeyCompare, class Container>
class flat_tree {
public:
    using value_type = typename Container::value_type;
    using iterator = typename Container::iterator;
    using const_iterator = typename Container::const_iterator;
    using container_type = Container;

    flat_tree() = default;

    flat_tree(Container data, KeyCompare comp = KeyCompare())
        : body_(std::move(data)), comp_(comp) {
        sort_and_unique();
    }

    template <class InputIt>
    flat_tree(InputIt first, InputIt last, KeyCompare comp = KeyCompare())
        : body_(first, last), comp_(comp) {
        sort_and_unique();
    }

    flat_tree(sorted_unique_t, Container data, KeyCompare comp = KeyCompare())
        : body_(std::move(data)), comp_(comp) {
        assert(is_sorted_and_unique());   // 诚实契约:debug 校验,release 信任
    }

    const_iterator find(const Key& key) const {
        auto it = std::lower_bound(
            body_.begin(), body_.end(), key,
            [&](const value_type& v, const Key& k) { return comp_(GetKeyFromValue{}(v), k); });
        if (it != body_.end() && !comp_(key, GetKeyFromValue{}(*it))) return it;
        return body_.end();
    }

    bool contains(const Key& key) const { return find(key) != body_.end(); }

    std::pair<iterator, bool> insert(value_type v) {
        const Key& key = GetKeyFromValue{}(v);
        auto it = std::lower_bound(body_.begin(), body_.end(), key,
            [&](const value_type& e, const Key& k) { return comp_(GetKeyFromValue{}(e), k); });
        if (it != body_.end() && !comp_(key, GetKeyFromValue{}(*it))) return {it, false};
        return {body_.emplace(it, std::move(v)), true};
    }

    std::size_t size() const { return body_.size(); }
    bool empty() const { return body_.empty(); }
    iterator begin() { return body_.begin(); }
    iterator end() { return body_.end(); }
    const_iterator begin() const { return body_.begin(); }
    const_iterator end() const { return body_.end(); }

    void sort_and_unique() {
        GetKeyFromValue ext;
        std::stable_sort(body_.begin(), body_.end(),
            [&](const value_type& a, const value_type& b) { return comp_(ext(a), ext(b)); });
        body_.erase(std::unique(body_.begin(), body_.end(),
            [&](const value_type& a, const value_type& b) {
                auto ka = ext(a), kb = ext(b);
                return !comp_(ka, kb) && !comp_(kb, ka);
            }), body_.end());
    }

    bool is_sorted_and_unique() const {
        GetKeyFromValue ext;
        for (auto it = body_.begin(); it != body_.end(); ++it) {
            auto nxt = it;
            ++nxt;
            if (nxt == body_.end()) break;
            if (!comp_(ext(*it), ext(*nxt))) return false;
        }
        return true;
    }

protected:
    Container body_;
    [[no_unique_address]] KeyCompare comp_;
};

}  // namespace internal

struct GetFirst {
    template <class K, class V>
    constexpr const K& operator()(const std::pair<K, V>& p) const { return p.first; }
};

template <class Key, class Mapped, class Compare = std::less<>,
          class Container = std::vector<std::pair<Key, Mapped>>>
class flat_map : public internal::flat_tree<Key, GetFirst, Compare, Container> {
    using base = internal::flat_tree<Key, GetFirst, Compare, Container>;
public:
    using mapped_type = Mapped;
    using base::base;

    mapped_type& operator[](const Key& key) {
        auto it = std::lower_bound(this->body_.begin(), this->body_.end(), key,
            [this](const typename base::value_type& v, const Key& k) {
                return this->comp_(GetFirst{}(v), k);
            });
        if (it == this->body_.end() || this->comp_(key, GetFirst{}(*it)))
            it = this->body_.emplace(it, std::piecewise_construct,
                                     std::forward_as_tuple(key),
                                     std::forward_as_tuple());
        return it->second;
    }

    mapped_type& at(const Key& key) {
        auto it = this->find(key);
        assert(it != this->body_.end());   // 教学版 assert;Chromium 用 CHECK
        return it->second;
    }

    const mapped_type& at(const Key& key) const {
        auto it = this->find(key);
        assert(it != this->body_.end());
        return it->second;
    }

    template <class M>
    std::pair<typename base::iterator, bool> insert_or_assign(const Key& key, M&& obj) {
        auto it = std::lower_bound(this->body_.begin(), this->body_.end(), key,
            [this](const typename base::value_type& v, const Key& k) {
                return this->comp_(GetFirst{}(v), k);
            });
        if (it != this->body_.end() && !this->comp_(key, GetFirst{}(*it))) {
            it->second = std::forward<M>(obj);
            return {it, false};
        }
        return {this->body_.emplace(it, key, std::forward<M>(obj)), true};
    }
};

template <class Key, class Compare = std::less<>, class Container = std::vector<Key>>
using flat_set = internal::flat_tree<Key, std::identity, Compare, Container>;

}  // namespace tamcpp::chrome
```

### include/mini_no_destructor.hpp（C++20）

```cpp
#pragma once
#include <new>
#include <type_traits>
#include <utility>

namespace tamcpp::chrome {

template <typename T>
class NoDestructor {
public:
    template <typename... Args>
    explicit NoDestructor(Args&&... args) {
        new (storage_) T(std::forward<Args>(args)...);   // placement new
    }
    explicit NoDestructor(const T& x) { new (storage_) T(x); }
    explicit NoDestructor(T&& x) { new (storage_) T(std::move(x)); }

    NoDestructor(const NoDestructor&) = delete;
    NoDestructor& operator=(const NoDestructor&) = delete;
    ~NoDestructor() = default;   // 只析构 char 成员,不调 ~T()

    const T& operator*() const { return *get(); }
    T& operator*() { return *get(); }
    const T* operator->() const { return get(); }
    T* operator->() { return get(); }
    const T* get() const { return reinterpret_cast<const T*>(storage_); }
    T* get() { return reinterpret_cast<T*>(storage_); }

private:
    static_assert(!(std::is_trivially_constructible_v<T> &&
                    std::is_trivially_destructible_v<T>),
                  "T trivially ctble+dtble: use constinit T directly");
    static_assert(!std::is_trivially_destructible_v<T>,
                  "T trivially destructible: use plain function-local static T");

    alignas(T) char storage_[sizeof(T)];
};

}  // namespace tamcpp::chrome
```
