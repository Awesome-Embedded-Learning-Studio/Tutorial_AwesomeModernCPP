---
chapter: 1
cpp_standard:
- 11
- 17
- 20
description: 面向有生命周期管理经验的读者,一篇走完 NoDestructor 的动机、接口与实现
  ——本系列 full/ 的精炼设计指南版(NoDestructor 体量小,合并设计+实现)
difficulty: advanced
order: 1
platform: host
prerequisites:
- 移动语义与完美转发
- NoDestructor 前置知识（零）：静态存储期、初始化与析构
reading_time_minutes: 7
related:
- NoDestructor 设计指南（二）：使用边界与测试
tags:
- host
- cpp-modern
- advanced
- 内存管理
- RAII
title: "NoDestructor 设计指南（一）：动机、接口与实现"
---
# NoDestructor 设计指南（一）：动机、接口与实现

> hands-on 轨,默认您已熟静态存储期、placement new 和对齐存储;不熟的话先过一遍 [full/ 前置知识](../full/pre-00-static-storage-and-init.md)。

Chromium 这头有个笔者一开始觉得反常的硬规矩:全局对象不许有构造,也不许有析构。`-Wglobal-constructors` 和 `-Wexit-time-destructors` 两个开关一开,您敢在全局作用域摆个带构造函数的对象,编译期就给您甩脸上。理由其实挺实在——构造拖慢启动、析构触发关停竞态,静态初始化顺序(SIOF)那笔糊涂账更是谁碰谁知道。可问题是全局单例到处都要:默认配置、feature flag、随机 nonce,哪个不是一启动就常驻。禁了,代码还怎么写?

笔者把现成的几条路都试了一遍,没一条能同时过两关。裸全局第一个就被否,直接撞枪口上。Meyers singleton 看着讨巧,`static T& f(){static T x;return x;}` 借 C++11 的 magic statics 把构造的线程安全和惰性都解决了,笔者一度以为它就是答案。可它只解决了构造那一半,`~T()` 该跑还得跑,关停时那一坨析构竞态照样在。最后一条是手撸 placement new,能成,但每个地方都得记得抄一套 static_assert、记得处理 LSan reachability,抄漏一处就是定时炸弹。

NoDestructor 就是 Chromium 给这条死路打的补丁。思路拆开看其实就两手:把对象挪进函数局部静态,绕开全局构造那道关,顺便蹭上 magic statics 的线程安全初始化;然后干脆不调 `~T()`,把全局析构那道关也一起绕过去。代价呢?对象"故意泄漏",不析构了,等进程退出时让操作系统兜底回收。听上去糙,可对一个要活到进程最后一刻的全局单例来说,这正是它该有的样子。

## 接口

```cpp
template <typename T>
class NoDestructor {
public:
    template <typename... Args>
    explicit NoDestructor(Args&&... args);   // 完美转发构造
    explicit NoDestructor(const T& x);        // 拷贝构造(方便 initializer_list)
    explicit NoDestructor(T&& x);             // 移动构造
    NoDestructor(const NoDestructor&) = delete;
    ~NoDestructor() = default;                // ← 关键:不调 ~T()
    const T& operator*() const;  T& operator*();
    const T* operator->() const; T* operator->();
    const T* get() const;  T* get();
};
```

接口就这么点,笔者第一次读完还挺意外。`*`/`->`/`get` 一套智能指针门面,拷贝直接 delete 掉——不然两个 NoDestructor 各自 `~char[]` 还好,要是有人想深拷贝里头的 `storage_` 那就乱套了。也不继承 T,这点是有意的:NoDestructor 想当的是个容器,语义上得保持独立,不掺和 T 的继承链。日常用法就一行,`static const NoDestructor<T> x(args); return *x;`,记住这个模子就够了。

## 实现(完整,~50 行)

```cpp
// Platform: host | C++ Standard: C++20
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
    explicit NoDestructor(T&& x)      { new (storage_) T(std::move(x)); }

    NoDestructor(const NoDestructor&) = delete;
    NoDestructor& operator=(const NoDestructor&) = delete;
    ~NoDestructor() = default;   // 只析构 char 成员,不调 ~T()

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

    // Chromium 在 LEAK_SANITIZER 构建下额外持 T* storage_ptr_ 作 LSan reachability 根
    // (crbug/40562930);教学版省略,用 LSan suppression 文件替代。
};

}  // namespace tamcpp::chrome
```

代码跑下来就这几处刻意为之的取舍,笔者逐条点一下,您看代码时就能对上号:

| 决策 | 实现 | 理由 |
|---|---|---|
| 存储用 `alignas(T) char[N]` | `alignas(T) char storage_[sizeof(T)]` | 内联缓冲,零堆分配,对齐满足 placement new |
| 构造用 placement new | `new (storage_) T(forward<Args>(args)...)` | 在 storage_ 上构造,不分配 |
| 析构 `= default` | `~NoDestructor() = default` | 只析构 char 成员(平凡),**不调 `~T()`**——这是"不析构"的根 |
| static_assert 把关 | 两条断言 | 只服务非平凡析构 T;平凡情况引导用 constinit/裸静态 |

## "不析构"到底是怎么做到的

表格里那一行 `~NoDestructor() = default` 是整个机制的心脏,笔者单独拎出来讲,因为它玩的是个挺妙的视角差。

`storage_` 声明的类型是 `char[]`,这是编译器在生成析构函数时唯一能看到的类型。`char` 的析构是平凡的,什么都不用做。T 呢?T 是后来用 placement new 按 `storage_` 起始地址原地构造上去的,对编译器来说它和 `storage_` 的"类型"没有半点关系,析构 `storage_` 时根本不会顺带想起它。于是 `~T()` 永远不会被排进 NoDestructor 的析构路径——T 就这么安安静静地活到进程结束,最后一刻由操作系统统一回收。一个"类型视角差",换来整个"不析构"的语义,笔者觉得这招相当漂亮。

## 参考资源

- [Chromium `base/no_destructor.h`](https://source.chromium.org/chromium/chromium/src/+/main:base/no_destructor.h)
- [NoDestructor 实战（二）：核心实现](../full/04-2-no-destructor-core-impl.md)
- [NoDestructor 前置知识（一）：placement new 与对齐存储](../full/pre-01-placement-new-and-aligned-storage.md)
