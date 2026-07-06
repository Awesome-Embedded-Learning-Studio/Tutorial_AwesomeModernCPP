---
chapter: 6
cpp_standard:
- 17
description: C++17 起,返回值优化(RVO/NRVO)让「按值返回大对象」几乎免费——返回值直接在调用方栈上构造,零拷贝零移动。本篇用一个 copy/move
  构造函数会计数的 Tracked 类型(编译器打不穿的演示)看清:URVO/NRVO 是 0 拷贝 0 移动,return std::move(局部) 是反模式(禁用
  NRVO 多 1 次 move),move 比 copy 便宜一个数量级
difficulty: advanced
order: 5
platform: host
prerequisites:
- std::function 的小缓冲区优化
- Benchmark 方法论参考卡
reading_time_minutes: 6
related:
- C++ 抽象的成本速查表
tags:
- host
- cpp-modern
- advanced
- 优化
- 移动语义
title: RVO、NRVO 与 move 的真实成本
---
# RVO、NRVO 与 move 的真实成本

## 「按值返回大对象」曾经很贵,现在免费

C++ 早期,「按值返回大对象」(比如 `vector<string> make()`)是被反复劝阻的,它意味着拷贝整个对象,贵得离谱。所以 C++ 圈长期流传着「返回大对象要用指针/引用」「用输出参数 `void make(T& out)`」之类的旧教条。

这些教条**在现代 C++ 里基本过时了**。C++17 起,**返回值优化(RVO / NRVO)是强制的**(URVO)或事实上的(NRVO):返回值**直接在调用方的栈上构造**,既不拷贝也不移动,零成本。我们用一个编译器打不穿的方法看清这件事。

## 用 copy/move 计数器看清(编译器打不穿)

RVO 的演示有个坑:**用计时测,编译器跨迭代优化会打穿**(我第一版计时程序里,「拷贝」居然比「RVO」还快,因为编译器把整个循环折叠了)。正确做法是用一个 **copy/move 构造函数会计数的类型**,它**直接数**发生了几次拷贝、几次移动,编译器再聪明也改不了你的计数器:

```cpp
struct Tracked {
    int v;
    static int64_t copies, moves;
    Tracked(int x) : v(x) {}
    Tracked(const Tracked& o) : v(o.v) { ++copies; }              // 计拷贝
    Tracked(Tracked&& o) noexcept : v(o.v) { ++moves; o.v = -1; } // 计移动
};
```

然后测四种「返回」写法,各看 copies/moves 计数:

```text
===== RVO/NRVO/move/copy 的拷贝/移动计数 =====
  URVO return Tracked(1):     copies=0  moves=0  → 零拷贝零移动
  NRVO return t(有名局部):   copies=0  moves=0  → 零拷贝零移动
  return std::move(t):        copies=0  moves=1  → 被强制 1 次 move(NRVO 被你禁了)
  return g_global(lvalue):    copies=1  moves=0  → 1 次拷贝(不能 RVO)
```

这张表是 RVO 教学的金标准(不依赖计时,编译器打不穿):

- **URVO(返回无名临时)**:`return Tracked(1);`。C++17 起是**强制拷贝消除(guaranteed copy elision)**,返回的 prvalue 直接在调用方初始化,既不拷贝也不移动。**0 拷贝 0 移动**。
- **NRVO(返回有名局部)**:`Tracked t(...); return t;`。命名局部变量的返回,编译器**事实上**会消除(C++17 前就普遍做了,C++17 起更多场景保证)。**0 拷贝 0 移动**。
- **`return std::move(t)`(反模式)**:你手写的 `std::move` **强制转成右值**,这**禁用了 NRVO**(NRVO 要求左值),编译器被迫走 move 构造。**0 拷贝 1 移动**,多了一次本可避免的 move。所以「`return std::move(局部)`」是 C++ 里著名的**反模式**,它只能让代码变慢,永远不会变快。
- **返回 lvalue(全局/参数)**:`return g_global;`。lvalue 不能 RVO/move(没资格),走拷贝。**1 拷贝 0 移动**。这是真正「贵」的情形:返回一个外部命名对象,必须拷贝它。

## 用 -fno-elide-constructors 看 RVO 关掉的样子

