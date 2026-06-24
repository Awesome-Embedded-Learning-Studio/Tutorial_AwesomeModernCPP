---
chapter: 7
cpp_standard:
- 17
- 20
description: 讲透 std::string_view——指针加长度的只读字符视图、sizeof 体积对比、零拷贝传参省掉
  char* 构造临时 string 的堆分配、悬垂是最大坑(remove_prefix/substr 物化才拷贝),以及 C++23 的 contains 与平凡可拷贝
difficulty: intermediate
order: 50
platform: host
prerequisites:
- string 深入：SSO、COW 与 resize_and_overwrite
- span：非拥有的连续视图
reading_time_minutes: 12
related:
- span：非拥有的连续视图
- string 深入：SSO、COW 与 resize_and_overwrite
tags:
- host
- cpp-modern
- intermediate
- 类型安全
title: string_view：非拥有的只读字符串视图
---

# string_view：非拥有的只读字符串视图

上一篇讲 `span` 的时候，我们把"非拥有视图"这层概念铺开了：一个对象只存指针和长度，不分配、不负责释放，拷贝它几乎不要钱。这一篇我们看它的字符表亲——`std::string_view`。

两者形神都像，但定位差着一层：`span<T>` 通吃任意元素类型，可读可写；`string_view` 专门给字符序列，**只读**、带字符串语义。它从 C++17 进标准，几乎一夜之间就把"只读字符串传参"这件事的老做法推翻了。我们先从它最朴素的内部表示讲起，把"为什么这么设计"和"怎么用对它"一起跑通。

## 内部表示：就一对（指针, 长度）

`string_view` 内部就两样东西：一个 `const CharT*` 指向首字符，一个 `size_t` 记录字符数。不分配、不拥有、不拷贝底层——这和 `span` 一模一样，区别只在"元素类型锁死是字符、且永远 const"。

所以它的体积是确定的：64 位平台上就是两个 8 字节字，共 16 字节。我们跑跑看，顺手和 `std::string` 对比：

```cpp
// Standard: C++20
#include <string>
#include <string_view>
#include <iostream>

int main()
{
    std::string s = "hello";
    std::string_view sv = s;
    std::cout << "sizeof(std::string)      = " << sizeof(std::string) << '\n';
    std::cout << "sizeof(std::string_view) = " << sizeof(std::string_view) << '\n';
    std::cout << "sizeof(void*)            = " << sizeof(void*) << '\n';
    std::cout << "sizeof(size_t)           = " << sizeof(size_t) << '\n';
    return 0;
}
```

`g++ -std=c++20 -O2`（本机 GCC 16.1.1，x86_64）跑出来：

```text
sizeof(std::string)      = 32
sizeof(std::string_view) = 16
sizeof(void*)            = 8
sizeof(size_t)           = 8
```

`string` 是 32 字节，`string_view` 只有它的一半——16 字节，正好是指针 + size。`string` 那 32 字节里塞了什么（SSO 缓冲、容量、长度、堆指针）我们放在 [04-string-memory-deep-dive](../containers/04-string-memory-deep-dive.md) 里讲透了，这里只需要记住结论：`string` 自己是一个"有状态、会分配、有 SSO"的重量级对象，`string_view` 是一个"两个字、不分配"的轻量视图。拷贝一个 `string_view`，就是拷贝那两个字，几乎不要钱。

::: warning 它天生只读
`string_view` 内部存的是 `const CharT*`，没有非 const 的版本。想改字符？没门——它就是个窗口，只能看，不能动。要可写，用 `span<char>`。
:::

## 零拷贝传参：这是它存在的最大理由

`string_view` 最值钱的一手，是替代 `const std::string&` 做只读字符串传参。听起来像是"换了个差不多的东西"，实际差距大得很——尤其是在调用方手里是个 `char*` / 字符串字面量的时候。

我们看一个最小对比。两个函数干同一件事（数元音字母），签名一个用 `const string&`，一个用 `string_view`：

