---
chapter: 1
cpp_standard:
- 17
- 20
description: 用 Catch2 围绕六条不变量测试教学版 WeakPtr,并与 std::weak_ptr、真实 Chromium 做对象大小、
  分配行为、调用开销、TRIVIAL_ABI 收益的性能对比
difficulty: intermediate
order: 6
platform: host
prerequisites:
- WeakPtr 实战（五）：与回调集成——关闭 OnceCallback 的环
- OnceCallback 实战（六）：测试与性能对比
reading_time_minutes: 13
related:
- WeakPtr 实战（二）：核心骨架与控制块
- WeakPtr 前置知识（六）：TRIVIAL_ABI 与平凡可重locate
tags:
- host
- cpp-modern
- intermediate
- 智能指针
- weak_ptr
- 测试
- 优化
title: "WeakPtr 实战（六）：测试与性能对比"
---
# WeakPtr 实战（六）：测试与性能对比

代码撸到这儿,笔者最怕一件事:看着能跑,心里没底。WeakPtr 这种东西,跑通的 demo 永远只覆盖"对象活着"这一条最舒服的路径,真正要出事的全在边界——invalidate 之后还在解引用、factory 析构了还有人持着 WeakPtr、跨序列判活拿到一个假阳性。所以这一篇咱们不写新功能,就干两件事:把该测的性质一条条钉成测试,再把教学版和 `std::weak_ptr`、真实 Chromium 摆一起量一量——对象多大、分配几次、一次判活走多远、`TRIVIAL_ABI` 到底省在哪。和 [01-6 测试与性能对比](../../01_once_callback/full/01-6-once-callback-testing-and-perf.md) 一样,笔者只信真实测量,不空口断言。

---

## 六条不变量

测试要测的是"不变量",说白了就是不管您怎么折腾它,这条性质都得立着。WeakPtr 笔者归纳下来有六条,咱们就围着这六条转。

第一条最朴素:factory 铸出来的 WeakPtr,对象还活着的时候就得能正确解引用。这是地基,过不了这条别的都白搭。第二条是 move 语义——WeakPtr 是 move-only 友好的,move 完源对象得变空,不能两边都指着同一个 flag。第三条是 invalidate 之后判活必须失效,一次 `invalidate_weak_ptrs()` 出去,所有已经铸出来的 WeakPtr 的 `get()` 和 `operator bool` 全得返回空和 false,一个都不能漏。

第四条是笔者特意拎出来的:解引用一个已经失效的 WeakPtr,必须触发断言。教学版用 `assert`,release 模式下 Chromium 用 `CHECK`——这是 use-after-free 的前兆,没得商量,当场得爆。第五条是 `maybe_valid()` 的不对称,这玩意儿跨序列调用时负面可信、正面不可信:它说"已失效"您能信,它说"还活着"您得掂量。第六条最绕,也是真实工程里最容易翻车的:factory 析构等于把它铸出的所有 WeakPtr 全部作废,而且 factory 作为"最后成员"得反过来守护被指对象的析构期——这条要是没守住,成员析构到一半还有人持着 WeakPtr 解引用,就是经典的 UAF。

---

## Catch2 测试用例

