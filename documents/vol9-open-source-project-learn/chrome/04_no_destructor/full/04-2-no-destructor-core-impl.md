---
chapter: 1
cpp_standard:
- 11
- 17
- 20
description: 实现 NoDestructor 核心——alignas storage_ + placement new 构造 + reinterpret_cast 访问
  + =default 析构跳过 ~T() + static_assert 把关
difficulty: intermediate
order: 2
platform: host
prerequisites:
- NoDestructor 实战（一）：动机与接口设计
- NoDestructor 前置知识（一）：placement new 与对齐存储
reading_time_minutes: 10
related:
- NoDestructor 实战（三）：何时用、何时不用
tags:
- host
- cpp-modern
- intermediate
- 内存管理
- RAII
title: "NoDestructor 实战（二）：核心实现"
---
# NoDestructor 实战（二）：核心实现

上一篇咱们把 NoDestructor 的目标 API 敲定了。接下来动手撸实现——笔者这里直接说一句,核心就 [前置知识（一）](./pre-01-placement-new-and-aligned-storage.md) 里那两个老伙计:placement new 加对齐存储。再叠一层刻意的"不析构"策略,外加两条 static_assert 把关,就齐了。整个类拢共不到 50 行,可每一行笔者都得跟您掰扯清楚它的理由——这玩意儿看着短,踩坑点一个不少。

## storage_:一段对齐好的内联缓冲

```cpp
// Platform: host | C++ Standard: C++20
#pragma once
#include <new>
#include <type_traits>
#include <utility>

namespace tamcpp::chrome {

template <typename T>
class NoDestructor {
    // 详细见下
private:
    alignas(T) char storage_[sizeof(T)];
};

}  // namespace tamcpp::chrome
```

整个类的状态就这么一行:`alignas(T) char storage_[sizeof(T)]`(no_destructor.h:122)。笔者第一次读的时候愣了一下——就这么个 char 数组,没了?还真就没了。`sizeof(T)` 个字节,容量刚好装一个 T;前面挂个 `alignas(T)`,是把数组的对齐拉到 T 那一档,否则 placement new 拿到的地址可能没对齐,直接撞向未定义行为。私有,外部谁都碰不到原始存储,只能乖乖走下面那几个 `get()`、`operator*`。

说穿了,NoDestructor 全部"数据"就是这一段对齐好的内联缓冲。没指针、没堆分配、没额外开销。后面那些花活,都建在这块地基上。

## 构造:placement new 加完美转发

```cpp
public:
    // 通用:从任意参数完美转发给 T 的构造函数
    template <typename... Args>
    explicit NoDestructor(Args&&... args) {
        new (storage_) T(std::forward<Args>(args)...);
    }

    // 从 T 直接拷贝/移动构造(方便 initializer_list 等场景)
    explicit NoDestructor(const T& x) { new (storage_) T(x); }
    explicit NoDestructor(T&& x)      { new (storage_) T(std::move(x)); }
```

通用构造这块笔者不多解释,`template<typename... Args>` 加 `std::forward<Args>(args)...`,标准完美转发,把参数原封不动甩给 T 的构造函数。`new (storage_) T(...)` 是 placement new,在 `storage_` 那块内存上原地构造 T——不分配内存,内存本身就是 `storage_`。

笔者真正想多嘴一句的是下面那两个看似多余的 `const T&` / `T&&` 重载。您可能跟笔者第一次一样纳闷:通用模板不是已经包打天下了吗,干嘛还单写这俩?坑就在 initializer_list 上。有些初始化场景得从一个现成的 T 构造,通用模板的完美转发这时候可能匹配不上 T 的拷贝/移动构造,或者干脆产生歧义。Chromium 干脆显式把这俩重载摆出来,就是为了保 `NoDestructor<std::vector<int>> v({1,2,3})` 这类写法能正确走到 vector 的 initializer_list 构造。少这一手,真到用时编译器报错能让人翻半天。

## 访问:reinterpret_cast 的合法性

```cpp
    const T& operator*()  const { return *get(); }
    T&       operator*()        { return *get(); }
    const T* operator->() const { return get(); }
    T*       operator->()       { return get(); }

    const T* get() const { return reinterpret_cast<const T*>(storage_); }
    T*       get()       { return reinterpret_cast<T*>(storage_); }
```

`get()` 里头那一手 `reinterpret_cast<T*>(storage_)`,笔者第一次看的时候心里咯噔一下——把 char 数组地址硬转成 `T*`,这玩意儿能合法?能(no_destructor.h:118-119)。前提是 placement new 已经在这块内存上把 T 真正构造出来了:那之后这块内存就被当作 T 对象在用,指向它的指针转 `T*` 是安全的。换句话说,合法性靠"先构造、再访问"的顺序撑着——您要是没 new 就 cast,那就是另一回事了。

`operator*` 和 `operator->` 全部转发到 `get()`,智能指针风格。const 版本吐 `const T*`/`const T&`,非 const 版本吐可变引用,跟 `std::unique_ptr` 的接口对齐。用法上,`NoDestructor<T>` 行为基本就是个 `T*`:

```cpp
static const NoDestructor<std::string> s("hi");
s->size();    // operator->
(*s)[0];      // operator*
s.get();      // 显式取指针
```

## 不析构:=default 这个动作藏着玄机

```cpp
    NoDestructor(const NoDestructor&) = delete;
    NoDestructor& operator=(const NoDestructor&) = delete;

    ~NoDestructor() = default;   // ← 关键:不调 ~T()!
```

