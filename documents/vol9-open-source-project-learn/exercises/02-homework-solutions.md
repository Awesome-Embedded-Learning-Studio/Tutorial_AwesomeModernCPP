---
title: "卷 9 课后练习参考答案（Homework）"
description: "卷 9（开源项目学习）课后练习的逐题详细解答：每个主题给出解题思路、逐步解答（每步标注知识点链接）与真实验证输出（g++ 16.1.1 / clang++ 22.1.8 / WSL Arch 实跑，含 ASan/UBSan/TSan 用例与编译失败用例）。"
chapter: 9
order: 2
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
reading_time_minutes: 45
prerequisites:
  - "卷 9 课后练习（Homework）"
related:
  - "卷 9 chrome/ 四个主题"
---

# 卷 9 课后练习参考答案（Homework）

> 所有命令与输出在 WSL Arch（g++ 16.1.1 / clang++ 22.1.8）下真实运行得到，默认 `-std=c++20`，仅 9.1-C（deducing this）与 9.C-3（`std::move_only_function`）挂 `-std=c++23`。UB 类题目的输出「只是这台机器这次的选择」，换编译器/优化级别可能不同——这正是每道题要您体会的东西。ASan 报告里的地址与 PID 每次运行都会变，别拿它们对答案，对的是**结构**。

## 9.1-A {#hw-9-1-a}

