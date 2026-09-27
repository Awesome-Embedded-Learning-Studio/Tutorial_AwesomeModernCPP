---
chapter: 12
cpp_standard:
- 11
- 14
- 17
- 20
description: 理解对齐规则和 sizeof 计算方法，掌握 alignas/alignof 的用法
difficulty: intermediate
order: 3
platform: host
prerequisites:
- 动态内存管理
reading_time_minutes: 15
tags:
- cpp-modern
- host
- intermediate
- 进阶
title: 内存对齐与填充
---
# 内存对齐与填充：多出来的字节去哪了

本章第一篇咱们把程序的内存空间拆成了栈、堆、静态区和代码段四大区域，搞清楚了数据"住在哪里"和"活多久"。

但是还有个问题，您可能想不到。**就是数据其实不是随意放置的，对于X64，我们是有严格的内存对齐要求的！**，说人话的版本就是：即便数据都在同一块内存区域里，它也不是想怎么摆放就怎么摆放的。您要是写过一阵子 C++，大概率遇到过这样的困惑：一个结构体明明只有三个成员，`sizeof` 出来的结果却比三个成员大小之和大了不少。见了鬼了，多出来的那些字节去哪了？

当当！答案，就是这一章的主题：**对齐（alignment）和填充（padding）**。编译器为了满足 CPU 访问内存的效率要求，会在结构体的成员之间插入"空白"字节，把每个成员对齐到特定地址边界上。

这些空白字节其实**不存储任何有效数据**，但它们**实实在在占据了内存空间**。理解对齐规则，既能让您准确预测 `sizeof` 的结果，又能在性能敏感的场景下通过调整成员顺序来减小结构体的大小。这种优化不需要改一行逻辑代码，仅仅是把成员声明的顺序换一下，就能省下可观的内存。

## 对齐——CPU 和内存之间的默契

要理解对齐，咱们得先看看 CPU 是怎么访问内存的。很多人以为 CPU 可以按字节自由读写任意地址的数据。从程序员的角度看确实如此，但底层硬件并不是这样工作的。现代 CPU 通过总线访问内存时，通常以字（word）为单位进行传输。一个 32 位 CPU 一次能读写 4 个字节，一个 64 位 CPU 一次能读写 8 个字节，而且硬件上往往要求这次读写的起始地址是字大小的整数倍。

咱们可以把内存想象成一排储物柜，每个柜子 4 格宽。要拿一个占了 4 格的物品（也就是一个 `int`），最快的方式是让它恰好从一个柜子的起始位置开始放，这样打开一个柜子就能一次拿完。但这个 `int` 要是跨越了两个柜子的边界，前两个格在第一个柜子里，后两个格在第二个柜子里，CPU 就得打开两个柜子，分别取出一部分，再拼起来返回。某些架构（比如 ARM）甚至直接拒绝这种跨越边界的访问，抛出一个硬件异常。

咱们看这就是对齐的底层原因：**CPU 访问对齐地址上的数据效率最高，访问未对齐地址要么变慢，要么直接报错**。所以编译器在安排结构体的内存布局时，会主动把每个成员放到满足其对齐要求的位置上，中间多出来的空间就是填充字节。

## 对齐规则——编译器是怎么填空白的

咱们看每种基本类型都有一个**自然对齐要求（natural alignment）**，它通常等于该类型的大小。`char` 是 1 字节对齐（放在哪里都行），`int` 是 4 字节对齐（地址必须是 4 的倍数），`double` 是 8 字节对齐（地址必须是 8 的倍数）。指针在 64 位系统上是 8 字节对齐，在 32 位系统上是 4 字节对齐。

我们来看看编译器是怎么处理的吧！相信这样对大家友好一些。毕竟，编译器处理我们的代码，对吧！编译器拿到一个结构体，首先就是遵守三个重要的规则。第一关，是这个：

> 结构体的每个成员都必须放在它自己自然对齐要求的整数倍地址上。前一个成员结束的位置要是不满足下一个成员的对齐要求，编译器就在两者之间插入填充字节，直到地址满足条件。

啥意思呢？简单的说就是：每个成员的起始地址，得是**它自己对齐值的整数倍**。前一个成员结束的位置要是没落在这样的整数倍上，编译器就往中间塞填充字节，垫到下一个成员能落在合格的地址上为止。咱们拿最小的组合排一遍，就两个成员，`char` 打头，`int` 跟在后面，够简单了！

