---
chapter: 7
cpp_standard:
- 17
- 20
description: 'Thoroughly explains `std::string_view`: a read-only character view consisting
  of a pointer and length, `sizeof` size comparison, zero-copy argument passing that
  avoids heap allocation from constructing temporary `std::string` from `char*`, the
  major pitfall of dangling references (copying only occurs upon materialization via
  `remove_prefix`/`substr`), and C++23''s `contains` and trivial copyability.'
difficulty: intermediate
order: 50
platform: host
prerequisites:
- string 深入：SSO、COW 与 resize_and_overwrite
- span：非拥有的连续视图
reading_time_minutes: 12
related:
- span：非拥有的连续视图
- string 深入：SSO、COW 与 resize_and_overwrite
tags:
- host
- cpp-modern
- intermediate
- 类型安全
title: 'string_view: A Non-Ownng Read-Only String View'
translation:
  source: documents/vol3-standard-library/strings/50-string-view.md
  source_hash: d5ceddd95c866b70af66355dadcc5d9f6cc6164cd3711a518978b1d5ce6ed797
  translated_at: '2026-06-24T02:27:43.844104+00:00'
  engine: anthropic
  token_count: 3161
---
# string_view: A Non-owning Read-only String View

In the previous post on `span`, we introduced the concept of a "non-owning view": an object that holds only a pointer and a length, performs no allocation, takes no responsibility for freeing memory, and is virtually free to copy. In this post, we will look at its character-oriented cousin—`std::string_view`.

They are similar in spirit, but their specific roles differ: `span<T>` works with any element type and can be read-write; `string_view` is specialized for character sequences, is **read-only**, and carries string semantics. Introduced in C++17, it almost overnight revolutionized the old ways of passing read-only strings. We will start with its most basic internal representation, covering both "why it is designed this way" and "how to use it correctly."

## Internal Representation: Just a (Pointer, Length) Pair

Inside, `string_view` consists of just two things: a `const CharT*` pointing to the first character, and a `size_t` recording the character count. No allocation, no ownership, no copying of the underlying data—this is exactly like `span`, with the only difference being that the element type is locked to characters and is always `const`.

Therefore, its size is deterministic: on a 64-bit platform, it is two 8-byte words, totaling 16 bytes. Let's run a quick test and compare it with `std::string`:

```cpp
// Standard: C++20
#include <string>
#include <string_view>
#include <iostream>

int main()
{
    std::string s = "hello";
    std::string_view sv = s;
    std::cout << "sizeof(std::string)      = " << sizeof(std::string) << '\n';
    std::cout << "sizeof(std::string_view) = " << sizeof(std::string_view) << '\n';
    std::cout << "sizeof(void*)            = " << sizeof(void*) << '\n';
    std::cout << "sizeof(size_t)           = " << sizeof(size_t) << '\n';
    return 0;
}
```

`g++ -std=c++20 -O2` (native GCC 16.1.1, x86_64) produces:

```text
sizeof(std::string)      = 32
sizeof(std::string_view) = 16
sizeof(void*)            = 8
sizeof(size_t)           = 8
```

`string` is 32 bytes, while `string_view` is half that size—16 bytes, which is exactly a pointer plus a size. We cover what exactly fills those 32 bytes in `string` (SSO buffer, capacity, length, heap pointer) in depth in [04-string-memory-deep-dive](../containers/04-string-memory-deep-dive.md), but for now, just remember the conclusion: `string` itself is a "stateful, allocating, SSO-enabled" heavyweight object, whereas `string_view` is a "two-word, non-allocating" lightweight view. Copying a `string_view` means copying those two words, which is practically free.

::: warning It is read-only by nature
`string_view` stores a `const CharT*` internally; there is no non-const version. Want to modify characters? No way—it's just a window, you can look but you can't touch. For writable access, use `span<char>`.
:::

## Zero-copy argument passing: The primary reason for its existence

The most valuable trick `string_view` offers is replacing `const std::string&` for passing read-only strings. This might sound like "just swapping one thing for another similar thing," but the actual difference is massive—especially when the caller has a `char*` or a string literal.

