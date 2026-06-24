---
chapter: 7
cpp_standard:
- 11
- 17
- 20
description: 讲透 iostream 的流层级与 streambuf 缓冲、cin/cout/cerr/clog 的默认缓冲差异、sync_with_stdio 与 cin.tie 为什么拖慢真实 benchmark 的一个数量级、流为什么会慢（locale 查找/虚函数/sentry/与 C stdio 同步），以及 failbit/badbit/eofbit 状态机；并用读 100 万 int 的实测画出 cin vs scanf vs from_chars 的速度差距
difficulty: intermediate
order: 55
platform: host
prerequisites:
- 迭代器适配器：反向、插入与流，把现成迭代器改出新行为
- charconv：零开销的数字与字符串互转
reading_time_minutes: 16
related:
- charconv：零开销的数字与字符串互转
- print：C++23 的直接输出与 iostream 解耦
- format：C++20 的类型安全格式化
- fstream：文件流读写、RAII 与它的可移植性坑
tags:
- host
- cpp-modern
- intermediate
- 基础
title: iostream：流抽象与它为什么这么慢
---

# iostream：流抽象与它为什么这么慢

写 C++ 的人大概都听过这么一句"忠告"：`cin` / `cout` 慢，刷算法题先关 `sync_with_stdio`，不然大数据过不去。这句话本身没错，但它把一件很值得讲清楚的事压成了一句口诀——`iostream` 到底慢在哪、关了同步为什么就快了、快完之后还留下了什么坑。这一篇我们就把 `<iostream>` 这套流抽象拆开跑一遍：先看清它的层级和缓冲设计，再用真实 benchmark 量出那个"数量级"差距，最后说清楚什么场景该用它、什么场景该绕开它。

我们会反复回到同一个具体任务上——**从标准输入读一百万个整数，求和**。这件事小到能贴完整代码，又足以让流抽象的每一层开销暴露出来。本机 GCC 16.1.1，`g++ -std=c++20 -O2`，数字都是真实跑出来的，绝对值会因机器波动，我们只关心数量级结论。

## 先把流抽象的层级理清

很多人对 `iostream` 的心智模型就是"`cin` 是输入、`cout` 是输出"，到这里就停了。可一旦你打开 `<iostream>` 的头文件，会看到一整套继承关系。我们把它从底到顶摆一遍，因为后面讲"为什么会慢"时，每一层都贡献了一部分开销：

```text
ios_base          ← 所有流的公共基类：格式标志、locale、状态位
  └─ ios          ← 加上 streambuf 指针和错误处理
       ├─ istream ← 输入：operator>>、get、getline
       └─ ostream ← 输出：operator<<、put、write
            └─ iostream ← 多继承自 istream 和 ostream
```

真正干活的是 `ios` 里挂着的那个 `streambuf` 指针。`istream` / `ostream` 本身只是"格式化和派发"——它们把 `>>` / `<<` 翻译成对字符的读写请求，再把请求转交给底层的 `streambuf`。`streambuf` 才是那个管缓冲、对接真正 I/O 通道（终端、文件、内存块）的角色。你可以把这一层关系理解为：

```text
你的代码  ──>>/<<──►  istream/ostream(格式化 + sentry + locale)
                          │
                          ▼  把字符请求委托下去
                      streambuf(缓冲、实际读写)
                          │
                          ▼
                    真正的 I/O 通道(stdin / 文件 / string)
```

这条链路是 `iostream` 抽象力的来源——同一份 `<<` / `>>` 代码，换个 `streambuf` 就能在屏幕、文件、内存之间无缝切换。但这也是它"慢"的根源之一：**每次 `<<` 都要走完一整条派发链**。我们后面实测会看到这条链到底有多贵。

`<iostream>` 头文件给我们预定义了四个标准流对象，对应 `stdin` / `stdout` / `stderr`：

- `std::cin` —— 绑 `stdin`，`istream`；
- `std::cout` —— 绑 `stdout`，`ostream`，**缓冲**；
- `std::cerr` —— 绑 `stderr`，`ostream`，**不缓冲**（unbuffered），每次 `<<` 都立刻刷出去；
- `std::clog` —— 同样绑 `stderr`，但**带缓冲**，和 `cout` 一样攒着写。

