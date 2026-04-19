---
chapter: 1
cpp_standard:
- 11
- 14
- 17
description: 掌握 C 语言的文件操作和标准库核心工具，包括文件读写、格式化 I/O、命令行参数处理，对比 C++ 流库和现代标准库工具
difficulty: beginner
order: 20
platform: host
prerequisites:
- 11 C 字符串与缓冲区安全
- 12 结构体与内存对齐
- 14 动态内存管理
reading_time_minutes: 11
tags:
- host
- cpp-modern
- beginner
- 入门
- 基础
title: 文件 I/O 与标准库概览
---
# 文件 I/O 与标准库概览

到目前为止，我们写过的程序有一个共同的局限——数据全在内存里，程序一结束就没了。现实世界的程序不是这样运作的：配置要从文件读、日志要写进文件、数据要在程序之间传来传去。这就轮到文件 I/O 登场了。

C 语言的文件操作建立在一套简洁但足够强大的 API 之上——`fopen` 打开、`fread`/`fwrite` 读写、`fclose` 关闭，外加 `printf`/`scanf` 家族做格式化输入输出。这些函数从 1970 年代一直活到今天。但它们也带着那个年代特有的粗糙感——类型不安全、错误处理靠全局变量、格式字符串和参数不匹配时编译器睁一只眼闭一只眼。C++ 后来用流库、`std::filesystem`、`std::format` 把这套体系重新包装了一遍，但理解 C 的原始 API 仍然是基础。

> **学习目标**
>
> - 完成本章后，你将能够：
> - [ ] 熟练使用 fopen/fclose/fread/fwrite 等文件操作函数
> - [ ] 理解文本模式与二进制模式的区别
> - [ ] 掌握 printf/scanf 家族的格式化 I/O
> - [ ] 使用 errno/perror/strerror 进行错误处理
> - [ ] 编写接受命令行参数的程序
> - [ ] 了解核心标准库工具
> - [ ] 理解 C++ 的流库、std::filesystem 和 std::format 如何改进 C 的方案

## 环境说明

本篇的所有代码在以下环境下验证通过：

- **操作系统**：Linux（Ubuntu 22.04+） / WSL2 / macOS
- **编译器**：GCC 11+（通过 `gcc --version` 确认版本）
- **编译选项**：`gcc -Wall -Wextra -std=c11`（开警告、指定 C11 标准）
- **验证方式**：所有代码可直接编译运行

## 第一步——上手文件操作

### 打开与关闭文件

```c
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    FILE* fp = fopen("data.txt", "r");
    if (fp == NULL) {
        perror("Failed to open data.txt");
        return EXIT_FAILURE;
    }
    // ... 读写操作 ...
    fclose(fp);
    return 0;
}
```text

> ⚠️ **踩坑预警**：**永远检查 fopen 返回值是否为 NULL**。文件不存在、权限不足、路径错误都会导致打开失败。如果不检查就直接使用 NULL 指针，程序会直接崩溃——没有任何有意义的错误信息。

模式字符串速查：

| 模式 | 读 | 写 | 文件不存在时 | 文件已存在时 |
|------|----|----|-------------|-------------|
| `"r"`  | 可以 | 不行 | 失败 | 从头开始读 |
| `"w"`  | 不行 | 可以 | 创建新文件 | **清空原有内容** |
| `"a"`  | 不行 | 可以 | 创建新文件 | 在末尾追加 |
| `"r+"` | 可以 | 可以 | 失败 | 从头开始读写 |
| `"w+"` | 可以 | 可以 | 创建新文件 | **清空后读写** |
| `"a+"` | 可以 | 可以 | 创建新文件 | 读从头部，写追加到末尾 |

> ⚠️ **踩坑预警**：`"w"` 和 `"w+"` 会**无条件清空**已有文件的内容。如果你只是想追加内容却用了 `"w"` 模式，恭喜——文件内容瞬间归零，而且没有确认步骤。使用前一定确认模式正确。

### 读写二进制数据

```c
typedef struct {
    uint16_t id;
    float value;
    uint32_t timestamp;
} Record;

// 写入
size_t written = fwrite(records, sizeof(Record), count, fp);

// 读取
size_t count = fread(buffer, sizeof(Record), max_count, fp);
```text

