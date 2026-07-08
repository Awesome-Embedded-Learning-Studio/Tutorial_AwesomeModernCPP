---
chapter: 1
cpp_standard:
- 23
description: 深入理解 CancelableToken 的设计——用 shared_ptr + atomic<bool> 实现轻量级取消机制，以及它如何集成到
  OnceCallback 的执行流程中
difficulty: beginner
order: 4
platform: host
prerequisites:
- OnceCallback 实战（二）：核心骨架搭建
- OnceCallback 前置知识速查：C++11/14/17 核心特性回顾
reading_time_minutes: 8
related:
- OnceCallback 实战（五）：then 链式组合
- OnceCallback 实战（六）：测试与性能对比
tags:
- host
- cpp-modern
- beginner
- 回调机制
- atomic
- 智能指针
- 引用计数
title: OnceCallback 实战（四）：取消令牌设计
---
# OnceCallback 实战（四）：取消令牌设计

回调这东西，绑出来的瞬间到真跑起来，中间通常隔着一段任务队列的调度。这段空档里啥都可能发生：绑定的对象没了、任务被上层撤了、用户已经关掉那个 tab 了。等回调终于轮到自己执行，它前面那个"我还有没有必要跑"的检查，就是这一篇要做的取消令牌（cancellation token）。

Chromium 在 `//base` 里把这事儿做成了 `WeakPtr` 那一整套——对象析构，挂在它身上的回调集体作废。那套机制完整讲下来量不小，咱们这里先做个最小可用版本：一个能被多人共享、一次作废全部失效、还能跨线程检查的轻量标志。等手头这个跑通，您回头看 Chromium 的 `WeakPtr` 会顺很多。

## 一个标志，怎么共享

取消令牌要解决的核心矛盾就一句话：作废这个动作发生在回调外部，检查这个动作发生在回调内部，两边大概率不在同一处、甚至不在同一线程。所以令牌本身得是个能被两边各拿一份、却又指向同一份状态的东西。

这意味着两件事必须同时成立。令牌得能拷贝，回调内部持一份，外部想取消它的代码也持一份，两份指到同一处；还有，那份共享状态的读写得是多线程安全的，外部线程调 `invalidate()`、回调线程查 `is_valid()`，这一对操作撞在一起不能读到撕裂的值。

咱们先看实现，再回头说为什么长这样。

## CancelableToken 的完整实现

整个取消令牌只有 18 行代码，但每一行都有它的道理。

```cpp
#pragma once
#include <atomic>
#include <memory>

namespace tamcpp::chrome {
class CancelableToken {
    struct Flag {
        std::atomic<bool> valid{true};
    };
    std::shared_ptr<Flag> flag_;

public:
    CancelableToken() : flag_(std::make_shared<Flag>()) {}

    void invalidate() {
        flag_->valid.store(false, std::memory_order_release);
    }

    bool is_valid() const {
        return flag_->valid.load(std::memory_order_acquire);
    }
};
} // namespace tamcpp::chrome
```

### 为什么非得套个嵌套结构体 Flag

笔者第一次写这玩意儿的时候手一快，直接在 `CancelableToken` 里塞了个 `std::atomic<bool> valid`，心想这不就完了？写完才发现不对劲——`shared_ptr` 这下没法管这个 `valid` 了。要共享它，您得让 `shared_ptr` 指向一个包含 `valid` 的对象。可这个对象如果是 `CancelableToken` 本身，那 `CancelableToken` 里又有个 `shared_ptr` 成员，这就绕成了 `shared_ptr<CancelableToken>` 包着 `shared_ptr<Flag>` 的套娃。

把要共享的那点状态单独拎出来，塞进一个 `Flag` 结构体，让 `shared_ptr` 直接管它，这层窗户纸才算捅破。`CancelableToken` 的拷贝和移动这时候都不用您操心——默认生成的拷贝构造就是把内部的 `shared_ptr<Flag>` 浅拷一份，引用计数自增，所有副本自然指向同一个 `Flag`。笔者后来想想，这结构体还有个意外好处：以后真要往里加东西，比如一个取消原因码，直接在 `Flag` 里加字段就行，外头一行都不用动。

### 共享是怎么落地的

光说"拷贝时共享"可能还是抽象。您看下面这段，`token2` 是 `token1` 的拷贝，两者内部那个 `shared_ptr<Flag>` 指到同一块内存。`token1` 调 `invalidate()` 改的就是那块内存里的 `valid`，`token2` 下次查 `is_valid()` 读的也是同一块，自然就看到 `false`：

```cpp
auto token1 = std::make_shared<CancelableToken>();
auto token2 = token1;  // 共享同一个 Flag

token1->invalidate();
assert(!token2->is_valid());  // token2 也看到了失效
```

这里笔者要提醒一句：示例里外层又套了个 `shared_ptr<CancelableToken>`，纯是为了写起来短。真正的用法是 `CancelableToken` 本身按值拷贝（它内部已经用 `shared_ptr` 共享状态了），不需要再在外面裹一层智能指针。

### acquire/release 这对配子

`invalidate()` 用 `memory_order_release` 存 `false`，`is_valid()` 用 `memory_order_acquire` 取。这不是随手选的。release 存保证：在它之前的那些写（比如作废前对对象状态的修改），在别的线程通过这次 store 读到新值时，全部变得可见。acquire 取则保证：读到那个新值之后，后续的读都能看到 release 之前的那批写。

