---
chapter: 1
cpp_standard:
- 11
- 14
- 17
description: Master C file operations and the core tools of the standard library, including
  file reading and writing, formatted I/O, and command-line argument handling, and
  compare them with the C++ stream libraries and modern standard library facilities
difficulty: beginner
order: 20
platform: host
prerequisites:
- C Strings and Buffer Safety
- Structures and Memory Alignment
- Dynamic Memory Management
reading_time_minutes: 30
tags:
- host
- cpp-modern
- beginner
- 入门
- 基础
title: File I/O and Standard Library Overview
translation:
  source: documents/vol1-fundamentals/c_tutorials/16-file-io-and-stdlib.md
  source_hash: 89f31506d42fb15de242d2806860d2956aa53d5f35a47ae5f5c1e4a27bfb21b4
  translated_at: '2026-11-22T16:39:43+00:00'
  engine: glm-5.3-refresh
  token_count: 21186
---
# File I/O and Standard Library Overview

Every program we have written so far shares one limitation—all of its data lives in memory, and the moment the program exits, it is gone. Real-world programs do not work that way: configuration has to be read from files, logs have to be written to files, and data has to travel back and forth between programs. That is where file I/O enters the picture.

C's file operations rest on a small API that is simple yet powerful enough—`fopen` to open, `fread`/`fwrite` to read and write, `fclose` to close, plus the `printf`/`scanf` families for formatted input and output. These functions have survived from the 1970s all the way to today. But they also carry the rough edges typical of that era—no type safety, error handling through a global variable, and a compiler that looks the other way when the format string and the arguments do not match. C++ later repackaged this whole system with the stream libraries, `std::filesystem`, and `std::format`, but understanding the raw C API is still the foundation.

## Step 1 — Getting Started with File Operations

### Opening and Closing Files

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

**Always check whether fopen returns NULL**. A missing file, insufficient permissions, or a wrong path can all make the open fail. If you skip the check and use the NULL pointer directly, the program crashes on the spot—with no meaningful error message.

Mode string quick reference:

| Mode | Read | Write | If the file does not exist | If the file already exists |
|------|------|-------|-----------------------------|----------------------------|
| `"r"`  | Yes | No  | Fails | Reads from the beginning |
| `"w"`  | No  | Yes | Creates a new file | **Erases the existing content** |
| `"a"`  | No  | Yes | Creates a new file | Appends to the end |
| `"r+"` | Yes | Yes | Fails | Reads and writes from the beginning |
| `"w+"` | Yes | Yes | Creates a new file | **Erases, then reads and writes** |
| `"a+"` | Yes | Yes | Creates a new file | Reads from the beginning; writes append to the end |

`"w"` and `"w+"` **unconditionally erase** the contents of an existing file. If all you wanted was to append but you picked `"w"` mode, congratulations—the file's content instantly drops to zero, with no confirmation step. Always make sure the mode is right before you use it.

### Reading and Writing Binary Data

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

The return value is the number of **complete items** processed, not a byte count. If it is smaller than the number of items you requested, either you have reached the end of the file or an error has occurred.

### Moving the File Position and Getting the Size

`fseek` moves the position indicator, and `ftell` reports the current position. One practical pattern is getting the file size:

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

### Don't Use feof as a Loop Condition

`feof` returns true only **after** a read operation has already failed. The correct approach is to check the read function's return value directly:

```c
int ch;
while ((ch = fgetc(fp)) != EOF) {
    putchar(ch);
}
```

`fgetc` returns an `int`, not a `char`. If you receive the return value into a `char`, on some platforms `EOF` (-1) gets truncated into a valid character value and the loop never ends. This trap blows up a fresh batch of beginners every year.

## Step 2 — Mastering Formatted I/O

### The printf Family

`printf` writes to stdout, `fprintf` writes to a specified file, and `sprintf`/`snprintf` write into a string buffer. The return value is the number of characters actually written.

```c
char buf[64];
snprintf(buf, sizeof(buf), "%s:%d", name, age);
```

A neat use of `snprintf` is probing the buffer size you need:

```c
int needed = snprintf(NULL, 0, "Result: %d items", item_count);
char* buf = malloc(needed + 1);
snprintf(buf, needed + 1, "Result: %d items", item_count);
```

### The scanf Family

`scanf` returns **the number of fields successfully matched**. `sscanf` is very handy for parsing strings:

```c
const char* input = "2024-01-15";
int year, month, day;
int count = sscanf(input, "%d-%d-%d", &year, &month, &day);
```

`scanf`'s `%s` does not check the buffer size. The safe approaches are to cap the length with `%Ns`, or to switch to the `fgets` + `sscanf` combo.

### Common Format Specifiers

| Specifier | Type | Specifier | Type |
|-----------|------|-----------|------|
| `%d` | int | `%f` | double |
| `%u` | unsigned | `%s` | string |
| `%x` | hex | `%zu` | size_t |
| `%ld` | long | `%lld` | long long |
| `%p` | pointer | `%%` | literal % |

## Step 3 — Text Mode vs Binary Mode

On Windows, text mode automatically converts `\n` into `\r\n`, while binary mode performs no conversion. On Linux/macOS the two are nearly indistinguishable. Whenever you handle binary data (images, struct images, protocol frames), always use `"rb"`/`"wb"`.

If you read a binary file in text mode on Windows, the read terminates early the moment it hits a `0x1A` byte—because `0x1A` is treated as EOF in Windows text mode. This is a classic cross-platform trap.

## Step 4 — Error Handling with errno

