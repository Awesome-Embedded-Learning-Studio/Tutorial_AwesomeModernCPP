---
title: "array：编译期固定大小的聚合容器"
description: "讲透 std::array：作为聚合类型零开销包住 C 数组、不退化为指针、std::get 与结构化绑定、永不失效的迭代器、constexpr 编译期查表，以及与 C 数组和 vector 的精确边界"
chapter: 7
order: 2
tags:
  - host
  - cpp-modern
  - intermediate
  - array
  - 容器
difficulty: intermediate
platform: host
cpp_standard: [11, 14, 17, 20]
reading_time_minutes: 12
related:
  - "vector 深入：三指针、扩容与迭代器失效"
---

# array：编译期固定大小的聚合容器

## array 到底是什么：零开销包住 C 数组的聚合类

`std::array` 是 C++11 给 C 数组补的「现代化外壳」。C 数组 `T[N]` 有几个老毛病：传参时退化为指针（丢掉长度）、没有 `.size()`、不能整体拷贝赋值、没法当函数返回值。`std::array<T, N>` 把这块连续内存包成一个类模板，补上 STL 接口，而且——这是关键——**它是个聚合类型（aggregate），没有任何额外开销**：`sizeof` 和 C 数组一模一样，没有虚函数、没有虚指针、没有额外成员。

```cpp
std::array<int, 5> a = {1, 2, 3, 4, 5};   // 大小 5 在编译期定死
a.size();        // 5
a[0];            // 1，O(1)
a.data();        // int*，指向底层连续内存
```

那个 `N` 是模板参数，是编译期常量。这意味着 array 的大小是类型的一部分——`std::array<int, 5>` 和 `std::array<int, 6>` 是两个不同的类型，不能互相赋值。代价换来的是零动态分配：array 占的内存就是那块连续数据，放在栈上或静态区，不碰堆。

## 和 C 数组的精确对比：不退化、有接口、能当对象

array 相对 C 数组的改进，一条条数清楚。第一，**不退化为指针**：C 数组传给函数会退化成 `T*`，丢掉长度；array 是个对象，传参时完整保留类型（包括 N），你要么传 `const std::array<T, N>&`，要么显式 `.data()` 给 C 接口。第二，**有 STL 接口**：`.size()`、`.empty()`、`.begin()` / `.end()`、`.data()`、`operator[]`、`.at()`，能直接喂给 `<algorithm>` 和范围 for。第三，**能整体拷贝赋值**：`auto b = a;` 就是逐元素拷贝，还能当函数返回值、当类的成员——这些 C 数组都做不到。

```cpp
std::array<int, 4> make() { return {1, 2, 3, 4}; }   // C 数组做不到
auto a = make();
auto b = a;        // 整体拷贝，C 数组做不到
b.fill(0);         // 一把清零
```

但底层还是那块连续内存。标准保证 array 是聚合，所以 `sizeof(std::array<T, N>)` 就等于 `sizeof(T) * N`（没有额外成员、没有尾部 padding 之外的浪费）。它没有额外开销，只是多了接口和类型安全。

## 和 vector 的边界：何时该用定长

array 和 vector 的分界线就一条：**大小编译期知道吗**。如果大小在编译期能定死、且不会变，用 array——零堆分配、零开销、可以 `constexpr`、放静态区省 RAM。如果大小运行时才确定、或需要增删，用 vector。

代价是对等的：array 大小是类型的一部分（`array<int, 5>` 和 `array<int, 6>` 不能通用），函数想接受「任意大小的 int 数组」就没法用 array（得用 `span` 或模板）；vector 没这个限制，但有堆分配和扩容开销。一句话：**定长用 array，变长用 vector**，中间地带（运行期已知大小但不想堆分配）可以等 C++26 的 `inplace_vector`，或自己管 buffer 配 `span`。

## 作为聚合类型的特权：std::get、结构化绑定、tuple 接口

array 是聚合类型，这让它在 C 数组之外还吃到一份「类 tuple」的红利。`std::get<I>(arr)` 能按编译期下标取元素（返回引用，带类型安全）；C++17 的结构化绑定能直接把小 array 拆成变量；`std::tuple_size`、`std::tuple_element` 也认识 array，所以 array 能塞进那些吃 tuple-like 类型的泛型代码里。

```cpp
std::array<int, 3> a = {10, 20, 30};
std::get<1>(a);            // 20，编译期下标，类型安全
auto [x, y, z] = a;        // 结构化绑定：x=10, y=20, z=30
static_assert(std::tuple_size_v<decltype(a)> == 3);
```

这些在 C 数组上都没有——C 数组拿不到 `std::get`，也不支持结构化绑定。对那种「固定几个值」的小数组（比如三维坐标、RGB），array 加结构化绑定比写个 struct 还顺手。

## 复杂度、迭代器失效与异常安全