```cpp
// Standard: C++20
long count_vowels_ref(const std::string& s) { /* 逐字符数 a/e/i/o/u */ }
long count_vowels_sv(std::string_view sv)   { /* 同上 */ }
```

当调用方手里是一段足够长的 `char*`（超过 SSO 阈值，放不进 `string` 的小对象缓冲），`const string&` 这条路会发生什么？**编译器得先拿这个 `char*` 构造一个临时 `std::string`**——意味着一次堆分配、一次拷贝——再把那个临时量的引用传进去。函数返回，临时量销毁，堆释放。而我们本来只想"只读地扫一遍"。

`string_view` 这条路则干净利落：直接拿 `char*` 和长度包一个 16 字节的视图传进去，不分配、不拷贝。

口说无凭，我们用全局 `operator new` 数一下堆分配的次数，让它原形毕露：

```cpp
// Standard: C++20
#include <string>
#include <string_view>
#include <iostream>
#include <new>

static int g_alloc_count = 0;
void* operator new(std::size_t n)
{
    ++g_alloc_count;
    std::cout << "  [alloc " << n << " bytes]\n";
    return std::malloc(n);
}
void operator delete(void* p) noexcept { std::free(p); }

void take_ref(const std::string& s) { (void)s; }
void take_sv(std::string_view sv)   { (void)sv; }

int main()
{
    const char* long_s = "01234567890123456789034567890123456789";  // 超过 SSO

    std::cout << "--- const string& x3 (long char*) ---\n";
    take_ref(long_s); take_ref(long_s); take_ref(long_s);

    std::cout << "--- string_view x3 (long char*) ---\n";
    take_sv(long_s); take_sv(long_s); take_sv(long_s);
    return 0;
}
```

跑出来：

```text
--- const string& x3 (long char*) ---
  [alloc 39 bytes]
  [alloc 39 bytes]
  [alloc 39 bytes]
--- string_view x3 (long char*) ---
```

证据很直白：`const string&` 路径**每次调用都分配一次**（三次调用 = 三次 `alloc`，39 = 38 个字符 + 空终止符），`string_view` 路径**一次都不分配**。这就是零拷贝传参的含金量——它省掉的是临时 `string` 的构造和析构，而不是省掉那点引用的间接。

把这条放到一个紧循环里跑个数量级。同一个长 payload（90 字节，稳超 SSO），五千万次调用：

```cpp
// Standard: C++20（节选,完整版见下方 benchmark 说明）
static const char* kPayload =
    "The quick brown fox jumps over the lazy dog - a non-trivial string payload.";

long count_vowels_ref(const std::string& s) { /* ... */ }
long count_vowels_sv(std::string_view sv)   { /* ... */ }

int main()
{
    constexpr int kIters = 50'000'000;
    volatile long sink = 0;
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < kIters; ++i) sink += count_vowels_ref(kPayload);
    auto t1 = std::chrono::steady_clock::now();
    for (int i = 0; i < kIters; ++i) sink += count_vowels_sv(kPayload);
    auto t2 = std::chrono::steady_clock::now();
    /* 打印两段耗时 */
}
```

`g++ -std=c++20 -O2` 连跑两遍，本机结果：

```text
const string& path (char* arg): 1820 ms
string_view  path (char* arg): 1360 ms
ratio (ref/sv): 1.34x
```

想自己跑一遍看比率？点开下面这个在线示例（在线运行约 2 秒，会打印两段耗时和比值）：

<OnlineCompilerDemo
  title="零拷贝传参：const string& vs string_view"
  source-path="code/examples/vol3/50_string_view_benchmark.cpp"
  description="90 字节 payload、5000 万次调用：const string& 每次构造临时 string，string_view 零分配——实测 string_view 快约 35%"
  allow-run
/>

`const string&` 比 `string_view` 慢约 34%。微秒绝对值会随机器波动，但"省掉临时 string 的分配/释放"带来的这档差距是稳健的——payload 越长（越容易超 SSO）、调用越频繁，差距越明显。需要提醒的是：如果调用方手里本来就是 `std::string`，`const string&` 直接绑上去不会构造临时量，两者就扯平了。`string_view` 的传参优势**专门**兑现给"只读、来源异构、来源是 `char*` / 字面量 / 子串"的场景。

