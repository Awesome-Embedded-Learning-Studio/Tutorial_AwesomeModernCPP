---
chapter: 5
cpp_standard:
- 11
- 14
- 17
- 20
description: Master `std::string` construction, concatenation, lookup, and substring
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
  source: documents/vol1-fundamentals/ch05/03-std-string.md
  source_hash: 59a3d0fd0f508d5c74aefe6e6c0ed300bc7374a995cd006e6e881c67ebae0c55
  translated_at: '2026-06-16T03:44:10.156091+00:00'
  engine: anthropic
  token_count: 2721
---
# std::string

In the previous tutorial, we spent a lot of effort wrestling with C-style strings—manually managing null terminators, carefully guarding against buffer overflows, and operating on every character array with `strcpy` and `strcat` as if walking on thin ice. If you are as fed up with this as I am, here is some news that will make you breathe a sigh of relief: the C++ Standard Library provides a real string type called `std::string`. It manages memory automatically, handles length automatically, supports intuitive concatenation and comparison, and basically fills all the pits we fell into with C strings.

In this chapter, we start with the construction methods of `std::string`, move through concatenation, searching, substring extraction, and interoperability with C strings, and finally tie all the knowledge together with a comprehensive string processing program. After finishing this, you will find that those blood-pressure-raising string operations (I've been there—after learning `std::string`, I sometimes couldn't figure out how to use C strings properly) can be written safely and elegantly in C++.

> **Learning Objectives**
>
> After completing this chapter, you will be able to:
>
> - [ ] Construct `std::string` objects in various ways
> - [ ] Perform string concatenation, insertion, deletion, and replacement
> - [ ] Master search and substring operations like `find` and `substr`
> - [ ] Correctly convert between C++ strings and C-style strings
> - [ ] Use conversion functions like `std::to_string` and `std::stoi`

## Environment Setup

We will conduct all subsequent experiments in the following environment:

- Platform: Linux x86_64 (WSL2 is acceptable)
- Compiler: GCC 13+ or Clang 17+
- Compiler flags: `-std=c++17 -Wall -Wextra`

## Step 1 — Constructing a String in Various Ways

`std::string` provides a rich set of constructors covering almost every scenario you can imagine:

```cpp
#include <iostream>
#include <string>

int main() {
    // 1. Default construction (empty string)
    std::string s1;

    // 2. Construct from a C-string literal
    std::string s2 = "Hello";

    // 3. Construct from a count and a single character
    std::string s3(5, 'A'); // "AAAAA"

    // 4. Copy construction
    std::string s4(s2);

    // 5. Construct from a substring (pos, count)
    std::string s5("World", 1, 3); // "orl"

    // 6. Move construction (C++11)
    std::string s6(std::move(s4));

    std::cout << "s1: [" << s1 << "]\n";
    std::cout << "s2: [" << s2 << "]\n";
    std::cout << "s3: [" << s3 << "]\n";
    std::cout << "s4: [" << s4 << "]\n"; // s4 is now empty (moved-from)
    std::cout << "s5: [" << s5 << "]\n";
    std::cout << "s6: [" << s6 << "]\n";
}
```

Output:

```text
s1: []
s2: [Hello]
s3: [AAAAA]
s4: []
s5: [orl]
s6: [Hello]
```

The first and fifth methods look like assignments, but the compiler is actually performing construction—this is C++ copy-initialization syntax, which has the same effect as `std::string s2("Hello")`. `s5` extracts 3 characters starting from index 1 of `"World"`, resulting in `"orl"`. This "partial construction" is very useful when parsing strings. We don't need to dive deep into move construction right now; just know that it is faster than copying because it "steals" the internal resources rather than duplicating them.

> ⚠️ **Warning**
> The source object after a move (`s4` above) is in a "valid but unspecified" state—you can assign to it or destroy it, but do not read its value for any meaningful logic. This is the basic contract of C++ move semantics, which we will cover in detail when we discuss move semantics in a later chapter.

## Step 2 — Basic Operations: Size, Access, and Empty Checks

