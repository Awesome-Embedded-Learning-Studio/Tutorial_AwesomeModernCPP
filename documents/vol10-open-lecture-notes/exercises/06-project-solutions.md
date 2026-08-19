---
title: "卷 10 Project 参考实现"
description: "卷 10 综合项目（safelist）的完整参考实现：分层任务逐步讲解，每步标注知识点链接，含 Number 窄化检测、ranges 统计排序、健壮输入、-Wconversion/sanitizer/格式门、完整四则溢出检测 SafeInt 与边界自检的真实运行输出。"
chapter: 10
order: 6
tags: [host, advanced, cpp-modern, 类型安全, 泛型, Ranges, 工程实践]
difficulty: advanced
platform: host
cpp_standard: [17, 20, 23]
reading_time_minutes: 20
prerequisites: []
related: []
---

# 卷 10 Project 参考实现

> 全部输出在 WSL Arch（g++ 16.1.1）真实运行得到。参考实现只是**一种**过关方式；你的实现不一样、验收标准对得上，就都是对的。注意本项目用 `.clang-format`（LLVM 基底 + Allman 大括号 + 4 空格缩进）做格式门，下面的代码就是格式门通过后的样子，照抄时别改格式。

## 核心任务（L2）：能跑起来的数值列表 {#pj-core}