GCC 有个 flag `-fno-elide-constructors` 关掉拷贝消除(用来复现老 C++ 行为或调试)。加上它重编:

```text
(加 -fno-elide-constructors:)
  URVO return Tracked(1):     copies=0  moves=0   ← C++17 guaranteed,关不掉
  NRVO return t(有名局部):   copies=0  moves=1   ← NRVO 被关,退化成 1 次 move
  return std::move(t):        copies=0  moves=1
  return g_global:            copies=1  moves=0
```

读出两件事:

- **URVO 在 C++17 是强制的,`-fno-elide` 都关不掉**(prvalue 初始化规则,不是优化)。所以 `return T(args)` 永远零成本。
- **NRVO 是「事实优化」,关掉后退化为一次 move**。move 比 copy 便宜(下面讲),所以即使 NRVO 没生效,你也就付一次 move 的成本,不是 copy。这是 C++「值语义安全」的兜底,最坏情况是一次便宜 move。

## move 比 copy 便宜多少

`std::move` 本身什么也不做(就是个右值转换);真正干活的是**移动构造函数**。对 `vector`/`string` 这种管理动态内存的类型,move 是**指针交换**(O(1)),copy 是**深拷贝**(O(n)):

```cpp
// vector 的 move 构造:O(1) 指针交换(注意参数是 vector&&,不是 const vector&&——move 要改源)
vector(vector&& o) noexcept : data_(o.data_), size_(o.size_) { o.data_ = nullptr; o.size_ = 0; }
// vector 的 copy 构造:O(n) 深拷贝
vector(const vector& o) : data_(new T[o.size()]) { copy o.data_ → data_; }
```

对一个 4 KB 的 `vector<int>`(1000 个元素),move 是几次指针赋值(纳秒级),copy 是分配 4KB + memcpy(几十到几百纳秒,取决于分配器)。**move 比 copy 便宜一个数量级以上**,元素越多差距越大。

但 move 不是免费,它仍是「构造一个新对象 + 析构被掏空的源对象」。所以**最便宜的是 RVO/NRVO(0 次),其次 move(1 次),最贵 copy(深拷贝)**。

## 实战规则

压成几条可背诵的:

1. **按值返回,放心写**。`return Tracked(args)`(URVO,C++17 强制免费)和 `T t(...); return t;`(NRVO,事实上免费)都零成本。
2. **`return std::move(局部)` 是反模式,别写**。它禁用 NRVO,只会变慢。编译器会在「返回局部变量」时自动按需转右值,你不用操心。
3. **move 不是免费,但比 copy 便宜一个数量级**(对大对象)。`std::vector`/`std::string` 的移动是 O(1)。
4. **`std::move` 用在「明确要转右值」的地方**:比如把局部变量塞进容器 `v.push_back(std::move(elem))`、转移 `unique_ptr` 所有权。别在 `return` 上用。
5. **返回外部 lvalue(全局、参数、成员)仍会拷贝**,那种情况考虑 `const&` 返回或显式 `std::move`(如果确实要转移所有权)。

这几条把「现代 C++ 怎么返回对象」讲全了。旧教条「按值返回慢」在现代 C++ + RVO 下基本不成立,**写得自然、写得值语义,编译器替你消除拷贝**。

一句话收口:RVO/NRVO 让按值返回零成本(URVO 是 C++17 强制 0/0,NRVO 是事实优化 0/0),编译器再聪明也打不穿,得用 copy/move 计数器验证;`return std::move(局部)` 是反模式,禁用 NRVO 多一次 move(0 拷贝 1 move),别写;move 比 copy 便宜一个数量级(O(1) 指针交换 vs O(n) 深拷贝),但不是免费;返回外部 lvalue 才会拷贝,那种情况考虑引用或显式转移。ch06 到此完结:虚函数、异常、std::function、速查表、RVO/move,C++ 主要抽象的成本都测过了。

## 参考资源

- cppreference *Copy elision*(C++17 guaranteed elision 规则)
- Meyer, S. *Effective Modern C++ Item 25*(reverse:对右值重载 vs 引用限定)——move 语义的工程用法
- Agner Fog《Optimizing software in C++》§7.16 *Returning objects》。本地
- 本篇实测代码:`code/volumn_codes/vol6-performance/ch06/rvo_move.cpp`(含 -fno-elide-constructors 对照)
