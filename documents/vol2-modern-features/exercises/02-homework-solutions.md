---
title: "卷 2 课后练习参考答案（Homework）"
description: "现代特性（C++11/14/17）卷课后练习的逐题详细解答：27 道题每题给出解题思路、逐步解答（每步标注知识点链接）与真实验证输出（g++ 16.1.1 / WSL Arch 实跑，UB 与内存题附 ASan/UBSan 报告）。"
chapter: 2
order: 2
tags: [host, intermediate, cpp-modern, 移动语义, 类型安全]
difficulty: intermediate
platform: host
cpp_standard: [11, 14, 17]
reading_time_minutes: 119
prerequisites:
  - "卷 2 课后练习（Homework）"
related:
  - "卷 2 全部章节（第 0~11 章）"
---

# 卷 2 课后练习参考答案（Homework）

> 所有命令与输出在 WSL Arch（g++ 16.1.1，`-std=c++17`）下真实运行得到；个别对比警告块出自 clang++ 22.1.8，已就地标注。UB 类题目的输出「只是这台机器这一次的选择」，换编译器/优化级别可能不同——这正是每道题要你体会的东西。ASan 报告里的地址与进程号每次运行不同，节选时已注明。

## 2.1-A {#hw-2-1-a}

**难度 L1** · 题面见 [homework](./01-homework.md#hw-2-1-a)

**思路**：`decltype((expr))` 按值类别求类型——左值得 `T&`、xvalue 得 `T&&`、prvalue 得 `T`，套进 `is_lvalue_reference_v`/`is_rvalue_reference_v` 就能给任意表达式做体检。最反直觉的是 `rref`：它声明成右值引用，但作为**有名字的变量**是左值。

1. 写 `value_category<T>()` 模板 + `SHOW` 宏，逐行打印。→ 知识点：[右值引用：从拷贝到移动](../ch00-move-semantics/01-rvalue-reference.md)「给任意表达式做值类别体检」一节
2. `static_assert` 钉死六个关键结论：`(x)` 左值、`(std::move(x))` xvalue、`(42)`/`(x+1)` prvalue、`(++x)` 左值、`(x++)` prvalue。→ 知识点：[右值引用：从拷贝到移动](../ch00-move-semantics/01-rvalue-reference.md)「值类别的全景图」一节

```cpp
// hw210a.cpp -- Homework 2.1-A: value category probe
// Standard: C++17
#include <iostream>
#include <type_traits>
#include <utility>

template <class T>
constexpr const char* value_category()
{
    if constexpr (std::is_lvalue_reference_v<T>) {
        return "lvalue";
    } else if constexpr (std::is_rvalue_reference_v<T>) {
        return "xvalue";
    } else {
        return "prvalue";
    }
}

#define SHOW(expr) \
    std::cout << "  " #expr "  ->  " << value_category<decltype((expr))>() << "\n"

int main()
{
    int x = 10;
    int& lref = x;
    int&& rref = 20;
    const char* p = "hi";

    std::cout << "--- value category checkup ---\n";
    SHOW(x);
    SHOW(lref);
    SHOW(rref);
    SHOW(42);
    SHOW(x + 1);
    SHOW(std::move(x));
    SHOW(++x);
    SHOW(x++);
    SHOW(*p);
    SHOW("hi"[0]);
    SHOW(p);

    static_assert(std::is_lvalue_reference_v<decltype((x))>, "x is lvalue");
    static_assert(std::is_rvalue_reference_v<decltype((std::move(x)))>, "move(x) is xvalue");
    static_assert(!std::is_reference_v<decltype((42))>, "42 is prvalue");
    static_assert(!std::is_reference_v<decltype((x + 1))>, "x+1 is prvalue");
    static_assert(std::is_lvalue_reference_v<decltype((++x))>, "++x is lvalue");
    static_assert(!std::is_reference_v<decltype((x++))>, "x++ is prvalue");
    std::cout << "all static_asserts passed\n";
    return 0;
}
```

**验证输出**：

```text
$ g++ -std=c++17 -Wall -Wextra -O2 -o hw210a hw210a.cpp && ./hw210a
--- value category checkup ---
  x  ->  lvalue
  lref  ->  lvalue
  rref  ->  lvalue
  42  ->  prvalue
  x + 1  ->  prvalue
  std::move(x)  ->  xvalue
  ++x  ->  lvalue
  x++  ->  prvalue
  *p  ->  lvalue
  "hi"[0]  ->  lvalue
  p  ->  lvalue
all static_asserts passed
```

要点：`rref` 是**有名字的变量**，C++ 的规则是「有名字的表达式就是左值」；`std::move(x)` 产出的 xvalue 一旦被命名（绑定给 `rref`、或传进函数参数）就「降级」回左值。这就是为什么完美转发需要 `std::forward` 把「当时是不是右值」重新盖章。

## 2.1-B {#hw-2-1-b}

**难度 L3** · 题面见 [homework](./01-homework.md#hw-2-1-b)

**思路**：`is_move_constructible` 只问「`T&&` 能不能构造出 `T`」——没有移动构造时，右值可以绑定到 `const T&`，编译器用拷贝构造「蒙混过关」。所以 B、C 都是 1，但性质完全不同：B 的「移动」是隐式拷贝构造（浅拷贝，double free），C 的移动是深拷贝。

1. 定义 A/B/C 三个类型 + 四个 trait 的报表函数。→ 知识点：[移动构造与移动赋值](../ch00-move-semantics/02-move-semantics.md)「规则五（Rule of Five）」一节
2. 预测：B、C 的 `is_move_constructible` 都是 1；B 的 `is_nothrow_move_constructible` 实测是 **1**——因为它顶上来的隐式拷贝构造（逐成员拷贝指针）恰好不抛异常，这是「运气好」，不是「有移动构造」；C 的自定义拷贝构造没标 `noexcept`，所以是 0。→ 知识点：[移动构造与移动赋值](../ch00-move-semantics/02-move-semantics.md)（`is_move_constructible` 为 true 是因为右值能绑 `const T&`）
3. B 的 double free 实测：`B b = std::move(a);` 两个 `data_` 指向同一块内存，ASan 当场按住。→ 知识点：[移动构造与移动赋值](../ch00-move-semantics/02-move-semantics.md)「规则五」一节（隐式拷贝构造做浅拷贝 → 双重 delete）

```cpp
// hw211b.cpp -- Homework 2.1-B: Rule of Five checkup
// Standard: C++17
#include <iostream>
#include <string>
#include <type_traits>
#include <vector>

// A: everything defaulted, members are standard library types
struct A
{
    std::string name;
    std::vector<int> data;
};

// B: only a user-declared destructor
struct B
{
    char* data_;
    explicit B(std::size_t n) : data_(new char[n]) {}
    ~B() { delete[] data_; }
};

// C: user destructor + copy, no move declared
struct C
{
    char* data_;
    explicit C(std::size_t n) : data_(new char[n]) {}
    C(const C& other) : data_(new char[1]) { *data_ = *other.data_; }
    ~C() { delete[] data_; }
};

template <class T>
void report(const char* label)
{
    std::cout << label << ":\n";
    std::cout << "  is_move_constructible            = "
              << std::is_move_constructible_v<T> << "\n";
    std::cout << "  is_nothrow_move_constructible   = "
              << std::is_nothrow_move_constructible_v<T> << "\n";
    std::cout << "  is_trivially_move_constructible = "
              << std::is_trivially_move_constructible_v<T> << "\n";
    std::cout << "  is_copy_constructible            = "
              << std::is_copy_constructible_v<T> << "\n";
}

int main()
{
    report<A>("A (all defaulted)");
    report<B>("B (destructor only)");
    report<C>("C (dtor + copy, no move)");
    return 0;
}
```

UB 部分（另存 `hw211b_ub.cpp`）：

```cpp
// hw211b_ub.cpp -- Homework 2.1-B UB part: std::move degrades to copy -> double free
#include <iostream>
#include <utility>

struct B
{
    char* data_;
    explicit B(std::size_t n) : data_(new char[n]) {}
    ~B() { delete[] data_; }
};

int main()
{
    std::cout << std::unitbuf;
    B a(64);
    B b = std::move(a);  // no move ctor: falls back to implicit copy ctor (shallow)
    std::cout << "a.data_=" << (void*)a.data_ << " b.data_=" << (void*)b.data_ << "\n";
    return 0;
}
```

**验证输出**：

```text
$ g++ -std=c++17 -Wall -Wextra -O2 -o hw211b hw211b.cpp && ./hw211b
A (all defaulted):
  is_move_constructible            = 1
  is_nothrow_move_constructible   = 1
  is_trivially_move_constructible = 0
  is_copy_constructible            = 1
B (destructor only):
  is_move_constructible            = 1
  is_nothrow_move_constructible   = 1
  is_trivially_move_constructible = 0
  is_copy_constructible            = 1
C (dtor + copy, no move):
  is_move_constructible            = 1
  is_nothrow_move_constructible   = 0
  is_trivially_move_constructible = 0
  is_copy_constructible            = 1

$ g++ -std=c++17 -Wall -Wextra -g -fsanitize=address -o hw211b_ub hw211b_ub.cpp && ./hw211b_ub
a.data_=0x716864de0080 b.data_=0x716864de0080
=================================================================
==378==ERROR: AddressSanitizer: attempting double-free on 0x716864de0080 in thread T0:
    #0 0x75086652dff9 in operator delete[](void*) ...
    #1 0x59c38f0f46cf in B::~B() /tmp/cpp-v2-hw1/hw211b_ub.cpp:9
    #2 0x59c38f0f4410 in main /tmp/cpp-v2-hw1/hw211b_ub.cpp:19
    ...（中间栈帧省略）
0x716864de0080 is located 0 bytes inside of 64-byte region [0x716864de0080,0x716864de00c0)
freed by thread T0 here:
    #1 0x59c38f0f46cf in B::~B() /tmp/cpp-v2-hw1/hw211b_ub.cpp:9
    ...
SUMMARY: AddressSanitizer: double-free /tmp/cpp-v2-hw1/hw211b_ub.cpp:9 in B::~B()
==378==ABORTING
```

要点：B 的 `is_nothrow_move_constructible == 1` 不是因为有移动构造——是顶上来的隐式拷贝构造恰好 noexcept。看到 `std::move` 没触发移动时，先查自己的类是不是只写了析构函数。「有资源就别只写析构」，要么把五个特殊成员补齐，要么换成 `std::string`/`std::unique_ptr` 这类自己会动的成员。

## 2.2-A {#hw-2-2-a}

**难度 L2** · 题面见 [homework](./01-homework.md#hw-2-2-a)

**思路**：删除器类型是 `unique_ptr` 模板参数的一部分——函数指针要占 8 字节存储，无捕获 lambda 和空函数对象靠 EBO 白嫖成 0。

1. 三种删除器形态 + 四个 `sizeof`。→ 知识点：[自定义删除器与侵入式引用计数](../ch01-smart-pointers/05-custom-deleter.md)「删除器的三种形态」一节、[unique_ptr 详解：独占所有权的零开销智能指针](../ch01-smart-pointers/02-unique-ptr.md)「零开销证明」一节
2. 函数对象版本写文件 + 静态计数器验证删除器恰好调用一次（析构 `if (file_)` 守卫与删除器互相配合）。→ 知识点：[RAII 深入理解：资源管理的基石](../ch01-smart-pointers/01-raii-deep-dive.md)（获取进构造、释放进析构）

```cpp
// hw220a.cpp -- Homework 2.2-A: deleter forms and sizeof
// Standard: C++17
#include <cstdio>
#include <iostream>
#include <memory>

static int close_count = 0;

void close_file_fn(FILE* f)
{
    if (f) {
        ++close_count;
        std::fclose(f);
    }
}

struct FcloseFunctor
{
    void operator()(FILE* f) const
    {
        if (f) {
            ++close_count;
            std::fclose(f);
        }
    }
};

int main()
{
    auto lam = [](FILE* f) { if (f) { ++close_count; std::fclose(f); } };
    (void)lam;

    std::cout << "sizeof(FILE*)                           = "
              << sizeof(FILE*) << "\n";
    std::cout << "sizeof(unique_ptr<FILE>)                = "
              << sizeof(std::unique_ptr<FILE>) << "\n";
    std::cout << "sizeof(unique_ptr<FILE,void(*)(FILE*)>) = "
              << sizeof(std::unique_ptr<FILE, void (*)(FILE*)>) << "\n";
    std::cout << "sizeof(unique_ptr<FILE,FcloseFunctor>)  = "
              << sizeof(std::unique_ptr<FILE, FcloseFunctor>) << "\n";
    std::cout << "sizeof(unique_ptr<FILE,decltype(lam)>)  = "
              << sizeof(std::unique_ptr<FILE, decltype(lam)>) << "\n";

    {
        std::unique_ptr<FILE, FcloseFunctor> f1(std::fopen("/tmp/hw220a.txt", "w"));
        std::fprintf(f1.get(), "hello from functor deleter\n");
    }
    std::cout << "close_count after scope exit = " << close_count << "\n";
    return 0;
}
```

**验证输出**：

```text
$ g++ -std=c++17 -Wall -Wextra -O2 -o hw220a hw220a.cpp && ./hw220a
sizeof(FILE*)                           = 8
sizeof(unique_ptr<FILE>)                = 8
sizeof(unique_ptr<FILE,void(*)(FILE*)>) = 16
sizeof(unique_ptr<FILE,FcloseFunctor>)  = 8
sizeof(unique_ptr<FILE,decltype(lam)>)  = 8
close_count after scope exit = 1
```

要点：默认删除器、空函数对象、无捕获 lambda 都是 8 字节（EBO：`unique_ptr` 把删除器当基类继承，空类大小被优化为 0）；函数指针版 16 字节（多存一个地址）。工程上优先函数对象——零开销还容易复用。

## 2.2-B {#hw-2-2-b}

**难度 L3** · 题面见 [homework](./01-homework.md#hw-2-2-b)

**思路**：`next` 和 `prev` 都用 `shared_ptr` 时形成环，离开作用域后两个节点的强计数各剩 1（互持），谁都不析构；`prev` 换成 `weak_ptr` 后环断在弱边。

1. 泄漏版：两个节点互指，离开作用域——析构日志不会出现，LSan 报 160 字节泄漏（两个控制块+对象）。→ 知识点：[weak_ptr 与循环引用：打破所有权的死锁](../ch01-smart-pointers/04-weak-ptr.md)「循环引用问题演示」一节
2. 修复版：`prev` 改 `weak_ptr`，析构按 a→b 依次发生，ASan 全绿。→ 知识点：[weak_ptr 与循环引用：打破所有权的死锁](../ch01-smart-pointers/04-weak-ptr.md)「weak_ptr 打破循环的原理」一节
3. 日志消失之谜：LSan 在进程退出时用 `_exit` 收尾，**缓冲的 stdout 来不及冲刷**——程序里加 `std::cout << std::unitbuf;` 实时冲刷，或者把诊断打到 stderr。→ 知识点：[shared_ptr 详解：共享所有权与引用计数](../ch01-smart-pointers/03-shared-ptr.md)（循环引用是静默泄漏）

```cpp
// hw221b_leak.cpp -- Homework 2.2-B: shared_ptr cycle -> leak (ASan/LSan)
#include <iostream>
#include <memory>
#include <string>
#include <utility>

struct Node
{
    std::string name;
    std::shared_ptr<Node> next;
    std::shared_ptr<Node> prev;
    explicit Node(std::string n) : name(std::move(n))
    {
        std::cout << "Node(" << name << ") constructed\n";
    }
    ~Node()
    {
        std::cout << "~Node(" << name << ") destroyed\n";
    }
};

int main()
{
    std::cout << std::unitbuf;  // realtime flush so LSan cannot swallow buffered lines
    {
        auto a = std::make_shared<Node>("A");
        auto b = std::make_shared<Node>("B");
        a->next = b;
        b->prev = a;
        std::cout << "leaving inner scope...\n";
    }
    std::cout << "inner scope ended\n";
    return 0;
}
```

修复版（`hw221b_fix.cpp`）只改一处：`std::weak_ptr<Node> prev;`，其余相同。

**验证输出**：

```text
$ g++ -std=c++17 -Wall -Wextra -g -fsanitize=address -o hw221b_leak hw221b_leak.cpp
$ ./hw221b_leak
Node(A) constructed
Node(B) constructed
leaving inner scope...
inner scope ended

=================================================================
==263==ERROR: LeakSanitizer: detected memory leaks

Indirect leak of 80 byte(s) in 1 object(s) allocated from:
    #0 0x73bf6e32d2a1 in operator new(unsigned long) ...
    #1 ... in std::__new_allocator<...>::allocate(...) ...
    ...（模板实例化栈帧省略，两处 make_shared 各报一次）
SUMMARY: AddressSanitizer: 160 byte(s) leaked in 2 allocation(s).
```

```text
$ g++ -std=c++17 -Wall -Wextra -g -fsanitize=address -o hw221b_fix hw221b_fix.cpp
$ ./hw221b_fix
Node(A) constructed
Node(B) constructed
leaving inner scope...
~Node(A) destroyed
~Node(B) destroyed
inner scope ended
```

要点：泄漏版里 `~Node` 一次都没出现——环里的两个节点永远「等对方先走」。修复版 `weak_ptr` 不参与强计数，`a` 一离开作用域强计数就归零，析构链自然触发。看到自己写双向/父子引用时，先问一句：哪条边是「拥有」、哪条边是「观察」。

## 2.3-A {#hw-2-3-a}

**难度 L1** · 题面见 [homework](./01-homework.md#hw-2-3-a)

**思路**：`constexpr` 函数是「双面间谍」——参数全是编译期常量时在编译期算，否则退化成普通函数；`const` 只保证「不可修改」，值可能来自运行时。

1. C++14 风格阶乘（循环）+ 斐波那契（递归三元）+ 四组 `static_assert`；`factorial(4) + 1` 做数组大小。→ 知识点：[constexpr 基础：编译期求值的艺术](../ch02-constexpr/01-constexpr-basics.md)「第二步——constexpr 函数」「static_assert 与 constexpr 的黄金搭档」两节
2. `const int kRuntime = runtime_seed();` 合法但只算「运行时常量」；`constexpr int kBad = runtime_seed();` 编译不过（注释留在程序里）。→ 知识点：[constexpr 基础：编译期求值的艺术](../ch02-constexpr/01-constexpr-basics.md)「编译期常量 vs const」一节

```cpp
// hw230a.cpp -- Homework 2.3-A: constexpr basics
// Standard: C++17
#include <iostream>

constexpr int factorial(int n)
{
    int result = 1;
    for (int i = 2; i <= n; ++i) {
        result *= i;
    }
    return result;
}

constexpr int fibonacci(int n)
{
    return n <= 1 ? n : fibonacci(n - 1) + fibonacci(n - 2);
}

static_assert(factorial(0) == 1, "factorial(0)");
static_assert(factorial(5) == 120, "factorial(5)");
static_assert(fibonacci(0) == 0, "fibonacci(0)");
static_assert(fibonacci(10) == 55, "fibonacci(10)");

// compile-time context: array size
int table[factorial(4) + 1];  // 25 ints

// const does NOT guarantee compile-time
int runtime_seed()
{
    return 7;
}
const int kRuntime = runtime_seed();
// constexpr int kBad = runtime_seed();  // compile error: not constant expression

int main()
{
    std::cout << "factorial(6)  = " << factorial(6) << "\n";
    std::cout << "fibonacci(12) = " << fibonacci(12) << "\n";
    std::cout << "table elements = " << sizeof(table) / sizeof(table[0]) << "\n";
    std::cout << "kRuntime = " << kRuntime << " (runtime const, unusable as array size)\n";
    return 0;
}
```

**验证输出**：

```text
$ g++ -std=c++17 -Wall -Wextra -O2 -o hw230a hw230a.cpp && ./hw230a
factorial(6)  = 720
fibonacci(12) = 144
table elements = 25
kRuntime = 7 (runtime const, unusable as array size)
```

要点：同一个 `factorial`，`factorial(4)` 在数组大小的上下文里编译期算完、`factorial(6)` 传给运行时变量就当普通函数跑。`const` 管的是「不许改」，`constexpr` 管的是「编译期就得算出来」。

## 2.3-B {#hw-2-3-b}

**难度 L3** · 题面见 [homework](./01-homework.md#hw-2-3-b)

**思路**：把教材的 CRC-32 换成 CRC-16/CCITT-FALSE：多项式 `0x1021`、初值 `0xFFFF`、MSB 优先；查表生成、查表更新两步都写进 `constexpr` 函数，标准校验值 `"123456789" -> 0x29B1` 用 `static_assert` 钉死。

1. `make_crc16_table()`：对每个字节 i，从 `i << 8` 出发做 8 轮「最高位为 1 就左移异或多项式、否则左移」。→ 知识点：[编译期计算实战：从查表到编译期字符串](../ch02-constexpr/04-compile-time-practice.md)「第一步——编译期查表」一节
2. `crc16_ccitt`：`crc = (crc << 8) ^ table[((crc >> 8) ^ data[i]) & 0xFF]`；`static_assert` 校验值 + 运行时打印表项和 `"hello"` 的结果。→ 知识点：[编译期计算实战：从查表到编译期字符串](../ch02-constexpr/04-compile-time-practice.md)（查表 + 静态断言的标准套路）

```cpp
// hw231b.cpp -- Homework 2.3-B: compile-time CRC-16/CCITT-FALSE table
// Standard: C++17
#include <array>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string>

constexpr std::array<std::uint16_t, 256> make_crc16_table()
{
    std::array<std::uint16_t, 256> table{};
    constexpr std::uint16_t kPoly = 0x1021u;
    for (std::uint32_t i = 0; i < 256; ++i) {
        std::uint16_t crc = static_cast<std::uint16_t>(i << 8);
        for (int j = 0; j < 8; ++j) {
            crc = (crc & 0x8000u) ? static_cast<std::uint16_t>((crc << 1) ^ kPoly)
                                  : static_cast<std::uint16_t>(crc << 1);
        }
        table[i] = crc;
    }
    return table;
}

constexpr auto kCrc16Table = make_crc16_table();

constexpr std::uint16_t crc16_ccitt(const std::uint8_t* data, std::size_t len)
{
    std::uint16_t crc = 0xFFFFu;
    for (std::size_t i = 0; i < len; ++i) {
        crc = static_cast<std::uint16_t>(
            (crc << 8) ^ kCrc16Table[((crc >> 8) ^ data[i]) & 0xFFu]);
    }
    return crc;
}

// check value: CRC-16/CCITT-FALSE of "123456789" is 0x29B1
constexpr std::uint8_t kCheck[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
static_assert(crc16_ccitt(kCheck, 9) == 0x29B1u, "CRC-16/CCITT-FALSE check value mismatch");

int main()
{
    std::cout << std::hex << std::setfill('0');
    std::cout << "table[0]    = 0x" << std::setw(4) << kCrc16Table[0] << "\n";
    std::cout << "table[1]    = 0x" << std::setw(4) << kCrc16Table[1] << "\n";
    std::cout << "table[0x31] = 0x" << std::setw(4) << kCrc16Table[0x31] << "\n";

    std::string msg = "hello";
    auto crc = crc16_ccitt(reinterpret_cast<const std::uint8_t*>(msg.data()), msg.size());
    std::cout << std::dec;
    std::cout << "crc16(\"hello\") = 0x" << std::hex << std::setw(4) << crc << "\n";
    std::cout << "static_assert check value passed\n";
    return 0;
}
```

**验证输出**：

```text
$ g++ -std=c++17 -Wall -Wextra -O2 -o hw231b hw231b.cpp && ./hw231b
table[0]    = 0x0000
table[1]    = 0x1021
table[0x31] = 0x2672
crc16("hello") = 0xd26e
static_assert check value passed
```

要点：`kCrc16Table` 整张表在编译期生成、直接进 `.rodata`，运行时零初始化开销；校验值 `static_assert` 把「生成逻辑对不对」从运行时搬到编译时。换多项式、换初值，改两个常量就能套用到别的 CRC 变体。

## 2.4-A {#hw-2-4-a}

**难度 L2** · 题面见 [homework](./01-homework.md#hw-2-4-a)

**思路**：四个实验分别考值捕获快照、`mutable` 副本、引用捕获、初始化捕获移动。预测顺序：①`is_high(150)` 为 1（lambda 里还是 100，150 > 100）；②第三次调用返回 3、外部 `counter` 是 0；③`sum` 是 60；④`*p` 是 42、外部指针为空。

1. 值捕获在 lambda **创建那一刻**复制变量；之后的修改只影响外部。→ 知识点：[Lambda 捕获机制深入](../ch03-lambda/02-lambda-capture.md)「值捕获——复制一份到闭包对象中」一节
2. `mutable` 让 `operator()` 非 const，计数器改的是闭包自己的副本。→ 知识点：[Lambda 捕获机制深入](../ch03-lambda/02-lambda-capture.md)（`mutable` 的语义）
3. 引用捕获存的是原始变量的地址，写就是写原变量。→ 知识点：[Lambda 捕获机制深入](../ch03-lambda/02-lambda-capture.md)「引用捕获——存储原始变量的地址」一节
4. 初始化捕获 `[p = std::move(ptr)]` 把 `unique_ptr` 移进闭包——C++11 做不到的事。→ 知识点：[Lambda 捕获机制深入](../ch03-lambda/02-lambda-capture.md)「C++14 初始化捕获」一节

```cpp
// hw240a.cpp -- Homework 2.4-A: capture semantics, four experiments
// Standard: C++17
#include <iostream>
#include <memory>

int main()
{
    // Exp 1: value capture is a snapshot
    int threshold = 100;
    auto is_high = [threshold](int v) { return v > threshold; };
    threshold = 200;
    std::cout << "1) value snapshot: is_high(150) = " << is_high(150)
              << " (outer threshold already 200)\n";

    // Exp 2: mutable counter modifies closure's own copy
    int counter = 0;
    auto make_counter = [counter]() mutable { return ++counter; };
    make_counter();
    make_counter();
    std::cout << "2) mutable counter: 3rd call = " << make_counter()
              << ", outer counter = " << counter << "\n";

    // Exp 3: reference capture mutates the original
    int sum = 0;
    auto accumulate = [&sum](int v) { sum += v; };
    accumulate(10);
    accumulate(20);
    accumulate(30);
    std::cout << "3) reference accumulate: sum = " << sum << "\n";

    // Exp 4: init capture moves a unique_ptr into the closure
    auto ptr = std::make_unique<int>(42);
    auto holder = [p = std::move(ptr)]() { return *p; };
    std::cout << "4) init capture: *p = " << holder()
              << ", original ptr empty = " << (ptr == nullptr) << "\n";
    return 0;
}
```

**验证输出**：

```text
$ g++ -std=c++17 -Wall -Wextra -O2 -o hw240a hw240a.cpp && ./hw240a
1) value snapshot: is_high(150) = 1 (outer threshold already 200)
2) mutable counter: 3rd call = 3, outer counter = 0
3) reference accumulate: sum = 60
4) init capture: *p = 42, original ptr empty = 1
```

要点：值捕获「复制那一刻的值」，引用捕获「永远跟着原变量」，初始化捕获「闭包自己拥有一份新状态」。只读不写默认值捕获，改外部才用引用捕获，想独占就用初始化捕获。

## 2.4-B {#hw-2-4-b}

**难度 L3** · 题面见 [homework](./01-homework.md#hw-2-4-b)

**思路**：lambda 匿名，函数体里没有名字可以自调用。Y 组合子把「自己」当第一个参数传进去：外层类 `operator()` 转发 `f_(*this, args...)`，lambda 的第一个参数 `auto&& self` 收到的就是整个组合子。

1. `YCombinator` 的 `operator()` 是模板——编译器看得见完整的 lambda 类型，整条递归链可内联；`std::function` 每次递归都走一层类型擦除的间接调用，教材实测慢 75~145 倍。→ 知识点：[泛型 Lambda 与模板 Lambda](../ch03-lambda/03-generic-lambda.md)「递归 Lambda」一节
2. 阶乘 + 求最大两个递归各写一个泛型 lambda；CTAD（`YCombinator(F) -> YCombinator<F>`）省掉工厂函数。→ 知识点：[泛型 Lambda 与模板 Lambda](../ch03-lambda/03-generic-lambda.md)、[类模板参数推导 (CTAD)](../ch06-auto-decltype/03-ctad.md)

```cpp
// hw241b.cpp -- Homework 2.4-B: recursion with generic lambda (Y combinator)
// Standard: C++17
#include <cstddef>
#include <iostream>
#include <utility>
#include <vector>

template <typename F>
class YCombinator
{
    F f_;

public:
    explicit YCombinator(F f) : f_(std::move(f)) {}

    template <typename... Args>
    decltype(auto) operator()(Args&&... args)
    {
        return f_(*this, std::forward<Args>(args)...);
    }
};

template <typename F>
YCombinator(F) -> YCombinator<F>;

int main()
{
    auto factorial = YCombinator([](auto&& self, int n) -> long long {
        if (n <= 1) {
            return 1;
        }
        return n * self(n - 1);
    });
    std::cout << "factorial(5)  = " << factorial(5) << "\n";
    std::cout << "factorial(12) = " << factorial(12) << "\n";

    std::vector<int> data = {3, 8, 1, 9, 4, 7};
    auto max_of = YCombinator([](auto&& self, const std::vector<int>& v, std::size_t i) -> int {
        if (i == 0) {
            return v[0];
        }
        int rest = self(v, i - 1);
        return v[i] > rest ? v[i] : rest;
    });
    std::cout << "max of {3,8,1,9,4,7} = " << max_of(data, data.size() - 1) << "\n";
    return 0;
}
```

**验证输出**：

```text
$ g++ -std=c++17 -Wall -Wextra -O2 -o hw241b hw241b.cpp && ./hw241b
factorial(5)  = 120
factorial(12) = 479001600
max of {3,8,1,9,4,7} = 9
```

要点：`self` 不是 lambda 自身，而是「能调自己的包装器」；`operator()` 是模板，`decltype(auto)` 把返回类型推导交给被包函数。递归深度小的场景两种写法都行，热路径上别用 `std::function` 版本。

## 2.5-A {#hw-2-5-a}

**难度 L1** · 题面见 [homework](./01-homework.md#hw-2-5-a)

**思路**：三大改进各验证一条：底层类型钉死 `sizeof == 1`；`Green` 的整数值是 1（隐式转换没了，要值就显式 `static_cast`）；switch 不写 default，漏分支由 `-Wswitch` 报警。

1. `enum class Color : std::uint8_t` + `static_assert`。→ 知识点：[enum class 与强类型枚举](../ch04-type-safety/01-enum-class.md)「指定底层类型与前向声明」一节
2. 隐式转换两行错误代码以注释留底（`int bad = Color::Red;`、跨枚举比较）。→ 知识点：[enum class 与强类型枚举](../ch04-type-safety/01-enum-class.md)「禁止隐式转换」一节
3. 完整版 switch 零警告；故意漏 `kConnected` 的版本编译出 `-Wswitch` 警告。→ 知识点：[enum class 与强类型枚举](../ch04-type-safety/01-enum-class.md)「switch 匹配与编译器警告」一节

```cpp
// hw250a.cpp -- Homework 2.5-A: enum class improvements + exhaustive switch
// Standard: C++17
#include <cstdint>
#include <iostream>

enum class Color : std::uint8_t { Red, Green, Blue };
enum class Fruit : std::uint8_t { Apple, Orange, Banana };

static_assert(sizeof(Color) == 1, "Color should fit in one byte");

enum class NetworkState : std::uint8_t
{
    kDisconnected,
    kConnecting,
    kConnected,
    kError,
};

// no default: -Wswitch will warn if a new enumerator is missed
const char* to_string(NetworkState s)
{
    switch (s) {
    case NetworkState::kDisconnected: return "disconnected";
    case NetworkState::kConnecting:   return "connecting";
    case NetworkState::kConnected:    return "connected";
    case NetworkState::kError:        return "error";
    }
    return "unknown";
}

int main()
{
    Color c = Color::Red;
    std::cout << "sizeof(Color) = " << sizeof(c) << "\n";
    std::cout << "static_cast<int>(Color::Green) = "
              << static_cast<int>(Color::Green) << "\n";
    std::cout << "to_string(kConnecting) = "
              << to_string(NetworkState::kConnecting) << "\n";

    // Compile-time rejections (uncomment to watch them fail):
    // int bad = Color::Red;                    // error: cannot convert
    // if (Color::Red == Fruit::Apple) {}       // error: no operator==
    return 0;
}
```

漏分支版（`hw250a_missing.cpp`）删掉 `kConnected` 那一行 case，其余相同。

**验证输出**：

```text
$ g++ -std=c++17 -Wall -Wextra -O2 -o hw250a hw250a.cpp && ./hw250a
sizeof(Color) = 1
static_cast<int>(Color::Green) = 1
to_string(kConnecting) = connecting

$ g++ -std=c++17 -Wall -Wextra -o hw250a_missing hw250a_missing.cpp
hw250a_missing.cpp: In function 'const char* to_string(NetworkState)':
hw250a_missing.cpp:14:12: warning: enumeration value 'kConnected' not handled in switch [-Wswitch]
   14 |     switch (s) {
      |            ^
```

要点：写了 `default`，`-Wswitch` 就闭嘴了——以后新增枚举值时所有漏改的 switch 都不再报警。不写 default，编译器替你做「新增枚举值 → 找到所有漏改的 switch」的穷尽性审计。

## 2.5-B {#hw-2-5-b}

**难度 L3** · 题面见 [homework](./01-homework.md#hw-2-5-b)

**思路**：递归结构不能直接放进 `variant`——`BinaryExpr` 里有两个 `Expr`，`Expr` 里又有 `BinaryExpr`，sizeof 无限套娃，必须用 `unique_ptr` 打破循环依赖。求值就是 `visit` 递归。

1. `Expr = variant<NumberLiteral, unique_ptr<BinaryExpr>>`，`eval` 用 `Overloaded` 访问者：叶子返回数值，节点先递归求左右子树再按 `op` 运算。→ 知识点：[std::variant：类型安全的联合体](../ch04-type-safety/03-variant.md)「AST 节点」一节（递归 variant 必须走指针）、「std::visit 与访问者模式」一节
2. 构造 $\frac{(2 + 3) \times (10 - 4)}{3}$：先手算——$\frac{5 \times 6}{3} = 10$。→ 知识点：[std::variant：类型安全的联合体](../ch04-type-safety/03-variant.md)（variant 的隐式构造收窄了构造代码）

```cpp
// hw251b.cpp -- Homework 2.5-B: recursive variant AST + visitor evaluator
// Standard: C++17
#include <iostream>
#include <memory>
#include <utility>
#include <variant>

struct NumberLiteral
{
    double value;
};

struct BinaryExpr;

using Expr = std::variant<NumberLiteral, std::unique_ptr<BinaryExpr>>;

struct BinaryExpr
{
    Expr left;
    char op;
    Expr right;
};

template <class... Ts>
struct Overloaded : Ts...
{
    using Ts::operator()...;
};

template <class... Ts>
Overloaded(Ts...) -> Overloaded<Ts...>;

double eval(const Expr& e)
{
    return std::visit(Overloaded{
        [](const NumberLiteral& n) -> double { return n.value; },
        [](const std::unique_ptr<BinaryExpr>& b) -> double {
            double l = eval(b->left);
            double r = eval(b->right);
            switch (b->op) {
            case '+': return l + r;
            case '-': return l - r;
            case '*': return l * r;
            case '/': return l / r;
            }
            return 0.0;
        }
    }, e);
}

int main()
{
    auto mk = [](double v) { return Expr(NumberLiteral{v}); };
    auto bin = [](Expr l, char op, Expr r) {
        return Expr(std::make_unique<BinaryExpr>(BinaryExpr{std::move(l), op, std::move(r)}));
    };

    // (2 + 3) * (10 - 4) / 3 = 10
    Expr e = bin(
        bin(mk(2.0), '+', mk(3.0)), '*',
        bin(bin(mk(10.0), '-', mk(4.0)), '/', mk(3.0)));
    std::cout << "(2+3)*(10-4)/3 = " << eval(e) << "\n";
    return 0;
}
```

**验证输出**：

```text
$ g++ -std=c++17 -Wall -Wextra -O2 -o hw251b hw251b.cpp && ./hw251b
(2+3)*(10-4)/3 = 10
```

要点：`Overloaded` 保证 visit 的分支覆盖全部备选类型——以后给 `Expr` 加新节点类型，`eval` 没同步扩展就连编译都过不了。值语义 + 递归指针，节点生命周期由 `variant` 和 `unique_ptr` 联手管理，一个 `delete` 都不用写。

## 2.6-A {#hw-2-6-a}

**难度 L1** · 题面见 [homework](./01-homework.md#hw-2-6-a)

**思路**：四种解包对象各走一遍；`uint8_t` 的 `operator<<` 会把它当字符打印（输出 `A` 而不是 65），前面加 `+` 触发整型提升；`auto` 绑定的是**匿名拷贝**的成员引用，改绑定变量不影响原对象，`auto&` 才直通原对象。

1. map 迭代 + `+id`、tuple 返回、原生数组、结构体四连解包。→ 知识点：[结构化绑定：一行解包多个值](../ch05-structured-bindings/01-structured-bindings.md)「从 pair 和 tuple 讲起」「原生数组与结构体」两节
2. auto vs auto& 语义对比。→ 知识点：[结构化绑定：一行解包多个值](../ch05-structured-bindings/01-structured-bindings.md)「三种绑定语义」一节

```cpp
// hw260a.cpp -- Homework 2.6-A: structured bindings, four targets
// Standard: C++17
#include <cstdint>
#include <iostream>
#include <map>
#include <string>
#include <tuple>

std::tuple<int, std::string, double> query_database(int id)
{
    return {id, "sensor_" + std::to_string(id), 23.5};
}

struct SensorReading
{
    std::uint8_t sensor_id;
    float value;
    std::uint32_t timestamp;
};

int main()
{
    // 1. map iteration: the key is uint8_t -- operator<< prints it as a char,
    //    unary + promotes it to int so it prints as a number
    std::map<std::uint8_t, std::string> sensor_names = {
        {65, "Temperature"}, {66, "Humidity"}};
    for (const auto& [id, name] : sensor_names) {
        std::cout << "id as char: '" << id << "', id with +: " << +id
                  << ", name: " << name << "\n";
    }

    // 2. tuple return
    auto [rid, rname, rvalue] = query_database(42);
    std::cout << "tuple: " << rid << " / " << rname << " / " << rvalue << "\n";

    // 3. raw array
    int rgb[3] = {255, 128, 0};
    auto [r, g, b] = rgb;
    std::cout << "rgb: " << r << " " << g << " " << b << "\n";

    // 4. struct + auto vs auto& semantics
    SensorReading reading{5, 23.5f, 1234567890};
    auto [id, val, ts] = reading;    // copy: modifying val does NOT touch reading
    val = 99.0f;
    std::cout << "copied val (binding) = " << val
              << ", original reading.value = " << reading.value << "\n";
    auto& [rid2, rval2, rts2] = reading;  // reference: mutates the original
    rval2 = 99.0f;
    std::cout << "after auto& write: reading.value = " << reading.value << "\n";
    (void)ts;
    (void)rts2;
    return 0;
}
```

**验证输出**：

```text
$ g++ -std=c++17 -Wall -Wextra -O2 -o hw260a hw260a.cpp && ./hw260a
id as char: 'A', id with +: 65, name: Temperature
id as char: 'B', id with +: 66, name: Humidity
tuple: 42 / sensor_42 / 23.5
rgb: 255 128 0
copied val (binding) = 99, original reading.value = 23.5
after auto& write: reading.value = 99
```

要点：`auto` 解包拿到的是拷贝（改绑定不改原对象），`auto&` 解包拿到的是原对象的引用（改绑定即改原对象）；大结构体用 `const auto&` 免拷贝，小类型直接 `auto`。`+id` 那个坑：`uint8_t` 被流操作符当成字符，加一元 `+` 提升成 `int` 才按数字打印。

## 2.6-B {#hw-2-6-b}

**难度 L2** · 题面见 [homework](./01-homework.md#hw-2-6-b)

**思路**：三件套各验证一条规则：init 变量在 if **和 else** 两个分支都可见；if 初始化器里的 RAII 对象析构发生在整个 if/else 之后（else 里锁还握着）；switch 初始化器 + 编译期哈希 case 标签完成字符串分派。

1. map insert + 结构化绑定进 if 初始化器。→ 知识点：[if/switch 初始化器：缩小变量作用域](../ch05-structured-bindings/02-init-statements.md)「结合结构化绑定」一节（init 变量在两个分支都可用）
2. `LockTracker` 追踪锁的生命周期边界。→ 知识点：[if/switch 初始化器：缩小变量作用域](../ch05-structured-bindings/02-init-statements.md)「锁守卫模式」一节
3. switch 初始化器 + `hash_string` 分派（`constexpr` 哈希，case 标签是编译期常量）。→ 知识点：[if/switch 初始化器：缩小变量作用域](../ch05-structured-bindings/02-init-statements.md)「switch 初始化器」一节、[编译期计算实战：从查表到编译期字符串](../ch02-constexpr/04-compile-time-practice.md)（编译期哈希）

```cpp
// hw261b.cpp -- Homework 2.6-B: if/switch init statements
// Standard: C++17
#include <cstddef>
#include <iostream>
#include <map>
#include <string>
#include <string_view>

constexpr std::size_t hash_string(std::string_view s)
{
    std::size_t h = 0;
    for (char c : s) {
        h = h * 31 + static_cast<std::size_t>(c);
    }
    return h;
}

struct LockTracker
{
    LockTracker() { std::cout << "  >> lock acquired\n"; }
    ~LockTracker() { std::cout << "  << lock released\n"; }
};

int dispatch(std::string_view input)
{
    switch (auto hash = hash_string(input); hash) {
    case hash_string("start"):  return 1;
    case hash_string("stop"):   return 2;
    case hash_string("status"): return 3;
    default:                    return 0;
    }
}

int main()
{
    // 1. map insert + structured binding; init variable visible in BOTH branches
    std::map<int, std::string> m{{1, "one"}};
    if (auto [it, ok] = m.insert({1, "ONE"}); ok) {
        std::cout << "if  branch: Inserted " << it->second << "\n";
    } else {
        std::cout << "else branch: Existing " << it->second << " (ONE not overwritten)\n";
    }

    // 2. lock guard: else branch still holds the lock
    std::cout << "entering if/else block\n";
    if (LockTracker lock; false) {
        // not executed
    } else {
        std::cout << "else branch running (lock still held)\n";
    }
    std::cout << "left if/else block\n";

    // 3. switch init + compile-time hash dispatch
    std::cout << "dispatch(start)  = " << dispatch("start") << "\n";
    std::cout << "dispatch(status) = " << dispatch("status") << "\n";
    std::cout << "dispatch(reboot) = " << dispatch("reboot") << "\n";
    return 0;
}
```

**验证输出**：

```text
$ g++ -std=c++17 -Wall -Wextra -O2 -o hw261b hw261b.cpp && ./hw261b
else branch: Existing one (ONE not overwritten)
entering if/else block
  >> lock acquired
else branch running (lock still held)
  << lock released
left if/else block
dispatch(start)  = 1
dispatch(status) = 3
dispatch(reboot) = 0
```

要点：插入已存在的 key 时 `insert` 返回 `{迭代器, false}`，`it` 在 else 分支照样能用；锁的析构排在「else 分支执行中」之后——想只锁 if 一半就换更细的写法。哈希分派记得教材那句话：哈希把无限输入压进有限范围，理论必然碰撞，要精确匹配得命中后再比一次原串。

## 2.7-A {#hw-2-7-a}

**难度 L2** · 题面见 [homework](./01-homework.md#hw-2-7-a)

**思路**：auto 的推导规则一句话——「模板推导同款，丢引用、丢顶层 const、保底层 const」。每组 `static_assert` 对应一条规则，代理类型（`vector<bool>`）和花括号初始化是两个独立的大坑。

1. 顶层 const 丢弃、引用丢弃、底层 const 保留、`auto&`/`auto&&`、`initializer_list`、代理类型、返回引用用 auto 接收成拷贝——七组 `static_assert`。→ 知识点：[auto 推导深入：不只是偷懒](../ch06-auto-decltype/01-auto-deep-dive.md)「auto 的推导规则」「auto 与初始化列表」「auto 与代理类型」「auto 不推导为引用」各节

```cpp
// hw270a.cpp -- Homework 2.7-A: auto deduction checkup
// Standard: C++17
#include <iostream>
#include <type_traits>
#include <vector>

int& get_ref()
{
    static int x = 42;
    return x;
}

int main()
{
    const int ci = 42;
    auto a = ci;              // int: top-level const dropped
    static_assert(std::is_same_v<decltype(a), int>);

    int val = 10;
    int& ref = val;
    auto b = ref;             // int: reference dropped
    static_assert(std::is_same_v<decltype(b), int>);

    const int* p = nullptr;   // low-level const
    auto q = p;               // const int*
    static_assert(std::is_same_v<decltype(q), const int*>);

    int* const p2 = nullptr;  // top-level const
    auto q2 = p2;             // int*
    static_assert(std::is_same_v<decltype(q2), int*>);

    auto& ar = ci;            // const int&
    static_assert(std::is_same_v<decltype(ar), const int&>);

    auto&& fwd1 = val;        // int&
    static_assert(std::is_same_v<decltype(fwd1), int&>);
    auto&& fwd2 = 42;         // int&&
    static_assert(std::is_same_v<decltype(fwd2), int&&>);

    auto init = {1, 2, 3};    // initializer_list<int>, NOT vector
    static_assert(std::is_same_v<decltype(init), std::initializer_list<int>>);
    std::cout << "auto x = {1,2,3}: initializer_list size = " << init.size() << "\n";

    auto gr = get_ref();      // int: reference dropped
    static_assert(std::is_same_v<decltype(gr), int>);
    std::cout << "auto from get_ref(): " << gr << " (copy)\n";

    std::vector<bool> bits = {true, false, true};
    auto bit0 = bits[0];      // proxy type, not bool&
    static_assert(!std::is_same_v<decltype(bit0), bool&>);
    std::cout << "vector<bool>[0] via auto: " << bit0 << "\n";
    std::cout << "all static_asserts passed\n";
    return 0;
}
```

**验证输出**：

```text
$ g++ -std=c++17 -Wall -Wextra -O2 -o hw270a hw270a.cpp && ./hw270a
auto x = {1,2,3}: initializer_list size = 3
auto from get_ref(): 42 (copy)
vector<bool>[0] via auto: 1
all static_asserts passed
```

要点：`auto x = {1,2,3}` 是 `initializer_list<int>`，想拿 `vector` 得显式写 `std::vector<int> v = {1,2,3};`；`vector<bool>` 的 `operator[]` 返回位代理，`auto&` 绑不上、`auto` 拿到的是代理拷贝——遇到「`auto&` 编译错但 `auto` 能过」，先怀疑代理类型。

## 2.7-B {#hw-2-7-b}

**难度 L3** · 题面见 [homework](./01-homework.md#hw-2-7-b)

**思路**：`decltype` 三条规则——不加括号的变量名给声明类型；加括号的（或一般左值表达式）给 `T&`；右值表达式给 `T`。`decltype(auto)` 用 decltype 规则推导返回类型，才能把 `int&` 一路保真。

1. 六个 `static_assert` 钉死 `decltype(x)`/`(x)`/`(x = 10)`/`(++x)`/`(x++)`/`(get_ref())` 的类型。→ 知识点：[decltype 与返回类型推导](../ch06-auto-decltype/02-decltype.md)「decltype 的推导规则」一节
2. `Container::operator[]` 用 `decltype(auto)` 保 `int&`——换成 `auto` 会返回拷贝，`c[0] = 99` 就改不动容器（且写成左值也编不过）。→ 知识点：[decltype 与返回类型推导](../ch06-auto-decltype/02-decltype.md)「函数返回类型中的应用」一节
3. 完美转发返回包装器 + 返回 `int&` 的 lambda，验证引用语义穿透两层转发。→ 知识点：[decltype 与返回类型推导](../ch06-auto-decltype/02-decltype.md)「完美转发返回值」一节、[完美转发：保持值类别的精确传递](../ch00-move-semantics/04-perfect-forwarding.md)
4. `return (x);` 悬垂陷阱：`decltype(auto)` 把括号表达式推成 `int&`，局部变量一死引用就悬空。→ 知识点：[decltype 与返回类型推导](../ch06-auto-decltype/02-decltype.md)「⚠️ 悬空引用的危险」一节

```cpp
// hw271b.cpp -- Homework 2.7-B: decltype checkup + decltype(auto)
// Standard: C++17
#include <iostream>
#include <type_traits>
#include <utility>
#include <vector>

int x = 42;
decltype(x) d1 = 100;      // int
decltype((x)) d2 = x;      // int& (parenthesized -> lvalue expression)
static_assert(std::is_same_v<decltype(d1), int>);
static_assert(std::is_same_v<decltype(d2), int&>);

int& get_ref()
{
    static int s = 7;
    return s;
}
static_assert(std::is_same_v<decltype(get_ref()), int&>);
static_assert(std::is_same_v<decltype(get_ref() + 1), int>);

decltype(x = 10) d3 = x;   // int& (assignment returns lvalue ref)
decltype(++x) d4 = x;      // int&
decltype(x++) d5 = 0;      // int (postfix ++ returns prvalue)
static_assert(std::is_same_v<decltype(d3), int&>);
static_assert(std::is_same_v<decltype(d4), int&>);
static_assert(std::is_same_v<decltype(d5), int>);

template <typename Callable, typename... Args>
decltype(auto) perfect_forward(Callable&& f, Args&&... args)
{
    return std::forward<Callable>(f)(std::forward<Args>(args)...);
}

class Container
{
public:
    decltype(auto) operator[](std::size_t i) { return data_[i]; }
    decltype(auto) operator[](std::size_t i) const { return data_[i]; }

private:
    std::vector<int> data_{1, 2, 3, 4};
};

int main()
{
    Container c;
    c[0] = 99;   // decltype(auto) preserves int&; plain auto would not compile here
    std::cout << "c[0] = " << c[0] << "\n";

    int v = 10;
    auto& ret = perfect_forward([](int& n) -> int& { n += 5; return n; }, v);
    std::cout << "after perfect_forward: v = " << v << ", ret = " << ret << "\n";
    std::cout << "all static_asserts passed\n";
    return 0;
}
```

**验证输出**：

```text
$ g++ -std=c++17 -Wall -Wextra -O2 -o hw271b hw271b.cpp && ./hw271b
c[0] = 99
after perfect_forward: v = 15, ret = 15
all static_asserts passed
```

要点：`(x)` 的一对括号把「变量名」变成「表达式」，类型就从 `int` 翻成 `int&`；`decltype(auto)` 的返回语句同理——`return x;` 安全、`return (x);` 对局部变量就是悬垂引用，编译器不一定每次都抓得住，别赌。

## 2.8-A {#hw-2-8-a}

**难度 L1** · 题面见 [homework](./01-homework.md#hw-2-8-a)

**思路**：属性是给编译器的「声明性提示」，本程序故意同时踩 `nodiscard` 忽略返回值、`deprecated` 调用、`maybe_unused` 参数、`fallthrough` 有意贯穿四条线，把真实警告全收下来。

1. `[[nodiscard]]` 函数返回值被丢弃 → `-Wunused-result`；`[[deprecated("...")]]` 被调用 → `-Wdeprecated-declarations`（自定义消息带进警告文本）。→ 知识点：[标准属性详解：让编译器成为你的代码审查员](../ch07-attributes/01-standard-attributes.md)「[[nodiscard]]」「[[deprecated]]」两节
2. `[[maybe_unused]]` 参数零警告；`[[fallthrough]]` 标明的贯穿零警告（`-Wimplicit-fallthrough` 只看「没标明的」贯穿）。→ 知识点：[标准属性详解：让编译器成为你的代码审查员](../ch07-attributes/01-standard-attributes.md)「[[maybe_unused]]」「[[fallthrough]]」两节

```cpp
// hw280a.cpp -- Homework 2.8-A: collect attribute-driven warnings
// Standard: C++17 (compile with -Wall)
#include <cstdio>
#include <cstdlib>

[[nodiscard]] int init_hardware()
{
    return 0;
}

[[deprecated("Use new_handler() instead")]]
void old_handler()
{
    std::printf("old handler\n");
}

[[noreturn]] void fatal_error(const char* msg)
{
    std::fprintf(stderr, "FATAL: %s\n", msg);
    std::abort();
}

void collect_warnings([[maybe_unused]] int unused_param)
{
    init_hardware();  // warning: ignoring return value
    old_handler();    // warning: deprecated

    switch (unused_param) {
    case 1:
        [[fallthrough]];  // intentional, no warning
    case 2:
        std::printf("handled case\n");
        break;
    default:
        break;
    }
}

int main()
{
    collect_warnings(1);
    return 0;
}
```

**验证输出**（`-Wall` 编译，警告原样）：

```text
$ g++ -std=c++17 -Wall -O2 -o hw280a hw280a.cpp
hw280a.cpp: In function 'void collect_warnings(int)':
hw280a.cpp:25:18: warning: ignoring return value of 'int init_hardware()', declared with attribute 'nodiscard' [-Wunused-result]
   25 |     init_hardware();  // warning: ignoring return value
      |     ~~~~~~~~~~~~~^~
hw280a.cpp:6:19: note: declared here
    6 | [[nodiscard]] int init_hardware()
      |                   ^~~~~~~~~~~~~
hw280a.cpp:26:16: warning: 'void old_handler()' is deprecated: Use new_handler() instead [-Wdeprecated-declarations]
   26 |     old_handler();    // warning: deprecated
      |     ~~~~~~~~~~~^~
hw280a.cpp:12:6: note: declared here
   12 | void old_handler()
      |      ^~~~~~~~~~~
$ ./hw280a
old handler
handled case
```

要点：两条警告正好对应两个属性，`deprecated` 的自定义消息直接出现在警告文本里；`[[fallthrough]]` 和 `[[maybe_unused]]` 所在处一条警告都没有——它们是「告诉编译器别喊」的属性，效果就是安静。

## 2.8-B {#hw-2-8-b}

**难度 L2** · 题面见 [homework](./01-homework.md#hw-2-8-b)

**思路**：先踩教材写法里的一个真实坑——GCC 16 下 `[[nodiscard]] enum class ...` 会被 `-Wattributes` 警告「属性被忽略」，正确位置是 `enum class [[nodiscard]] ...`（放在 `enum class` 关键字**之后**）。这是教材 ch07 例子与 GCC 16 实际行为的出入，如实记下。然后完成迁移 + 状态机，`-Wall -Wextra` 零警告收尾。

1. 属性位置实测：两种写法各编译一遍，贴出第一版的真实警告。→ 知识点：[标准属性详解：让编译器成为你的代码审查员](../ch07-attributes/01-standard-attributes.md)「属性位置的正确放置」一节（属性放错位置会被忽略——这里是活的例子）
2. 错误码 `enum class [[nodiscard]]` + 新接口 + `[[deprecated]]` 旧接口；状态机用 `[[fallthrough]]` 共享 Idle/Starting 的初始化逻辑，Paused 和 Error 空 case 直接贯穿（空 case 不需要 `[[fallthrough]]`）。→ 知识点：[标准属性详解：让编译器成为你的代码审查员](../ch07-attributes/01-standard-attributes.md)、[enum class 与强类型枚举](../ch04-type-safety/01-enum-class.md)

```cpp
// hw281b.cpp -- Homework 2.8-B: migration + state machine, zero warnings
// Standard: C++17
#include <cstdint>
#include <cstdio>

// NOTE: GCC 16 requires the attribute AFTER the 'enum class' keyword.
// "[[nodiscard]] enum class ..." is silently ignored (-Wattributes).
enum class [[nodiscard]] ErrorCode : std::uint8_t
{
    kOk = 0,
    kInvalidParam = 1,
    kTimeout = 2,
};

[[deprecated("Use read_sensor_new() which returns ErrorCode")]]
bool read_sensor_old(std::uint8_t id, std::uint16_t* value)
{
    (void)id;
    (void)value;
    return true;
}

ErrorCode read_sensor_new(std::uint8_t id, std::uint16_t* value)
{
    if (id > 3u) {
        return ErrorCode::kInvalidParam;
    }
    *value = static_cast<std::uint16_t>(id * 100u);
    return ErrorCode::kOk;
}

enum class State { kIdle, kStarting, kRunning, kPaused, kError };

void handle_state(State& current, char ev)
{
    switch (current) {
    case State::kIdle:
        if (ev == 'S') {
            current = State::kStarting;
        }
        [[fallthrough]];  // Idle shares init logic with Starting
    case State::kStarting:
        std::printf("init...\n");
        current = (ev == 'E') ? State::kError : State::kRunning;
        break;
    case State::kRunning:
        if (ev == 'P') {
            current = State::kPaused;
        }
        break;
    case State::kPaused:
    case State::kError:
        std::printf("recover\n");
        current = State::kIdle;
        break;
    }
}

int main()
{
    std::uint16_t value = 0;
    ErrorCode ec = read_sensor_new(2, &value);
    if (ec == ErrorCode::kOk) {
        std::printf("sensor 2 = %u\n", static_cast<unsigned>(value));
    }
    // read_sensor_old(2, &value);  // uncomment: deprecation warning

    State s = State::kIdle;
    handle_state(s, 'S');
    std::printf("state = %d\n", static_cast<int>(s));
    handle_state(s, 'P');
    std::printf("state = %d\n", static_cast<int>(s));
    return 0;
}
```

**验证输出**：

```text
$ # 教材写法（属性放在 enum class 之前）：GCC 16 忽略并警告
$ g++ -std=c++17 -Wall -Wextra -O2 -c hw281b_wrong.cpp
hw281b_wrong.cpp:6:26: warning: attribute ignored in declaration of 'enum class ErrorCode' [-Wattributes]
    6 | [[nodiscard]] enum class ErrorCode : std::uint8_t
      |                          ^~~~~~~~~
hw281b_wrong.cpp:6:26: note: attribute for 'enum class ErrorCode' must follow the 'enum class' keyword

$ # 修正位置后：零警告
$ g++ -std=c++17 -Wall -Wextra -O2 -o hw281b hw281b.cpp && ./hw281b
sensor 2 = 200
init...
state = 2
state = 3
```

要点：第一版编译的 `-Wattributes` 警告是 GCC 16 的真实行为——`[[nodiscard]]` 放错了侧就会被静默忽略，属性白加（教材原文尚未修订，以本实测为准）。挪到 `enum class` 关键字之后，`-Wall -Wextra` 干净通过。这是「属性位置决定属性生效对象」的活教材：位置错了编译器要么忽略、要么作用到错的目标上。

## 2.9-A {#hw-2-9-a}

**难度 L2** · 题面见 [homework](./01-homework.md#hw-2-9-a)

**思路**：把教材的 `key=value` 解析改成 HTTP 头的 `Key: value` 格式——冒号分隔、两侧 trim、逗号分段，全程 `string_view` 的 `find/substr/remove_prefix/remove_suffix`，`optional` 表示「这一截不是合法头」。

1. `parse_header`：找冒号、拆两半、四个 while 做 trim、空 key 判 `nullopt`。→ 知识点：[string_view 内部原理：非拥有字符串视图](../ch08-string-view/01-string-view-internals.md)「修改视图本身」一节
2. 主循环 `remove_prefix` 逐段消费输入——每一次 `substr` 只是指针偏移 + 长度截断，O(1)；`std::string::substr` 则是 O(n) 的分配加拷贝，同样三段输入要三次堆分配。→ 知识点：[string_view 性能分析](../ch08-string-view/02-string-view-performance.md)「substr：O(1) vs O(n) 的天壤之别」一节

```cpp
// hw290a.cpp -- Homework 2.9-A: zero-copy "Header: value" parsing
// Standard: C++17
#include <iostream>
#include <optional>
#include <string_view>
#include <utility>

/// Parse "Key: value"; trims whitespace around both parts.
std::optional<std::pair<std::string_view, std::string_view>>
parse_header(std::string_view entry)
{
    auto pos = entry.find(':');
    if (pos == std::string_view::npos) {
        return std::nullopt;
    }
    auto key = entry.substr(0, pos);
    auto value = entry.substr(pos + 1);
    auto is_ws = [](char c) { return c == ' ' || c == '\t'; };
    while (!key.empty() && is_ws(key.front())) key.remove_prefix(1);
    while (!key.empty() && is_ws(key.back())) key.remove_suffix(1);
    while (!value.empty() && is_ws(value.front())) value.remove_prefix(1);
    while (!value.empty() && is_ws(value.back())) value.remove_suffix(1);
    if (key.empty()) {
        return std::nullopt;
    }
    return std::make_pair(key, value);
}

int main()
{
    const char* raw = "Host: example.com, Accept: */*, Content-Length: 42";
    std::string_view input(raw);
    while (!input.empty()) {
        auto comma = input.find(',');
        auto segment = (comma == std::string_view::npos) ? input : input.substr(0, comma);
        if (auto hv = parse_header(segment)) {
            std::cout << "header[" << hv->first << "] = " << hv->second << "\n";
        } else {
            std::cout << "skip: [" << segment << "]\n";
        }
        if (comma == std::string_view::npos) {
            break;
        }
        input.remove_prefix(comma + 1);
    }
    return 0;
}
```

**验证输出**：

```text
$ g++ -std=c++17 -Wall -Wextra -O2 -o hw290a hw290a.cpp && ./hw290a
header[Host] = example.com
header[Accept] = */*
header[Content-Length] = 42
```

要点：整个解析过程的堆分配次数是 **0**——`raw` 是静态字面量，所有 view 都是「指针 + 长度」的指针调整；`std::string` 版同样的逻辑要构造三次临时字符串（每次分配 + 拷贝 + 释放）。代价是这些 view 全部指向 `raw`，`raw` 死了它们全悬空——这正是下一题的主题。

## 2.9-B {#hw-2-9-b}

**难度 L4** · 题面见 [homework](./01-homework.md#hw-2-9-b)

**思路**：两个陷阱各埋一颗雷、各出一份真实报告。雷 1 是「返回局部 string 的 view」——字符串超过 SSO 阈值住进堆，函数一返回堆内存被还掉，ASan 报 **heap-use-after-free**；雷 2 是「临时 string 转 view」——短字符串住在临时对象的栈上 SSO 缓冲里，语句结束临时死掉，ASan 报 **stack-use-after-scope**。两份报告类型不同，正是 SSO 的活证据。

1. 雷 1：`make_view_of_local` 返回局部 `std::string` 的 view（20 字节字符串走堆），main 里解引用。→ 知识点：[string_view 陷阱与最佳实践](../ch08-string-view/03-string-view-pitfalls.md)「陷阱一：悬垂引用」一节、[string_view 内部原理：非拥有字符串视图](../ch08-string-view/01-string-view-internals.md)「SSO：Small String Optimization」一节
2. 雷 2：`trim(std::string("  hello"))`——临时 string 活到完整表达式结束，返回的 view 立刻悬空（5 字节字符串走 SSO，数据住在栈上）。→ 知识点：[string_view 陷阱与最佳实践](../ch08-string-view/03-string-view-pitfalls.md)「隐式临时对象更隐蔽」一节
3. 修复：返回拥有所有权的 `std::string`（NRVO/移动兜底），或者让 view 只观察活得够久的具名对象。→ 知识点：[string_view 陷阱与最佳实践](../ch08-string-view/03-string-view-pitfalls.md)「最佳实践速查表」一节

```cpp
// hw291b_bad1.cpp -- trap 1: return a view of a local string
#include <iostream>
#include <string>
#include <string_view>

std::string_view make_view_of_local()
{
    std::string s = "I live on the stack";
    return s;   // UB: the string dies on return
}

int main()
{
    std::cout << std::unitbuf;
    auto v1 = make_view_of_local();
    std::cout << "v1 = " << v1 << "\n";   // use-after-free
    return 0;
}
```

```cpp
// hw291b_bad2.cpp -- trap 2: view of a temporary std::string
#include <iostream>
#include <string>
#include <string_view>

std::string_view trim(std::string_view input)
{
    while (!input.empty() && input.front() == ' ') {
        input.remove_prefix(1);
    }
    return input;
}

int main()
{
    std::cout << std::unitbuf;
    auto v2 = trim(std::string("  hello"));  // temporary dies at end of statement
    std::cout << "v2 = " << v2 << "\n";      // use-after-free
    return 0;
}
```

修复版（`hw291b_fix.cpp`）：

```cpp
// hw291b_fix.cpp -- Homework 2.9-B fix: owner survives the view
#include <iostream>
#include <string>
#include <string_view>

// Fix 1: return an owning std::string; callers may take a view of it.
std::string make_string_safe()
{
    return "I live in the returned object";
}

// Fix 2: views only observe data that the caller guarantees outlives them.
std::string_view trim(std::string_view input)
{
    while (!input.empty() && input.front() == ' ') {
        input.remove_prefix(1);
    }
    return input;
}

int main()
{
    std::string s = make_string_safe();
    std::string_view v1 = s;
    std::cout << "v1 = " << v1 << "\n";

    std::string raw = "  hello";
    std::string_view v2 = trim(raw);  // raw outlives v2
    std::cout << "v2 = " << v2 << "\n";
    return 0;
}
```

**验证输出**（真实报告节选；地址/进程号每次运行不同，堆栈帧已省略）：

```text
$ g++ -std=c++17 -Wall -Wextra -g -fsanitize=address,undefined \
      -fsanitize-address-use-after-scope -o hw291b_bad1 hw291b_bad1.cpp
$ ./hw291b_bad1
v1 = =================================================================
==594==ERROR: AddressSanitizer: heap-use-after-free on address 0x7443649e0040 at pc ...
READ of size 19 at 0x7443649e0040 thread T0
    #2 ... in std::operator<< ...(std::basic_string_view<char, ...> const&) /usr/include/c++/16/string_view:780
    #3 ... in main /tmp/cpp-v2-hw3b/hw291b_bad1.cpp:16
    ...
freed by thread T0 here:
    #6 ... in make_view_of_local() /tmp/cpp-v2-hw3b/hw291b_bad1.cpp:10
    ...
SUMMARY: AddressSanitizer: heap-use-after-free (...) in std::__ostream_insert(...)
==594==ABORTING

$ ./hw291b_bad2
v2 = =================================================================
==603==ERROR: AddressSanitizer: stack-use-after-scope on address 0x731160cf0072 at pc ...
READ of size 5 at 0x731160cf0072 thread T0
    #3 ... in main /tmp/cpp-v2-hw3b/hw291b_bad2.cpp:18
    ...
Address 0x731160cf0072 is located in stack of thread T0 at offset 114 in frame
    #0 ... in main /tmp/cpp-v2-hw3b/hw291b_bad2.cpp:15
  This frame has 3 object(s):
    [64, 80) 'v2' (line 17)
    [96, 128) '<unknown>' <== Memory access at offset 114 is inside this variable
    ...
SUMMARY: AddressSanitizer: stack-use-after-scope (...) in std::__ostream_insert(...)
==603==ABORTING

$ g++ -std=c++17 -Wall -Wextra -g -fsanitize=address,undefined \
      -fsanitize-address-use-after-scope -o hw291b_fix hw291b_fix.cpp
$ ./hw291b_fix
v1 = I live in the returned object
v2 = hello
```

要点：雷 1 的 `v1` 连输出都只吐出一半就被按住（`<<` 在打印时读到已释放内存）；雷 2 里 ASan 甚至点名了「offset 114 落在那个 `<unknown>` 变量里」——那就是临时 `std::string` 的 SSO 缓冲。同一种 bug 两份不同报告，根因是一条：**view 不拥有数据，谁的数据死了 view 就悬空**。修复的姿势是让「拥有者」比「观察者」活得久。

## 2.10-A {#hw-2-10-a}

**难度 L2** · 题面见 [homework](./01-homework.md#hw-2-10-a)

**思路**：path 的纯语法分解 + 三组修改实验。这里有个教材表述与实测不符的点要如实记下：教材说「`replace_extension` 不改变原始对象」，但 libstdc++ 上它返回 `path&` 并**就地修改接收者**——真实输出里 `p` 变成了 `report.txt`。

1. 三个路径的 `root_path/parent/filename/stem/extension` 分解（`archive.tar.gz` 的 extension 只取最后一个 `.gz`、stem 是 `archive.tar`）。→ 知识点：[path 操作：跨平台路径处理](../ch09-filesystem/01-filesystem-path.md)「路径分解：把路径拆开来看」一节
2. `replace_extension` 实测就地修改（教材原文与 libstdc++ 行为不一致，以实测为准）；`+=` 纯字符串拼接 vs `/=` 路径组件追加；绝对右操作数压过左操作数。→ 知识点：[path 操作：跨平台路径处理](../ch09-filesystem/01-filesystem-path.md)「路径修改」「append 和 concat」两节

```cpp
// hw2a0a.cpp -- Homework 2.10-A: path decomposition and modification
// Standard: C++17
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

void decompose(const fs::path& p)
{
    std::cout << "raw: " << p.string() << "\n";
    std::cout << "  root_path [" << p.root_path().string() << "]"
              << " parent [" << p.parent_path().string() << "]"
              << " filename [" << p.filename().string() << "]\n";
    std::cout << "  stem [" << p.stem().string() << "]"
              << " extension [" << p.extension().string() << "]\n";
}

int main()
{
    decompose("/home/user/docs/report.tar.gz");
    decompose("config.ini");
    decompose("/tmp/archive.tar.gz");

    // NOTE: on libstdc++, replace_extension() modifies the receiver in place
    // (it returns path&). The textbook claim "does not modify the original"
    // does not hold on this implementation.
    fs::path p = "/home/user/report.pdf";
    std::cout << "p before:  " << p << "\n";
    p.replace_extension(".txt");
    std::cout << "p after replace_extension(.txt): " << p << "\n";

    fs::path f = "file";
    f += ".txt";
    std::cout << "concat (+=):              " << f << "\n";
    fs::path f2 = "file";
    f2 /= ".txt";
    std::cout << "append (/=):              " << f2 << "\n";

    fs::path base = "/home/user";
    std::cout << "base / \"/tmp/x\" (absolute rhs wins): " << (base / "/tmp/x") << "\n";
    return 0;
}
```

**验证输出**：

```text
$ g++ -std=c++17 -Wall -Wextra -O2 -o hw2a0a hw2a0a.cpp && ./hw2a0a
raw: /home/user/docs/report.tar.gz
  root_path [/] parent [/home/user/docs] filename [report.tar.gz]
  stem [report.tar] extension [.gz]
raw: config.ini
  root_path [] parent [] filename [config.ini]
  stem [config] extension [.ini]
raw: /tmp/archive.tar.gz
  root_path [/] parent [/tmp] filename [archive.tar.gz]
  stem [archive.tar] extension [.gz]
p before:  "/home/user/report.pdf"
p after replace_extension(.txt): "/home/user/report.txt"
concat (+=):              "file.txt"
append (/=):              "file/.txt"
base / "/tmp/x" (absolute rhs wins): "/tmp/x"
```

要点：`replace_extension` 在本机 libstdc++ 上是**就地修改**（`p` 从 `report.pdf` 变成 `report.txt`）——教材那句「不改变原始对象」与标准库实际行为不符（教材原文尚未修订，以本实测为准），要用「改完的原对象」还是「改前的旧路径」，务必先自己跑一遍再定代码。`+=` 是纯字符串拼接，`/=` 把右操作数当独立路径组件；右操作数是绝对路径时，`/` 直接返回右操作数。

## 2.10-B {#hw-2-10-b}

**难度 L3** · 题面见 [homework](./01-homework.md#hw-2-10-b)

**思路**：自建目录树 + 递归遍历 + 按扩展名分组统计 + 找最大文件。遍历顺序未指定，所以分组用 `std::map`（自动按扩展名字典序输出），最大文件用「当前最大」变量在线维护。

1. `recursive_directory_iterator(root, skip_permission_denied, ec)` 遍历；`entry.is_regular_file()` 过滤；`directory_entry` 的 `file_size()` 走缓存，省系统调用。→ 知识点：[目录遍历与搜索](../ch09-filesystem/03-directory-iteration.md)「recursive_directory_iterator」「directory_entry：不只是 path」两节
2. `std::map` 分组聚合 + 在线最大文件；`error_code` 版接口避免权限错误中断遍历。→ 知识点：[目录遍历与搜索](../ch09-filesystem/03-directory-iteration.md)「遍历时过滤」一节、[文件与目录操作](../ch09-filesystem/02-filesystem-ops.md)「错误处理的两种模式」一节

```cpp
// hw2a1b.cpp -- Homework 2.10-B: extension census + largest file
// Standard: C++17
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <map>
#include <string>

namespace fs = std::filesystem;

int main(int argc, char** argv)
{
    fs::path root = (argc > 1) ? fs::path(argv[1]) : fs::path("/tmp/hw2a1b_tree");
    std::error_code ec;
    auto options = fs::directory_options::skip_permission_denied;

    std::map<std::string, std::pair<int, std::uintmax_t>> by_ext;
    std::uintmax_t max_size = 0;
    fs::path max_path;

    for (auto it = fs::recursive_directory_iterator(root, options, ec);
         it != fs::recursive_directory_iterator(); ++it) {
        if (ec) {
            ec.clear();
            continue;
        }
        const auto& entry = *it;
        if (!entry.is_regular_file()) {
            continue;
        }
        auto ext = entry.path().extension().string();
        auto size = entry.file_size();
        by_ext[ext].first += 1;
        by_ext[ext].second += size;
        if (size > max_size) {
            max_size = size;
            max_path = entry.path();
        }
    }

    std::cout << "root: " << root.string() << "\n";
    for (const auto& [ext, stat] : by_ext) {
        std::cout << "  ext " << (ext.empty() ? "(none)" : ext)
                  << ": " << stat.first << " file(s), "
                  << stat.second << " byte(s)\n";
    }
    std::cout << "largest: " << max_path.filename().string()
              << " (" << max_size << " bytes)\n";
    return 0;
}
```

**验证输出**：

```text
$ g++ -std=c++17 -Wall -Wextra -O2 -o hw2a1b hw2a1b.cpp
$ # 建树: a.txt(10B)、b.log(100B)、sub/c.txt(50B)、sub/d.bin(200B)
$ ./hw2a1b /tmp/hw2a1b_tree
root: /tmp/hw2a1b_tree
  ext .bin: 1 file(s), 200 byte(s)
  ext .log: 1 file(s), 100 byte(s)
  ext .txt: 2 file(s), 60 byte(s)
largest: d.bin (200 bytes)
```

要点：三个扩展名分组各就各位（两个 .txt 合计 60 字节），最大文件在线跟踪一次遍历搞定。遍历顺序未指定但 `std::map` 保证输出按扩展名字典序；换成 `unordered_map` 输出顺序就乱了。

## 2.11-A {#hw-2-11-a}

**难度 L2** · 题面见 [homework](./01-homework.md#hw-2-11-a)

**思路**：`optional` 的三种典型场景各来一段：解析（可能失败）、查找（可能没有）、默认值（`value_or` 收尾）。`from_chars` 的返回值是「指针 + 错误码」结构，配合结构化绑定一行收下。

1. `parse_int`/`parse_double`：`from_chars` 全部消费完且无错误才 `return value`，否则 `nullopt`。→ 知识点：[optional 用于错误处理](../ch10-error-handling/02-optional-error.md)「解析操作」一节、[结构化绑定：一行解包多个值](../ch05-structured-bindings/01-structured-bindings.md)
2. map 查找的 `optional` 封装 + `value_or` 默认值。→ 知识点：[optional 用于错误处理](../ch10-error-handling/02-optional-error.md)「查找操作」「带默认值的场景」两节

```cpp
// hw2b0a.cpp -- Homework 2.11-A: optional for parse/find/default
// Standard: C++17
#include <charconv>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>

std::optional<int> parse_int(std::string_view sv)
{
    int value = 0;
    auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), value);
    if (ec == std::errc{} && ptr == sv.data() + sv.size()) {
        return value;
    }
    return std::nullopt;
}

std::optional<double> parse_double(std::string_view sv)
{
    double value = 0.0;
    auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), value);
    if (ec == std::errc{} && ptr == sv.data() + sv.size()) {
        return value;
    }
    return std::nullopt;
}

int main()
{
    std::cout << "parse_int(\"42\")   = " << parse_int("42").value_or(-1) << "\n";
    std::cout << "parse_int(\"42a\")  = " << parse_int("42a").value_or(-1) << "\n";
    std::cout << "parse_double(\"3.14\") = " << parse_double("3.14").value_or(0.0) << "\n";
    std::cout << "parse_double(\"x\")    = " << parse_double("x").value_or(0.0) << "\n";

    std::unordered_map<int, std::string> users = {{1, "Alice"}, {2, "Bob"}};
    auto find_user = [&users](int id) -> std::optional<std::string> {
        auto it = users.find(id);
        if (it != users.end()) {
            return it->second;
        }
        return std::nullopt;
    };
    std::cout << "find_user(2) = " << find_user(2).value_or("(none)") << "\n";
    std::cout << "find_user(3) = " << find_user(3).value_or("(none)") << "\n";

    std::optional<std::string> cfg = std::nullopt;
    std::cout << "config default = " << cfg.value_or("INFO") << "\n";
    return 0;
}
```

**验证输出**：

```text
$ g++ -std=c++17 -Wall -Wextra -O2 -o hw2b0a hw2b0a.cpp && ./hw2b0a
parse_int("42")   = 42
parse_int("42a")  = -1
parse_double("3.14") = 3.14
parse_double("x")    = 0
find_user(2) = Bob
find_user(3) = (none)
config default = INFO
```

要点：`optional` 把「可能没有值」写进类型签名，调用方不检查连值都拿不到；`from_chars` 的「全消费」判据（`ptr` 走到末尾）比 `stoi` 的异常套路更轻。`value_or` 一条线兜住所有「没值怎么办」。

## 2.11-B {#hw-2-11-b}

**难度 L3** · 题面见 [homework](./01-homework.md#hw-2-11-b)

**思路**：教材的简化版 `expected` 有个真实的编译坑——匿名 union 里放着 `std::string` 这类非平凡成员时，union 的隐式默认构造/析构被删除，整个类实例化即报错。修复是给 union 起名并补上一对空的 `Storage()`/`~Storage()`（活跃成员由 `expected` 自己负责构造/析构）。然后搭「地址解析链」。

1. 先复现编译错误（原版匿名 union 的原样报错），再修复：`union Storage { T val_; E err_; Storage() {} ~Storage() {} } storage_;`。→ 知识点：[std::expected\<T, E>：类型安全的错误传播](../ch10-error-handling/03-expected-error.md)「C++17 环境下的简化实现」一节（教材代码的编译坑与修法——这是教材以外必须补的机制点）
2. `and_then` 串联 `validate_input → split_address`，`transform` 把成功值拼成字符串；五个输入覆盖全部四条错误路径。→ 知识点：[std::expected\<T, E>：类型安全的错误传播](../ch10-error-handling/03-expected-error.md)「monadic 操作」一节、[错误处理模式总结：选择指南与最佳实践](../ch10-error-handling/04-error-patterns.md)

```cpp
// hw2b1b.cpp -- Homework 2.11-B: C++17 simplified expected + error chain
// Standard: C++17
#include <charconv>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

enum class AddrError
{
    kEmptyInput,
    kMissingColon,
    kEmptyHost,
    kBadPort,
    kPortOutOfRange,
};

const char* to_string(AddrError e)
{
    switch (e) {
    case AddrError::kEmptyInput:     return "input is empty";
    case AddrError::kMissingColon:   return "no colon found";
    case AddrError::kEmptyHost:      return "host is empty";
    case AddrError::kBadPort:        return "port is not a number";
    case AddrError::kPortOutOfRange: return "port out of range [1,65535]";
    }
    return "unknown";
}

template <typename E>
struct unexpected
{
    E value;
    constexpr explicit unexpected(E v) : value(std::move(v)) {}
};

template <typename T, typename E>
class expected
{
    bool has_value_;
    union Storage
    {
        T val_;
        E err_;
        Storage() {}        // no active member at birth; expected picks one
        ~Storage() {}       // expected destroys the active member itself
    } storage_;

public:
    expected(const T& v) : has_value_(true) { new (&storage_.val_) T(v); }
    expected(T&& v) : has_value_(true) { new (&storage_.val_) T(std::move(v)); }
    expected(unexpected<E> u) : has_value_(false) { new (&storage_.err_) E(std::move(u.value)); }

    expected(const expected& other) : has_value_(other.has_value_)
    {
        if (has_value_) {
            new (&storage_.val_) T(other.storage_.val_);
        } else {
            new (&storage_.err_) E(other.storage_.err_);
        }
    }

    expected& operator=(const expected& other)
    {
        if (this != &other) {
            if (has_value_) {
                storage_.val_.~T();
            } else {
                storage_.err_.~E();
            }
            has_value_ = other.has_value_;
            if (has_value_) {
                new (&storage_.val_) T(other.storage_.val_);
            } else {
                new (&storage_.err_) E(other.storage_.err_);
            }
        }
        return *this;
    }

    ~expected()
    {
        if (has_value_) {
            storage_.val_.~T();
        } else {
            storage_.err_.~E();
        }
    }

    constexpr bool has_value() const noexcept { return has_value_; }
    constexpr explicit operator bool() const noexcept { return has_value_; }

    T& value() { return storage_.val_; }
    const T& value() const { return storage_.val_; }
    const E& error() const { return storage_.err_; }
    T& operator*() { return storage_.val_; }
    T* operator->() { return &storage_.val_; }

    template <typename F>
    auto and_then(F&& f) -> decltype(f(std::declval<T>()))
    {
        using R = decltype(f(std::declval<T>()));
        if (has_value_) {
            return f(storage_.val_);
        }
        return R(unexpected<E>{storage_.err_});
    }

    template <typename F>
    auto transform(F&& f) -> expected<decltype(f(std::declval<T>())), E>
    {
        using U = decltype(f(std::declval<T>()));
        if (has_value_) {
            return expected<U, E>(f(storage_.val_));
        }
        return expected<U, E>(unexpected<E>{storage_.err_});
    }
};

struct NetworkAddress
{
    std::string host;
    int port;
};

expected<std::string, AddrError> validate_input(std::string_view input)
{
    if (input.empty()) {
        return unexpected<AddrError>{AddrError::kEmptyInput};
    }
    return std::string(input);
}

expected<NetworkAddress, AddrError> split_address(std::string input)
{
    auto colon = input.rfind(':');
    if (colon == std::string::npos) {
        return unexpected<AddrError>{AddrError::kMissingColon};
    }
    NetworkAddress addr;
    addr.host = input.substr(0, colon);
    if (addr.host.empty()) {
        return unexpected<AddrError>{AddrError::kEmptyHost};
    }
    std::string port_str = input.substr(colon + 1);
    int port = 0;
    auto [ptr, ec] = std::from_chars(port_str.data(), port_str.data() + port_str.size(), port);
    if (ec != std::errc{} || ptr != port_str.data() + port_str.size()) {
        return unexpected<AddrError>{AddrError::kBadPort};
    }
    if (port < 1 || port > 65535) {
        return unexpected<AddrError>{AddrError::kPortOutOfRange};
    }
    addr.port = port;
    return addr;
}

int main()
{
    auto run = [](std::string_view s) {
        auto result = validate_input(s)
            .and_then(split_address)
            .transform([](const NetworkAddress& a) {
                return a.host + ":" + std::to_string(a.port);
            });
        if (result) {
            std::cout << "  OK    \"" << s << "\" -> " << *result << "\n";
        } else {
            std::cout << "  FAIL  \"" << s << "\" -> " << to_string(result.error()) << "\n";
        }
    };
    run("192.168.1.1:8080");
    run("localhost");
    run(":9090");
    run("host:99999");
    run("");
    return 0;
}
```

**验证输出**：

```text
$ g++ -std=c++17 -Wall -Wextra -O2 -o hw2b1b hw2b1b.cpp && ./hw2b1b
  OK    "192.168.1.1:8080" -> 192.168.1.1:8080
  FAIL  "localhost" -> no colon found
  FAIL  ":9090" -> host is empty
  FAIL  "host:99999" -> port out of range [1,65535]
  FAIL  "" -> input is empty

$ # ASan/UBSan 构建下同样输出，零报告（exit 0）
```

要点：union 修复后 ASan/UBSan 全绿；`and_then` 让「哪一步失败，错误就原样穿透到链尾」，`transform` 只处理成功路径。教材那份实现若直接抄会撞上匿名 union 的删除构造——改完再跑，这才是「能编译」的 C++17 `expected`。

## 2.12-A {#hw-2-12-a}

**难度 L2** · 题面见 [homework](./01-homework.md#hw-2-12-a)

**思路**：三个 cooked 形式字面量（整数 `unsigned long long`、浮点 `long double`）+ 两条 `static_assert` 钉死编译期求值；再演示标准库的 `chrono`/`string_view` 字面量。后缀命名规则：不带下划线的留给标准库，用户后缀必须 `_` 开头。

1. `_ms`/`_KiB`/`_kHz` 三个 `constexpr operator""` + `static_assert`。→ 知识点：[用户自定义字面量基础](../ch11-user-defined-literals/01-udl-basics.md)「operator"" 的四种形式」「编译期 vs 运行期」两节
2. `1s + 500ms` 的 chrono 运算、`"hello"sv` 的 string_view 字面量。→ 知识点：[用户自定义字面量基础](../ch11-user-defined-literals/01-udl-basics.md)「标准库字面量」「命名规则」两节

```cpp
// hw2c0a.cpp -- Homework 2.12-A: user-defined literals + std literals
// Standard: C++17
#include <chrono>
#include <cstdint>
#include <iostream>
#include <string_view>

struct Milliseconds
{
    std::uint64_t value;
};

struct Bytes
{
    std::uint64_t value;
};

struct Hertz
{
    std::uint32_t value;
};

constexpr Milliseconds operator""_ms(unsigned long long v)
{
    return Milliseconds{v};
}

constexpr Bytes operator""_KiB(unsigned long long v)
{
    return Bytes{v * 1024};
}

constexpr Hertz operator""_kHz(long double v)
{
    return Hertz{static_cast<std::uint32_t>(v * 1000.0L)};
}

constexpr auto kTimeout = 500_ms;
static_assert(kTimeout.value == 500);

constexpr auto kBuffer = 4_KiB;
static_assert(kBuffer.value == 4096);

constexpr auto kFreq = 1.5_kHz;
static_assert(kFreq.value == 1500);

int main()
{
    using namespace std::chrono_literals;
    using namespace std::string_view_literals;

    std::cout << "500_ms  = " << kTimeout.value << " ms\n";
    std::cout << "4_KiB   = " << kBuffer.value << " bytes\n";
    std::cout << "1.5_kHz = " << kFreq.value << " Hz\n";

    auto t = 1s + 500ms;
    std::cout << "1s + 500ms = " << t.count() << " ms\n";

    auto sv = "hello"sv;
    std::cout << "sv = " << sv << ", length " << sv.size() << "\n";
    return 0;
}
```

**验证输出**：

```text
$ g++ -std=c++17 -Wall -Wextra -O2 -o hw2c0a hw2c0a.cpp && ./hw2c0a
500_ms  = 500 ms
4_KiB   = 4096 bytes
1.5_kHz = 1500 Hz
1s + 500ms = 1500 ms
sv = hello, length 5
```

要点：三条 `static_assert` 说明这些字面量在编译期就求值完毕，运行时零开销；`1s + 500ms` 的 chrono 运算自带单位换算（同单位类相加，结果是 `milliseconds{1500}`）。后缀不带下划线（`ms`、`s`、`sv`）是标准库预留的，自己写的一定要 `_` 开头。

## 2.12-B {#hw-2-12-b}

**难度 L3** · 题面见 [homework](./01-homework.md#hw-2-12-b)

**思路**：`Quantity<T, UnitTag>` 用空标签把「同底层类型、不同单位」劈成不同静态类型；跨单位运算只对「长度/时间 → 速度」这类物理上有意义的组合开绿灯。`2 * 100.0_m` 会撞上模板推导冲突（`T` 一边是 `int` 一边是 `long double`），按教材给的重载补一个 `int × long double` 版本。

1. `Quantity` 模板 + 加/减/标量乘/比较 + `operator*` 反向重载。→ 知识点：[UDL 实战：类型安全的单位系统](../ch11-user-defined-literals/02-udl-practice.md)「第一步：长度单位系统」一节
2. `_m/_km/_s/_h` 字面量 + `Length/Duration → Speed`、`Speed × Duration → Length`。→ 知识点：[UDL 实战：类型安全的单位系统](../ch11-user-defined-literals/02-udl-practice.md)「第二步：时间与速度单位」一节
3. 四条 `static_assert` + 两条「编译不过」的注释。→ 知识点：[UDL 实战：类型安全的单位系统](../ch11-user-defined-literals/02-udl-practice.md)（编译期单位检查）、[强类型 typedef：防止混淆的类型安全](../ch04-type-safety/02-strong-types.md)（phantom type 零开销）

```cpp
// hw2c1b.cpp -- Homework 2.12-B: type-safe unit system (length/time/speed)
// Standard: C++17
#include <iostream>

struct MeterTag {};
struct SecondTag {};
struct SpeedTag {};

template <typename T, typename UnitTag>
struct Quantity
{
    T value;
    constexpr explicit Quantity(T v) : value(v) {}

    constexpr Quantity operator+(Quantity other) const { return Quantity{value + other.value}; }
    constexpr Quantity operator-(Quantity other) const { return Quantity{value - other.value}; }
    constexpr Quantity operator*(T scalar) const { return Quantity{value * scalar}; }
    constexpr Quantity operator/(T scalar) const { return Quantity{value / scalar}; }
    constexpr bool operator==(Quantity other) const { return value == other.value; }
};

template <typename T, typename UnitTag>
constexpr Quantity<T, UnitTag> operator*(T scalar, Quantity<T, UnitTag> q)
{
    return q * scalar;
}

// int scalar times a long double quantity: T deduction would conflict,
// so this overload covers the mixed case.
template <typename UnitTag>
constexpr Quantity<long double, UnitTag> operator*(int scalar, Quantity<long double, UnitTag> q)
{
    return Quantity<long double, UnitTag>{q.value * static_cast<long double>(scalar)};
}

using Length = Quantity<long double, MeterTag>;
using Duration = Quantity<long double, SecondTag>;
using Speed = Quantity<long double, SpeedTag>;

constexpr Length operator""_m(long double v) { return Length{v}; }
constexpr Length operator""_m(unsigned long long v)
{
    return Length{static_cast<long double>(v)};
}
constexpr Length operator""_km(long double v) { return Length{v * 1000.0L}; }
constexpr Duration operator""_s(long double v) { return Duration{v}; }
constexpr Duration operator""_s(unsigned long long v)
{
    return Duration{static_cast<long double>(v)};
}
constexpr Duration operator""_h(long double v) { return Duration{v * 3600.0L}; }

constexpr Speed operator/(Length l, Duration t)
{
    return Speed{l.value / t.value};
}

constexpr Length operator*(Speed s, Duration t)
{
    return Length{s.value * t.value};
}

constexpr auto d_total = 1.0_km + 500.0_m;   // 1500 m
static_assert(d_total.value == 1500.0L);

constexpr auto v1 = 36.0_km / 1.0_h;         // 10 m/s
static_assert(v1.value == 10.0L);

constexpr auto d_back = v1 * 90.0_s;         // 900 m
static_assert(d_back.value == 900.0L);

constexpr auto d_scaled = 2 * 100.0_m;       // 200 m
static_assert(d_scaled.value == 200.0L);

// Compile errors (uncomment to watch):
// auto bad = 100_m + 50_s;    // Length + Duration
// auto bad2 = 100_m * 10_s;   // Length * Duration

int main()
{
    std::cout << "1km + 500m = " << static_cast<double>(d_total.value) << " m\n";
    std::cout << "36km / 1h  = " << static_cast<double>(v1.value) << " m/s\n";
    std::cout << "v1 * 90s   = " << static_cast<double>(d_back.value) << " m\n";
    std::cout << "all static_asserts passed\n";
    return 0;
}
```

**验证输出**：

```text
$ g++ -std=c++17 -Wall -Wextra -O2 -o hw2c1b hw2c1b.cpp && ./hw2c1b
1km + 500m = 1500 m
36km / 1h  = 10 m/s
v1 * 90s   = 900 m
all static_asserts passed
```

要点：`36.0_km / 1.0_h` 在编译期算出 `10 m/s`——36 km/h 就是 10 m/s；`2 * 100.0_m` 需要教材补的那个 `int × long double` 重载，因为主模板要求标量类型与 `Quantity` 的 `T` 完全一致。取消注释的两行错误代码会给出「no match for operator+」这类报错——编译期单位检查，运行时零开销。

## 2.C-1 {#hw-2-c-1}

**难度 L3** · 题面见 [homework](./01-homework.md#hw-2-c-1)

**思路**：拆解任务——string_view 分段拆键值对、值按「bool → int → double → 字符串」优先级解析进 variant、Overloaded 访问者打印。真正的坑在最后：lambda 里直接捕获结构化绑定变量 `key` 在 C++17 不合法（C++20 的 P1091R3 才允许）；显式捕获 `[&key]` 时 g++ 16.1.1 与 clang++ 22.1.8 都亮 `-Wc++20-extensions`。注意这个警告行为**随分解形状而异**：对 `struct` 分解（本题这种），gcc 对隐式捕获 `[&]`/`[=]` 照样警告（与 clang 一样）；gcc 的「隐式捕获静默」只出现在 **tuple-like 分解**（`std::pair`/`std::tuple`，如 map 迭代）下——「不合法」与「编译器拦不拦」是两回事，而「拦不拦」还取决于代码形状。教材 ch05「C++17 就支持直接捕获」的说法与标准不符；用初始化捕获 `[k = key]` 复制一份才是干净的 C++17。

1. 主循环 `find(';')` + `remove_prefix` 消费；每段 `find('=')` 拆键值。→ 知识点：[string_view 内部原理：非拥有字符串视图](../ch08-string-view/01-string-view-internals.md)「修改视图本身」一节
2. `parse_value` 用 `from_chars` 依次尝试 int、double，失败落字符串；bool 精确匹配。→ 知识点：[std::variant：类型安全的联合体](../ch04-type-safety/03-variant.md)「配置值」一节
3. `Overloaded` 访问者 + 结构化绑定遍历；捕获坑用初始化捕获规避。→ 知识点：[std::variant：类型安全的联合体](../ch04-type-safety/03-variant.md)「使用 lambda 的简单 visit」「重载集合」两节、[Lambda 捕获机制深入](../ch03-lambda/02-lambda-capture.md)「C++14 初始化捕获」一节

```cpp
// hw2c1.cpp -- Homework C-1: zero-copy config parser (string_view + variant)
// Standard: C++17
#include <charconv>
#include <iostream>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

using ConfigValue = std::variant<int, double, std::string_view, bool>;

struct Entry
{
    std::string_view key;
    ConfigValue value;
};

template <class... Ts>
struct Overloaded : Ts...
{
    using Ts::operator()...;
};

template <class... Ts>
Overloaded(Ts...) -> Overloaded<Ts...>;

ConfigValue parse_value(std::string_view sv)
{
    if (sv == "true") {
        return true;
    }
    if (sv == "false") {
        return false;
    }
    int iv = 0;
    auto [p1, ec1] = std::from_chars(sv.data(), sv.data() + sv.size(), iv);
    if (ec1 == std::errc{} && p1 == sv.data() + sv.size()) {
        return iv;
    }
    double dv = 0.0;
    auto [p2, ec2] = std::from_chars(sv.data(), sv.data() + sv.size(), dv);
    if (ec2 == std::errc{} && p2 == sv.data() + sv.size()) {
        return dv;
    }
    return sv;
}

int main()
{
    const char* raw = "host=192.168.1.1;port=8080;debug=true;pi=3.14;name=alpha";
    std::string_view input(raw);
    std::vector<Entry> entries;

    while (!input.empty()) {
        auto semi = input.find(';');
        std::string_view segment =
            (semi == std::string_view::npos) ? input : input.substr(0, semi);
        auto eq = segment.find('=');
        if (eq != std::string_view::npos) {
            auto key = segment.substr(0, eq);
            auto value = segment.substr(eq + 1);
            entries.push_back(Entry{key, parse_value(value)});
        }
        if (semi == std::string_view::npos) {
            break;
        }
        input.remove_prefix(semi + 1);
    }

    // C++17: structured bindings CANNOT be captured by lambdas (that is a
    // C++20 extension). Init-capture a copy of the key instead.
    for (const auto& [key, value] : entries) {
        std::visit(Overloaded{
            [k = key](int v) { std::cout << k << " = " << v << " (int)\n"; },
            [k = key](double v) { std::cout << k << " = " << v << " (double)\n"; },
            [k = key](bool v) {
                std::cout << k << " = " << (v ? "true" : "false") << " (bool)\n";
            },
            [k = key](std::string_view v) { std::cout << k << " = " << v << " (string)\n"; }
        }, value);
    }
    return 0;
}
```

**验证输出**：

```text
$ g++ -std=c++17 -Wall -Wextra -O2 -o hw2c1 hw2c1.cpp && ./hw2c1
host = 192.168.1.1 (string)
port = 8080 (int)
debug = true (bool)
pi = 3.14 (double)
name = alpha (string)
```

捕获坑的真实警告（直接捕获 `key` 的版本——显式捕获 `[&key]` 时 **g++ 16.1.1 与 clang++ 22.1.8 都报警**，四个 lambda 各一条，此处各节选一条）：

```text
$ g++ -std=c++17 -Wall -Wextra -c hw2c1_sb_capture.cpp
hw2c1_sb_capture.cpp:75:15: warning: captured structured bindings are a C++20 extension [-Wc++20-extensions]
   75 |             [&key](int v) { std::cout << key << " = " << v << " (int)\n"; },
      |               ^~~

$ clang++ -std=c++17 -Wall -Wextra -c hw2c1_sb_capture.cpp
hw2c1_sb_capture.cpp:75:15: warning: captured structured bindings are a C++20 extension [-Wc++20-extensions]
   75 |             [&key](int v) { std::cout << key << " = " << v << " (int)\n"; },
      |               ^
```

把捕获改成**隐式**的 `[&]`（教材最常写的形态），gcc 16 在本**题 struct 分解下照样报警**（和显式捕获一样）——两种编译器、两种捕获形态，此题形状下行为一致；gcc 的「隐式捕获静默」只在 tuple-like 分解（pair/tuple）下成立：

```text
$ g++ -std=c++17 -Wall -Wextra -c hw2c1_sb_implicit.cpp
hw2c1_sb_implicit.cpp:11:31: warning: captured structured bindings are a C++20 extension [-Wc++20-extensions]
   11 |             [&](int v) { std::cout << key << " = " << v << " (int)
"; },
      |                               ^~~
hw2c1_sb_implicit.cpp:10:15: note: declared here
   10 |             auto [key, value] = entry;
      |               ^~~
```

要点：五个值全部零拷贝分类落袋——`192.168.1.1` 不是合法数字所以落字符串、`pi` 落 double、`port` 落 int、`debug` 落 bool。结构化绑定捕获是 C++20 才进标准的（P1091R3），教材 ch05 在这个点上说得不准（教材原文尚未修订，以本实测为准）；显式捕获 `[&key]` 两个编译器都亮 `-Wc++20-extensions`；隐式捕获 `[&]` 在本题 struct 分解下 gcc 也照报（gcc 的静默只在 tuple-like 分解下出现）——「不合法」与「编译器拦不拦」确实是两回事，拦不拦还随代码形状变；用 `[k = key]`（C++14 初始化捕获）既干净又不越标准。

## 2.C-2 {#hw-2-c-2}

**难度 L4** · 题面见 [homework](./01-homework.md#hw-2-c-2)

**思路**：`make_object` 的 `Args&&... args` 是转发引用参数包，`std::forward<Args>(args)...` 把每个实参的「值类别」原样透传给 `TrackedString` 的构造函数。四个实验的机制各不相同：A 左值 → 拷贝构造一次；B `std::move` → 移动构造一次（源对象变空）；C 字面量 → 直接匹配 `const char*` 构造函数，零拷贝零移动；D 里 `make_object` 返回的 `unique_ptr` 是 prvalue，C++17 保证消除 + `unique_ptr` 的移动构造（noexcept）让三个对象一次都不被搬。

1. `make_object` 转发工厂（仿 `make_unique`）。→ 知识点：[完美转发：保持值类别的精确传递](../ch00-move-semantics/04-perfect-forwarding.md)「完美转发在标准库中的应用」一节
2. 四个实验 + 计数报表。→ 知识点：[右值引用：从拷贝到移动](../ch00-move-semantics/01-rvalue-reference.md)「push_back vs emplace_back」的追踪手法、[RVO 与 NRVO：编译器的返回值优化](../ch00-move-semantics/03-rvo-nrvo.md)（C++17 保证消除）
3. D 的解释：`unique_ptr` 本身可移动不可拷贝、移动 noexcept，所以能进 vector；`push_back(prvalue)` 走 C++17 保证消除，连 `unique_ptr` 的移动都被省掉。→ 知识点：[unique_ptr 详解：独占所有权的零开销智能指针](../ch01-smart-pointers/02-unique-ptr.md)「移动语义与 unique_ptr 的深层关系」一节

```cpp
// hw2c2.cpp -- Homework C-2: perfect forwarding factory + move tracking
// Standard: C++17
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

struct TrackedString
{
    std::string data;
    static int ctor_count;
    static int copy_count;
    static int move_count;

    explicit TrackedString(const char* s) : data(s)
    {
        ++ctor_count;
        std::cout << "  [ctor from const char*] \"" << data << "\"\n";
    }
    TrackedString(const TrackedString& other) : data(other.data)
    {
        ++copy_count;
        std::cout << "  [copy] \"" << data << "\"\n";
    }
    TrackedString(TrackedString&& other) noexcept : data(std::move(other.data))
    {
        ++move_count;
        std::cout << "  [move] \"" << data << "\"\n";
    }
    ~TrackedString()
    {
        std::cout << "  [dtor] \"" << data << "\"\n";
    }
};

int TrackedString::ctor_count = 0;
int TrackedString::copy_count = 0;
int TrackedString::move_count = 0;

template <typename T, typename... Args>
std::unique_ptr<T> make_object(Args&&... args)
{
    return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
}

void reset()
{
    TrackedString::ctor_count = 0;
    TrackedString::copy_count = 0;
    TrackedString::move_count = 0;
}

int main()
{
    std::cout << "=== A. lvalue argument: one copy ===\n";
    reset();
    {
        TrackedString name("alpha");
        auto p = make_object<TrackedString>(name);
    }
    std::cout << "ctor=" << TrackedString::ctor_count
              << " copy=" << TrackedString::copy_count
              << " move=" << TrackedString::move_count << "\n\n";

    std::cout << "=== B. rvalue (std::move): one move ===\n";
    reset();
    {
        TrackedString name("beta");
        auto p = make_object<TrackedString>(std::move(name));
    }
    std::cout << "ctor=" << TrackedString::ctor_count
              << " copy=" << TrackedString::copy_count
              << " move=" << TrackedString::move_count << "\n\n";

    std::cout << "=== C. string literal: direct conversion, zero copy/move ===\n";
    reset();
    {
        auto p = make_object<TrackedString>("gamma");
    }
    std::cout << "ctor=" << TrackedString::ctor_count
              << " copy=" << TrackedString::copy_count
              << " move=" << TrackedString::move_count << "\n\n";

    std::cout << "=== D. vector<unique_ptr<TrackedString>>, three push_backs ===\n";
    reset();
    {
        std::vector<std::unique_ptr<TrackedString>> v;
        v.push_back(make_object<TrackedString>("d1"));
        v.push_back(make_object<TrackedString>("d2"));
        v.push_back(make_object<TrackedString>("d3"));
    }
    std::cout << "ctor=" << TrackedString::ctor_count
              << " copy=" << TrackedString::copy_count
              << " move=" << TrackedString::move_count << "\n";
    return 0;
}
```

**验证输出**：

```text
$ g++ -std=c++17 -Wall -Wextra -O2 -o hw2c2 hw2c2.cpp && ./hw2c2
=== A. lvalue argument: one copy ===
  [ctor from const char*] "alpha"
  [copy] "alpha"
  [dtor] "alpha"
  [dtor] "alpha"
ctor=1 copy=1 move=0

=== B. rvalue (std::move): one move ===
  [ctor from const char*] "beta"
  [move] "beta"
  [dtor] "beta"
  [dtor] ""
ctor=1 copy=0 move=1

=== C. string literal: direct conversion, zero copy/move ===
  [ctor from const char*] "gamma"
  [dtor] "gamma"
ctor=1 copy=0 move=0

=== D. vector<unique_ptr<TrackedString>>, three push_backs ===
  [ctor from const char*] "d1"
  [ctor from const char*] "d2"
  [ctor from const char*] "d3"
  [dtor] "d1"
  [dtor] "d2"
  [dtor] "d3"
ctor=3 copy=0 move=0
```

要点：A 的 `[copy]` 是 `name`（左值）被转发进构造函数时触发；B 的 `[move]` 后源对象析构时打印空串——「被移动过」的活证据；C 里 `"gamma"` 直接匹配 `const char*` 构造，转发保住了这个最佳路径，若改成按值接收就会多一次拷贝；D 三个对象只构造、零拷贝零移动——`unique_ptr` 的 noexcept 移动让它们能住进 vector，C++17 的保证消除把 `push_back` 参数构造直接挪进 vector 的槽位，`TrackedString` 从头到尾没被搬过。

## 2.C-3 {#hw-2-c-3}

**难度 L5** · 题面见 [homework](./01-homework.md#hw-2-c-3)

**思路**：MiniAny = 32 字节对齐缓冲 + 堆溢出通道 + 五个函数指针组成的 vtable。两条暗雷：①**字节级交换炸 SSO**——短 `std::string` 的字符就住在 string 对象内部的本地缓冲里，把 MiniAny 的 storage 按字节对调，字符串内部指针还指向老位置（另一侧对象的栈上缓冲），析构试图 free 栈上地址，报的是 **invalid free**；而 `memcpy` 直接复制**堆上**大字符串的指针、两份对象析构各 free 一次，才报 **double free**——两种病对应两种报告。正确做法是走 vtable 的**类型化移动**（`do_move` 用 placement new 调 `T` 的移动构造，SSO 字符串会正确地把字符搬进新位置）。②`mini_any_cast<int&>(a)` 返回可修改引用就地改写，`mini_any_cast<int>(std::move(a))` 按值把对象搬出来——三个重载和 `std::any_cast` 一个套路。本题口径：难度按「用本卷知识可解的最难问题」标定 L5，题源为 cppreference 的 any 教学实现与教材 ch04-any、ch03-std-function 两节的机制组合。

1. `VTable` 五个函数指针 + `VTableFor<T>` 模板实例；`construct` 按 `sizeof` 选 SBO 或堆。→ 知识点：[std::any 与类型擦除](../ch04-type-safety/05-any.md)「类型擦除与 Small Buffer Optimization」一节、[std::function、std::invoke 与可调用对象](../ch03-lambda/04-std-function.md)「手动类型擦除：函数指针表替代虚函数」一节
2. 拷贝走 `do_copy`、移动走 `do_move` + 源置空；`steal` 承担「类型化搬运」，`swap` 用「临时接管 → 两次 steal」实现（**绝不能字节对调**）。→ 知识点：[移动构造与移动赋值](../ch00-move-semantics/02-move-semantics.md)（资源转移 + 源对象置空）、[RAII 深入理解：资源管理的基石](../ch01-smart-pointers/01-raii-deep-dive.md)（析构必经）
3. `mini_any_cast` 三个重载（const 返回 const 引用、非 const 返回可修改引用、右值按值搬出）+ 类型不符抛 `BadMiniAnyCast`；属性字典应用；ASan/UBSan 全绿。→ 知识点：[std::any 与类型擦除](../ch04-type-safety/05-any.md)「any_cast」一节

```cpp
// hw2c3.cpp -- Homework C-3 (L5): MiniAny -- type-erased container with SBO
// Standard: C++17
#include <cstddef>
#include <iostream>
#include <new>
#include <string>
#include <type_traits>
#include <typeinfo>
#include <utility>
#include <vector>

class BadMiniAnyCast : public std::bad_cast
{
public:
    const char* what() const noexcept override { return "BadMiniAnyCast"; }
};

class MiniAny
{
    static constexpr std::size_t kBufSize = 32;
    static constexpr std::size_t kBufAlign = alignof(std::max_align_t);

    using CopyFn = void (*)(void* dst, const void* src);
    using MoveFn = void (*)(void* dst, void* src);
    using DestroyFn = void (*)(void* obj);
    using TypeFn = const std::type_info& (*)();

    struct VTable
    {
        CopyFn copy;
        MoveFn move;
        DestroyFn destroy;
        TypeFn type;
        std::size_t size;
    };

    template <typename T>
    struct VTableFor
    {
        static void do_copy(void* dst, const void* src)
        {
            new (dst) T(*static_cast<const T*>(src));
        }
        static void do_move(void* dst, void* src)
        {
            new (dst) T(std::move(*static_cast<T*>(src)));
        }
        static void do_destroy(void* obj)
        {
            static_cast<T*>(obj)->~T();
        }
        static const std::type_info& do_type()
        {
            return typeid(T);
        }
        static constexpr VTable value{do_copy, do_move, do_destroy, do_type, sizeof(T)};
    };

    alignas(kBufAlign) unsigned char storage_[kBufSize];
    void* heap_ = nullptr;
    const VTable* vtable_ = nullptr;

    void* ptr()
    {
        return heap_ != nullptr ? heap_ : static_cast<void*>(storage_);
    }
    const void* cptr() const
    {
        return heap_ != nullptr ? heap_ : static_cast<const void*>(storage_);
    }

    template <typename U>
    void construct(U&& value)
    {
        using D = std::decay_t<U>;
        static_assert(std::is_copy_constructible_v<D>,
                      "MiniAny requires a copy-constructible type");
        if (sizeof(D) > kBufSize) {
            heap_ = ::operator new(sizeof(D));
            new (heap_) D(std::forward<U>(value));
        } else {
            new (storage_) D(std::forward<U>(value));
        }
        vtable_ = &VTableFor<D>::value;
    }

    void destroy()
    {
        if (vtable_ != nullptr) {
            vtable_->destroy(ptr());
            ::operator delete(heap_);
            heap_ = nullptr;
            vtable_ = nullptr;
        }
    }

    // Typed move: steal other content into this (this must be empty).
    void steal(MiniAny&& other) noexcept
    {
        vtable_ = other.vtable_;
        if (vtable_ == nullptr) {
            return;
        }
        if (vtable_->size > kBufSize) {
            heap_ = other.heap_;
            other.heap_ = nullptr;
        } else {
            vtable_->move(storage_, other.storage_);
        }
        other.vtable_ = nullptr;
    }

    template <typename T>
    friend const T& mini_any_cast(const MiniAny& a);
    template <typename T>
    friend T& mini_any_cast(MiniAny& a);
    template <typename T>
    friend T mini_any_cast(MiniAny&& a);

public:
    MiniAny() = default;

    template <typename T, typename U = std::decay_t<T>,
              typename = std::enable_if_t<!std::is_same_v<U, MiniAny>>>
    MiniAny(T&& value)
    {
        construct(std::forward<T>(value));
    }

    MiniAny(const MiniAny& other) : vtable_(other.vtable_)
    {
        if (vtable_ == nullptr) {
            return;
        }
        if (vtable_->size > kBufSize) {
            heap_ = ::operator new(vtable_->size);
            vtable_->copy(heap_, other.cptr());
        } else {
            vtable_->copy(storage_, other.cptr());
        }
    }

    MiniAny(MiniAny&& other) noexcept
    {
        steal(std::move(other));
    }

    MiniAny& operator=(MiniAny&& other) noexcept
    {
        if (this != &other) {
            destroy();
            steal(std::move(other));
        }
        return *this;
    }

    MiniAny& operator=(const MiniAny& other)
    {
        if (this != &other) {
            MiniAny tmp(other);
            *this = std::move(tmp);
        }
        return *this;
    }

    ~MiniAny()
    {
        destroy();
    }

    friend void swap(MiniAny& a, MiniAny& b) noexcept
    {
        MiniAny tmp(std::move(a));
        a = std::move(b);
        b = std::move(tmp);
    }

    bool has_value() const noexcept { return vtable_ != nullptr; }
    const std::type_info& type() const noexcept
    {
        return vtable_ != nullptr ? vtable_->type() : typeid(void);
    }
};

// const view: read-only reference into a const MiniAny
template <typename T>
const T& mini_any_cast(const MiniAny& a)
{
    if (!a.has_value() || a.type() != typeid(std::remove_reference_t<T>)) {
        throw BadMiniAnyCast{};
    }
    return *static_cast<const std::remove_reference_t<T>*>(a.cptr());
}

// mutable view: reference that can modify the held value in place
template <typename T>
T& mini_any_cast(MiniAny& a)
{
    if (!a.has_value() || a.type() != typeid(std::remove_reference_t<T>)) {
        throw BadMiniAnyCast{};
    }
    return *static_cast<std::remove_reference_t<T>*>(a.ptr());
}

// by-value overload: move the held value out of an rvalue MiniAny
template <typename T>
T mini_any_cast(MiniAny&& a)
{
    if (!a.has_value() || a.type() != typeid(T)) {
        throw BadMiniAnyCast{};
    }
    return std::move(*static_cast<T*>(a.ptr()));
}

int main()
{
    std::cout << "sizeof(MiniAny) = " << sizeof(MiniAny) << "\n";

    MiniAny a = 42;
    std::cout << "int:    " << mini_any_cast<int>(a) << "\n";
    a = 3.14;
    std::cout << "double: " << mini_any_cast<double>(a) << "\n";
    a = std::string("hello");
    std::cout << "string: " << mini_any_cast<std::string>(a) << "\n";

    try {
        mini_any_cast<int>(a);
    } catch (const BadMiniAnyCast& e) {
        std::cout << "caught: " << e.what() << "\n";
    }

    a = std::string(2000, 'x');   // exceeds SBO -> heap path
    std::cout << "big string size = " << mini_any_cast<std::string>(a).size() << "\n";

    MiniAny b = a;                // copy independence
    mini_any_cast<std::string>(b)[0] = 'y';
    std::cout << "b[0] = " << mini_any_cast<std::string>(b)[0]
              << ", a[0] = " << mini_any_cast<std::string>(a)[0] << "\n";

    // attribute dictionary
    std::vector<std::pair<std::string, MiniAny>> attrs;
    attrs.emplace_back("health", MiniAny{100});
    attrs.emplace_back("name", MiniAny{std::string("player")});
    attrs.emplace_back("speed", MiniAny{1.5});
    for (const auto& [k, v] : attrs) {
        if (v.type() == typeid(int)) {
            std::cout << k << " = " << mini_any_cast<int>(v) << " (int)\n";
        } else if (v.type() == typeid(double)) {
            std::cout << k << " = " << mini_any_cast<double>(v) << " (double)\n";
        } else if (v.type() == typeid(std::string)) {
            std::cout << k << " = " << mini_any_cast<std::string>(v) << " (string)\n";
        }
    }

    // cast overload semantics: T& mutates in place,
    // the by-value overload moves the held value out of an rvalue MiniAny
    MiniAny box = 7;
    mini_any_cast<int&>(box) = 99;
    std::cout << "after int& write: " << mini_any_cast<int>(box) << "\n";
    int taken = mini_any_cast<int>(std::move(box));
    std::cout << "moved out by value: " << taken << "\n";
    return 0;
}
```

**验证输出**：

```text
$ g++ -std=c++17 -Wall -Wextra -O2 -o hw2c3 hw2c3.cpp && ./hw2c3
sizeof(MiniAny) = 48
int:    42
double: 3.14
string: hello
caught: BadMiniAnyCast
big string size = 2000
b[0] = y, a[0] = x
health = 100 (int)
name = player (string)
speed = 1.5 (double)
after int& write: 99
moved out by value: 99

$ g++ -std=c++17 -Wall -Wextra -g -fsanitize=address,undefined -o hw2c3_asan hw2c3.cpp
$ ./hw2c3_asan
（输出与上面完全一致，无任何 sanitizer 报告）
```

暗雷①实测（两个坏版本各跑一次，真实 ASan 报告节选；地址/进程号每次运行不同）：

**场景一：字节级交换两块 SBO 缓冲**（`hw2c3_badswap.cpp`，`a`/`b` 各装一个短字符串，`for (...) std::swap(a.storage_[i], b.storage_[i])` 后析构——字符串内部指针还指向**另一侧的栈上缓冲**；测试版把 MiniAny 类抽到共用头 `miniany_common.hpp`，栈帧里的 `miniany_common.hpp:48` 即 `do_destroy`）：

```text
$ g++ -std=c++17 -Wall -Wextra -g -fsanitize=address -o hw2c3_badswap hw2c3_badswap.cpp
$ ./hw2c3_badswap
before byte swap: a=hello, b=world
after byte swap:  a=hello, b=world
=================================================================
==2337==ERROR: AddressSanitizer: attempting free on address which was not malloc()-ed: 0x7a64f8ff00d0 in thread T0
    ...
    #5 ... in std::__cxx11::basic_string<...>::~basic_string()
    #6 ... in MiniAny::VTableFor<...>::do_destroy(void*) miniany_common.hpp:48
    ...
Address 0x7a64f8ff00d0 is located in stack of thread T0 at offset 208 in frame
    #0 ... in main hw2c3_badswap.cpp:5
    [192, 240) 'a' (line 7) <== Memory access at offset 208 is inside this variable
SUMMARY: AddressSanitizer: bad-free ... in MiniAny::VTableFor<...>::do_destroy(void*)
==2337==ABORTING
```

**场景二：`memcpy` 整对象复制堆字符串**（`hw2c3_badmemcpy.cpp`，`a` 装 2000 字节大字符串走堆，`memcpy(&b, &a, sizeof(MiniAny))` 后析构——两份对象共享**同一个堆指针**）：

```text
$ g++ -std=c++17 -Wall -Wextra -g -fsanitize=address -o hw2c3_badmemcpy hw2c3_badmemcpy.cpp
$ ./hw2c3_badmemcpy
a.size=2000, b.size=2000
=================================================================
==2343==ERROR: AddressSanitizer: attempting double-free on 0x77e4995e0080 in thread T0:
    ...
0x77e4995e0080 is located 0 bytes inside of 2001-byte region [0x77e4995e0080,0x77e4995e0851)
freed by thread T0 here:
    ...
SUMMARY: AddressSanitizer: double-free ... in MiniAny::VTableFor<...>::do_destroy(void*)
==2343==ABORTING
```

短字符串字节交换后，字符串内部指针指向**另一侧对象的栈上缓冲**，析构 free 栈上地址 → invalid free（场景一）；`memcpy` 复制的是**堆上**字符串的指针，两份对象析构各 free 一次 → double free（场景二）。两种病对应两种报告。

要点：48 字节 = 32（SBO）+ 8（堆指针）+ 8（vtable 指针）。`hello` 这类短字符串住在 SBO 里、大字符串走堆；`b[0]='y'` 改不动 `a`——深拷贝独立。字节级交换的错误版本里，短字符串的**内部数据指针还指向旧位置**（另一侧的栈上缓冲），析构 free 栈上地址报 invalid free；`memcpy` 整对象复制堆字符串才是 double free——两种病两种报告，这正是「搬对象要走类型化移动」的原因。`mini_any_cast<int&>(a)` 返回可修改引用就地改写、`mini_any_cast<int>(std::move(a))` 按值把对象搬出来，和 `std::any_cast` 的重载一个套路。