也正是这个原因，现代 API 接受只读字符串时越来越倾向于写 `string_view`：它同时能接 `std::string`、`char*`、字面量、另一个 `string_view`，调用方都不用改。这正是当年 `span` 想统一"一段 T"传参时，字符这边早已被 `string_view` 提前办妥的事。

## remove_prefix / remove_suffix / substr：视口操作，O(1)

既然是个视图，那"调整看哪一段"就应该是廉价的。`string_view` 配了三件套，全是 **O(1)**、全是调整视口、不拷贝底层：

- `remove_prefix(n)` —— 起点向后挪 n，相当于砍掉前 n 个字符；
- `remove_suffix(n)` —— 终点向前挪 n，相当于砍掉后 n 个字符；
- `substr(pos, count)` —— 返回一个新的 `string_view`，指向 `[pos, pos+count)`，还是不拷贝。

这套东西在解析场景里特别顺手。比如解析一个 URL，切成 scheme / host / path 三段，全程零拷贝：

```cpp
// Standard: C++20
#include <string>
#include <string_view>
#include <iostream>

int main()
{
    std::string url = "https://example.com/path/to/file";
    std::string_view sv{url};

    std::string_view scheme = sv;
    scheme.remove_prefix(8);              // 跳过 "https://"
    std::cout << "after remove_prefix(8): " << scheme << '\n';

    std::string_view host = scheme;
    auto slash = host.find('/');
    if (slash != std::string_view::npos) {
        host.remove_suffix(host.size() - slash);   // 截到第一个 '/'
    }
    std::cout << "host: " << host << '\n';

    std::string_view path = sv.substr(8 + host.size());   // "/path/to/file"
    std::cout << "path: " << path << '\n';
    return 0;
}
```

跑出来：

```text
after remove_prefix(8): example.com/path/to/file
host: example.com
path: /path/to/file
```

三段视图全部指向原 `url` 那块内存，没动一个字节。这就是"视图"该有的样子——和 `span` 的 `subspan` / `first` / `last` 是一个模子刻出来的。

## 物化才拷贝：从 view 构造 string 不免费

用着用着你会想：`string_view` 这么省，那我到底什么时候才会真的拷贝？答案是**当你把它物化成 `std::string` 的那一刻**。

```cpp
std::string_view path = /* 某段视图 */;
std::string owned = std::string{path};   // 这里发生拷贝:分配 + 逐字符复制
```

从 `string_view` 构造 `std::string` 是一次完整的拷贝——标准库要分配一块新内存，把视图里的字符一个个复制过去。这不是 bug，这是必然：`string` 是所有者，要拥有自己的副本，就得真的把数据拿过来。

实践中这意味着什么？一个常见误用是"为了'统一接口'全用 `string_view`，然后在函数里 `std::string{sv}` 转回去存进容器或成员"——这么转一道，零拷贝的好处全没了，反而多了一次间接。**`string_view` 的正确用法是：一路只读传递，直到真的需要所有权那一刻才物化，而且只物化一次。** 如果你发现一个值在函数里被反复物化，那它本来就该是个 `std::string`，不是 `string_view`。

## 最大的坑：它不持有，会悬垂

`string_view` 最致命的坑，是它"非拥有"性质的必然代价——**它不管底层活多久**。底层活着它就有用，底层没了，它就是一根指向已释放内存的野指针，访问即未定义行为。

最经典的姿势，是返回一个函数内 `string` 的视图。函数结束、`string` 析构、视图悬垂：

```cpp
// Standard: C++20
#include <string>
#include <string_view>
#include <iostream>

std::string_view bad_return()
{
    std::string local = "hello world";
    return std::string_view{local};   // local 在此销毁,返回的 view 立刻悬垂
}

int main()
{
    auto sv = bad_return();
    std::cout << "sv (dangling, UB): " << sv << '\n';
    std::cout << "sv.size(): " << sv.size() << '\n';
    return 0;
}
```

