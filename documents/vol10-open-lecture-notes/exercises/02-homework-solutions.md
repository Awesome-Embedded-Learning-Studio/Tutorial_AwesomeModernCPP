---
title: "卷 10 课后练习参考答案（Homework）"
description: "卷 10 课后练习的逐题详细解答：每道题给出解题思路、逐步解答（每步标注知识点链接）与真实验证输出（WSL Arch g++ 16.1.1 / clang++ 22.1.8 实跑；C++26 题以 g++ 16 -std=c++26 实测）。含 UB 题的 UBSan 实跑记录。"
chapter: 10
order: 2
tags: [host, advanced, cpp-modern, concepts, Ranges, optional, 移动语义, 内存安全]
difficulty: advanced
platform: host
cpp_standard: [17, 20, 23, 26]
reading_time_minutes: 45
prerequisites: []
related: []
---

# 卷 10 课后练习参考答案（Homework）

> 所有命令与输出在 WSL Arch（g++ 16.1.1，clang++ 22.1.8 交叉验证）下真实运行得到。计时类数字因机器而异，重点是数量级和结论；UB 类题目的输出「只是这台机器这次的选择」，换编译器/优化级别可能不同——这正是每道题要你体会的东西。C++26 特性的验证环境详见每道题的编译命令。

## 10.1-A {#hw-10-1-a}

