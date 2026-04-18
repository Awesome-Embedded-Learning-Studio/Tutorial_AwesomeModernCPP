---
title: "C 字符串与缓冲区安全"
description: "理解 C 字符串 \\0 终止的内存模型，掌握 string.h 核心函数和 snprintf 安全格式化，识别并防范缓冲区溢出漏洞"
chapter: 1
order: 15
tags:
  - host
  - cpp-modern
  - beginner
  - 入门
  - 基础
difficulty: beginner
platform: host
reading_time_minutes: 20
cpp_standard: [11, 17]
prerequisites:
  - "指针与数组、const 和空指针"
---

# C 字符串与缓冲区安全

C 语言里没有真正的"字符串类型"——这是每一个从 C 转向 C++ 的开发者都会发出的感叹。在 C 的世界里，字符串就是一块以 `\0` 结尾的 `char` 数组，所有的操作都建立在这个约定之上。这个约定简单到令人感动，也脆弱到令人崩溃——你忘记写那个 `\0`，整个程序的行为就是未定义的；你把一个 100 字节的字符串拷进 50 字节的缓冲区，缓冲区后面的内存就被你踩烂了。

历史上无数的安全漏洞，从早期的 Morris Worm 到近年的各种 CVE，追根溯源都指向同一件事：**缓冲区溢出**。这篇教程我们要做的就是把 C 字符串从里到外拆一遍，搞清楚它的本质、掌握安全操作的手法、认识那些经典的陷阱，最终为后面学 C++ 的 `std::string` 打下扎实的底层基础。

> **学习目标**
>
> 完成本章后，你将能够：
>
> - [ ] 理解 C 字符串 `\0` 终止的内存模型
> - [ ] 熟练使用 `string.h` 中的核心字符串和内存操作函数
> - [ ] 掌握 `snprintf` 进行安全的格式化输出
> - [ ] 识别并防范缓冲区溢出漏洞

## 环境说明

我们接下来的所有实验都在这个环境下进行：

- 平台：Linux x86\_64（WSL2 也可以）
- 编译器：GCC 13+ 或 Clang 17+
- 编译选项：`-Wall -Wextra -std=c17`

强烈建议在练习时加上 `-fsanitize=address` 编译选项——AddressSanitizer 能在运行时捕获绝大多数缓冲区越界访问，是 C 字符串操作的安全网。

## 第一步——搞清楚 C 字符串在内存里长什么样

### 它就是数组，多了个 `\0`

C 字符串本质上就是一个 `char` 数组，在有效内容的末尾多放了一个值为 `0` 的字节（`\0`，空字符）。编译器不帮你检查这个终止符是否存在，标准库的字符串函数也不检查——一切都靠你自己维护这个约定。

来看内存里到底是什么样子：

```c
char greeting[] = "Hello";
// 下标：   [0] [1] [2] [3] [4] [5]
// 内容：    'H' 'e' 'l' 'l' 'o' '\0'
// sizeof(greeting) == 6  （包含终止符）
// strlen(greeting) == 5  （不包含终止符）
```text

这里有一个非常容易混淆的点：`sizeof` 和 `strlen` 的区别。`sizeof` 是编译期运算符，返回整个数组占用的字节数，包括 `\0`；`strlen` 是运行时函数，从头开始数字符直到遇到 `\0`，返回的是不含终止符的长度。

来看三种初始化方式的区别：

```c
// 方式一：字符串字面量自动加 \0
char a[] = "Hi";              // sizeof == 3, strlen == 2

// 方式二：逐字符初始化——不会自动加 \0
char b[] = {'H', 'i'};        // sizeof == 2，这不是 C 字符串！

// 方式三：手动加终止符
char c[] = {'H', 'i', '\0'};  // sizeof == 3, strlen == 2，这才是合法的 C 字符串
```text

方式二是一个合法的 `char` 数组，但**不是** C 字符串——把它传给 `strlen` 或 `printf("%s")` 会一直往后读内存，直到碰巧遇到一个 `0` 字节。这就是未定义行为。

> ⚠️ **踩坑预警**
> `sizeof` 和 `strlen` 的混淆是新手最常犯的错误之一。记住：`sizeof` 是编译期算的整个数组大小（含 `\0`），`strlen` 是运行时扫描到 `\0` 的字符数（不含 `\0`）。数组传给函数后退化为指针，`sizeof` 就只返回指针大小了——这时候只能靠 `strlen`。