`g++ -std=c++20 -O2` 跑出来（注意：这是 UB，你的输出可能不同，甚至看起来"正常"——这正是它可怕的地方）：

```text
sv (dangling, UB): �h�2
sv.size(): 11
```

`size()` 还是 11，因为长度在 `string_view` 构造时就抄进了对象，`local` 析构不碰它；但底层那 11 个字符的内存已经还给堆了，`operator<<` 去读就读出垃圾。

更隐蔽的姿势是**拼接临时量**。`s + "x"` 会产生一个临时 `string`，把 view 绑上去，语句一结束临时量就没了：

```cpp
// Standard: C++20
std::string s = "abc";
std::string_view sv = s + "x";   // 临时 string 销毁,sv 悬垂
std::cout << sv << '\n';         // UB
```

光这么写，因为临时量的内存在栈帧里还没被冲掉，有时甚至打印出"abcx"——看着没事，实际是定时炸弹。我们往里面塞几次新分配，把那块缓冲冲掉，破绽就露出来了：

```cpp
// Standard: C++20
int main()
{
    std::string s = "abc";
    std::string_view sv = s + "x";          // 悬垂
    for (int i = 0; i < 3; ++i) {
        std::string noise(64, char('A' + i));
        std::cout << "noise: " << noise << '\n';
    }
    std::cout << "sv: [" << sv << "] size=" << sv.size() << '\n';
    return 0;
}
```

`g++ -std=c++20 -O0` 跑出来：

```text
noise: AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
noise: BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB
noise: CCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCC
sv: [@   ] size=4
```

`size` 还显示 4，但 `sv` 的内容已经变成 `@` 加一堆空白——那块内存被 `noise` 系列的分配覆盖了。

::: warning 别靠"看着没事"骗自己
上面 `sv = s + "x"` 在 `-O2` 下经常打印出"正常"的 `abcx`，因为临时量的栈槽还没被复用。**这是 UB，不是"能跑"**。换成 `g++ -std=c++20 -O1 -fsanitize=address` 再跑同样这段，ASan 会立刻把它按在地上：
:::

```text
==535629==ERROR: AddressSanitizer: stack-use-after-scope on address 0x...
READ of size 4 at 0x... thread T0
    #2 in std::operator<< <char, ...>(..., std::basic_string_view<char, ...>)
    #3 in main /tmp/sv_concat2.cpp:14
```

`stack-use-after-scope`——临时量出了作用域还去读它的数据。ASan 这种工具就是用来戳破"看着没事"的 UB 的。涉及 `string_view` 的生命周期怀疑，开 `-fsanitize=address` 跑一遍，比肉眼靠谱得多。

这一类坑的根因全在一条铁律：**`string_view` 的生命周期不得超过它指向的数据**。只要你不把它绑到临时量、不把它存得比底层久、不从返回函数内局部 `string` 的视图，它就是安全的。

## C++20 / C++23：补上的几个小接口

`string_view` 本身从 C++17 落地后，后续标准往它身上补了几个小而实的接口，我们逐个在 GCC 16.1.1 上验证一下。

C++20 给了 `starts_with` / `ends_with`，语义一目了然：

```cpp
// Standard: C++20
std::string_view sv = "hello world";
sv.starts_with("hello");   // true
sv.ends_with("world");     // true
```

C++23 给了 `contains`，把过去 `find(x) != npos` 那套啰嗦写法一行解决：

```cpp
// Standard: C++23
sv.contains("lo wo");   // true
sv.contains("xyz");     // false
```

跑出来：

```text
starts_with("hello"): true
ends_with("world"):   true
contains("lo wo"):    true
contains("xyz"):      false
```

C++23 还把"平凡可拷贝"从"所有实现早就这么干"提升成了标准硬要求。我们验证一下：

```cpp
// Standard: C++23
std::cout << std::is_trivially_copyable_v<std::string_view>;   // 1
std::cout << __cpp_lib_string_contains;                        // 202011
```

