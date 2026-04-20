---
title: "std::string"
description: "掌握 std::string 的构造、拼接、查找和子串操作，学会在 C++ 中安全高效地处理字符串"
chapter: 5
order: 3
difficulty: beginner
reading_time_minutes: 12
platform: host
prerequisites:
  - "std::array"
tags:
  - cpp-modern
  - host
  - beginner
  - 入门
  - 基础
cpp_standard: [11, 14, 17, 20]
---

# std::string

在上一篇教程里，我们花了大把篇幅和 C 风格字符串搏斗——手动管理 `\0` 终止符、小心翼翼地防止缓冲区溢出、用 `strncpy` 和 `snprintf` 如履薄冰地操作每一段字符数组。如果你跟我一样被这些折腾得够呛，那么接下来这个消息会让你松一大口气：C++ 标准库给我们准备了一个真正的字符串类型，叫做 `std::string`，它自动管理内存、自动处理长度、支持直观的拼接和比较，基本上把我们之前在 C 里踩过的坑全部填平了。

这一章我们从 `std::string` 的构造方式开始，一路走到拼接、查找、子串提取、与 C 字符串的互操作，最后用一个综合的字符串处理程序把所有知识串起来。学完之后你会发现，以前那些让人血压拉满的字符串操作，在 C++ 里可以写得既安全又优雅。

> **学习目标**
>
> 完成本章后，你将能够：
>
> - [ ] 使用多种方式构造 `std::string` 对象
> - [ ] 进行字符串的拼接、插入、删除和替换操作
> - [ ] 掌握 `find`、`substr` 等搜索与子串操作
> - [ ] 在 C++ string 和 C 风格字符串之间正确转换
> - [ ] 使用 `std::to_string` 和 `std::stoi` 等转换函数

## 环境说明

我们接下来的所有实验都在这个环境下进行：

- 平台：Linux x86\_64（WSL2 也可以）
- 编译器：GCC 13+ 或 Clang 17+
- 编译选项：`-Wall -Wextra -std=c++17`

## 第一步——用各种方式构造一个 string

`std::string` 提供了相当丰富的构造函数，覆盖了你能想到的几乎所有场景：

```cpp
// string_construct.cpp
#include <iostream>
#include <string>

int main()
{
    // 从字面量构造
    std::string s1 = "hello";
    // 重复字符：10 个 'x'
    std::string s2(10, 'x');
    // 拷贝构造
    std::string s3(s1);
    // 从另一个 string 的一部分构造（起始位置，长度）
    std::string s4(s1, 1, 3);  // "ell"
    // 用 + 直接拼接构造
    std::string s5 = s1 + " world";
    // 空字符串
    std::string s6;
    // 移动构造（C++11）
    std::string s7 = std::move(s5);

    std::cout << s1 << "\n" << s2 << "\n" << s3 << "\n"
              << s4 << "\n" << s7 << "\n"
              << "s6 empty: " << std::boolalpha << s6.empty() << "\n";
    return 0;
}
```

输出：

```text
hello
xxxxxxxxxx
hello
ell
hello world
s6 empty: true
```

第一种和第五种写法看起来像赋值，但实际上编译器做的是构造——这是 C++ 的拷贝初始化语法，效果和 `std::string s1("hello")` 一样。`std::string s4(s1, 1, 3)` 从 `s1` 的下标 1 开始截取 3 个字符，结果是 `"ell"`——这种"部分构造"在解析字符串的时候非常好用。移动构造我们暂时不用深究，只需要知道它比拷贝更快，因为它把内部资源"偷"了过来而不是复制一份。

> ⚠️ **踩坑预警**
> 移动之后的源对象（上面的 `s5`）处于"有效但未指定"（valid but unspecified）的状态——你可以对它赋值、可以析构它，但不要读取它的值做任何有意义的判断。这是 C++ 移动语义的基本契约，后续章节讲到移动语义的时候我们会详细展开。

## 第二步——基本操作：大小、访问和判空

```cpp
std::string s = "Hello, C++";
s.size();       // 10
s.length();     // 10（和 size 等价）
s.empty();      // false
s[0];           // 'H'
s.at(1);        // 'e'（越界时抛 std::out_of_range）
s.front();      // 'H'
s.back();       // '+'
```

`size()` 和 `length()` 完全等价。大多数 C++ 开发者倾向于 `size()`，因为它和其他标准库容器保持一致。