`cerr` 不缓冲这条很关键，我们直接上手验证一下。下面这段代码故意在两次 `cout` 输出中间塞了个 `cerr` 输出和一段 `sleep`，看缓冲行为到底怎么体现：

```cpp
// Standard: C++20
#include <chrono>
#include <iostream>
#include <thread>

int main() {
    std::cout << "[cout] 这一串会先在 cout 的缓冲里待着";
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    // cerr 不缓冲：哪怕 cout 还没 flush，cerr 立刻出去
    std::cerr << "[cerr] 我不缓冲，立刻打到 stderr\n";
    std::cout << " (cout 这一段补完才一起 flush)\n";
    return 0;
}
```

把 stdout 和 stderr 合并到同一个终端看，输出顺序长这样：

```text
[cout] 这一串会先在 cout 的缓冲里待着[cerr] 我不缓冲，立刻打到 stderr
 (cout 这一段补完才一起 flush)
```

注意第一行——`[cout]` 那串本应先发生，却和 `[cerr]` 挤在了同一行；而 `cerr` 那条消息**先于** `cout` 的后半段出现在屏幕上。这就是"cerr 不缓冲、cout 缓冲"的活体证据：`cout` 把 `"这一串..."` 攒在缓冲区里没出去，`cerr` 那条则立刻穿透到 `stderr`，最后程序退出时 `cout` 才连同 `(cout 这一段...)` 一起 flush。所以错误诊断信息默认走 `cerr` 是有道理的——**就算程序在下一行就崩了，错误消息也已经刷出去了**，不会被卡在 `cout` 的缓冲里陪葬。

## sync_with_stdio 和 cin.tie：两个会拖慢真实读写的开关

讲清楚层级之后，我们直接进到这篇文章最实战的部分。`iostream` 默认开了两个"为了安全而拖慢"的机制，算法题里那句"先关 `sync_with_stdio`"关的就是它们俩。

第一个是 `std::ios_base::sync_with_stdio`，默认 `true`。它让 `cin` / `cout` / `cerr` 和 C 标准库的 `stdin` / `stdout` / `stderr` **保持同步**——保证你混用 `std::cin` 和 `scanf`、`std::cout` 和 `printf` 时，读写顺序和"只用一边"时一致。这个保证的代价是：标准库实现得让 `cin` / `cout` 和 C 的 `FILE*` 共享同一套缓冲与位置，最常见的实现方式是**让 `cin` / `cout` 几乎退化成逐字符去走 C stdio**。一逐字符，缓冲就废了一半。

第二个是 `std::cin.tie(&std::cout)`，默认把 `cin` 绑在 `cout` 上。绑定的语义是：**每次从 `cin` 读取之前，先把绑定的 `cout` flush 掉**。这又是为了交互式程序的正确性——典型场景是先 `cout << "Enter x: "` 打提示、再 `cin >> x` 读输入，绑定了就不怕提示还卡在缓冲里没显示，用户就已经被挡在读入上了。代价是：**每次读操作都额外白送一次 `cout` 的 flush**，大量读写时这就是一笔纯浪费。

这两个开关合在一起，对"从 `cin` 大量读"的影响有多大？我们用开头说的那个任务直接量。下面这个小程序从标准输入读一百万个 `int` 求和，`argv[1]` 是 `0` 走默认路径、是 `1` 关掉两个开关：

```cpp
// Standard: C++20
#include <chrono>
#include <cstdio>
#include <iostream>

int main(int argc, char** argv) {
    const bool fast = (argc > 1 && argv[1][0] == '1');
    if (fast) {
        std::ios_base::sync_with_stdio(false);
        std::cin.tie(nullptr);
    }
    auto t0 = std::chrono::high_resolution_clock::now();
    long acc = 0;
    int x;
    while (std::cin >> x) acc += x;
    auto t1 = std::chrono::high_resolution_clock::now();
    std::fprintf(stderr, "mode=%s  time=%.1f ms  sum=%ld\n",
                 fast ? "fast(sync off)" : "default(sync on)",
                 std::chrono::duration<double, std::milli>(t1 - t0).count(), acc);
    return 0;
}
```

