---
title: "print：C++23 的直接输出与 iostream 解耦"
description: 讲透 std::print/std::println 怎么绕开 cout 的 sync_with_stdio 与 locale 开销直接写流、关掉 sync 后和 cout 混用为什么会顺序错乱（以及用 print(cout,…) 一招救回来）、真实 benchmark 下 print 对比 cout/printf 的数量级，以及 FILE*/ostream 双重载与 Unicode 输出的工程取舍
chapter: 7
order: 53
cpp_standard:
  - 23
difficulty: intermediate
platform: host
reading_time_minutes: 14
prerequisites:
  - "迭代器适配器：反向、插入与流"
  - "char8_t 与 UTF-8"
related:
  - "容器选择指南：按操作、内存与失效规则挑对容器"
tags:
  - host
  - cpp-modern
  - intermediate
  - 基础
---

# print：C++23 的直接输出与 iostream 解耦

`std::format`（C++20）解决了「怎么把数据拼成字符串」的问题，但留下一个尴尬的尾巴：拼好的字符串要落到屏幕上，还是得交回 `std::cout << std::format(...)`。这一绕，就把 `format` 辛辛苦苦省下来的开销又还回去了——`cout` 背着 iostream 那一整套同步与 locale 机制，每次 `<<` 都不便宜。`std::print` / `std::println`（C++23）就是来收这个尾的：把格式化和输出合到一次调用里，直接写流，不再经过 iostream 的 `<<` 链路。

这一篇我们聚焦 `print` 的**输出语义**——它凭什么能比 `cout` 快、和 `cout` 混用时那个经典的顺序错乱坑、真实 benchmark 下的数量级，以及 `FILE*` 和 `ostream` 两套重载怎么选。格式串的语法（`{}`、`{:x}`、`{:.3f}` 这些）归上一篇 `std::format`，这里不重复，我们只关心「拼好的东西怎么出去、出去得快不快、和别的输出方式打不打架」。

## 先上手跑一跑

`print` 的基本长相，和 `format` 几乎一样，区别只在于它直接把结果写到 stdout，而不是返回字符串：

```cpp
// Standard: C++23
#include <print>
#include <cstdio>

int main()
{
    // 重载 (1):直接写 stdout
    std::print("Hello, {}\n", "world");
    std::print("{2} {1}{0}!\n", 23, "C++", "Hello");   // 手动索引,可以乱序

    // println:末尾自动加换行,不用自己补 \n
    std::println("一行带换行: {}", 42);

    // 带 format-spec 的格式串(语法细节归 format 那篇)
    std::println("十六进制: {:x}  浮点: {:.3f}", 255, 3.14159265);

    // 转义花括号
    std::println("字典字面量: {{key: {}}}", "value");

    // print 到 stderr(stderr 无缓冲,行不会卡)
    std::println(stderr, "这条进 stderr");
    return 0;
}
```

用 `g++ -std=c++23 -O2`（本机 GCC 16.1.1）跑出来：

```text
这条进 stderr
Hello, world
Hello C++23!
一行带换行: 42
十六进制: ff  浮点: 3.142
字典字面量: {key: value}
```

注意第一行——`stderr` 的内容反而排在最前面。这不是 bug，恰恰是我们要讲的核心机制：`println(stderr, ...)` 写的是无缓冲的 `stderr`，立刻落地；而 `print` 写的 `stdout` 在这里（重定向到管道/编辑器输出窗口时）是块缓冲，要等程序结束统一刷出来。两条流各走各的缓冲，谁先落地取决于谁不缓冲。这个「各走各的缓冲」正是后面顺序错乱坑的根源。

## 为什么 print 能比 cout 快：绕开两层开销

要理解 `print` 的设计动机，得先看 `cout` 为什么慢。`std::cout << "i=" << i << '\n'` 这种写法，每次 `<<` 都背着两样东西：

