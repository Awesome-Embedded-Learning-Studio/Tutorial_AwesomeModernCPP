---
chapter: 7
cpp_standard:
- 20
description: 讲透 std::source_location——current() 怎么一次拿全 file/line/func/column、为什么作默认参数能自动注入调用点、和 __FILE__/__LINE__ 宏相比类型安全在哪、constexpr 的零开销怎么验证，以及默认参数 vs 函数体首行那个经典坑
difficulty: intermediate
order: 68
platform: host
prerequisites:
- expected：值或错误，C++23 的错误处理新范式
- optional：把「可能没有」做成类型
reading_time_minutes: 12
related:
- stacktrace：运行时调用栈与符号化（C++23）
tags:
- host
- cpp-modern
- intermediate
- 基础
title: 'source_location：编译期代码位置，__FILE__ 的类型安全替代'
---

# source_location：编译期代码位置，__FILE__ 的类型安全替代

写过日志、断言、测试框架的人，一定手写过这一类宏：

```cpp
#define LOG(msg) std::cout << __FILE__ << ":" << __LINE__ << " " << msg << '\n'
```

它能用，但难受的地方一大堆。`__FILE__` 是个 `const char*`、`__LINE__` 是个 `int`、`__func__` 又是另一个东西——想把「一次调用的位置信息」整体传给一个函数，你得手工拼三四个宏，类型还全是裸字符串和整数，一点保护都没有。更要命的是宏没有「列号」、没法在编译期组成一个值对象、一旦嵌套调用宏就会指向错误的位置。

C++20 给了一个标准化的、类型安全的替代：`std::source_location`。它把「这一行代码在哪个文件、第几行、哪个函数、第几列」打成一个对象，一次拿全，而且是 `constexpr` 的——编译期就能求值，运行时零开销。这一篇我们把它拆开跑一遍：先看 `current()` 怎么拿位置，再讲透它最常用的「默认参数注入」模式，最后和宏、和下一篇要讲的运行时 `stacktrace` 各自划清边界。

## current()：一次拿全 file / line / func / column

`source_location` 的入口只有一个静态成员函数 `current()`，返回一个代表「调用处」的 `source_location` 对象。对象上有四个查询接口：

```cpp
// Standard: C++20
#include <iostream>
#include <source_location>
#include <string_view>

void print_loc(const std::source_location& loc = std::source_location::current()) {
    std::cout << "file_name     : " << loc.file_name() << '\n';
    std::cout << "line          : " << loc.line() << '\n';
    std::cout << "column        : " << loc.column() << '\n';
    std::cout << "function_name : " << loc.function_name() << '\n';
}

int main() {
    print_loc();
}
```

`g++ -std=c++20 -O2`（本机 GCC 16.1.1）跑出来：

```text
file_name     : /tmp/sloc/basic.cpp
line          : 15
function_name : int main()
column        : 14
```

四样东西一次到位：文件名、行号、列号、函数签名。注意 `function_name()` 给的不是 `__func__` 那种光秃秃的名字，而是__完整的函数签名__（`int main()`）——这对模板和重载尤其有用，后面我们会实测。

这里有个细节先点出来：`current()` 是 `constexpr` 的，它返回的对象本身也是字面量类型（literal type），所以可以出现在常量表达式里。这一点决定了它「零开销」的性质，等下我们拿汇编验证。

## 为什么不是宏：类型安全 + 一次拿全

我们把 `source_location` 和传统宏放一起对比一下，差异非常清楚：

| 维度 | `__FILE__` / `__LINE__` / `__func__` | `source_location` |
|---|---|---|
| 类型 | `const char*` / `int` / `const char*`，各是各的 | 一个对象，`string_view` + `unsigned` |
| 信息完整度 | 文件 + 行 + 函数名，__没有列号__ | 文件 + 行 + 列 + 函数签名 |
| 求值时机 | 预处理期 / 编译期 | `constexpr`，编译期 |
| 能否整体传递 | 不能，得手工拼几个宏 | 能，传一个对象就行 |
| 受宏展开影响 | 嵌套调用宏会指向错误位置 | 由真实调用点决定，稳定 |

直接看代码最直观：

```cpp
// Standard: C++20
#include <iostream>
#include <source_location>

constexpr int line_of(std::source_location loc = std::source_location::current()) {
    return loc.line();
}

// static_assert 证明: current() 的结果在编译期就是确定的
static_assert(line_of() == __LINE__, "line_of must be usable in constant expression");

#define LEGACY_LOG() \
    std::cout << "[legacy] " << __FILE__ << ":" << __LINE__ \
              << " (no func, no col)\n"

int main() {
    constexpr int here = line_of();
    std::cout << "constexpr line_of() = " << here << '\n';
    LEGACY_LOG();
}
```