Let's look at a minimal comparison. Two functions do the same thing (count vowels), but one signature uses `const string&` and the other uses `string_view`:

```cpp
// Standard: C++20
long count_vowels_ref(const std::string& s) { /* 逐字符数 a/e/i/o/u */ }
long count_vowels_sv(std::string_view sv)   { /* 同上 */ }
```

When the caller holds a `char*` that is long enough (exceeding the SSO threshold, so it won't fit into the small object buffer of `std::string`), what happens with the `const string&` approach? **The compiler must first construct a temporary `std::string` from that `char*`**—which implies a heap allocation and a copy—before passing a reference to that temporary object into the function. When the function returns, the temporary is destroyed and the heap is freed. Yet, our original intention was simply to "scan it read-only."

The `string_view` approach is much cleaner: we simply wrap the `char*` and the length into a 16-byte view and pass it in, with no allocation and no copying.

Talk is cheap. Let's expose the truth by overriding the global `operator new` to count the number of heap allocations:

```cpp
// Standard: C++20
#include <string>
#include <string_view>
#include <iostream>
#include <new>

static int g_alloc_count = 0;
void* operator new(std::size_t n)
{
    ++g_alloc_count;
    std::cout << "  [alloc " << n << " bytes]\n";
    return std::malloc(n);
}
void operator delete(void* p) noexcept { std::free(p); }

void take_ref(const std::string& s) { (void)s; }
void take_sv(std::string_view sv)   { (void)sv; }

int main()
{
    const char* long_s = "01234567890123456789034567890123456789";  // 超过 SSO

    std::cout << "--- const string& x3 (long char*) ---\n";
    take_ref(long_s); take_ref(long_s); take_ref(long_s);

    std::cout << "--- string_view x3 (long char*) ---\n";
    take_sv(long_s); take_sv(long_s); take_sv(long_s);
    return 0;
}
```

Output:

```text
--- const string& x3 (long char*) ---
  [alloc 39 bytes]
  [alloc 39 bytes]
  [alloc 39 bytes]
--- string_view x3 (long char*) ---
```

The evidence is straightforward: the `const string&` approach **allocates on every call** (three calls = three `alloc`s, 39 = 38 characters + null terminator), while the `string_view` approach **allocates zero times**. This demonstrates the value of zero-copy argument passing—it eliminates the construction and destruction of temporary `string` objects, not just the indirection of the reference.

Let's scale this up in a tight loop. Using the same long payload (90 bytes, well beyond the SSO limit), for fifty million calls:

```cpp
// Standard: C++20（节选,完整版见下方 benchmark 说明）
static const char* kPayload =
    "The quick brown fox jumps over the lazy dog - a non-trivial string payload.";

long count_vowels_ref(const std::string& s) { /* ... */ }
long count_vowels_sv(std::string_view sv)   { /* ... */ }

int main()
{
    constexpr int kIters = 50'000'000;
    volatile long sink = 0;
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < kIters; ++i) sink += count_vowels_ref(kPayload);
    auto t1 = std::chrono::steady_clock::now();
    for (int i = 0; i < kIters; ++i) sink += count_vowels_sv(kPayload);
    auto t2 = std::chrono::steady_clock::now();
    /* 打印两段耗时 */
}
```

Running `g++ -std=c++20 -O2` twice on the local machine yields the following results:

```text
const string& path (char* arg): 1820 ms
string_view  path (char* arg): 1360 ms
ratio (ref/sv): 1.34x
```

Want to run it yourself to see the ratios? Open the online example below (it takes about 2 seconds to run online and prints two durations and the ratio):

<OnlineCompilerDemo
  title="Zero-copy argument passing: const string& vs string_view"
  source-path="code/examples/vol3/50_string_view_benchmark.cpp"
  description="90-byte payload, 50 million calls: const string& constructs a temporary string each time, string_view has zero allocations—measured ~35% faster for string_view"
  allow-run
/>

`const string&` is about 34% slower than `string_view`. While absolute microsecond values fluctuate by machine, the gap caused by "eliminating temporary string allocation/deallocation" is robust—the longer the payload (more likely to exceed SSO) and the more frequent the calls, the more obvious the difference. Note that if the caller already holds a `std::string`, `const string&` binds directly without constructing a temporary, so the two are on par. The argument-passing advantage of `string_view` is **specifically** realized in scenarios that are "read-only, heterogeneous in source, and sourced from `char*`, literals, or substrings."

This is precisely why modern APIs increasingly prefer `string_view` for accepting read-only strings: it can accept `std::string`, `char*`, literals, or another `string_view` without requiring changes from the caller. This mirrors what `span` did for unifying "a span of T" arguments, but for characters, `string_view` had already settled this long ago.

## remove_prefix / remove_suffix / substr: Viewport operations, O(1)

Since it is a view, "adjusting which part is being viewed" should be cheap. `string_view` comes with a trio of tools, all **O(1)**, all adjusting the viewport, and none copying the underlying data:

- `remove_prefix(n)` — Moves the start forward by n, effectively discarding the first n characters;
- `remove_suffix(n)` — Moves the end backward by n, effectively discarding the last n characters;
- `substr(pos, count)` — Returns a new `string_view` pointing to `[pos, pos+count)`, still without copying.

This toolkit is particularly handy for parsing scenarios. For example, parsing a URL into scheme / host / path segments can be done entirely with zero-copy:

```cpp
// Standard: C++20
#include <string>
#include <string_view>
#include <iostream>

int main()
{
    std::string url = "https://example.com/path/to/file";
    std::string_view sv{url};

    std::string_view scheme = sv;
    scheme.remove_prefix(8);              // 跳过 "https://"
    std::cout << "after remove_prefix(8): " << scheme << '\n';

    std::string_view host = scheme;
    auto slash = host.find('/');
    if (slash != std::string_view::npos) {
        host.remove_suffix(host.size() - slash);   // 截到第一个 '/'
    }
    std::cout << "host: " << host << '\n';

    std::string_view path = sv.substr(8 + host.size());   // "/path/to/file"
    std::cout << "path: " << path << '\n';
    return 0;
}
```

Here is the output:

```text
```

```text
after remove_prefix(8): example.com/path/to/file
host: example.com
path: /path/to/file
```

All three segments of the view point to the original memory block of `url`, without moving a single byte. This is exactly how a "view" should behave—it is cast from the same mold as `span`'s `subspan`, `first`, and `last`.

## Materialization triggers copying: constructing a `string` from a view is not free

As you use this, you might wonder: `string_view` is so efficient, so when does copying actually happen? The answer is **when you materialize it into a `std::string`**.

```cpp
std::string_view path = /* 某段视图 */;
std::string owned = std::string{path};   // 这里发生拷贝:分配 + 逐字符复制
```

Constructing a `std::string` from a `string_view` is a full copy—the standard library must allocate a new block of memory and copy the characters from the view one by one. This isn't a bug; it's inevitable: `string` is an owner, so to possess its own copy, it must actually take the data.

What does this mean in practice? A common misuse is "using `string_view` everywhere to 'unify the interface,' then converting back with `std::string{sv}` inside a function to store in a container or member variable." Doing this negates the zero-copy benefit and adds an extra layer of indirection. **The correct way to use `string_view` is: pass it along as read-only until the moment ownership is truly needed, and only materialize it once.** If you find a value is being repeatedly materialized within a function, it should have been a `std::string` to begin with, not a `string_view`.

## The Biggest Pitfall: It Doesn't Own, It Dangles

The most fatal pitfall of `string_view` is the inevitable cost of its "non-owning" nature—**it doesn't manage the lifetime of the underlying data**. As long as the underlying data lives, the view is useful; once the underlying data is gone, the view becomes a dangling pointer pointing to freed memory, and accessing it is undefined behavior.

The classic mistake is returning a view of a `string` local to a function. The function ends, the `string` is destroyed, and the view dangles:

```cpp
std::string_view bad_func() {
    std::string local = "temporary data";
    return local; // Oops! Returns a view of a dead string.
}
```

```cpp
// Standard: C++20
#include <string>
#include <string_view>
#include <iostream>

std::string_view bad_return()
{
    std::string local = "hello world";
    return std::string_view{local};   // local 在此销毁,返回的 view 立刻悬垂
}

int main()
{
    auto sv = bad_return();
    std::cout << "sv (dangling, UB): " << sv << '\n';
    std::cout << "sv.size(): " << sv.size() << '\n';
    return 0;
}
```

`g++ -std=c++20 -O2` produces this output (Note: this is undefined behavior (UB), your output might differ, or it might even look "normal"—which is exactly what makes it so dangerous):

```text
sv (dangling, UB): �h�2
sv.size(): 11
```

`size()` is still 11, because the length was copied into the object when the `string_view` was constructed, so the destruction of `local` does not affect it. However, the underlying memory holding those 11 characters has already been returned to the heap, so `operator<<` reads garbage.

A more subtle pitfall is **binding to temporaries**. `s + "x"` produces a temporary `string`. If we bind a view to it, that temporary is destroyed as soon as the statement ends:

```cpp
// Standard: C++20
std::string s = "abc";
std::string_view sv = s + "x";   // 临时 string 销毁,sv 悬垂
std::cout << sv << '\n';         // UB
```

Writing it this way is dangerous. Since the temporary's memory on the stack hasn't been overwritten yet, it might even print "abcx"—it looks fine, but it's actually a ticking time bomb. If we allocate a few more times to overwrite that buffer, the flaw is revealed:

```cpp
// Standard: C++20
int main()
{
    std::string s = "abc";
    std::string_view sv = s + "x";          // 悬垂
    for (int i = 0; i < 3; ++i) {
        std::string noise(64, char('A' + i));
        std::cout << "noise: " << noise << '\n';
    }
    std::cout << "sv: [" << sv << "] size=" << sv.size() << '\n';
    return 0;
}
```

Here is the output when running `g++ -std=c++20 -O0`:

```text
noise: AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
noise: BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB
noise: CCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCC
sv: [@   ] size=4
```

`size` still reports 4, but the contents of `sv` have become `@` followed by a bunch of whitespace—that memory was overwritten by the allocations in the `noise` loop.

::: warning Don't rely on "it looks fine" to fool yourself
The `sv = s + "x"` above often prints the "normal" `abcx` under `-O2`, because the temporary's stack slot hasn't been reused yet. **This is UB, not "it works".** Switch to `g++ -std=c++20 -O1 -fsanitize=address` and run the same snippet; ASan will immediately catch it:
:::

```text
==535629==ERROR: AddressSanitizer: stack-use-after-scope on address 0x...
READ of size 4 at 0x... thread T0
    #2 in std::operator<< <char, ...>(..., std::basic_string_view<char, ...>)
    #3 in main /tmp/sv_concat2.cpp:14
```

`stack-use-after-scope` — attempting to read data from a temporary after it has gone out of scope. Tools like ASan are designed to expose "looks fine" UB (undefined behavior). If you suspect a lifetime issue involving `string_view`, running with `-fsanitize=address` is far more reliable than inspecting the code by eye.

The root cause of these pitfalls all comes down to one iron rule: **the lifetime of a `string_view` must not exceed the data it points to**. As long as you don't bind it to a temporary, don't store it longer than the underlying data, and don't return a view of a local `string` from a function, it is safe.

## C++20 / C++23: A Few Minor Interface Additions

Since `string_view` arrived in C++17, subsequent standards have bolted on a few small but practical interfaces. Let's verify them one by one using GCC 16.1.1.

C++20 introduced `starts_with` and `ends_with`, whose semantics are self-explanatory:

```cpp
// Standard: C++20
std::string_view sv = "hello world";
sv.starts_with("hello");   // true
sv.ends_with("world");     // true
```

C++23 introduces `contains`, replacing the verbose `find(x) != npos` pattern with a single line:

```cpp
// Standard: C++23
sv.contains("lo wo");   // true
sv.contains("xyz");     // false
```

Here is the output:

```text
...
```

```text
starts_with("hello"): true
ends_with("world"):   true
contains("lo wo"):    true
contains("xyz"):      false
```

C++23 also elevated "trivially copyable" from "something all implementations have been doing anyway" to a hard standard requirement. Let's verify this:

```cpp
// Standard: C++23
std::cout << std::is_trivially_copyable_v<std::string_view>;   // 1
std::cout << __cpp_lib_string_contains;                        // 202011
```

```text
is_trivially_copyable_v<string_view> = true
__cpp_lib_string_contains = 202011
```

The practical significance of being "trivially copyable" is this: `string_view` can be safely passed across binary boundaries, moved via `memcpy`, and placed into shared memory. The compiler can optimize its copies aggressively. This is the underlying qualification that makes it suitable as the "common currency for read-only passing."

::: warning Regarding "Dangling Views in Range-For Loops"
Some online resources claim that "range-based for loops iterating over a temporary view" were fixed in C++23—this is inaccurate. `string_view` itself does not own data. Iterating over a **view bound to a temporary string** still results in the data disappearing when the temporary is destroyed. The Standard does not, and cannot, "save" it at the language level. What truly helps is **toolchain diagnostics**: enable static/runtime checks like `-fsanitize=address` or `-Wdangling` (GCC/Clang). C++23 added things like `contains` and the trivially copyable requirement to `string_view` at the **interface and type** level, not lifecycle management. That red line for lifetimes must be guarded by you, from start to finish.
:::

Incidentally, C++23 also added the ability to construct `string_view` from any contiguous range (P1989). This means `std::vector<char>` can be passed directly to a function accepting `string_view`, eliminating the need to manually hand-roll `.data()` + `.size()`. Still on the way for C++26 is `subview` (returns a sub-view, similar to `substr` but more aligned with ranges style); GCC 16.1.1 hasn't landed it yet, so we'll wait for the official release.

## Summary

Breaking down `string_view` to this level reveals its full picture—a **read-only character view consisting of a pointer and a length**, valued for passing arguments, and dangerous for dangling references. Let's wrap up with a few key conclusions:

- **Internal Representation**: A pair of `const CharT*` + `size_t`. On 64-bit systems, it is 16 bytes—half the size of `std::string` (32 bytes). It allocates nothing, owns nothing, and copying it is just copying two words.
- **Zero-Copy Passing**: Replaces `const string&` for receiving read-only strings. The biggest win is when the caller holds a `char*` or a literal—saving a heap allocation for a temporary `string`. If the caller already holds a `string`, the two are roughly equal.
- **View Operations**: `remove_prefix` / `remove_suffix` / `substr` are all O(1) and involve no copying; the underlying data remains as long as it was, only the viewing window changes.
- **Materialization Copies**: Constructing a `std::string` from a `string_view` is a full copy. The correct usage is to pass it read-only all the way down, only materializing it into a `string` when ownership is actually needed.
- **The Biggest Pitfall is Dangling**: Returning a view of a local `string`, binding to a temporary (e.g., `s + "x"`), or storing it longer than the underlying data are all UB (undefined behavior). "Looks fine" doesn't mean it is; enabling `-fsanitize=address` is the most stable way to verify.
- **C++20/23**: `starts_with` / `ends_with` (C++20), `contains` (C++23), and the trivially copyable requirement (C++23) are all ready. The lifecycle red line wasn't "fixed"; it relies on you guarding it and toolchain diagnostics.

To distinguish it from its sister `span` in one sentence: use `span<T>` for arbitrary, potentially writable data types; use `string_view` for read-only character sequences. One faces bytes, the other faces characters; they share the same mechanism but have clear division of labor.

## References

- [cppreference: std::basic_string_view](https://en.cppreference.com/w/cpp/string/basic_string_view) — Overview of members, constructors, `remove_prefix`/`substr`/`contains`, and version annotations.
- [cppreference: std::basic_string_view::contains (C++23)](https://en.cppreference.com/w/cpp/string/basic_string_view/contains) — `contains` and the `__cpp_lib_string_contains` feature macro.
- [P0123 `string_view` Proposal Family](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2017/n4618.pdf) — Design motivation prior to C++17 landing.
- [P1989R2: Range constructor for `string_view`](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2020/p1989r2.html) — C++23 construction of `string_view` from a contiguous range.
- This volume's [span: Non-owning Contiguous View](../containers/08-span.md) — The "non-owning view" sister article sharing the same mechanism; one for bytes, one for characters.