1. **与 C stdio 的同步**（`sync_with_stdio`，默认开）。为了保证 `std::cout` 和 `std::printf` 输出顺序一致，libstdc++ 在每次 `<<` 时都要去协调 C 的 `FILE*` 缓冲——这是个实打实的运行期开销。
2. **locale 感知**。iostream 的格式化（数字、货币、日期）要查 locale，即便你根本没设过 locale，这条检查路径也在。

`std::print` 把这两层都绕开了。它直接调用 C 的 `FILE*` 写入（`stdout` 走 `fwrite` 那一套），格式化用 `std::format` 的编译期解析结果，**完全不经 iostream**。cppreference 对它的描述就是一句「equivalent to `std::print(stdout, fmt, args...)`」，底层落到 `vprint_unicode` / `vprint_nonunicode` 这种直接写流的函数上。

换句话说，`cout` 那套「同步 + locale + 每个操作符一次函数调用」的包袱，`print` 一个都不背。这正是它设计目标里那句「decouple from iostream」的字面意思。

但这里有个**反直觉**的点要先点破：`print` **不**是「永远最快的输出方式」。它的价值不在绝对速度第一，而在「不用关 sync、不用碰 locale，就能拿到接近 `printf` 的速度，同时保留 `format` 的类型安全」。下一节的 benchmark 会把这件事说清楚，别被「print 很快」的口号带歪。

## 实测：print 对 cout、对 printf

光说「绕开开销」不够，我们直接上 benchmark。把 200 万条短行分别用 `cout`（开/关 sync 两种）、`printf`、`print` 写到 `/dev/null`（排除终端 I/O 的噪声，只测格式化与缓冲逻辑本身），计时到微秒：

```cpp
// Standard: C++23
#include <cstdio>
#include <iostream>
#include <print>
#include <chrono>

constexpr int kIterations = 2'000'000;

static void report(const char* name, std::chrono::steady_clock::time_point t0,
                   std::chrono::steady_clock::time_point t1)
{
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
    std::fprintf(stderr, "%-22s %lld us\n", name, (long long)us);
}

void bench_cout_sync()
{
    std::ios::sync_with_stdio(true);   // 默认
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < kIterations; ++i) {
        std::cout << "i=" << i << " sq=" << i * 2 << '\n';
    }
    report("cout (sync=true)", t0, std::chrono::steady_clock::now());
}

void bench_cout_nosync()
{
    std::ios::sync_with_stdio(false);  // 常见的"加速 cout"写法
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < kIterations; ++i) {
        std::cout << "i=" << i << " sq=" << i * 2 << '\n';
    }
    report("cout (sync=false)", t0, std::chrono::steady_clock::now());
}

void bench_printf()
{
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < kIterations; ++i) {
        std::printf("i=%d sq=%d\n", i, i * 2);
    }
    report("printf", t0, std::chrono::steady_clock::now());
}

void bench_print()
{
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < kIterations; ++i) {
        std::print("i={} sq={}\n", i, i * 2);
    }
    report("print", t0, std::chrono::steady_clock::now());
}

int main()
{
    std::freopen("/dev/null", "w", stdout);   // 排除终端 I/O 噪声
    bench_cout_sync();
    bench_printf();
    bench_print();
    bench_cout_nosync();
    return 0;
}
```

本机 GCC 16.1.1，`-std=c++23 -O2`，连跑三次（微秒绝对值会随负载波动，我们只看数量级和相对关系）：

```text
cout (sync=true)       180151 us
printf                 128323 us
print                  176287 us
cout (sync=false)      150366 us
cout (sync=true)       179359 us
printf                 133809 us
print                  171167 us
cout (sync=false)      155003 us
cout (sync=true)       191023 us
printf                 133643 us
print                  176044 us
cout (sync=false)      171143 us
```

想自己跑一遍？点开下面这个在线示例（用 `-std=c++23` 编译，耗时打到 stderr）：