```cpp
#include <iostream>
#include <string>

int main() {
    std::string s = "Hello";

    // Size
    std::cout << "Length: " << s.length() << "\n"; // 5
    std::cout << "Size: " << s.size() << "\n";     // 5

    // Access
    char c1 = s[1]; // 'e'
    char c2 = s.at(2); // 'l'

    // Empty check
    if (s.empty()) {
        std::cout << "String is empty\n";
    } else {
        std::cout << "String is not empty\n";
    }
}
```

`s.length()` and `s.size()` are completely equivalent. Most C++ developers prefer `size()` because it is consistent with other standard library containers.

Both `operator[]` and `at()` can access characters via index, but they differ in out-of-bounds behavior: `operator[]` performs no checking and results in undefined behavior on violation; `at()` throws an `std::out_of_range` exception. If you aren't 100% sure about the boundaries, using `at()` is safer—this minor performance cost is nothing compared to spending two hours hunting down a memory corruption bug.

> ⚠️ **Warning**
> `std::string`'s `size()` returns the count of underlying `char` bytes, not the "number of visible characters" (glyphs). For pure ASCII strings they are the same, but if the string contains UTF-8 encoded Chinese characters, `size()` for "你好" is 6, not 2, because each Chinese character occupies 3 bytes. Correctly handling Unicode strings requires specialized libraries (like ICU), but you must be aware of this pitfall early on.

## Step 3 — Concatenation, Insertion, Deletion, and Replacement

```cpp
#include <iostream>
#include <string>

int main() {
    std::string s = "Hello";

    // Concatenation
    s += " World"; // "Hello World"
    s.push_back('!'); // "Hello World!"

    // Insertion
    s.insert(5, ","); // "Hello, World!"

    // Deletion
    s.erase(5, 1); // "Hello World!" (removes the comma)

    // Replacement
    s.replace(6, 5, "C++"); // "Hello C++!" (replaces "World" with "C++")

    std::cout << s << "\n";
}
```

`operator+=` and `append` have similar functions; `operator+=` is more concise, while `append` provides more overloaded versions (such as appending only a specific segment of another string). `push_back` can only append a single character, consistent with the `push_back` interface of other containers like `std::vector`. `insert` inserts content at a specific position, `erase` removes a specified number of characters starting from a position, and `replace` substitutes a specified range with new content. The new string's length can differ from the replaced section.

These operations are safe because `std::string` manages memory automatically—space is expanded automatically when insertion runs out of room, and manual character shifting isn't required during deletion. Compared to the old days of manually calculating offsets and cautiously calling `memmove` in C, this is paradise.

## Step 4 — Searching and Substrings

```cpp
#include <iostream>
#include <string>

int main() {
    std::string s = "Hello World";

    // Find substring
    size_t pos = s.find("World");
    if (pos != std::string::npos) {
        std::cout << "Found at: " << pos << "\n"; // 6
    }

    // Find character (find_first_of)
    size_t vowels = s.find_first_of("aeiou");
    if (vowels != std::string::npos) {
        std::cout << "First vowel at: " << vowels << "\n"; // 1 ('e')
    }

    // Substring
    std::string sub = s.substr(0, 5); // "Hello"
    std::cout << "Substring: " << sub << "\n";
}
```

The most critical concept here is `std::string::npos`. It is a constant with the value `std::numeric_limits<size_t>::max()`. When a search operation fails to find the target, it returns `npos`. Therefore, after every call to `find`, you must check if the return value equals `npos`, rather than using it directly as a boolean—because `npos` converts to `true` as a boolean. Writing `if (s.find(...))` enters the branch when not found, which is another classic trap for beginners.

`find_first_of` and `find_last_of` behave somewhat specially: they don't look for an entire substring, but look for **any one character** from the parameter string. `find_first_of("aeiou")` returns 1, because `'e'` is the first character in `"Hello World"` that matches any character in `"aeiou"`.

Substring extraction uses `substr`, which returns a new `std::string` containing a specified number of characters starting from a position. Omitting the count extracts to the end:

```cpp
std::string s = "Hello";
std::string sub = s.substr(1); // "ello"
```