**题面见 [05-project](./05-project.md#pj-core)**

**思路**：`add` 走「`strtold` 解析成 `long double` → `narrow_convert<long long>` 落库」这条链，把「带小数」「超范围」的输入在构造那一刻就拦下；`Number<T>` 的构造函数是所有魔法发生的地方。→ 知识点：[类型安全、Number 约束与边界检查](../cppcon/2025/01-concept-based-generic-programming/01-type-safety-and-number-concept.md)「把检查塞进类型里：Number\<T>」一节

**`Number` 与窄化检测**——L1 热身从这里开始：concept 骨架 + `Number<T>` 声明先过 `g++ -c` 零警告。注意：pj-core 阶段你可以照讲座写原版 `would_narrow`，但 pj-gates 的 sanitizer 门会逼你换成下面的范围检查版（原版里 `static_cast<long long>(NaN/1e300)` 是 UB，见 [10.C-3](./02-homework-solutions.md#hw-10-c-3)）：

```cpp
template <typename T, typename U> constexpr bool would_narrow(U u) noexcept
{
    if constexpr (std::is_same_v<T, U>)
    {
        return false;
    }
    if constexpr (std::is_unsigned_v<T> && std::is_signed_v<U>)
    {
        if (u < 0)
            return true;
    }
    if constexpr (std::is_floating_point_v<U> && std::is_integral_v<T>)
    {
        using L = long long;
        const long double lo = static_cast<long double>(std::numeric_limits<T>::lowest());
        const long double hi = static_cast<long double>(std::numeric_limits<T>::max());
        if (!(u >= lo && u <= hi))
            return true;
        if (u != static_cast<U>(static_cast<L>(u)))
            return true;
        return false;
    }
    T t = static_cast<T>(u);
    if (static_cast<U>(t) != u)
    {
        return true;
    }
    return false;
}

template <typename T, typename U> constexpr T narrow_convert(U u)
{
    if (would_narrow<T>(u))
    {
        throw std::invalid_argument("narrowing conversion detected");
    }
    return static_cast<T>(u);
}

template <typename T> class Number
{
    T value_;

  public:
    template <typename U> constexpr Number(U u) : value_(narrow_convert<T>(u)) {}
    constexpr Number(T t) : value_(t) {}
    constexpr operator T() const noexcept { return value_; }
    constexpr T get() const noexcept { return value_; }
};
```

**健壮解析**——`strtold` 的 endptr 校验挡住「不是数字」，`narrow_convert` 挡住「数字但装不下/带小数」。→ 知识点：[类型安全、Number 约束与边界检查](../cppcon/2025/01-concept-based-generic-programming/01-type-safety-and-number-concept.md)「发生 narrowing 的时候怎么办？抛异常」一节

```cpp
bool parse_number(const std::string &s, long long &out)
{
    if (s.empty())
        return false;
    char *end = nullptr;
    const long double val = std::strtold(s.c_str(), &end);
    if (end == s.c_str() || *end != '\0')
    {
        return false; // 不是合法数字
    }
    out = narrow_convert<long long>(val); // 越界/带小数在这里被拦
    return true;
}
```

**`do_list`**——`list` 命令背后的函数，按序打印索引和值：

```cpp
void do_list(const std::vector<Number<long long>> &store)
{
    for (std::size_t i = 0; i < store.size(); ++i)
    {
        std::cout << "[" << i << "] " << store[i].get() << "\n";
    }
}
```

**命令循环骨架**——`getline` 读行、按空格切命令词与参数、`else if` 分派。完整文件里 main 放在所有辅助函数之后（类型与函数定义在前、main 最后），`do_calc`/`run_selfcheck` 的定义见 pj-l5 小节——把 main 完整贴在这里，是为了让分派结构一眼看到底。→ 知识点：[工具链与项目设计底线](../cppcon/2025/02-some-assembly-required/06-toolchain-and-project-design.md)（可读性优先的直白结构）

```cpp
int main()
{
    std::vector<Number<long long>> store;
    std::string line;
    std::cout << "命令: add / list / sum / avg / max / sort / calc / chk / quit\n";
    while (true)
    {
        std::cout << "> " << std::flush;
        if (!std::getline(std::cin, line))
        {
            break;
        }
        const std::size_t sp = line.find(' ');
        const std::string cmd = (sp == std::string::npos) ? line : line.substr(0, sp);
        const std::string args = (sp == std::string::npos) ? std::string{} : line.substr(sp + 1);

        if (cmd == "quit")
        {
            break;
        }
        else if (cmd == "add")
        {
            long long v = 0;
            try
            {
                if (!parse_number(args, v))
                {
                    std::cout << "不是合法数字: " << args << "\n";
                }
                else
                {
                    store.emplace_back(v);
                    std::cout << "已添加 " << v << "\n";
                }
            }
            catch (const std::invalid_argument &e)
            {
                std::cout << "数字越界: " << args << " -> " << e.what() << "\n";
            }
        }
        else if (cmd == "list")
        {
            do_list(store);
        }
        else if (cmd == "sum")
        {
            do_sum(store);
        }
        else if (cmd == "avg")
        {
            do_avg(store);
        }
        else if (cmd == "max")
        {
            do_max(store);
        }
        else if (cmd == "sort")
        {
            do_sort(store);
        }
        else if (cmd == "calc")
        {
            do_calc(args);
        }
        else if (cmd == "chk")
        {
            run_selfcheck();
        }
        else
        {
            std::cout << "未知命令: " << cmd << "\n";
        }
    }
    return 0;
}
```

**验证输出**（核心任务的完整会话，见文末一次性完整会话；这里先单独贴 add/list 的关键行为）：

```text
$ g++ -std=c++20 -O2 -Wall -Wextra -Wconversion -Werror safelist.cpp -o safelist
$ ./safelist
命令: add / list / sum / avg / max / sort / calc / chk / quit
> add 100
已添加 100
> add 2000000000000000000
已添加 2000000000000000000
> add 3.5
数字越界: 3.5 -> narrowing conversion detected
> add abc
不是合法数字: abc
> list
[0] 100
[1] 2000000000000000000
> quit
```

3.5 被窄化检测拦下（浮点转整数、有小数部分），`abc` 被 endptr 校验拦下——两条防线各管一摊。→ 知识点：[类型安全、Number 约束与边界检查](../cppcon/2025/01-concept-based-generic-programming/01-type-safety-and-number-concept.md)（窄化检测与输入校验的分工）

## 进阶任务（L3）：统计与排序 {#pj-opt}

**题面见 [05-project](./05-project.md#pj-opt)**

**思路**：`avg` 用整数除法——列表存的就是整数，整数平均天然截断；换成浮点就要引浮点误差和打印格式两摊麻烦。`sort` 用 `std::ranges::sort` + 显式比较器，只传容器不传迭代器对。→ 知识点：[Ranges、Views 与管道组合](../cppcon/2025/03-back-to-basics-ranges/03-ranges-views-and-composition.md)「受约束算法：少传一个参数，少一个出错的机会」一节

```cpp
void do_sum(const std::vector<Number<long long>> &store)
{
    long long total = 0;
    for (const auto &n : store)
    {
        total += n.get();
    }
    std::cout << "总和 = " << total << "\n";
}

void do_avg(const std::vector<Number<long long>> &store)
{
    if (store.empty())
    {
        std::cout << "列表为空\n";
        return;
    }
    long long total = 0;
    for (const auto &n : store)
    {
        total += n.get();
    }
    const long long count = static_cast<long long>(store.size());
    std::cout << "平均 = " << total / count << " (整数除法)\n";
}

void do_max(const std::vector<Number<long long>> &store)
{
    if (store.empty())
    {
        std::cout << "列表为空\n";
        return;
    }
    std::size_t best = 0;
    for (std::size_t i = 1; i < store.size(); ++i)
    {
        if (store[i].get() > store[best].get())
        {
            best = i;
        }
    }
    std::cout << "最大 = " << store[best].get() << " ([" << best << "])\n";
}

void do_sort(std::vector<Number<long long>> &store)
{
    std::ranges::sort(store, [](const Number<long long> &a, const Number<long long> &b)
                      { return a.get() > b.get(); });
    std::cout << "排序(降序):\n";
    for (std::size_t i = 0; i < store.size(); ++i)
    {
        std::cout << "[" << i << "] " << store[i].get() << "\n";
    }
}
```

**验证输出**：

```text
> add 3000000000000000000
已添加 3000000000000000000
> sum
总和 = 5000000000000000100
> avg
平均 = 1666666666666666700 (整数除法)
> max
最大 = 3000000000000000000 ([2])
> sort
排序(降序):
[0] 3000000000000000000
[1] 2000000000000000000
[2] 100
```

`count` 用 `static_cast<long long>(store.size())` 显式化 `size_t → long long` 的转换——这就是 `-Wconversion` 会逼你写出来的东西。→ 知识点：[WG21 标准化与 x86/RISC-V 汇编哲学](../cppcon/2025/02-some-assembly-required/07-wg21-standardization-and-assembly-philosophy.md)「应对策略：不猜，用工具」一节的警告选项实践

## 再进阶任务（L4）：把门装上 {#pj-gates}

**题面见 [05-project](./05-project.md#pj-gates)**

**思路**：①健壮性 = 每个输入点都假设「用户会敲歪」；②`-Wconversion -Werror` 逼你显式化每个可能丢数据的转换；③sanitizer 门验证「没有 UB 藏在会话里」；④格式门 + 汇编审计收尾。

**验证输出**——严格编译（零警告即零输出）：

```text
$ g++ -std=c++20 -O2 -Wall -Wextra -Wconversion -Werror safelist.cpp -o safelist; echo "exit=$?"
exit=0
```

**验证输出**——sanitizer 构建下的完整会话（含 `1e300` 用例，零报告）：

```text
$ g++ -std=c++20 -O1 -g -fsanitize=address,undefined safelist.cpp -o safelist_san
$ ./safelist_san
命令: add / list / sum / avg / max / sort / calc / chk / quit
> add 100
已添加 100
> add 2000000000000000000
已添加 2000000000000000000
> add 3000000000000000000
已添加 3000000000000000000
> add 3.5
数字越界: 3.5 -> narrowing conversion detected
> add abc
不是合法数字: abc
> add 1e300
数字越界: 1e300 -> narrowing conversion detected
> list
[0] 100
[1] 2000000000000000000
[2] 3000000000000000000
> sum
总和 = 5000000000000000100
> avg
平均 = 1666666666666666700 (整数除法)
> max
最大 = 3000000000000000000 ([2])
> sort
排序(降序):
[0] 3000000000000000000
[1] 2000000000000000000
[2] 100
> calc add 9223372036854775807 1
calc 捕获: addition overflow
> calc div 7 0
calc 捕获: division by zero
> calc mul 3037000500 3037000500
calc 捕获: multiplication overflow
> chk
[PASS] LLONG_MAX+1 被拦
[PASS] LLONG_MIN-1 被拦
[PASS] LLONG_MAX*2 被拦
[PASS] LLONG_MIN/-1 被拦
[PASS] 42/0 被拦
边界自检 5/5 通过
> quit
$ echo "exit=$?"
exit=0
```

**验证输出**——格式门与门后再编译：

```text
$ clang-format --dry-run --Werror safelist.cpp; echo "exit=$?"
exit=0
$ g++ -std=c++20 -O2 -Wall -Wextra -Wconversion -Werror safelist.cpp -o safelist_fmt; echo "exit=$?"
exit=0
```

**验证输出**——`do_sum` 的汇编审计（`-O2 -S`，节选核心循环）：

```asm
_Z6do_sumRKSt6vectorI6NumberIxESaIS1_EE:
    pushq   %rbx
    movq    (%rdi), %rax
    xorl    %ebx, %ebx
    movq    8(%rdi), %rdx
    cmpq    %rax, %rdx
    je  .L123
.L124:
    addq    (%rax), %rbx
    addq    $8, %rax
    cmpq    %rax, %rdx
    jne .L124
.L123:
    movl    $9, %edx
    leaq    .LC8(%rip), %rsi
    leaq    _ZSt4cout(%rip), %rdi
    call    _ZSt16__ostream_insertIcSt11char_traitsIcEERSt13basic_ostreamIT_T0_ES6_PKS3_l@PLT
    movq    %rbx, %rsi
    leaq    _ZSt4cout(%rip), %rdi
    call    _ZNSo9_M_insertIxEERSoT_@PLT
    movl    $1, %edx
    leaq    .LC7(%rip), %rsi
    popq    %rbx
    movq    %rax, %rdi
    jmp _ZSt16__ostream_insertIcSt11char_traitsIcEERSt13basic_ostreamIT_T0_ES6_PKS3_l@PLT
```

循环体就是 `addq (%rax), %rbx`（直接读 8 字节元素）+ 指针推进 + 边界比较——**没有任何对 `Number::get()` 的 call**，`get()` 被完全内联掉了。一个包装类型被优化到「和裸 `long long` 的求和循环一样」，这就是零开销抽象的可信证据。→ 知识点：[C++：底层汇编探秘](../cppcon/2025/02-some-assembly-required/01-personal-journey-and-from-assembly-to-cpp.md)（抽象有没有代价，汇编说了算）

## 终极挑战（L5）：SafeInt —— 四则运算全查溢出 {#pj-l5}

**题面见 [05-project](./05-project.md#pj-l5)**

**思路**：signed 溢出是 UB，不能像 unsigned 那样「回绕后再比大小」——编译器基于「signed 不会溢出」做优化，回绕出来的值本身就是 UB 的产物。所以 `+`/`-`/`*` 全部走编译器内置的 `__builtin_*_overflow`，`/` 单独查除零和 `LLONG_MIN / -1`。→ 知识点：[类型安全、Number 约束与边界检查](../cppcon/2025/01-concept-based-generic-programming/01-type-safety-and-number-concept.md)「原文错误更正」一节的 `safe_add` 思路推广

```cpp
struct SafeInt
{
    long long v;

    explicit SafeInt(long long x) : v(x) {}

    SafeInt operator+(SafeInt o) const
    {
        long long r = 0;
        if (__builtin_add_overflow(v, o.v, &r))
        {
            throw std::overflow_error("addition overflow");
        }
        return SafeInt(r);
    }

    SafeInt operator-(SafeInt o) const
    {
        long long r = 0;
        if (__builtin_sub_overflow(v, o.v, &r))
        {
            throw std::overflow_error("subtraction overflow");
        }
        return SafeInt(r);
    }

    SafeInt operator*(SafeInt o) const
    {
        long long r = 0;
        if (__builtin_mul_overflow(v, o.v, &r))
        {
            throw std::overflow_error("multiplication overflow");
        }
        return SafeInt(r);
    }

    SafeInt operator/(SafeInt o) const
    {
        if (o.v == 0)
        {
            throw std::domain_error("division by zero");
        }
        if (v == std::numeric_limits<long long>::min() && o.v == -1)
        {
            throw std::overflow_error("division overflow");
        }
        return SafeInt(v / o.v);
    }
};
```

**`calc` 命令解析与分派**——`calc <add|sub|mul|div> <a> <b>`：两个参数用 `strtoll` + `errno` 校验（pj-gates 要求的健壮性链），解析成功后分派到 `SafeInt` 四则，越界异常统一在命令层捕获打印；非法参数、未知运算各有一条报错。→ 知识点：[WG21 标准化与 x86/RISC-V 汇编哲学](../cppcon/2025/02-some-assembly-required/07-wg21-standardization-and-assembly-philosophy.md)「应对策略：不猜，用工具」一节（每个输入点都假设「用户会敲歪」）

```cpp
// calc <add|sub|mul|div> <a> <b>:strtoll + errno 校验参数,异常统一在命令层捕获
void do_calc(const std::string &args)
{
    std::istringstream iss(args);
    std::string op;
    std::string as;
    std::string bs;
    if (!(iss >> op >> as >> bs))
    {
        std::cout << "用法: calc <add|sub|mul|div> <a> <b>\n";
        return;
    }
    char *end = nullptr;
    errno = 0;
    const long long a = std::strtoll(as.c_str(), &end, 10);
    if (end == as.c_str() || *end != '\0' || errno == ERANGE)
    {
        std::cout << "calc 参数不是合法整数: " << as << "\n";
        return;
    }
    errno = 0;
    const long long b = std::strtoll(bs.c_str(), &end, 10);
    if (end == bs.c_str() || *end != '\0' || errno == ERANGE)
    {
        std::cout << "calc 参数不是合法整数: " << bs << "\n";
        return;
    }
    try
    {
        const SafeInt x(a);
        const SafeInt y(b);
        if (op == "add")
        {
            std::cout << (x + y).v << "\n";
        }
        else if (op == "sub")
        {
            std::cout << (x - y).v << "\n";
        }
        else if (op == "mul")
        {
            std::cout << (x * y).v << "\n";
        }
        else if (op == "div")
        {
            std::cout << (x / y).v << "\n";
        }
        else
        {
            std::cout << "未知运算: " << op << "\n";
        }
    }
    catch (const std::exception &e)
    {
        std::cout << "calc 捕获: " << e.what() << "\n";
    }
}
```

**`chk` 边界自检**——`chk` 用五条边界用例做回归，把「测试即验收」落成一行命令的轻量自检（同样是「不猜，用工具」的思路：验收不靠肉眼，靠每次都能重跑的自检）。main 里的接线是 `else if (cmd == "calc") { do_calc(args); } else if (cmd == "chk") { run_selfcheck(); }`，完整 main 见 pj-core 小节。→ 知识点：同上「应对策略：不猜，用工具」一节

```cpp
int run_selfcheck()
{
    int passed = 0;
    const auto expect_throw = [&passed](auto &&f, const char *name)
    {
        try
        {
            f();
            std::cout << "[FAIL] " << name << " 没被拦\n";
        }
        catch (const std::exception &)
        {
            std::cout << "[PASS] " << name << " 被拦\n";
            ++passed;
        }
    };
    const long long mx = std::numeric_limits<long long>::max();
    const long long mn = std::numeric_limits<long long>::min();
    expect_throw([mx]() { (void)(SafeInt(mx) + SafeInt(1)); }, "LLONG_MAX+1");
    expect_throw([mn]() { (void)(SafeInt(mn) - SafeInt(1)); }, "LLONG_MIN-1");
    expect_throw([mx]() { (void)(SafeInt(mx) * SafeInt(2)); }, "LLONG_MAX*2");
    expect_throw([mn]() { (void)(SafeInt(mn) / SafeInt(-1)); }, "LLONG_MIN/-1");
    expect_throw([]() { (void)(SafeInt(42) / SafeInt(0)); }, "42/0");
    std::cout << "边界自检 " << passed << "/5 通过\n";
    return passed == 5 ? 0 : 1;
}
```

**验证输出**（sanitizer 构建下，零报告）：

```text
> calc add 9223372036854775807 1
calc 捕获: addition overflow
> calc div 7 0
calc 捕获: division by zero
> calc mul 3037000500 3037000500
calc 捕获: multiplication overflow
> calc sub -5 3
-8
> calc add 1 2
3
> chk
[PASS] LLONG_MAX+1 被拦
[PASS] LLONG_MIN-1 被拦
[PASS] LLONG_MAX*2 被拦
[PASS] LLONG_MIN/-1 被拦
[PASS] 42/0 被拦
边界自检 5/5 通过
```

`calc sub -5 3 = -8` 走合法路径，说明 `SafeInt` 不是「全拦」，是「只拦真溢出」——这也正是讲座里 safe_int 的设计意图：错误在发生的那一刻被抓住，而不是等它传播到某个边界检查里才被发现。→ 知识点：[类型安全、Number 约束与边界检查](../cppcon/2025/01-concept-based-generic-programming/01-type-safety-and-number-concept.md)「用 safe_int 给 span 加上真正的保护」一节（从源头杜绝错误值）

到这里，「让类型系统守门」就有了实物：`Number` 守构造、`SafeInt` 守算术、`strtold`/endptr 守解析、`-Wconversion` 守转换、sanitizer 守 UB、格式门守可读性。整个项目没有一个地方在赌「用户会输对」「数字不会溢出」——每一层都有一道看得见的门。
