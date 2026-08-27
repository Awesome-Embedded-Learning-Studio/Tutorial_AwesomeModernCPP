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
- C 字符串与缓冲区安全
- 结构体与内存对齐
- 动态内存管理
reading_time_minutes: 30
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

- 平台：Linux x86\_64（WSL2 也可以）
- 编译器：GCC 13+ 或 Clang 17+
- 编译选项：`-std=c17 -Wall -Wextra -Wpedantic`
- 验证方式：所有代码可直接编译运行

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
```

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
```

返回值是成功处理的**完整块数**，不是字节数。如果返回值小于请求的块数，说明要么到了文件末尾，要么发生了错误。

### 移动文件位置与获取大小

`fseek` 移动位置指针，`ftell` 查询当前位置。一个实用的模式是获取文件大小：

```c
long get_file_size(FILE* fp) {
    // 保存调用前的位置。
    long original = ftell(fp);
    // 将文件位置移至文件末尾。
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    // 恢复到调用前的位置。
    fseek(fp, original, SEEK_SET);
    return size;
}
```

### 别把 feof 当循环条件

`feof` 只有在读取操作已经失败**之后**才会返回真。正确的做法是直接检查读取函数的返回值：

```c
int ch;
while ((ch = fgetc(fp)) != EOF) {
    putchar(ch);
}
```

> ⚠️ **踩坑预警**：`fgetc` 返回 `int` 而不是 `char`。如果你用 `char` 接收返回值，在某些平台上 `EOF`（-1）会被截断为一个有效的字符值，导致循环永远不会结束。这个坑每年都会炸到一批新手。

## 第二步——掌握格式化 I/O

### printf 家族

`printf` 输出到 stdout，`fprintf` 输出到指定文件，`sprintf`/`snprintf` 输出到字符串缓冲区。返回值是实际输出的字符数。

```c
char buf[64];
snprintf(buf, sizeof(buf), "%s:%d", name, age);
```

`snprintf` 的一个巧妙用法是探测所需缓冲区大小：

```c
int needed = snprintf(NULL, 0, "Result: %d items", item_count);
char* buf = malloc(needed + 1);
snprintf(buf, needed + 1, "Result: %d items", item_count);
```

### scanf 家族

`scanf` 返回**成功匹配的字段数**。`sscanf` 从字符串解析非常方便：

```c
const char* input = "2024-01-15";
int year, month, day;
int count = sscanf(input, "%d-%d-%d", &year, &month, &day);
```

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
```

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
```

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
```

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
```

### std::span（C++17）

`std::span<const int>` 把指针+长度绑在一起，解决了数组退化丢失长度信息的老问题。

### `<system_error>`

`std::error_code` 是值类型，线程安全，比全局 `errno` 安全得多。

## 小结

文件操作的核心是 `FILE*` 和 `fopen`/`fclose`/`fread`/`fwrite`，格式化 I/O 靠 `printf`/`scanf` 家族，错误处理靠 `errno` + `perror`。标准库提供了数值转换、排序搜索、数学函数、字符分类、断言等基础工具。C++ 用流库、`std::filesystem`、`std::format`、`std::error_code` 对这些工具做了全面的类型安全升级。

## 练习

### 练习 1：配置文件解析器

**难度：进阶** · fgets 逐行解析 key=value

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
    // 练习： 调用 parse_config 和 find_config
    return 0;
}
```

::: details 参考答案

**config_parser.c**

```c
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_LINE 256
#define MAX_KEY 64
#define MAX_VALUE 128
#define PARSE_CONFIG_ERROR SIZE_MAX

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

/**
 * @brief 去除字符串首尾的空白字符（原地修改，返回指向处理结果的首地址）
 * @param str 要处理的字符串（会被修改）
 * @return char* 去除空白后字符串的首地址
 *
 */

char* trim(char* str) {
    char* end;  // 用于从字符串末尾向前扫描的指针

    // 跳过字符串开头的所有空白字符（空格、制表符、换行等）
    while (isspace((unsigned char)*str)) {
        ++str;
    }

    // 若跳过开头空白后字符串已结束（整行都是空白），则直接返回空字符串
    if (*str == '\0') {
        return str;
    }

    // end 指向最后一个字符的位置（跳过末尾的 '\0'）
    end = str + strlen(str) - 1;
    // 从后向前跳过尾部空白字符，直到遇到有效字符或回到字符串开头
    while (end > str && isspace((unsigned char)*end)) {
        --end;
    }
    // 在最后一个有效字符之后写入结束符，截断尾部空白
    end[1] = '\0';
    return str;
}

