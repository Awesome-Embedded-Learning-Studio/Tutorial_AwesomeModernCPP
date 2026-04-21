---
title: "string_view 内部原理：非拥有字符串视图"
description: "理解 string_view 的实现机制、与 SSO 的对比和构造来源"
chapter: 8
order: 1
tags:
  - host
  - cpp-modern
  - intermediate
difficulty: intermediate
platform: host
cpp_standard: [17]
reading_time_minutes: 15
prerequisites:
  - "Chapter 0: 右值引用"
related:
  - "string_view 性能分析"
  - "string_view 陷阱与最佳实践"
---

# string_view 内部原理：非拥有字符串视图

笔者最近在写一个 IniParser 项目的时候，跟字符串打交道打得快要吐了——split、trim、substr，各种操作满天飞。每次用 `std::string` 做子串操作都意味着一次堆分配，解析一个配置文件下来，堆上的碎片比笔者的桌面还乱。后来笔者认真研究了一下 `std::string_view`，才发现 C++17 给我们准备了一个这么好用的工具。不过用好它的前提是真正理解它的内部机制，否则很容易踩到生命周期的坑——这个我们留到下一篇陷阱篇再细说。

这篇文章我们聚焦在 `string_view` 的内部原理上：它到底长什么样，为什么这么轻量，跟 `std::string` 的本质区别在哪里，以及它提供了哪些操作。

> **学习目标**
>
> - 完成本章后，你将能够：
> - [ ] 理解 `string_view` 的内部表示（指针 + 长度）
> - [ ] 区分"视图"和"拥有"两种语义
> - [ ] 掌握 `string_view` 的构造来源和核心成员函数
> - [ ] 了解与 `const std::string&` 参数的本质区别

## string_view 到底是个什么东西

`std::string_view`（C++17）是一个轻量、不可变的"字符串视图"类型。关键词是"视图"——它**不拥有**字符缓冲区，只保存两样东西：指向字符序列起始处的指针，以及这段序列的长度。所以你看，这个名字起得非常直白：它就是一个"view"，一个观察窗口，而不是数据的拥有者。