喂给它同一份 7.5 MiB、一百万个整数的数据文件，连跑三次：

```text
=== default cin (sync on, tied) ===
mode=default(sync on)  time=176.6 ms  sum=3499993500000
mode=default(sync on)  time=177.8 ms  sum=3499993500000
mode=default(sync on)  time=176.8 ms  sum=3499993500000
=== fast cin (sync off + untie) ===
mode=fast(sync off)  time=41.4 ms  sum=3499993500000
mode=fast(sync off)  time=39.8 ms  sum=3499993500000
mode=fast(sync off)  time=39.8 ms  sum=3499993500000
```

**从 177 ms 掉到 40 ms，4 倍多提速**——这就是那句话的全部实证。两条曲线对得很齐：三次默认都是 176~178 ms，三次 fast 都是 39~42 ms，结论非常稳。

更有意思的是 40 ms 这个数字本身。还记得前面那张派发链图吗？默认状态下，`cin` 因为要和 C stdio 同步，被逼着几乎逐字符去走 `FILE*` 的位置，所以慢。一旦关掉同步，`cin` 自己的那层 `streambuf` 终于能放开手脚用自己的缓冲区批量读，于是速度立刻追了上来——和我们后面马上要测的 `scanf` 持平，甚至略快。换句话说，**关 `sync_with_stdio` 不是施了什么魔法，只是把那条被同步拖累的派发链解开了**。

### 关掉同步之后留下的两个坑

提速是真提速，但这一刀下去也割断了两样东西，不留意就会踩。我们一个个看。

::: warning 别再混用 cin/cout 和 scanf/printf
关掉 `sync_with_stdio` 之后，`cin` / `cout` 走自己的缓冲，`scanf` / `printf` 走 C 的 `FILE*` 缓冲，这两套缓冲**互不知道对方的存在**，输出的先后顺序不再有保证。下面这段代码源代码顺序是 `printf 1`、`cout 2`、`printf 3`、`cout 4`：

```cpp
// Standard: C++20
#include <cstdio>
#include <iostream>

int main(int argc, char** argv) {
    if (argc > 1) std::ios_base::sync_with_stdio(false);  // 传参 = 关同步
    std::printf("[printf] 1\n");
    std::cout << "[cout]   2\n";
    std::printf("[printf] 3\n");
    std::cout << "[cout]   4\n";
    return 0;
}
```

开同步（默认）跑出来，四行老老实实按源代码顺序：

```text
[printf] 1
[cout]   2
[printf] 3
[cout]   4
```

关掉同步再跑（连跑多次结果一致），顺序整个错乱——两套缓冲各自攒着各自 flush：

```text
[cout]   2
[cout]   4
[printf] 1
[printf] 3
```

规律很直白：`cout` 的两行被它自己的缓冲攒到一起、`printf` 的两行被 C 缓冲攒到一起，谁的缓冲先满 / 先被 flush 谁先出去。所以那条铁律是——**关了 `sync_with_stdio` 之后，整个程序要么全用 `cin` / `cout`，要么全用 `scanf` / `printf`，不要混**。如果你确实需要混用又怕乱序，C++23 的 `std::print(std::cout, ...)` 是个干净出路（见本卷 [53-print](../strings/53-print.md)）。
:::

::: warning 关掉 tie 后交互式提示要自己 flush
`cin.tie(nullptr)` 关掉的是"读之前自动 flush `cout`"。在批处理场景里这是纯赚的——没有提示要打，每次读白 flush 一次纯属浪费。但如果你写的是交互式程序，习惯性地这样写：

```cpp
std::cout << "Enter x: ";   // 提示没换行，也不手 flush
std::cin >> x;
```

在默认 `tie` 下，`cin >> x` 会先把 `cout` 刷掉，用户就能在键盘前看到 `Enter x:` 再输入。可一旦你为了"提速"顺手 `cin.tie(nullptr)`，这个自动 flush 就没了，提示可能卡在 `cout` 缓冲里迟迟不显示，用户面对一个黑屏等输入，体验直接拉胯。结论：**`tie` 该不该关，取决于你是不是真的在读前有 `cout` 提示要刷**。纯数据吞吐就关，交互就留。
:::