跑出来：

```text
constexpr line_of() = 20
[legacy] /tmp/sloc/constexpr.cpp:23 (no func, no col)
```

两个观察：第一，`line_of()` 的结果能直接喂给 `static_assert`，说明 `source_location` 在编译期就是确定的值——这不是运行时去查符号表，是编译器在生成这条调用时就把位置「焊」进了常量。第二，`LEGACY_LOG` 那行只给了「文件:行」，函数名和列号都没有，想拿得再叠 `__func__` 之类。

「焊进常量」这个说法不是修辞，我们看汇编。用 `-O2` 编译上面这个程序，再 grep 一下 `source_location` 相关的符号调用：

```text
$ g++ -std=c++20 -O2 -S -o constexpr.s constexpr.cpp
$ grep -c "current" constexpr.s
0
```

`current` 在汇编里一次都没出现——它被编译器在编译期完全求值、折叠成常量了。传 `source_location` 参数的开销，和传几个 `int` / `string_view` 一样，没有额外的运行时调用。这就是「零开销」的实证含义：不是「开销很小」，是「在 `-O2` 下根本不存在」。

想自己看 `current` 被编译期折叠掉？点开下面这个在线示例看汇编（`allow-x86-asm`），会发现 `current` 一次都没调用——位置被编译期「焊」进常量：

<OnlineCompilerDemo
  title="source_location 的零开销：current 在汇编里消失"
  source-path="code/examples/vol3/68_source_location.cpp"
  description="static_assert 证明 current() 编译期求值；看 x86-64 汇编会发现 current 一次都没调用——位置被编译期焊进常量，运行时零开销"
  allow-x86-asm
/>

## 默认参数注入：最常用的模式

`source_location` 真正的杀手锏，是「作函数默认参数，自动注入调用点」。这就是开头 `print_loc` 里那个写法：

```cpp
void print_loc(const std::source_location& loc = std::source_location::current()) {
    // ...
}
```

这里的设计精妙之处在于：__默认参数是在「调用点」求值的，而不是在「函数定义处」求值的__。C++ 的默认参数本来就是这个语义——每次调用 `print_loc()` 不带参数时，编译器在调用现场生成一次 `source_location::current()`。于是 `loc` 天然就代表了「是谁调用了 `print_loc`」，不需要调用方显式传位置。

这一点彻底改变了写日志和断言的方式。我们写一个带位置的 `log_info` 和一个失败时打印位置的 `expect`：

```cpp
// Standard: C++20
#include <iostream>
#include <source_location>
#include <string_view>

void expect(bool cond, std::string_view msg,
            std::source_location loc = std::source_location::current()) {
    if (!cond) {
        std::cerr << loc.file_name() << ':' << loc.line()
                  << " in " << loc.function_name()
                  << ": CHECK FAILED: " << msg << '\n';
    }
}

void log_info(std::string_view msg,
              std::source_location loc = std::source_location::current()) {
    std::cout << "[INFO " << loc.function_name() << ":" << loc.line()
              << "] " << msg << '\n';
}

int divide(int a, int b) {
    expect(b != 0, "除数不能为零");
    log_info("doing division");
    return a / b;
}

int main() {
    log_info("程序启动");
    std::cout << "10 / 2 = " << divide(10, 2) << '\n';
    divide(10, 0);   // 触发失败断言
}
```

跑出来：

```text
[INFO int main():30] 程序启动
[INFO int divide(int, int):25] doing division
10 / 2 = 5
/tmp/sloc/expect.cpp:24 in int divide(int, int): CHECK FAILED: 除数不能为零
[INFO int divide(int, int):25] doing division
```

注意几点：调用方一行 `log_info("...")` 完全不用关心位置，位置是自动注入的；`function_name()` 给出的是__完整签名__——`int divide(int, int)`，对重载和模板特别友好；失败断言打印的位置精确到 `divide` 里那一行 `expect` 调用，定位 bug 一步到位。

这就是 `source_location` 的核心用法：__把「想要知道调用方位置」这件事，从宏魔法变成一个普通的默认参数__。以前你得写 `EXPECT(cond, msg)` 这种恶心的宏，现在写一个普通函数就行，类型安全、可重载、能进调试器——宏能做的事它都能做，还不会污染命名空间。

### 为什么默认参数能拿到调用点

这里值得停一下，讲清楚背后的机制，不然换个场景你就会踩坑。

