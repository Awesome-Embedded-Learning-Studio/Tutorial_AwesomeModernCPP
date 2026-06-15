---
chapter: 5
cpp_standard:
- 11
- 14
- 17
- 20
description: Master `std::string` construction, concatenation, searching, and substring
  operations, and learn to handle strings safely and efficiently in C++.
difficulty: beginner
order: 3
platform: host
prerequisites:
- std::array
reading_time_minutes: 14
tags:
- cpp-modern
- host
- beginner
- 入门
- 基础
title: std::string
translation:
  engine: anthropic
  source: documents/vol1-fundamentals/ch05/03-std-string.md
  source_hash: aced1be774180f534a1b374bc257368dd47eb18d4100eafc35e92e4b2e6f90cc
  token_count: 2725
  translated_at: '2026-05-26T10:50:09.490984+00:00'
---
# std::string

In the previous tutorial, we spent a lot of time wrestling with C-style strings—manually managing the ``\0`` terminator, carefully preventing buffer overflows, and treading on thin ice with ``strncpy`` and ``snprintf`` when manipulating every character array. If you're as exhausted by this as I am, you'll breathe a sigh of relief at this next piece of news: the C++ standard library provides a true string type called ``std::string``. It automatically manages memory, handles length automatically, supports intuitive concatenation and comparison, and essentially fills in all the pitfalls we encountered in C.

In this chapter, we start with the various ways to construct a ``std::string``, then move on to concatenation, searching, substring extraction, and interoperability with C strings. Finally, we tie all the knowledge together with a comprehensive string processing program. After completing this chapter, you'll find that those blood-pressure-raising string operations (I know from experience—after learning `std::string`, I sometimes couldn't even figure out how to use C strings properly anymore) can be written both safely and elegantly in C++.

> **Learning Objectives**
>
> After completing this chapter, you will be able to:
>
> - [ ] Construct ``std::string`` objects in multiple ways
> - [ ] Perform string concatenation, insertion, deletion, and replacement
> - [ ] Master search and substring operations like ``find`` and ``substr``
> - [ ] Correctly convert between C++ strings and C-style strings
> - [ ] Use conversion functions like ``std::to_string`` and ``std::stoi``

## Environment Setup

We will run all of the following experiments in this environment:

- Platform: Linux x86\_64 (WSL2 is also fine)
- Compiler: GCC 13+ or Clang 17+
- Compiler flags: ``-Wall -Wextra -std=c++17``

## Step 1 — Constructing a string in Various Ways

``std::string`` provides a rich set of constructors that covers almost every scenario you can think of:

````cpp
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
````

Output:

````text
hello
xxxxxxxxxx
hello
ell
hello world
s6 empty: true
````

The first and fifth approaches look like assignment, but the compiler actually performs construction—this is C++'s copy initialization syntax, which has the same effect as ``std::string s1("hello")``. ``std::string s4(s1, 1, 3)`` extracts 3 characters starting from index 1 of ``s1``, resulting in ``"ell"``—this "partial construction" is extremely useful when parsing strings. We don't need to dive deep into move construction right now; just know that it's faster than copying because it "steals" the internal resources instead of duplicating them.

> ⚠️ **Pitfall Warning**
> After being moved from, the source object (``s5`` above) is in a "valid but unspecified" state—you can assign to it and destruct it, but do not read its value for any meaningful logic. This is the fundamental contract of C++ move semantics, and we will elaborate on it in a later chapter when we cover move semantics.

## Step 2 — Basic Operations: Size, Access, and Empty Checks

````cpp
std::string s = "Hello, C++";
s.size();       // 10
s.length();     // 10（和 size 等价）
s.empty();      // false
s[0];           // 'H'
s.at(1);        // 'e'（越界时抛 std::out_of_range）
s.front();      // 'H'
s.back();       // '+'
````

``size()`` and ``length()`` are completely equivalent. Most C++ developers prefer ``size()`` because it stays consistent with other standard library containers.

Both ``operator[]`` and ``at()`` can access characters via index, but they differ in out-of-bounds behavior: ``s[100]`` performs no checks whatsoever, resulting in undefined behavior (UB); ``s.at(100)``, on the other hand, throws a ``std::out_of_range`` exception. If you aren't one hundred percent sure about the boundaries, using ``at()`` is much safer—compared to spending two hours tracking down a memory out-of-bounds bug, this tiny performance overhead is negligible.

> ⚠️ **Pitfall Warning**
> ``std::string``'s ``size()`` returns the number of underlying ``char`` elements, not the "number of characters visible to the naked eye." For pure ASCII strings, the two are identical, but if the string contains UTF-8 encoded Chinese characters, ``std::string s = "你好";``'s ``s.size()`` will be 6 instead of 2, because each Chinese character takes up 3 bytes. Correctly handling Unicode strings requires dedicated libraries (like ICU), but you need to be aware of this pitfall in advance.

