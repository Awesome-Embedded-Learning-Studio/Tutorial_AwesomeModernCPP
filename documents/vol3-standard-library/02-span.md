---
title: "span：非拥有的连续视图"
description: "讲透 std::span：指针加长度的非拥有视图、动态与静态 extent 的内存差异、统一接收 array/vector/C 数组、零拷贝切片 subspan、字节视图 as_bytes，以及悬垂视图的生命周期陷阱"
chapter: 7
order: 8
tags:
  - host
  - cpp-modern
  - intermediate
  - span
  - 容器
difficulty: intermediate
platform: host
cpp_standard: [17, 20]
reading_time_minutes: 14
related:
  - "array：编译期固定大小的聚合容器"
  - "vector 深入：三指针、扩容与迭代器失效"
---

# span：非拥有的连续视图

## span 是什么：一个指针加一个长度，仅此而已

`std::span` 是 C++20 给「一段连续数据」配的标准化视图。它不拥有这段内存，只持有两样东西：一个指针，一个长度。就这么简单——你可以把它理解成一个「带边界信息的指针」，或者 C 里 `(ptr, len)` 这对参数的正式封装。它不分配、不释放、不拷贝底层数据，拷贝一个 span 就是拷贝那两个字（指针和 size），极其廉价。

```cpp
std::vector<int> v = {1, 2, 3, 4};
std::span<int> s(v);       // s 指向 v 的数据，但不拥有
s.size();                  // 4
s[0];                      // 1
s.data() == v.data();      // true
```

它的核心价值在「传参」：函数想接受「一段 T 数据」时，用 `std::span<const T>` 能统一接收 C 数组、`std::array`、`std::vector`、`(指针, 长度)` 等所有连续来源，既不拷贝数据，也不用把函数写成模板。

## 为什么需要它：指针+长度传参的老毛病

C/C++ 里传「一段内存」给函数，老办法是 `void f(T* ptr, std::size_t n)`。这招能跑，但毛病不少：长度 `n` 的单位是元素还是字节得靠注释或猜；函数会不会修改数据看 `T*` 还是 `const T*`，容易漏；调用方传错长度没有任何编译期保护；而且这俩参数得成对传、成对记。span 把指针和长度打包进一个对象，类型（`span<const T>` vs `span<T>`）直接表达只读/可写意图，长度跟着对象走，丢不了。

```cpp
// 老办法：长度单位、只读与否全靠注释
void process_old(const uint8_t* buf, std::size_t n);

// span 办法：类型即语义
void process(std::span<const uint8_t> buf);   // 明确：只读，长度内建
void mutate(std::span<uint8_t> buf);          // 明确：会改，长度内建
```

这比写 `template<class C> void process(const C& c)` 也更省事——不用为每种容器实例化一份，避免编译膨胀。

## 动态 extent 与静态 extent

span 有两种形态，区别在「长度是运行时存还是编译期定」。`std::span<T>`（完整写法 `std::span<T, std::dynamic_extent>`）是**动态 extent**：长度作为成员存着，运行时任意；`std::span<T, N>` 是**静态 extent**：长度 `N` 编译期定死，不在对象里存。

这个区别会直接体现在 `sizeof` 上——咱们待会儿跑跑看。动态 extent 要存指针 + size（两个字），静态 extent 只存指针（size 编译期已知，省掉）。日常里动态 extent 更常用（数据长度往往运行时才定），静态 extent 适合「我知道就是 N 个」的场合，能省一个字的存储，还能换来一点编译期检查。

```cpp
int arr[4];
std::span<int, 4> s_fixed(arr);     // 只能绑长度 4 的数据
std::span<int>    s_dyn(arr);       // 任意长度，运行时记 4
```

## 接收任意连续来源：array / vector / C 数组 / 指针+长度

span 的构造函数覆盖了几乎所有连续数据来源，这让函数参数用 `span` 能一统江湖：

```cpp
void print(std::span<const int> s);

int buf[] = {0x10, 0x20, 0x30};
std::array<int, 3> a = {1, 2, 3};
std::vector<int>   v = {4, 5, 6, 7};
int* p = v.data();

print(buf);                 // C 数组（自动推 N）
print(a);                   // std::array
print(v);                   // std::vector
print({p, 2});              // 指针 + 长度
```

调用方不用拷贝数据，函数内部也不用为每种容器写重载或模板。注意 `span<const T>` 表示只读视图——如果函数要改数据，用 `span<T>`（非 const）。

## subspan、first、last：零拷贝切片

span 提供 `subspan(offset, count)`、`first(n)`、`last(n)` 三件套，返回的是新的 span（还是非拥有视图），不拷贝任何数据。这在协议解析、缓冲区处理里特别顺手——把一个大 buffer 切成 header / payload，各自当 span 传下去：