复杂度一目了然：随机访问 `operator[]` 和 `.at()` 都是 O(1)，遍历 O(n)，没有扩容、没有重分配——因为大小定死。

**迭代器失效**这块，array 是最省心的：它永不失效。因为 array 是固定大小的聚合，没有扩容、没有插入删除（接口里压根没有 `push_back` / `insert`），迭代器、引用、指针一旦拿到，只要 array 对象本身还活着，就一直有效。这点比 vector（扩容全失效）、deque、list 都干净。

异常安全上有个要留意的点：`.at(i)` 会做边界检查，越界抛 `std::out_of_range`；而 `operator[]` 不检查，越界是未定义行为。在异常关闭的环境（比如 `-fno-exceptions`），`.at()` 的越界会退化成 `std::terminate`，所以那种场景只能用 `operator[]` 并自己保证索引正确。

## 跑跑看：零开销与 constexpr

光说「零开销」不够实在，咱们跑跑看。先确认 sizeof 真的和 C 数组一样：

```cpp
#include <array>
#include <iostream>

int main()
{
    int raw[8];
    std::array<int, 8> arr;
    std::cout << "sizeof(int[8])        = " << sizeof(raw) << '\n';
    std::cout << "sizeof(array<int,8>)  = " << sizeof(arr) << '\n';
    std::cout << "data() 指向首元素？   " << (arr.data() == &arr[0]) << '\n';
    return 0;
}
```

```bash
g++ -std=c++20 -O2 -o /tmp/array_sizeof /tmp/array_sizeof.cpp && /tmp/array_sizeof
```

```text
sizeof(int[8])        = 32
sizeof(array<int,8>)  = 32
data() 指向首元素？   1
```

sizeof 完全相等，没有额外开销——array 就是那块连续内存，套了个类。`data()` 也确实指向首元素，可以放心交给 C 接口或 DMA。

array 的另一大本事是 **constexpr**——它能在编译期完成初始化和计算，生成的数据直接放进只读段。一个经典用法是编译期生成 CRC 查表：

```cpp
#include <array>
#include <cstdint>

constexpr std::array<uint32_t, 256> make_crc_table()
{
    std::array<uint32_t, 256> t{};
    for (std::size_t i = 0; i < 256; ++i) {
        uint32_t crc = static_cast<uint32_t>(i);
        for (int j = 0; j < 8; ++j) {
            crc = (crc & 1) ? (0xEDB88320u ^ (crc >> 1)) : (crc >> 1);
        }
        t[i] = crc;
    }
    return t;
}

// 编译期算完，进只读段；运行时零开销
constexpr auto crc_table = make_crc_table();
static_assert(crc_table.size() == 256);
static_assert(crc_table[0] == 0x00000000u);   // 输入 0，结果 0
```

这 256 项表在编译期就算好了，程序运行时直接从只读段读，既不占 RAM、也不花运行时 CPU。这种「编译期查表」是 array + constexpr 的黄金组合——C 数组配 constexpr 做不到这么干净（尤其涉及拷贝返回时）。

## 延伸：嵌入式里的 array（DMA / flash / 栈）

array 因为零堆分配、连续内存、可 constexpr，在嵌入式里特别受欢迎，这里补几个实战要点（主线之外，按需取用）。第一，**连续内存保证**：`.data()` 返回的指针指向一段连续存储，可以安全交给 DMA 或 HAL，前提是元素类型是 trivially copyable。第二，**放静态区省 RAM**：大数组用 `static` 或放进 `.bss`，查表数据用 `constexpr` 直接进 flash，不占 RAM。第三，**栈深度**：小数组放栈上没问题，但要留意任务 / ISR 的栈深度限制，别在窄栈里放大 array。

## 临了收几句

array 是 C 数组的现代化外壳，零开销、有 STL 接口、不退化、能当对象，还能借聚合身份吃到 `std::get` 和结构化绑定。它永不失效迭代器、可以 constexpr、零堆分配——只要大小编译期能定死，它就是比 C 数组和 vector 都更合适的选择。下一篇我们看它的「动态版」vector，从固定走向可变，代价是堆和扩容。

想直接上手运行看看效果？点开下面的在线示例（能运行、也能看汇编）：

<OnlineCompilerDemo
  title="array：零开销聚合容器与 constexpr 查表"
  source-path="code/examples/vol3/02_array.cpp"
  description="sizeof 与 C 数组一致、constexpr CRC 编译期查表、结构化绑定"
  allow-run
/>

## 参考资源

- [std::array — cppreference](https://en.cppreference.com/w/cpp/container/array)
- [聚合类型 — cppreference](https://en.cppreference.com/w/cpp/language/aggregate_initialization)
- [容器迭代器失效规则总表 — cppreference](https://en.cppreference.com/w/cpp/container#Iterator_invalidation)
