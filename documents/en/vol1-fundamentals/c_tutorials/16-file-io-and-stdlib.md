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
  source_hash: 39c4341765fc67babac6c544ebcb84395a3dd9b635a2fc9e4b6e67a55e357c89
  translated_at: '2026-09-25T13:40:45+00:00'
  engine: anthropic
  token_count: 7000
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
    // ... read/write operations ...
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

// Write
size_t written = fwrite(records, sizeof(Record), count, fp);

// Read
size_t count = fread(buffer, sizeof(Record), max_count, fp);
```

The return value is the number of **complete items** processed, not a byte count. If it is smaller than the number of items you requested, either you have reached the end of the file or an error has occurred.

### Moving the File Position and Getting the Size

`fseek` moves the position indicator, and `ftell` reports the current position. One practical pattern is getting the file size:

```c
long get_file_size(FILE* fp) {
    // Save the position from before the call.
    long original = ftell(fp);
    // Move the file position to the end of the file.
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    // Restore the position from before the call.
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
    // Output: fopen failed: No such file or directory
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
assert(arr != NULL);   // Debug: terminates the program if the condition is false
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

/// @brief Strip leading and trailing whitespace from a string
char* trim(char* str);

/// @brief Parse a configuration file
size_t parse_config(const char* path, ConfigEntry* entries, size_t max_entries);

/// @brief Look up a given key among the config entries
const char* find_config(const ConfigEntry* entries, size_t count, const char* key);

int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <config_file>\n", argv[0]);
        return 1;
    }
    // Exercise: call parse_config and find_config
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
/// @brief Strip leading and trailing whitespace from a string
char* trim(char* str);

/// @brief Parse a configuration file
size_t parse_config(const char* path, ConfigEntry* entries, size_t max_entries);

/// @brief Look up a given key among the config entries
const char* find_config(const ConfigEntry* entries, size_t count, const char* key);

/**
 * @brief Strip leading and trailing whitespace from a string (modifies it in place, returns a pointer to the start of the result)
 * @param str the string to process (it will be modified)
 * @return char* pointer to the first character of the string after whitespace removal
 *
 */

char* trim(char* str) {
    char* end;  // pointer used to scan backward from the end of the string

    // Skip all whitespace at the start of the string (spaces, tabs, newlines, etc.)
    while (isspace((unsigned char)*str)) {
        ++str;
    }

    // If the string has already ended after skipping the leading whitespace (the whole line was whitespace), return the empty string directly
    if (*str == '\0') {
        return str;
    }

    // end points at the position of the last character (skipping the trailing '\0')
    end = str + strlen(str) - 1;
    // Skip trailing whitespace from back to front until a valid character is found or we are back at the start of the string
    while (end > str && isspace((unsigned char)*end)) {
        --end;
    }
    // Write the terminator after the last valid character, cutting off the trailing whitespace
    end[1] = '\0';
    return str;
}

/**
 * @brief Parse a configuration file (each line has the form key=value; lines starting with # are comments and are ignored)
 * @param path        path of the configuration file
 * @param entries     array that receives the parsed config entries
 * @param max_entries maximum number of entries the array can hold
 * @return number of parsed entries; PARSE_CONFIG_ERROR on error
 *
 * Processing logic:
 *   1. Validate the arguments;
 *   2. Open the file; on failure, print an error message and return PARSE_CONFIG_ERROR;
 *   3. Read line by line:
 *        - strip the comment part after the in-line '#';
 *        - trim leading and trailing whitespace with trim;
 *        - look for the '=' separator; skip the line if it is not found;
 *        - split at '=' into key and value, trimming each;
 *        - copy safely into the entries array with snprintf and count it; return an error once max_entries is exceeded.
 */
