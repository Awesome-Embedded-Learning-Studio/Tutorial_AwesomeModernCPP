---
chapter: 5
cpp_standard:
- 11
- 14
- 17
- 20
description: Master std::string construction, concatenation, searching, and substring operations, and learn to handle strings safely and efficiently in C++
difficulty: beginner
order: 3
platform: host
prerequisites:
- std::array
reading_time_minutes: 15
tags:
- cpp-modern
- host
- beginner
- 入门
- 基础
title: std::string
translation:
  source: documents/vol1-fundamentals/ch05/03-std-string.md
  source_hash: d6861e58d0e90bdbbea19c1cb1b11d8f26dfcb98e8a29c5d81150338859c5b57
  translated_at: '2026-09-25T10:53:18+00:00'
  engine: anthropic
  token_count: 8200
---
# std::string: Finally, No More Babysitting \0

In the previous tutorial we spent page after page wrestling with C-style strings: managing the `\0` terminator by hand, tiptoeing around buffer overflows, and edging through every character array with `strncpy` and `snprintf` as if walking on thin ice. If all that has worn you out as much as it wore me out, here is some news that will let you breathe a huge sigh of relief: the C++ standard library hands us a genuine string type called `std::string`. It manages memory automatically, tracks its own length, supports intuitive concatenation and comparison, and pretty much fills in every pit we fell into back in C.

In this chapter we start with the ways to construct a `std::string`, walk through concatenation, searching, substring extraction, and interoperability with C strings, and finally tie all of that knowledge together in one comprehensive string-processing program. By the end you will find that the string operations that used to send your blood pressure through the roof (mine included—after I first learned `std::string`, I sometimes got worse at using C strings) can be written in C++ both safely and effortlessly.

## The Many Ways to Construct a String

`std::string` offers a generous set of constructors that cover almost every scenario you can think of:

```cpp
// string_construct.cpp
#include <iostream>
#include <string>

int main()
{
    // Construct from a literal
    std::string s1 = "hello";
    // Repeated characters: 10 'x's
    std::string s2(10, 'x');
    // Copy construction
    std::string s3(s1);
    // Construct from part of another string (start position, length)
    std::string s4(s1, 1, 3);  // "ell"
    // Construct by concatenating directly with +
    std::string s5 = s1 + " world";
    // Empty string
    std::string s6;
    // Move construction (C++11)
    std::string s7 = std::move(s5);

    std::cout << s1 << "\n" << s2 << "\n" << s3 << "\n"
              << s4 << "\n" << s7 << "\n"
              << "s6 empty: " << std::boolalpha << s6.empty() << "\n";
    return 0;
}
```

Output:

```text
hello
xxxxxxxxxx
hello
ell
hello world
s6 empty: true
```

The first and fifth forms look like assignment, but what the compiler actually performs is construction—this is C++'s copy-initialization syntax, and it has exactly the same effect as `std::string s1("hello")`. `std::string s4(s1, 1, 3)` takes 3 characters starting at index 1 of `s1`, giving `"ell"`; this kind of "partial construction" is extremely handy when parsing strings. We don't need to dig into move construction just yet—all we need to know is that it is faster than copying, because it "steals" the internal resources instead of making a duplicate of them.

After being moved from, the source object (`s5` above) is left in a "valid but unspecified" state—we may assign to it and destroy it, but we must not read its value and draw any meaningful conclusion from it. This is the fundamental contract of C++ move semantics, and we will expand on it in detail when later chapters get to move semantics.

## Basic Operations: Size, Access, and Emptiness

```cpp
std::string s = "Hello, C++";
s.size();       // 10
s.length();     // 10 (equivalent to size)
s.empty();      // false
s[0];           // 'H'
s.at(1);        // 'e' (throws std::out_of_range when out of range)
s.front();      // 'H'
s.back();       // '+'
```

`size()` and `length()` are completely equivalent. Most C++ developers lean toward `size()` because it stays consistent with the other standard library containers, so that is the convention we will follow too.

Both `operator[]` and `at()` access a character by index; the difference lies in out-of-bounds behavior: `s[100]` performs no check at all and the behavior is completely undefined, while `s.at(100)` throws a `std::out_of_range` exception. Unless you are one hundred percent certain, `at()` is the safer choice—compared with spending two hours hunting down an out-of-bounds memory bug, that little bit of overhead costs nothing.