<OnlineCompilerDemo
  title="cout / printf / std::print 格式化性能对比"
  source-path="code/examples/vol3/53_print_benchmark.cpp"
  description="200 万条短行写 /dev/null，对比 cout(sync 开/关)、printf、std::print 的格式化耗时——print 不动 sync 就拿到类型安全的一档速度"
  allow-run
  run-options="-O2 -std=c++23"
/>

把数量级结论捋清楚：

- `printf` 最快（约 130 ms），但代价是 C 风格的可变参数——类型不安全、格式串和参数对不上只能运行期崩。
- `cout(sync=false)` 次之（约 155–170 ms），但这是**要你手动关掉同步**换来的，一旦关了，它和 `printf`/`print` 的输出顺序就不再被标准库保证（下一节的坑）。
- `print` 稳定在 170–180 ms，**不需要任何 sync 开关**就拿到这个速度，并且带 `format` 的类型安全。
- `cout(sync=true)`（默认）最慢，约 180–190 ms——这就是大多数人不改设置时实际拿到的 `cout` 性能。

所以诚实的结论是：`print` **不是绝对速度冠军**（`printf` 和关了 sync 的 `cout` 能持平甚至更快），但它在你「不想动 sync、又想要类型安全的格式化」时，是性价比最高的那一档。如果代码已经到处是 `cout` 且你为性能关掉了 sync，单独把几行热路径换成 `print` 不一定带来收益——`print` 的主战场是新代码和想彻底摆脱 iostream 的场景。

## 真正的坑：print 和 cout 混用会顺序错乱

性能是 `print` 的卖点，但日常最容易绊倒人的是**同步**。`print` 直接写 C 的 `stdout` 缓冲，`cout`（在 libstdc++ 里）有自己的 streambuf。两者默认通过 `sync_with_stdio(true)` 协调，所以默认情况下顺序是对的：

```cpp
// Standard: C++23
#include <iostream>
#include <print>

int main()
{
    // sync_with_stdio 默认 true,顺序正确
    std::cout << "第一行(cout)\n";
    std::print("第二行(print)\n");
    std::cout << "第三行(cout)\n";
    std::print("第四行(print)\n");
    return 0;
}
```

```text
第一行(cout)
第二行(print)
第三行(cout)
第四行(print)
```

但很多人为了性能会写 `std::ios::sync_with_stdio(false)`。这一行一旦加上，`cout` 和 C stdio 的协调就断了，两条缓冲各刷各的——顺序立刻错乱：

```cpp
// Standard: C++23
#include <iostream>
#include <print>

int main()
{
    std::ios::sync_with_stdio(false);   // 常见的"加速 cout"写法

    std::cout << "第一行(cout)\n";
    std::print("第二行(print)\n");
    std::cout << "第三行(cout)\n";
    std::print("第四行(print)\n");

    std::cout.flush();   // 不 flush,cout 残留可能根本看不到
    return 0;
}
```

把输出重定向到管道（块缓冲模式），稳定复现：

```text
第一行(cout)
第三行(cout)
第二行(print)
第四行(print)
```

`cout` 的两行全跑到前面去了，`print` 的两行挤在后面——这显然不是源代码里的顺序。原因正是上面说的：关掉 sync 后 `cout` 的 streambuf 和 `print` 写的 C `stdout` 缓冲互不通气，谁先攒满、谁先被 flush，谁就先落地。这里 `cout` 在程序结束统一 flush，所以它的内容整块压到了后面（但相对自己内部顺序是对的）。

::: warning 关了 sync 就别再混用 print(stdout) 和 cout
`sync_with_stdio(false)` 是个「全局开关」——它一旦关掉，影响的是整个程序里 `cout`/`cin` 和 C stdio 的关系。如果你在某个性能热点关了 sync，然后又在别处既用 `cout` 又用 `print`（默认走 stdout），输出顺序就不被保证了。这种交错在终端（行缓冲）上可能凑巧看不出来，一旦重定向到文件或管道（块缓冲）就稳定翻车。排查这种 bug 极其折磨人，因为「在我机器上顺序是对的」。
:::

### 一招救回来：print(cout, …) 走同一条缓冲