### 字符串字面量与指针的区别

字符串字面量存储在程序的只读数据段里，修改它是未定义行为：

```c
const char* s = "Hello";   // s 指向只读内存中的 "Hello\0"
// s[0] = 'h';            // 未定义行为！很可能段错误

char t[] = "Hello";        // 数组拷贝，数据在栈上，可以修改
t[0] = 'h';               // 没问题
```text

`const char* s = "Hello"` 让指针指向只读数据段中的字符串，`char t[] = "Hello"` 把字符串内容拷贝一份到栈上的数组里。前者不能修改，后者可以。搞混这两个的话，后面调试起来会非常痛苦。

## 第二步——掌握 string.h 的核心函数

`<string.h>` 是 C 语言字符串和内存操作的核心头文件。我们分三组来看：长度与复制、拼接与比较、内存操作。

### 长度与复制

`strlen` 返回字符串长度（不含终止符），原理是从头到尾逐字节扫描直到找到 `\0`——时间复杂度 O(n)，在循环里反复调用同一个字符串的 `strlen` 是经典的性能浪费。

`strcpy` 把源字符串完整复制到目标缓冲区。问题在于它**完全不管**目标缓冲区有多大——源字符串比目标缓冲区长就溢出。

`strncpy` 是带长度限制的版本，但行为有点微妙：它会复制最多 `n` 个字符。如果 `strlen(src) >= n`，复制完 `n` 个字符就停，**但不会自动追加终止符**。这个行为坑了无数人。

```c
#include <stdio.h>
#include <string.h>

int main(void)
{
    char src[] = "Hello, World!";  // 13 字符 + \0
    char dst[8];

    strncpy(dst, src, sizeof(dst) - 1);  // 最多复制 7 个字符
    dst[sizeof(dst) - 1] = '\0';          // 手动保证终止！

    printf("dst = \"%s\"\n", dst);
    return 0;
}
```text

```bash
gcc -Wall -Wextra -std=c17 str_copy.c -o str_copy && ./str_copy
```text

运行结果：

```text
dst = "Hello, "
```text

这个模式在 C 代码里反复出现：`strncpy` + 手动 `\0` 终止。如果你在某处看到 `strncpy` 但没有紧跟的 `\0` 终止处理，那大概率就是一个隐患。

> ⚠️ **踩坑预警**
> `strncpy` 不保证终止！如果源字符串长度 >= n，它复制完 n 个字符就停，不会自动追加 `\0`。每次使用 `strncpy` 后必须手动在最后一个位置写 `\0`。

### 拼接与比较

`strcat` 把源字符串追加到目标字符串末尾。同样不管目标缓冲区还剩多少空间。`strncat` 是带长度限制的版本，第三个参数 `n` 指的是**最多追加的字符数**，而且 `strncat` 保证会在追加后自动加 `\0`（这点和 `strncpy` 不同）。

```c
char buffer[32] = "Hello";
strncat(buffer, ", World", sizeof(buffer) - strlen(buffer) - 1);
// buffer 现在是 "Hello, World"
```text

`strcmp` 逐字符比较两个字符串，相等返回 `0`。用 `==` 比较两个字符串只比较指针地址，不是内容——这是经典的新手错误。

```c
if (strcmp(cmd, "START") == 0) {
    start_motor();
}
```text

### 内存操作：memcpy、memmove、memset

这三个函数操作原始内存，不关心 `\0` 终止符，按字节计数，处理任何类型的数据。

`memcpy` 从源地址复制 `n` 个字节到目标地址，要求源和目标不重叠。`memmove` 功能相同但正确处理了重叠情况——代价是可能稍慢一点。`memset` 把一块内存的每个字节设为指定值。

```c
#include <stdio.h>
#include <string.h>

int main(void)
{
    int src[] = {1, 2, 3, 4, 5};
    int dst[5];

    // 不涉及重叠，用 memcpy
    memcpy(dst, src, sizeof(src));

    // 在同一数组内移动——涉及重叠，必须用 memmove
    memmove(src + 1, src, 3 * sizeof(int));

    printf("dst: %d %d %d %d %d\n", dst[0], dst[1], dst[2], dst[3], dst[4]);
    printf("src: %d %d %d %d %d\n", src[0], src[1], src[2], src[3], src[4]);
    return 0;
}
```text

运行结果：

```text
dst: 1 2 3 4 5
src: 1 1 2 3 5
```text