**难度 L1** · 题面见 [01-homework](./01-homework.md#hw-10-1-a)

**思路**：concept 是编译期求值的布尔谓词；三条分支按 `||` 合取，只要有一条成立就判窄化——注意 `bool ← int` 与 `char ← unsigned char` 在本机是**两条分支同时成立**（下面用探针跑给你看），但分支重叠不影响结论。

```cpp
#include <concepts>
#include <cstdio>
#include <limits>
#include <type_traits>

template<typename T>
concept number = std::integral<T> || std::floating_point<T>;

template<typename T, typename U>
concept smaller_range =
    number<T> && number<U> &&
    (std::numeric_limits<T>::max() < std::numeric_limits<U>::max() ||
     std::numeric_limits<T>::min() > std::numeric_limits<U>::min());

// 核心判断:从 U 到 T 的赋值是否会发生窄化
// 注意:&& 与 || 混用时,三个分支必须被括号包成一整体,
// 否则 number<T> && number<U> 只约束第一个分支(优先级问题)
template<typename T, typename U>
concept narrowing_assign =
    number<T> && number<U> &&
    (
        smaller_range<T, U> ||
        (std::floating_point<U> && std::integral<T>) ||
        (std::integral<T> && std::integral<U> &&
         std::signed_integral<U> != std::signed_integral<T>)
    );

// 题面要求:前五个用例用 static_assert 全部通过,
// 最后一个(平台相关)用运行期 printf 判定
static_assert(narrowing_assign<bool, int>, "int -> bool 窄化");
static_assert(narrowing_assign<float, double>, "double -> float 窄化(精度)");
static_assert(narrowing_assign<int, double>, "double -> int 窄化(丢小数)");
static_assert(!narrowing_assign<double, float>, "float -> double 不窄化");
static_assert(!narrowing_assign<long long, int>, "int -> long long 不窄化");

// 三个子分支的归属探针:看每个用例到底走哪条(哪几条)分支
template<typename T, typename U>
void probe(const char* name)
{
    const bool b1 = smaller_range<T, U>;
    const bool b2 = std::floating_point<U> && std::integral<T>;
    const bool b3 = std::integral<T> && std::integral<U> &&
                    (std::signed_integral<U> != std::signed_integral<T>);
    std::printf("%-28s 范围更小=%d 浮点转整数=%d 有符号性不同=%d  =>  %d\n",
                name, (int)b1, (int)b2, (int)b3, (int)(b1 || b2 || b3));
}

int main()
{
    std::printf("分支归属(前三个子分支各自的值,最右是合取结果):\n");
    probe<bool, int>("bool <- int");
    probe<float, double>("float <- double");
    probe<int, double>("int <- double");
    probe<double, float>("double <- float");
    probe<long long, int>("long long <- int");
    probe<char, unsigned char>("char <- unsigned char");
    std::printf("\n");
    std::printf("narrowing_assign<bool, int>          = %d\n", narrowing_assign<bool, int>);
    std::printf("narrowing_assign<float, double>      = %d\n", narrowing_assign<float, double>);
    std::printf("narrowing_assign<int, double>        = %d\n", narrowing_assign<int, double>);
    std::printf("narrowing_assign<double, float>      = %d\n", narrowing_assign<double, float>);
    std::printf("narrowing_assign<long long, int>     = %d\n", narrowing_assign<long long, int>);
    std::printf("narrowing_assign<char,unsigned char> = %d (本机 char 是 signed)\n",
                narrowing_assign<char, unsigned char>);
    std::printf("static_assert x5: 全部通过\n");
    return 0;
}
```

1. `bool ← int` 在本机**两条分支同时成立**：`smaller_range`（`bool` 的 `max()` 是 `true` 即 1，`1 < int 的 max` 恒真）**和**「有符号性不同」（`int` 是 signed、`bool` 是 unsigned，`signed_integral` 不同）；`int ← double` 同样是两条（`smaller_range` + 「浮点转整数」）。分支重叠不影响结论，`||` 合取后仍是 1。`float ← double` 只走 `smaller_range` 一条（`float::max < double::max`），不是「浮点转整数」分支——精度损失也属于窄化。→ 知识点：[类型安全、Number 约束与边界检查](../cppcon/2025/01-concept-based-generic-programming/01-type-safety-and-number-concept.md)「动手写一个 narrowing 判断」一节
2. `char ← unsigned char` 在本机判为 1，且同样是**两条分支同时成立**：`smaller_range`（本机 `char` 是 signed，$127 < 255$）**和**「有符号性不同」（-1 赋过去会变 255，拦得对）。换成 `char` 是 unsigned 的平台（如部分 ARM 平台），这两条分支会一起翻转成 0——这就是「平台相关」的含义，也是**最后一例不能写成 static_assert、必须运行期打印**的原因：写死 `static_assert(narrowing_assign<char, unsigned char>)` 在另一类平台上会编译失败。→ 知识点：同上「还有一些边界情况要想清楚」一节（`char` 的有符号性是实现定义的）
3. 括号问题：`&&` 优先级高于 `||`，不加括号时 `number<T> && number<U>` 只约束第一个 `||` 分支，后两个分支会对非数字类型求值——语义就错了。加括号把三个分支合成一个整体，再由外层 `number<T> && number<U>` 统一约束。→ 知识点：同上

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra narrow.cpp -o narrow && ./narrow
分支归属(前三个子分支各自的值,最右是合取结果):
bool <- int                  范围更小=1 浮点转整数=0 有符号性不同=1  =>  1
float <- double              范围更小=1 浮点转整数=0 有符号性不同=0  =>  1
int <- double                范围更小=1 浮点转整数=1 有符号性不同=0  =>  1
double <- float              范围更小=0 浮点转整数=0 有符号性不同=0  =>  0
long long <- int             范围更小=0 浮点转整数=0 有符号性不同=0  =>  0
char <- unsigned char        范围更小=1 浮点转整数=0 有符号性不同=1  =>  1

narrowing_assign<bool, int>          = 1
narrowing_assign<float, double>      = 1
narrowing_assign<int, double>        = 1
narrowing_assign<double, float>      = 0
narrowing_assign<long long, int>     = 0
narrowing_assign<char,unsigned char> = 1 (本机 char 是 signed)
static_assert x5: 全部通过
```

## 10.1-B {#hw-10-1-b}

**难度 L4** · 题面见 [01-homework](./01-homework.md#hw-10-1-b)

**思路**：`narrow_convert` 只检查「赋值瞬间的值还装不装得下」；而 `unsigned + unsigned` 的回绕发生在**进入构造函数之前**——`value_ + other.get()` 先按 `unsigned int` 规则回绕成一个合法值，构造函数看到的已经是「干净的」705032704 了。

```cpp
#include <iostream>
#include <limits>
#include <stdexcept>
#include <type_traits>

// 复刻自讲座 01 的 would_narrow / narrow_convert,按验证需要调整
template<typename T, typename U>
constexpr bool would_narrow(U u) noexcept
{
    if constexpr (std::is_same_v<T, U>) {
        return false;
    }
    // signed -> unsigned 且源值为负:一定是窄化
    // 不能只靠 round-trip:补码下 int(-1)->unsigned->int(-1) 可逆
    if constexpr (std::is_unsigned_v<T> && std::is_signed_v<U>) {
        if (u < 0) return true;
    }
    T t = static_cast<T>(u);
    if (static_cast<U>(t) != u) {
        return true;
    }
    if constexpr (std::is_floating_point_v<U> && std::is_integral_v<T>) {
        if (u != static_cast<U>(static_cast<long long>(u))) {
            return true;
        }
    }
    return false;
}

template<typename T, typename U>
constexpr T narrow_convert(U u)
{
    if (would_narrow<T>(u)) {
        throw std::invalid_argument("narrowing conversion detected");
    }
    return static_cast<T>(u);
}

template<typename T>
class Number
{
    T value_;

public:
    template<typename U>
    constexpr Number(U u) : value_(narrow_convert<T>(u)) {}
    constexpr Number(T t) : value_(t) {}
    constexpr operator T() const noexcept { return value_; }
    constexpr T get() const noexcept { return value_; }

    template<typename U>
    constexpr auto operator+(const Number<U>& other) const
        -> Number<std::common_type_t<T, U>>
    {
        using R = std::common_type_t<T, U>;
        return Number<R>(value_ + other.get());
    }
};

// 讲座给出的修复:窄化检测管不了同类型算术回绕,溢出要单独查
template<typename T>
constexpr T safe_add(T a, T b)
{
    if constexpr (std::is_unsigned_v<T>) {
        if (a > std::numeric_limits<T>::max() - b) {
            throw std::overflow_error("unsigned addition overflow");
        }
    } else {
        T result{};
        if (__builtin_add_overflow(a, b, &result)) {
            throw std::overflow_error("signed addition overflow");
        }
        return result;
    }
    return a + b;
}

int main()
{
    Number<unsigned int> x = 3000000000u;
    Number<unsigned int> y = 2000000000u;
    auto s = x + y;
    std::cout << "x + y = " << s << " (Number 没拦:unsigned 回绕是 well-defined)\n";

    try {
        auto ok = safe_add(3000000000u, 2000000000u);
        std::cout << "safe_add unsigned = " << ok << "\n";
    } catch (const std::exception& e) {
        std::cout << "safe_add(unsigned) 捕获: " << e.what() << "\n";
    }

    try {
        auto ok = safe_add(2147483647, 1);
        std::cout << "safe_add signed = " << ok << "\n";
    } catch (const std::exception& e) {
        std::cout << "safe_add(signed) 捕获: " << e.what() << "\n";
    }

    try {
        Number<char> c = 300;
        std::cout << "c = " << static_cast<int>(c.get()) << "\n";
    } catch (const std::invalid_argument& e) {
        std::cout << "Number<char>(300) 捕获: " << e.what() << "\n";
    }

    Number<double> d = 3.0;
    std::cout << "Number<double> d = " << d.get() << "\n";
    return 0;
}
```

1. $x + y$ 返回 705032704、不抛异常：`common_type_t<unsigned, unsigned>` 就是 `unsigned`，`value_ + other.get()` 在构造 `Number<unsigned>` 之前就已经按无符号规则回绕（3000000000 + 2000000000 mod 2³² = 705032704），构造函数拿到的同类型值当然是「不窄化」的。→ 知识点：[类型安全、Number 约束与边界检查](../cppcon/2025/01-concept-based-generic-programming/01-type-safety-and-number-concept.md)「原文错误更正：unsigned 算术溢出不会被 narrow_convert 检测到」一节（含 `would_narrow` 对同类型直接返回 false 的机制）
2. `safe_add` 的 unsigned 分支把加法换成减法检查（`a > max - b`），signed 分支靠 `__builtin_add_overflow`（signed 溢出是 UB，不能用回绕后比大小来查）。两个场景都被拦。→ 知识点：同上（编译器内置函数是 signed 溢出唯一干净的查法）
3. 职责边界：`narrow_convert` 管的是「类型转换」，不管「算术运算」。让类型系统管赋值、让算术层管溢出，各司其职——这也是讲座里「让真正应该负责处理的类处理对应的错误」的思路。→ 知识点：同上「用 safe_int 给 span 加上真正的保护」一节

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra numcheck.cpp -o numcheck && ./numcheck
x + y = 705032704 (Number 没拦:unsigned 回绕是 well-defined)
safe_add(unsigned) 捕获: unsigned addition overflow
safe_add(signed) 捕获: signed addition overflow
Number<char>(300) 捕获: narrowing conversion detected
Number<double> d = 3
```

705032704 与讲座「原文错误更正」小节记录的实测值一致。

## 10.2-A {#hw-10-2-a}

**难度 L2** · 题面见 [01-homework](./01-homework.md#hw-10-2-a)

**思路**：System V AMD64 ABI 前六个整数参数依次走 `rdi/rsi/rdx/rcx/r8/r9`，第七个上栈；返回值走 `rax`。`imul` 的两操作数形式把结果写回第一个操作数，所以还得补一条 `mov` 送进 `eax`。

**验证输出**（`g++ -std=c++20 -O2 -S abi.cpp`，去掉 `.cfi_*` 噪音）：

```asm
_Z6squarei:
    imull   %edi, %edi
    movl    %edi, %eax
    ret
_Z9add_threelll:
    addq    %rsi, %rdi
    leaq    (%rdi,%rdx), %rax
    ret
_Z9sum_sevenlllllll:
    addq    %rsi, %rdi
    addq    %rdx, %rdi
    addq    %rcx, %rdi
    addq    %r8, %rdi
    leaq    (%rdi,%r9), %rax
    addq    8(%rsp), %rax
    ret
main:
    movl    $1156, %eax
    ret
```

1. `square`：`imull %edi, %edi` 是两操作数形式（目标即源），结果落在 `edi`；ABI 要求返回值在 `eax`，所以那条 `movl` 省不掉。x86-64 的第一个整数参数在 `rdi`，`int` 只用它的低 32 位 `edi`。→ 知识点：[阅读汇编与寄存器 ABI](../cppcon/2025/02-some-assembly-required/02-reading-assembly-and-registers-abi.md)「x86-64 的版本」一节
2. `add_three`：`a` 在 `rdi`、`b` 在 `rsi`、`c` 在 `rdx`，结果 `rax`。→ 知识点：[阅读汇编与寄存器 ABI](../cppcon/2025/02-some-assembly-required/02-reading-assembly-and-registers-abi.md)「函数参数到底在哪个寄存器里」一节
3. `sum_seven`：前六个在 `rdi/rsi/rdx/rcx/r8/r9`，第七个 `g` 从 `8(%rsp)` 读——`call` 指令把返回地址压进 `[rsp]`，所以第一个栈上参数偏移是 8。→ 知识点：同上「动手验证一下」一节（七参数版本）
4. `main` 只剩一条 `movl $1156, %eax`：28 + 6 = 34，34² = 1156，整个计算在编译期折叠成常量——这就是「看优化」要开 `-O2` 的意义，`-O0` 下你会看到一堆栈搬运。→ 知识点：[阅读汇编与寄存器 ABI](../cppcon/2025/02-some-assembly-required/02-reading-assembly-and-registers-abi.md)「优化等级会彻底改变你看到的东西」一节

## 10.2-B {#hw-10-2-b}

**难度 L3** · 题面见 [01-homework](./01-homework.md#hw-10-2-b)

**思路**：`'A'..'F'` 是 65..70、`'a'..'f'` 是 97..102，全部 ≥ 64。一个 64 位整数只装得下 ASCII 0..63，单表方案里 `1ULL << 'A'` 就是「移位量 ≥ 位宽」的 UB——在 constexpr 求值里 UB 直接升级为编译错误，这是好事：它把一颗运行期炸弹提前引爆在编译期。

**单表方案的编译报错（真实输出）**：

```text
$ g++ -std=c++20 -Wall -Wextra -O2 hexlook_single.cpp
hexlook.cpp:12:46:   in 'constexpr' expansion of 'make_hex_table()'
   12 | constexpr uint64_t kHexTable = make_hex_table();
      |                                ~~~~~~~~~~~~~~^~
hexlook.cpp:8:60: error: right operand of shift expression '(1 << 65)' is greater than
or equal to the precision 64 of the left operand [-fpermissive]
    8 |     for (int c = 'A'; c <= 'F'; ++c) table |= (uint64_t{1} << c);
      |                                               ~~~~~~~~~~~~~^~~~~
```

**双表方案**：

```cpp
#include <cstdint>
#include <cstdio>

// 一个 64 位整数只够覆盖 ASCII 0..63。
// 数字 '0'-'9' 在 48..57,能放进低位表;
// 大写 'A'-'F' 在 65..70、小写 'a'-'f' 在 97..102,必须放进第二张表。
constexpr uint64_t make_hex_lo()
{
    uint64_t table = 0;
    for (int c = '0'; c <= '9'; ++c) table |= (uint64_t{1} << c);
    return table;
}
constexpr uint64_t make_hex_hi()
{
    uint64_t table = 0;
    for (int c = 'A'; c <= 'F'; ++c) table |= (uint64_t{1} << (c - 64));
    for (int c = 'a'; c <= 'f'; ++c) table |= (uint64_t{1} << (c - 64));
    return table;
}
constexpr uint64_t kHexLo = make_hex_lo();
constexpr uint64_t kHexHi = make_hex_hi();

bool is_hex_bitlookup(char c)
{
    unsigned char uc = static_cast<unsigned char>(c);  // 负数 char 不惹事
    if (uc >= 64) {
        return (kHexHi >> (uc - 64)) & 1;
    }
    return (kHexLo >> uc) & 1;
}

bool is_hex_naive(char c)
{
    return (c >= '0' && c <= '9') ||
           (c >= 'A' && c <= 'F') ||
           (c >= 'a' && c <= 'f');
}

int main()
{
    bool all_match = true;
    for (int i = 0; i < 128; ++i) {
        char c = static_cast<char>(i);
        if (is_hex_bitlookup(c) != is_hex_naive(c)) {
            std::printf("Mismatch at ASCII %d: bitlookup=%d naive=%d\n",
                        i, is_hex_bitlookup(c), is_hex_naive(c));
            all_match = false;
        }
    }
    std::printf("ASCII 0..127 全对: %s\n", all_match ? "yes" : "no");
    std::printf("'0'=%d '9'=%d 'A'=%d 'F'=%d 'a'=%d 'f'=%d 'g'=%d ' '=%d\n",
                is_hex_bitlookup('0'), is_hex_bitlookup('9'),
                is_hex_bitlookup('A'), is_hex_bitlookup('F'),
                is_hex_bitlookup('a'), is_hex_bitlookup('f'),
                is_hex_bitlookup('g'), is_hex_bitlookup(' '));
    std::printf("kHexLo=0x%llX kHexHi=0x%llX\n",
                (unsigned long long)kHexLo, (unsigned long long)kHexHi);
    return 0;
}
```

1. 报错来自 constexpr 求值阶段：`1 << 65` 在编译期就是 UB，constexpr 语境把它变成硬错误。这就是讲座反复强调的「位移量 ≥ 位宽是 UB」的直接证据。→ 知识点：[Compiler Explorer 深度使用与 AI 辅助](../cppcon/2025/02-some-assembly-required/03-compiler-explorer-and-ai-assisted.md)「位查找表技巧的原理」一节（位移量越界是 UB、x86 掩码行为不可依赖）
2. 双表方案把 `'A'-'F'`、`'a'-'f'` 挪进高位表（$c - 64$ 归位到 1..6 和 33..38），低位表只管数字。这与讲座「一个 64 位整数覆盖 ASCII 0-63，两个覆盖到 127」的提示一致。→ 知识点：同上
3. 先转 `unsigned char` 再移位：`char` 可能为负（某些扩展字符），负数参与移位实现定义；转成 `unsigned char` 后取值范围固定 0..255，两条分支的移位量都安全。→ 知识点：同上（「务必先转换为 unsigned char，这是 C++ 核心指南中也提到的要点」）

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra -O2 hexlook.cpp -o hexlook && ./hexlook
ASCII 0..127 全对: yes
'0'=1 '9'=1 'A'=1 'F'=1 'a'=1 'f'=1 'g'=0 ' '=0
kHexLo=0x3FF000000000000 kHexHi=0x7E0000007E
```

`kHexHi = 0x7E0000007E` 展开成二进制正好是 bit 1..6（大写）与 bit 33..38（小写）置位。

## 10.3-A {#hw-10-3-a}

**难度 L1** · 题面见 [01-homework](./01-homework.md#hw-10-3-a)

**思路**：旧 tag 体系里压根没有「连续」这个档位（`contiguous_iterator_tag` 是 C++20 才补的），所以 `array`/`deque`/裸指针的 legacy tag 全都笼统标成 `random_access`；concept 体系是正交约束，能把「随机可访问」和「物理上连续」拆成两件独立的事。

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra -O2 itercat.cpp -o itercat && ./itercat
std::array<int,5>            legacy=random_access  cpp20=contiguous_iterator
std::deque<int>              legacy=random_access  cpp20=random_access_iterator
std::forward_list<int>       legacy=forward        cpp20=forward_iterator
std::map<int,int>            legacy=bidirectional  cpp20=bidirectional_iterator
int* (raw pointer)           legacy=random_access  cpp20=contiguous_iterator
static_assert checks: PASS
```

1. `deque` 两套体系都说「随机访问」但没有「连续」：它内部是一段一段的分块存储，`it + n` 可以，把迭代器当连续内存喂给 `memcpy` 不行。→ 知识点：[从循环到迭代器：遍历数据的抽象之路](../cppcon/2025/03-back-to-basics-ranges/01-from-loops-to-iterators.md)「迭代器类别体系」一节（deque 分块存储的例子）
2. `array`/裸指针被 concept 标成 `contiguous_iterator`：「连续」意味着你可以安全地把底层数据当成一块连续内存交给 C 接口（`memcpy`、CUDA kernel、SIMD 指令）——这个性质旧 tag 表达不出来。→ 知识点：同上（concepts 比 tag 强的地方、为什么 Ranges 必须等 C++20）

## 10.3-B {#hw-10-3-b}

**难度 L3** · 题面见 [01-homework](./01-homework.md#hw-10-3-b)

**思路**：惰性管道是「一个元素贯穿到底」的执行模型；`take(5)` 让遍历在凑满 5 个元素后立刻停，谓词只被调用几十次。总次数由三部分构成：`begin()` 定位首元素、5 次 `++` 各扫一批、最后一次 `++` 发现计数耗尽。

**关键源码**（`pred_calls` 是文件级计数器，谓词函数每次被调用自增一次；拆解实验用「先 `begin()`、计数器清零、再逐步 `++`」的方式把各阶段分开计数）：

```cpp
static long pred_calls = 0;

bool pred7(int x)
{
    ++pred_calls;
    return x % 7 == 0;
}

int main()
{
    const int N = 10000000;
    std::vector<int> v(N);
    for (int i = 0; i < N; ++i)
    {
        v[i] = i;
    }

    // 短路计数:全量 filter vs filter+take(5)
    pred_calls = 0;
    long full_sum = 0;
    for (int x : v | std::views::filter(pred7))
    {
        full_sum += x;
    }
    const long full_calls = pred_calls;

    pred_calls = 0;
    long take_sum = 0;
    for (int x : v | std::views::filter(pred7) | std::views::take(5))
    {
        take_sum += x;
    }
    const long take_calls = pred_calls;

    std::printf("短路实验: 全量谓词调用=%ld (和=%ld)\n", full_calls, full_sum);
    std::printf("加 take(5) 谓词调用=%ld (和=%ld)\n", take_calls, take_sum);

    // 拆解 take(5):begin()/end()/每次 ++ 各消耗几次谓词调用
    auto tv = v | std::views::filter(pred7) | std::views::take(5);

    pred_calls = 0;
    auto it = tv.begin();
    std::printf("  单独 begin(): %ld 次调用\n", pred_calls);

    pred_calls = 0;
    auto it_end = tv.end();
    (void)it_end;
    std::printf("  单独 end():   %ld 次调用\n", pred_calls);

    // 迭代阶段独立计数:begin() 之后清零,下面各行是"这一轮 ++ 的累计值"
    it = tv.begin();
    pred_calls = 0;
    std::printf("  迭代取值 %d 时累计 %ld 次\n", *it, pred_calls);
    for (int k = 0; k < 4; ++k)
    {
        ++it;
        std::printf("  迭代取值 %d 时累计 %ld 次\n", *it, pred_calls);
    }
    ++it;
    std::printf("  最后一次 ++ 后累计 %ld 次\n", pred_calls);
}
```

**验证输出**：

```text
$ g++ -std=c++23 -Wall -Wextra -O2 lazybench.cpp -o lazybench && ./lazybench
短路实验: 全量谓词调用=10000000 (和=7142857857142)
加 take(5) 谓词调用=36 (和=70)
  单独 begin(): 1 次调用
  单独 end():   0 次调用
  迭代取值 0 时累计 0 次
  迭代取值 7 时累计 7 次
  迭代取值 14 时累计 14 次
  迭代取值 21 时累计 21 次
  迭代取值 28 时累计 28 次
  最后一次 ++ 后累计 35 次
sum eager=37499992500000 lazy=37499992500000
eager (ranges::to + 求和): 34 ms
lazy  (直接遍历 view):    16 ms
```

拆解输出的**口径**：`begin()`/`end()` 两行是各自单独跑一遍的计数；「迭代取值」各行来自同一轮迭代，但计数器在 `begin()` 之后**清零**——所以那些「累计」是**迭代阶段的独立计数**（不含 `begin()` 那次），行与行之间是累计值、不是单步增量，不能把各行数字直接相加。

1. 36 的构成：`begin()` 用 find_if 定位第一个 7 的倍数（0），1 次调用；迭代 5 次，每次 `++` 要扫过 6 个非倍数再命中 1 个（7/14/21/28），共 4 × 7 = 28 次；取到第 5 个元素后最后一次 `++` 又扫了 29..35（7 次）才发现 take 计数耗尽。1 + 28 + 7 = 36，与上面「迭代取值」行的累计轨迹（0→7→14→21→28→35）一致。→ 知识点：[Ranges、Views 与管道组合](../cppcon/2025/03-back-to-basics-ranges/03-ranges-views-and-composition.md)「管道短路：lazy 带来的效率」一节（一个元素贯穿到底的执行模型）
2. eager vs lazy：物化临时 vector 要 34ms，直接遍历 view 要 16ms，快约 2 倍——eager 多花一次遍历和一次 500 万元素的分配拷贝，结果完全一致（37499992500000）。→ 知识点：同上「实验：eager vs lazy，到底差多少」一节

## 10.4-A {#hw-10-4-a}

**难度 L2** · 题面见 [01-homework](./01-homework.md#hw-10-4-a)

**思路**：C++03 风格的 swap 是三次深拷贝（一次拷贝构造 + 两次拷贝赋值）；把每步的实参换成 `std::move(x)` 后，重载决议改选移动版本——`std::move` 本身不移动任何东西，它只是把左值铸成右值引用。

**关键源码**（`copies`/`moves` 是文件级静态计数器，`Str` 是裸指针 + 完整五件套）：

```cpp
// 简化字符串类:裸指针 + 完整五件套,静态计数器 copies/moves
class Str
{
    char* p_;
    std::size_t n_;

    static char* dup(const char* s, std::size_t n)
    {
        char* q = new char[n + 1];
        std::memcpy(q, s, n + 1);
        return q;
    }

public:
    explicit Str(const char* s = "") : p_(nullptr), n_(std::strlen(s))
    {
        p_ = dup(s, n_);
    }
    ~Str()
    {
        delete[] p_;
    }
    Str(const Str& o) : p_(nullptr), n_(o.n_)          // 拷贝构造
    {
        ++copies;
        p_ = dup(o.p_, n_);
    }
    Str(Str&& o) noexcept : p_(o.p_), n_(o.n_)         // 移动构造
    {
        ++moves;
        o.p_ = nullptr;
        o.n_ = 0;
    }
    Str& operator=(const Str& o)                       // 拷贝赋值
    {
        if (this != &o)
        {
            ++copies;
            char* q = dup(o.p_, o.n_);
            delete[] p_;
            p_ = q;
            n_ = o.n_;
        }
        return *this;
    }
    Str& operator=(Str&& o) noexcept                   // 移动赋值
    {
        if (this != &o)
        {
            ++moves;
            delete[] p_;
            p_ = o.p_;
            n_ = o.n_;
            o.p_ = nullptr;
            o.n_ = 0;
        }
        return *this;
    }
};

// C++03 风格的三行 swap
template <typename T>
void copy_swap(T& x, T& y)
{
    T temp(x);
    x = y;
    y = temp;
}

// 同样三行,每步实参加 std::move
template <typename T>
void move_swap(T& x, T& y)
{
    T temp(std::move(x));
    x = std::move(y);
    y = std::move(temp);
}
```

**验证输出**：

```text
$ g++ -std=c++20 -O2 swapcnt.cpp -o swapcnt && ./swapcnt
拷贝版 swap x 100000 次: 2 ms, copies=300000 moves=0
移动版 swap x 100000 次: 0 ms, copies=0 moves=300000
```

1. 三次拷贝 = `temp(x)` 的拷贝构造、`x = y` 的拷贝赋值、`y = temp` 的拷贝赋值；每次都是 `new + memcpy`。移动版对应三次移动：偷指针 + 置空源，零堆分配。→ 知识点：[拷贝的开销与移动的动机](../cppcon/2025/04-back-to-basics-move-semantics/01-copy-cost-and-motivation.md)「C++03 的 swap：三次深拷贝」一节
2. `std::move` 只是 `static_cast<T&&>`，它做的唯一的事是让实参在重载决议里匹配 `T&&` 版本。→ 知识点：[移动操作、std::move 与拷贝消除](../cppcon/2025/04-back-to-basics-move-semantics/03-move-ops-stdmove-and-elision.md)「std::move：C++ 中被误解最深的函数」一节
3. `temp`、`x`、`y` 都有名字、生命周期跨多条语句，编译器不敢假设它们之后不再被用——「有名字就是左值」的保守规则要求你显式授权。→ 知识点：同上「为什么需要 std::move：swap 中的命名陷阱」一节

## 10.4-B {#hw-10-4-b}

**难度 L3** · 题面见 [01-homework](./01-homework.md#hw-10-4-b)

**思路**：`vector` 扩容要提供强异常安全保证——中途抛异常必须能回滚。移动是「破坏性」的（资源已被偷走、回滚不了），所以只有 `noexcept` 的移动构造才被允许上扩容这条路径。

**关键源码**（`copies`/`moves` 是文件级静态计数器，移动构造的 `noexcept` 用宏开关控制）：

```cpp
#ifdef NOEXCEPT
#define MOVE_NOEXCEPT noexcept
#else
#define MOVE_NOEXCEPT
#endif

class Holder
{
    char* buf_;
    char tag_;

public:
    explicit Holder(char tag) : buf_(new char[8]), tag_(tag)
    {
        for (int i = 0; i < 3; ++i)
        {
            buf_[i] = tag;
        }
        buf_[3] = '\0';
    }
    ~Holder()
    {
        delete[] buf_;
    }
    Holder(const Holder& o) : buf_(new char[8]), tag_(o.tag_)
    {
        ++copies;
        std::memcpy(buf_, o.buf_, 4);
    }
    Holder(Holder&& o) MOVE_NOEXCEPT : buf_(o.buf_), tag_(o.tag_)
    {
        ++moves;
        o.buf_ = nullptr;
    }
    const char* text() const
    {
        return buf_;
    }
};

int main()
{
    std::vector<Holder> v;
    v.reserve(4);
    v.emplace_back('A');
    v.emplace_back('B');
    v.emplace_back('C');
    v.emplace_back('D');
    v.emplace_back('E');   // 第五个触发扩容 4 -> 8
    std::printf("reserve(4) 塞 5 个后: copies=%ld moves=%ld (最后元素=%s)\n",
                copies, moves, v.back().text());
    return 0;
}
```

**验证输出**：

```text
$ g++ -std=c++17 -O2 noexceptv.cpp -o noex0 && ./noex0
reserve(4) 塞 5 个后: copies=4 moves=0 (最后元素=EEE)
$ g++ -std=c++17 -O2 -DNOEXCEPT noexceptv.cpp -o noex1 && ./noex1
reserve(4) 塞 5 个后: copies=0 moves=4 (最后元素=EEE)
```

1. 没 `noexcept`：扩容 4 → 8 时前 4 个元素全部**拷贝**到新内存（4 次拷贝、0 次移动），尽管移动构造明明存在。→ 知识点：[移动操作、std::move 与拷贝消除](../cppcon/2025/04-back-to-basics-move-semantics/03-move-ops-stdmove-and-elision.md)「noexcept 的重要性：vector 扩容的隐藏陷阱」一节
2. 加了 `noexcept`：4 次移动、0 次拷贝。原因：拷贝路径下原数据还在、可安全回滚；移动路径一旦中途抛异常，已移动的元素回不去。→ 知识点：同上（`vector` 的强异常安全保证）
3. 工程影响：持有动态内存的类（字符串、缓冲区、大对象）在大量数据场景下，一个漏写的 `noexcept` 就是数量级的性能差距——而且测试小数据时根本发现不了。→ 知识点：同上

## 10.6-A {#hw-10-6-a}

**难度 L1** · 题面见 [01-homework](./01-homework.md#hw-10-6-a)

**思路**：`optional<T>` 是拥有所有权的值类型：声明不等于构造，拷贝各自独立，移动后源对象仍处于「有值」状态（libstdc++ 的实现里移动只是把 `T` 移走、optional 本身仍 engaged）。

**验证输出**：

```text
$ g++ -std=c++17 -Wall -Wextra tracer.cpp -o tracer && ./tracer
a has_value=0
Tracer()
copy ctor
b has_value=1
move ctor
c has_value=1 b has_value=1
~Tracer()
after reset a has_value=0 c has_value=1
~Tracer()
~Tracer()
```

1. 声明 `optional<Tracer> a` 时没有 `Tracer()`——对象直到 `emplace()` 才真正构造；`b = a` 走拷贝构造，各自独立。→ 知识点：[std::optional 的值语义底子](../cppcon/2025/06-evolution-of-std-optional/02-value-semantics-of-optional.md)「实测：值语义到底『值』在哪」一节
2. `c = std::move(b)` 走移动构造；移动后 `b.has_value()` 仍是 1——moved-from 的 optional 在 libstdc++ 里仍 engaged，只是里面的 `Tracer` 被移走了（标准只保证「有效但未指定」）。→ 知识点：[移动操作、std::move 与拷贝消除](../cppcon/2025/04-back-to-basics-move-semantics/03-move-ops-stdmove-and-elision.md)「moved-from 状态：有效但不可知」一节
3. `a.reset()` 只析构 a 自己的那份，`c` 不受影响；程序退出时 b、c 各析构一次，a 为空不析构。→ 知识点：[std::optional 的值语义底子](../cppcon/2025/06-evolution-of-std-optional/02-value-semantics-of-optional.md)「它到底是个什么类型」一节（`variant<T, monostate>` 的等价描述）

## 10.6-B {#hw-10-6-b}

**难度 L3** · 题面见 [01-homework](./01-homework.md#hw-10-6-b)

**思路**：`optional<T&>` 内部就是一个指针，赋值永远是重绑定（改的是指针指向谁），与 optional 当前有没有值无关——这样行为才可静态推导。

**关键源码**：

```cpp
struct Account
{
    int balance;
};

int main()
{
    Account alice{100};
    Account bob{200};

    std::optional<Account&> e;
    std::optional<Account&> cur(alice);

    e = bob;              // 空 optional 赋 bob -> 绑定
    cur = bob;            // 已绑 alice 的赋 bob -> 重绑定
    cur->balance += 50;   // 穿透引用改 bob

    std::printf("e->balance=%d cur->balance=%d alice.balance=%d bob.balance=%d\n",
                e->balance, cur->balance, alice.balance, bob.balance);

    int x = 100;
    int y = 250;
    std::optional<int&> oa(x);
    std::optional<int&> ob(y);
    std::swap(oa, ob);
    std::printf("swap 后 *oa=%d *ob=%d (x=%d y=%d 都不变)\n", *oa, *ob, x, y);

    int v = 42;
    auto mo = std::make_optional(v);   // 拷贝,得到 optional<int>
    std::optional<int&> ref(v);        // 显式写引用类型
    v = 99;
    const bool ctad_is_ref = std::is_same_v<decltype(std::optional{v}), std::optional<int&>>;
    std::printf("make_optional=%d(拷贝) ref=%d(引用) CTAD 是 optional<int&>? %d\n",
                *mo, *ref, (int)ctad_is_ref);
    return 0;
}
```

**验证输出**（先在 C++23 下确认报错，再切 C++26）：

```text
$ g++ -std=c++23 -Wall -Wextra rebind.cpp
In file included from rebind.cpp:2:
/usr/include/c++/16/optional: In instantiation of 'union std::_Optional_payload_base<Account&>::_Storage<Account&, true>':
/usr/include/c++/16/optional:307:30:   required from 'struct std::_Optional_payload_base<Account&>'
  307 |       _Storage<_Stored_type> _M_payload;
      |                              ^~~~~~~~~~
...（optional 内部 union 存不下引用类型的一串模板实例化错误,这是"引用特化只在 C++26 有"的活证据）
$ g++ -std=c++26 -Wall -Wextra rebind.cpp -o rebind && ./rebind
e->balance=250 cur->balance=250 alice.balance=100 bob.balance=250
swap 后 *oa=250 *ob=100 (x=100 y=250 都不变)
make_optional=42(拷贝) ref=99(引用) CTAD 是 optional<int&>? 0
```

1. 空的 `e` 赋 `bob` → 绑定；已绑 alice 的 `cur` 赋 `bob` → **重绑定**（alice.balance 还是 100）；`cur->balance += 50` 改的是 bob。赋值行为不依赖状态，`opt = value` 这行代码的含义可以静态推导。→ 知识点：[optional 引用是什么，以及赋值为什么一定是重绑定](../cppcon/2025/06-evolution-of-std-optional/03-optional-reference-and-assignment.md)「赋值：重新绑定，不是值拷贝」「为什么不『有值就拷、没值就绑』」两节
2. `swap` 后 `*oa=250 *ob=100` 而 x/y 原值不变——交换的是两个内部指针，证明底层就是指针。→ 知识点：同上「vector<bool> 的幽灵」一节（对指针建模）
3. `make_optional(v)` 得到 `optional<int>`（v 改成 99 它还是 42），`CTAD std::optional{v}` 也退化为值版本（0 表示不是 `optional<int&>`）——想要引用语义就老老实实写全类型名。→ 知识点：同上「make_optional 和 CTAD 在引用上挖的坑」一节

## 10.C-1 {#hw-10-c-1}

**难度 L3** · 题面见 [01-homework](./01-homework.md#hw-10-c-1)

**思路**：`optional<T&>` 负责「可能不存在的引用」，ranges 管道负责「一遍遍历的统计」，两者组合正好覆盖 map 查找 + 批处理的两个痛点。

**关键源码**：

```cpp
template <typename M>
std::optional<typename M::mapped_type&> try_get(M& m, const typename M::key_type& k)
{
    auto it = m.find(k);
    if (it == m.end())
    {
        return std::nullopt;
    }
    return it->second;
}

int main()
{
    std::unordered_map<std::string, int> scores = {
        {"Alice", 88}, {"Bob", 57}, {"Carol", 95}, {"Dave", 76}, {"Eve", 66}};

    auto r = try_get(scores, "Bob");
    if (r)
    {
        *r += 10;   // 直接改表里的值
    }
    auto z = try_get(scores, "Zed");
    if (!z)
    {
        std::printf("Zed not found, no exception\n");
    }

    // 注意:filter_view 的 begin() 是非 const 成员,这两个 view 不能声明成 const
    auto vals = scores | std::views::values;
    auto passing = vals | std::views::filter([](int s) { return s >= 60; });

    long total = 0;
    for (int v : vals)
    {
        total += v;
    }
    long pass_total = 0;
    long pass_cnt = 0;
    for (int v : passing)
    {
        pass_total += v;
        ++pass_cnt;
    }
    std::printf("总人数=%zu 总分=%ld\n", scores.size(), total);
    std::printf("及格=%ld 及格总分=%ld 及格均分=%ld\n", pass_cnt, pass_total,
                pass_cnt == 0 ? 0 : pass_total / pass_cnt);

    long manual_total = 0;
    for (const auto& kv : scores)
    {
        manual_total += kv.second;
    }
    std::printf("手写循环对照: 总分=%ld 人数=%zu 一致\n", manual_total, scores.size());
    std::printf("Bob 现在的分数=%d\n", scores.at("Bob"));
    return 0;
}
```

**验证输出**：

```text
$ g++ -std=c++26 -Wall -Wextra -O2 scorepipe.cpp -o scorepipe && ./scorepipe
Zed not found, no exception
总人数=5 总分=392
及格=5 及格总分=392 及格均分=78
手写循环对照: 总分=392 人数=5 一致
Bob 现在的分数=67
```

1. `try_get(scores, "Bob")` 返回 `optional<int&>`，`*r += 10` 直接改表里的值（57 → 67），没有 `.second` 的噪音；查 Zed 走 `nullopt` 分支，不抛异常。总分 392 = 88+67+95+76+66——统计发生在改分之后，所以「及格=5」是改分后的真实状态。→ 知识点：[optional 引用是什么，以及赋值为什么一定是重绑定](../cppcon/2025/06-evolution-of-std-optional/03-optional-reference-and-assignment.md)「map 查找：最能说明问题的痛点」一节
2. `views::values` 把 map 投影成值的 view，`| views::filter(≥60)` 惰性过滤，一遍统计出及格人数与总分；手写循环对照结果一致。注意 `views::filter` 的 view 不能声明成 `const`——`filter_view::begin()` 是非 const 成员，`const` 了就编不过。→ 知识点：[Ranges、Views 与管道组合](../cppcon/2025/03-back-to-basics-ranges/03-ranges-views-and-composition.md)「views：惰性求值，Ranges 的灵魂」一节

## 10.C-2 {#hw-10-c-2}

**难度 L4** · 题面见 [01-homework](./01-homework.md#hw-10-c-2)

**思路**：把四个 `noinline` 函数的真实汇编并排看，零开销抽象的证据就摆在指令层面——range-based for 和恒真 filter 的 view 都退化成了同一条指针循环，`make_big` 直接写进调用者的栈缓冲。

**关键源码**（四个函数全部 `noinline`，防止优化把它们并进 main 而失去独立函数体）：

```cpp
__attribute__((noinline)) long sum_loop(const std::vector<int>& store)
{
    long total = 0;
    for (std::size_t i = 0; i < store.size(); ++i)
    {
        total += store[i];
    }
    return total;
}

__attribute__((noinline)) long sum_rangefor(const std::vector<int>& store)
{
    long total = 0;
    for (int v : store)
    {
        total += v;
    }
    return total;
}

__attribute__((noinline)) long sum_view(const std::vector<int>& store)
{
    long total = 0;
    for (int v : store | std::views::filter([](int) { return true; }))
    {
        total += v;
    }
    return total;
}

struct Big
{
    long data[4];
};

__attribute__((noinline)) Big make_big(long a, long b)
{
    Big r{};
    r.data[0] = a;
    r.data[1] = b;
    return r;
}

__attribute__((noinline)) Big use_big()
{
    return make_big(1, 2);
}
```

**验证输出**（`g++ -std=c++20 -O2 -S audit.cpp`，去掉 `.cfi_*` 与对齐噪音，节选五个函数）：

```asm
_Z8sum_loopRKSt6vectorIiSaIiEE:
    movq    (%rdi), %rsi
    movq    8(%rdi), %rax
    subq    %rsi, %rax
    je  .L1
    sarq    $2, %rax
    xorl    %edx, %edx
    movq    %rax, %rdi
    xorl    %eax, %eax
.L3:
    movslq  (%rsi,%rdx,4), %rcx
    addq    $1, %rdx
    addq    %rcx, %rax
    cmpq    %rdi, %rdx
    jb  .L3
.L1:
    ret
_Z12sum_rangeforRKSt6vectorIiSaIiEE:
    movq    (%rdi), %rax
    movq    8(%rdi), %rsi
    xorl    %edx, %edx
    cmpq    %rax, %rsi
    je  .L9
.L11:
    movslq  (%rax), %rcx
    addq    $4, %rax
    addq    %rcx, %rdx
    cmpq    %rsi, %rax
    jne .L11
.L9:
    movq    %rdx, %rax
    ret
_Z8sum_viewRKSt6vectorIiSaIiEE:
    movq    (%rdi), %rax
    movq    8(%rdi), %rsi
    xorl    %edx, %edx
    cmpq    %rax, %rsi
    je  .L14
.L16:
    movslq  (%rax), %rcx
    addq    $4, %rax
    addq    %rcx, %rdx
    cmpq    %rsi, %rax
    jne .L16
.L14:
    movq    %rdx, %rax
    ret
_Z8make_bigll:
    pxor    %xmm0, %xmm0
    movq    %rsi, (%rdi)
    movq    %rdi, %rax
    movq    %rdx, 8(%rdi)
    movups  %xmm0, 16(%rdi)
    ret
_Z7use_bigv:
    subq    $24, %rsp
    movl    $1, %esi
    movq    %fs:40, %rdx
    movq    %rdx, 8(%rsp)
    movl    $2, %edx
    call    _Z8make_bigll
    movq    8(%rsp), %rax
    subq    %fs:40, %rax
    jne .L23
    movq    %rdi, %rax
    addq    $24, %rsp
    ret
.L23:
    call    __stack_chk_fail@PLT
```

1. `sum_rangefor` 和 `sum_view` 的循环体**逐条一致**（都是 `movslq (%rax) / addq $4,%rax / addq / cmpq / jne`）——恒真 filter 被完全优化掉了，view 和 range-based for 没有任何额外开销。`sum_loop` 用的是计数循环（`sarq $2` 先算出元素个数），形态不同但同样是直通循环。→ 知识点：[从循环到迭代器：遍历数据的抽象之路](../cppcon/2025/03-back-to-basics-ranges/01-from-loops-to-iterators.md)「range-based for 和手写循环，编译出来一样吗」一节
2. `make_big` 直接把 `%rsi`/`%rdx` 写进 `(%rdi)`/`8(%rdi)`——`%rdi` 是调用者传进来的**返回缓冲地址**（隐式参数），32 字节零初始化用一条 `pxor` + `movups` 完成。→ 知识点：[阅读汇编与寄存器 ABI](../cppcon/2025/02-some-assembly-required/02-reading-assembly-and-registers-abi.md)「隐式参数——this 指针和返回值优化」一节
3. `use_big` 里 `movl $1,%esi` / `movl $2,%edx` 装好实参后直接 `call make_big`——`%rdi`（调用者传来的返回缓冲地址）原样透传，`call` 之后没有任何拷贝/搬运指令。中间的 `%fs:40` 是 Arch 的 GCC 默认开启的 `-fstack-protector-strong` 栈金丝雀，与返回值无关——NRVO 把拷贝消灭得连移动都不剩。→ 知识点：[移动操作、std::move 与拷贝消除](../cppcon/2025/04-back-to-basics-move-semantics/03-move-ops-stdmove-and-elision.md)「NRVO：比移动更牛的优化」一节

## 10.C-3 {#hw-10-c-3}

**难度 L5** · 题面见 [01-homework](./01-homework.md#hw-10-c-3)

**思路**：讲座原版 `would_narrow` 的浮点转整数分支里，`static_cast<long long>(u)` 对 NaN/±Inf/超范围值是 UB。先让 sanitizer 抓现行，再用「先查范围、后转换」的顺序把 UB 从代码里摘掉。有一个环境坑必须先说清楚：**GCC 的 `float-cast-overflow` 检查不在 `-fsanitize=undefined` 默认集合里**（和 Clang 不同），要显式写成 `-fsanitize=undefined,float-cast-overflow`；另外，如果把 NaN 写成 `__builtin_nan("")` 直接传参，GCC 会在编译期常量折叠掉转换，UB 就不会在运行期被 instrument——所以复现代码要用 `noinline` 函数在**运行时**喂入。

**第一步：最小复现抓现行**：

```cpp
#include <cstdio>
__attribute__((noinline)) double get_nan() { return __builtin_nan(""); }
__attribute__((noinline)) double get_big() { return 1e300; }
__attribute__((noinline)) int f(double x) { return static_cast<int>(x); }
int main() { std::printf("%d %d\n", f(get_nan()), f(get_big())); return 0; }
```

```text
$ g++ -std=c++20 -O1 -g -fsanitize=undefined,float-cast-overflow \
      -fno-sanitize-recover=all ub_probe.cpp -o ub_probe && ./ub_probe
ub_probe.cpp:4:69: runtime error: 1e+300 is outside the range of representable values of type 'int'
```

把讲座原版 `would_narrow`（含 `static_cast<long long>(u)` 分支）喂入运行时 NaN/1e300，可恢复模式下抓到：

```text
$ g++ -std=c++20 -O1 -g -fsanitize=undefined,float-cast-overflow ub_demo.cpp -o ub_demo && ./ub_demo
ub_demo.cpp:14:26: runtime error: nan is outside the range of representable values of type 'int'
would_narrow<int>(NaN) = 1
would_narrow<int>(1e300) = 1
$ g++ -std=c++20 -O2 ub_demo.cpp -o ub_demo_plain && ./ub_demo_plain
would_narrow<int>(NaN) = 1
would_narrow<int>(1e300) = 1
```

普通构建下两个用例都「碰巧返回 1」——这正是最阴险的 UB：结果看起来全对。同一个 UB，最小函数里先抓 1e300、模板版本里先抓 NaN，具体哪条先触发取决于代码形态，这正是 UB「不保证」的本色。

**第二步：修复版**：

```cpp
#include <cmath>
#include <cstdio>
#include <limits>
#include <type_traits>

// 修复版:浮点转整数前先做范围/有限性检查,消除 static_cast 的 UB
template<typename T, typename U>
constexpr bool would_narrow_safe(U u) noexcept
{
    if constexpr (std::is_same_v<T, U>) return false;
    if constexpr (std::is_unsigned_v<T> && std::is_signed_v<U>) {
        if (u < 0) return true;
    }
    if constexpr (std::is_floating_point_v<U> && std::is_integral_v<T>) {
        using L = long long;
        const long double lo = static_cast<long double>(std::numeric_limits<T>::lowest());
        const long double hi = static_cast<long double>(std::numeric_limits<T>::max());
        // NaN 的任何比较都是 false,落在 !(...) 里被判窄化;超范围同理
        if (!(u >= lo && u <= hi)) return true;
        if (u != static_cast<U>(static_cast<L>(u))) return true;  // 此时转换已安全
        return false;
    }
    T t = static_cast<T>(u);
    if (static_cast<U>(t) != u) return true;
    return false;
}
```

1. 修复的核心是把「先转换再比较」倒过来：先用 `long double` 做范围检查（x86-64 的 80 位 `long double` 能精确表示全部 64 位整数，所以 `(double)LLONG_MAX` 这类「转成 double 后恰好越过边界」的值也会被判为窄化），确认落在 `[lowest, max]` 内之后，`static_cast<long long>` 才是安全的。NaN 的一切比较都是 false，自然落入「拦」的分支。→ 知识点：[类型安全、Number 约束与边界检查](../cppcon/2025/01-concept-based-generic-programming/01-type-safety-and-number-concept.md)（`would_narrow` 的实现与 round-trip 检测的局限）
2. 16 个 `int` 边界用例 + 4 个 `long long` 关键用例的判定表全对，同一份代码在 `-fsanitize=undefined,float-cast-overflow` 下**零报告**。→ 知识点：[WG21 标准化与 x86/RISC-V 汇编哲学](../cppcon/2025/02-some-assembly-required/07-wg21-standardization-and-assembly-philosophy.md)「应对策略：不猜，用工具」一节（UB 要靠工具抓，不靠肉眼）

**验证输出**：

```text
$ g++ -std=c++20 -O1 -g -fsanitize=undefined,float-cast-overflow \
      -fno-sanitize-recover=all fixed.cpp -o fixed && ./fixed
int 版(16 个边界用例, 1 表示会被拦):
  NaN        -> int : 1
  +Inf       -> int : 1
  -Inf       -> int : 1
  0.0        -> int : 0
  -0.0       -> int : 0
  0.5        -> int : 1
  -0.5       -> int : 1
  1e300      -> int : 1
  -1e300     -> int : 1
  INT_MAX    -> int : 0
  INT_MAX+1  -> int : 1
  INT_MIN    -> int : 0
  INT_MIN-1  -> int : 1
  3.0        -> int : 0
  3.5        -> int : 1
  1.5        -> int : 1
long long 版关键用例:
  INT_MIN        -> long long : 0
  1e18           -> long long : 0
  (double)LLONG_MAX        -> long long : 1
  (double)LLONG_MAX + 1024 -> long long : 1
  -0.0           -> long long : 0
UBSan: 本程序应零报告
```

注意最后两行：`(double)LLONG_MAX` 在 double 下其实是 2⁶³（比 LLONG_MAX 大 1），转回 `long long` 必溢出，所以判窄化是**对**的——这种「源类型转一圈后恰好越过边界」的用例正是 L5 要逼你想清楚的边角。