这个坑有个很干净的解法，前提是你得知道 `print` 不止能写 `FILE*`——C++23 给了它一个 **`ostream` 重载**。把 `print` 的目标从默认的 `stdout` 换成 `cout` 本身，输出就走 `cout` 的 streambuf，和 `<<` 共享同一条缓冲，顺序自然就对回来了：

```cpp
// Standard: C++23
#include <iostream>
#include <print>

int main()
{
    std::ios::sync_with_stdio(false);
    std::cout << "A(cout)\n";
    std::print(std::cout, "B(print→cout)\n");   // 走 cout 自己的缓冲
    std::cout << "C(cout)\n";
    std::print(std::cout, "D(print→cout)\n");

    std::cout.flush();
    return 0;
}
```

```text
A(cout)
B(print→cout)
C(cout)
D(print→cout)
```

顺序完全正确。对比一下两组就很清楚了：

```text
print(stdout) 与 cout 交错(sync=false):     → A C B D  (错乱)
print(cout)   与 <<  同缓冲(sync=false):     → A B C D  (正确)
```

机理一句话：`print(FILE*)` 和 `cout` 是两条独立缓冲，关了 sync 就各刷各的；`print(ostream)` 复用 `cout` 的 streambuf，和 `<<` 同生共死。所以工程上有个简单决策——**只要你的程序里关了 sync，混用时一律用 `print(std::cout, ...)`，别用裸 `print(...)`**。这样既拿到了 `print` 的格式化便利，又不会和 `cout` 打架。

## print 的两副面孔：FILE* 与 ostream

上一节的解法能成立，关键在于 `print` 有两套写流的重载。这件事容易被忽略，值得单独拎出来讲清楚，因为它直接决定了你能不能「换条路绕坑」。

```cpp
// Standard: C++23
#include <iostream>
#include <sstream>
#include <print>

int main()
{
    // 一副面孔:FILE*(stdout/stderr/自己 fopen 的文件)
    std::print(stdout, "写 stdout: {}\n", 1);
    std::println(stderr, "写 stderr: {}", 2);   // stderr 无缓冲,适合日志/错误

    // 另一副面孔:ostream(cout/cerr/stringstream/任何 ostream)
    std::ostringstream os;
    std::print(os, "写 stringstream: {} = {}\n", "x", 3.14);
    std::println(os, "第二行");
    std::print("{}", os.str());   // 把攒下来的内容一次性吐到 stdout

    // 当然也能直接喂 cout
    std::print(std::cout, "直接 print 到 cout: {}\n", 7);
    return 0;
}
```

```text
写 stderr: 2
写 stdout: 1
写 stringstream: x = 3.14
第二行
直接 print 到 cout: 7
```

两套重载对应两个不同的底层路径：

- `print(FILE*, …)` 走 C 的 `fwrite`，经 C stdio 缓冲。`stdout` 是块缓冲（重定向时）/ 行缓冲（终端时），`stderr` 无缓冲。
- `print(ostream&, …)` 走 ostream 的 `streambuf`，和 `<<` 共用缓冲。写到 `ostringstream` 就能在内存里攒字符串，写到 `cout` 就和 `<<` 同缓冲（上一节的救法）。

工程上怎么选？三句话：

- **纯新代码、性能优先**：直接 `print(...)` / `println(...)`，走 `stdout`，最干净最快。
- **要和存量 `cout` 代码混、且关了 sync**：用 `print(std::cout, ...)`，保证顺序。
- **要往内存里攒格式化结果**（比如构造日志、序列化）：用 `print(oss, ...)` 写到 `ostringstream`，比 `oss << std::format(...)` 少一次中间字符串构造。

## print 到 stderr 与缓冲语义

`print` 默认写 `stdout`，但日志和错误信息更常见的目标是 `stderr`。这点 `print` 也直接支持，而且有个天然的便利：`stderr` 是**无缓冲**的，每次写入立刻落地，所以拿 `println(stderr, ...)` 打日志，不用担心程序崩了日志还卡在缓冲里没刷出来。