返回值是成功处理的**完整块数**，不是字节数。如果返回值小于请求的块数，说明要么到了文件末尾，要么发生了错误。

### 移动文件位置与获取大小

`fseek` 移动位置指针，`ftell` 查询当前位置。一个实用的模式是获取文件大小：

```c
long get_file_size(FILE* fp) {
    long original = ftell(fp);
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, original, SEEK_SET);
    return size;
}
```text

### 别把 feof 当循环条件

`feof` 只有在读取操作已经失败**之后**才会返回真。正确的做法是直接检查读取函数的返回值：

```c
int ch;
while ((ch = fgetc(fp)) != EOF) {
    putchar(ch);
}
```text

> ⚠️ **踩坑预警**：`fgetc` 返回 `int` 而不是 `char`。如果你用 `char` 接收返回值，在某些平台上 `EOF`（-1）会被截断为一个有效的字符值，导致循环永远不会结束。这个坑每年都会炸到一批新手。

## 第二步——掌握格式化 I/O

### printf 家族

`printf` 输出到 stdout，`fprintf` 输出到指定文件，`sprintf`/`snprintf` 输出到字符串缓冲区。返回值是实际输出的字符数。

```c
char buf[64];
snprintf(buf, sizeof(buf), "%s:%d", name, age);
```text

`snprintf` 的一个巧妙用法是探测所需缓冲区大小：

```c
int needed = snprintf(NULL, 0, "Result: %d items", item_count);
char* buf = malloc(needed + 1);
snprintf(buf, needed + 1, "Result: %d items", item_count);
```text

### scanf 家族

`scanf` 返回**成功匹配的字段数**。`sscanf` 从字符串解析非常方便：

```c
const char* input = "2024-01-15";
int year, month, day;
int count = sscanf(input, "%d-%d-%d", &year, &month, &day);
```text

> ⚠️ **踩坑预警**：`scanf` 的 `%s` 不检查缓冲区大小，安全的做法是用 `%Ns` 指定最大长度，或者改用 `fgets` + `sscanf` 组合。

### 常用格式说明符

| 说明符 | 类型 | 说明符 | 类型 |
|--------|------|--------|------|
| `%d` | int | `%f` | double |
| `%u` | unsigned | `%s` | string |
| `%x` | hex | `%zu` | size_t |
| `%ld` | long | `%lld` | long long |
| `%p` | pointer | `%%` | 字面 % |

## 第三步——搞清楚文本模式与二进制模式

在 Windows 上，文本模式会自动把 `\n` 转换为 `\r\n`，二进制模式不做转换。在 Linux/macOS 上两者几乎无区别。处理二进制数据（图片、结构体镜像、协议帧）务必用 `"rb"`/`"wb"`。

> ⚠️ **踩坑预警**：如果你在 Windows 上用文本模式读取一个二进制文件，遇到 `0x1A` 字节时读取会提前终止——因为 `0x1A` 在 Windows 文本模式下被当作 EOF。这是一个经典的跨平台陷阱。

## 第四步——用 errno 做错误处理

`errno`（`<errno.h>`）是全局错误码变量。函数执行成功时**不会**清零 `errno`，只有出错时才设置。正确做法是先检查返回值确认出错了，再读 `errno`。

`perror` 把你传入的字符串和系统错误信息拼接输出：

```c
FILE* fp = fopen("nonexistent.txt", "r");
if (fp == NULL) {
    perror("fopen failed");
    // 输出：fopen failed: No such file or directory
}
```text

`strerror` 返回错误码对应的字符串描述，适合用在自定义的错误信息中。

## 第五步——处理命令行参数

```c
int main(int argc, char* argv[]) {
    printf("Program: %s\n", argv[0]);
    for (int i = 1; i < argc; i++) {
        printf("  argv[%d] = %s\n", i, argv[i]);
    }
    return 0;
}
```text

`argv[0]` 是程序名，`argv[1]` 到 `argv[argc-1]` 是参数，`argv[argc]` 是 `NULL`。

## 标准库速查

### `<stdlib.h>`：通用工具

`atoi` 简单但无错误检测，`strtol` 更安全（可检测溢出和部分解析）。`qsort` 快速排序、`bsearch` 二分查找，都通过函数指针比较。`rand`/`srand` 伪随机数的随机质量较差，够用但别依赖它做安全相关的事。