## 一次性看清：iostream 到底为什么慢

到现在我们都在拿 `sync` / `tie` 说事，可就算把这两个开关都关掉，`cin` / `cout` 还是比裸的 `from_chars` 慢上一截。我们这就把这条派发链上**每一个贵的地方**都点出来，你会理解为什么 `iostream` 即便"优化过"也快不到哪里去：

**locale 查找。** `>>` / `<<` 默认要按当前 locale 来格式化——比如整数里的千分位分隔符、浮点的小数点、布尔值的 `true` / `false` 文本，都受 locale 影响。哪怕你什么都不配，走 C locale 也得查一遍。我们在这卷的 [51-charconv](../strings/51-charconv.md) 里详细对比过，`charconv` 砍掉 locale 后能快好几倍，开销就藏在这。

**虚函数派发。** `istream` / `ostream` 把 `>>` / `<<` 实现成对 `streambuf` 虚函数的调用（`sputc` / `sbumpc` / `xsputn` 之类），`streambuf` 又是抽象类，具体走哪个实现要运行期决定。编译器很难把这条链完全内联优化掉，每次 `<<` 都背着一层间接调用。

**sentry 对象。** 这是很多人不知道的一层。标准规定，`>>` / `<<` 的每一次调用，进入时都要先构造一个 `sentry` 对象——它负责检查流状态、对 `streambuf` 加锁（保证多线程下一次 `<<` 是原子的）、做前置准备，析构时再收尾。也就是说，**你看到的每一次 `<< x`，底下都对应一次 sentry 构造 + 析构**。一次两次无所谓，一百万次循环里这就是实打实的开销。这也是为什么"把多个 `<<` 拼成一次调用"（比如用 `std::format` 先拼好再一次性 `<<`）能比"连写十个 `<<`"快——sentry 少构造几次。

**与 C stdio 同步。** 也就是前面 `sync_with_stdio` 那一节讲的，默认开启、把标准流逼成逐字符走 C `FILE*`，量级差距最大的一刀。

**格式解析。** `>>` / `<<` 不是单纯搬运字节，它还要做"跳前导空白、识别符号、按宽度截断、拼成整数"这一整套解析；`<<` 反过来要把整数格式化成字符。这本来是必要的活，但 `iostream` 把这套活和上面的 locale、虚函数、sentry 全捆在了一起，每读一个数都全走一遍。

把这些加起来，`iostream` 慢就不神秘了——**它不是某一个点慢，而是每一层都贡献了一点**。换来的好处也很实在：类型安全（编译期就知道你在 `<<` 一个 `int`，不会像 `printf` 那样类型对不上就未定义行为）、自动扩展（自定义类型重载 `operator<<` 就能塞进任何 `ostream`）、和异常/RAII 体系无缝配合。这就是为什么它不会、也不该被"优化掉"——它贵在抽象，抽象的账总得有人付。

## 把三个方案放一起：cin vs scanf vs from_chars

讲到这里，最该回答的问题来了：面对"读一百万个 int"这种活，我们到底该用谁？我们一次性把三条路放在同一份数据上跑：默认 `cin`、关了同步的 `cin`、C 的 `scanf`、以及 `fread` 把整个文件 slurp 进内存后再用 `from_chars` 解析。后者是最"暴力"的快路径——绕开所有流抽象，直接读字节、直接解析。

`scanf` 和 `fread + from_chars` 两条路径的代码核心分别长这样：

```cpp
// Standard: C++20
// 路径 A：scanf，直接走 FILE* 缓冲
long acc = 0;
int x;
while (std::scanf("%d", &x) == 1) acc += x;

// 路径 B：fread 把 stdin 整块读进内存，再 from_chars 逐个解析
std::vector<char> buf;
{ char chunk[1 << 16]; size_t n;
  while ((n = std::fread(chunk, 1, sizeof(chunk), stdin)) > 0)
      buf.insert(buf.end(), chunk, chunk + n); }
const char* first = buf.data();
const char* last  = buf.data() + buf.size();
long acc2 = 0;
while (first < last) {
    while (first < last && (*first == ' ' || *first == '\n')) ++first;  // from_chars 不跳前导空白，自己跳
    if (first >= last) break;
    int y;
    auto r = std::from_chars(first, last, y);
    if (r.ec != std::errc{}) break;
    acc2 += y;
    first = r.ptr;
}
```

