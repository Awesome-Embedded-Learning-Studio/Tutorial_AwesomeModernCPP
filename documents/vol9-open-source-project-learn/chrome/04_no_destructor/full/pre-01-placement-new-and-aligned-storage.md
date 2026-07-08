---
chapter: 0
cpp_standard:
- 11
- 17
- 20
description: 拆解 placement new(不分配、只构造)与对齐存储(alignas/alignof),
  以及 NoDestructor 如何用 char storage_[sizeof(T)] + reinterpret_cast 手动管理生命周期
difficulty: intermediate
order: 1
platform: host
prerequisites:
- NoDestructor 前置知识（零）：静态存储期、初始化与析构
reading_time_minutes: 9
related:
- NoDestructor 实战（二）：核心实现
tags:
- host
- cpp-modern
- intermediate
- 内存管理
- RAII
title: "NoDestructor 前置知识（一）：placement new 与对齐存储"
---
# NoDestructor 前置知识（一）：placement new 与对齐存储

[pre-00](./pre-00-static-storage-and-init.md) 咱们说 NoDestructor "构造 T 但不让它析构"。这话听着玄,落到代码上其实就两个底层机制在撑着:placement new(在您指定的地址上构造对象,不分配内存),外加一段按 T 对齐好的 `char` 数组当 T 的家。咱们这一篇就把这俩拆开揉碎——它们不光是 NoDestructor 的核心,也是 C++ 手动生命周期管理的家常工具,以后写内存池、写容器都得跟它们打交道。

---

## 普通 new vs placement new

普通的 `new T(args)` 其实是两步合一:先 `operator new(sizeof(T))` 从堆上抠一块 `sizeof(T)` 的内存,再在那块内存上敲 `T` 的构造函数。`delete ptr` 反过来——先析构,再 `operator delete(ptr)` 把内存还回去。`new` 把"分配"和"构造"打包在一起,绝大多数时候这正是您想要的。可有时候您手上已经有一块内存了(栈数组、内存池、mmap 来的),只想让构造函数在上面跑一遍,别再替您要新内存。这就是 placement new 要解决的问题。

### placement new:只构造,不分配

语法就是在 `new` 后面多塞个 `(addr)`,告诉它往哪儿构造:`new (addr) T(args)`。

```cpp
#include <new>   // placement new 需要

alignas(int) unsigned char buf[sizeof(int)];   // 已有内存(一段 char 数组)
int* p = new (buf) int(42);                      // 在 buf 上构造 int,不分配!
*p == 42;
```

它只干"构造"那一步,在您给的 `addr` 上敲 `T` 的构造函数,`operator new` 根本没被叫起来。内存是您的,生命周期也归您管。

析构这一侧就得您自己动手了——手动调析构函数 `p->~T()`。千万别写成 `delete p`,`delete` 会再去试着释放内存,可这块内存压根不是从堆上分配的,一释放就出错:

```cpp
using I = int;
I* p = new (buf) I(42);
p->~I();        // 手动析构(对 int 这种平凡类型其实没必要,但机制如此)
// buf 本身是栈数组,自动回收,不归 placement new 管
```

(这里有个小编译器梗:对裸内置类型名,伪析构调用得套个 typedef 别名——`p->~int()` 主流编译器不接受,得 `using I=int; p->~I();`。笔者第一次撞见的时候愣了好一会儿。)

placement new 真正值钱的地方在这儿——它把"对象什么时候生、什么时候灭"和"内存是谁的、什么时候还"拆成了两件事。您可以在栈内存上构造,也可以在内存池、共享内存、mmap 来的块上构造,想什么时候构造就什么时候构造,想什么时候析构就手动调一下析构。手动生命周期管理的活儿,基本都从这一句起手。

---

## 对齐:alignof 与 alignas

placement new 还有个前提:您递过去的地址得满足 `T` 的对齐要求。所谓对齐(alignment),就是"对象地址必须是某个值的整数倍"——CPU 访问对齐的地址更快,某些架构上访问没对齐的地址,直接给您一个硬件异常,连跑都不让跑。

两个关键字分一下工。`alignof(T)` 查询 `T` 的对齐要求是多少字节,`alignof(int)` 通常拿到 4,`alignof(double)` 拿到 8。`alignas(N)` 则反过来,是您主动给某个变量或类型指定对齐,`alignas(16) int x;` 就是逼着 `x` 凑齐 16 字节对齐。一个问、一个答。

要是您给 placement new 递了个没对齐的地址,行为是未定义的:

```cpp
unsigned char buf[13];           // 地址可能不是 4 字节对齐
new (buf) int(42);               // UB!buf 的对齐可能不够 int
```

所以递内存这步,对齐必须满足 `T`,没得商量。

### NoDestructor 的写法:`alignas(T) char storage_[sizeof(T)]`

NoDestructor 是这么把对齐这关过去的(no_destructor.h:122):

```cpp
alignas(T) char storage_[sizeof(T)];
```