`operator[]` 和 `at()` 都能通过下标访问字符，区别在于越界行为：`s[100]` 不做任何检查，行为完全未定义；`s.at(100)` 则会抛出 `std::out_of_range` 异常。如果你对边界没有百分之百的把握，用 `at()` 更安全——比起花两小时追查一个内存越界的 bug，这点性能开销根本不算什么。

> ⚠️ **踩坑预警**
> `std::string` 的 `size()` 返回的是底层 `char` 的个数，不是"肉眼看到的字数"。对于纯 ASCII 字符串两者一致，但如果字符串里包含 UTF-8 编码的中文，`std::string s = "你好";` 的 `s.size()` 是 6 而不是 2，因为每个中文字符占 3 个字节。正确处理 Unicode 字符串需要专门的库（比如 ICU），但这个坑一定要提前知道。

## 第三步——拼接、插入、删除与替换

```cpp
std::string s = "Hello";
s += " World";          // "Hello World"
s.append("!!!");        // "Hello World!!!"
s.push_back('?');       // "Hello World!!!?"
s.insert(5, ",");       // "Hello, World!!!?"
s.erase(5, 1);          // "Hello World!!!?"  删掉刚才插入的逗号
s.replace(6, 5, "C++"); // "Hello C++!!!?"    World -> C++
s.clear();              // 变成空字符串
```

`+=` 和 `append()` 功能类似，`+=` 更简洁，`append()` 提供更多重载版本（比如只追加另一个 string 的某一段）。`push_back()` 只能追加单个字符，和 `vector` 的 `push_back()` 接口一致。`insert(pos, str)` 在 `pos` 处插入 `str`，`erase(pos, len)` 从 `pos` 开始删除 `len` 个字符，`replace(pos, len, new_str)` 把从 `pos` 开始的 `len` 个字符替换成 `new_str`，新字符串的长度可以和被替换的部分不同。

这些操作之所以安全，是因为 `std::string` 内部自动管理内存——插入时空间不够会自动扩容，删除时不需要手动移动后面的字符。相比 C 里手动算偏移量、小心翼翼调用 `memmove` 的日子，这简直是天堂。

## 第四步——查找与子串

```cpp
std::string s = "Hello, hello, HELLO!";

s.find("hello");                    // 7（区分大小写）
s.find("Hello");                    // 0
s.find("xyz");                      // std::string::npos
s.find("hello", 2);                 // 7（从位置 2 开始找）
s.rfind("hello");                   // 7（反向查找）
s.find_first_of("aeiou");          // 1（第一个元音字母 e）
s.find_last_of("aeiou");           // 16（最后一个元音字母 O... 不对，是 O 的小写位置）
```

这里最关键的概念是 `std::string::npos`。它是一个常量，值是 `std::size_t` 的最大值。当查找操作没找到目标时返回 `npos`。所以每次调用 `find` 之后，都要检查返回值是否等于 `npos`，而不是拿它当 bool 用——因为 `npos` 转换成 bool 是 `true`，直接写 `if (s.find("x"))` 在没找到的时候反而进入分支，这是另一个经典的新手陷阱。

`find_first_of` 和 `find_last_of` 的行为比较特殊：它们不是查找整个子串，而是查找参数字符串中的**任意一个字符**。`find_first_of("aeiou")` 返回 1，因为 `s[1]` 是 `'e'`，是 `"aeiou"` 中最先匹配到的字符。

子串提取用 `substr(pos, len)`，从位置 `pos` 开始截取 `len` 个字符，返回一个新的 `std::string`。省略 `len` 则取到末尾：

```cpp
std::string t = "Hello, World!";
t.substr(7, 5);  // "World"
t.substr(7);     // "World!"
```

`substr()` 返回的是一个新对象，会分配内存并拷贝字符。如果你只需要遍历某个范围而不需要独立的副本，用 `std::string_view`（C++17）会更高效——这个我们在后续章节再展开。

## 第五步——比较字符串

在 C 里比较两个字符串得用 `strcmp`，C++ 的 `std::string` 重载了比较运算符，直观得多：

```cpp
std::string a = "apple", b = "banana", c = "apple";
a == c;      // true
a != b;      // true
a < b;       // true（字典序）
a.compare(b);  // 负数（等价于 strcmp 的返回值语义）
```

`compare()` 成员函数的优势在于支持部分比较，例如 `s.compare(7, 5, "World")` 拿 `s` 从下标 7 开始的 5 个字符和 `"World"` 比较是否相等。这种能力在解析协议、处理固定格式文本时会用到。

## 第六步——与 C 字符串互操作