/**
 * @brief 解析配置文件（每行格式：key=value，# 开头为注释会被忽略）
 * @param path        配置文件路径
 * @param entries     存放解析结果的配置项数组
 * @param max_entries 数组最多能存放的配置项个数
 * @return 解析出的配置项数量；发生错误时返回 PARSE_CONFIG_ERROR
 *
 * 处理逻辑：
 *   1. 校验入参是否合法；
 *   2. 打开文件，失败则打印错误信息并返回 PARSE_CONFIG_ERROR；
 *   3. 逐行读取：
 *        - 去掉行内 '#' 之后的注释部分；
 *        - 用 trim 去掉首尾空白；
 *        - 查找 '=' 分隔符，找不到则跳过该行；
 *        - 以 '=' 为界拆分为 key 和 value，并分别 trim；
 *        - 用 snprintf 安全复制进 entries 数组并计数；超过 max_entries 时返回错误。
 */
size_t parse_config(const char* path, ConfigEntry* entries, size_t max_entries)
{
    FILE* file;                                 // 文件流指针
    char line[MAX_LINE];                        // 读取单行文本的缓冲区，大小由 MAX_LINE 决定
    size_t count = 0;                           // 已解析出的配置项数量
    size_t line_number = 0;                     // 当前读取到的物理行号

    // 参数校验：路径和数组不能为空，数组容量必须大于 0。
    if (path == NULL || entries == NULL || max_entries == 0) {
        return PARSE_CONFIG_ERROR;
    }

    // 以只读文本模式打开配置文件
    file = fopen(path, "r");
    // 打开失败：用 perror 打印系统错误信息。
    if (file == NULL) {
        perror(path);
        return PARSE_CONFIG_ERROR;
    }

    // 读完整个文件：一旦有效配置项超过数组容量，就报告错误而不是静默丢弃。
    while (fgets(line, sizeof(line), file) != NULL) {
        char* comment = strchr(line, '#');      // 查找注释起始字符 '#'
        char* equal;                            // 指向 '=' 分隔符的指针
        char* key;                              // 指向 key 部分
        char* value;                            // 指向 value 部分
        int next_char;
        int key_length;
        int value_length;

        ++line_number;

        // 缓冲区已满时向前读取一个字符，区分“刚好装满”和真正的超长行
        if (strchr(line, '\n') == NULL) {
            next_char = fgetc(file);
            if (next_char != '\n' && next_char != EOF) {
                while (next_char != '\n' && next_char != EOF) {
                    next_char = fgetc(file);
                }
                fprintf(stderr, "%s:%zu: 行长度超过 %d 个字符\n",
                        path, line_number, MAX_LINE - 1);
                fclose(file);
                return PARSE_CONFIG_ERROR;
            }
        }

        // 若行内含有 '#'，则将其截断，忽略其后所有注释内容
        if (comment != NULL) {
            *comment = '\0';
        }

        // 去掉行首尾空白，得到 key 候选字符串
        key = trim(line);
        // 去掉空白后为空行，跳过
        if (*key == '\0') {
            continue;
        }

        // 查找 '=' 分隔符
        equal = strchr(key, '=');
        // 找不到 '='，说明不是合法的 key=value 行，跳过
        if (equal == NULL) {
            continue;
        }

        // 用 '\0' 替换 '='，把字符串在分隔符处切开，同时保留 '=' 之后的原始内容
        *equal = '\0';
        // 分别处理 key（'=' 之前）和 value（'=' 之后），各自去掉首尾空白
        key = trim(key);
        value = trim(equal + 1);
        // key 去空白后为空，说明缺少有效键名，跳过
        if (*key == '\0') {
            continue;
        }

        if (count == max_entries) {
            fprintf(stderr, "%s:%zu: 配置项数量超过上限 %zu\n", path,
                    line_number, max_entries);
            fclose(file);
            return PARSE_CONFIG_ERROR;
        }

        // 复制后检查 snprintf 返回值，拒绝被截断的 key 或 value
        key_length = snprintf(entries[count].key, sizeof(entries[count].key), "%s", key);
        value_length = snprintf(entries[count].value, sizeof(entries[count].value), "%s", value);
        if (key_length < 0 || value_length < 0 ||
            (size_t)key_length >= sizeof(entries[count].key) ||
            (size_t)value_length >= sizeof(entries[count].value)) {
            fprintf(stderr, "%s:%zu: 键名或值过长\n", path, line_number);
            fclose(file);
            return PARSE_CONFIG_ERROR;
        }
        ++count;    // 增加已解析数量
    }

    if (ferror(file)) {
        fprintf(stderr, "%s: 读取配置文件失败\n", path);
        fclose(file);
        return PARSE_CONFIG_ERROR;
    }
    if (fclose(file) != 0) {
        perror(path);
        return PARSE_CONFIG_ERROR;
    }

    return count;
}

