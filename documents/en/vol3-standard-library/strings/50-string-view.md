---
chapter: 7
cpp_standard:
- 17
- 20
description: 'A thorough guide to std::string_view: a read-only view of pointer plus
  length, sizeof comparisons, zero-copy parameter passing that avoids the heap allocation
  of building a temporary string from char*, dangling views as the biggest pitfall
  (remove_prefix/substr, copies only on materialization), plus C++23 contains and
  the trivially copyable guarantee.'
difficulty: intermediate
order: 50
platform: host
prerequisites:
- 'Deep Dive into string: SSO, COW, and resize_and_overwrite'
- 'span: A Non-owning Contiguous View'
reading_time_minutes: 12
related:
- 'span: A Non-owning Contiguous View'
- 'Deep Dive into string: SSO, COW, and resize_and_overwrite'
tags:
- host
- cpp-modern
- intermediate
- 类型安全
title: 'string_view: Non-Owning Read-Only String View'
translation:
  source: documents/vol3-standard-library/strings/50-string-view.md
  source_hash: d5ceddd95c866b70af66355dadcc5d9f6cc6164cd3711a518978b1d5ce6ed797
  translated_at: '2026-09-25T23:49:14+00:00'
  engine: anthropic
  token_count: 9000
---
# string_view: Non-Owning Read-Only String View

When we covered `span` in the previous article, we rolled out the whole notion of a "non-owning view": an object that stores nothing but a pointer and a length, allocates nothing, is responsible for no deallocation, and costs almost nothing to copy. In this article we meet its character-sequence cousin: `std::string_view`.

The two look alike in form and in spirit, but their mandates sit one layer apart: `span<T>` takes arbitrary element types, readable or writable; `string_view` is dedicated to character sequences — **read-only**, with string semantics. It entered the standard with C++17 and practically overnight overturned the old way of passing read-only strings. We'll start from its plainest internal representation and work our way through both "why it is designed this way" and "how to use it correctly".

## Internal Representation: Just a (Pointer, Length) Pair

Inside, a `string_view` holds exactly two things: a `const CharT*` pointing at the first character, and a `size_t` recording the character count. No allocation, no ownership, no copying of the underlying data — exactly like `span`, with the only difference being that "the element type is locked down to characters, and always const".

So its size is deterministic: on a 64-bit platform, two 8-byte words, 16 bytes in total. Let's run it and compare against `std::string` while we're at it:

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

Compiled with `g++ -std=c++20 -O2` (local GCC 16.1.1, x86_64), this prints:

```text
sizeof(std::string)      = 32
sizeof(std::string_view) = 16
sizeof(void*)            = 8
sizeof(size_t)           = 8
```

`string` is 32 bytes; `string_view` is half of that — 16 bytes, exactly a pointer plus a size. What those 32 bytes of `string` contain (the SSO buffer, capacity, length, heap pointer) was covered thoroughly in [04-string-memory-deep-dive](../containers/04-string-memory-deep-dive.md); here you only need the conclusion: a `string` is a heavyweight object that is stateful, allocates, and carries SSO, while a `string_view` is a lightweight, non-allocating view of two words. Copying a `string_view` means copying those two words — essentially free.

::: warning Read-only by nature
`string_view` stores a `const CharT*` internally; there is no non-const version. Want to modify characters? No dice — it's a window: look, but don't touch. For writable access, use `span<char>`.
:::

## Zero-Copy Parameter Passing: The Biggest Reason It Exists

The most valuable move `string_view` offers is replacing `const std::string&` for read-only string parameters. It may sound like "swapping in something roughly equivalent", but the real-world gap is huge — especially when the caller is holding a `char*` or a string literal.

Here's a minimal comparison. Two functions do the same job (counting vowels), one signed with `const string&`, the other with `string_view`:

```cpp
// Standard: C++20
long count_vowels_ref(const std::string& s) { /* count a/e/i/o/u character by character */ }
long count_vowels_sv(std::string_view sv)   { /* same as above */ }
```

What happens on the `const string&` path when the caller holds a long enough `char*` (past the SSO threshold, too big for `string`'s small-object buffer)? **The compiler must first construct a temporary `std::string` from that `char*`** — meaning one heap allocation and one copy — and then pass a reference to that temporary in. When the function returns, the temporary is destroyed and the heap block is freed. And all we originally wanted was to "scan it read-only".

The `string_view` path is clean and direct: wrap the `char*` and its length into a 16-byte view and pass that in — no allocation, no copy.

Talk is cheap, so let's count heap allocations by overriding global `operator new` and let the evidence speak for itself:

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
    const char* long_s = "01234567890123456789034567890123456789";  // exceeds SSO

    std::cout << "--- const string& x3 (long char*) ---\n";
    take_ref(long_s); take_ref(long_s); take_ref(long_s);

    std::cout << "--- string_view x3 (long char*) ---\n";
    take_sv(long_s); take_sv(long_s); take_sv(long_s);
    return 0;
}
```

This prints:

```text
--- const string& x3 (long char*) ---
  [alloc 39 bytes]
  [alloc 39 bytes]
  [alloc 39 bytes]