**难度 L1** · 题面见 [homework](01-homework.md#hw-9-1-a)

**思路**：`SlotTraits` 就是教材 `FuncTraits` 换了个业务场景，主模板不提供定义、偏特化拆 `R(Args...)`，套路一字不差。三条是非题考的是"函数类型比函数指针更底层"：函数类型不是指针，但函数名在多数表达式里会退化成指针。

1. `SlotTraits` 偏特化把 `int(double, char)` 拆成 `ReturnType=int`、`Args...={double,char}`，`kArity=2`；`void()` 的 `ReturnType=void`、`kArity=0`。全部用 `static_assert` 在编译期钉死——编过即验证。→ 知识点：[OnceCallback 前置知识（一）：函数类型与模板偏特化](../chrome/01_once_callback/full/pre-01-once-callback-function-type-and-specialization.md)「动手实践：撸一个 FuncTraits」一节
2. `is_function_v<void()>` 为真；`is_pointer_v<void()>` 为假（它是个函数类型）；`is_pointer_v<void(*)()>` 为真（这才是函数指针）。预测：真/假/真。→ 知识点：同上「函数类型：C++ 里一个容易被错过的类型」一节
3. 运行期打印 `kArity` 的 3，证明偏特化不仅编译期成立、运行期也能把常量拿出来用。→ 知识点：同上（`static constexpr` 成员是编译期常量）

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra hw91a.cpp -o hw91a.out && ./hw91a.out
SlotTraits<int(int,int,int)>::kArity = 3
```

完整代码：

```cpp
#include <cstddef>
#include <iostream>
#include <tuple>
#include <type_traits>

template <typename T>
struct SlotTraits;   // 主模板:不提供定义,挡住非函数类型

template <typename R, typename... Args>
struct SlotTraits<R(Args...)> {
    using ReturnType = R;
    using ArgsTuple = std::tuple<Args...>;
    static constexpr std::size_t kArity = sizeof...(Args);
};

static_assert(std::is_same_v<SlotTraits<int(double, char)>::ReturnType, int>);
static_assert(SlotTraits<int(double, char)>::kArity == 2);
static_assert(std::is_same_v<SlotTraits<void()>::ReturnType, void>);
static_assert(SlotTraits<void()>::kArity == 0);

// 是非题:真 / 假 / 真
static_assert(std::is_function_v<void()>);
static_assert(!std::is_pointer_v<void()>);
static_assert(std::is_pointer_v<void (*)()>);

int main() {
    std::cout << "SlotTraits<int(int,int,int)>::kArity = "
              << SlotTraits<int(int, int, int)>::kArity << '\n';
    return 0;
}
```

## 9.1-B {#hw-9-1-b}

**难度 L2** · 题面见 [homework](01-homework.md#hw-9-1-b)

**思路**：`uniform_call` 内部只写一行 `std::invoke`，四类可调用对象的分派全交给标准库。第三类用引用和指针各调一次，是为了看清 `std::invoke` 对"对象指针"的自动解引用。

1. 自由函数与 lambda 走 `f(args...)` 那条常规路径。→ 知识点：[OnceCallback 前置知识（二）：std::invoke 与统一调用协议](../chrome/01_once_callback/full/pre-02-once-callback-invoke-and-callable.md)「std::invoke 的分派规则」一节
2. 成员函数指针：`std::invoke(&Account::deposit, a, 100)` 展开成 `(a.*deposit)(100)`；`std::invoke(&Account::deposit, &a, 40)` 展开成 `((*&a).*deposit)(40)`——第一实参是指针时它替您解引用，`balance` 最终 140。→ 知识点：同上「std::invoke 的分派规则」一节（INVOKE 表达式三分类）
3. 数据成员指针：`std::invoke(&Rect::width, r)` 等价 `r.width`，读到 7。→ 知识点：同上（指向数据成员的指针走 `obj.*pmd`）
4. 反面验证：不用 `std::invoke` 直接 `f(args...)`，编译器在实例化那一刻报 `must use '.*' or '->*' to call pointer-to-member function`——成员函数指针压根没有 `operator()`，调用语法与普通函数不是一套。→ 知识点：同上「问题：可调用对象的调用语法分裂」一节

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra hw91b.cpp -o hw91b.out && ./hw91b.out
free fn     : 7
lambda      : 15
member fn   : 140
member data : 7

$ g++ -std=c++20 -Wall -Wextra hw91b_bad.cpp -o hw91b_bad.out
hw91b_bad.cpp: In instantiation of 'decltype(auto) direct_call(F&&, Args&& ...) [with F = void (Account::*)(int); Args = {Account&, int}]':
hw91b_bad.cpp:15:16:   required from here
   15 |     direct_call(&Account::deposit, a, 100);
      |     ~~~~~~~~~~~^~~~~~~~~~~~~~~~~~~~~~~~~~~
hw91b_bad.cpp:10:13: error: must use '.*' or '->*' to call pointer-to-member function in 'f (...)', e.g. '(... ->* f) (...)'
   10 |     return f(args...);   // 直接调用:成员函数指针过不去
      |            ~^~~~~~~~~
```

正面代码：

```cpp
#include <functional>
#include <iostream>

int add(int a, int b) { return a + b; }

struct Account {
    int balance = 0;
    void deposit(int v) { balance += v; }
};

struct Rect {
    int width = 7;
    int height = 3;
};

template <typename F, typename... Args>
decltype(auto) uniform_call(F&& f, Args&&... args) {
    return std::invoke(std::forward<F>(f), std::forward<Args>(args)...);
}

int main() {
    std::cout << "free fn     : " << uniform_call(&add, 3, 4) << '\n';

    int base = 10;
    auto lam = [base](int x) { return base + x; };
    std::cout << "lambda      : " << uniform_call(lam, 5) << '\n';

    Account a;
    uniform_call(&Account::deposit, a, 100);   // 引用姿势
    uniform_call(&Account::deposit, &a, 40);   // 指针姿势
    std::cout << "member fn   : " << a.balance << '\n';

    Rect r;
    std::cout << "member data : " << uniform_call(&Rect::width, r) << '\n';
    return 0;
}
```

反面代码（把注释那行打开就是真实报错）：

```cpp
template <typename F, typename... Args>
decltype(auto) direct_call(F&& f, Args&&... args) {
    return f(args...);   // 直接调用:成员函数指针过不去
}
```

## 9.1-C {#hw-9-1-c}

**难度 L3** · 题面见 [homework](01-homework.md#hw-9-1-c)

**思路**：这是本卷第一个"复刻核心机制"的题。骨架五个零件照着教材拼：偏特化、三态、`not_the_same_t` 约束、deducing this 的 `run()`、先取出再执行的 `impl_run`。

1. 主模板只声明不定义，偏特化 `OnceCallback<R(Args...)>` 落地。`Status` 用 `uint8_t` 打底省内存。→ 知识点：[once_callback 设计指南（二）：逐步实现](../chrome/01_once_callback/hands_on/02-once-callback-implementation.md)「第一步：核心骨架」一节、[OnceCallback 实战（二）：核心骨架搭建](../chrome/01_once_callback/full/01-2-once-callback-core-skeleton.md)「第二步：数据成员」一节
2. 模板构造挂 `not_the_same_t`：把"传进来的恰好是 `OnceCallback` 本身"的情形踢出模板候选，让移动构造接手，否则模板会劫持移动。→ 知识点：[OnceCallback 前置知识（四）：Concepts 与 requires 约束](../chrome/01_once_callback/full/pre-04-once-callback-concepts-and-requires.md)「把 not_the_same_t 拆开看」一节
3. `run()` 的 deducing this：`cb.run()` 时 `Self` 推成 `OnceCallback&`，`static_assert` 当场炸并给出人话指引；`std::move(cb).run()` 时 `Self` 是非引用类型，放行。→ 知识点：[OnceCallback 前置知识（六）：Deducing this (C++23)](../chrome/01_once_callback/full/pre-06-once-callback-deducing-this.md)「落到 `run()` 上」一节
4. `impl_run` 三步顺序不能换：先 `std::move(func_)` 取出、再置空与置 `kConsumed`、最后才执行——即使回调体内抛异常，状态也已经是"已消费"，不会卡在中间态。→ 知识点：[once_callback 设计指南（二）：逐步实现](../chrome/01_once_callback/hands_on/02-once-callback-implementation.md)「消费语义的内部实现思路」一节
5. 死亡用例：第二次 `run` 时 `assert(status_ == Status::kValid)` 失败，进程 SIGABRT，退出码 134。→ 知识点：同上（消费后二次访问是契约违规）

**验证输出**（g++ 与 clang++ 双编译器同款输出）：

```text
$ g++ -std=c++23 -Wall -Wextra -Wpedantic hw91c.cpp -o hw91c.out && ./hw91c.out
non-void  : 7
void      : called=true
move-only : 42
move      : src is_null=true target=1
$ clang++ -std=c++23 -Wall -Wextra hw91c.cpp -o hw91c_clang.out && ./hw91c_clang.out
non-void  : 7
void      : called=true
move-only : 42
move      : src is_null=true target=1

$ g++ -std=c++23 -Wall -Wextra hw91c_death.cpp -o hw91c_death.out
$ ./hw91c_death.out; printf 'death run exit=%d\n' $?
hw91c_death.out: hw91c_death.cpp:57: R OnceCallback<R(Args ...)>::impl_run(Args ...) [with R = int; Args = {int}]: Assertion `status_ == Status::kValid' failed.
Aborted
death run exit=134
```

正面代码（核心骨架，`bind_once`/取消令牌留给 Lab）：

```cpp
#include <cassert>
#include <concepts>
#include <cstdint>
#include <functional>
#include <iostream>
#include <memory>
#include <type_traits>

template <typename F, typename T>
concept not_the_same_t = !std::is_same_v<std::decay_t<F>, T>;

template <typename Signature>
class OnceCallback;   // 主模板:只有声明,没有定义

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
        : status_(other.status_), func_(std::move(other.func_)) {
        other.status_ = Status::kEmpty;
    }
    OnceCallback& operator=(OnceCallback&& other) noexcept {
        if (this != &other) {
            status_ = other.status_;
            func_ = std::move(other.func_);
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
    [[nodiscard]] bool is_cancelled() const noexcept { return status_ != Status::kValid; }

private:
    R impl_run(Args... args) {
        assert(status_ == Status::kValid);
        auto f = std::move(func_);      // 先取出
        func_ = nullptr;                // 再置空
        status_ = Status::kConsumed;    // 状态先行
        if constexpr (std::is_void_v<R>) {
            f(std::forward<Args>(args)...);
        } else {
            return f(std::forward<Args>(args)...);
        }
    }

    Status status_ = Status::kEmpty;
    std::move_only_function<FuncSig> func_;
};

int main() {
    // 1 非 void 返回
    OnceCallback<int(int, int)> add([](int a, int b) { return a + b; });
    std::cout << "non-void  : " << std::move(add).run(3, 4) << '\n';

    // 2 void 返回
    bool called = false;
    OnceCallback<void()> side([&called] { called = true; });
    std::move(side).run();
    std::cout << "void      : called=" << std::boolalpha << called << '\n';

    // 3 move-only 捕获
    auto p = std::make_unique<int>(42);
    OnceCallback<int()> cap([p = std::move(p)] { return *p; });
    std::cout << "move-only : " << std::move(cap).run() << '\n';

    // 4 移动语义:移动只是搬家,run 才是消费
    OnceCallback<int()> m1([] { return 1; });
    OnceCallback<int()> m2 = std::move(m1);
    std::cout << "move      : src is_null=" << m1.is_null()
              << " target=" << std::move(m2).run() << '\n';
    return 0;
}
```

死亡用例只需把 `main` 换成连跑两次：

```cpp
int main() {
    OnceCallback<int(int)> cb([](int x) { return x * 2; });
    std::move(cb).run(5);   // 第一次:合法,=10
    std::move(cb).run(5);   // 第二次:assert 炸,abort
    return 0;
}
```

## 9.2-A {#hw-9-2-a}

**难度 L1** · 题面见 [homework](01-homework.md#hw-9-2-a)

**思路**：①四行输出依次是 `use_count=1`、`~Foo`、`expired=1`、`locked=no`——出块后 strong count 归零，`Foo` 析构，`expired()` 翻真，`lock()` 还回空。②"延寿"延的是**对象本身**的命：`lock()` 返回一个有效的 `shared_ptr`，它持有 strong count，直到您用完，对象都不会被析构。

1. `sp` 在块内是唯一强引用，`use_count=1`；`wp` 只加 weak count、不插手 strong count。→ 知识点：[WeakPtr 前置知识（零）：弱引用与生命周期难题](../chrome/02_weak_ptr/full/pre-00-weak-ptr-weak-reference-and-lifetime.md)「std::weak_ptr:标准库的弱引用」一节
2. 出块后 `sp` 析构、`Foo` 析构（打印 `~Foo`），但控制块因为 `wp` 还在而保留——`make_shared` 把对象和控制块合并成一块分配，所以"对象析构 ≠ 内存归还"：这块内存要等 `wp` 也销毁才真正释放。→ 知识点：同上「make_shared 与控制块:一个反直觉的内存细节」一节
3. `lock()` 把判活和升级成强引用塞进同一个原子操作：活着给有效 `shared_ptr`，死了给空。`expired()+解引用` 的两步之间有 TOCTOU 窗口。→ 知识点：同上「为什么必须用 lock(),而不是 expired() + 构造」一节

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra hw92a.cpp -o hw92a.out && ./hw92a.out
use_count=1
~Foo
expired=1
locked=no
```

①的注释答案（60 字内）：「`lock()` 在判活的同时把 strong count 加一，返回的 `shared_ptr` 让对象在您使用期间保持存活，窗口不存在。」

## 9.2-B {#hw-9-2-b}

**难度 L2** · 题面见 [homework](01-homework.md#hw-9-2-b)

**思路**：侵入式引用计数的三个记忆点：计数是对象成员（一次分配）、`release` 用 `acq_rel`（读最新值 + 归零时发布析构前的写）、析构 private 堵外部 delete。

1. `add_ref` 用 `relaxed`——纯计数不传信号；`release` 用 `acq_rel`——`fetch_sub` 是读改写，acquire 一侧看得到别的线程的最新计数，release 一侧把析构前的写交给接管 `delete` 的线程；`has_one_ref` 用 `acquire`。→ 知识点：[WeakPtr 前置知识（一）：侵入式引用计数与 scoped_refptr](../chrome/02_weak_ptr/full/pre-01-weak-ptr-intrusive-refcount-and-scoped-refptr.md)「手写一个最小的侵入式引用计数」一节、[WeakPtr 前置知识（二）：std::atomic 与 memory_order](../chrome/02_weak_ptr/full/pre-02-weak-ptr-atomic-and-memory-order.md)「回到 release() 的 acq_rel」一节
2. 两个 `scoped_refptr` 共享时 `has_one_ref()==0`，一个时 `==1`；构造/析构计数各 1 次——一个 Flag 一次 `new`、一次 `delete`，没有控制块的二次分配。→ 知识点：[WeakPtr 前置知识（一）](../chrome/02_weak_ptr/full/pre-01-weak-ptr-intrusive-refcount-and-scoped-refptr.md)「对比表」一节（侵入式一次分配）
3. 外部 `delete p.get()` 撞上 private 析构：`'Flag::~Flag()' is private within this context`——编译器在调用点把路堵死，误删从源头消灭。→ 知识点：同上「一个必须堵的坑:别让用户直接 new/delete」一节

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra hw92b.cpp -o hw92b.out && ./hw92b.out
after 1st ref : has_one_ref=1
after copy    : has_one_ref=0
after p2 dies : has_one_ref=1
ctor=1 dtor=1

$ g++ -std=c++20 -Wall -Wextra hw92b_bad.cpp -o hw92b_bad.out
hw92b_bad.cpp: In function 'int main()':
hw92b_bad.cpp:50:18: error: 'Flag::~Flag()' is private within this context
   50 |     delete p.get();   // 越权:撞 private 析构
      |                  ^
hw92b_bad.cpp:44:5: note: declared private here
   44 |     ~Flag() { ++dtor_count; }
      |     ^
```

完整代码（含 `RefCountedThreadSafe` 与 `scoped_refptr` 两个类）：

```cpp
#include <atomic>
#include <cstdio>

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
    explicit operator bool() const noexcept { return ptr_ != nullptr; }
private:
    T* ptr_ = nullptr;
};

struct Flag : RefCountedThreadSafe {
    static int ctor_count;
    static int dtor_count;
    Flag() { ++ctor_count; }
private:
    template <typename> friend class scoped_refptr;   // 只有它有权 delete
    ~Flag() { ++dtor_count; }
};
int Flag::ctor_count = 0;
int Flag::dtor_count = 0;

int main() {
    {
        auto p1 = scoped_refptr<Flag>(new Flag);
        std::printf("after 1st ref : has_one_ref=%d\n", p1.get()->has_one_ref());
        {
            auto p2 = p1;
            std::printf("after copy    : has_one_ref=%d\n", p1.get()->has_one_ref());
        }
        std::printf("after p2 dies : has_one_ref=%d\n", p1.get()->has_one_ref());
    }
    std::printf("ctor=%d dtor=%d\n", Flag::ctor_count, Flag::dtor_count);
    return 0;
}
```

## 9.2-C {#hw-9-2-c}

**难度 L3** · 题面见 [homework](01-homework.md#hw-9-2-c)

**思路**：三层骨架照教材拼：Flag（`RefCountedThreadSafe` + `AtomicFlag`）→ WeakReference（`scoped_refptr<Flag>` 壳）→ WeakPtr（`WeakReference` + 允许悬垂的 `T*`）→ factory 铸币。第 4 问的 ASan 反例是这题最值钱的一问——它把"最后成员"从条文变成血的教训。

1. `AtomicFlag::Set` 用 release、`IsSet` 用 acquire：失效方把"对象已不可用"发布出去，判活方 acquire 读——只要读到失效位，失效前的所有写都可见。这是无锁判活的全部底牌。→ 知识点：[WeakPtr 前置知识（二）：std::atomic 与 memory_order](../chrome/02_weak_ptr/full/pre-02-weak-ptr-atomic-and-memory-order.md)「回到 WeakPtr:AtomicFlag 的 release/acquire 配对」一节
2. `WeakPtr::get()` 守门链：`get() → ref_.IsValid() → flag_->IsValid() → !invalidated_.IsSet()`——一次 acquire-load，有效才给 `ptr_`，失效老实交 `nullptr`。→ 知识点：[WeakPtr 实战（二）：核心骨架与控制块](../chrome/02_weak_ptr/full/02-2-weak-ptr-core-skeleton-and-control-block.md)「get() 的守门链」一节
3. 共享 Flag 让"一次 invalidate 集体失效"白送：两个 WeakPtr 来自同一个 factory，同一枚 Flag 翻面，两个 `get()` 同时归零。→ 知识点：[WeakPtr 实战（三）：WeakPtrFactory 与「最后成员」惯用法](../chrome/02_weak_ptr/full/02-3-weak-ptr-factory-and-last-member.md)「WeakPtrFactory 内部那枚 Flag」一节
4. 最后成员：factory 后声明 → 最先析构 → 析构时自动 `Invalidate` 罩住其余成员的析构期。第 3 问的 `Controller` 出作用域后 `cwp` 判假，就是这条因果链。→ 知识点：同上「重头戏:『最后成员』惯用法」一节
5. 第 4 问反例（factory 放前面）：逆序析构时 `value_` 先析构（堆先释放），随后 `member` 析构经 WeakPtr 调 `touch()` 摸已释放的堆——ASan 当场点名 `heap-use-after-free`，栈直接指向 `Member::~Member`。factory 放对位置时 `observer` 先失效，这一摸根本不会发生。→ 知识点：同上「两条叠起来:为什么 factory 非得放最后」一节

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra hw92c.cpp -o hw92c.out && ./hw92c.out
alive : bool=1 x=42 get==&foo:1
invalidate: wp.get=(nil) wp2.get=(nil) was_invalidated=1
before scope exit: bool=1
after scope exit : bool=0 get=(nil)
$ clang++ -std=c++20 -Wall -Wextra hw92c.cpp -o hw92c_clang.out && ./hw92c_clang.out
alive : bool=1 x=42 get==&foo:1
invalidate: wp.get=(nil) wp2.get=(nil) was_invalidated=1
before scope exit: bool=1
after scope exit : bool=0 get=(nil)

$ g++ -std=c++20 -Wall -Wextra -fsanitize=address -g -O1 hw92c_bad.cpp -o hw92c_bad_asan.out && ./hw92c_bad_asan.out
=================================================================
==560==ERROR: AddressSanitizer: heap-use-after-free on address 0x730096be0030 at pc 0x5f2182f68af0 bp 0x7fff22732c40 sp 0x7fff22732c30
READ of size 4 at 0x730096be0030 thread T0
    #0 0x5f2182f68aef in BadController::touch() const /tmp/cpp-fix9/hw/hw92c_bad.cpp:154
    #1 0x5f2182f68aef in BadController::Member::~Member() /tmp/cpp-fix9/hw/hw92c_bad.cpp:161
    #2 0x5f2182f68aef in BadController::~BadController() /tmp/cpp-fix9/hw/hw92c_bad.cpp:151
    #3 0x5f2182f68aef in main /tmp/cpp-fix9/hw/hw92c_bad.cpp:171
0x730096be0030 is located 0 bytes inside of 4-byte region [0x730096be0030,0x730096be0034)
freed by thread T0 here:
    #1 0x5f2182f68737 in std::default_delete<int>::operator()(int*) const /usr/include/c++/16/bits/unique_ptr.h:92
    #2 0x5f2182f68737 in std::unique_ptr<int, std::default_delete<int> >::~unique_ptr() /usr/include/c++/16/bits/unique_ptr.h:408
    #3 0x5f2182f68737 in BadController::~BadController() /tmp/cpp-fix9/hw/hw92c_bad.cpp:151
SUMMARY: AddressSanitizer: heap-use-after-free /tmp/cpp-fix9/hw/hw92c_bad.cpp:154 in BadController::touch() const
```

（地址与 PID 每次运行都变，对的是结构：`READ of size 4`、释放方是 `unique_ptr` 析构、触发方是 `Member::~Member`。）

完整代码（三层 + factory + 四组验证驱动。三层实现与 Lab 共享头文件 `mini_weak_ptr.hpp` 逐字同构——Lab 版套在 `tamcpp::chrome` 命名空间里，这里按题面"单文件"要求裸写）：

```cpp
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <utility>

// ==================== Flag -> WeakReference -> WeakPtr 三层 + factory ====================
// 与 Lab 共享头文件 mini_weak_ptr.hpp 逐字同构(Lab 版套在 tamcpp::chrome 命名空间内,
// 这里按题面"单文件"要求裸写)。

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

// Flag:侵入式引用计数 + 原子失效位
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

// WeakReference:scoped_refptr<Flag> 壳,判活走一次 acquire-load
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

// WeakReferenceOwner:Flag 的唯一主人,析构自动 Invalidate
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

// WeakPtr:WeakReference + 允许悬垂的 T*
template <typename T>
class WeakPtr {
public:
    WeakPtr() = default;
    WeakPtr(std::nullptr_t) noexcept {}

    T* get() const noexcept { return ref_.IsValid() ? ptr_ : nullptr; }
    T& operator*() const { assert(ref_.IsValid()); return *ptr_; }
    T* operator->() const { assert(ref_.IsValid()); return ptr_; }
    explicit operator bool() const noexcept { return get() != nullptr; }
    void reset() noexcept { ref_.Reset(); ptr_ = nullptr; }
    bool maybe_valid() const noexcept { return ref_.MaybeValid(); }
    bool was_invalidated() const noexcept { return ptr_ && !ref_.IsValid(); }

private:
    friend class WeakPtrFactory<T>;
    WeakPtr(internal::WeakReference&& ref, T* ptr) noexcept
        : ref_(std::move(ref)), ptr_(ptr) {
        assert(ptr);
    }
    internal::WeakReference ref_;
    T* ptr_ = nullptr;
};

// WeakPtrFactory:铸币机,析构时自动失效所有已铸 WeakPtr
template <typename T>
class WeakPtrFactory {
public:
    WeakPtrFactory() = delete;
    explicit WeakPtrFactory(T* ptr) : ptr_(ptr) { assert(ptr); }
    WeakPtrFactory(const WeakPtrFactory&) = delete;
    WeakPtrFactory& operator=(const WeakPtrFactory&) = delete;

    WeakPtr<T> get_weak_ptr() const { return WeakPtr<T>(owner_.GetRef(), ptr_); }
    void invalidate_weak_ptrs() { owner_.Invalidate(); }
    bool has_weak_ptrs() const { return owner_.HasRefs(); }

private:
    internal::WeakReferenceOwner owner_;
    T* ptr_;
};

// ==================== 四组验证驱动 ====================

struct Foo { int x = 42; };

struct Controller {
    void touch() { ++state_; }
    WeakPtr<Controller> get_weak() { return weak_factory_.get_weak_ptr(); }
private:
    int state_ = 0;                          // 成员在前
    WeakPtrFactory<Controller> weak_factory_{this};   // factory 最后声明
};

int main() {
    // 1 对象活着:判真、取值正确、get() 等于对象地址
    Foo foo;
    WeakPtrFactory<Foo> foo_factory(&foo);
    WeakPtr<Foo> wp = foo_factory.get_weak_ptr();
    std::printf("alive : bool=%d x=%d get==&foo:%d\n",
                static_cast<bool>(wp) ? 1 : 0, wp->x, wp.get() == &foo ? 1 : 0);

    // 2 invalidate_weak_ptrs 一次失效所有已铸 WeakPtr
    WeakPtr<Foo> wp2 = foo_factory.get_weak_ptr();
    foo_factory.invalidate_weak_ptrs();
    std::printf("invalidate: wp.get=%p wp2.get=%p was_invalidated=%d\n",
                static_cast<void*>(wp.get()), static_cast<void*>(wp2.get()),
                wp.was_invalidated() ? 1 : 0);

    // 3 factory 析构(对象出作用域)→ 手中 WeakPtr 自动失效:"最后成员"惯用法
    WeakPtr<Controller> cwp;
    {
        Controller c;
        cwp = c.get_weak();
        c.touch();
        std::printf("before scope exit: bool=%d\n", static_cast<bool>(cwp) ? 1 : 0);
    }
    std::printf("after scope exit : bool=%d get=%p\n",
                static_cast<bool>(cwp) ? 1 : 0, static_cast<void*>(cwp.get()));
    return 0;
}
```

`BadController` 反例：把上面的三层实现原样拷进 `hw92c_bad.cpp`（补上 `<memory>`），`main` 换成下面这个"故意把 factory 声明挪到成员前面"的反例：

```cpp
// ==================== 反例:factory 声明挪到成员前面 ====================
class BadController {
public:
    BadController() { member.observer = weak_factory_.get_weak_ptr(); }
    int touch() const { return *value_; }   // 摸一个"可能已经析构"的堆资源

    WeakPtrFactory<BadController> weak_factory_{this};   // 先声明 → 最后析构

    struct Member {
        WeakPtr<BadController> observer;                 // 析构期还拿着 WeakPtr 的观察者
        ~Member() {
            if (observer) std::printf("teardown touch=%d\n", observer->touch());   // UAF
        }
    } member;

    std::unique_ptr<int> value_{std::make_unique<int>(42)};   // 后声明 → 先析构(先释放堆)
};

int main() {
    BadController bc;   // main 结束析构 bc:value_ 先析构、member 再经"仍有效"的 WeakPtr 摸它
    return 0;
}
```

要点：`member` 析构时 `weak_factory_` 还没析构，WeakPtr 判活为真，`touch()` 顺着"有效"的弱指针摸到已经析构的 `value_`——这正是"factory 放前面"留出来的那个窗口。factory 放最后时它最先析构、先失效，`if (observer)` 直接短路，摸都不摸。

## 9.3-A {#hw-9-3-a}

**难度 L2** · 题面见 [homework](01-homework.md#hw-9-3-a)

**思路**：计数统计"每次构造"。不透明比较器下 `find("alpha")` 的参数类型是 `CountingString`，`const char*` 得先隐式构造一个临时对象；透明比较器下 `const char*` 原样透传进比较，由混合 `operator<` 直接比 `std::string` 和 `const char*`，一个临时都不造。

1. 关键前提：给 `CountingString` 补上两枚混合 `operator<`（`CountingString` vs `const char*` 双向），这样透明路径的比较本身不需要转换——和 `std::string` 自带混合比较的处境对齐。→ 知识点：[flat_map 前置知识（三）：比较器、strict_weak_order 与透明查找](../chrome/03_flat_map/full/pre-03-flat-map-comparator-and-transparent.md)「透明查找:不构造临时对象」一节
2. 不透明 `std::less<CountingString>`：`find` 形参是 `CountingString`，`"alpha"` 先构造一个临时 `CountingString`（内部还要 `std::string` 一次堆分配）→ 构造计数 +1。→ 知识点：同上「std::less vs std::less<>:不透明 vs 透明」一节
3. 透明 `std::less<>`：`find` 直接拿 `const char*` 去二分，比较走混合 `operator<`，全程零构造 → +0。这正是 flat_map 默认 `Compare = std::less<>` 的原因。→ 知识点：同上「flat_map 怎么实现透明:KeyT 的编译期分流」一节、[flat_map 实战（一）：动机与接口设计](../chrome/03_flat_map/full/03-1-flat-map-motivation-and-api-design.md)「为什么默认比较器是透明的 std::less<>」一节

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra hw93a.cpp -o hw93a.out && ./hw93a.out
opaque     : found=1 constructions=1
transparent: found=1 constructions=0
```

完整代码：

```cpp
#include <cstdio>
#include <functional>
#include <map>
#include <string>

struct CountingString {
    static int made;   // 构造总次数
    std::string s;
    CountingString(const char* p) : s(p) { ++made; }
    CountingString(const CountingString& o) : s(o.s) { ++made; }
    CountingString& operator=(const CountingString&) = default;
};
int CountingString::made = 0;

bool operator<(const CountingString& a, const CountingString& b) { return a.s < b.s; }
bool operator<(const CountingString& a, const char* b) { return a.s < b; }
bool operator<(const char* a, const CountingString& b) { return a < b.s; }

int main() {
    {
        std::map<CountingString, int, std::less<CountingString>> m;   // 不透明
        m.emplace("alpha", 1);
        m.emplace("beta", 2);
        int before = CountingString::made;
        auto it = m.find("alpha");
        std::printf("opaque     : found=%d constructions=%d\n",
                    it != m.end() ? 1 : 0, CountingString::made - before);
    }
    {
        std::map<CountingString, int, std::less<>> m;                 // 透明
        m.emplace("alpha", 1);
        m.emplace("beta", 2);
        int before = CountingString::made;
        auto it = m.find("alpha");
        std::printf("transparent: found=%d constructions=%d\n",
                    it != m.end() ? 1 : 0, CountingString::made - before);
    }
    return 0;
}
```

## 9.3-B {#hw-9-3-b}

**难度 L3** · 题面见 [homework](01-homework.md#hw-9-3-b)

**思路**：`flat_tree` 的四个模板参数里，`GetKeyFromValue` 是灵魂——同一份代码戴 `GetFirst` 帽子就是 map，换 `std::identity` 就是 set。插入没有摊还这件事，从 shift 曲线里看得最清楚。

1. `sort_and_unique`：`stable_sort` 按提取出的 key 排序（等价元素保相对顺序），`unique` 用 `!comp(a,b) && !comp(b,a)` 判等（等价类定义，比直接 `==` 更稳），`erase` 截尾。→ 知识点：[flat_map 实战（二）：flat_tree 核心骨架](../chrome/03_flat_map/full/03-2-flat-map-flattree-skeleton.md)「有序不变量:每次 mutation 后保持有序 + 唯一」一节
2. `find` = `lower_bound` 二分 + 判等一步；`insert` = `lower_bound` 找位 + 重复拒绝 + `emplace`（shift）。→ 知识点：[flat_map 实战（三）：查找与插入](../chrome/03_flat_map/full/03-3-flat-map-lookup-and-insert.md)「查找:lower_bound O(lg n)」「插入:lower_bound + emplace,O(n) shift」两节
3. shift 实测：头部插 10 万次 283 ms vs 尾部 0 ms——`push_back` 摊还 O(1)（扩容翻倍、偶尔一次 O(n) 摊平），而 flat_map 每次插入都实打实挪 O(n)，**没有偶尔一次大代价可摊**，所以单次与摊还都是 O(n)。→ 知识点：[flat_map 前置知识（二）：复杂度与摊还分析](../chrome/03_flat_map/full/pre-02-flat-map-complexity-and-amortized.md)「flat_map 单元素插入没有摊还优惠」一节

**验证输出**（本机实测 283 ms，与教材 264 ms 同量级，机器差异正常）：

```text
$ g++ -std=c++20 -Wall -Wextra -O2 hw93b.cpp -o hw93b.out && ./hw93b.out
size=4 keys: 1 2 3 5
find(2): hit
find(9): miss
insert(4): inserted=1
insert(4) again: inserted=0
emplace(begin) x100000: 283 ms
push_back      x100000: 0 ms
```

完整代码：

```cpp
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace internal {

template <class Key, class GetKeyFromValue, class KeyCompare, class Container>
class flat_tree {
public:
    using value_type = typename Container::value_type;
    using iterator = typename Container::iterator;
    using const_iterator = typename Container::const_iterator;

    flat_tree(Container data, KeyCompare comp = KeyCompare())
        : body_(std::move(data)), comp_(comp) {
        sort_and_unique();
    }

    const_iterator find(const Key& key) const {
        auto it = std::lower_bound(
            body_.begin(), body_.end(), key,
            [&](const value_type& v, const Key& k) { return comp_(GetKeyFromValue{}(v), k); });
        if (it != body_.end() && !comp_(key, GetKeyFromValue{}(*it))) return it;
        return body_.end();
    }

    std::pair<iterator, bool> insert(value_type v) {
        const Key& key = GetKeyFromValue{}(v);
        auto it = std::lower_bound(body_.begin(), body_.end(), key,
            [&](const value_type& e, const Key& k) { return comp_(GetKeyFromValue{}(e), k); });
        if (it != body_.end() && !comp_(key, GetKeyFromValue{}(*it))) return {it, false};
        return {body_.emplace(it, std::move(v)), true};
    }

    std::size_t size() const { return body_.size(); }
    iterator begin() { return body_.begin(); }
    iterator end() { return body_.end(); }

private:
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
    Container body_;
    [[no_unique_address]] KeyCompare comp_;
};

}  // namespace internal

struct GetFirst {
    template <class K, class V>
    constexpr const K& operator()(const std::pair<K, V>& p) const { return p.first; }
};

template <class K, class V>
using mini_flat_map = internal::flat_tree<K, GetFirst, std::less<>,
                                          std::vector<std::pair<K, V>>>;

int main() {
    // 1 无序带重复构造 → 升序去重
    mini_flat_map<int, std::string> m{std::vector<std::pair<int, std::string>>{
        {3, "c"}, {1, "a"}, {2, "b"}, {1, "dup"}, {5, "e"}, {2, "dup2"}}};
    std::printf("size=%zu keys:", m.size());
    for (auto it = m.begin(); it != m.end(); ++it)
        std::printf(" %d", it->first);
    std::printf("\n");

    // 2 查找:命中 / 未命中
    std::printf("find(2): %s\n", m.find(2) != m.end() ? "hit" : "miss");
    std::printf("find(9): %s\n", m.find(9) != m.end() ? "hit" : "miss");

    // 3 插入拒绝重复 key
    auto [it, ok] = m.insert({4, "d"});
    std::printf("insert(4): inserted=%d\n", ok ? 1 : 0);
    auto [it2, ok2] = m.insert({4, "again"});
    std::printf("insert(4) again: inserted=%d\n", ok2 ? 1 : 0);

    // 4 shift 曲线
    constexpr int N = 100'000;
    {
        std::vector<int> a;
        auto t1 = std::chrono::steady_clock::now();
        for (int i = 0; i < N; ++i) a.emplace(a.begin(), i);
        auto t2 = std::chrono::steady_clock::now();
        std::printf("emplace(begin) x%d: %lld ms\n", N,
            static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count()));
    }
    {
        std::vector<int> b;
        auto t3 = std::chrono::steady_clock::now();
        for (int i = 0; i < N; ++i) b.push_back(i);
        auto t4 = std::chrono::steady_clock::now();
        std::printf("push_back      x%d: %lld ms\n", N,
            static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(t4 - t3).count()));
    }
    return 0;
}
```

## 9.3-C {#hw-9-3-c}

**难度 L3** · 题面见 [homework](01-homework.md#hw-9-3-c)

**思路**：tag dispatch 的全部机制就是一个空 struct + 一个 `inline constexpr` 实例——重载决议期挑函数，运行期一分钱不收。撒谎的下场分 debug/release 两个世界。

1. `sorted_unique_t` 是空标签，`sorted_unique` 是常量实例；普通构造排序去重、sorted_unique 构造跳过排序只做 `assert(is_sorted_and_unique())`。→ 知识点：[flat_map 前置知识（四）：tag dispatch 与 sorted_unique_t](../chrome/03_flat_map/full/pre-04-flat-map-tag-dispatch-and-sorted-unique.md)「tag dispatch:用类型挑函数」一节
2. 撒谎（`{1,3,2}` 冒充有序）：debug 下 `assert` 炸在第 17 行 `is_sorted_unique()`，SIGABRT、退出码 134。→ 知识点：同上「DCHECK(is_sorted_and_unique):debug 抓撒谎的人」一节、[flat_map 实战（四）：sorted_unique 构造优化](../chrome/03_flat_map/full/03-4-flat-map-sorted-unique-construction.md)「DCHECK(is_sorted_and_unique):诚实契约」一节
3. `-DNDEBUG` 下 `assert` 整体消失，同一个撒谎程序"活"了下来并照单全收——这就是诚实契约：您用 O(N) 构造换"数据确实有序"的承诺，debug 替您把关、release 完全信任。契约两头：容器给性能，调用方给诚实。→ 知识点：[flat_map 前置知识（四）](../chrome/03_flat_map/full/pre-04-flat-map-tag-dispatch-and-sorted-unique.md)「零成本:release 完全不付费」一节

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra hw93c.cpp -o hw93c_debug.out && ./hw93c_debug.out
hw93c_debug.out: hw93c.cpp:17: MiniMap::MiniMap(sorted_unique_t, std::vector<int>): Assertion `is_sorted_unique()' failed.
Aborted
debug run exit=134

$ g++ -std=c++20 -Wall -Wextra -DNDEBUG hw93c.cpp -o hw93c_rel.out && ./hw93c_rel.out
  [normal ctor] sorted+uniqued
normal: size=3 first=1
  [sorted_unique ctor] no sort
sorted_unique(honest): size=4 first=1
  [sorted_unique ctor] no sort
sorted_unique(lying): size=3 first=1
```

完整代码：

```cpp
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <vector>

struct sorted_unique_t {};
inline constexpr sorted_unique_t sorted_unique{};

class MiniMap {
public:
    explicit MiniMap(std::vector<int> data) : data_(std::move(data)) {
        std::sort(data_.begin(), data_.end());
        data_.erase(std::unique(data_.begin(), data_.end()), data_.end());
        std::printf("  [normal ctor] sorted+uniqued\n");
    }
    MiniMap(sorted_unique_t, std::vector<int> data) : data_(std::move(data)) {
        assert(is_sorted_unique());   // debug 校验,不排序
        std::printf("  [sorted_unique ctor] no sort\n");
    }
    std::size_t size() const { return data_.size(); }
    int first() const { return data_.front(); }

private:
    bool is_sorted_unique() const {
        for (std::size_t i = 1; i < data_.size(); ++i)
            if (!(data_[i - 1] < data_[i])) return false;
        return true;
    }
    std::vector<int> data_;
};

int main() {
    MiniMap a{std::vector<int>{3, 1, 2, 1}};
    std::printf("normal: size=%zu first=%d\n", a.size(), a.first());

    MiniMap b(sorted_unique, std::vector<int>{1, 2, 3, 4});
    std::printf("sorted_unique(honest): size=%zu first=%d\n", b.size(), b.first());

    MiniMap c(sorted_unique, std::vector<int>{1, 3, 2});   // 撒谎:debug 下 abort
    std::printf("sorted_unique(lying): size=%zu first=%d\n", c.size(), c.first());
    return 0;
}
```

## 9.4-A {#hw-9-4-a}

**难度 L1** · 题面见 [homework](01-homework.md#hw-9-4-a)

**思路**：placement new 管"构造"，`= default` 析构管"不析构"，`alignas(T)` 管"对齐"——三样凑齐就是 NoDestructor 的最小复刻。

1. 编译器生成的 `~MiniNoDestructor()` 只析构**成员**，而它唯一的成员是 `char storage_[...]`——`char` 的析构平凡，什么都不做。`Noisy` 是后来用 placement new "长"在 `storage_` 上的，与 `storage_` 在类型系统里的身份无关，编译器不会想起它。所以只打印 `Noisy()`，`~Noisy()` 永不打印。→ 知识点：[NoDestructor 前置知识（一）：placement new 与对齐存储](../chrome/04_no_destructor/full/pre-01-placement-new-and-aligned-storage.md)「手动生命周期:构造了不析构」一节
2. `alignof(Noisy)=4`；打印 `buf` 起始地址往后 0/1/2/3 字节各偏移对 4 的余数，必有一个非 0——把 placement new 怼到那个非 0 偏移上就是未对齐访问，未定义行为。`alignas(T)` 就是把这块 `char` 数组的对齐提到 T 那一档，让 `new (storage_) T(...)` 永远踩在合法地址上。→ 知识点：同上「对齐:alignof 与 alignas」一节

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra hw94a.cpp -o hw94a.out && ./hw94a.out
Noisy()
value=42
alignof(Noisy)=4
addr offsets mod alignof: 0 1 2 3 (必有一个非 0,即错位)
(program exiting; ~Noisy was never printed)
```

完整代码：

```cpp
#include <cstdint>
#include <cstdio>
#include <new>
#include <utility>

template <typename T>
class MiniNoDestructor {
public:
    template <typename... Args>
    explicit MiniNoDestructor(Args&&... args) {
        new (storage_) T(std::forward<Args>(args)...);
    }
    MiniNoDestructor(const MiniNoDestructor&) = delete;
    ~MiniNoDestructor() = default;   // 只析构 char 成员,不碰 T
    T& operator*() { return *get(); }
    T* operator->() { return get(); }
    T* get() { return reinterpret_cast<T*>(storage_); }
    const T& operator*() const { return *get(); }
    const T* operator->() const { return get(); }
    const T* get() const { return reinterpret_cast<const T*>(storage_); }
private:
    alignas(T) char storage_[sizeof(T)];
};

struct Noisy {
    Noisy() { std::puts("Noisy()"); }
    ~Noisy() { std::puts("~Noisy()"); }
    int v = 42;
};

int main() {
    {
        static const MiniNoDestructor<Noisy> nd;
        std::printf("value=%d\n", nd->v);
    }
    std::printf("alignof(Noisy)=%zu\n", alignof(Noisy));
    alignas(1) unsigned char buf[sizeof(Noisy) + 4];
    auto base = reinterpret_cast<std::uintptr_t>(&buf[0]);
    std::printf("addr offsets mod alignof: %zu %zu %zu %zu (必有一个非 0,即错位)\n",
                base % alignof(Noisy), (base + 1) % alignof(Noisy),
                (base + 2) % alignof(Noisy), (base + 3) % alignof(Noisy));
    std::puts("(program exiting; ~Noisy was never printed)");
    return 0;
}
```

## 9.4-B {#hw-9-4-b}

**难度 L2** · 题面见 [homework](01-homework.md#hw-9-4-b)

**思路**：16 线程并发首调，计数必须是 1——这层保证**不是** NoDestructor 给的，是 C++11 magic statics 给的：函数局部静态变量首次初始化时，其他并发进入的线程会被挡住等它完成。

1. 机制名：**magic statics（魔法静态）**，语言保证；GCC/Clang 底下的实现手段是 `__cxa_guard_acquire/__cxa_guard_release` 那套守卫。NoDestructor 自己一行锁都没加，它对线程安全的贡献是零——功劳全在"函数局部静态"这个用法上。→ 知识点：[NoDestructor 前置知识（零）：静态存储期、初始化与析构](../chrome/04_no_destructor/full/pre-00-static-storage-and-init.md)「magic statics:C++11 的线程安全保证」一节
2. 变式（每次 `make_unique` 一个新对象）：计数 16——每次调用都真的构造一次，`s` 只构造一次的语义来自"函数局部静态"，换掉它魔法就没了。这也是教材反复强调"包在函数局部静态里"的原因：这个姿势同时干了绕开全局构造器和把线程安全转交 magic statics 两件事。→ 知识点：[NoDestructor 实战（三）：何时用、何时不用](../chrome/04_no_destructor/full/04-3-no-destructor-when-to-use.md)「magic statics 复盘:线程安全靠它,不靠 NoDestructor」一节

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra -pthread hw94b.cpp -o hw94b.out && ./hw94b.out
function-local static: ctor_count=1
heap-per-call        : ctor_count=16
```

完整代码：

```cpp
#include <atomic>
#include <cstdio>
#include <memory>
#include <thread>
#include <vector>

struct SharedCounter {
    SharedCounter() { ctor_count.fetch_add(1, std::memory_order_relaxed); }
    inline static std::atomic<int> ctor_count{0};
};

const SharedCounter& GetSharedCounter() {
    static const SharedCounter s;   // magic statics:并发首调只构造一次
    return s;
}

int main() {
    {
        std::vector<std::thread> ts;
        for (int i = 0; i < 16; ++i)
            ts.emplace_back([] { (void)GetSharedCounter(); });
        for (auto& t : ts) t.join();
        std::printf("function-local static: ctor_count=%d\n",
                    SharedCounter::ctor_count.load());
    }
    SharedCounter::ctor_count.store(0);
    {
        std::vector<std::thread> ts;
        for (int i = 0; i < 16; ++i)
            ts.emplace_back([] {
                auto p = std::make_unique<SharedCounter>();   // 变式:每次造一个新的
                (void)p;
            });
        for (auto& t : ts) t.join();
        std::printf("heap-per-call        : ctor_count=%d\n",
                    SharedCounter::ctor_count.load());
    }
    return 0;
}
```

## 9.C-1 {#hw-9-c-1}

**难度 L3** · 题面见 [homework](01-homework.md#hw-9-c-1)

**思路**：`bind_weak_once` 就是 `InvokeHelper<true>::MakeItSo` 的教学翻译——`if (!receiver) return;` 一行对应工业版取消点的全部语义。第 3 问考的是 IsValid 与 MaybeValid 的边界。

1. 执行前判活：`!receiver` 走 `operator bool → get() → ref_.IsValid() → 原子 acquire-load`，一整条守门链。→ 知识点：[WeakPtr 实战（五）：与回调集成——关闭 OnceCallback 的环](../chrome/02_weak_ptr/full/02-5-weak-ptr-bind-integration.md)「关键:检查走 IsValid,不是 MaybeValid」一节
2. 对象死后：factory 析构先失效所有 WeakPtr，回调里 `if (!receiver) return;` 静默退出——"事后安全 no-op"，不是 `Unretained` 那种"事后报警 UAF"。→ 知识点：同上「vs Unretained(this):事后安全 no-op vs 事后报警 UAF」一节
3. 走 `get()` 而不是 `maybe_valid()`：`get()` 在绑定序列上给出**确定性**结果，判活通过 ⇒ 此刻对象真的活着，可以安全 deref；`maybe_valid()` 是跨序列的乐观 hint，正面结果不可信——拿它当 deref 的通行证迟早翻车。→ 知识点：[WeakPtr 实战（四）：序列亲和性与 lazy 绑定](../chrome/02_weak_ptr/full/02-4-weak-ptr-sequence-affinity-and-lazy-binding.md)「IsValid vs MaybeValid:同序列准 vs 跨序列 hint」一节

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra hw9c1.cpp -o hw9c1.out && ./hw9c1.out
got 42
(done; the second call printed nothing)
```

核心代码（WeakPtr 三层复用 9.2-C 的骨架，这里给出 `bind_weak_once` 与驱动）：

```cpp
// bind_weak_once:成员方法 + WeakPtr receiver → std::function<void()>
// 对应 InvokeHelper<true>::MakeItSo 的取消点 if (!target) return;
template <typename T, typename... Bound>
std::function<void()> bind_weak_once(void (T::*method)(Bound...),
                                     WeakPtr<T> receiver,
                                     Bound... bound) {
    return [method, receiver = std::move(receiver),
            bound = std::make_tuple(std::move(bound)...)]() mutable {
        if (!receiver) return;   // ← 取消点:对象死后静默 no-op
        std::apply(
            [&](auto&&... args) { (receiver.get()->*method)(args...); },
            bound);
    };
}

struct Controller {
    void on_work_done(int v) { std::printf("got %d\n", v); }
    WeakPtr<Controller> get_weak() { return weak_factory_.get_weak_ptr(); }
private:
    WeakPtrFactory<Controller> weak_factory_{this};   // 最后成员
};

int main() {
    std::function<void()> task;
    {
        Controller c;
        task = bind_weak_once(&Controller::on_work_done, c.get_weak(), 42);
        task();   // 对象活着 → got 42
    }             // c 析构 → factory 先失效所有 WeakPtr
    task();       // 对象已死 → 静默 no-op,什么都不打印
    std::puts("(done; the second call printed nothing)");
    return 0;
}
```

## 9.C-2 {#hw-9-c-2}

**难度 L4** · 题面见 [homework](01-homework.md#hw-9-c-2)

**思路**：这一题复刻 `bind_internal.h` 的编译期接线（源码出处：Chromium `base/functional/bind_internal.h` 的 `kIsWeakMethod`/`IsWeakReceiver`/`WeakCallReturnsVoid`，教材 02-5 全文引用）。三块拼图：类型特征认出 WeakPtr、编译期开关选分支、static_assert 强制 void。

1. `is_instantiation_of<Template, T>` 判"T 是不是 `Template<...>` 的实例化"，`IsWeakReceiver<T>` 拿它判 `WeakPtr`。→ 知识点：[WeakPtr 实战（五）](../chrome/02_weak_ptr/full/02-5-weak-ptr-bind-integration.md)「编译期接线:kIsWeakMethod / IsWeakReceiver」一节
2. `kIsWeakMethod<true, T, Args...>` 只在"成员方法 + receiver 是 WeakPtr"时为真；`bind` 里 `constexpr bool is_weak = kIsWeakMethod<true, DecayedReceiver>` 在编译期选定 `InvokeHelper<true/false>` 分派。→ 知识点：同上「编译期接线」「调用期分派:InvokeHelper\<true\>::MakeItSo」两节
3. 强制 void：`static_assert(!is_weak || std::is_void_v<Ret>)`——weak 调用取消时执行的是 `return;`（没值），方法带返回值的话取消那一刻无值可返，所以签名层面直接拒绝。真实报错如下。→ 知识点：同上「弱调用强制 void 返回」一节
4. "先 Unwrap 再判活"：`make_it_so` 里先取出 receiver、再 `if (!receiver) return;`——对允许跨线程、在 `Unwrap` 里 `Lock()` 的弱指针实现，先判活再 Unwrap 两步之间会开一道 race 的缝。本卷 `WeakPtr` 的 `Unwrap` 是透传所以无碍，但通用模板要守这条契约。→ 知识点：同上「"先 Unwrap 再判活"的 race 防御」一节

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra hw9c2.cpp -o hw9c2.out && ./hw9c2.out
weak got 7
(object died; the task would silently no-op now)
weak got 8

$ g++ -std=c++20 -Wall -Wextra hw9c2_bad.cpp -o hw9c2_bad.out
hw9c2_bad.cpp: In instantiation of 'std::function<void()> bind(M, Receiver&&, Bound&& ...) [with M = int (Controller::*)(); Receiver = WeakPtr<Controller>; Bound = {}]':
hw9c2_bad.cpp:159:21:   required from here
   159 |     auto task = bind(&Controller::get_value, c.get_weak());   // 应编译失败
      |                 ~~~~^~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
hw9c2_bad.cpp:136:28: error: static assertion failed: weak call must return void: cancellation has no value to return
  136 |     static_assert(!is_weak || std::is_void_v<typename method_traits<M>::Ret>,
      |                   ~~~~~~~~~^~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  • '((!(bool)is_weak) || ((bool)std::is_void_v<int>))' evaluates to false
```

核心代码（类型特征 + 双分派 + 统一入口）：

```cpp
// 特征 1:ToCheck 是不是 Template<...> 的实例化
template <template <typename...> class Template, typename T>
struct is_instantiation_of : std::false_type {};

template <template <typename...> class Template, typename... Params>
struct is_instantiation_of<Template, Template<Params...>> : std::true_type {};

// 特征 2:receiver 是不是 WeakPtr<?>
template <typename T>
struct IsWeakReceiver : is_instantiation_of<WeakPtr, T> {};

static_assert(IsWeakReceiver<WeakPtr<int>>::value);
static_assert(!IsWeakReceiver<int*>::value);
static_assert(!IsWeakReceiver<int>::value);

// 编译期开关:成员方法 + receiver 是 WeakPtr 才走 weak 分支
template <bool is_method, typename... Args>
inline constexpr bool kIsWeakMethod = false;

template <typename T, typename... Args>
inline constexpr bool kIsWeakMethod<true, T, Args...> = IsWeakReceiver<std::decay_t<T>>::value;

// 成员方法返回类型拆解
template <typename Sig>
struct method_traits;

template <typename R, typename T, typename... Args>
struct method_traits<R (T::*)(Args...)> {
    using Ret = R;
    using Class = T;
};

// 调用期分派:InvokeHelper<true/false>
template <bool is_weak_call>
struct InvokeHelper;

template <>
struct InvokeHelper<true> {
    template <typename M, typename Receiver, typename Tuple>
    static void make_it_so(M method, const Receiver& receiver, Tuple& bound) {
        // 先 Unwrap(取出 receiver),再判活——顺序不能反
        if (!receiver) return;   // ← 取消点
        std::apply(
            [&](auto&&... args) {
                (receiver.get()->*method)(std::forward<decltype(args)>(args)...);
            },
            bound);
    }
};

template <>
struct InvokeHelper<false> {
    template <typename M, typename Receiver, typename Tuple>
    static void make_it_so(M method, Receiver receiver, Tuple& bound) {
        std::apply(
            [&](auto&&... args) {
                (receiver->*method)(std::forward<decltype(args)>(args)...);
            },
            bound);
    }
};

// 统一入口 bind:编译期选分支
template <typename M, typename Receiver, typename... Bound>
std::function<void()> bind(M method, Receiver&& receiver, Bound&&... bound) {
    using DecayedReceiver = std::decay_t<Receiver>;
    constexpr bool is_weak = kIsWeakMethod<true, DecayedReceiver>;

    static_assert(!is_weak || std::is_void_v<typename method_traits<M>::Ret>,
                  "weak call must return void: cancellation has no value to return");

    return [method, receiver = std::forward<Receiver>(receiver),
            bound = std::make_tuple(std::forward<Bound>(bound)...)]() mutable {
        InvokeHelper<is_weak>::make_it_so(method, receiver, bound);
    };
}
```

## 9.C-3 {#hw-9-c-3}

**难度 L5** · 题面见 [homework](01-homework.md#hw-9-c-3)

**思路**：Vyukov 经典 MPSC（stub 哨兵；节点一次性、不回收，无需断链）只有两个函数：`push` 用 `exchange` + release 接链；`pop` 读 next、遇空时用 head 比对识别"push 在途"、必要时把哨兵重新推回去。每个内存序都有一句话的账：

1. `push` 里 `n->next.store(relaxed)`：节点还没发布，只有自己看得到，不需要同步。→ 知识点：[WeakPtr 前置知识（二）：std::atomic 与 memory_order](../chrome/02_weak_ptr/full/pre-02-weak-ptr-atomic-and-memory-order.md)「relaxed:只保证原子,不传递信息」一节
2. `push` 里 `head_.exchange(n, acq_rel)`：一次 RMW 同时"读到旧栈顶"与"发布新栈顶"——acquire 一侧看到别的生产者的最新 head，release 一侧让消费端看见新节点。→ 知识点：同上「六种 memory order」表（acq_rel 行）
3. `push` 里 `prev->next.store(n, release)`：把"节点已入队"连同节点里的任务载荷一起发布；消费端的 acquire 读到这里，就同时看到了全部载荷——这是"无锁也能安全取任务"的根。→ 知识点：同上「acquire / release:建立 happens-before」一节
4. `pop` 里 `next.load(acquire)` 与 head 的 acquire 读：前者接住生产者的 release；后者识别"交换了 head 但还没接链"的在途 push（此时 `tail != head`，返回空重试）。→ 知识点：同上「回到 WeakPtr:AtomicFlag 的 release/acquire 配对」一节（同一对 pattern）
5. 取消令牌：`dead_token->invalidate()` 用 release、`is_valid()` 用 acquire，消费端先查令牌再执行——失效任务跳过，计数恰好 90000。→ 知识点：[OnceCallback 实战（四）：取消令牌设计](../chrome/01_once_callback/full/01-4-once-callback-cancellation-token.md)「acquire/release 这对配子」一节

**验证输出**（普通 -O2、TSan、ASan/UBSan 三份构建全部零报告，计数一分不差）：

```text
$ g++ -std=c++23 -Wall -Wextra -O2 hw9c3.cpp -o hw9c3.out && ./hw9c3.out
executed=90000 expected=90000

$ g++ -std=c++23 -Wall -Wextra -fsanitize=thread -O1 -g hw9c3.cpp -o hw9c3_tsan.out && ./hw9c3_tsan.out
executed=90000 expected=90000    ← 无任何 TSan 报告

$ g++ -std=c++23 -Wall -Wextra -fsanitize=address,undefined -O1 -g hw9c3.cpp -o hw9c3_asan.out && ./hw9c3_asan.out
executed=90000 expected=90000    ← 无任何 ASan/UBSan 报告
```

核心代码（完整文件见题面要求，队列与压测骨架）：

```cpp
// ---- CancelableToken(01-4 设计)----
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

// ---- 队列节点:任务 + 原子 next ----
struct Node {
    std::atomic<Node*> next{nullptr};
    std::move_only_function<void()> fn;
    std::shared_ptr<CancelableToken> token;
};

// ---- Vyukov 经典侵入式 MPSC(stub 哨兵;节点一次性、不回收,无需断链)----
class MpscQueue {
public:
    MpscQueue() : head_(&stub_), tail_(&stub_) {
        stub_.next.store(nullptr, std::memory_order_relaxed);
    }

    // 多生产者并发调用
    void push(Node* n) {
        n->next.store(nullptr, std::memory_order_relaxed);
        // acq_rel:exchange 同时获得旧栈顶、发布新栈顶
        Node* prev = head_.exchange(n, std::memory_order_acq_rel);
        // release:把"节点已入队"发布给消费端;消费端的 acquire 读到这里即见全部载荷
        prev->next.store(n, std::memory_order_release);
    }

    // 仅消费线程调用;返回 nullptr 表示"空,或 push 在途,稍后重试"
    Node* pop() {
        Node* t = tail_;
        Node* next = t->next.load(std::memory_order_acquire);   // 发布点
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
        Node* h = head_.load(std::memory_order_acquire);
        if (t != h) return nullptr;   // push 在途(交换了 head、还没接链)
        push(&stub_);                 // 队列空了:把哨兵重新推回去
        next = t->next.load(std::memory_order_acquire);
        if (next != nullptr) {
            tail_ = next;
            return t;
        }
        return nullptr;
    }

private:
    std::atomic<Node*> head_;
    Node* tail_ = nullptr;   // 消费端私有
    Node stub_;
};

int main() {
    constexpr int kProducers = 4;
    constexpr int kPerProducer = 25'000;   // 共 10 万任务
    constexpr int kTotal = kProducers * kPerProducer;

    // 每 10 个任务里 1 个挂"已取消"令牌 → 预期执行 90000
    auto alive_token = std::make_shared<CancelableToken>();
    auto dead_token = std::make_shared<CancelableToken>();
    dead_token->invalidate();

    std::atomic<int> executed{0};
    MpscQueue q;
    std::vector<Node*> nodes;   // 节点含 atomic,不可移动:用裸指针逐个分配
    nodes.reserve(kTotal);
    for (int i = 0; i < kTotal; ++i) {
        auto* n = new Node();
        n->token = (i % 10 == 0) ? dead_token : alive_token;
        n->fn = [&executed] { executed.fetch_add(1, std::memory_order_relaxed); };
        nodes.push_back(n);
    }

    std::vector<std::thread> producers;
    for (int p = 0; p < kProducers; ++p) {
        producers.emplace_back([&q, &nodes, p] {
            for (int i = 0; i < kPerProducer; ++i)
                q.push(nodes[p * kPerProducer + i]);
        });
    }

    std::thread consumer([&q, kTotal] {
        int popped = 0;
        while (popped < kTotal) {
            Node* n = q.pop();
            if (n == nullptr) continue;   // 空 / push 在途:再试
            if (n->token && n->token->is_valid()) n->fn();
            ++popped;
        }
    });

    for (auto& t : producers) t.join();
    consumer.join();

    std::printf("executed=%d expected=%d\n",
                executed.load(std::memory_order_relaxed), kTotal - kTotal / 10);
    for (auto* n : nodes) delete n;   // 收尾回收
    return 0;
}
```

两个细节值得记一笔：节点含 `std::atomic` 所以不可移动，`vector<Node>` 会编译失败，堆节点用裸指针逐个分配；`pop` 返回 nullptr 有两种含义（真空/在途），消费端轮询重试即可，因为总数是确定的上限。