size_t parse_config(const char* path, ConfigEntry* entries, size_t max_entries)
{
    FILE* file;                                 // file stream pointer
    char line[MAX_LINE];                        // buffer for one line of text; its size is set by MAX_LINE
    size_t count = 0;                           // number of config entries parsed so far
    size_t line_number = 0;                     // current physical line number being read

    // Argument validation: the path and the array must not be NULL, and the array capacity must be greater than 0.
    if (path == NULL || entries == NULL || max_entries == 0) {
        return PARSE_CONFIG_ERROR;
    }

    // Open the configuration file in read-only text mode
    file = fopen(path, "r");
    // Open failed: print the system error message with perror.
    if (file == NULL) {
        perror(path);
        return PARSE_CONFIG_ERROR;
    }

    // Read through the whole file: as soon as valid entries exceed the array capacity, report an error instead of silently dropping them.
    while (fgets(line, sizeof(line), file) != NULL) {
        char* comment = strchr(line, '#');      // look for the '#' that starts a comment
        char* equal;                            // pointer to the '=' separator
        char* key;                              // points to the key part
        char* value;                            // points to the value part
        int next_char;
        int key_length;
        int value_length;

        ++line_number;

        // When the buffer is full, read one character ahead to tell "it fit exactly" from a genuinely over-long line
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

        // If the line contains a '#', truncate there and ignore all comment content after it
        if (comment != NULL) {
            *comment = '\0';
        }

        // Trim leading and trailing whitespace to get the candidate key string
        key = trim(line);
        // Empty after trimming means a blank line; skip it
        if (*key == '\0') {
            continue;
        }

        // Look for the '=' separator
        equal = strchr(key, '=');
        // No '=' means this is not a valid key=value line; skip it
        if (equal == NULL) {
            continue;
        }

        // Replace '=' with '\0' to cut the string at the separator, while keeping the original content after '=' intact
        *equal = '\0';
        // Handle the key (before '=') and the value (after '=') separately, trimming each
        key = trim(key);
        value = trim(equal + 1);
        // An empty key after trimming means there is no valid key name; skip it
        if (*key == '\0') {
            continue;
        }

        if (count == max_entries) {
            fprintf(stderr, "%s:%zu: 配置项数量超过上限 %zu\n", path,
                    line_number, max_entries);
            fclose(file);
            return PARSE_CONFIG_ERROR;
        }

        // After copying, check the snprintf return values and reject any truncated key or value
        key_length = snprintf(entries[count].key, sizeof(entries[count].key), "%s", key);
        value_length = snprintf(entries[count].value, sizeof(entries[count].value), "%s", value);
        if (key_length < 0 || value_length < 0 ||
            (size_t)key_length >= sizeof(entries[count].key) ||
            (size_t)value_length >= sizeof(entries[count].value)) {
            fprintf(stderr, "%s:%zu: 键名或值过长\n", path, line_number);
            fclose(file);
            return PARSE_CONFIG_ERROR;
        }
        ++count;    // increment the parsed count
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
 * @brief Look up a given key in the config entry array and return the corresponding value
 * @param entries the config entry array
 * @param count   number of valid elements in the array
 * @param key     the key name to look for
 * @return const char* pointer to the corresponding value when found; NULL when not found or on invalid arguments
 */
const char* find_config(const ConfigEntry* entries, size_t count, const char* key) {
    size_t i;   // loop index

    // Argument validation: neither the array nor the lookup key may be NULL
    if (entries == NULL || key == NULL) {
        return NULL;
    }

    // Walk every config entry, comparing key names with strcmp
    for (i = 0; i < count; ++i) {
        if (strcmp(entries[i].key, key) == 0) {
            return entries[i].value;   // matching key found; return a pointer to its value
        }
    }

    return NULL;    // finished the loop without a match; return NULL
}

/**
 * @brief Program entry point: read and parse the configuration file named on the command line, then print all config entries
 * @param argc number of command-line arguments (including the program name)
 * @param argv command-line argument array; argv[1] should be the configuration file path
 * @return int program exit code: 0 for success, 1 for a usage error, 2 for a configuration parse failure
 */
int main(int argc, char* argv[])
{
    ConfigEntry entries[32];    // the array holds at most 32 config entries
    size_t count;               // number of entries actually parsed
    size_t i;                   // loop index

    // Check the argument count: it must be the program name plus the configuration file path.
    if (argc != 2) {
        // Print a usage message to standard error and return a non-zero exit code
        fprintf(stderr, "用法: %s <配置文件>\n", argv[0]);
        return 1;
    }

    // Compute how many elements the array can hold, then call parse_config to parse the configuration file
    count = parse_config(argv[1], entries, sizeof(entries) / sizeof(entries[0]));
    if (count == PARSE_CONFIG_ERROR) {
        return 2;
    }
    // Walk through and print every parsed entry (in key=value format)
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

/// @brief Copy a file
int copy_file(const char* src_path, const char* dst_path)
{
    // Exercise: implement it
    // 1. Open the source file with "rb" and the destination file with "wb"
    // 2. Loop fread/fwrite
    // 3. Use fseek/ftell to get the total size and print progress
    // 4. Error handling: close in the reverse order of opening
    return -1;
}

int main(int argc, char* argv[]) {
    // Exercise: parse the command-line arguments and call copy_file
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

/* Check whether the two paths point to the same existing file, so that opening the destination does not truncate the source. */
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

/// @brief Copy a file
/// @param src_path source file path
/// @param dst_path destination file path
/// @return 0 on success, -1 on failure
int copy_file(const char *src_path, const char *dst_path)
{
    FILE *src = NULL;                /* source file pointer, initialized to NULL */
    FILE *dst = NULL;                /* destination file pointer, initialized to NULL */
    unsigned char buffer[BUFFER_SIZE]; /* buffer for reading and writing data */
    long total_size;                 /* total size of the source file (bytes) */
    long copied_size = 0;            /* number of bytes copied so far */
    int result = -1;                 /* function return value; failure by default */
    int dst_opened = 0;              /* whether the destination file was ever opened successfully */
    int progress_active = 0;         /* whether a progress line without a newline is currently on screen */

    if (src_path == NULL || dst_path == NULL) {
        fprintf(stderr, "源文件和目标文件路径不能为空\n");
        return -1;
    }

    if (same_file(src_path, dst_path)) {
        fprintf(stderr, "源文件和目标文件不能是同一个文件: '%s'\n", src_path);
        return -1;
    }

    /* Open the source file in read-only binary mode */
    src = fopen(src_path, "rb");
    if (src == NULL) {
        fprintf(stderr, "无法打开源文件 '%s'\n", src_path);
        goto cleanup;                /* skip the remaining steps; go straight to cleanup and return */
    }

    /* Move the file pointer to the end of the file so we can read the size */
    if (fseek(src, 0, SEEK_END) != 0) {
        fprintf(stderr, "无法获取源文件大小 '%s'\n", src_path);
        goto cleanup;
    }

    /* The current file pointer position is the file's total size */
    total_size = ftell(src);
    if (total_size < 0) {
        fprintf(stderr, "无法获取源文件大小 '%s'\n", src_path);
        goto cleanup;
    }

    /* Move the file pointer back to the beginning of the file, ready to read the content */
    if (fseek(src, 0, SEEK_SET) != 0) {
        fprintf(stderr, "无法定位源文件 '%s'\n", src_path);
        goto cleanup;
    }

    /* Create/overwrite the destination file in write-only binary mode */
    dst = fopen(dst_path, "wb");
    if (dst == NULL) {
        fprintf(stderr, "无法打开目标文件 '%s'\n", dst_path);
        goto cleanup;
    }
    dst_opened = 1;

    /* An empty source file (size 0) shows the 100% progress immediately */
    if (total_size == 0) {
        printf("进度: 100%%\n");
    }

    /* Loop: read the source file's content and write it to the destination file */
    while (1) {
        /* Read at most one buffer's worth of bytes from the source file */
        size_t bytes_read = fread(buffer, 1, sizeof(buffer), src);

        if (bytes_read > 0) {
            /* Write what was read into the destination file */
            size_t bytes_written = fwrite(buffer, 1, bytes_read, dst);

            /* A written byte count that does not match means the write failed */
            if (bytes_written != bytes_read) {
                if (progress_active) {
                    putchar('\n');
                    fflush(stdout);
                    progress_active = 0;
                }
                fprintf(stderr, "写入目标文件失败 '%s'\n", dst_path);
                goto cleanup;
            }

            /* Accumulate the copied byte count, then compute and print the progress percentage */
            copied_size += (long)bytes_written;
            if (total_size > 0) {
                int percent = (int)(((long double)copied_size * 100.0L) /
                                    (long double)total_size);
                if (percent > 100) {
                    percent = 100;
                }
                printf("\r进度: %d%%", percent);     /* \r moves the cursor back to the start of the line, overwriting the old progress */
                fflush(stdout);                     /* force-flush the output buffer so the progress shows immediately */
                progress_active = 1;
            }
        }

        /* Fewer bytes read than the buffer size means the end of the file has been reached */
        if (bytes_read < sizeof(buffer)) {
            if (ferror(src)) {                      /* check whether the read failed */
                if (progress_active) {
                    putchar('\n');
                    fflush(stdout);
                    progress_active = 0;
                }
                fprintf(stderr, "读取源文件失败 '%s'\n", src_path);
                goto cleanup;
            }
            break;                                  /* end of file reached normally; end the loop */
        }
    }

    /* After the loop, show the 100% progress once more and end the line */
    if (total_size > 0) {
        printf("\r进度: 100%%\n");
        progress_active = 0;
    }

    /* Close the destination file explicitly and check for errors */
    if (fclose(dst) != 0) {
        dst = NULL;
        fprintf(stderr, "关闭目标文件失败 '%s'\n", dst_path);
        goto cleanup;
    }
    dst = NULL;                                     /* closed successfully; set to NULL to prevent a double close */

    if (fclose(src) != 0) {
        src = NULL;
        fprintf(stderr, "关闭源文件失败 '%s'\n", src_path);
        goto cleanup;
    }
    src = NULL;

    result = 0;                                     /* everything succeeded; mark success */

cleanup:
    if (progress_active) {
        putchar('\n');
        fflush(stdout);
    }
    /* Unified cleanup: close any handle that is not NULL, avoiding resource leaks */
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
    return result;      /* return the result */
}

int main(int argc, char *argv[])
{
    /* Argument count check: program name + source file path + destination file path, 3 in total */
    if (argc != 3) {
        fprintf(stderr, "用法: %s <源文件> <目标文件>\n", argv[0]);
        return EXIT_FAILURE;
    }

    /* Call the copy function; a non-zero result means failure */
    if (copy_file(argv[1], argv[2]) != 0) {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;    /* exit successfully */
}
```

Compile and run:

```bash
gcc -std=c17 -Wall -Wextra -Wpedantic file_copy.c -o file_copy

# A. Basic function: copy + verify
head -c 1048576 /dev/urandom > /tmp/src.bin     # 1MB of random binary data
./file_copy /tmp/src.bin /tmp/dst.bin            # you should see the progress bar refresh in place with \r
cmp /tmp/src.bin /tmp/dst.bin && echo "OK 内容一致"

# B. Empty file (size 0; should show 100%)
: > /tmp/empty.bin
./file_copy /tmp/empty.bin /tmp/e.bin; wc -c /tmp/e.bin   # the correct result is 0

# C. Large file (watch the progress climb, ending at 100%)
head -c 100000000 /dev/urandom > /tmp/big.bin    # 100MB; you can watch the percentage tick up
./file_copy /tmp/big.bin /tmp/big_dst.bin

# D. Error handling (each should print an error; the exit code is non-zero)
./file_copy                                             # missing arguments → usage message
./file_copy /tmp/src.bin /tmp/src.bin; echo $?           # same file → refused
./file_copy /tmp/没有的文件 /tmp/x.bin; echo $?           # source missing → error
./file_copy /tmp/src.bin /tmp/不存在目录/x.bin; echo $?   # destination not writable → error

```

Result (during a normal copy, `\r` keeps refreshing the progress in place on one line, so the terminal ultimately retains only `100%`):

```text
# A. Copy src.bin → dst.bin; the progress bar keeps refreshing on the same line and finally rests at 100%
进度: 100%
OK 内容一致

# B. Empty file: shows 100% immediately; the destination file's size is 0
进度: 100%
0 /tmp/e.bin

# D. Error handling (each item prints one error line; every exit code is non-zero)
用法: ./file_copy <源文件> <目标文件>
源文件和目标文件不能是同一个文件: '/tmp/src.bin'
无法打开源文件 '/tmp/没有的文件'
无法打开目标文件 '/tmp/不存在目录/x.bin'
```

Notice that no intermediate percentages appear in the output of a successful copy: `\r` moves the cursor back to the start of the line and each new percentage directly overwrites the old one, so to the naked eye there is just one progress jump from 0% to 100%. Only if you pipe the program somewhere or redirect its output to a file do those step-by-step intermediate percentages survive. Note also that `long` with `ftell` is used here to demonstrate getting the size of an ordinary seekable file; on 32-bit systems it typically tops out around 2 GiB, and when handling very large files you should switch to the large-file positioning interface your target platform provides.

:::

Hint: use `fseek` + `ftell` to get the source file's size, and overwrite the same line with `\r` to build the progress bar.