The `size()` of a `std::string` returns the number of underlying `char`s, not the number of characters your eyes see. For pure ASCII strings the two agree, but if the string contains Chinese text in UTF-8 encoding, the `s.size()` of `std::string s = "你好";` is 6 rather than 2, because each Chinese character occupies 3 bytes. Handling Unicode strings properly takes a dedicated library (ICU, for example), but this is a pit we absolutely need to know about ahead of time.

## Concatenation, Insertion, Deletion, and Replacement

```cpp
std::string s = "Hello";
s += " World";          // "Hello World"
s.append("!!!");        // "Hello World!!!"
s.push_back('?');       // "Hello World!!!?"
s.insert(5, ",");       // "Hello, World!!!?"
s.erase(5, 1);          // "Hello World!!!?"  removes the comma we just inserted
s.replace(6, 5, "C++"); // "Hello C++!!!?"    World -> C++
s.clear();              // becomes an empty string
```

`+=` and `append()` do similar jobs; day to day we reach for `+=` more often, while `append()` provides additional overloaded versions (appending only a slice of another string, for example). `push_back()` can only append a single character, matching the `push_back()` interface of `vector`. `insert(pos, str)` inserts `str` at `pos`; `erase(pos, len)` deletes `len` characters starting at `pos`; `replace(pos, len, new_str)` swaps the `len` characters starting at `pos` for `new_str`, and the new string's length may differ from the segment being replaced.

The reason these operations are safe is that `std::string` manages memory internally: when an insert runs out of space it grows automatically, and after an erase we never have to shuffle the trailing characters by hand. Compared with computing offsets manually and calling `memmove` with utmost care in C, we can use these operations with far more peace of mind.

## Searching and Substrings

```cpp
std::string s = "Hello, hello, HELLO!";

s.find("hello");                    // 7 (case-sensitive)
s.find("Hello");                    // 0
s.find("xyz");                      // std::string::npos
s.find("hello", 2);                 // 7 (searching from position 2)
s.rfind("hello");                   // 7 (reverse search)
s.find_first_of("aeiou");          // 1 (the first vowel, 'e')
s.find_last_of("aeiou");           // 11 (case-sensitive; the last hit is the 'o' in the second hello)
```

The most crucial concept here is `std::string::npos`. It is a constant whose value is the maximum value of `std::size_t`. When a search operation fails to find the target, it returns `npos`. That is why, after every call to `find`, we must check whether the return value equals `npos` instead of using it as a bool—since `npos` converts to bool as `true`, writing `if (s.find("x"))` actually enters the branch when nothing was found. Another classic beginner trap.

Note that `find_first_of` and `find_last_of` behave rather unusually: they do not search for an entire substring, but for **any single character** of the argument string. `find_first_of("aeiou")` returns 1, because `s[1]` is `'e'`, the first character that matches anything in `"aeiou"`.

| Form | What the function looks for | What it returns on a hit |
| --- | --- | --- |
| `s.find("abc")` | The full `"abc"`, contiguous and in the same order | The starting index of that substring |
| `s.find('a')` | The character `'a'` | The index of the first `'a'` |
| `s.find_first_of("abc")` | Any one of `a`/`b`/`c` | The index of the first character belonging to this set |
| `s.find_first_not_of("abc")` | The first character that is not `a`/`b`/`c` | The index of the first character outside this set |

As we can see, all of these forms search from left to right, starting at index `0` by default, and **return only the first position that satisfies the condition**. They do not modify the original string.

For substring extraction we use `substr(pos, len)`, which takes `len` characters starting at position `pos` and returns a new `std::string`. Omit `len` and it takes everything through the end:

```cpp
std::string t = "Hello, World!";
t.substr(7, 5);  // "World"
t.substr(7);     // "World!"
```

`substr()` returns a brand-new object: it allocates memory and copies the characters. If you only need to iterate over a range rather than own an independent copy, `std::string_view` (C++17) is more efficient—we will come back to it in a later chapter.

## Comparing Strings

In C, comparing two strings means calling `strcmp`; C++'s `std::string` overloads the comparison operators, which is far more intuitive:

```cpp
std::string a = "apple", b = "banana", c = "apple";
a == c;      // true
a != b;      // true
a < b;       // true (lexicographic order)
a.compare(b);  // a negative value (equivalent to strcmp's return-value semantics)
```