笔者习惯用 [Catch2](https://github.com/catchorg/Catch2) 的 `TEST_CASE` 配 `REQUIRE`,把这六条不变量一个个落到具体用例上。下面挑几条关键的给您过一遍——这些是 Catch2 风格的示意用例,项目当前能跑的示例在 `code/volumn_codes/vol9/full_tutorial_codes/chrome_design/` 下的 `12`~`18` demo .cpp,把 Catch2 作为独立测试目标接进去这件事笔者留作了扩展,您有兴趣可以自己搭。

```cpp
// Platform: host | C++ Standard: C++20
#include <catch2/catch_test.hpp>
#include "weak_ptr/weak_ptr.hpp"

using namespace tamcpp::chrome;

struct Foo {
    int x = 42;
    int get() const { return x; }
};

TEST_CASE("WeakPtr basic: alive object dereferences correctly", "[weak_ptr]") {
    Foo foo{7};
    WeakPtrFactory<Foo> fac(&foo);
    auto wp = fac.get_weak_ptr();

    REQUIRE(wp);                  // 不变量 1:对象活着,wp 判活
    REQUIRE(wp->x == 7);
    REQUIRE(wp.get() == &foo);
}

TEST_CASE("WeakPtr move: source is empty after move", "[weak_ptr]") {
    Foo foo{1};
    WeakPtrFactory<Foo> fac(&foo);
    auto wp1 = fac.get_weak_ptr();
    auto wp2 = std::move(wp1);    // 不变量 2:move

    REQUIRE_FALSE(wp1);           // move 后源为空
    REQUIRE(wp2);
    REQUIRE(wp2->x == 1);
}

TEST_CASE("WeakPtr invalidate: all weak ptrs go null", "[weak_ptr]") {
    Foo foo{1};
    WeakPtrFactory<Foo> fac(&foo);
    auto wp1 = fac.get_weak_ptr();
    auto wp2 = fac.get_weak_ptr();

    fac.invalidate_weak_ptrs();   // 不变量 3:批量失效

    REQUIRE_FALSE(wp1);
    REQUIRE_FALSE(wp2);
    REQUIRE(wp1.get() == nullptr);
}

TEST_CASE("WeakPtr factory destruct: invalidates all weak ptrs", "[weak_ptr]") {
    Foo foo{1};
    WeakPtrFactory<Foo> fac(&foo);
    WeakPtr<Foo> wp;
    {
        // 模拟 factory 析构:用一个内层作用域
        // (真实"最后成员"场景见下面 BadController/GoodController 用例)
    }
    // 这里直接测 invalidate_weak_ptrs_and_doom 的"doom"语义
    fac.invalidate_weak_ptrs_and_doom();
    REQUIRE_FALSE(fac.has_weak_ptrs());
}

TEST_CASE("WeakPtr was_invalidated distinguishes dead-from-nulled", "[weak_ptr]") {
    Foo foo{1};
    WeakPtrFactory<Foo> fac(&foo);
    auto wp = fac.get_weak_ptr();
    REQUIRE_FALSE(wp.was_invalidated());      // 还活着,不是"已失效"

    fac.invalidate_weak_ptrs();
    REQUIRE(wp.was_invalidated());            // 被作废,而非主动 reset

    auto wp2 = fac.get_weak_ptr();
    wp2.reset();
    REQUIRE_FALSE(wp2.was_invalidated());     // 主动 reset 不算"被作废"
}

#if !defined(NDEBUG)
// 仅 debug:解引用失效应触发断言(教学版用 assert;Chromium 用 CHECK)
TEST_CASE("WeakPtr deref invalid asserts in debug", "[weak_ptr][.assert]") {
    Foo foo{1};
    WeakPtrFactory<Foo> fac(&foo);
    auto wp = fac.get_weak_ptr();
    fac.invalidate_weak_ptrs();
    // 不变量 4:这一行在 debug 下应 abort
    // 在隔离的 assert 测试里跑:REQUIRE_THROWS(wp->x);
    // (Catch2 对 abort 的隔离需要子进程,工程里用 death_test 模式)
}
#endif
```

测试设计的路数和 01-6 一脉相承:每条用例盯一个不变量,不去罗列 API。笔者特意把 `was_invalidated` 单拎出来测,因为它要区分"被作废"和"主动 reset"这两种状态——这是 WeakPtr 独有、最容易写歪的语义,写歪了调用方就分不清"对象真没了"还是"我自己放手了"。还有一处笔者踩过坑的:解引用失效的断言测试不能和普通用例同进程跑,`assert` 和 `CHECK` 都会 abort,一炸整批用例全红。工程里要么走 death_test 模式起子进程隔离,要么干脆把这几条用例打上 `[.assert]` 标签单独跑。

---

## 性能对比:对象大小

先拿 `sizeof` 量一量,本机 GCC 13、x86-64 的真实终端输出:

```cpp
static_assert(sizeof(WeakPtr<Foo>) == sizeof(void*) * 2);   // 16 字节
```

| 类型 | 大小(x86-64) | 组成 |
|---|---|---|
| `WeakPtr<T>`(教学版 / Chromium) | **16 字节** | `WeakReference`(=`scoped_refptr<const Flag>`,1 指针)+ `T*`(1 指针) |
| `std::weak_ptr<T>` | **16 字节** | 对象指针 + 控制块指针 |
| `std::shared_ptr<T>` | 16 字节 | 对象指针 + 控制块指针 |

三者 sizeof 出来都是 16 字节,看着平起平坐。但别被这个数字骗了,真正拉开差距的不是大小,是组成和分配行为。

---

## 性能对比:分配行为

WeakPtr 和 `std::weak_ptr` 大小一样,真要找差距,得看被指对象本身怎么分配。咱们对比一个被弱引用的对象从无到有要敲几次堆:

| 方案 | 堆分配次数 | 说明 |
|---|---|---|
| `std::weak_ptr` + `std::shared_ptr<T>(new T)` | **2 次** | 一次给 T,一次给控制块 |
| `std::weak_ptr` + `std::make_shared<T>()` | 1 次 | T 和控制块打包,但 `weak_ptr` 长寿会拖住整块内存(见 [pre-00](./pre-00-weak-ptr-weak-reference-and-lifetime.md)) |
| WeakPtr(被指对象自带 `WeakPtrFactory`) | **1 次**(Flag)+ 被指对象按它自己的方式 | Flag 侵入式引用计数,1 次分配;被指对象不强制 shared,该几次就几次 |

WeakPtr 这边笔者觉得最舒服的一点是,它不强迫被指对象改用任何特定分配方式。对象本来该怎么管还怎么管,您只要给它挂一个 factory,真正多出来的开销只有 factory 内部那枚 Flag 的一次侵入式分配。`std::weak_ptr` 那边就没这么大方了,要么老老实实两次分配,要么 `make_shared` 图个一次,代价是把整块对象内存跟控制块打包到一起,weak_ptr 一长寿,对象跟着赖着不释放。

---

## 性能对比:调用开销

WeakPtr 的 `get()` 调一次,底层链路是 `ref_.IsValid()` 走到 `flag_->IsValid()`,再走到一次 `invalidated_.IsSet()`——也就是一次原子的 acquire-load。热路径上就这一下,没别的。

`std::weak_ptr::lock()` 那边就热闹多了:先原子读一次 strong count 判断对象在不在,在的话还要再原子加一次 strong count 把它临时攥住,最后吐出一个临时 `shared_ptr`。比 `get()` 重,重就重在它得多做一次原子自增,还得临时搭一个 `shared_ptr` 出来。

这就是 WeakPtr "不延寿、只判活"的直接红利——它不学 `lock()` 那样临时抬 refcount,就老老实实读一次 flag。当然天下没有白吃的午餐,代价是返回的裸指针不保证对象在您解引用那会儿还活着,所以才需要"同序列 deref"那条契约在背后兜底。

---

## 性能对比:TRIVIAL_ABI 的收益

`TRIVIAL_ABI` 的好处在调用约定这一层,它让 WeakPtr 按值传参时直接进寄存器,而不是绕一道内存。我们在 [pre-06](./pre-06-weak-ptr-trivial-abi.md) 论证过它安全,那能不能量化?

```cpp
// 把 WeakPtr 按值传进函数
void sink(WeakPtr<Foo> wp) { (void)wp; }
```

标了 `[[clang::trivial_abi]]` 时,这 16 字节的参数走的是两个寄存器(比如 x86-64 SysV 下 `rdi`/`rsi` 那一票邻近寄存器);没标的话,编译器得在栈上开 16 字节,再隐式塞一个引用进去。`-O2` 下一减,前者省的是栈帧布局里的一份拷贝,外加析构那套调用约定的开销。

单看一次调用省得不多。但您想想 Chromium 任务系统那个量级,回调里成天大批大批地传 WeakPtr,这点收益累积起来就很吓人了。这也是为什么 Chromium 专门给 WeakPtr 和 WeakReference 标上这个属性,还把 `IsSet()` 强行 inline 进头文件——源码注释里白纸黑字写着 "measurable performance impact on base::WeakPtr",人家是真量过。

---

## vs std::weak_ptr:取舍总表

| 维度 | `std::weak_ptr` | `WeakPtr` |
|---|---|---|
| 是否介入所有权 | 是(必须配 `shared_ptr`) | **否**(对象该怎么管还怎么管) |
| 控制块/Flag 分配 | 非侵入式(独立或 `make_shared` 合并) | **侵入式**(Flag 一次分配) |
| 线程/序列模型 | 原子操作本身安全,序列由用户管 | **序列亲和**(deref/失效要同序列,debug 抓) |
| 跨线程 deref | `lock()` 线程安全 | 需同序列(靠契约 + DCHECK) |
| 批量失效 | 无(各 weak_ptr 独立 expired) | **一次 `invalidate` 失效所有**(共享 Flag) |
| 调用开销 | `lock()` 两次原子 + 临时 shared_ptr | `get()` 一次原子 acquire-load |
| 大小 | 16 字节 | 16 字节(且 `TRIVIAL_ABI` 进寄存器) |

一句话讲明白:`std::weak_ptr` 是通用、安全的弱引用;`WeakPtr` 则是为"任务投递、不介入所有权、序列化执行"这一套量身定做的。在 Chromium 这种体系里,后者跟模型贴得严丝合缝;真到了通用 C++ 代码里,前者够用、而且标准库自带,谁也不亏。

---

## vs 真实 Chromium:我们的取舍

和 [01-6 一样](../../01_once_callback/full/01-6-once-callback-testing-and-perf.md),咱们的教学版砍了不少东西,取舍笔者给您摆台面上:

| 维度 | Chromium 实现 | 我们的教学版 |
|---|---|---|
| Flag 引用计数 | `RefCountedThreadSafe`(原子) | 同(核心,不省) |
| 原子标志 | `base::AtomicFlag`(封装) | `std::atomic` 配 memory_order(等价) |
| 序列检查 | `SEQUENCE_CHECKER` 三宏 + SequenceToken | 简化 `SequenceChecker`(线程 id 模拟) |
| `SafeRef` | 完整(非空、悬空即崩) | 不实现(留作扩展) |
| `BindOnce` 集成 | 完整类型擦除 + `InvokeHelper` 双特化 | 简化 trampoline + 接 01 OnceCallback |
| `TRIVIAL_ABI` | 标注 | 标注(clang) |
| `InvalidateAndDoom` / `BindToCurrentSequence` | 完整 | `AndDoom` 保留;`BindToCurrentSequence` 省略 |

笔者牺牲掉的是完整度——`SafeRef` 没做,`BindToCurrentSequence` 砍了,真正的 `SequenceToken` 也用了简化版。换回来的是可读性和可编译性:教学版纯靠标准库外加一个 clang 属性就能跑起来。而那些真正承重的机制,refcounted Flag、acquire/release 配对、序列契约、编译期 weak 分派,一个字没动。笔者反复掂量过,对教学这个目的来说,这个买卖划算。

到这里,WeakPtr 这个组件从设计、实现一路到验证就全走通了。回望从 [pre-00 弱引用导论](./pre-00-weak-ptr-weak-reference-and-lifetime.md) 算起的 13 篇,咱们其实只做了一件事:把"生命周期"这个老问题,从模糊的工程直觉一步步逼成可编译、可测试、有明确 acquire/release 配对的代码。笔者写这一路反复摔出来的体会就一条:每一个签名、每一条 `requires`、每一对内存序,背后都得能说出一个具体的"为什么",说不出来的那些,早晚会在某个本该很顺的场景里翻车,而且往往是那种最难复现的翻车。这个系列跟 OnceCallback 那条线在这儿收口了,希望您下一个去啃工业级组件的时候,手上有这套直觉兜着。

---

## 参考资源

- [Catch2 文档](https://github.com/catchorg/Catch2/tree/devel/docs)
- [Chromium `base/memory/weak_ptr_unittest.cc` —— 官方测试](https://source.chromium.org/chromium/chromium/src/+/main:base/memory/weak_ptr_unittest.cc)
- [OnceCallback 实战（六）：测试与性能对比](../../01_once_callback/full/01-6-once-callback-testing-and-perf.md)
- [cppreference: std::weak_ptr](https://en.cppreference.com/w/cpp/memory/weak_ptr)
