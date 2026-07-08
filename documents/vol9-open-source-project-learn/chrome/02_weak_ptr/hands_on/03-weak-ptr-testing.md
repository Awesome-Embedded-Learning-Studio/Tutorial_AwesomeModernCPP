---
chapter: 1
cpp_standard:
- 17
- 20
description: WeakPtr 的测试策略——围绕六条不变量设计用例,并量化对象大小/分配/调用开销,
  与 std::weak_ptr 和真实 Chromium 做取舍对比
difficulty: advanced
order: 3
platform: host
prerequisites:
- weak_ptr 设计指南（二）：逐步实现
- OnceCallback 设计指南（三）：测试策略与性能对比
reading_time_minutes: 7
related:
- weak_ptr 设计指南（一）：动机、接口与控制块设计
- WeakPtr 实战（六）：测试与性能对比
tags:
- host
- cpp-modern
- advanced
- 智能指针
- weak_ptr
- 测试
- 优化
title: "weak_ptr 设计指南（三）：测试策略与性能对比"
---
# weak_ptr 设计指南（三）：测试策略与性能对比

上一篇撸完实现,笔者心里其实没那么踏实——代码能编过是一回事,语义对不对是另一回事。WeakPtr 这种东西最怕的就是"看起来能跑":您测一个 happy path,绿了;可 UAF、析构竞态、move 后源对象状态这些坑,全藏在边界上。咱们这一篇就把上一篇承诺的六条不变量一条条钉回测试里,顺便拿真实数据跟 `std::weak_ptr` 和真实 Chromium 比一比,看教学版到底省在了哪、又牺牲了什么。思路跟 [OnceCallback 设计指南（三）](../../01_once_callback/hands_on/03-once-callback-testing.md) 一脉相承:不变量驱动,数据说话,不空口。

## 六条不变量 → 测试矩阵

| # | 不变量 | 必须成立的断言 |
|---|---|---|
| 1 | 基本可用 | 对象活着时 `wp` 判活、`get()` 返回真地址 |
| 2 | move 语义 | move 后源对象为空(`operator bool == false`) |
| 3 | invalidate 后失效 | `invalidate_weak_ptrs()` 后所有已铸 `wp.get()==nullptr` |
| 4 | CHECK-on-deref | 解引用失效 `wp` 触发断言(debug assert / release CHECK) |
| 5 | maybe_valid 不对称 | 负面可信(false⇒必失效)、正面不可信 |
| 6 | factory 析构失效 | factory 析构后所有 wp 失效;最后成员守护成员析构期 |

## 关键用例(Catch2 风格)

六条不变量听着抽象,落到测试上其实就挑那些"写错了一定会爆"的边界。笔者这里挑三条最能锁语义的——共享 Flag 的集体失效、`was_invalidated` 区分作废与主动 reset、最后成员的析构顺序。项目当前可运行的是 `code/volumn_codes/vol9/full_tutorial_codes/chrome_design/` 下 `12`~`18` 那几个 demo .cpp,Catch2 测试目标接入留作扩展,这里先看用例长什么样:

```cpp
// Platform: host | C++ Standard: C++20
#include <catch2/catch_test.hpp>
#include "weak_ptr/weak_ptr.hpp"
using namespace tamcpp::chrome;

struct Foo { int x = 42; };

TEST_CASE("invalidate kills all weak ptrs sharing the flag", "[weak_ptr]") {
    Foo foo;  WeakPtrFactory<Foo> fac(&foo);
    auto wp1 = fac.get_weak_ptr();
    auto wp2 = fac.get_weak_ptr();          // 同一 factory → 共享同一 Flag
    fac.invalidate_weak_ptrs();
    REQUIRE_FALSE(wp1);                     // 不变量 3:集体失效
    REQUIRE_FALSE(wp2);
}

TEST_CASE("was_invalidated vs reset", "[weak_ptr]") {
    Foo foo;  WeakPtrFactory<Foo> fac(&foo);
    auto wp_a = fac.get_weak_ptr();
    fac.invalidate_weak_ptrs();
    REQUIRE(wp_a.was_invalidated());        // 被作废
    auto wp_b = fac.get_weak_ptr();
    wp_b.reset();
    REQUIRE_FALSE(wp_b.was_invalidated());  // 主动 reset 不算作废
}

// 不变量 6:最后成员守护成员析构期
struct Good {                                  // ✓ factory 最后声明
    std::vector<int> buf_;
    WeakPtrFactory<Good> fac_{this};
};
struct Bad {                                   // ✗ factory 先声明 → 最后析构
    WeakPtrFactory<Bad> fac_{this};
    std::vector<int> buf_;
};
TEST_CASE("last-member idiom: destruction order", "[weak_ptr][.death]") {
    // Good:fac_ 先析构 → WeakPtr 失效 → buf_ 才析构
    // Bad:buf_ 先析构 → fac_ 后析构 → 中间窗口 WeakPtr 仍有效(可悬垂 deref)
    // 用 TSan/AddressSanitizer 在隔离 death test 里验证 Good 不 UAF
}
```