落到咱们这场景里，一个线程调 `invalidate()`，另一个线程紧接着查 `is_valid()`，只要后者读到了 `false`，前者 invalidate 之前干的所有活儿，对后者都是可见的。您不会撞上"我刚 invalidate 完，怎么 is_valid 还说 true"这种鬼事。这也是它敢跨线程用的底气。如果两个序都换成 `memory_order_relaxed`，可见性保证就没了，flag 翻没翻、别的线程什么时候看到，全凭运气，这是新手最容易手滑的地方。

## 集成到 OnceCallback

取消令牌得挂到回调身上才有用武之地。挂的入口是一个 `set_token()`：

```cpp
void set_token(std::shared_ptr<CancelableToken> token) {
    token_ = std::move(token);
}
```

`token_` 默认是空的 `shared_ptr`，等于"这个回调不参与取消机制"。一旦 `set_token` 进来，令牌就被搬进回调内部，跟着回调一起活一起死。这里笔者特意用了 `shared_ptr<CancelableToken>` 而不是裸 `CancelableToken`，是因为 OnceCallback 是 move-only 的，它持有的东西要么能整体搬走、要么得是个能廉价拷贝的句柄——`shared_ptr` 正好是后者。

### is_cancelled() 看的是两个地方

```cpp
[[nodiscard]] bool is_cancelled() const noexcept {
    if (status_ != Status::kValid) return true;
    if (token_ && !token_->is_valid()) return true;
    return false;
}
```

这个判断不是只看令牌。它先瞄一眼回调自己的 `status_`：空回调（`kEmpty`）和已经跑过的回调（`kConsumed`）都直接算作"已取消"。这是合理的，空回调里头没东西可执行，跑过的回调不该再跑。`status_` 这一关过了，才轮到令牌出场，有令牌且令牌已失效，也算取消。两道关卡都得在，只看令牌的话，空回调和已消费回调就漏网了。

### impl_run() 里的那一道门

```cpp
ReturnType impl_run(FuncArgs... args) {
    assert(status_ == Status::kValid);

    // 取消检查在执行前
    if (token_ && !token_->is_valid()) {
        status_ = Status::kConsumed;
        func_ = nullptr;
        if constexpr (std::is_void_v<ReturnType>) {
            return;
        } else {
            throw std::bad_function_call{};
        }
    }

    // 正常消费流程...
}
```

取消检查摆在执行可调用对象**之前**，这个位置很关键。检查一旦命中，回调当场被标记成 `kConsumed`——`func_` 也跟着置空，里头那个 lambda 及其捕获的资源随即释放。从外部看，这次 `run()` 像是消费掉了回调，只是没真正执行函数体。这个"消费但不执行"的语义，是后面 void 和非 void 行为分叉的根。

## void 和非 void 回调，取消时为啥不一样

这是整个设计里笔者觉得最值得停下来说的一处。同样是命中取消检查，void 回调直接 `return`，啥也不报；非 void 回调却抛 `std::bad_function_call`。乍看好像不一致，细想是被迫的。

void 回调的调用方压根不期待返回值——`std::move(cb).run();` 调完就完了，他根本不知道您执行没执行。所以取消时静默跳过，对调用方是完全透明的，没毛病。

非 void 回调就尴尬了。调用方写的是 `int result = std::move(cb).run();`，他盯着那个返回值呢。回调被取消了，您给啥返回值？随便填个 0？那调用方拿到一个 0，会以为回调正经跑完给了他 0，后续逻辑照着 0 往下走——这种"看起来成功其实啥也没干"的 bug，比崩了还难查。所以这里宁可抛异常，明明白白告诉调用方"这次没跑成，您自己看着办"。

Chromium 的选择更狠：直接 `CHECK` 失败终止进程。它的逻辑是，在我这套架构里，调用方本来就该在调 `run()` 之前自己查 `is_cancelled()`，您都查过了还往里冲，那就是 bug，崩给您看。咱们这里走异常那条路，主要是为了让测试好写，单元测试里 `REQUIRE_THROWS` 就能断言，不至于一跑用例整个进程挂掉。两种选择没有对错，看您这套回调打算用在多严苛的环境里。

## 使用示例

```cpp
using namespace tamcpp::chrome;

// 创建令牌和回调
auto token = std::make_shared<CancelableToken>();
bool executed = false;

OnceCallback<void()> cb([&executed] { executed = true; });
cb.set_token(token);

// 令牌有效时，正常执行
assert(!cb.is_cancelled());
std::move(cb).run();
assert(executed);  // 回调被执行了

// 创建另一个回调，这次先取消令牌
executed = false;
auto cb2 = OnceCallback<void()>([&executed] { executed = true; });
cb2.set_token(token);
token->invalidate();  // 作废令牌

assert(cb2.is_cancelled());
std::move(cb2).run();  // 取消的 void 回调不执行，不抛异常
assert(!executed);     // 回调没有被执行
```

第二个例子要看仔细：`cb2.run()` 确实被调了，但里头的 lambda 一行没跑。`impl_run()` 在执行前撞见令牌失效，直接把回调消费掉然后 `return`——`executed` 还是 `false`，这就是 void 回调取消时的透明语义。

## 参考资源

- [cppreference: std::shared_ptr](https://en.cppreference.com/w/cpp/memory/shared_ptr)
- [cppreference: std::atomic](https://en.cppreference.com/w/cpp/atomic/atomic)
- [Chromium WeakPtr 文档](https://chromium.googlesource.com/chromium/src/+/main/docs/memory_model/weak_ptr.md)