> ⚠️ **踩坑预警**
> `memcpy` 处理重叠区域是未定义行为。如果你不确定两块内存是否重叠，直接用 `memmove`——性能差异微乎其微，但安全性天差地别。

## 第三步——用 snprintf 做安全的格式化

`sprintf` 是格式化输出到字符串的函数，但它和 `strcpy` 一样不管目标缓冲区大小。`snprintf` 是它的安全版本，第二个参数指定缓冲区大小，保证不会写入超过这个大小的字节数（包括终止符）。

```c
#include <stdio.h>

int main(void)
{
    char buf[32];
    int value = 42;
    const char* unit = "degrees";

    int written = snprintf(buf, sizeof(buf), "Temperature: %d %s", value, unit);
    printf("Result: \"%s\"\n", buf);
    printf("Written: %d, Buffer size: %zu\n", written, sizeof(buf));

    if (written >= (int)sizeof(buf)) {
        printf("Output was truncated!\n");
    }
    return 0;
}
```text

```bash
gcc -Wall -Wextra -std=c17 snprintf_demo.c -o snprintf_demo && ./snprintf_demo
```text

运行结果：

```text
Result: "Temperature: 42 degrees"
Written: 23, Buffer size: 32
```text

`snprintf` 的返回值非常有用：它返回**如果不截断的话会写入多少个字符**（不含终止符）。如果这个值大于等于缓冲区大小，说明输出被截断了。

在嵌入式开发中，`snprintf` 基本上是构造字符串的唯一推荐方式——日志格式化、传感器数据拼接、通信协议的命令组装，全都应该走 `snprintf`。

## 第四步——理解缓冲区溢出为什么这么危险

到现在我们已经反复提到"缓冲区溢出"了，现在正式拆解它到底是怎么回事。

### 经典溢出场景

缓冲区溢出的本质很简单：往缓冲区里写的数据超过了它的容量，多余的数据溢出到相邻的内存区域，覆盖了不该被修改的数据。在栈上的缓冲区溢出尤其危险，因为函数的返回地址就存在栈帧里——攻击者可以精心构造超长输入来覆盖返回地址，使程序跳转到攻击者指定的代码。Morris Worm 在 1988 年就是利用这种攻击传播的。

```c
#include <stdio.h>
#include <string.h>

void vulnerable_function(const char* user_input)
{
    char buffer[16];
    strcpy(buffer, user_input);  // 如果 user_input 长度 >= 16，溢出！
    printf("You said: %s\n", buffer);
}
```text

### 三道防线

第一道防线：**永远使用带长度限制的函数**。

| 危险函数 | 安全替代 | 说明 |
|----------|----------|------|
| `strcpy` | `strncpy` + 手动终止 | 或改用 `snprintf` |
| `strcat` | `strncat` | 注意第三个参数的含义 |
| `sprintf` | `snprintf` | 优先选择 |
| `gets` | `fgets` | `gets` 已在 C11 中被彻底移除 |
| `scanf("%s")` | `%Ns` 或 `fgets` + `sscanf` | 指定最大宽度 |

第二道防线是编译器选项。`-fstack-protector` 会在栈帧中插入 canary 值，函数返回前检查它是否被篡改。`-D_FORTIFY_SOURCE=2` 让编译器在编译期把不安全函数替换成安全版本。

第三道防线是 AddressSanitizer（`-fsanitize=address`），能精确定位每次越界读写发生的位置。

```bash
# 推荐的开发编译命令
gcc -std=c17 -Wall -Wextra -g -fsanitize=address -fstack-protector-all your_code.c
```text

## C++ 衔接

如果你跟着这篇教程一路敲到这里，大概已经感受到 C 字符串操作的繁琐了——每一个 `strncpy` 后面都要手动加 `\0`，每一次拼接都要计算剩余空间。C++ 通过几个核心组件从根本上解决了这些问题。

`std::string` 在内部维护一个动态分配的字符数组，自动处理 `\0` 终止、内存分配和释放、容量增长。你不需要手动指定缓冲区大小，不需要担心溢出：

```cpp
#include <string>

std::string s1 = "Hello";
std::string s2 = "World";
std::string result = s1 + ", " + s2 + "!";  // 自动扩容
printf("C string: %s\n", result.c_str());    // 和 C API 无障碍交互
```text

`std::string_view`（C++17）不拥有字符串数据，只持有一个指针和长度，本质上是 `(const char*, size_t)` 的封装。传参时零拷贝，兼容 C 字符串和 `std::string`。不过要注意它不拥有数据——指向临时对象的 `string_view` 是经典的悬空引用陷阱。

