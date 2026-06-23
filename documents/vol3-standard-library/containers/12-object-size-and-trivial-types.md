---
chapter: 7
cpp_standard:
- 11
- 14
- 17
- 20
description: 讲透 sizeof/alignof 与内存填充、trivial/trivially_copyable/standard-layout 的精确区分、POD
  的拆分、何时能安全 memcpy、聚合初始化与 C++20 指定初始化
difficulty: intermediate
order: 12
platform: host
reading_time_minutes: 8
related:
- array：编译期固定大小的聚合容器
tags:
- host
- cpp-modern
- intermediate
- 类型安全
- 容器
title: 对象大小、对齐与平凡类型
---
# 对象大小、对齐与平凡类型

写底层代码、和 C 接口打交道、或优化内存占用时，常被一串看似晦涩的名词绕晕：`sizeof`、`alignof`、`alignas`、`trivial`、`standard-layout`、`trivially_copyable`、聚合（aggregate）……这些概念看起来零碎，其实是一张互相勾连的地图：它们决定对象的内存表示、拷贝语义、能否安全 `memcpy`、能否与 C 结构体 ABI 兼容、以及初始化的灵活性。这一篇把它们理顺。

## 大小与对齐：为什么 sizeof 不总是成员之和

`sizeof(T)` 报的是对象在内存中**占据的字节数**（完整对象表示，含必要的填充），`alignof(T)` 报的是该类型的**对齐约束**——对象起始地址必须是 `alignof(T)` 的整数倍。为了让每个成员都落在自己要求的对齐上，成员之间、以及结构尾部，可能需要填充（padding）。

看一个最常见的例子：

```cpp
struct A {
    char c;   // 1 字节，offset 0
    int  i;   // 4 字节，对齐 4，offset 4
};
// offset 0: c，offset 1..3: 填充，offset 4..7: i
// sizeof(A) == 8
```

如果把顺序换一下，填充会变多：

```cpp
struct B {
    char a;   // offset 0
    int  i;   // offset 4（前面填 3 字节）
    char b;   // offset 8
};
// 尾部还要填 3 字节，让 sizeof 是 alignof(B)=4 的倍数
// sizeof(B) == 12
```

把两个 `char` 放在一起，填充就省下来了：

```cpp
struct C {
    char a;   // offset 0
    char b;   // offset 1
    int  i;   // offset 4（前面填 2 字节）
};
// sizeof(C) == 8
```

同样的成员，只是换了声明顺序，`B` 占 12 字节、`C` 只占 8 字节——这就是「合理安排成员顺序省内存」的来源。结构的整体对齐是它成员中**最大对齐**的值，编译器还会在尾部加填充，保证 `sizeof(T)` 是 `alignof(T)` 的倍数（这关系到数组里元素的间隔）。

可以用 `alignas` 强制改变对齐，比如给 SIMD 缓冲区指定 16 字节对齐：

```cpp
struct alignas(16) Vec4 {
    float x, y, z, w;   // sizeof == 16，alignof == 16
};
```

`alignas` 要小心：提高对齐会改变 `sizeof` 和 ABI，在要求对齐访问的硬件上把对象放到不对齐的地址可能直接崩溃。

## trivial / trivially_copyable / standard-layout：三个容易混的概念

C++ 标准把一组「类型属性」拆开来，精确表达「这个类型的对象在内存里怎么行为」。这是 C++11 的设计（把历史上的 POD 拆成几件事）。先把几个常被混淆的词摆清楚：

- **trivial（平凡）类型**：特殊成员（默认构造、拷贝/移动构造、赋值、析构）都是编译器生成的、没有自定义逻辑。换句话说，构造/拷贝/析构不产生任何运行时代码——对象的比特位就是它的全部，没有隐藏动作。
- **trivially_copyable（可平凡拷贝）类型**：可以安全地用 `memcpy` 按字节拷贝（拷完目标有同样的对象表示，且能正常析构）。**这是能否用 `memcpy` 的判据**。根据C++23草案:
<https://eel.is/c++draft/class.prop#11>, 大致有三个要求:
  1. 至少有一个拷贝/移动构造或赋值操作不是 deleted
  2. 所有存在的（eligible）拷贝/移动构造函数和拷贝/移动赋值运算符都是 trivial.
  3. 析构函数是 trivial.