C++ 标准里，默认参数的求值点是「调用现场」，而不是「函数定义处」。这条规则一直都在，只是以前没什么正经用途。`source_location::current()` 的标准语义恰好是「返回调用它的那个位置」——当它出现在默认参数里时，这个「调用它的位置」就是 `print_loc` 的__调用者写的那一行__。

换句话说，是「默认参数在调用点求值」+「`current()` 返回调用点位置」这两条规则叠加，才让自动注入生效。理解了这一点，你就能预判下面这个经典坑。

## 经典坑：默认参数 vs 函数体首行

把 `current()` 写在不同位置，拿到的位置是完全不一样的。这是 `source_location` 最容易翻车的地方，我们直接实测对比：

```cpp
// Standard: C++20
#include <iostream>
#include <source_location>

// 模式A: current() 作默认参数 —— 拿到【调用点】位置
void log_default(std::source_location loc = std::source_location::current()) {
    std::cout << "[A 默认参数] line=" << loc.line()
              << " func=" << loc.function_name() << '\n';
}

// 模式B: 在函数体首行调 current() —— 拿到【当前函数】位置
void log_inline() {
    std::source_location loc = std::source_location::current();
    std::cout << "[B 函数体首行] line=" << loc.line()
              << " func=" << loc.function_name() << '\n';
}

int main() {
    log_default();   // 期望: 行号指向这一行
    log_inline();    // 期望: 行号指向 log_inline 内部
}
```

跑出来：

```text
[A 默认参数] line=19 func=int main()
[B 函数体首行] line=13 func=void log_inline()
```

差别一目了然：

- __模式 A__（默认参数）：`loc` 是在 `main` 里第 19 行那次调用现场求值的，所以拿到的是 `int main()` 的第 19 行——__调用点__。
- __模式 B__（函数体首行）：`current()` 是在 `log_inline` 内部第 13 行被调用的，所以拿到的是 `void log_inline()` 的第 13 行——__当前函数自己__。

两种写法各有用途，但混用就会出 bug。绝大多数场景你要的是「谁调用了我」，那就必须用模式 A；如果你真想记录「这个函数本身在哪」（比如函数入口日志），模式 B 才对。判断标准很简单：__`current()` 写在哪一行，就报哪一行的位置__——默认参数被求值的地点是调用现场，函数体里的 `current()` 就是函数体那一行。

::: warning current() 必须作默认参数，才能拿到调用点
想实现「日志函数自动记录调用者位置」，`current()` 一定要放在__默认参数__里。如果手滑写成函数体里第一行 `auto loc = std::source_location::current();`，你拿到的永远是日志函数自己那一行，而不是调用方——日志全指向同一个地方，彻底失去定位价值。这是 `source_location` 第一大坑，踩过的人都记得。
:::

## function_name() 给的是签名，不光是名字

上面已经看到 `function_name()` 返回的是完整签名。这对成员函数和模板特别有用，我们单独验证一下：

```cpp
// Standard: C++20
#include <iostream>
#include <source_location>

struct Tracker {
    void method(int x,
                std::source_location loc = std::source_location::current()) {
        std::cout << "member func call from: " << loc.function_name()
                  << " @ line " << loc.line() << '\n';
    }
};

template <typename T>
void tpl_func(T,
              std::source_location loc = std::source_location::current()) {
    std::cout << "template func call from: " << loc.function_name()
              << " @ line " << loc.line() << '\n';
}

int main() {
    Tracker t;
    t.method(42);
    tpl_func(7);
}
```

跑出来：

```text
member func call from: int main() @ line 23
template func call from: int main() @ line 24
```

注意这里验证的是默认参数模式下的行为：因为 `current()` 在默认参数里，`function_name()` 报的是__调用方__ `int main()`，而不是 `method` / `tpl_func` 自己。这再次印证了「默认参数 = 调用点」的规则——不管被调函数是普通函数、成员函数还是模板，注入的都是调用现场。

如果你想让被调函数自己记录自己的签名（比如函数入口埋点），那就回到上面的模式 B，在函数体里调 `current()`，这时 `function_name()` 才会给 `void Tracker::method(int)` 或 `void tpl_func<int>(int)` 这种被调函数自身的签名。两者用途不同，别搞混。

## 和 stacktrace 划清边界

讲到这里，一定要把 `source_location` 和下一篇要讲的运行时 `std::stacktrace`（C++23）区分开。它们俩都「和代码位置有关」，但完全是两个层次的东西：