```cpp
struct Tiny {
    char a;   // 1 字节对齐
    int  b;   // 4 字节对齐
};
```

咱们可爱的编译器开始排布内存：从 `a` 这里开始。`a` 占掉偏移量 0 这 1 个字节，轮到了 `b` 这个成员，它的大小是 4 个字节，所以它就要求起始地址是 4 的倍数了，采纳偏移量 1 这个决策是不合格的，只能排到偏移量 4 上去了。您看这个中间的偏移量 1、2、3，就是编译器垫进去的 3 个填充字节。`b` 排在偏移量 4、占 4 个字节，正好收在 8 上，`sizeof(Tiny)` 就是 8。画出来是这样：

![Tiny 的内存布局](./03-tiny-padding.drawio)

第二关，是这个：

> 结构体本身的整体大小必须是它最大成员对齐要求的整数倍。也就是说，结构体里要是有一个 `double`（8 字节对齐），那整个结构体的大小必须是 8 的倍数，哪怕最后一个成员后面还有空余空间，也要用填充字节补齐。

成员排完，还不算完！整个结构体的大小**也得是最大对齐值的整数倍**，差了几个字节，就在尾巴上补几个填充。太干吧了对吧，咱们还是排一遍，`int` 打头、`char` 收尾，还是那两个成员，就是把顺序倒了过来：

```cpp
struct Flip {
    int  b;   // 4 字节对齐
    char a;   // 1 字节对齐
};
```

咱们把 `Flip` 排开。`int` 占偏移量 0 到 3，`char` 占偏移量 4，排完一共 5 个字节；5 不是 4 的倍数，尾巴上补 3 个字节，OK，所以凑成 8，这下知道这个 8 咋来的吧！

![Flip 的内存布局](./03-flip-padding.drawio)

跟 `Tiny` 一样，`Flip` 也是 8 个字节，只是填充从中间挪到了尾巴上。这就是第二关的规则。

第三关，是这个：

> 结构体自身的对齐要求等于它最大成员的对齐要求。这个规则影响的是"当这个结构体作为另一个结构体的成员时，它应该放在哪里"。

结构体里既然装着要按 4 对齐的 `int`，整个结构体就不能乱放，**得先落到 4 的倍数地址上**，里面的成员才能各自落在合格的地址上。所以结构体自己的对齐值，取最大成员的那个就够了。咱们还是排一遍，这回我们把上一条的 `Flip` 整个，请到一个新的结构体上去，孩子，让您来当一回成员！

```cpp
struct Outer {
    char tag;  // 1 字节对齐
    Flip in;   // 上一条那个 Flip：8 字节、4 字节对齐
};
```

咱们接着排。`tag` 占掉偏移量 0 这 1 个字节；轮到 `in`，它跟 `Tiny` 里那个 `b` 一样，只能排到偏移量 4，中间的偏移量 1、2、3 又是 3 个填充字节，`in` 自己占 8 个字节，一路占到偏移量 11。画出来是这样：

![Outer 的内存布局](./03-outer-padding.drawio)

而且 `Outer` 自己的对齐值也因此是 4，结构体套结构体，规则就这么一层层接着用。`sizeof(Outer)` 您现在也能口算了：1 加 3 个填充再加 8，正好 12 个字节，不多不少，好极了!

三条规则单看都不难，合到一个结构体上是什么效果，咱们直接看代码。

## sizeof 的真相——填充字节藏在哪

来看一个经典的例子，可能是您在面试题里见过的那种：

```cpp
struct BadLayout {
    char a;   // 1 字节
    int  b;   // 4 字节
    char c;   // 1 字节
};
```

三个成员加起来是 `1 + 4 + 1 = 6` 个字节，但 `sizeof(BadLayout)` 在大多数平台上是 **12**。多出来的 6 个字节全是填充。咱们逐个成员分析一下编译器到底做了什么。