- **standard-layout（标准布局）类型**：有可预测的内存布局规则（成员按声明顺序排布、没有复杂的访问控制 / 虚继承 / 多重基类导致的不确定布局）。**这是能否和 C struct 布局兼容的判据**。

一个关键事实：老概念 `POD`（Plain Old Data）在 C++11 被拆成了 `trivial` 和 `standard-layout`，`POD` 在语义上就是「既 trivial 又 standard-layout」。所以那些和 ABI、C 互操作相关的安全假设，现在用 `std::is_standard_layout_v<T>` 和 `std::is_trivially_copyable_v<T>` 分别检查。

举个把它们串起来的例子：

```cpp
struct S {
    int    x;
    double y;
    // 没有用户定义构造/析构/拷贝、没有虚函数、没有基类
};
// S 通常是 trivial、trivially_copyable、standard-layout -> POD
static_assert(std::is_trivially_copyable_v<S>);
static_assert(std::is_standard_layout_v<S>);
```

对比一个非平凡的：

```cpp
struct T_0 {
    int x;
};
// T_0 是 trivial且trivially_copyable
static_assert(std::is_trivial_v<T_0>);
static_assert(std::is_trivially_copyable_v<T_0>);

struct T_1 {
    T_1() { /* 自定义构造 */ }
    int x;
};
// T 不是 trivial（用户定义了构造），但在这个例子中是trivially_copyable
// 注意，默认构造函数完全不在检查范围内。
static_assert(!std::is_trivial_v<T_1>);
static_assert(std::is_trivially_copyable_v<T_1>);

struct T_2 {
    T_2() {}
    T_2(const T_2&) { /* 自定义拷贝构造 */ }
    int x;
};
static_assert(!std::is_trivial_v<T_2>);
static_assert(!std::is_trivially_copyable_v<T_2>);
```

再强调一条易错的：**trivial ≠ trivially_copyable**，前者强调特殊成员（尤其默认构造）的「平凡性」，后者强调按字节复制是否安全。判断能不能 `memcpy`，用 `std::is_trivially_copyable_v<T>`，别用 `is_trivial`。

## 跑跑看：布局与类型属性实测

光说 `sizeof(B)==12`、`sizeof(C)==8` 太抽象，咱们用 `static_assert` 把这些假设钉进编译期，再跑出来看一眼：

```cpp
#include <type_traits>
#include <cstdint>
#include <iostream>

struct A { char c; int i; };
struct B { char a; int i; char b; };
struct C { char a; char b; int i; };
struct alignas(16) Vec4 { float x, y, z, w; };
struct S { int x; double y; };
struct T { T() {} int x; };

static_assert(sizeof(A) == 8);
static_assert(sizeof(B) == 12);
static_assert(sizeof(C) == 8);
static_assert(sizeof(Vec4) == 16 && alignof(Vec4) == 16);
static_assert(std::is_trivially_copyable_v<S> && std::is_standard_layout_v<S>);
static_assert(!std::is_trivial_v<T>);

int main()
{
    std::cout << "sizeof(A)=" << sizeof(A) << " sizeof(B)=" << sizeof(B)
              << " sizeof(C)=" << sizeof(C) << " sizeof(Vec4)=" << sizeof(Vec4) << '\n';
    return 0;
}
```

```bash
g++ -std=c++20 -O2 -o /tmp/object_size_test /tmp/object_size_test.cpp && /tmp/object_size_test
```

```text
sizeof(A)=8 sizeof(B)=12 sizeof(C)=8 sizeof(Vec4)=16
```

`static_assert` 全部成立（编译通过就说明 A=8、B=12、C=8、Vec4=16、S 既平凡可拷贝又标准布局、T 非平凡——这些假设全对）。这也是这类知识的正确用法：**把你对布局/类型的假设，用 `static_assert` 写进代码**，假设一变编译就拦住你，比注释靠谱得多。

## 聚合与指定初始化：从花括号到 C++20