`substr` returns a new object, allocating memory and copying characters. If you only need to iterate over a range without an independent copy, using `std::string_view` (C++17) is more efficient—we will expand on this in later chapters.

## Step 5 — Comparing Strings

In C, comparing two strings requires `strcmp`. C++'s `std::string` overloads comparison operators, which is much more intuitive:

```cpp
#include <iostream>
#include <string>

int main() {
    std::string s1 = "Apple";
    std::string s2 = "Banana";

    if (s1 == s2) {
        std::cout << "Equal\n";
    } else if (s1 < s2) {
        std::cout << "s1 < s2\n"; // Output: s1 < s2
    }

    // Member function compare
    int result = s1.compare(s2); // < 0
    if (result == 0) std::cout << "Same";
    else if (result < 0) std::cout << "s1 smaller";
    else std::cout << "s1 larger";
}
```

The advantage of the `compare` member function is that it supports partial comparison, for example `s1.compare(0, 3, "App")` compares the 3 characters starting at index 0 of `s1` with `"App"`. This capability is useful when parsing protocols or handling fixed-format text.

## Step 6 — Interoperability with C Strings

No matter how good `std::string` is, many third-party libraries, OS APIs, and embedded SDKs still accept `const char*`. Getting a C-style string from `std::string` requires two key functions:

```cpp
#include <iostream>
#include <string>
#include <cstring>

int main() {
    std::string s = "Hello";

    // c_str
    const char* cstr = s.c_str();
    std::cout << std::strlen(cstr) << "\n";

    // data (C++17 and later)
    const char* data = s.data();
    std::cout << data << "\n";
}
```

`c_str()` guarantees returning a `const char*` terminated by a null character (`\0`), which can be passed directly to `printf`, `fopen`, or any function expecting a C string. `data()` behaves identically to `c_str()` starting from C++17.

Here is a rule you must remember: the pointers returned by `c_str()` and `data()` are **owned by the string object**. Once the string is modified or destroyed, the pointers become invalid. Therefore, never store the return value of `c_str()` and then perform operations that might change the string—complete all modifications first, then call `c_str()` to pass to the C API.

## Step 7 — Numeric Conversion and Line Input

```cpp
#include <iostream>
#include <string>

int main() {
    // Number to String
    std::string s1 = std::to_string(123);
    std::string s2 = std::to_string(3.14);

    // String to Number
    int i = std::stoi("42");
    double d = std::stod("3.14");

    std::cout << s1 << ", " << s2 << "\n";
    std::cout << i << ", " << d << "\n";
}
```

`std::to_string` results for floating-point numbers might not be "pretty"—`std::to_string(3.14)` outputs `3.140000`, because it uses `%f` formatting. If you need precise control over floating-point output format, you still need to use `std::format` (C++20) or `std::stringstream` from the `<iomanip>` library.

## Practical Exercise — Comprehensive String Processing

Now let's synthesize all the knowledge we've learned and write a slightly practical string processing program. This program demonstrates several common text processing patterns: splitting by a delimiter, counting character frequency, finding and replacing, and simple CSV parsing.

```cpp
#include <iostream>
#include <string>
#include <vector>
#include <map>

// Split string by delimiter
std::vector<std::string> split(const std::string& s, char delimiter) {
    std::vector<std::string> tokens;
    size_t start = 0;
    size_t end = s.find(delimiter);

    while (end != std::string::npos) {
        tokens.push_back(s.substr(start, end - start));
        start = end + 1;
        end = s.find(delimiter, start);
    }

    tokens.push_back(s.substr(start)); // Last part
    return tokens;
}

// Count character frequency
std::map<char, int> count_chars(const std::string& s) {
    std::map<char, int> counts;
    for (char c : s) {
        counts[c]++;
    }
    return counts;
}

// Find and replace all
std::string replace_all(std::string s, const std::string& from, const std::string& to) {
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.length(), to);
        pos += to.length();
    }
    return s;
}

int main() {
    // 1. Split
    std::string text = "one,two,three";
    auto parts = split(text, ',');
    std::cout << "Split result:\n";
    for (const auto& p : parts) {
        std::cout << " - " << p << "\n";
    }

    // 2. Count
    std::string sample = "hello";
    auto counts = count_chars(sample);
    std::cout << "\nChar counts:\n";
    for (const auto& [c, n] : counts) {
        std::cout << " '" << c << "': " << n << "\n";
    }

    // 3. Replace
    std::string data = "color: red, color: green";
    std::string fixed = replace_all(data, "color", "colour");
    std::cout << "\nReplace result: " << fixed << "\n";
}
```