但要注意，这个「无缓冲」的好处只在 `stderr` 上成立。`print` 写 `stdout` 时是**块缓冲**的（重定向到文件/管道时），它**不会因为遇到 `\n` 就刷新**——这和 `std::endl`（会 flush）的语义不一样。来个对比就很直观：

```cpp
// Standard: C++23
#include <print>
#include <cstdlib>

int main()
{
    std::print("第一行(带换行)\n");
    std::print("第二行没换行就崩了");
    std::abort();   // 模拟崩溃
}
```

直接在终端跑，和在管道里跑，结果不一样：

```text
=== 终端(行缓冲)输出 ===
[exit=134]                          # 整段都没出来,abort 前缓冲没刷
=== 重定向到管道(块缓冲)输出 ===
[done]                              # 同样什么都没有
```

两种情况下，`print` 攒在缓冲里的内容都因为 `abort()`（它不刷用户态缓冲，直接 `_exit`）丢了——包括第一行那个带 `\n` 的。这说明 `print` 在 `stdout` 上**不随换行刷新**，要靠程序正常退出时统一刷。如果你的程序可能异常退出（崩溃、`_exit`、被信号杀掉），想保证关键日志落地，要么写到 `stderr`（无缓冲），要么在关键点手动 `std::fflush(stdout)`。

::: warning print 不像 endl 那样随换行 flush
`std::cout << ... << std::endl` 里的 `endl` 会顺手 flush，所以很多人误以为「输出换行就会刷缓冲」。`print` **没有这个行为**——它写 `stdout` 是块缓冲，`\n` 只是个普通字符。需要强制刷的时候，自己 `std::flush` 对应流，或干脆把关键输出丢到无缓冲的 `stderr`。崩溃丢日志的 bug，根子十有八九在这里。
:::

## 编译器支持与 feature-test

`std::print` / `std::println` 是 C++23 特性，头文件 `<print>`。本机 GCC 16.1.1 完整支持，feature-test macro 看一眼就清楚：

```cpp
// Standard: C++23
#include <print>

int main()
{
#ifdef __cpp_lib_print
    std::println("__cpp_lib_print = {}", __cpp_lib_print);
#endif
#ifdef __cpp_lib_format
    std::println("__cpp_lib_format = {}", __cpp_lib_format);
#endif
    // println() 无参重载:标准里算 C++26,但 cppreference 注明
    // "all known implementations make them available in C++23 mode"
    std::print("password");
    std::println();   // 只输出换行
    return 0;
}
```

```text
__cpp_lib_print = 202406
__cpp_lib_format = 202304
password
```

几个点提醒一下：

- `__cpp_lib_print` 在 GCC 16 上是 `202406`，这个值对应的其实是 C++23 的缺陷报告（DR），把「无缓冲格式化输出」和对更多可格式化类型的支持回填到了 C++23。所以你看到 `202406` 不代表要用 C++26 编——`-std=c++23` 就能拿到。
- 无参 `std::println()`（只输出换行）按标准是 C++26 加的，但主流实现（GCC/Clang/MSVC）都在 C++23 模式下就提供了，本机实测 `g++ -std=c++23` 直接能编能跑。要严格可移植就写 `std::print("\n")`，不挑这个细节。
- 老 GCC（13 之前）没有 `<print>`。如果你的代码要在老工具链上编，要么 `#ifdef __cpp_lib_print` 包起来退化到 `std::cout << std::format(...)`，要么引入 {fmt} 库。

## Unicode 输出：print 多干的一件事

`print` 还承担了一件 `printf` 不管的事：Unicode 终端适配。cppreference 里它的等价实现分两条路——如果普通字面量编码是 UTF-8，走 `vprint_unicode`；否则走 `vprint_nonunicode`。这个分流不是摆设：Windows 控制台历史上默认代码页不是 UTF-8（老的是 GBK/CP437），`print` 在内部做了转换，保证 UTF-8 内容在 Windows 终端也能正确显示，而不是吐出一堆乱码。Linux/macOS 终端原生 UTF-8，这条路径基本是透传。