```cpp
void recv_packet(std::span<uint8_t> buffer)
{
    if (buffer.size() < 4) {
        return;
    }
    auto header  = buffer.first(4);          // 前 4 字节视图
    uint16_t len = static_cast<uint16_t>(header[2] | (header[3] << 8));
    if (buffer.size() < 4 + len) {
        return;
    }
    auto payload = buffer.subspan(4, len);   // 跳过 header 取 payload 视图
    // payload 仍是非拥有视图，零拷贝
}
```

整个过程中没有任何字节被拷贝，切出来的 header / payload 都指向原 buffer 内部。

## 字节视图：as_bytes / as_writable_bytes

处理二进制数据时，常需要把 `span<T>` 当成原始字节看。`std::as_bytes(s)` 返回 `span<const std::byte>`，`std::as_writable_bytes(s)` 返回 `span<std::byte>`（仅当 T 非 const 时可用）。这对 CRC、序列化、内存 dump 这类「把结构当字节流」的场景很合适：

```cpp
std::span<int> data = /* ... */;
auto bytes = std::as_bytes(data);          // span<const std::byte>，只读字节
// crc(bytes.data(), bytes.size());
```

注意区分只读和可写：读用 `as_bytes`，要原地改字节用 `as_writable_bytes`（且底层 span 必须 non-const）。

## 生命周期：span 不拥有，悬垂会咬人

span 最大的坑，也是它「非拥有」性质的必然代价：**它不管理底层内存的生命周期**。底层活多久，span 就最多活多久；底层没了，span 就是悬垂视图，访问就是未定义行为。最经典的错误是 span 绑了一个临时对象，然后把它返回出去：

```cpp
std::span<int> bad()
{
    std::vector<int> v = {1, 2, 3};
    return v;   // v 在函数结束时销毁，返回的 span 立刻悬垂
}
```

调用方拿到这个 span 再访问，就是访问已释放内存。记住这条铁律：**span 的生命周期不得超过它所指向的数据**。只要你不把 span 绑到临时量、不把它存得比底层数据久，它就是安全的。

## 跑跑看：动态 vs 静态 extent 的 sizeof

前面说动态 extent 存两个字、静态 extent 只存指针，咱们跑跑看：

```cpp
#include <span>
#include <iostream>

int main()
{
    int arr[4] = {};
    std::span<int>        dyn;            // 动态 extent：可默认构造（空 span）
    std::span<int, 4>     fixed(arr);     // 静态 extent：必须绑定数据
    std::cout << "sizeof(span<int>)    = " << sizeof(dyn) << '\n';
    std::cout << "sizeof(span<int,4>)  = " << sizeof(fixed) << '\n';
    std::cout << "sizeof(void*)        = " << sizeof(void*) << '\n';
    return 0;
}
```

```bash
g++ -std=c++20 -O2 -o /tmp/span_sizeof /tmp/span_sizeof.cpp && /tmp/span_sizeof
```

```text
sizeof(span<int>)    = 16
sizeof(span<int,4>)  = 8
sizeof(void*)        = 8
```

（64 位平台，GCC 16.1.1。）动态 extent 是 16 字节（一个 8 字节指针 + 一个 8 字节 size），静态 extent 只有 8 字节（就一个指针，size 编译期已知，省掉了）。这就是静态 extent 的存储优势——在大量传递 span 的场景（比如嵌入式里满地都是的 buffer 视图），省一半的字是有意义的。

## 延伸：嵌入式里的 span（DMA / 协议解析）

span 因为轻量、零拷贝、跨容器统一，在嵌入式里几乎是「现代版 buffer 指针」，这里补几个实战用法（主线之外，按需取用）。DMA 回调把数据放进固定 buffer 后，用 span 切片解析 header / payload，无需拷贝；从 Flash 读数据到缓冲区，用 span 切块处理；中断 / 实时路径里传小段数据，span 拷贝廉价（就两个字）。只要守住「span 不拥有、不超底层生命周期」这条线，它就是裸指针的安全替代。

## 临了收几句：span 和 string_view 怎么分

span 和 string_view 都是「非拥有视图」，分界看元素类型：`span<T>` 通用于任意元素类型（包括可写、包括 `std::byte`），`string_view` 专门给字符序列（只读、带字符串语义）。处理二进制 buffer / 任意类型数据用 span，处理文本用 string_view。一句话记 span：它是指针加长度的正式封装，传参统一、切片零拷贝，但你得自己管好生命周期。

## 参考资源

- [std::span — cppreference](https://en.cppreference.com/w/cpp/container/span)
- [std::byte — cppreference](https://en.cppreference.com/w/cpp/types/byte)
- [P0122 span 提案 — open-std](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2018/p0122r7.pdf)
