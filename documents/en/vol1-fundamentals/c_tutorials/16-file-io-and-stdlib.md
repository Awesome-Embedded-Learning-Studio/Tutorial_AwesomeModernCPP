---
chapter: 1
cpp_standard:
- 11
- 14
- 17
description: Master C file operations and core standard library tools, including file
  I/O, formatted I/O, and command-line argument handling, while comparing them with
  C++ stream libraries and modern standard library tools.
difficulty: beginner
order: 20
platform: host
prerequisites:
- 11 C 字符串与缓冲区安全
- 12 结构体与内存对齐
- 14 动态内存管理
reading_time_minutes: 8
tags:
- host
- cpp-modern
- beginner
- 入门
- 基础
title: File I/O and Standard Library Overview
translation:
  engine: anthropic
  source: documents/vol1-fundamentals/c_tutorials/16-file-io-and-stdlib.md
  source_hash: e9a734634f87a00129e5ca66d6817aec7c2976dd5bdea8a4ba8ef4fa7c84c657
  token_count: 1855
  translated_at: '2026-06-13T11:43:06.371064+00:00'
---
# File I/O and Standard Library Overview

Up to this point, every program we have written shares a common limitation—data resides entirely in memory and vanishes once the program ends. Real-world programs do not work this way: configurations must be read from files, logs written to files, and data transferred between programs. This is where file I/O comes into play.

C's file operations are built upon a concise yet powerful API—`fopen` to open, `fread`/`fwrite` to read and write, `fclose` to close, plus the `printf`/`scanf` family for formatted input and output. These functions have survived from the 1970s to the present day. However, they also carry the rough edges characteristic of that era—type safety issues, error handling relying on global variables, and compilers turning a blind eye to mismatches between format strings and arguments. C++ later repackaged this system with the stream library, `std::filesystem`, and `std::format`, but understanding C's raw API remains the foundation.

> **Learning Objectives**
>
> - After completing this chapter, you will be able to:
> - [ ] Skillfully use file operation functions like fopen/fclose/fread/fwrite
> - [ ] Understand the difference between text mode and binary mode
> - [ ] Master formatted I/O with the printf/scanf family
> - [ ] Use errno/perror/strerror for error handling
> - [ ] Write programs that accept command-line arguments
> - [ ] Understand core standard library utilities
> - [ ] Understand how C++'s stream library, std::filesystem, and std::format improve upon C's approach

## Environment

All code in this article has been verified in the following environment:

- **Operating System**: Linux (Ubuntu 22.04+) / WSL2 / macOS
- **Compiler**: GCC 11+ (Confirm version via `gcc --version`)
- **Compiler Flags**: `-Wall -Wextra -std=c11` (Enable warnings, specify C11 standard)
- **Verification**: All code can be compiled and run directly

## Step 1 — Getting Started with File Operations

### Opening and Closing Files

```c
FILE *fp = fopen("log.txt", "w"); // Open for writing
if (!fp) {
    // Handle error
}
// ... perform operations ...
fclose(fp);
```

> ⚠️ **Pitfall Warning**: **Always check if fopen returns NULL**. File not found, insufficient permissions, or incorrect paths will cause the open to fail. If you use a NULL pointer directly without checking, the program will crash immediately—without any meaningful error message.

Mode string cheat sheet:

| Mode | Read | Write | If file doesn't exist | If file already exists |
|------|------|-------|----------------------|-------------------------|
| `"r"`  | Yes | No | Fails | Reads from start |
| `"w"`  | No | Yes | Creates new file | **Clears original content** |
| `"a"`  | No | Yes | Creates new file | Appends to end |
| `"r+"` | Yes | Yes | Fails | Reads and writes from start |
| `"w+"` | Yes | Yes | Creates new file | **Clears then reads/writes** |
| `"a+"` | Yes | Yes | Creates new file | Reads from start, writes append to end |

> ⚠️ **Pitfall Warning**: `"w"` and `"w+"` will **unconditionally clear** the contents of an existing file. If you meant to append content but used the `"w"` mode, congratulations—the file content is instantly zeroed out, and there is no confirmation step. Always confirm the mode is correct before use.

### Reading and Writing Binary Data

```c
int data[256];
size_t count = fread(data, sizeof(int), 256, fp); // Read 256 integers
fwrite(data, sizeof(int), count, fp);             // Write them back
```

The return value is the number of **complete blocks** successfully processed, not the number of bytes. If the return value is less than the requested number of blocks, it indicates either end-of-file or an error.

### Moving File Position and Getting Size

`fseek` moves the position pointer, `ftell` queries the current position. A useful pattern is to get the file size:

```c
fseek(fp, 0, SEEK_END); // Jump to end
long size = ftell(fp);  // Get position = size
fseek(fp, 0, SEEK_SET); // Jump back to start
```

### Don't Use feof as a Loop Condition

`feof` only returns true **after** a read operation has already failed. The correct approach is to check the return value of the read function directly:

```c
int c;
while ((c = fgetc(fp)) != EOF) {
    putchar(c);
}
```

> ⚠️ **Pitfall Warning**: `fgetc` returns `int` rather than `char`. If you use `char` to receive the return value, on some platforms `EOF` (-1) will be truncated to a valid character value, causing the loop to never end. This pitfall catches a batch of newbies every year.

## Step 2 — Mastering Formatted I/O

### The printf Family

`printf` outputs to stdout, `fprintf` outputs to a specified file, `sprintf`/`snprintf` output to a string buffer. The return value is the actual number of characters output.

```c
int year = 2025;
printf("Year: %d\n", year);                     // 10 chars
char buf[64];
int len = snprintf(buf, sizeof(buf), "%d", year); // Returns 4
```

A clever use of `snprintf` is to probe the required buffer size:

```c
int needed = snprintf(NULL, 0, "%d %s", 42, "test"); // Returns 8, excluding null terminator
char *buf = malloc(needed + 1);
snprintf(buf, needed + 1, "%d %s", 42, "test");
```

### The scanf Family

`scanf` returns the **number of fields successfully matched**. `sscanf` is very convenient for parsing from strings:

```c
int x, y;
sscanf("10:20", "%d:%d", &x, &y); // Returns 2, x=10, y=20
```

> ⚠️ **Pitfall Warning**: `scanf`'s `%s` does not check buffer size. The safe approach is to use `%ms` (GNU extension) to specify the maximum length, or switch to the `fgets` + `sscanf` combination.

### Common Format Specifiers

| Specifier | Type | Specifier | Type |
|-----------|------|-----------|------|
| `%d` | int | `%f` | double |
| `%u` | unsigned | `%s` | string |
| `%x` | hex | `%zu` | size_t |
| `%ld` | long | `%lld` | long long |
| `%p` | pointer | `%%` | Literal % |

## Step 3 — Understanding Text Mode vs. Binary Mode

On Windows, text mode automatically converts `\r\n` to `\n`, while binary mode makes no conversion. On Linux/macOS, there is almost no difference between the two. When handling binary data (images, structure dumps, protocol frames), always use `"rb"`/`"wb"`.

> ⚠️ **Pitfall Warning**: If you read a binary file in text mode on Windows, the read will terminate early when encountering a `0x1A` byte—because `0x1A` (Ctrl+Z) is treated as EOF in Windows text mode. This is a classic cross-platform trap.

## Step 4 — Error Handling with errno

`errno` (in `<errno.h>`) is a global error code variable. Functions do **not** clear `errno` on success; they only set it when an error occurs. The correct practice is to check the return value first to confirm an error, and then read `errno`.

`perror` concatenates your passed string with the system error message and outputs it:

```c
if (ferror(fp)) {
    perror("File read failed"); // Prints: File read failed: Error description
}
```

`strerror` returns the string description corresponding to the error code, suitable for use in custom error messages.

## Step 5 — Handling Command-Line Arguments

```c
int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: %s <file>\n", argv[0]);
        return 1;
    }
    // argv[1] is the first argument
}
```

`argv[0]` is the program name, `argv[1]` through `argv[argc-1]` are the arguments, and `argv[argc]` is `NULL`.

## Standard Library Quick Reference

### `<stdlib.h>`: General Utilities

`atoi` is simple but offers no error detection; `strtol` is safer (can detect overflow and partial parsing). `qsort` for quicksort, `bsearch` for binary search, both using function pointers for comparison. `rand`/`srand` pseudo-random numbers have poor randomness quality; they are sufficient but don't rely on them for security-related tasks.

### `<math.h>`: Math Functions

Trigonometric functions (sin/cos/tan), exponential/logarithmic (pow/sqrt/log/exp), rounding (ceil/floor/round), absolute value (fabs). All have three versions: float (f suffix), double, and long double (l suffix).

> ⚠️ **Pitfall Warning**: Linking the math library on GCC/Linux requires the `-lm` option. If you forget to add this option, the compiler will report `undefined reference to 'pow'` or similar errors—the code itself is fine, just missing a link option.

### `<ctype.h>`: Character Classification

`isdigit`/`isalpha`/`isalnum`/`isxdigit`/`isupper`/`islower` determine character categories; `toupper`/`tolower` convert case. Arguments must be cast to `unsigned char` first, otherwise negative values of signed `char` can lead to undefined behavior.

### `<assert.h>`: Assert Macro

```c
assert(ptr != NULL); // If false, abort program
```

Defining `NDEBUG` removes all asserts completely. Used to catch programming errors, not to handle runtime errors.

### `<stddef.h>`: Fundamental Types

`sizeof` (object size), `NULL` (null pointer), `offsetof` (structure member offset), `ptrdiff_t` (pointer difference). `size_t` is unsigned; watch out for underflow when iterating in reverse: `for (size_t i = n; i-- > 0;)` is the safe way to write it.

## C++ Bridge

### Stream Library (iostream/fstream/sstream)

The C++ stream library achieves **type safety** through operator overloading—passing the wrong type results in a compilation failure. Destructors automatically close files (RAII). `std::string` is returned directly by `std::getline`, eliminating buffer overflow risks.

### std::filesystem (C++17)

Cross-platform directory traversal, file attribute queries, path manipulation—no more need to write `#ifdef _WIN32`.

### std::format (C++20)

Combines the concise syntax of printf with type safety:

```cpp
std::string s = std::format("Year: {}", 2025);
```

### std::span (C++20)

`std::span` binds a pointer and a length together, solving the long-standing problem of array decay losing length information.

### `<system_error>`

`std::error_code` is a value type and thread-safe, much safer than the global `errno`.

## Summary

The core of file operations lies in `fopen` and `fread`/`fwrite`/`fseek`/`ftell`. Formatted I/O relies on the `printf`/`scanf` family, and error handling depends on `errno` + `perror`. The standard library provides fundamental tools like numeric conversion, sorting/searching, math functions, character classification, and assertions. C++ has comprehensively upgraded these tools for type safety using the stream library, `std::filesystem`, `std::format`, and `std::span`.

## Exercises

### Exercise 1: Configuration File Parser

Parse a configuration file in `.ini` format, ignoring `#` comments and empty lines.

```text
# config.ini
port=8080
mode=debug
```

Hint: Use `fgets` to read line by line, `strchr` to find the `=` position, and trim whitespace.

### Exercise 2: File Copy Tool

Specify source and target files via command-line arguments, support binary file copying, and display progress.

Hint: Use `fseek` + `ftell` to get the source file size, and use `\r` to overwrite the same line to implement a progress bar.