实测本机（普通字面量编码就是 UTF-8）：

```cpp
// Standard: C++23
#include <print>

int main()
{
    std::println("中文测试: 你好世界 π ≈ 3.14");
    std::println("emoji: 🚀 ✓ ★");
    return 0;
}
```

```text
中文测试: 你好世界 π ≈ 3.14
emoji: 🚀 ✓ ★
```

这点对跨平台代码挺重要——同样是输出 Unicode，`printf` 在 Windows 上要你自己 `SetConsoleOutputCP(CP_UTF8)` 再折腾，`print` 把这层封装好了。但要注意前提是**源文件字面量真的是 UTF-8 编码**（编译期 `vprint_unicode` 路径才会生效）；如果你的源文件是 GBK 又想走 Unicode 路径，得自己保证编码一致性，`print` 帮不了你把非 UTF-8 字面量「变」成 UTF-8。

## 小结

把 `print` 这一套收一下：

- **定位**：`print`/`println` 是 C++23 给 `format` 配的「直接输出」层，绕开 iostream 的 `sync_with_stdio` 与 locale 开销，直接写 C 流。
- **性能**：实测 200 万条短行写 `/dev/null`，`printf` 约 130 ms 最快，`print` 约 175 ms，`cout(sync=false)` 约 155–170 ms，`cout(sync=true)` 默认最慢约 185 ms。`print` 不是绝对冠军，但「不开 sync、带类型安全」这一档它最划算。
- **同步坑**：`print(stdout)` 与 `cout` 默认通过 `sync_with_stdio(true)` 协调，顺序正确；一旦 `sync_with_stdio(false)`，两者缓冲不通气，输出顺序错乱（重定向到管道/文件时稳定复现）。解法：混用时改用 `print(std::cout, ...)`，复用 `cout` 的 streambuf，顺序就对回来。
- **双重载**：`print(FILE*, …)` 走 C stdio 缓冲，`print(ostream&, …)` 走 ostream streambuf。写日志走 `stderr`（无缓冲），攒字符串走 `ostringstream`。
- **缓冲语义**：`print` 写 `stdout` 是块缓冲，**不随 `\n` 刷新**（和 `endl` 不同）；程序异常退出（abort/_exit/信号）会丢缓冲区里没刷的内容。关键日志要么进 `stderr`，要么手动 flush。
- **编译器支持**：GCC 16.1.1 `-std=c++23` 完整支持，`__cpp_lib_print = 202406`（C++23 DR）。无参 `println()` 标准是 C++26 但主流实现 C++23 模式即提供。老工具链（GCC<13）无 `<print>`，退化到 `cout << format(...)`。
- **Unicode**：UTF-8 字面量编码下走 `vprint_unicode`，在 Windows 非 UTF-8 终端自动做转换，跨平台输出 Unicode 比 `printf` 省心。

下一篇我们换到文本处理的另一条线，看 `format` 库更深的玩法——自定义类型的 formatter 特化、ranges/pair/tuple 的格式化，把 `print`/`format` 这套从「能用」推到「能定制」。

## 参考资源

- [cppreference: std::print (C++23)](https://en.cppreference.com/w/cpp/io/print) —— `print` 的 `FILE*` 重载、与 `stdout`/`vprint_unicode` 的等价关系、feature-test macro
- [cppreference: std::println (C++23)](https://en.cppreference.com/w/cpp/io/println) —— `println` 的各重载、无参版本与 C++26 的关系
- [cppreference: std::print(std::ostream) (C++23)](https://en.cppreference.com/w/cpp/io/basic_ostream/print) —— `ostream` 重载，复用 streambuf、与 `<<` 同缓冲
- [cppreference: sync_with_stdio](https://en.cppreference.com/w/cpp/io/ios_base/sync_with_stdio) —— iostream 与 C stdio 同步开关的语义与性能影响