--- string_view x3 (long char*) ---
```

The evidence is plain: the `const string&` path **allocates on every call** (three calls = three `alloc` lines; 39 = 38 characters + the null terminator), while the `string_view` path **allocates not even once**. That is what zero-copy parameter passing is worth — what it saves is the construction and destruction of a temporary `string`, not the modest indirection of passing a reference.

Now take this into a tight loop and watch the order of magnitude. Same long payload (90 bytes, comfortably past SSO), fifty million calls:

```cpp
// Standard: C++20 (excerpt; see the benchmark notes below for the full version)
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
    /* print both elapsed times */
}
```

Two consecutive runs of `g++ -std=c++20 -O2`, local results:

```text
const string& path (char* arg): 1820 ms
string_view  path (char* arg): 1360 ms
ratio (ref/sv): 1.34x
```

Want to run it yourself and check the ratio? Open the online demo below (about 2 seconds to run online; it prints both elapsed times and the ratio):

<OnlineCompilerDemo
  title="Zero-Copy Parameter Passing: const string& vs string_view"
  source-path="code/examples/vol3/50_string_view_benchmark.cpp"
  description="90-byte payload, 50 million calls: const string& constructs a temporary string on every call while string_view allocates nothing — measured, string_view is about 35% faster"
  allow-run
/>

`const string&` lands about 34% slower than `string_view`. Absolute microsecond numbers will drift from machine to machine, but this gap from "skipping the temporary string's allocation/free" is stable — the longer the payload (the more easily it exceeds SSO) and the more frequent the calls, the wider the gap. One caveat worth stating: if the caller already holds a `std::string`, `const string&` binds to it directly with no temporary, and the two are neck and neck. `string_view`'s parameter-passing edge cashes out **specifically** in scenarios that are "read-only, heterogeneous in source, fed from `char*` / literals / substrings".

For exactly this reason, modern APIs increasingly favor `string_view` when accepting read-only strings: it takes `std::string`, `char*`, literals, and another `string_view` alike, and no caller has to change a thing. This is the errand `string_view` had already finished for the character world by the time `span` arrived trying to unify "a run of T" parameter passing.

## remove_prefix / remove_suffix / substr: Viewport Operations, All O(1)

Since it is a view, "adjusting which slice you look at" should be cheap. `string_view` ships with a three-piece kit, all **O(1)**, all viewport adjustments, none copying the underlying data:

- `remove_prefix(n)` — moves the start forward by n, effectively chopping off the first n characters;
- `remove_suffix(n)` — moves the end back by n, effectively chopping off the last n characters;
- `substr(pos, count)` — returns a new `string_view` pointing at `[pos, pos+count)`, still without copying.

This kit is particularly handy in parsing scenarios. For example, splitting a URL into scheme / host / path segments, zero copies the whole way:

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
    scheme.remove_prefix(8);              // skip "https://"
    std::cout << "after remove_prefix(8): " << scheme << '\n';

    std::string_view host = scheme;
    auto slash = host.find('/');
    if (slash != std::string_view::npos) {
        host.remove_suffix(host.size() - slash);   // truncate at the first '/'
    }
    std::cout << "host: " << host << '\n';

    std::string_view path = sv.substr(8 + host.size());   // "/path/to/file"
    std::cout << "path: " << path << '\n';
    return 0;
}
```

This prints:

```text
after remove_prefix(8): example.com/path/to/file
host: example.com
path: /path/to/file
```

All three views point into the original `url`'s memory; not one byte was touched. That is what a "view" is supposed to look like — cut from the same mold as `span`'s `subspan` / `first` / `last`.

## Copies Happen Only at Materialization: Constructing a string from a view Isn't Free

As you use it, a question surfaces: `string_view` is so frugal — when do I actually pay for a copy? The answer is **the moment you materialize it into a `std::string`**.

```cpp
std::string_view path = /* some view */;
std::string owned = std::string{path};   // the copy happens here: allocate + copy character by character
```

Constructing a `std::string` from a `string_view` is a full copy — the standard library allocates a fresh block of memory and copies the view's characters over one by one. That is not a bug; it is inevitable: a `string` is an owner, and owning its own copy means actually taking possession of the data.