> 参考：[cppreference -- std::basic_string_view](https://en.cppreference.com/w/cpp/string/basic_string_view.html)

### 内部表示：两个字段搞定一切

虽然 C++ 标准没有规定具体的内部结构，但所有主流实现（libstdc++、libc++、MSVC STL）用的都是同一个方案——两个字段的简单结构：

```cpp
template<class CharT, class Traits = std::char_traits<CharT>>
class basic_string_view {
    const CharT* _ptr;   // 指向底层字符序列（不拥有）
    size_t       _len;   // 长度（不含 '\0'）
};
```

就这两个字段，一个指针一个长度。复制一个 `string_view` 只是复制这两个字——在 64 位系统上就是 16 字节。没有堆分配，没有引用计数，没有析构逻辑。这就是它轻量的根本原因。

### 与 std::string 的关系：视图 vs 拥有

理解 `string_view` 最关键的一步，是搞清楚"视图"和"拥有"的区别。`std::string` 是一个拥有者：它在堆上分配内存来存储字符，负责这块内存的生命周期管理，包括构造、拷贝、移动和最终释放。你可以把它类比为"这套房子是我买的，房产证上写我的名字"。

而 `string_view` 是一个观察者：它不分配任何内存，只是指了指别人的数据说"我看看这个"。就像是你朋友买了房子，你拿着钥匙去串门——你能用客厅、厨房，但房子不是你的，哪天朋友把房子卖了（底层 `string` 被销毁了），你手里的钥匙就废了。

这种设计带来的直接好处是：任何"子串操作"都不需要开辟新内存。比如 `substr` 只是把指针往后挪、把长度缩短，复杂度是 O(1)。而 `std::string::substr` 需要分配新内存并拷贝字符，复杂度是 O(n)。这个差异在频繁做子串操作的场景（比如解析器、协议处理）中会非常明显。

我们用代码直观对比一下 `string_view` 和 `std::string` 在子串操作上的行为差异。`string_view::substr` 的实现大致等价于：

```cpp
string_view substr(size_t pos, size_t count) const {
    return string_view(_ptr + pos, min(count, _len - pos));
}
```

完全没有开辟新内存，仅调整指针和长度。而 `std::string::substr` 必须走一遍完整的分配-拷贝流程。假设我们要处理一个 1MB 的配置文件，对其中的每个字段都做一次 `substr`，可能有几千次调用——用 `std::string` 就是几千次堆分配，用 `string_view` 就是几千次指针调整。差距不言而喻。

除了 `substr`，`find`、`compare`、`rfind` 等查询操作也直接基于 `_ptr` 指向的内存遍历（依赖 `Traits::compare`），不涉及新内存创建。`string_view` 的设计哲学可以用一句话概括：它是一个 lightweight facade（轻量外壳），把任意字符序列变成"可操作的只读字符串对象"，但永远不负责内存。这既是它最大的优势，也是所有风险的根源——毕竟不负责清理，就要有人负责，而那个人就是你，程序员。

### SSO：Small String Optimization

说到 `std::string` 的开销，就不得不提 SSO（Small String Optimization）。主流 `std::string` 实现都采用了 SSO 策略：当字符串足够短（通常 15-22 字节，取决于实现）时，字符数据直接存放在对象内部的缓冲区里，不需要堆分配。只有当字符串超过这个阈值时，才会切换到堆分配模式。

SSO 是一个很棒的优化——短字符串的拷贝变得很便宜。但它并不能消除所有开销。一个 `std::string` 对象本身就有 32 字节左右的大小（包含 SSO 缓冲区、长度、容量等信息），而且它的拷贝语义意味着即使触发 SSO，也需要把字符数据逐字节复制一遍。相比之下，`string_view` 只有 16 字节，拷贝永远是两个字的 memcpy，不管字符串有多长。

这个对比不是说 `string_view` 比 `std::string` 好——它们解决的是不同的问题。`std::string` 管理所有权，`string_view` 提供只读视图。在需要修改字符串或持有字符串副本的场景下，`std::string` 仍然是唯一选择。

### 与 const char* 的本质对比

如果我们把视角再拉远一点，`string_view` 的设计其实在概念上是对 `const char*` 的封装。如果说 `std::string` 封装了 `char[]`（带所有权），那 `string_view` 封装的就是 `const char*`（不带所有权，但多了长度信息）。这个"多了长度信息"看起来是一个小改动，但它带来的实际影响非常大。

`const char*` 获取长度需要调用 `strlen`，这是一次 O(n) 的遍历。更糟糕的是，如果你的函数内部多次用到字符串长度，而又不主动缓存它，就会反复调用 `strlen`，不知不觉就变成了 O(n^2) 的性能模式。而 `string_view` 把长度直接存在对象里，`size()` 是 O(1) 的——就是一个成员变量的读取。

另外一个常被忽视的问题是，`const char*` 只能表示以 `\0` 结尾的字符串。这意味着它无法正确处理包含零字节的二进制数据，也无法在不修改原始数据的情况下表示子串（因为子串末尾不一定有 `\0`）。`string_view` 用显式长度解决了这两个问题：它可以指向任意字节序列（包括中间有 `\0` 的），也可以安全地表示任意子区间。

| 特性 | `std::string_view` | `const char*` |
|------|---------------------|---------------|
| 是否包含长度 | 有 `size()`，O(1) | 没有，需要 `strlen`，O(n) |
| 表示子串是否安全 | 完整支持（有长度） | 只能通过临时修改 `\0` 或传递额外长度 |
| 是否支持含零字符的序列 | 可以（长度独立） | 不行，依赖 NUL 终止 |
| 高级接口（查找、比较） | 丰富的成员函数 | 几乎没有，只能用 C 函数 |
| 字面量写法 | `"abc"sv` | `"abc"` |

核心区别总结成一句话：`string_view = (指针, 长度)`，`const char* = 指针 + 隐含以 '\0' 终止`。`string_view` 的显式长度是一个巨大的优势，因为在很多场景下，`\0` 并不是我们的意图。

## 构造来源：从哪里来

我们今天的实验环境如下：Linux 系统，GCC 13 或 Clang 17 以上版本，编译选项 `-std=c++17 -O2`。所有代码示例都可以直接编译运行。

`string_view` 可以从多种来源构造。最常见的是以下三种：

`string_view` 可以从多种来源构造。最常见的是以下三种：

第一种是从 C 风格字符串字面量构造。字符串字面量的存储是静态的（放在可执行文件的 data 段），所以 `string_view` 指向它是安全的，生命周期覆盖整个程序运行期：

```cpp
std::string_view sv = "hello, world";
// sv 指向静态存储区的字符串字面量，永远有效
```

第二种是从 `std::string` 构造。`std::string` 提供了到 `string_view` 的隐式转换操作符，所以你可以直接传值：

```cpp
std::string str = "hello";
std::string_view sv = str;  // 隐式转换
// sv 指向 str 的内部缓冲区，只要 str 还活着就安全
```

⚠️ 这里有一个经典陷阱：如果 `str` 是一个临时对象，那 `sv` 就会指向被销毁的内存——也就是悬垂引用。比如 `std::string_view sv = std::string("temp");` 就是未定义行为。这个问题我们会在陷阱篇详细讨论。

第三种是从指定区间构造，手动传入指针和长度：

```cpp
const char* buf = "hello, world";
std::string_view sv(buf, 5);  // 只看前 5 个字符："hello"
```

这种方式灵活性最高，也是很多解析器内部使用的构造方式。你甚至可以指向一个包含 `\0` 的缓冲区中间某段——因为 `string_view` 用长度来界定边界，不依赖 `\0` 结尾。

C++17 还提供了字面量后缀 `sv`，可以直接写 `"hello"sv` 得到一个 `std::string_view`。这个后缀定义在 `std::literals::string_view_literals` 命名空间中：

```cpp
using namespace std::literals::string_view_literals;
auto sv = "hello"sv;  // std::string_view
```

## 与 const std::string& 参数的区别

很多教程会告诉你"用 `string_view` 替代 `const std::string&` 做函数参数"。这句话大体是对的，但我们需要理解两者的具体差异，才能在正确的场景做出正确的选择。

`const std::string&` 做参数时，调用者必须提供一个 `std::string` 对象。如果调用者手头只有一个 `const char*` 或字符串字面量，编译器会隐式构造一个临时的 `std::string`——这涉及一次可能的堆分配和拷贝。而 `string_view` 做参数时，无论是 `std::string`、`const char*` 还是字符串字面量，都能直接构造 `string_view`，代价只是复制一个指针和一个长度。

```cpp
// 方式一：const string& 参数
void process_old(const std::string& s);

process_old(std::string("temp"));  // 构造 string → 传引用
process_old("literal");            // 隐式构造临时 string → 传引用 → 临时对象析构
process_old(some_c_string);        // 隐式构造临时 string → strlen + 可能的分配

// 方式二：string_view 参数
void process_new(std::string_view sv);

process_new(std::string("temp"));  // 从 string 隐式构造 view → 无额外分配
process_new("literal");            // 直接构造 view → 零分配
process_new(some_c_string);        // 直接构造 view → 只是 O(1) 的 strlen
```

你会发现，`string_view` 版本避免了不必要的临时 `std::string` 构造。在调用频繁的热路径函数中，这种差异会累积出可观的性能收益。不过有一个反向的差异：`const std::string&` 保证数据是以 `\0` 结尾的（因为来源一定是 `std::string`），而 `string_view` 不保证。如果你的函数内部需要调用 C API（比如 `printf("%s", ...)`），那 `string_view` 反而可能给你挖坑。

## 核心成员函数一览

了解了原理之后，我们来看看 `string_view` 提供了哪些操作。

### 元素访问

`operator[]` 和 `at()` 用于按索引访问字符。`operator[]` 不做边界检查（release 模式下），`at()` 会做边界检查并在越界时抛 `std::out_of_range`。`data()` 返回指向底层字符序列的指针。`size()` 和 `length()` 返回字符数量，`empty()` 判断是否为空。

```cpp
std::string_view sv = "hello";

char c = sv[1];         // 'e'，无边界检查
char d = sv.at(1);      // 'e'，有边界检查
const char* p = sv.data();  // 指向 'h' 的指针
std::size_t n = sv.size();  // 5
bool e = sv.empty();        // false
```

⚠️ `data()` 的返回值**不保证**以 `\0` 结尾。如果 `string_view` 是通过 `substr` 或 `remove_suffix` 生成的，那 `data()` 指向的缓冲区末尾很可能没有 `\0`。把 `data()` 直接传给要求 NUL 终止的 C API 是一个常见 bug 来源。如果确实需要 NUL 终止的字符串，必须显式构造 `std::string(sv)`。

### 修改视图本身

`string_view` 提供了三个修改自身的操作——注意，修改的是"视图"本身（即指针和长度），而不是底层的数据。这些操作都是 O(1) 的，因为只是调整两个字段：

```cpp
std::string_view sv = "hello, world";

// remove_prefix：把视图的起始位置向后移动 n 个字符
sv.remove_prefix(7);   // sv 变成 "world"

// remove_suffix：把视图的末尾向前缩短 n 个字符
std::string_view sv2 = "hello, world";
sv2.remove_suffix(7);  // sv2 变成 "hello"

// swap：交换两个 string_view 的内容
std::string_view a = "first";
std::string_view b = "second";
a.swap(b);  // a -> "second", b -> "first"
```

`remove_prefix` 和 `remove_suffix` 在解析器中特别有用。比如你要跳过固定前缀、去掉末尾的分隔符，直接调用这两个函数就行了，不需要创建新的 `string_view` 对象。

我们来看一个稍微完整一点的解析场景：从一个 `"key=value"` 格式的字符串中提取 key 和 value。这在配置文件解析、HTTP header 解析中非常常见。

```cpp
#include <string_view>
#include <iostream>
#include <optional>
#include <utility>

/// @brief 从 "key=value" 格式的字符串中提取键值对
/// @param entry 输入字符串视图，如 "host=localhost"
/// @return 成功返回 (key, value) pair，失败返回 std::nullopt
std::optional<std::pair<std::string_view, std::string_view>>
parse_kv(std::string_view entry) {
    auto pos = entry.find('=');
    if (pos == std::string_view::npos) {
        return std::nullopt;
    }
    auto key = entry.substr(0, pos);
    auto value = entry.substr(pos + 1);
    // 去掉前后空白
    while (!key.empty() && key.front() == ' ') {
        key.remove_prefix(1);
    }
    while (!key.empty() && key.back() == ' ') {
        key.remove_suffix(1);
    }
    while (!value.empty() && value.front() == ' ') {
        value.remove_prefix(1);
    }
    while (!value.empty() && value.back() == ' ') {
        value.remove_suffix(1);
    }
    if (key.empty()) {
        return std::nullopt;
    }
    return std::make_pair(key, value);
}

int main() {
    const char* raw = "  host = localhost ; port = 8080 ";
    std::string_view input(raw);
    // 手动按 ';' 分割，逐个解析键值对
    while (!input.empty()) {
        auto semi = input.find(';');
        auto segment = (semi == std::string_view::npos)
                           ? input
                           : input.substr(0, semi);
        auto result = parse_kv(segment);
        if (result) {
            std::cout << "key=[" << result->first << "] "
                      << "value=[" << result->second << "]\n";
        }
        if (semi == std::string_view::npos) {
            break;
        }
        input.remove_prefix(semi + 1);
    }
    return 0;
}
```

运行结果：

```text
key=[host] value=[localhost]
key=[port] value=[8080]
```

注意这里的关键操作：我们用 `remove_prefix` 来逐段消费输入字符串，用 `substr` 来提取不包含分隔符的片段，用 `remove_prefix` / `remove_suffix` 来做 trim。整个过程对原始数据零拷贝——`string_view` 只是反复调整指针和长度。在解析器的热路径上，这种模式可以显著减少内存分配次数。

但同样要注意：这个例子中 `raw` 是一个 `const char*` 字面量，生命周期覆盖整个程序。如果 `raw` 来自一个 `std::string` 局部变量，那函数返回后所有 `string_view` 都会悬空。这就是笔者反复强调的——理解生命周期是使用 `string_view` 的第一要义。

## 实战：手写一个简单的 token 分割器

说了这么多原理，我们用一个实际例子来感受一下 `string_view` 的用法。下面是一个按分隔符分割字符串的函数：

```cpp
#include <string_view>
#include <vector>
#include <iostream>

std::vector<std::string_view> split(std::string_view input, char delim) {
    std::vector<std::string_view> tokens;
    while (true) {
        auto pos = input.find(delim);
        if (pos == std::string_view::npos) {
            if (!input.empty()) {
                tokens.push_back(input);
            }
            break;
        }
        tokens.push_back(input.substr(0, pos));
        input.remove_prefix(pos + 1);  // 跳过分隔符
    }
    return tokens;
}

int main() {
    std::string line = "name=Alice;age=30;city=Beijing";
    auto tokens = split(line, ';');
    for (auto tk : tokens) {
        std::cout << "[" << tk << "]\n";
    }
    return 0;
}
```

运行结果：

```text
[name=Alice]
[age=30]
[city=Beijing]
```

注意看 `split` 函数内部的逻辑：我们反复调用 `remove_prefix` 来推进视图的起始位置，用 `substr` 来提取每个 token。整个过程中没有任何堆分配（除了 `vector` 本身的增长），所有操作都是 O(1) 的指针调整。如果用 `std::string` 来实现，每次 `substr` 都会分配新内存——对于一个简单的 INI 文件解析器来说，这种开销完全是不必要的。

⚠️ 返回的 `string_view` 向量指向的是原始 `line` 的内部缓冲区。如果 `line` 被销毁了，这些 `string_view` 全部悬空。在实际项目中，你可能需要用 `std::string` 拷贝这些 token，或者在文档中明确标注返回值的生命周期约束。

## 嵌入式实战：命令解析

`string_view` 在嵌入式场景中同样有用。很多嵌入式系统需要通过串口接收文本命令（比如 AT 指令集、自定义调试命令），然后用 `string_view` 来解析这些命令可以避免不必要的字符串拷贝，这在堆内存受限的 MCU 上尤其有价值。

```cpp
#include <string_view>
#include <cstring>

/// @brief 简单的串口命令解析器
/// @param cmd 输入命令视图，如 "LED ON" 或 "PWM 128"
void handle_command(std::string_view cmd) {
    // 去掉末尾的换行符
    while (!cmd.empty() && (cmd.back() == '\r' || cmd.back() == '\n')) {
        cmd.remove_suffix(1);
    }

    // 按空格分割命令和参数
    auto space = cmd.find(' ');
    auto verb = (space == std::string_view::npos) ? cmd : cmd.substr(0, space);

    if (verb == "LED") {
        auto arg = (space == std::string_view::npos)
                       ? std::string_view{}
                       : cmd.substr(space + 1);
        if (arg == "ON") {
            hal_gpio_write(kLedPin, true);
        } else if (arg == "OFF") {
            hal_gpio_write(kLedPin, false);
        }
    } else if (verb == "PWM") {
        auto arg = cmd.substr(space + 1);
        // 将 string_view 转为整数
        int value = 0;
        for (char c : arg) {
            if (c >= '0' && c <= '9') {
                value = value * 10 + (c - '0');
            }
        }
        hal_pwm_set_duty(value);
    }
}
```

这个例子展示了嵌入式场景中 `string_view` 的典型用法：接收一个从串口缓冲区切出来的命令片段，通过 `remove_suffix` 去掉换行符，按空格分割动词和参数，然后做简单的字符串匹配。全程零堆分配——所有操作都是指针和长度的调整。对于只有几十 KB RAM 的 MCU 来说，这种"零分配"的字符串处理方式几乎是唯一可行的选择。

## 小结

`string_view` 的本质就是一个"指针 + 长度"的非拥有视图。它不分配内存，复制代价极低（16 字节），子串操作全是 O(1)。它可以从 `const char*`、`std::string`、字面量等多种来源构造，是函数参数的理想选择。但它不保证 NUL 终止，不管理数据生命周期——这些"不负责"的事情，才是使用时需要格外小心的地方。

理解了这些内部原理之后，下一篇我们就来看看 `string_view` 在性能方面的实际收益，用基准测试数据说话。

## 参考资源

- [cppreference: std::basic_string_view](https://en.cppreference.com/w/cpp/string/basic_string_view.html)
- [cppreference: basic_string_view 构造函数](https://en.cppreference.com/w/cpp/string/basic_string_view/basic_string_view.html)
- [cppreference: data() 说明（不保证 NUL）](https://en.cppreference.com/w/cpp/string/basic_string_view/data.html)
- [cppreference: operator""sv](https://en.cppreference.com/w/cpp/string/basic_string_view/operator%22%22sv.html)
- [cppreference: remove_prefix](https://en.cppreference.com/w/cpp/string/basic_string_view/remove_prefix.html)