就这一行,干了两件事。`char storage_[sizeof(T)]` 先开出一块 `sizeof(T)` 字节的 char 数组,容量刚好够装一个 T——`char` 是最"宽容"的类型,任意字节都能塞,当通用缓冲最合适。`alignas(T)` 再把这块数组的对齐从 char 默认的 1 提到 `T` 那一档。两下一凑,`storage_` 的地址保证是 `alignof(T)` 的整数倍,placement new 直接往上招呼就行,不用再担心踩到对齐的雷。

这是 C++ 手写缓冲存储的标准写法。老代码里您还会常看到 `std::aligned_storage<sizeof(T), alignof(T)>` 这套模板——它在 C++23 里被标了弃用(见 LWG3867/P2967),`alignas(T) char buf[sizeof(T)]` 才是现在的推荐写法,直观,不绕模板那道弯。

### 访问:`reinterpret_cast<T*>(storage_)`

构造完了,还得把这块 char 内存当 `T` 用——靠 `reinterpret_cast<T*>(storage_)` 把地址转成 `T*`。这步是合法的:placement new 跑过之后,这块 char 内存里头确实住着一个货真价实的 T 对象,`reinterpret_cast` 指过去有定义。NoDestructor 的 `get()` 就是这么写的(no_destructor.h:118-119):

```cpp
T* get() { return reinterpret_cast<T*>(storage_); }
```

---

## 手动生命周期:构造了不析构

把前面几样凑齐,就看明白 NoDestructor 在干什么了。它手里攥着一块 `alignas(T) char storage_[sizeof(T)]` 的裸缓冲,构造的时候 `new (storage_) T(args...)` 把 T placement-new 上去。然后——重点来了——它压根不给 T 留一条析构的路。`~NoDestructor()` 是 `= default`,析构的是那个 char 数组,而 char 数组是平凡类型,什么都不做。`~T()` 在这条路径上永远不会被叫起来。

这就是"构造了不析构"的全部秘密:T 被 placement new 捏出来之后,就一直在那块 `storage_` 里待着,直到进程退出,操作系统把整块进程内存——包括里头那个 T——当普通内存一起回收。注意这个回收者不是 T 的析构函数,是 OS。

### 这安全吗?

T 自己持有的资源怎么办?比如 `NoDestructor<vector<int>>`,vector 在堆上分配的那一堆元素。坦白说,这些资源不会被 `~T()` 释放——因为 `~T()` 压根没跑。它们靠的是 OS 在进程退出时统一回收整片地址空间。程序运行期间这块内存算是"泄漏"了,可程序都要结束了,泄漏给谁看?OS 反正会兜底。

真正会出事的是另一种情况:T 的析构带副作用。比如有个析构函数负责把日志 flush 到磁盘、或者通知另一个进程"我走了"。这种副作用不会发生,因为析构没跑。所以 NoDestructor 只适合那种"析构就是释放资源"的类型——析构一跑副作用就没了的,别用。

---

## 一个最小复刻

光说不练假把式,咱们自己撸个最小版,亲手摸一下 placement new 加"不析构"是个什么手感:

```cpp
// Platform: host | C++ Standard: C++17
#include <cassert>
#include <cstdio>
#include <new>
#include <string>

template <typename T>
class MiniNoDestructor {
public:
    template <typename... Args>
    explicit MiniNoDestructor(Args&&... args) {
        new (storage_) T(std::forward<Args>(args)...);   // placement new
    }
    ~MiniNoDestructor() = default;   // 不调 ~T()!
    MiniNoDestructor(const MiniNoDestructor&) = delete;

    T& operator*() { return *get(); }
    T* operator->() { return get(); }
    T* get() { return reinterpret_cast<T*>(storage_); }

private:
    alignas(T) char storage_[sizeof(T)];
};

struct Noisy {
    Noisy() { std::puts("Noisy()"); }
    ~Noisy() { std::puts("~Noisy()"); }   // 这个析构永远不跑
};

int main() {
    {
        static const MiniNoDestructor<Noisy> nd;   // 构造一次
        // 离开作用域/程序退出:~MiniNoDestructor 跑(平凡),~Noisy 不跑
    }
    std::puts("(程序退出前 ~Noisy 不会打印)");
    return 0;
}
```

跑一下您就看见了:`Noisy()` 打印一次,可 `~Noisy()` 那行——一行都打不出来。这就是 NoDestructor 的"不析构",实打实的。

---

零件齐了。placement new 给咱们"只构造、不分配"的能力,`alignas(T) char storage_[sizeof(T)]` 把对齐这关趟过去,`~NoDestructor()=default` 又把析构这条路悄悄堵死——三样一凑,T 就这么在 `storage_` 里赖着不走,直到 OS 在进程退出时统一收拾。下一篇咱们该动手把 NoDestructor 真正组装起来了,光有零件不行,还得看它怎么把初始化顺序、`reinterpret_cast` 的合法路径这些边角都兜住。

## 参考资源

- [cppreference: placement new](https://en.cppreference.com/w/cpp/language/new#Placement_new)
- [cppreference: alignof / alignas](https://en.cppreference.com/w/cpp/language/alignas)
- [cppreference: std::aligned_storage(C++17 起 deprecated)](https://en.cppreference.com/w/cpp/types/aligned_storage)
- [Chromium `base/no_destructor.h` —— storage_ 与 get()](https://source.chromium.org/chromium/chromium/src/+/main:base/no_destructor.h)