What does this mean in practice? A common misuse is "using `string_view` everywhere for a 'uniform interface', then converting back with `std::string{sv}` inside the function to store into a container or a member" — run it through that conversion and the zero-copy benefit is gone entirely, plus you have added an extra indirection. **The correct way to use `string_view`: pass it along read-only all the way, and materialize only at the moment ownership is genuinely needed — and materialize exactly once.** If you catch a value being materialized repeatedly inside a function, it should have been a `std::string` in the first place, not a `string_view`.

## The Biggest Pitfall: It Doesn't Own, So It Dangles

`string_view`'s deadliest pitfall is the inevitable price of being non-owning: **it takes no responsibility for how long the underlying data lives**. While the data lives, the view is useful; once the data is gone, the view is a wild pointer into freed memory, and touching it is undefined behavior.

The most classic posture: returning a view of a function-local `string`. The function ends, the `string` destructs, the view dangles:

```cpp
// Standard: C++20
#include <string>
#include <string_view>
#include <iostream>

std::string_view bad_return()
{
    std::string local = "hello world";
    return std::string_view{local};   // local is destroyed here; the returned view dangles immediately
}

int main()
{
    auto sv = bad_return();
    std::cout << "sv (dangling, UB): " << sv << '\n';
    std::cout << "sv.size(): " << sv.size() << '\n';
    return 0;
}
```

Run with `g++ -std=c++20 -O2` this prints (note: this is UB; your output may differ, or even look "normal" — that is exactly what makes it scary):

```text
sv (dangling, UB): �h�2
sv.size(): 11
```

`size()` still reports 11, because the length was copied into the `string_view` object at construction and `local`'s destruction doesn't touch it; but the memory holding those 11 characters has already been returned to the heap, so `operator<<` reads out garbage.

The sneakier posture is **binding to a concatenation temporary**. `s + "x"` produces a temporary `string`; bind a view to it, and the temporary is gone the moment the statement ends:

```cpp
// Standard: C++20
std::string s = "abc";
std::string_view sv = s + "x";   // the temporary string is destroyed; sv dangles
std::cout << sv << '\n';         // UB
```

Written exactly like that, the temporary's memory sometimes hasn't been flushed out of the stack frame yet, so it may even print "abcx" — looks fine, but it is a time bomb. Let's shove a few fresh allocations in to wash that buffer away and expose the flaw:

```cpp
// Standard: C++20
int main()
{
    std::string s = "abc";
    std::string_view sv = s + "x";          // dangling
    for (int i = 0; i < 3; ++i) {
        std::string noise(64, char('A' + i));
        std::cout << "noise: " << noise << '\n';
    }
    std::cout << "sv: [" << sv << "] size=" << sv.size() << '\n';
    return 0;
}
```

Run with `g++ -std=c++20 -O0`:

```text
noise: AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
noise: BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB
noise: CCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCC
sv: [@   ] size=4
```

`size` still shows 4, but `sv`'s contents have turned into `@` plus a stretch of blanks — that block of memory got overwritten by the `noise` series of allocations.

::: warning Don't fool yourself with "looks fine"
The `sv = s + "x"` snippet above often prints "normal" `abcx` under `-O2`, because the temporary's stack slot hasn't been reused yet. **This is UB, not "it works"**. Rerun the same code with `g++ -std=c++20 -O1 -fsanitize=address`, and ASan will pin it to the ground instantly:
:::

```text
==535629==ERROR: AddressSanitizer: stack-use-after-scope on address 0x...
READ of size 4 at 0x... thread T0
    #2 in std::operator<< <char, ...>(..., std::basic_string_view<char, ...>)
    #3 in main /tmp/sv_concat2.cpp:14
```

`stack-use-after-scope` — reading a temporary's data after it has left its scope. Tools like ASan exist precisely to puncture "looks fine" UB. Whenever you suspect a `string_view` lifetime issue, a run with `-fsanitize=address` beats the naked eye by a mile.

The root cause of this whole family of pitfalls boils down to one iron law: **a `string_view`'s lifetime must not outlast the data it points to**. As long as you don't bind it to temporaries, don't store it longer than the underlying data lives, and don't return views of function-local `string`s, it is safe.

## C++20 / C++23: Small Interfaces Added Later

Since `string_view` landed in C++17, later standards have patched a few small but practical interfaces onto it. Let's verify each one on GCC 16.1.1.

C++20 brought `starts_with` / `ends_with`, whose semantics are self-evident:

```cpp
// Standard: C++20
std::string_view sv = "hello world";
sv.starts_with("hello");   // true
sv.ends_with("world");     // true
```

C++23 brought `contains`, collapsing the old verbose `find(x) != npos` idiom into a single line:

```cpp
// Standard: C++23
sv.contains("lo wo");   // true
sv.contains("xyz");     // false
```

This prints:

```text
starts_with("hello"): true
ends_with("world"):   true
contains("lo wo"):    true
contains("xyz"):      false
```

C++23 also promoted "trivially copyable" from "every implementation already does this" to a hard standard requirement. Let's verify:

```cpp
// Standard: C++23
std::cout << std::is_trivially_copyable_v<std::string_view>;   // 1
std::cout << __cpp_lib_string_contains;                        // 202011
```

```text
is_trivially_copyable_v<string_view> = true
__cpp_lib_string_contains = 202011
```

The practical significance of "trivially copyable" here: a `string_view` can be safely passed across binary boundaries, moved around with `memcpy`, dropped into shared memory, and the compiler can optimize its copies with both hands free. This is the foundational qualification that makes it fit to serve as "common currency for read-only parameter passing".

::: warning On "dangling views over temporaries in range-for"
Some online resources claim that "range-based for over an expression returning a temporary view" was fixed in C++23 — that claim is inaccurate. `string_view` itself holds no data; iterating a **view bound to a temporary string** still loses the data when the temporary destructs, and the standard neither does nor can "rescue" that at the language level. What actually helps is **toolchain diagnostics**: enable static/runtime checks such as `-fsanitize=address` and `-Wdangling` (GCC/Clang). What C++23 added to `string_view` is **interface- and type-level** material — `contains` and the trivially-copyable requirement — not lifetime management. That lifetime red line is yours to hold, from start to finish.
:::

Incidentally, C++23 also gave `string_view` the ability to construct from any contiguous range (P1989), so a `std::vector<char>` can be fed straight to a function taking `string_view` — no more hand-rolling `.data()` + `.size()`. Still on the road toward C++26 is `subview` (returns a sub-view, similar to `substr` but closer to the ranges style); GCC 16.1.1 hasn't landed it yet, so that one waits for the official release.

## Summary

With `string_view` dissected this far, its full shape is clear — a **read-only character view of a pointer plus a length**, valuable for parameter passing, treacherous for dangling. Let's collect the key conclusions:

- **Internal representation**: a `const CharT*` plus a `size_t`; 16 bytes on 64-bit, half of `std::string`'s 32 bytes; no allocation, no ownership, and a copy is just copying two words.
- **Zero-copy parameter passing**: replacing `const string&` for read-only strings, with the biggest win when the caller holds a `char*` / literal — one heap allocation for a temporary `string` saved. When the caller already holds a `string`, the two are even.
- **Viewport operations**: `remove_prefix` / `remove_suffix` / `substr` are all O(1) and never copy; the underlying data stays whatever length it is — only the window you look through changes.
- **Copies only at materialization**: constructing a `std::string` from a `string_view` is a full copy. The correct usage is read-only passing all the way, materializing exactly once when ownership is truly needed.
- **The biggest pitfall is dangling**: returning a view of a function-local `string`, binding to a concatenation temporary (`s + "x"`), outliving the underlying data — all UB. "Looks fine" doesn't mean no problem; verifying with `-fsanitize=address` is the safest bet.
- **C++20/23**: `starts_with` / `ends_with` (C++20), `contains` (C++23), and the hard trivially-copyable requirement (C++23) are all in place; the lifetime red line was never "fixed" — it is held up by you plus toolchain diagnostics.

One sentence to tell it from its sibling `span`: use `span<T>` for arbitrary, possibly writable data; use `string_view` for read-only character sequences. One is oriented toward bytes, the other toward characters — same mechanism at the core, a clean division of labor.

## References

- [cppreference: std::basic_string_view](https://en.cppreference.com/w/cpp/string/basic_string_view) — the full landscape of members, constructors, `remove_prefix`/`substr`/`contains`, with per-version annotations
- [cppreference: std::basic_string_view::contains (C++23)](https://en.cppreference.com/w/cpp/string/basic_string_view/contains) — `contains` and the `__cpp_lib_string_contains` feature-test macro
- [P0123 `string_view` proposal family](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2017/n4618.pdf) — design motivations from before the C++17 landing
- [P1989R2: Range constructor for `string_view`](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2020/p1989r2.html) — C++23 construction of `string_view` from a contiguous range
- In this volume: [span: A Non-owning Contiguous View](../containers/08-span.md) — the sibling piece on the same "non-owning view" mechanism, one for bytes and one for characters