/**
 * @brief 在配置项数组中查找指定 key，并返回对应的 value
 * @param entries 配置项数组
 * @param count   数组中的有效元素个数
 * @param key     要查找的键名
 * @return const char* 找到时返回对应 value 的指针；找不到或入参非法时返回 NULL
 */
const char* find_config(const ConfigEntry* entries, size_t count, const char* key) {
    size_t i;   // 循环下标

    // 参数校验：数组或查找键不能为空
    if (entries == NULL || key == NULL) {
        return NULL;
    }

    // 遍历每个配置项，用 strcmp 比较键名
    for (i = 0; i < count; ++i) {
        if (strcmp(entries[i].key, key) == 0) {
            return entries[i].value;   // 找到匹配的 key，返回其 value 指针
        }
    }

    return NULL;    // 遍历结束仍未找到，返回 NULL
}

/**
 * @brief 程序入口：读取命令行指定的配置文件并解析，然后打印所有配置项
 * @param argc 命令行参数个数（含程序名）
 * @param argv 命令行参数数组，argv[1] 应为配置文件路径
 * @return int 程序退出码：0 表示成功，1 表示用法错误，2 表示配置解析失败
 */
int main(int argc, char* argv[])
{
    ConfigEntry entries[32];    // 定义数组，最多存放 32 个配置项
    size_t count;               // 实际解析出的配置项数量
    size_t i;                   // 循环下标

    // 检查参数个数：必须是程序名 + 配置文件路径。
    if (argc != 2) {
        // 打印用法提示到标准错误输出，并返回非零退出码
        fprintf(stderr, "用法: %s <配置文件>\n", argv[0]);
        return 1;
    }

    // 计算数组能容纳的元素个数，并调用 parse_config 解析配置文件
    count = parse_config(argv[1], entries, sizeof(entries) / sizeof(entries[0]));
    if (count == PARSE_CONFIG_ERROR) {
        return 2;
    }
    // 遍历并打印所有解析出的配置项（key=value 格式）
    for (i = 0; i < count; ++i) {
        printf("%s=%s\n", entries[i].key, entries[i].value);
    }

    return EXIT_SUCCESS;
}
```

先捏一个示例配置文件（也可以手动创建，`#` 开头是注释，空行会被跳过）：

```bash
cat > config.ini <<'EOF'
# 服务器配置示例
# 注释行可以被忽略

server_type = production
listen_port = 8080
enable_tls = true
log_level = debug

# 下面的行会被解析
database_url = mysql://user@localhost:3306/db
max_connections = 128
EOF
```

编译运行：

```bash
gcc -std=c17 -Wall -Wextra -Wpedantic config_parser.c -o config_parser
./config_parser config.ini
```

运行结果（解析后按行序打印所有配置项，注释和空行被忽略）：

```text
server_type=production
listen_port=8080
enable_tls=true
log_level=debug
database_url=mysql://user@localhost:3306/db
max_connections=128
```

这里每一项的 `key` 和 `value` 都经过了首尾去空白处理，`=` 两侧的空格不会出现在输出里；`find_config` 则可用于按 key 取 value，例如查 `listen_port` 时它会返回 `8080`。

:::

提示：用 `fgets` 逐行读取，`strchr` 找 `=` 位置，`trim` 去除空白。

### 练习 2：文件复制工具

**难度：基础** · fread/fwrite 加进度

通过命令行参数指定源文件和目标文件，支持二进制文件复制，显示进度。

```c
#include <stdio.h>
#include <stdlib.h>

#define BUFFER_SIZE 4096U

/// @brief 复制文件
int copy_file(const char* src_path, const char* dst_path)
{
    // 练习： 实现
    // 1. "rb" 打开源文件，"wb" 打开目标文件
    // 2. 循环 fread/fwrite
    // 3. 用 fseek/ftell 获取总大小，打印进度
    // 4. 错误处理：先打开的后关闭
    return -1;
}

int main(int argc, char* argv[]) {
    // 练习： 解析命令行参数，调用 copy_file
    return 0;
}
```

::: details 参考答案

**file_copy.c**

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define BUFFER_SIZE 4096U