咱们看 `a` 是 `char`，1 字节对齐，放在偏移量 0 的位置，占 1 个字节。接下来到 `b`，它是 `int`，需要 4 字节对齐，也就是说它的起始偏移量必须是 4 的倍数。但 `a` 只占到了偏移量 1，所以编译器在偏移量 1、2、3 的位置插入 3 个填充字节，把 `b` 放到偏移量 4，占据偏移量 4、5、6、7。然后到 `c`，`char` 只要 1 字节对齐，跟在 `b` 后面没问题，放在偏移量 8，占 1 个字节。

咱们看到目前为止一共用了 9 个字节。但是别忘了第二条规则：结构体的整体大小必须是最大成员对齐要求的整数倍。这里最大对齐是 `int` 的 4 字节，所以结构体大小得是 4 的倍数，9 不是 4 的倍数，于是编译器在末尾再填 3 个字节，凑到 12。画成图就是这样的：

![BadLayout 的内存布局](./03-badlayout-padding.drawio)

成员声明顺序直接影响填充量和结构体大小。面试常考，实战中更常踩——特别是在网络协议、文件格式等需要精确控制内存布局的场景下，不注意成员顺序可能导致数据对不上。更关键的是，咱们把结构体直接 `memcpy` 发送出去，接收端用不同编译器解析，填充规则可能不一样，数据直接错位。

现在咱们把成员顺序调一下，把大的放前面：

```cpp
struct GoodLayout {
    int  b;   // 4 字节
    char a;   // 1 字节
    char c;   // 1 字节
};
```

咱们看 `b` 在偏移量 0，占 4 字节；`a` 在偏移量 4，1 字节对齐，没问题。`c` 紧随其后放在偏移量 5。到此为止用了 6 个字节，整体大小需要是 4 的倍数——补 2 个字节到 8。`sizeof(GoodLayout)` 是 **8**，比刚才的 12 少了三分之一。

![GoodLayout 的内存布局](./03-goodlayout-padding.drawio)

仅仅是换了成员声明顺序，没改任何逻辑，结构体就瘦了 4 个字节。咱们的程序里要有百万个这样的对象，省下来的就是 4 MB 内存。所以一条实用的经验法则是：**按对齐要求从大到小排列成员**——把 `double`、`int64_t` 放最前面，然后是 `int`、`float`，最后是 `char` 和 `bool`。

## alignas 和 alignof——手动控制对齐

编译器的默认对齐规则在绝大多数情况下已经足够好了，但有些场景需要咱们手动干预。C++11 引入了 `alignas` 和 `alignof` 两个关键字，分别用来指定对齐要求和查询对齐要求。

`alignof` 的用法很简单，给它一个类型，它返回那个类型的对齐要求（字节数）。`alignof(int)` 是 4，`alignof(double)` 是 8，`alignof(char)` 是 1。您甚至可以对结构体使用它：`alignof(GoodLayout)` 返回 4，因为它的最大成员 `int` 是 4 字节对齐。

咱们看 `alignas` 则用来强制指定对齐。它可以用在变量声明上，也可以用在类型定义上：

```cpp
// 强制单个变量按 16 字节对齐
alignas(16) char buffer[1024];

// 强制结构体类型按 64 字节对齐（一个缓存行的大小）
struct alignas(64) CacheLine {
    int data[14];  // 56 字节 + 编译器自动补齐到 64
};
```

`alignas` 最典型的应用场景有三个。第一个是 SIMD 指令：SSE 要求操作数 16 字节对齐，AVX 要求 32 字节对齐，AVX-512 要求 64 字节对齐。咱们的数据要是没有按要求的边界对齐，SIMD 加载指令会直接抛出硬件异常，程序当场崩溃。第二个是缓存行：现代 CPU 的缓存行通常是 64 字节，数据结构要是横跨两个缓存行，一次读取就得起两次缓存加载，把它对齐到缓存行边界就能避免这种跨界。多线程场景下还有个名字容易混的坑要分清：伪共享（false sharing）说的是两个线程各写各的变量、变量却挤在同一个缓存行里，缓存行在两个核之间来回失效，性能白白掉一截——它的解法不是对齐边界，而是用 `alignas(64)` 把不同线程写的变量隔到不同的缓存行上。第三个是硬件交互：某些 DMA 控制器或外设要求缓冲区的物理地址必须是特定对齐的，这时候就得用 `alignas` 来保证。