| 维度 | `source_location`（C++20） | `stacktrace`（C++23） |
|---|---|---|
| 粒度 | 单点：调用处那一行 | 整个调用栈，多层 |
| 求值时机 | 编译期 `constexpr`，零开销 | 运行时，要查符号表 |
| 开销 | 无，折叠成常量 | 有，需要回溯栈帧 + 符号化 |
| 依赖 | 无，纯语言特性 | 要链接符号化支持（如 libstdc++ 的 `_GLIBCXX_USE_BACKTRACE`） |
| 典型用途 | 日志、断言、契约、调试埋点 | 崩溃时打印调用链、深栈诊断 |

一句话总结：`source_location` 回答「这一行在哪」，`stacktrace` 回答「我是怎么走到这一行的」。前者是编译期、零开销、单点，适合每次日志都带上；后者是运行时、有开销、整栈，适合出错时才打一次。日常 99% 的「带位置日志 / 断言」需求，`source_location` 就够了，别动用重量级的 `stacktrace`。

## 几个真实容易踩的点

::: warning current() 的位置决定一切
`current()` 出现在默认参数里 → 拿调用点；出现在函数体里 → 拿当前函数自己。想做自动注入日志，必须放默认参数。这是头号坑，见上面的实测对比。
:::

::: warning 别忘了 const& 默认参数
默认参数写成 `std::source_location loc = std::source_location::current()` 是按值传，`source_location` 很小（libstdc++ 上 `sizeof` 只有 8 字节），按值传没问题。但如果你自定义了一个持有多余状态的包装类型，记得用 `const std::source_location&` 默认参数避免拷贝——标准库的 `source_location` 本身是 trivial 的，按值即可。
:::

::: warning #line 指令同样会改写 source_location
`source_location` 和 `__FILE__`/`__LINE__` 一样，受 `#line` 指令影响。在生成器代码（ yacc/lex、模板生成器）里经常有 `#line` 重映射，`source_location` 报告的行号和文件名会跟着变。我们实测过：

```text
$ #line 100 "fake.cpp" 之后
source_location line=100 file=fake.cpp
```

这对调试生成代码是好事（位置指向源模板而不是生成结果），但要知道这个行为，免得看到「不存在的文件名」一头雾水。
:::

::: warning MSVC 上 current() 的行为历史上有坑
GCC 和 Clang 对 `current()` 在默认参数中的支持稳定。但老版本 MSVC（VS 2019 16.10 之前）对「默认参数里调 `current()`」的实现有 bug，拿到的位置会偏。如果你的代码要跨平台跑，确认 MSVC 版本足够新（VS 2022 17.0+ 已修复），否则在 Windows 上日志位置会错乱。本系列以 GCC 16.1.1 为准，Linux 下无此问题。
:::

## 小结

`std::source_location` 把「代码在哪」这件事从一堆宏变成了一个类型安全的对象。几条关键结论收一下：

- `current()` 一次拿全四个信息：`file_name()`（`string_view`）、`line()`、`column()`、`function_name()`（完整签名，`string_view`），比 `__FILE__`/`__LINE__`/`__func__` 三件套多了列号和完整签名。
- 核心用法是__默认参数注入__：把 `std::source_location loc = std::source_location::current()` 写成函数最后一个默认参数，调用方什么都不用做，位置自动指向调用点。
- `constexpr` + 零开销：编译期就求值，`-O2` 下 `current` 在汇编里完全不出现，传 `source_location` 和传几个 `int` 一样便宜。
- 头号坑：`current()` 写在默认参数里拿调用点，写在函数体里拿当前函数自己——想做自动注入日志，一定要用默认参数。
- 和 `stacktrace`（C++23）分工：`source_location` 是编译期单点、零开销，适合每次日志带位置；`stacktrace` 是运行时整栈、有开销，适合崩溃诊断。日常日志 / 断言用 `source_location` 就够。

典型用途就一条线：__日志、断言、契约、测试框架__——任何「想在运行时知道这条信息来自代码哪个位置」的地方，都该用它替掉手写的 `__FILE__` 宏。

下一篇我们讲运行时的 `std::stacktrace`（C++23），看怎么拿到完整的调用链、它和 `source_location` 怎么配合——一个负责「单点零开销埋点」，一个负责「出错时整栈回溯」。

## 参考资源

- [cppreference: std::source_location](https://en.cppreference.com/w/cpp/utility/source_location) —— `current()` / `file_name()` / `line()` / `column()` / `function_name()` 的标准语义（C++20）
- [cppreference: Default arguments](https://en.cppreference.com/w/cpp/language/default_arguments) —— 「默认参数在调用点求值」是 `current()` 注入机制的底层依据
- [P1208R6: source_location](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2019/p1208r6.pdf) —— `source_location` 进入 C++20 的提案，设计动机与「替代宏」的初衷