拷贝删除,理由 [04-1](./04-1-no-destructor-motivation-and-api.md) 里讲过,这里不重复。真正承重的是底下那行 `~NoDestructor() = default`——整个设计的命脉就压在这儿。

笔者头一次读这行是懵的:`= default` 不就是"让编译器生成默认析构"吗,这怎么能跳过 `~T()`?后来把它跟成员表对上才反应过来。编译器生成的默认析构,析构的是**成员**;而 NoDestructor 唯一的成员是 `char storage_[sizeof(T)]`。char 数组的析构是平凡的,什么都不做。编译器更不会自作主张把 `storage_` 当 T 来析构——T 是 placement new 后期才“长”在 `storage_` 上的,跟 `storage_` 在类型系统里的“身份”八竿子打不着。`~NoDestructor()` 跑完,`~T()` 一次都没被调用过;T 就这么在内存里"活着",一直活到程序退场,被操作系统当普通进程内存一把回收掉。

这就是"不析构"的实现根因,说穿了就一句话:让 NoDestructor 的析构只看到 char 成员,看不到 T。

---

## static_assert 把关:用类型系统拦住误用

```cpp
private:
    static_assert(!(std::is_trivially_constructible_v<T> &&
                    std::is_trivially_destructible_v<T>),
                  "T 平凡可构造且平凡析构:请直接用 constinit T,不需要 NoDestructor");

    static_assert(!std::is_trivially_destructible_v<T>,
                  "T 平凡析构:请直接用函数局部静态 T,不需要 NoDestructor");

    alignas(T) char storage_[sizeof(T)];
};
```

这两条 static_assert(no_destructor.h:85-93),笔者觉得是整个设计里最体贴的一手——它把 NoDestructor 卡死在"该用它的场景"上:**只服务非平凡析构的 T**。咱们一种一种看。

头一种,T 平凡可构造又平凡析构——典型是 `int`、POD struct 这类。这种您直接 `static constexpr T x = ...;` 或者 `constinit T x;` 就完事了,根本用不着 NoDestructor。硬要套上去,第一条断言当场把您拦下来。

再一种,T 平凡析构但非平凡可构造,比如某些带非平凡构造的类。这种也不归 NoDestructor 管——函数局部静态 `static T x;` 就够了,反正析构是平凡的,不会产生全局析构器。这种情况下用 NoDestructor,第二条断言拦。

最后才是 NoDestructor 真正该出手的情形:T 非平凡析构,像 `std::string`、`std::vector`、`std::map` 这种。两条断言都不触发,它才是个恰当工具。

笔者特别欣赏这套设计的地方:它不光拦,还顺手在报错信息里告诉您该怎么改。读者哪天手滑写个 `NoDestructor<int>`,编译期就吃一记明确提示,而不是留个隐患在生产里跑半年才撞见。

---

## 完整实现:五层合起来跑一遍

把上面五层拼到一起,完整就是这么一份头文件:

```cpp
// no_destructor.hpp
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
        new (storage_) T(std::forward<Args>(args)...);
    }
    explicit NoDestructor(const T& x) { new (storage_) T(x); }
    explicit NoDestructor(T&& x)      { new (storage_) T(std::move(x)); }

    NoDestructor(const NoDestructor&) = delete;
    NoDestructor& operator=(const NoDestructor&) = delete;
    ~NoDestructor() = default;

    const T& operator*()  const { return *get(); }
    T&       operator*()        { return *get(); }
    const T* operator->() const { return get(); }
    T*       operator->()       { return get(); }
    const T* get() const { return reinterpret_cast<const T*>(storage_); }
    T*       get()       { return reinterpret_cast<T*>(storage_); }

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

笔者这里直接给一段验证代码,咱们上机看看效果:

```cpp
#include <iostream>
#include <string>
#include "no_destructor.hpp"

struct Noisy {
    Noisy(int x) : v(x) { std::puts("Noisy()"); }
    ~Noisy() { std::puts("~Noisy()"); }   // 永不打印
    int v;
};

const std::string& DefaultName() {
    static const tamcpp::chrome::NoDestructor<std::string> s("chromium");
    return *s;
}

int main() {
    std::cout << DefaultName() << "\n";          // chromium
    static const tamcpp::chrome::NoDestructor<Noisy> n(42);
    std::cout << n->v << "\n";                   // 42
    // 程序退出:~NoDestructor 跑(平凡),~string 和 ~Noisy 都不跑
    return 0;
}
```

您会看到终端里打出 `chromium` 和 `42`,但 **`~Noisy()` 那行从头到尾不打印**——不析构生效了。

到这里 NoDestructor 的实现就算撸通了。拢共五层,笔者再带您过一遍:`storage_` 那段对齐缓冲;placement new 加完美转发的构造;`reinterpret_cast<T*>` 的访问(`get`/`operator*`/`->`);`= default` 这一手跳过 `~T()`、只析构 char 成员的析构策略;最后两条 static_assert 把关,把工具卡在非平凡析构的 T 上。整个类不到 50 行,每一行笔者都跟您掰扯过它的理由。

实现归实现,真正容易翻车的是另一种事:什么时候该用、什么时候绝对别用。这正是 NoDestructor 用错的富矿,下一篇咱们就专门拆它。

## 参考资源

- [Chromium `base/no_destructor.h` 完整源码](https://source.chromium.org/chromium/chromium/src/+/main:base/no_destructor.h)
- [cppreference: placement new](https://en.cppreference.com/w/cpp/language/new#Placement_new)
- [cppreference: is_trivially_destructible](https://en.cppreference.com/w/cpp/types/is_destructible)
- [NoDestructor 实战（一）：动机与接口设计](./04-1-no-destructor-motivation-and-api.md)