## Step 3 — Concatenation, Insertion, Deletion, and Replacement

````cpp
std::string s = "Hello";
s += " World";          // "Hello World"
s.append("!!!");        // "Hello World!!!"
s.push_back('?');       // "Hello World!!!?"
s.insert(5, ",");       // "Hello, World!!!?"
s.erase(5, 1);          // "Hello World!!!?"  删掉刚才插入的逗号
s.replace(6, 5, "C++"); // "Hello C++!!!?"    World -> C++
s.clear();              // 变成空字符串
````

``+=`` and ``append()`` have similar functionality; ``+=`` is more concise, while ``append()`` provides more overloaded versions (such as appending only a specific segment of another string). ``push_back()`` can only append a single character, consistent with the ``push_back()`` interface of ``vector``. ``insert(pos, str)`` inserts ``str`` at position ``pos``, ``erase(pos, len)`` deletes ``len`` characters starting from ``pos``, and ``replace(pos, len, new_str)`` replaces ``len`` characters starting at ``pos`` with ``new_str``. The new string's length can differ from the replaced portion.

These operations are safe because ``std::string`` automatically manages memory internally—it automatically expands capacity when there isn't enough space during insertion, and you don't need to manually shift subsequent characters when deleting. Compared to the old days in C of manually calculating offsets and cautiously calling ``memmove``, this is absolute paradise.

## Step 4 — Searching and Substrings

````cpp
std::string s = "Hello, hello, HELLO!";

s.find("hello");                    // 7（区分大小写）
s.find("Hello");                    // 0
s.find("xyz");                      // std::string::npos
s.find("hello", 2);                 // 7（从位置 2 开始找）
s.rfind("hello");                   // 7（反向查找）
s.find_first_of("aeiou");          // 1（第一个元音字母 e）
s.find_last_of("aeiou");           // 16（最后一个元音字母 O... 不对，是 O 的小写位置）
````

The most critical concept here is ``std::string::npos``. It is a constant whose value is the maximum value of ``std::size_t``. When a search operation fails to find the target, it returns ``npos``. Therefore, after every call to ``find``, you must check whether the return value equals ``npos``, rather than using it as a bool—because ``npos`` converted to a bool is ``true``, and writing ``if (s.find("x"))`` directly would actually enter the branch when nothing is found. This is another classic beginner trap.

The behavior of ``find_first_of`` and ``find_last_of`` is somewhat special: they don't search for an entire substring, but rather for **any single character** from the parameter string. ``find_first_of("aeiou")`` returns 1, because ``s[1]`` is ``'e'``, which is the first character matched in ``"aeiou"``.

Substring extraction uses ``substr(pos, len)``, which extracts ``len`` characters starting from position ``pos`` and returns a new ``std::string``. Omitting ``len`` extracts to the end:

````cpp
std::string t = "Hello, World!";
t.substr(7, 5);  // "World"
t.substr(7);     // "World!"
````

``substr()`` returns a new object, which allocates memory and copies the characters. If you only need to iterate over a range and don't require an independent copy, using ``std::string_view`` (C++17) is more efficient—we'll expand on this in a later chapter.

## Step 5 — Comparing Strings

In C, comparing two strings requires ``strcmp``. C++'s ``std::string`` overloads comparison operators, making it much more intuitive:

````cpp
std::string a = "apple", b = "banana", c = "apple";
a == c;      // true
a != b;      // true
a < b;       // true（字典序）
a.compare(b);  // 负数（等价于 strcmp 的返回值语义）
````

The advantage of the ``compare()`` member function is that it supports partial comparison. For example, ``s.compare(7, 5, "World")`` takes the 5 characters starting at index 7 of ``s`` and compares them for equality against ``"World"``. This capability comes in handy when parsing protocols or processing fixed-format text.

## Step 6 — Interoperability with C Strings

No matter how great ``std::string`` is, many third-party libraries, operating system APIs, and embedded SDKs still accept ``const char*``. Getting a C-style string from a ``std::string`` requires two key functions:

````cpp
std::string s = "Hello, C API!";
const char* p = s.c_str();   // 返回以 \0 结尾的 const char*
const char* q = s.data();    // C++17 起与 c_str() 完全等价
````

``c_str()`` guarantees returning a ``const char*`` terminated by ``\0``, which can be passed directly to ``fopen``, ``printf``, or any other function expecting a C string. Starting in C++17, ``data()`` behaves exactly the same as ``c_str()``.

Here is a rule you must memorize: the pointers returned by ``c_str()`` and ``data()`` are **owned by the string object**. Once the string is modified or destroyed, the pointers become invalid. Therefore, never store the return value of ``c_str()`` and then perform operations that might change the string—complete all modifications first, and only call ``c_str()`` at the very end to pass it to a C API.