`alignas` 只能增大对齐要求，不能减小。`alignas(1) int x;` 不会真的把 `int` 变成 1 字节对齐，编译器会忽略这个请求，因为 `int` 的自然对齐就是 4。咱们要是试图写 `alignas(3)` 这种不是 2 的幂次的值，编译器会直接报错。

咱们再看 C++17 引入的 `std::aligned_storage`（C++23 起被 deprecated，建议直接用 `alignas`），以及 `<memory>` 里的 `std::align` 函数，用于在运行时在给定的缓冲区中找到一个满足对齐要求的地址。这些工具在实现自定义分配器或类型擦除容器（比如 `std::any` 的底层存储）时非常实用。

## 打包结构体——pragma pack 的双刃剑

有时候咱们确实不想要任何填充。。。有做网络编程和二进制安全的朋友嘛！咱们代表性的例子就是比如网络协议的头部结构、二进制文件格式、或者跟硬件寄存器映射一一对应的结构体。这种情况下可以用 `#pragma pack` 来告诉编译器：别给我加填充了。

```cpp
#pragma pack(push, 1)  // 保存当前对齐设置，然后设为 1 字节对齐
struct RawHeader {
    uint8_t  version;   // 偏移 0
    uint16_t length;    // 偏移 1（不再是 2 的倍数！）
    uint32_t checksum;  // 偏移 3（不再是 4 的倍数！）
};
#pragma pack(pop)       // 恢复之前的对齐设置
```

咱们看 `sizeof(RawHeader)` 现在是 `1 + 2 + 4 = 7`，没有任何填充。每个成员紧紧挨着前一个成员，内存布局完全紧凑。这种写法在网络编程和二进制文件解析中非常常见。

但 `#pragma pack` 是一把真正的双刃剑，咱们用不好的代价相当惨痛。

对打包结构体的成员取引用（reference）是未定义行为。考虑 `uint32_t& ref = header.checksum;`：`checksum` 在偏移量 3 上，不是一个 4 的倍数，而 `uint32_t&` 要求它指向的地址必须是 4 字节对齐的。编译器可能会生成假设地址已对齐的 SIMD 指令，导致程序在某些架构上崩溃，或者在另一些架构上默默返回错误数据。咱们需要读取打包结构体中的成员时，先把它的值拷贝到一个局部变量里再使用，不要直接绑引用。

打包后的结构体在某些平台上访问未对齐成员会触发总线错误（bus error），在 x86 上虽然硬件会处理未对齐访问，但性能会下降。咱们要是只想减小结构体大小，优先考虑调整成员顺序，而不是用 `#pragma pack`。`#pragma pack` 应该只用于"内存布局必须精确匹配外部格式"的场景。

## 动手验证——alignment.cpp

现在咱们把上面的知识综合起来，写一个完整的程序来验证各种对齐行为。这个程序定义了多个结构体，打印它们的 `sizeof` 和成员偏移量，让您直观地看到填充字节的位置，同时演示如何通过重排成员来优化布局。

