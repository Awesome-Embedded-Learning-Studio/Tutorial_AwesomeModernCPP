---
title: "std::initializer_list：花括号背后的轻量序列"
description: "讲透 std::initializer_list：编译器为 {…} 生成的只读视图、浅拷贝与 const 元素、元素无法移动进容器的「移动陷阱」、花括号初始化的重载优先级，以及与容器构造的关系"
chapter: 7
order: 11
tags:
  - host
  - cpp-modern
  - intermediate
  - 容器
difficulty: intermediate
platform: host
cpp_standard: [11, 14, 17]
reading_time_minutes: 12
related:
  - "vector 深入：三指针、扩容与迭代器失效"
  - "span：非拥有的连续视图"
---

# std::initializer_list：花括号背后的轻量序列

## initializer_list 是什么：编译器为 {…} 生成的只读视图

`std::initializer_list` 是 C++11 给「花括号列表初始化」配的标准库类型。你写 `vector<int>{1, 2, 3}` 或 `f({1, 2, 3})` 时，编译器会在背后构造一个 `std::initializer_list<int>`，代表 `{1, 2, 3}` 这段序列。它本身是个极轻量的对象——大致就是一个指针加一个长度，和 `span` 一样属于「不拥有数据的视图」。

```cpp
std::initializer_list<int> il = {1, 2, 3};   // 编译器构造，指向底层 const int[3]
il.size();        // 3
il.begin();       // 指向首元素
il.end();         // 尾后
```

关键性质有三条：它**不拥有**元素（元素在编译器生成的一段底层 const 数组里），元素是 **const**（只读），拷贝它是**浅拷贝**（就拷贝那个指针和长度，不拷贝元素）。这三条决定了它的全部行为，也埋着它最出名的坑。

## 它有多轻：浅拷贝，元素只读

initializer_list 的拷贝是浅的——拷贝一个 initializer_list 就是拷贝它内部那个指针（和长度），底层那段 const 数组纹丝不动。所以拿 initializer_list 传参几乎零成本，和传指针差不多。

```cpp
void f(std::initializer_list<int> il);   // 按值传，其实是浅拷贝（指针 + size）
f({1, 2, 3, 4, 5});   // 不拷贝 5 个 int，只传一个视图
```

但「元素是 const」这一点要记住：initializer_list 里的元素是 `const T`，你拿不到非 const 访问。这看起来无害，却在和移动语义结合时挖了个大坑——下一节专门说。

## 移动陷阱：{…} 里的元素，进容器时只能拷贝

这是 initializer_list 最经典的坑。你想把几个对象塞进 vector，顺手写了 `vector<T>{a, b, c}`，以为现代 C++ 会高效地移动它们——结果它们是**拷贝**进去的。

根因就在「元素是 const」：initializer_list 的元素是 `const T`，而移动构造需要 `T&&`（非 const）。vector 从 initializer_list 构造时，得把每个 const 元素拷进自己的存储，const 拷不出 move，只能 copy。哪怕你在花括号里写 `std::move`，也只能让对象「移动进 initializer_list」（因为构造那一步接的是右值），可一旦进了 initializer_list 它就成了 const，再往 vector 里搬就只能拷贝了。

咱们量一下，看看到底拷了几次。用一个能统计拷贝 / 移动次数的类型：