聚合（aggregate）是一类方便的类型：它允许用花括号直接列出成员来初始化（aggregate initialization），在写数据描述（配置结构、寄存器映射）时极其直观，也天然适合 `constexpr`。直观地说，聚合就是「没有用户自定义构造函数、没有虚函数、非静态成员都是 public、没有基类（或满足标准布局限制）」的类型——编译器可以简单地把初始化值按成员顺序拷进对象表示。

```cpp
struct Point { int x, y; };
Point p1{1, 2};    // 聚合初始化，成员按声明顺序赋值

struct Config { int baud; int parity; int stop_bits; };
constexpr Config default_cfg{115200, 0, 1};   // 还能 constexpr
```

C++20 引入了**指定初始化**（designated initializer，C 早就有，C++20 才正式纳入），让聚合初始化更可读、对成员顺序不敏感：

```cpp
struct S { int a, b, c; };
S s1{.b = 2, .a = 1, .c = 3};   // 成员顺序无所谓
S s2{.a = 1};                   // 只初始化 a，其余默认/零初始化
```

嵌套结构和数组下标也能指定，初始化复杂布局（寄存器表、协议头）时特别顺手：

```cpp
struct Header { uint16_t id; uint16_t flags; };
struct Packet { Header hdr; uint8_t payload[8]; };

Packet pkt{
    .hdr     = {.id = 0x1234, .flags = 0x1},
    .payload = {[0] = 0xAA, [3] = 0x55}   // 只给第 0、3 个元素赋值
};
```

注意：指定初始化只适用于**聚合类型**，有用户自定义构造函数的类用不了这个语法。

## 把它们用起来：类型属性的实战原则

把上面这些点串成几条可操作的原则。第一，定义要和 C 交互或走 DMA 的数据结构（寄存器映射、协议头、序列化格式）时，确保它是 **standard-layout**（布局可预期）且最好 **trivially_copyable**（能 memcpy 或把一块内存直接 reinterpret 成它）——避免虚函数、避免私有非静态成员、别写自定义构造/析构/拷贝，并在接口处用 `static_assert` 把这些不变量钉死：

```cpp
static_assert(std::is_standard_layout_v<MyRegs>);
static_assert(std::is_trivially_copyable_v<MyRegs>);
```

第二，对齐会影响 `sizeof` 和数组布局。硬件或 DMA 要特殊对齐（16 字节 cache line、SIMD）就用 `alignas` 明确指定，并记得它会改变 `sizeof` 和 ABI。

第三，初始化优先用花括号和指定初始化，可读、抗成员顺序变动、还常能 constexpr。

第四，拷贝语义：**只有 `trivially_copyable` 的类型，才能安全地 `memcpy(&dst, &src, sizeof(T))`**。对含虚函数、含非平凡析构或特殊成员的类，别做二进制拷贝，老实用构造/拷贝/赋值。

## 小结

- `alignof` 决定对齐要求，`sizeof` 报真正占用（含填充）；合理安排成员顺序能省填充。
- `trivial`、`trivially_copyable`、`standard-layout` 是标准对类型属性的精细划分：要 `memcpy` 看 `trivially_copyable`，要和 C 布局兼容看 `standard-layout`，`POD` = 既平凡又标准布局。
- 聚合初始化方便；C++20 指定初始化更可读、不依赖成员顺序。
- 把对布局和类型的假设用 `static_assert` 写进代码，让编译器替你守这些不变量。

想直接上手运行看看效果？点开下面的在线示例（能运行、也能看汇编）：

<OnlineCompilerDemo
  title="对象大小与平凡类型：trivial / trivially_copyable / standard-layout"
  source-path="code/examples/vol3/12_object_size.cpp"
  description="编译期 type_traits 查类型属性、static_assert 卡约束、vptr 与对齐的 sizeof 代价"
  allow-run
/>

## 参考资源

- [类型特性（type traits） — cppreference](https://en.cppreference.com/w/cpp/header/type_traits)
- [标准布局类型 — cppreference](https://en.cppreference.com/w/cpp/language/data_members#Standard_layout)
- [指定初始化（C++20） — cppreference](https://en.cppreference.com/w/cpp/language/aggregate_initialization#Designated_initializers)