Compile and run:

```bash
g++ -std=c++17 -Wall -Wextra main.cpp -o string_demo
./string_demo
```

Output:

```text
Split result:
 - one
 - two
 - three

Char counts:
 'e': 1
 'h': 1
 'l': 2
 'o': 1

Replace result: colour: red, colour: green
```

Let's look at the logic of these functions one by one. The core of `split` is repeatedly calling `find` to skip delimiters, then using `substr` to extract the segment. The pattern of "skip whitespace, find delimiter, extract, loop" is very common in text processing and is worth remembering as a standard idiom.

`count_chars` uses `std::map` to count frequency. `std::map` is internally sorted, so the output is arranged in lexicographical order by character. Here we use an associative container for the first time; you don't need to understand all the details, just know it's a collection of "key-value" pairs, and `operator[]` access creates a default value (0 for integers) if the key doesn't exist.

`replace_all` demonstrates an important pattern: when doing `find` + `replace` in a loop, move the search start position to after the replacement result each time; otherwise, if the `to` string contains the `from` content, it will create an infinite loop. The logic for CSV parsing is similar to splitting words, just with a comma as the delimiter.

## Exercises

These three exercises cover the most core operations of `std::string`. I recommend writing them yourself before checking the logic.

### Exercise 1: Word Counter

Write a function `int count_words(const std::string& s)` that counts how many words are in a string (separated by spaces, ignoring consecutive spaces and leading/trailing spaces). Hint: You can use a loop with `find` and `substr`, or count "transitions from whitespace to non-whitespace".

### Exercise 2: Simple Find and Replace Tool

Write a function `std::string replace(std::string s, const std::string& from, const std::string& to)` that replaces all occurrences of `from` in `s` with `to`. Requirement: Handle the case where `from` is an empty string (return the original text, otherwise `find` will return 0 causing an infinite loop).

### Exercise 3: trim Function

Write two functions, `ltrim` and `rtrim`, to remove whitespace characters (spaces, `\t`, `\n`) from the beginning and end of a string respectively, then combine them into a `trim` function. Hint: `ltrim` uses `find_first_not_of` to locate the first non-whitespace character and then `substr`; `rtrim` is similar, using `find_last_not_of`.

## Summary

In this chapter, starting from the various pain points of C-style strings, we learned about `std::string`, the string type provided by the C++ Standard Library. Let's review the core points:

- `std::string` manages memory automatically, eliminating the need for manual allocation and deallocation, fundamentally preventing buffer overflows.
- Diverse construction methods: literals, repeated characters, copying, partial extraction, `operator+` concatenation, covering common use cases.
- The `find` series of functions and `substr` are core tools for text processing, with `npos` serving as the sentinel value for "not found".
- `c_str()` and `data()` provide a bridge for interoperability with C APIs, but pay attention to pointer lifetimes.
- `std::to_string` and `std::stoi`/`std::stod` solve conversion needs between strings and numbers.

This concludes Chapter 5, "Arrays and Strings". We started from the most basic C arrays, passed through the low-level perspective of pointer arithmetic, and finally arrived at the high-level abstraction of `std::string`. This path itself reflects C++'s design philosophy: **low-level capabilities are not reduced, but the standard library provides safe and easy-to-use tools at the upper layer**. Next, in Chapter 6, we will enter the world of C++ Object-Oriented Programming—classes and objects. That is the true stage of C++.

---

> **Self-Assessment**: If you are still unsure about the check mechanism for `find` returning `npos`, I suggest going back and retyping the code in the "Searching and Substrings" section, paying special attention to the update logic of `pos` in loops. String operations are the foundation for all future projects, so spending extra time here is absolutely worth it.