/* 检查两个路径是否指向同一个已有文件，避免目标文件打开时截断源文件。 */
static int same_file(const char *src_path, const char *dst_path)
{
    struct stat src_info;
    struct stat dst_info;

    if (strcmp(src_path, dst_path) == 0) {
        return 1;
    }

    if (stat(src_path, &src_info) == 0 && stat(dst_path, &dst_info) == 0) {
        return src_info.st_dev == dst_info.st_dev &&
               src_info.st_ino == dst_info.st_ino;
    }

    return 0;
}

/// @brief 复制文件
/// @param src_path 源文件路径
/// @param dst_path 目标文件路径
/// @return 成功返回 0，失败返回 -1
int copy_file(const char *src_path, const char *dst_path)
{
    FILE *src = NULL;                /* 源文件指针，初始化为 NULL */
    FILE *dst = NULL;                /* 目标文件指针，初始化为 NULL */
    unsigned char buffer[BUFFER_SIZE]; /* 用于读写数据的缓冲区 */
    long total_size;                 /* 源文件总大小（字节） */
    long copied_size = 0;            /* 已复制的字节数 */
    int result = -1;                 /* 函数返回值，默认失败 */
    int dst_opened = 0;              /* 目标文件是否曾经成功打开 */
    int progress_active = 0;         /* 当前是否有未换行的进度输出 */

    if (src_path == NULL || dst_path == NULL) {
        fprintf(stderr, "源文件和目标文件路径不能为空\n");
        return -1;
    }

    if (same_file(src_path, dst_path)) {
        fprintf(stderr, "源文件和目标文件不能是同一个文件: '%s'\n", src_path);
        return -1;
    }

    /* 以只读二进制模式打开源文件 */
    src = fopen(src_path, "rb");
    if (src == NULL) {
        fprintf(stderr, "无法打开源文件 '%s'\n", src_path);
        goto cleanup;                /* 跳过后续步骤，直接清理并返回 */
    }

    /* 将文件指针移动到文件末尾，以便获取文件大小 */
    if (fseek(src, 0, SEEK_END) != 0) {
        fprintf(stderr, "无法获取源文件大小 '%s'\n", src_path);
        goto cleanup;
    }

    /* 获取当前文件指针位置，即文件的总大小 */
    total_size = ftell(src);
    if (total_size < 0) {
        fprintf(stderr, "无法获取源文件大小 '%s'\n", src_path);
        goto cleanup;
    }

    /* 将文件指针重新移动到文件开头，准备读取内容 */
    if (fseek(src, 0, SEEK_SET) != 0) {
        fprintf(stderr, "无法定位源文件 '%s'\n", src_path);
        goto cleanup;
    }

    /* 以只写二进制模式创建/覆盖目标文件 */
    dst = fopen(dst_path, "wb");
    if (dst == NULL) {
        fprintf(stderr, "无法打开目标文件 '%s'\n", dst_path);
        goto cleanup;
    }
    dst_opened = 1;

    /* 源文件为空（大小为 0）时直接显示 100% 进度 */
    if (total_size == 0) {
        printf("进度: 100%%\n");
    }

    /* 循环读取源文件内容并写入目标文件 */
    while (1) {
        /* 从源文件读取最多一个缓冲区的字节 */
        size_t bytes_read = fread(buffer, 1, sizeof(buffer), src);

        if (bytes_read > 0) {
            /* 将读取到的内容写入目标文件 */
            size_t bytes_written = fwrite(buffer, 1, bytes_read, dst);

            /* 写入字节数不一致说明写入失败 */
            if (bytes_written != bytes_read) {
                if (progress_active) {
                    putchar('\n');
                    fflush(stdout);
                    progress_active = 0;
                }
                fprintf(stderr, "写入目标文件失败 '%s'\n", dst_path);
                goto cleanup;
            }

            /* 累加已复制的字节数，并计算并输出进度百分比 */
            copied_size += (long)bytes_written;
            if (total_size > 0) {
                int percent = (int)(((long double)copied_size * 100.0L) /
                                    (long double)total_size);
                if (percent > 100) {
                    percent = 100;
                }
                printf("\r进度: %d%%", percent);     /* \r 表示回车回行首，覆盖旧进度 */
                fflush(stdout);                     /* 强制刷新输出缓冲区，立即显示进度 */
                progress_active = 1;
            }
        }

        /* 如果读取字节数小于缓冲区大小，说明已到达文件末尾 */
        if (bytes_read < sizeof(buffer)) {
            if (ferror(src)) {                      /* 判断读取是否发生错误 */
                if (progress_active) {
                    putchar('\n');
                    fflush(stdout);
                    progress_active = 0;
                }
                fprintf(stderr, "读取源文件失败 '%s'\n", src_path);
                goto cleanup;
            }
            break;                                  /* 正常到达文件末尾，结束循环 */
        }
    }

    /* 循环结束后再次显示 100% 进度并换行 */
    if (total_size > 0) {
        printf("\r进度: 100%%\n");
        progress_active = 0;
    }

    /* 显式关闭目标文件，并检查是否出错 */
    if (fclose(dst) != 0) {
        dst = NULL;
        fprintf(stderr, "关闭目标文件失败 '%s'\n", dst_path);
        goto cleanup;
    }
    dst = NULL;                                     /* 关闭成功，置空防止重复关闭 */

    if (fclose(src) != 0) {
        src = NULL;
        fprintf(stderr, "关闭源文件失败 '%s'\n", src_path);
        goto cleanup;
    }
    src = NULL;

    result = 0;                                     /* 全部操作成功，标记成功 */

cleanup:
    if (progress_active) {
        putchar('\n');
        fflush(stdout);
    }
    /* 统一清理：若句柄不为 NULL 则关闭，避免资源泄漏 */
    if (dst != NULL) {
        fclose(dst);
    }
    if (result != 0 && dst_opened) {
        if (remove(dst_path) == 0) {
            fprintf(stderr, "复制失败，已删除不完整的目标文件 '%s'\n", dst_path);
        } else {
            fprintf(stderr, "复制失败，无法删除不完整的目标文件 '%s'\n", dst_path);
        }
    }
    if (src != NULL) {
        fclose(src);
    }
    return result;      /* 返回结果 */
}