四条路径都喂同一份一百万整数的数据文件，时间取多次运行的最小值（绝对值随机器波动，只看数量级）：

```text
cin   (sync on,  默认)     ~177 ms
scanf                      ~59 ms
cin   (sync off + untie)   ~40 ms
fread + from_chars         ~18 ms
```

这几个数字放一起，结论非常清楚：

- **默认 `cin` 是四条里最慢的**——因为它要和 C stdio 同步，逐字符走 `FILE*`，连 `scanf` 都跑不过它。
- **`scanf` 大约 59 ms**，比默认 `cin` 快 3 倍。它直接用 C 的 `FILE*` 缓冲，没有 `iostream` 那条派发链，也不付 sentry 的钱。
- **关掉同步的 `cin` 大约 40 ms**，反超 `scanf` 一点点。这说明 `iostream` 的派发链本身**并不比 C stdio 慢**——一旦把"同步"这个枷锁去掉，它自己的 `streambuf` 缓冲同样高效。
- **`fread + from_chars` 大约 18 ms**，再快一倍多。这条路径把缓冲（`fread` 一次一大块）和解析（`from_chars` 无 locale、无异常、无分配）都压到了最低开销，是性能敏感场景的正确归宿。`from_chars` 为什么能这么快，[51-charconv](../strings/51-charconv.md) 里有专门的拆解。

::: warning 一个容易误读的对比
有人会拿"内存里 `std::stringstream >>`"和"内存里 `sscanf`"对比，然后下结论说 `iostream` 比 `scanf` 快/慢。这里要小心：**`sscanf` 在内存字符串上表现极差**（本机实测可以慢到几十秒级别），因为它的某些实现对剩余缓冲会做重复扫描，这和它走 `FILE*` 时的行为完全两回事。所以请把"读标准输入"作为公平战场——也就是上面这张表——别拿内存里的 `sscanf` 当代表，那会得到误导性的结论。
:::

一句话收口：**`sync_with_stdio(false) + cin.tie(nullptr)` 能让 `cin` / `cout` 追平 `scanf` / `printf` 这一档；但真要榨性能，快路径是 `from_chars`（输入）和 `std::print` / `std::format_to`（输出），`iostream` 这一层的开销始终在那里**。

## 流的状态机：failbit / badbit / eofbit

聊完性能，我们把 `iostream` 另一个容易让人翻车的机制讲透——它的错误状态。每个流内部有三个状态位：

- `goodbit`（其实是 0）——一切正常；
- `failbit` —— 上一次操作**因为格式原因失败**了（比如想读 `int` 却碰到了 `"hello"`），流本身没坏，清掉状态能继续用；
- `badbit` —— 流**真的出问题了**（底层 I/O 错误、缓冲损坏这种），这种通常不可恢复；
- `eofbit` —— 读到了末尾。

最关键的认知是：**一旦 `failbit` 或 `badbit` 被置位，后续的 `>>` / `<<` 全部变成空操作**——流会拒绝工作，直到你 `clear()` 把状态重置。我们用一段代码把这套状态机活跑一遍，从字符串流里依次读 `int`、`int`、`int`，但中间夹了一个 `"hello"`：