```cpp
// alignment.cpp
// 编译: g++ -std=c++17 -O0 alignment.cpp -o alignment && ./alignment

#include <cstddef>
#include <cstdint>
#include <iostream>

// --- 结构体定义 ---

struct BadLayout {
    char  a;
    int   b;
    char  c;
};

struct GoodLayout {
    int   b;
    char  a;
    char  c;
};

struct alignas(16) AlignedBuffer {
    int data[3];  // 12 字节，补齐到 16
};

#pragma pack(push, 1)
struct PackedHeader {
    uint8_t  version;
    uint16_t length;
    uint32_t crc;
};
#pragma pack(pop)

struct MixedTypes {
    char    flag;
    double  value;
    int     count;
    short   id;
};

struct ReorderedMixed {
    double  value;
    int     count;
    short   id;
    char    flag;
};

// --- 工具函数 ---

/// 打印结构体信息和成员偏移量
template <typename T>
void print_struct_info(const char* name)
{
    std::cout << name << ":\n";
    std::cout << "  sizeof = " << sizeof(T)
              << ", alignof = " << alignof(T) << "\n";
}

int main()
{
    std::cout << "=== sizeof 和 alignof 对比 ===\n\n";

    print_struct_info<BadLayout>("BadLayout");
    std::cout << "  偏移量: a=" << offsetof(BadLayout, a)
              << ", b=" << offsetof(BadLayout, b)
              << ", c=" << offsetof(BadLayout, c) << "\n\n";

    print_struct_info<GoodLayout>("GoodLayout");
    std::cout << "  偏移量: b=" << offsetof(GoodLayout, b)
              << ", a=" << offsetof(GoodLayout, a)
              << ", c=" << offsetof(GoodLayout, c) << "\n\n";

    print_struct_info<AlignedBuffer>("AlignedBuffer");
    std::cout << "  偏移量: data=" << offsetof(AlignedBuffer, data) << "\n\n";

    print_struct_info<PackedHeader>("PackedHeader");
    std::cout << "  偏移量: version=" << offsetof(PackedHeader, version)
              << ", length=" << offsetof(PackedHeader, length)
              << ", crc=" << offsetof(PackedHeader, crc) << "\n\n";

    print_struct_info<MixedTypes>("MixedTypes");
    std::cout << "  偏移量: flag=" << offsetof(MixedTypes, flag)
              << ", value=" << offsetof(MixedTypes, value)
              << ", count=" << offsetof(MixedTypes, count)
              << ", id=" << offsetof(MixedTypes, id) << "\n\n";

    print_struct_info<ReorderedMixed>("ReorderedMixed");
    std::cout << "  偏移量: value=" << offsetof(ReorderedMixed, value)
              << ", count=" << offsetof(ReorderedMixed, count)
              << ", id=" << offsetof(ReorderedMixed, id)
              << ", flag=" << offsetof(ReorderedMixed, flag) << "\n\n";

    std::cout << "=== 优化效果 ===\n";
    std::cout << "BadLayout  -> GoodLayout: "
              << sizeof(BadLayout) << " -> " << sizeof(GoodLayout)
              << " (节省 " << sizeof(BadLayout) - sizeof(GoodLayout)
              << " 字节)\n";
    std::cout << "MixedTypes -> ReorderedMixed: "
              << sizeof(MixedTypes) << " -> " << sizeof(ReorderedMixed)
              << " (节省 " << sizeof(MixedTypes) - sizeof(ReorderedMixed)
              << " 字节)\n";

    return 0;
}
```

咱们把完整代码放在下面，点"动手试一试"直接跑：

<OnlineCompilerDemo
  title="动手验证：alignment.cpp"
  source-path="code/examples/vol1/29_alignment.cpp"
  description="在线打印各结构体的 sizeof、alignof 和成员偏移量。您可以试着照 GoodLayout 的样子重排 BadLayout，亲眼看填充字节消失。"
  run-options="-O0 -std=c++17"
  allow-run
/>

咱们看 `BadLayout` 有 6 字节的填充（a 后面 3 字节，c 后面 3 字节），而 `GoodLayout` 只有 2 字节的尾部填充。`MixedTypes` 的情况更夸张——一个 `char` 和一个 `double` 之间被塞了 7 字节的填充，整体膨胀到了 24 字节，而 `ReorderedMixed` 只需要 16 字节。这就是成员排序的威力：同样的数据，不同的排列方式，内存占用可以相差 33% 甚至更多。

`PackedHeader` 则展示了打包的效果。我们没有任何填充，大小正好等于所有成员之和，但注意它的对齐要求变成了 1，这意味着它出现在另一个结构体里时可以放在任意位置。`AlignedBuffer` 展示了 `alignas(16)` 的效果：虽然数据只有 12 字节，但整个结构体被强制对齐到 16 字节边界，大小也是 16。

## 练习

### 练习 1：手动计算 sizeof

请您在不编译的情况下，预测以下每个结构体的 `sizeof` 和每个成员的偏移量：

```cpp
struct X {
    char   a;
    double b;
    int    c;
};

struct Y {
    double a;
    int    b;
    char   c;
};

struct Z {
    char a;
    char b;
    int  c;
    int  d;
};
```

然后用代码验证您的预测，毕竟瞪眼法瞪不出来问题，对吧，哈哈。

### 练习 2：优化结构体布局

以下结构体在 64 位系统上的 `sizeof` 是多少？请您重新排列成员使其尽可能小：

```cpp
struct Monster {
    bool     is_alive;
    double   health;
    char     name[16];
    int      level;
    float    speed;
    uint64_t experience;
};
```