这三条盯的都是语义边界,不是 API 表面。共享 Flag 的集体失效,验的是"一个 invalidate 大家一起死"这条承诺到底成不成立;`was_invalidated` 那条更细——主动 `reset()` 不该算作被作废,笔者写的时候特意拿 `wp_b` 跟 `wp_a` 对照,就是怕把两种"变空"混成一回事。最后成员那条是析构顺序的命门,笔者单独拎出来讲。

不变量 4(CHECK-on-deref)和 6(析构顺序)有个麻烦:它们会 abort。直接塞进普通 TEST_CASE 里跑,整个二进制都得跟着挂。所以得隔离成 death test,让它在子进程里崩——这套路数跟 01-6 处理 OnceCallback 单次消费断言是同一套,笔者在那篇已经趟过一遍了。

## 性能:对象大小

先看最直观的——一个 `WeakPtr<T>` 到底吃多少字节。笔者直接用 `static_assert` 定住,编不过就是错:

```cpp
static_assert(sizeof(WeakPtr<Foo>) == sizeof(void*) * 2);   // 16 字节(x86-64)
```

| 类型 | sizeof | 组成 |
|---|---|---|
| `WeakPtr<T>` | 16 | `WeakReference`(scoped_refptr,1 ptr)+ `T*`(1 ptr) |
| `std::weak_ptr<T>` | 16 | 对象指针 + 控制块指针 |

sizeof 打出来一样,笔者一开始还有点意外——`std::weak_ptr` 名声在外,本以为会更紧凑。但再一琢磨就对上了:两边都是两个指针,一个指对象、一个指控制块/Flag,结构上就是对称的。真正的差别在分配行为(见下表),不在大小。

还有一处 `sizeof` 看不出来的收益:`TRIVIAL_ABI` 让 WeakPtr 按值传参时能整个塞进寄存器(两个寄存器就够),`std::weak_ptr` 做不到。这是 ABI 层的事,benchmark 不一定测得到,但在热路径上它是实打实省了栈操作。

## 性能:分配与调用

大小一样,真正拉开差距的是分配次数和判活开销。笔者把两边逐项对了一遍:

| 维度 | `std::weak_ptr` | `WeakPtr` |
|---|---|---|
| 被指对象分配 | `shared_ptr(new T)` 2 次;`make_shared` 1 次但捆死内存 | 不强制:Flag 1 次侵入式 + 对象按自身方式 |
| 判活开销 | `lock()`:原子读 strong count + 若存活再 inc + 构造临时 shared_ptr | `get()`:1 次原子 acquire-load,返回裸指针 |
| 跨序列 deref | `lock()` 线程安全 | 需同序列(契约 + DCHECK) |
| 批量失效 | 无 | **一次 invalidate 失效所有**(共享 Flag) |

表里最值得琢磨的一行是判活开销。`get()` 比 `lock()` 轻,不是巧——`lock()` 得先原子读 strong count,确认存活后再 inc 一次,最后还给您一个临时 `shared_ptr`(出来还得 dec 回去),一来一回好几趟原子操作。`get()` 就一次 acquire-load,返回裸指针,完事。代价当然有:返回的是裸指针,没人帮您管同步,得靠"同序列"这条契约兜底。笔者认为这买卖划算——契约是 debug 下 DCHECK 抓的,真违规了开发阶段就爆,不至于带到线上。

## vs 真实 Chromium:教学版取舍

| 维度 | Chromium | 教学版 |
|---|---|---|
| Flag refcount | `RefCountedThreadSafe` | 同 |
| 原子标志 | `base::AtomicFlag` | `std::atomic` + memory_order(等价) |
| 序列检查 | `SEQUENCE_CHECKER` + SequenceToken | 简化(线程 id 模拟) |
| `SafeRef` | 完整 | 不实现 |
| `BindOnce` 集成 | 完整 `InvokeHelper` 双特化 | 简化 trampoline |
| `InvalidateAndDoom` | 完整 | 保留 |
| `BindToCurrentSequence` | 完整 | 省略 |

笔者做教学版时的取舍逻辑是这样的:周边能省的省——`SafeRef`、`BindToCurrentSequence` 这些砍掉,序列检查拿线程 id 模拟;但核心机制一动不动,refcounted Flag、acquire/release 配对、序列契约、编译期 weak 分派,该是怎样还怎样。理由很简单,周边是工程便利,核心是正确性命门,砍周边顶多用着别扭,砍核心那就不是 WeakPtr 了。

到这儿,WeakPtr 组件的设计、实现、验证三篇就走完了。回头看,它跟 OnceCallback 是一对姊妹篇:01-4 那会儿笔者图省事甩的取消令牌,工业级正解就是这套 WeakPtr 体系,现在算是闭环了。

## 参考资源

- [Chromium `base/memory/weak_ptr_unittest.cc`](https://source.chromium.org/chromium/chromium/src/+/main:base/memory/weak_ptr_unittest.cc)
- [Catch2 文档](https://github.com/catchorg/Catch2/tree/devel/docs)
- [weak_ptr 设计指南（一）：动机、接口与控制块设计](./01-weak-ptr-design.md)
- [OnceCallback 设计指南（三）：测试策略与性能对比](../../01_once_callback/hands_on/03-once-callback-testing.md)