```cpp
// Standard: C++20
#include <iostream>
#include <sstream>
#include <string>

int main() {
    std::istringstream iss("42  hello  99");
    int x;

    iss >> x;   // 正常读到 42
    std::cout << "读到 " << x
              << "  good=" << iss.good() << " fail=" << iss.fail()
              << " eof=" << iss.eof() << " bool(iss)=" << static_cast<bool>(iss) << '\n';

    iss >> x;   // 想读 int，却碰到 hello —— failbit 置位，x 不变
    std::cout << "格式不匹配后: good=" << iss.good()
              << " fail=" << iss.fail()
              << " bool(iss)=" << static_cast<bool>(iss) << '\n';

    int y = -999;
    iss >> y;   // 流处于 fail 状态，这次 >> 是空操作，y 不变
    std::cout << "y 还是 " << y << "，因为流在 fail 状态下 >> 被忽略\n";

    iss.clear();   // 清掉 failbit，"hello" 仍在缓冲里等着
    std::string s;
    iss >> s;      // 用 string 把 "hello" 消化掉
    iss >> x;      // 继续读到 99
    std::cout << "clear() 之后: s=" << s << " x=" << x << '\n';

    // 读到末尾再读：eofbit 和 failbit 一起置位
    iss >> x;
    std::cout << "读到末尾后: eof=" << iss.eof()
              << " fail=" << iss.fail() << '\n';
    return 0;
}
```

跑出来的状态变化：

```text
读到 42  good=1 fail=0 eof=0 bool(iss)=1
格式不匹配后: good=0 fail=1 bool(iss)=0
y 还是 -999，因为流在 fail 状态下 >> 被忽略
clear() 之后: s=hello x=99
读到末尾后: eof=1 fail=1
```

这条状态机有几个实战要点：

**`operator bool`（以及 `operator!`）是判断流能不能用的统一入口。** 标准库给了流一个到 `bool` 的隐式转换，它等价于 `!fail()`——也就是只要 `failbit` 或 `badbit` 没置位，就当 `true`。这正是循环里那种惯用法的根基：

```cpp
while (iss >> x) sum += x;   // >> 返回流本身，流再转 bool
```

`>> x` 返回的是 `istream&`（也就是流自己），它再隐式转 `bool`：读到有效数据就继续，读到末尾（`eofbit` 会连同 `failbit` 一起置位）或格式错误就退出。这种写法比"先 `>>`、再判 `eof()`"干净也安全得多——**单纯判 `eof()` 是经典坑**，因为它只在"读过了末尾"之后才置位，最后一次读到的数据可能是半成品。

::: warning clear() 之后缓冲里的"坏字符"还在
`clear()` 只重置状态位，**不会动缓冲区里那个导致失败的字符**。所以上面例子里 `clear()` 完，`"hello"` 仍卡在流的读取位置，下次 `>> int` 还是会立刻失败。处理办法是要么像示例那样用一个 `std::string` 把它读走，要么 `iss.ignore(...)` 跳过一段。很多人 `clear()` 之后发现"还是读不出来"，原因十有八九就是这个。
:::

**`badbit` 和 `failbit` 的区别要分清。** `failbit` 是"这次读不出 `int`，但你清一下状态还能救"；`badbit` 是"流坏了，别挣扎了"。交互式解析里遇到坏数据，正确套路通常是：`clear()` + `ignore()` 跳过坏字段，继续往后读。终端 / 管道断开之类导致的底层错误才会真的进 `badbit`，那种情况通常该直接退出。

## 什么时候该用 iostream，什么时候别用

讲了这么多 iostream 的不是，得把话说公道。它不是该被消灭的工具，而是该用在正确场景的工具。

**该用 `iostream` 的场景：**

- **简单交互、命令行小工具。** 几行 `cout << "..." << x` 配 `cin >> x`，类型安全、可读性好、自定义类型重载一下 `<<` 就能直接打印，这种场合开发效率远比那点 I/O 开销重要。
- **调试日志。** 尤其是走 `std::cerr` / `std::clog`——错误和诊断信息要的是"立刻刷出去"和"不被缓冲吞掉"，这恰恰是 `cerr` 不缓冲的设计意图，性能根本不是这里的主诉求。
- **需要类型安全、又不想引 `printf` 那套未定义行为风险的地方。** `printf("%d", x)` 里 `x` 类型对不上就是未定义行为，编译器不一定报；`std::cout << x` 类型错直接编译失败。

**不该用 `iostream` 的场景：**