## Step 7 — Numeric Conversion and Line Input

````cpp
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
````

The result of ``std::to_string`` for floating-point numbers might not look very "pretty"—``to_string(3.14)`` outputs ``3.140000`` because it uses ``%f`` formatting. If you need precise control over floating-point output formatting, you still need to use ``std::setprecision`` from ``<iomanip>`` or ``std::snprintf``.

## Practical Exercise — Comprehensive String Processing

Now let's combine all the knowledge we've learned so far and write a slightly more practical string processing program. This program demonstrates several common text processing patterns: splitting by delimiters, counting character frequencies, find-and-replace, and simple CSV parsing.

````cpp
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
````

Compile and run:

````bash
g++ -std=c++17 -Wall -Wextra -o string_demo string_demo.cpp
./string_demo
````

Output:

````text
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
````

Let's walk through the logic of each function. The core of ``split_into_words`` is repeatedly calling ``find_first_not_of`` to skip whitespace, then using ``find`` to locate the next delimiter, and finally using ``substr`` to extract the word. This "skip whitespace, find delimiter, extract, loop" pattern is extremely common in text processing, and we recommend memorizing it as a standard recipe.

``count_char_frequency`` uses a ``std::map`` to count frequencies. Internally, a ``std::map`` is sorted, so the output is arranged in lexicographical order by character. Here we use an associative container for the first time; you don't need to understand all the details yet—just know that it's a collection of "key-value" pairs, and when accessing via ``[]``, if the key doesn't exist, it automatically creates a default value (``int`` is 0).

``find_and_replace`` demonstrates an important pattern: when doing ``find`` + ``replace`` in a loop, you must move the search starting position to after the replacement result each time. Otherwise, if ``replacement`` contains the content of ``target``, you'll end up in an infinite loop. The logic of ``parse_csv_line`` is similar to splitting words, just with commas as the delimiter instead.

## Exercises

These three exercises cover the most core operations of ``std::string``. We recommend writing the code yourself before checking against the suggested approaches.

### Exercise 1: Word Counter

Write a function ``count_words(const std::string& s)`` that counts how many words are in a string (separated by spaces, ignoring consecutive spaces and leading/trailing spaces). Hint: you can use a loop with ``find`` and ``find_first_not_of``, or you can count the "number of transitions from whitespace to non-whitespace."

### Exercise 2: Simple Find-and-Replace Tool

Write a function ``replace_all(std::string text, const std::string& from, const std::string& to)`` that replaces all occurrences of ``from`` in ``text`` with ``to``. Requirement: handle the case where ``from`` is an empty string (return the original text directly, otherwise ``find("")`` will return 0 and cause an infinite loop).

### Exercise 3: trim Function

Write two functions, ``ltrim`` and ``rtrim``, to remove whitespace characters (spaces, ``\t``, ``\n``) from the beginning and end of a string, respectively. Then combine them into a ``trim`` function. Hint: for ``ltrim``, use ``find_first_not_of(" \t\n")`` to find the first non-whitespace character and then ``substr``; ``rtrim`` is similar, using ``find_last_not_of``.

## Summary

In this chapter, starting from the various pain points of C-style strings, we learned about ``std::string``, the string type provided by the C++ standard library. Let's review the core takeaways:

- ``std::string`` automatically manages memory, requiring no manual allocation or deallocation, which fundamentally eliminates buffer overflow issues
- Diverse construction methods: literals, repeated characters, copying, partial extraction, and ``+`` concatenation, covering common use cases
- The ``find`` family of functions and ``substr`` are core tools for text processing, with ``npos`` serving as the sentinel value for "not found"
- ``c_str()`` and ``data()`` provide a bridge for interoperability with C APIs, but you must pay attention to pointer lifetimes
- Functions like ``std::to_string`` and ``std::stoi``/``std::stod`` address the need for conversion between strings and numeric values

With this, the content of Chapter 5, "Arrays and Strings," is fully concluded. We started from the most basic C arrays, went through the low-level perspective of pointer arithmetic, and finally arrived at the high-level abstraction of ``std::string``. This path itself reflects C++'s design philosophy: **low-level capabilities are not diminished in the slightest, but the standard library provides safe and easy-to-use tools at the higher level**. Next, in Chapter 6, we will enter the world of C++ object-oriented programming—classes and objects. That is the true stage for C++.

---

> **Self-Assessment**: If you're still unsure about the mechanism of checking ``find`` against ``npos``, we suggest going back and retyping the code from the "Searching and Substrings" section, paying special attention to the update logic of ``pos`` within the loop. String operations are the foundation for all future projects, so spending a little extra time here is absolutely worth it.