不管 `std::string` 有多好用，很多第三方库、操作系统 API、嵌入式 SDK 依然接受 `const char*`。从 `std::string` 拿到 C 风格字符串需要两个关键函数：

```cpp
std::string s = "Hello, C API!";
const char* p = s.c_str();   // 返回以 \0 结尾的 const char*
const char* q = s.data();    // C++17 起与 c_str() 完全等价
```

`c_str()` 保证返回以 `\0` 结尾的 `const char*`，可以直接传给 `fopen`、`printf` 等任何期望 C 字符串的函数。`data()` 在 C++17 起行为和 `c_str()` 完全一致。

这里有一条必须牢记的规则：`c_str()` 和 `data()` 返回的指针**由 string 对象持有**，一旦 string 被修改或销毁，指针就失效了。所以永远不要把 `c_str()` 的返回值存下来然后去做可能改变 string 的操作——先完成所有修改，最后再调用 `c_str()` 传给 C API。

## 第七步——数值转换与行输入

```cpp
// 数值 -> 字符串
std::to_string(42);      // "42"
std::to_string(3.14);    // "3.140000"（注意：用的是 %f 格式）

// 字符串 -> 数值
std::stoi("42");         // int: 42
std::stol("1234567890"); // long: 1234567890
std::stod("3.14159");    // double: 3.14159
std::stoi("  123abc");   // 123（跳过前导空白，遇非数字停止）

// 读取一整行（cin >> s 遇空格就停，getline 会读到换行为止）
std::string line;
std::getline(std::cin, line);
```

`std::to_string` 对浮点数的结果可能不太"漂亮"——`to_string(3.14)` 输出 `3.140000`，因为它用的是 `%f` 格式化。如果你需要精确控制浮点数的输出格式，还是得用 `<iomanip>` 里的 `std::setprecision` 或者 `std::snprintf`。

## 实战演练——综合字符串处理

现在我们把前面学到的所有知识综合起来，写一个稍微有实际意义的字符串处理程序。这个程序演示几种常见的文本处理模式：按分隔符拆分、统计字符频率、查找替换、以及简单的 CSV 解析。

```cpp
// string_demo.cpp
#include <iostream>
#include <map>
#include <string>

/// @brief 把句子按空格拆分成单词，输出每个单词
void split_into_words(const std::string& sentence)
{
    std::cout << "--- 拆分单词 ---" << std::endl;
    std::size_t start = 0;
    std::size_t end = 0;

    while (start < sentence.size()) {
        start = sentence.find_first_not_of(' ', start);
        if (start == std::string::npos) {
            break;
        }
        end = sentence.find(' ', start);
        if (end == std::string::npos) {
            end = sentence.size();
        }
        std::cout << "  [" << sentence.substr(start, end - start) << "]\n";
        start = end + 1;
    }
}

/// @brief 统计每个字符出现的次数（区分大小写）
void count_char_frequency(const std::string& text)
{
    std::cout << "\n--- 字符频率统计 ---" << std::endl;
    std::map<char, int> freq;
    for (char c : text) {
        freq[c]++;
    }
    for (const auto& [ch, count] : freq) {
        std::cout << "  '" << ch << "': " << count << "\n";
    }
}

/// @brief 在 text 中查找所有 target 并替换为 replacement
std::string find_and_replace(std::string text,
                             const std::string& target,
                             const std::string& replacement)
{
    std::cout << "\n--- 查找替换 ---\n  原文: " << text << std::endl;
    std::size_t pos = 0;
    while ((pos = text.find(target, pos)) != std::string::npos) {
        text.replace(pos, target.size(), replacement);
        pos += replacement.size();  // 跳过已替换部分，避免死循环
    }
    std::cout << "  结果: " << text << std::endl;
    return text;
}

/// @brief 简单的 CSV 行解析（不处理引号转义）
void parse_csv_line(const std::string& line)
{
    std::cout << "\n--- CSV 解析 ---\n  输入: " << line << std::endl;
    std::size_t start = 0;
    int idx = 0;
    while (true) {
        std::size_t comma = line.find(',', start);
        if (comma == std::string::npos) {
            std::cout << "  字段 " << idx << ": [" << line.substr(start)
                      << "]\n";
            break;
        }
        std::cout << "  字段 " << idx << ": ["
                  << line.substr(start, comma - start) << "]\n";
        start = comma + 1;
        idx++;
    }
}

int main()
{
    split_into_words("C++ is a powerful and efficient language");
    count_char_frequency("hello world");
    find_and_replace("the cat sat on the mat", "the", "a");
    parse_csv_line("Alice,30,Engineer,New York");
    return 0;
}
```