int main(int argc, char *argv[])
{
    /* 参数个数校验：需要程序名 + 源文件路径 + 目标文件路径，共 3 个 */
    if (argc != 3) {
        fprintf(stderr, "用法: %s <源文件> <目标文件>\n", argv[0]);
        return EXIT_FAILURE;
    }

    /* 调用复制函数，若非 0 说明失败 */
    if (copy_file(argv[1], argv[2]) != 0) {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;    /* 成功退出 */
}
```

编译运行：

```bash
gcc -std=c17 -Wall -Wextra -Wpedantic file_copy.c -o file_copy

# A. 基础功能：拷贝 + 校验
head -c 1048576 /dev/urandom > /tmp/src.bin     # 1MB 随机二进制
./file_copy /tmp/src.bin /tmp/dst.bin            # 应看到进度条 \r 原地刷新
cmp /tmp/src.bin /tmp/dst.bin && echo "OK 内容一致"

# B. 空文件（大小 0，应显示 100%）
: > /tmp/empty.bin
./file_copy /tmp/empty.bin /tmp/e.bin; wc -c /tmp/e.bin   # 正确为 0

# C. 大文件（观察进度递增，末尾 100%）
head -c 100000000 /dev/urandom > /tmp/big.bin    # 100MB，能看到百分比跳动
./file_copy /tmp/big.bin /tmp/big_dst.bin

# D. 错误处理（每项应打印错误、退出码非 0）
./file_copy                                             # 缺参数 → 用法提示
./file_copy /tmp/src.bin /tmp/src.bin; echo $?           # 同文件 → 拒绝
./file_copy /tmp/没有的文件 /tmp/x.bin; echo $?           # 源不存在 → 报错
./file_copy /tmp/src.bin /tmp/不存在目录/x.bin; echo $?   # 目标不可写 → 报错

```

运行结果（正常拷贝时，`\r` 让进度在同一行原地刷新，因此终端最终只保留 `100%`）：

```text
# A. 拷贝 src.bin → dst.bin，进度条在同一行不断刷新，最终停在 100%
进度: 100%
OK 内容一致

# B. 空文件：直接显示 100%，目标文件大小为 0
进度: 100%
0 /tmp/e.bin

# D. 错误处理（每项打印一行错误，退出码均为非 0）
用法: ./file_copy <源文件> <目标文件>
源文件和目标文件不能是同一个文件: '/tmp/src.bin'
无法打开源文件 '/tmp/没有的文件'
无法打开目标文件 '/tmp/不存在目录/x.bin'
```

注意拷贝成功时的输出里没有出现中间的百分比：`\r` 会把光标移回行首，新百分比直接覆盖旧值，所以肉眼看只有一个从 0% 跳到 100% 的进度。如果把这个程序接到管道或重定向到文件，才会看到中间那些一步一格的百分比都留了下来。这里用 `long` 与 `ftell` 演示普通可定位文件的大小获取；在 32 位系统上它通常只能表示到约 2 GiB，处理超大文件时应改用目标平台提供的大文件定位接口。

:::

提示：用 `fseek` + `ftell` 获取源文件大小，`\r` 覆写同一行实现进度条。