### `<math.h>`：数学函数

三角函数（sin/cos/tan）、指数对数（pow/sqrt/log/exp）、取整（ceil/floor/round）、绝对值（fabs）。都有 float（f 后缀）、double、long double（l 后缀）三个版本。

> ⚠️ **踩坑预警**：链接数学库在 GCC/Linux 上需要 `-lm` 选项。如果你忘了加这个选项，编译器会报 `undefined reference to 'sin'` 之类的错误——代码本身没问题，就是少了个链接选项。

### `<ctype.h>`：字符分类

`isalpha`/`isdigit`/`isspace`/`isalnum`/`isupper`/`islower` 判断字符类别，`tolower`/`toupper` 大小写转换。参数必须先强转为 `unsigned char`，否则有符号 char 的负值会导致未定义行为。

### `<assert.h>`：断言宏

```c
assert(arr != NULL);   // Debug: 条件为假时终止程序
```text

定义 `NDEBUG` 后所有 assert 完全移除。用于抓编程错误，不是处理运行时错误。

### `<stddef.h>`：基础类型

`size_t`（对象大小）、`NULL`（空指针）、`offsetof`（结构体偏移量）、`ptrdiff_t`（指针差值）。`size_t` 是无符号的，反向遍历时注意下溢：`for (size_t i = count; i-- > 0; )` 是安全写法。

## C++ 衔接

### 流库（iostream/fstream/sstream）

C++ 流库通过运算符重载实现**类型安全**——传错类型直接编译失败。析构函数自动关闭文件（RAII）。`std::getline` 直接返回 `std::string`，不存在缓冲区溢出风险。

### std::filesystem（C++17）

跨平台的目录遍历、文件属性查询、路径操作——不再需要写 `#ifdef _WIN32`。

### std::format（C++20）

结合了 printf 的简洁语法和类型安全：

```cpp
std::string s = std::format("{} is {} years old", name, age);
```text

### std::span（C++17）

`std::span<const int>` 把指针+长度绑在一起，解决了数组退化丢失长度信息的老问题。

### `<system_error>`

`std::error_code` 是值类型，线程安全，比全局 `errno` 安全得多。

## 小结

文件操作的核心是 `FILE*` 和 `fopen`/`fclose`/`fread`/`fwrite`，格式化 I/O 靠 `printf`/`scanf` 家族，错误处理靠 `errno` + `perror`。标准库提供了数值转换、排序搜索、数学函数、字符分类、断言等基础工具。C++ 用流库、`std::filesystem`、`std::format`、`std::error_code` 对这些工具做了全面的类型安全升级。

## 练习

### 练习 1：配置文件解析器

解析 `key=value` 格式的配置文件，忽略 `#` 注释和空行。

```c
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>

#define MAX_LINE 256
#define MAX_KEY 64
#define MAX_VALUE 128

typedef struct {
    char key[MAX_KEY];
    char value[MAX_VALUE];
} ConfigEntry;

/// @brief 去除字符串首尾的空白字符
char* trim(char* str);

/// @brief 解析配置文件
size_t parse_config(const char* path, ConfigEntry* entries, size_t max_entries);

/// @brief 在配置项中查找指定 key
const char* find_config(const ConfigEntry* entries, size_t count, const char* key);

int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <config_file>\n", argv[0]);
        return 1;
    }
    // TODO: 调用 parse_config 和 find_config
    return 0;
}
```text

提示：用 `fgets` 逐行读取，`strchr` 找 `=` 位置，`trim` 去除空白。

### 练习 2：文件复制工具

通过命令行参数指定源文件和目标文件，支持二进制文件复制，显示进度。

```c
#include <stdio.h>
#include <stdlib.h>

#define kBufferSize 4096

/// @brief 复制文件
int copy_file(const char* src_path, const char* dst_path)
{
    // TODO: 实现
    // 1. "rb" 打开源文件，"wb" 打开目标文件
    // 2. 循环 fread/fwrite
    // 3. 用 fseek/ftell 获取总大小，打印进度
    // 4. 错误处理：先打开的后关闭
    return -1;
}

int main(int argc, char* argv[]) {
    // TODO: 解析命令行参数，调用 copy_file
    return 0;
}
```text

提示：用 `fseek` + `ftell` 获取源文件大小，`\r` 覆写同一行实现进度条。