```text
is_trivially_copyable_v<string_view> = true
__cpp_lib_string_contains = 202011
```

这一条"平凡可拷贝"的实际意义在于：`string_view` 可以安全地跨二进制边界传、用 `memcpy` 搬、塞进共享内存，编译器对它的拷贝可以放开手脚优化。这是它适合做"只读传参的通用货币"的底层资格。

::: warning 关于"range-for 上临时 view 的悬垂"
网上有些资料把"range-based for 遍历一个返回临时 view 的表达式"说成 C++23 被修掉了——这个说法不准确。`string_view` 本身不持有数据，遍历一个**绑定到临时 string 的 view**，数据照样会随临时量析构而消失，标准并没有、也无法用语言层把它"救回来"。真正能帮你的是**工具链诊断**：开 `-fsanitize=address`、`-Wdangling`（GCC/Clang）这类静态/运行期检查。C++23 给 `string_view` 补的是 `contains` 和平凡可拷贝要求这类**接口和类型**层面的东西，不是生命周期。生命周期那条红线，自始至终得你自己守。
:::

顺带一提，C++23 还给 `string_view` 补了从任意 contiguous range 构造的能力（P1989），所以 `std::vector<char>` 能直接喂给接受 `string_view` 的函数，不必再 `.data()` + `.size()` 手搓。C++26 还在路上的是 `subview`（返回子视图，和 `substr` 类似但更贴合 ranges 风格），GCC 16.1.1 暂未落地，等正式发布再说。

## 小结

把 `string_view` 拆到这一步，它的全貌就清楚了——一个**指针加长度的只读字符视图**，价值在传参、坑在悬垂。几条关键结论收一下：

- **内部表示**：一对 `const CharT*` + `size_t`，64 位上 16 字节，是 `std::string`（32 字节）的一半；不分配、不拥有，拷贝就是拷贝两个字。
- **零拷贝传参**：替代 `const string&` 接收只读字符串，最大的赢面在调用方手里是 `char*` / 字面量时——省掉一次临时 `string` 的堆分配。调用方手里本就是 `string` 时两者扯平。
- **视口操作**：`remove_prefix` / `remove_suffix` / `substr` 全是 O(1)、全不拷贝；底层该多长还多长，只是看的窗口变了。
- **物化才拷贝**：从 `string_view` 构造 `std::string` 是一次完整拷贝。正确用法是一路只读传递，直到真正需要所有权时才物化一次。
- **最大坑是悬垂**：返回函数内 `string` 的视图、绑拼接临时量（`s + "x"`）、存得比底层久，都是 UB。"看着没事"不代表没问题，开 `-fsanitize=address` 验证最稳。
- **C++20/23**：`starts_with` / `ends_with`（C++20）、`contains`（C++23）、平凡可拷贝硬要求（C++23）都已就绪；生命周期那条红线没被"修掉"，靠的是你自己守 + 工具链诊断。

和它的姊妹 `span` 一句话分清：处理任意类型、可能可写的数据用 `span<T>`；处理只读字符序列用 `string_view`。一个面向字节、一个面向字符，机制同源，分工明确。

## 参考资源

- [cppreference: std::basic_string_view](https://en.cppreference.com/w/cpp/string/basic_string_view) —— 成员、构造、`remove_prefix`/`substr`/`contains` 全景与各版本标注
- [cppreference: std::basic_string_view::contains (C++23)](https://en.cppreference.com/w/cpp/string/basic_string_view/contains) —— `contains` 与 `__cpp_lib_string_contains` 特性宏
- [P0123 `string_view` 提案家族](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2017/n4618.pdf) —— C++17 落地前的设计动机
- [P1989R2: Range constructor for `string_view`](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2020/p1989r2.html) —— C++23 从 contiguous range 构造 `string_view`
- 本卷 [span：非拥有的连续视图](../containers/08-span.md) —— 同机制的"非拥有视图"姊妹篇，一个字节一个字符