The strength of the `compare()` member function is that it supports partial comparison: `s.compare(7, 5, "World")` takes the 5 characters of `s` starting at index 7 and compares them with `"World"` for equality. We will lean on this ability when parsing protocols or processing fixed-format text.

## Interoperating with C Strings

No matter how pleasant `std::string` is to use, plenty of third-party libraries, operating-system APIs, and embedded SDKs still accept `const char*`. Two key functions get us from a `std::string` to a C-style string:

```cpp
std::string s = "Hello, C API!";
const char* p = s.c_str();   // returns a \0-terminated const char*
const char* q = s.data();    // fully equivalent to c_str() since C++17
```

`c_str()` guarantees a `\0`-terminated `const char*` that we can pass directly to `fopen`, `printf`, or any other function expecting a C string. Since C++17, `data()` behaves exactly like `c_str()`.

There is one rule to burn into memory here: the pointers returned by `c_str()` and `data()` are **owned by the string object**—the moment the string is modified or destroyed, the pointers dangle. So we never stash the return value of `c_str()` and then perform operations that might change the string—finish all modifications first, and call `c_str()` as the last step before handing it to the C API.

## Numeric Conversions and Line Input

```cpp
// number -> string
std::to_string(42);      // "42"
std::to_string(3.14);    // "3.140000" (note: formatted with %f)

// string -> number
std::stoi("42");         // int: 42
std::stol("1234567890"); // long: 1234567890
std::stod("3.14159");    // double: 3.14159
std::stoi("  123abc");   // 123 (skips leading whitespace, stops at a non-digit)

// Reading a whole line (cin >> s stops at whitespace; getline reads up to the newline)
std::string line;
std::getline(std::cin, line);
```

The results `std::to_string` produces for floating-point numbers may not look very "pretty": `to_string(3.14)` outputs `3.140000`, because it formats with `%f`. When you need precise control over the output format of floating-point numbers, you still want `std::setprecision` from `<iomanip>` or `std::snprintf`.

## Hands-On: Comprehensive String Processing

Let's combine everything we have learned so far and write a string-processing program with a bit of practical value. It demonstrates several common text-processing patterns: splitting on a delimiter, counting character frequency, find-and-replace, and simple CSV parsing.

```cpp
// string_demo.cpp
#include <iostream>
#include <map>
#include <string>

/// @brief Split a sentence into words by spaces and print each word
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

/// @brief Count how many times each character occurs (case-sensitive)
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

/// @brief Find every occurrence of target in text and replace it with replacement
std::string find_and_replace(std::string text,
                             const std::string& target,
                             const std::string& replacement)
{
    std::cout << "\n--- 查找替换 ---\n  原文: " << text << std::endl;
    std::size_t pos = 0;
    while ((pos = text.find(target, pos)) != std::string::npos) {
        text.replace(pos, target.size(), replacement);
        pos += replacement.size();  // skip past the replacement to avoid an infinite loop
    }
    std::cout << "  结果: " << text << std::endl;
    return text;
}

/// @brief Parse a simple CSV line (no quote escaping handled)
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

Compile and run:

```bash
g++ -std=c++17 -Wall -Wextra -o string_demo string_demo.cpp
./string_demo
```

Output:

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

Let's look at the thinking behind each of these functions. The heart of `split_into_words` is calling `find_first_not_of` repeatedly to skip whitespace, then using `find` to locate the next delimiter, and finally `substr` to cut out the word. This "skip whitespace, find the delimiter, extract, loop" pattern is extremely common in text processing—we suggest you memorize it as a fixed recipe.

`count_char_frequency` uses a `std::map` to tally frequencies. A `std::map` is sorted internally, so the output comes out in lexicographic order by character. This is our first encounter with an associative container; there is no need to understand every detail yet—just know that it is a collection of key-value pairs, and that accessing with `[]` auto-creates a default value when the key does not exist (0 for `int`).

`find_and_replace` showcases an important pattern: when we write a loop doing `find` + `replace`, after each replacement we must move the search start position past the replacement result; otherwise, if `replacement` contains the content of `target`, we get stuck in an infinite loop. The logic of `parse_csv_line` is similar to splitting words, only with the delimiter swapped for a comma.

## Exercises

These three exercises cover the most essential operations of `std::string`. We suggest you write them yourself first, then check your approach against the solutions.

### Exercise 1: Word Counter

Write a function `count_words(const std::string& s)` that counts how many words the string contains (separated by spaces, ignoring consecutive spaces and leading/trailing spaces). Hint: you can use a loop with `find` and `find_first_not_of`, or count the number of "transitions from whitespace to non-whitespace".

::: details Reference Solution

```cpp
#include <iostream>
#include <string>