编译运行：

```bash
g++ -std=c++17 -Wall -Wextra -o string_demo string_demo.cpp
./string_demo
```

输出：

```text
--- 拆分单词 ---
  [C++]
  [is]
  [a]
  [powerful]
  [and]
  [efficient]
  [language]

--- 字符频率统计 ---
  ' ': 1
  'd': 1
  'e': 1
  'h': 1
  'l': 3
  'o': 2
  'r': 1
  'w': 1

--- 查找替换 ---
  原文: the cat sat on the mat
  结果: a cat sat on a mat

--- CSV 解析 ---
  输入: Alice,30,Engineer,New York
  字段 0: [Alice]
  字段 1: [30]
  字段 2: [Engineer]
  字段 3: [New York]
```

我们逐个看一下这几个函数的思路。`split_into_words` 的核心是反复调用 `find_first_not_of` 跳过空白、再用 `find` 定位下一个分隔符，然后 `substr` 截取单词。这个"跳过空白、找到分隔符、截取、循环"的模式在文本处理中非常常见，建议当作固定套路来记。

`count_char_frequency` 用了 `std::map` 来统计频率。`std::map` 内部是排序的，所以输出按字符的字典序排列。这里我们第一次用到了关联容器，不需要理解全部细节，只需要知道它是一个"键-值"对的集合，`[]` 访问时如果键不存在会自动创建默认值（`int` 就是 0）。

`find_and_replace` 展示了一个重要的模式：循环中做 `find` + `replace` 时，每次替换完要把搜索起始位置挪到替换结果之后，否则如果 `replacement` 里包含 `target` 的内容就会陷入死循环。`parse_csv_line` 的逻辑和拆分单词类似，只是分隔符换成了逗号。

## 练习

这三道练习覆盖了 `std::string` 最核心的操作，建议自己动手写完再对照思路检查。

### 练习 1：单词计数器

写一个函数 `count_words(const std::string& s)`，统计字符串中有多少个单词（以空格分隔，忽略连续空格和首尾空格）。提示：可以用循环配合 `find` 和 `find_first_not_of`，也可以数"从空白到非空白的过渡次数"。

### 练习 2：简单查找替换工具

写一个函数 `replace_all(std::string text, const std::string& from, const std::string& to)`，把 `text` 中所有出现的 `from` 替换为 `to`。要求处理 `from` 为空字符串的情况（直接返回原文，否则 `find("")` 会返回 0 导致死循环）。

### 练习 3：trim 函数

写两个函数 `ltrim` 和 `rtrim`，分别去掉字符串开头和末尾的空白字符（空格、`\t`、`\n`），然后组合出一个 `trim` 函数。提示：`ltrim` 用 `find_first_not_of(" \t\n")` 找到第一个非空白字符然后 `substr`；`rtrim` 类似，用 `find_last_not_of`。

## 小结

这一章我们从 C 风格字符串的种种痛点出发，学习了 `std::string` 这个 C++ 标准库为我们提供的字符串类型。让我们回顾一下核心要点：

- `std::string` 自动管理内存，不需要手动分配和释放，从根本上杜绝了缓冲区溢出问题
- 构造方式多样：字面量、重复字符、拷贝、部分截取、`+` 拼接，覆盖了常见的使用场景
- `find` 系列函数和 `substr` 是文本处理的核心工具，`npos` 是"没找到"的哨兵值
- `c_str()` 和 `data()` 提供了与 C API 互操作的桥梁，但要注意指针的生命周期
- `std::to_string` 和 `std::stoi`/`std::stod` 等函数解决了字符串和数值之间的转换需求

到这里，第五章"数组与字符串"的内容就全部结束了。我们从最基础的 C 数组出发，经过了指针运算的底层视角，最后到达了 `std::string` 这个高层抽象。这条路径本身就反映了 C++ 的设计哲学：**底层能力一点不减，但标准库在上层提供了安全又好用的工具**。接下来第六章我们要进入 C++ 面向对象的世界——类和对象。那才是 C++ 真正的舞台。

---

> **难度自评**：如果你对 `find` 返回 `npos` 的检查机制还不太确定，建议回头把"查找与子串"那一节的代码重新敲一遍，特别注意循环中 `pos` 的更新逻辑。字符串操作是后续所有项目的基础，这里多花点时间绝对值得。