```cpp
#include <iostream>
#include <vector>
#include <string>
#include <utility>

struct Counted {
    std::string s;
    inline static int copies = 0;
    inline static int moves = 0;
    Counted(std::string x) : s(std::move(x)) {}
    Counted(const Counted& o) : s(o.s) { ++copies; }
    Counted(Counted&& o) noexcept : s(std::move(o.s)) { ++moves; }
};

int main()
{
    // 场景 1：左值构造 initializer_list → vector
    {
        Counted a{"a"}, b{"b"}, c{"c"};
        Counted::copies = 0;
        Counted::moves = 0;
        std::vector<Counted> v{a, b, c};
        std::cout << "vector{a,b,c}        : copies=" << Counted::copies
                  << " moves=" << Counted::moves << "\n";
    }
    // 场景 2：move 进 initializer_list → vector（陷阱：进 vector 那步还是拷贝）
    {
        Counted a{"a"}, b{"b"}, c{"c"};
        Counted::copies = 0;
        Counted::moves = 0;
        std::vector<Counted> v{std::move(a), std::move(b), std::move(c)};
        std::cout << "vector{move(a),...}  : copies=" << Counted::copies
                  << " moves=" << Counted::moves << "\n";
    }
    // 场景 3：不用 initializer_list，push_back(move) → 全移动
    {
        Counted a{"a"}, b{"b"}, c{"c"};
        Counted::copies = 0;
        Counted::moves = 0;
        std::vector<Counted> v;
        v.reserve(3);
        v.push_back(std::move(a));
        v.push_back(std::move(b));
        v.push_back(std::move(c));
        std::cout << "push_back(move)      : copies=" << Counted::copies
                  << " moves=" << Counted::moves << "\n";
    }
    return 0;
}
```

```bash
g++ -std=c++17 -O2 -o /tmp/init_list_test /tmp/init_list_test.cpp && /tmp/init_list_test
```

```text
vector{a,b,c}        : copies=6 moves=0
vector{move(a),...}  : copies=3 moves=3
push_back(move)      : copies=0 moves=3
```

三个场景对比着看。第一种 `vector{a, b, c}`（左值）：6 次拷贝、0 次移动——3 次拷贝构造 initializer_list 的元素，再 3 次拷贝进 vector。第二种 `vector{std::move(a), ...}`：3 次拷贝、3 次移动——`std::move` 让对象移动进了 initializer_list（省了 3 次拷贝），但进 vector 那一步还是 3 次拷贝，const 移不动。第三种 `push_back(std::move(...))`：0 次拷贝、3 次移动——绕开 initializer_list，直接 move 进 vector，零拷贝。

所以记住这个性能坑：**把若干对象塞进容器，`vector{move(a), ...}` 仍会拷贝进 vector，只有 `push_back(move)` 才零拷贝**。当 T 是重型类型（大 string、大 vector），这个差距是实打实的拷贝开销。

## 花括号优先：为什么 {…} 总爱匹配 initializer_list 构造

initializer_list 还有个「重载偏好」：只要一个类的构造函数有 `initializer_list` 版本，花括号初始化就会优先选它，哪怕别的构造函数看起来更「合身」。最经典的翻车现场是 `vector<int>`：

```cpp
std::vector<int> v1(10, 0);    // 圆括号：10 个 0（count + value 构造）
std::vector<int> v2{10, 0};    // 花括号：两个元素 10 和 0（initializer_list 构造！）
```

`v1` 是 10 个 0，`v2` 是 `{10, 0}` 两个元素——同一段意图，圆括号和花括号给出了完全不同的结果，就因为花括号优先匹配了 `initializer_list<int>` 构造。这不是 bug，是规则：花括号初始化在有 `initializer_list` 构造时优先它。所以构造容器时，`(count, value)` 和 `{a, b}` 别混用，意图不同就用不同括号。

## 临了收几句

`std::initializer_list` 是花括号列表初始化背后的轻量视图：不拥有、元素 const、拷贝浅。它让 `{1, 2, 3}` 这种写法优雅地传给函数和容器，但「元素 const」埋了两个要记的点——一是移动陷阱（`vector{...}` 进容器必拷贝，重型类型要用 `push_back(move)`），二是花括号优先（有 `initializer_list` 构造时，`{}` 会抢着匹配）。下一篇我们离开初始化，去看类型本身的内存布局：对象大小与平凡类型。

想直接上手运行看看效果？点开下面的在线示例（能运行、也能看汇编）：

<OnlineCompilerDemo
  title="std::initializer_list：只读视图与移动陷阱"
  source-path="code/examples/vol3/11_initializer_lists.cpp"
  description="花括号生成的只读视图、元素 const 无法 move 的拷贝陷阱、花括号重载优先级"
  allow-run
/>

## 参考资源

- [std::initializer_list — cppreference](https://en.cppreference.com/w/cpp/utility/initializer_list)
- [列表初始化 — cppreference](https://en.cppreference.com/w/cpp/language/list_initialization)