`errno` (from `<errno.h>`) is a global error-code variable. A successful call does **not** reset `errno` to zero; it is only set when something goes wrong. The correct procedure is to check the return value first to confirm that an error occurred, and only then read `errno`.

`perror` prints the string you pass in, joined with the system error message:

```c
FILE* fp = fopen("nonexistent.txt", "r");
if (fp == NULL) {
    perror("fopen failed");
    // 输出：fopen failed: No such file or directory
}
```

`strerror` returns the string description corresponding to an error code, which is handy inside custom error messages.

## Step 5 — Handling Command-Line Arguments

```c
int main(int argc, char* argv[]) {
    printf("Program: %s\n", argv[0]);
    for (int i = 1; i < argc; i++) {
        printf("  argv[%d] = %s\n", i, argv[i]);
    }
    return 0;
}
```

`argv[0]` is the program name, `argv[1]` through `argv[argc-1]` are the arguments, and `argv[argc]` is `NULL`.

## Standard Library Quick Reference

### `<stdlib.h>`: General Utilities

`atoi` is simple but has no error detection; `strtol` is safer (it can detect overflow and partial parses). `qsort` sorts and `bsearch` does a binary search, both comparing through function pointers. The pseudo-random numbers from `rand`/`srand` are of rather poor statistical quality—good enough to get by, but never rely on them for anything security-related.

### `<math.h>`: Math Functions

Trigonometric functions (sin/cos/tan), exponentials and logarithms (pow/sqrt/log/exp), rounding (ceil/floor/round), and absolute value (fabs). Each comes in three versions: float (`f` suffix), double, and long double (`l` suffix).

On GCC/Linux, linking against the math library requires the `-lm` option. If you forget it, the build reports something like `undefined reference to 'sin'`—there is nothing wrong with the code itself; you are just missing a linker option.

### `<ctype.h>`: Character Classification

`isalpha`/`isdigit`/`isspace`/`isalnum`/`isupper`/`islower` test a character's category, and `tolower`/`toupper` convert case. The argument must first be cast to `unsigned char`; otherwise the negative values of a signed char cause undefined behavior.

### `<assert.h>`: The assert Macro

```c
assert(arr != NULL);   // Debug: 条件为假时终止程序
```

Once `NDEBUG` is defined, every assert is removed entirely. Use them to catch programming errors, not to handle runtime errors.

### `<stddef.h>`: Basic Types

`size_t` (object sizes), `NULL` (the null pointer), `offsetof` (struct member offsets), and `ptrdiff_t` (pointer differences). `size_t` is unsigned, so watch out for underflow when iterating backwards: `for (size_t i = count; i-- > 0; )` is the safe way to write it.

## Bridging into C++

### Stream Libraries (iostream/fstream/sstream)

The C++ stream libraries achieve **type safety** through operator overloading—pass the wrong type and the compile simply fails. Destructors close files automatically (RAII). `std::getline` returns a `std::string` directly, so there is no buffer-overflow risk at all.

### std::filesystem (C++17)

Cross-platform directory traversal, file attribute queries, and path manipulation—no more writing `#ifdef _WIN32`.

### std::format (C++20)

It combines printf's concise syntax with type safety:

```cpp
std::string s = std::format("{} is {} years old", name, age);
```

### std::span (C++17)

`std::span<const int>` bundles a pointer and a length together, solving the age-old problem of arrays decaying and losing their length information.

### `<system_error>`

`std::error_code` is a value type and thread-safe—far safer than the global `errno`.

## Exercises

### Exercise 1: A Configuration File Parser

**Difficulty: Intermediate** · Parse key=value line by line with fgets

Parse a configuration file in `key=value` format, ignoring `#` comments and blank lines.

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

::: details Reference solution

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

First, whip up a sample configuration file (you can also create it by hand; lines starting with `#` are comments, and blank lines are skipped):

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

Compile and run:

```bash
gcc -std=c17 -Wall -Wextra -Wpedantic config_parser.c -o config_parser
./config_parser config.ini
```

Result (after parsing, all entries print in line order; comments and blank lines are ignored):

```text
server_type=production
listen_port=8080
enable_tls=true
log_level=debug
database_url=mysql://user@localhost:3306/db
max_connections=128
```

Here every entry's `key` and `value` has been trimmed at both ends, so the spaces around `=` never show up in the output; `find_config` can then fetch a value by key—for example, looking up `listen_port` returns `8080`.

:::

Hint: read line by line with `fgets`, locate the `=` with `strchr`, and strip whitespace with `trim`.

### Exercise 2: A File Copy Tool

**Difficulty: Basic** · fread/fwrite plus a progress display

Take the source and destination files from command-line arguments, support copying binary files, and display progress.

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

::: details Reference solution

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

Compile and run:

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

Result (during a normal copy, `\r` keeps refreshing the progress in place on one line, so the terminal ultimately retains only `100%`):

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

Notice that no intermediate percentages appear in the output of a successful copy: `\r` moves the cursor back to the start of the line and each new percentage directly overwrites the old one, so to the naked eye there is just one progress jump from 0% to 100%. Only if you pipe the program somewhere or redirect its output to a file do those step-by-step intermediate percentages survive. Note also that `long` with `ftell` is used here to demonstrate getting the size of an ordinary seekable file; on 32-bit systems it typically tops out around 2 GiB, and when handling very large files you should switch to the large-file positioning interface your target platform provides.

:::

Hint: use `fseek` + `ftell` to get the source file's size, and overwrite the same line with `\r` to build the progress bar.