有了这两个工具之后，`strcpy`、`strcat`、`sprintf`、`strlen` 在 C++ 代码中基本不应该再直接出现。当然，在和 C API 交互、或者在资源极度受限的嵌入式环境中，这些函数仍然是必要的——这也是为什么我们花了一整篇来学它们。

## 常见陷阱

| 陷阱 | 说明 | 解决方法 |
|------|------|----------|
| `strncpy` 不保证终止 | 源字符串长度 >= n 时不会追加 `\0` | 始终手动设置最后一个字节为 `\0` |
| 用 `==` 比较字符串 | 比较的是指针地址，不是内容 | 用 `strcmp` |
| 修改字符串字面量 | 存储在只读段，修改触发段错误 | 用数组拷贝：`char s[] = "Hello"` |
| `strncat` 的第三个参数 | 是"最多追加的字符数"，不是缓冲区总大小 | 用 `sizeof(dst) - strlen(dst) - 1` |
| `memcpy` 处理重叠区域 | 未定义行为 | 重叠时使用 `memmove` |

## 小结

C 字符串就是一个以 `\0` 终止的 `char` 数组，没有类型系统的保护，所有安全责任都在程序员身上。`string.h` 提供的函数族是操作字符串的基本工具，不带长度限制的版本（`strcpy`、`strcat`、`sprintf`）是缓冲区溢出的主要来源，应优先使用带 `n` 的版本或者 `snprintf`。`memcpy` 用于不重叠的内存复制，`memmove` 用于可能重叠的情况。编译器选项提供了额外的安全网。C++ 的 `std::string` 自动管理内存，`std::string_view` 提供零拷贝引用——理解底层的 C 字符串模型是理解这些 C++ 工具为什么这样设计的前提。

## 练习

### 练习 1：安全字符串库

实现一组安全的字符串操作函数，让每个函数都知道目标缓冲区的大小，自动处理截断和终止：

```c
#include <stddef.h>

/// @brief 安全地复制字符串到目标缓冲区
/// @param dst 目标缓冲区
/// @param src 源字符串
/// @param dst_size 目标缓冲区总大小（含终止符）
/// @return 实际复制的字符数（不含终止符）；如果 dst 为 NULL 返回 0
size_t safe_str_copy(char* dst, const char* src, size_t dst_size);

/// @brief 安全地拼接字符串
/// @param dst 目标缓冲区（已有内容）
/// @param src 要追加的字符串
/// @param dst_size 目标缓冲区总大小（含终止符）
/// @return 拼接后字符串的总长度（不含终止符）
size_t safe_str_cat(char* dst, const char* src, size_t dst_size);

/// @brief 安全地格式化字符串
/// @param dst 目标缓冲区
/// @param dst_size 目标缓冲区总大小
/// @param format 格式字符串
/// @param ... 格式参数
/// @return 实际写入的字符数（不含终止符）
size_t safe_str_format(char* dst, size_t dst_size, const char* format, ...);
```text

提示：`safe_str_copy` 可以基于 `strncpy` 实现，但必须保证终止；`safe_str_cat` 需要先算出目标字符串当前长度，再计算剩余可用空间；`safe_str_format` 直接用 `vsnprintf` 实现即可。

### 练习 2：字符串分割函数

实现一个将字符串按分隔符切分的函数：

```c
/// @brief 将字符串按分隔符切分，返回各子串的起止位置
/// @param input 待分割的字符串（函数不会修改 input）
/// @param delim 分隔字符（单字符）
/// @param out_starts 输出数组：各子串的起始位置
/// @param out_lengths 输出数组：各子串的长度
/// @param max_tokens out_starts/out_lengths 数组的容量
/// @return 实际找到的子串数量
size_t str_split(
    const char* input,
    char delim,
    const char** out_starts,
    size_t* out_lengths,
    size_t max_tokens
);
```text

提示：遍历 `input`，记录每个子串的起始指针和长度。遇到分隔符时结束当前子串，开始下一个。不要忘记处理字符串末尾的最后一个子串。

## 参考资源

- [string.h - cppreference](https://en.cppreference.com/w/c/string/byte)
- [stdio.h 格式化函数 - cppreference](https://en.cppreference.com/w/c/io)
- [Buffer Overflow - OWASP](https://owasp.org/www-community/vulnerabilities/Buffer_Overflow)