- **性能敏感的大量数字读写。** 协议解析、序列化、CSV / JSON 解析、算法题大数据点。这条路径的正确归宿是 `from_chars` / `to_chars`（[51-charconv](../strings/51-charconv.md)），几十倍的差距不是省一点的事。
- **需要类型安全又需要格式串表达力的输出。** 这种诉求在 C++20 之后有了更好的答案——`std::format`（[52-format](../strings/52-format.md)）和 C++23 的 `std::print` / `std::println`（[53-print](../strings/53-print.md)）。`print` 直接写流、不经过 `<<` 派发链，本卷 53 那篇实测过它对 `cout` 的数量级优势。
- **需要二进制、随机访问、mmap 的大文件读写。** 这是文件流的活，归 [56-fstream](56-fstream.md) 那一篇；本篇聚焦标准流，这里只提一句：`fstream` 在大文件随机读写上同样不是性能工具，真要快得换 `mmap` 或 C 的 `stdio`。

一条决策主线：**iostream 是"安全且方便"的默认值，不是"快"的默认值**。一旦你开始为它的速度写 work-around（关同步、解绑、`<< '\n'` 不用 `endl`），通常就意味着你该换工具了，而不是继续在这个抽象层里挤性能。

## 小结

把 `<iostream>` 这一趟的关键结论收一下：

- **层级**：`ios_base` → `ios` → `istream` / `ostream` → `iostream`；真正干活、管缓冲的是挂着的 `streambuf`，`<<` / `>>` 只负责格式化和把请求派发下去。
- **四个标准流**：`cout` / `clog` 缓冲，`cerr` 不缓冲（每次 `<<` 立刻刷）——所以错误诊断默认走 `cerr`，不怕崩在缓冲里。
- **两个性能开关**：`sync_with_stdio(false)` 解开与 C stdio 的同步（默认拖累 `cin` 逐字符走 `FILE*`）、`cin.tie(nullptr)` 省掉每次读前的 `cout` flush。实测读一百万 int，从 177 ms 掉到 40 ms，**约 4 倍提速**。
- **关同步的代价**：别再混用 `cin` / `cout` 和 `scanf` / `printf`（顺序会乱，实测 `printf 1 cout 2` 能打出 `cout 2 / cout 4 / printf 1 / printf 3`）；交互式提示要自己 flush。
- **为什么慢**：locale 查找 + 虚函数派发 + 每次 `<<` 的 sentry 构造 + 与 C stdio 同步 + 格式解析，每一层都贡献一点，贵在抽象而非某单点。
- **横向对比（读 100 万 int，本机 GCC 16.1.1）**：`cin` 默认 ~177 ms、`scanf` ~59 ms、`cin` 关同步 ~40 ms、`fread + from_chars` ~18 ms。关了同步的 `cin` ≈ `scanf`，但 `from_chars` 再快一倍多。
- **状态机**：`goodbit` / `failbit` / `badbit` / `eofbit`；`fail` 或 `bad` 一置位，后续 `>>` / `<<` 全部空操作，要 `clear()` 才能恢复，但 `clear()` 不动缓冲里的坏字符（得 `ignore` 或读走）。
- **选型**：简单交互、调试日志、类型安全优先的小工具——用 `iostream`；大量数字读写——`charconv`；要格式串表达力的类型安全输出——`format` / `print`；二进制大文件——`fstream` / `mmap`。

下一篇我们进文件流——`fstream` 的三类文件流、`open` 模式、RAII 自动 `close` 的生命周期坑，以及大文件读写为什么也该换工具。

## 参考资源

- [cppreference: iostream](https://en.cppreference.com/w/cpp/header/iostream) —— 标准流对象 `cin` / `cout` / `cerr` / `clog` 与头文件总览
- [cppreference: std::ios_base::sync_with_stdio](https://en.cppreference.com/w/cpp/io/ios_base/sync_with_stdio) —— 同步开关的语义与"关掉后不保证顺序"
- [cppreference: std::basic_streambuf](https://en.cppreference.com/w/cpp/io/basic_streambuf) —— 底层缓冲抽象
- [cppreference: std::basic_istream::sentry](https://en.cppreference.com/w/cpp/io/basic_istream/sentry) —— 每次 `>>` 构造的 sentry 对象
- [cppreference: std::basic_ios](https://en.cppreference.com/w/cpp/io/basic_ios) —— `fail` / `bad` / `eof` / `clear` / `operator bool` 状态机