int count_words(const std::string& str)
{
    std::cout << "--- 拆分单词并统计单词数量 ---" << std::endl;
    std::size_t start = 0;
    std::size_t end = 0;
    std::size_t count = 0;
    while (true) {
        start = str.find_first_not_of(" ,.", end);
        if (start == std::string::npos) {
            break;
        }
        end = str.find_first_of(" ,.", start);
        if (end == std::string::npos) {
            end = str.size();
        }
        count++;
        std::cout << "  [" << str.substr(start, end - start) << "]\n";
    }
    return count;
}

int main()
{
    std::string str = "Hello, this is a sample string for counting words.";
    int word_count = count_words(str);
    std::cout << "单词总数: " << word_count << std::endl;
    return 0;
}
```

Compile and run:

```bash
g++ -std=c++17  -Wall -Wextra main.cpp -o main &&./main
```

Result:

```text
--- 拆分单词并统计单词数量 ---
  [Hello]
  [this]
  [is]
  [a]
  [sample]
  [string]
  [for]
  [counting]
  [words]
单词总数: 9
```

> Note that when splitting words, this sample code ignores not only spaces but also punctuation such as `,` and `.` on our behalf

:::

### Exercise 2: A Simple Find-and-Replace Tool

Write a function `replace_all(std::string& text, const std::string& from, const std::string& to)` that replaces every occurrence of `from` in `text` with `to`. It must handle the case where `from` is an empty string (return the text unchanged; otherwise `find("")` returns 0 and the loop never ends).

::: details Reference Solution

```cpp
#include <iostream>
#include <string>

void replace_all(std::string& str, const std::string& from, const std::string& to)
{
    std::size_t start_pos = 0;
    if (from.empty()) {
        return;
    }
    while ((start_pos = str.find(from, start_pos)) != std::string::npos) {
        str.replace(start_pos, from.length(), to);
        start_pos += to.length();
    }
}

int main()
{
    const std::string original = "Hello, World! World is beautiful.";
    std::string modified = original;
    replace_all(modified, "World", "Universe");
    std::cout << "Original: " << original << std::endl;
    std::cout << "Modified: " << modified << std::endl;
    return 0;
}
```

Compile and run:

```bash
g++ -std=c++17  -Wall -Wextra main.cpp -o main &&./main
```

Result:

```text
Original: Hello, World! World is beautiful.
Modified: Hello, Universe! Universe is beautiful.
```

:::

### Exercise 3: A trim Function

Write two functions, `ltrim` and `rtrim`, that strip whitespace characters (spaces, `\t`, `\n`) from the beginning and the end of a string respectively, and then combine them into a `trim` function. Hint: `ltrim` uses `find_first_not_of(" \t\n")` to find the first non-whitespace character and then `substr`; `rtrim` is similar, using `find_last_not_of`.

::: details Reference Solution

```cpp
#include <iostream>
#include <string>

void ltrim(std::string& s)
{
    std::size_t pos = s.find_first_not_of(" ");
    if (pos != std::string::npos) {
        s = s.substr(pos);
    }
}

void rtrim(std::string& s)
{
    std::size_t pos = s.find_last_not_of(" ");
    if (pos != std::string::npos) {
        s = s.substr(0, pos + 1);
    }
}

void trim(std::string& s)
{
    ltrim(s);
    rtrim(s);
}

int main()
{
    std::string str = "   Hello, World!   ";
    std::cout << "原始的: '" << str << "'" << std::endl;
    trim(str);
    std::cout << "修剪后的: '" << str << "'" << std::endl;
    return 0;
}
```

Compile and run:

```bash
g++ -std=c++17  -Wall -Wextra main.cpp -o main &&./main
```

Result:

```text
原始的: '   Hello, World!   '
修剪后的: 'Hello, World!'
```

:::
