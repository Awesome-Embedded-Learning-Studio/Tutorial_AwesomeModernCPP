---
title: "mini STL in Practice (Part 3): Vector — the Rule of Five, Exception Safety, and Concepts"
description: "Completing Vector's copy and move operations: deep copy, the three steps of copy-and-swap, move-and-swap's steal-then-swap, hard evidence of the zero copies noexcept buys, and the concepts constraint on resize. Along the way, a post-mortem of a bug I fell into myself: swap written as a steal, three rounds of tests that never caught it, until UBSan dragged it into the light."
chapter: 3
order: 3
tags:
  - host
  - cpp-modern
  - intermediate
  - 容器
  - vector
  - 移动语义
  - concepts
difficulty: intermediate
platform: host
reading_time_minutes: 13
cpp_standard: [17, 20, 23]
prerequisites:
  - "mini STL in Practice (Part 2): Vector — Growth and Relocation"
related:
  - "mini STL in Practice (Part 4): RingBuffer — the Shortest Introduction to Ring Buffers"
translation:
  source: documents/vol8-domains/data-structure/03-vector-copy-move-and-concepts.md
  source_hash: abfc069a37045e7d6c0d03ba756e9d43addba407c03b96091c969385884ec580
  translated_at: '2026-09-25T08:48:08+00:00'
  engine: anthropic
  token_count: 5700
---

# mini STL in Practice (Part 3): Vector — the Rule of Five, Exception Safety, and Concepts

Before we begin, one number to stare at—we'll explain where it comes from at the end of this part:

```text
outer=20 个,元素活着 100 个,元素级拷贝 0 次
```

Twenty `Vector`s, each holding five elements, move into an outer `Vector`; the outer one grows through several rounds along the way, and element-level copy constructions happen exactly zero times. That 0 is what the two `noexcept`s in our Rule of Five earn. At the end of the previous part, Vector still had `DISABLE_COPY` hanging on it; in this part we write the full set, one by one: destructor, copy constructor, copy assignment, move constructor, move assignment.

## swap

```cpp
    void swap(Vector& other) noexcept {
        std::swap(buffer_, other.buffer_);
        std::swap(current_cnt_, other.current_cnt_);
    }
```

Internally, `std::swap` is three moves: make a temporary, pour the contents back and forth, and let the temporary destruct. The reason it works directly on `RawBuffer` is that move assignment we wrote in Part 1—release your own block first, then take over the other's, then leave the other null. You might ask: in that first step, when we release our own block, what if the pointer in hand is null? No problem—freeing a null pointer is a no-op, explicitly allowed by the standard. With that floor under us, the three steps click together seamlessly. The copy assignment and move assignment we write later all stand on these four lines.

Get one character of this function wrong, and everything that uses it has to be redone. I took a fall on it myself; later there's a whole section devoted to the post-mortem of that accident.

## Copy Constructor and Copy Assignment

```cpp
    Vector(const Vector& other) {
        // capacity or the default size is a judgment call; the standard library uses the size in use — we align with that
        reserve(other.current_cnt_);
        for (size_t i = 0; i < other.current_cnt_; ++i) {
            std::construct_at(buffer_.data() + i, other.buffer_.data()[i]);
        }
        current_cnt_ = other.current_cnt_;
    }
    Vector& operator=(const Vector& other) {
        Vector other_{other};
        swap(other_);
        return *this;
    }
```

Look at the copy constructor first: what it does is a deep copy—allocate a separate block of memory, then copy-construct the elements over one by one. The shallow copy (copying only the pointer) ends with two Vectors sharing one block: one destructs, and the other is left holding a dangling pointer. How much memory to allocate is a trade-off: we allocate by the size in use, skipping the other's spare capacity, matching the standard library.

Copy assignment goes through copy-and-swap, in three steps. First, build a temporary `other_` via the copy constructor—note that this step touches fresh new memory; `*this` hasn't changed a single byte yet. Then swap contents with it. Finally the function returns, the temporary destructs, and the old data goes offline with it. What's so good about this shape? Watch how its rival dies. The intuitive version is "kill the old data first, then copy in the new", and it has two ways to die. The first is self-assignment. In real code, self-assignment usually hides behind an alias: two references pointing at the same object, one `a = b` line inside a function—nobody guarantees they aren't one and the same. When it really happens, you've already killed the old data, and then you go copy from "the other"—copying from your own fresh corpse. The second is exceptions: the copy constructor throws mid-way because memory ran out; your own data is already destroyed, the object is stuck half-dead, and there is no recovering it. Copy-and-swap is immune to both. Self-assignment? Worst case you copy a wasted extra and swap it back—the data is unharmed. Copy throws? The throw happens before `*this` is touched—you come out unscratched. In the end you don't even need the `if (this != &other)` special case, which here is genuinely unnecessary.

We deliberately verify this situation in the tests, using exactly an alias: `Vector<int>& alias = assigned; assigned = alias;`. That little detour dodges clangd's nagging about deliberate self-assignment. Self-assignment must be safe—but it doesn't need a parade.

## A One-Word Bug: "Steal"

Now the post-mortem. I once wrote another version where `swap` looked like this:

```cpp
    void swap(Vector& other) noexcept {
        buffer_ = std::move(other.buffer_);   // ← grabs the other's block; our own old block is already released
        std::swap(current_cnt_, other.current_cnt_);
    }
```

Zoom in on that line: `buffer_ = std::move(other.buffer_)` calls `RawBuffer`'s move **assignment**—release our own block, take over the other's, leave the other null. Net effect: the other's block becomes ours, and our original block gets released. So copy assignment `a = b` becomes: the temporary copies out b's contents, a grabs the temporary's block, and the temporary goes to its destructor holding a null pointer and a count that was never reset—calling destructors one by one against `(nullptr, old size)`. That is undefined behavior.

This bug stayed hidden through three consecutive earlier rounds of tests. Every element taking the assignment path was `int`—trivially destructible—so the destructor loop was pruned at compile time and the null pointer was never touched. In the fourth round we switched in non-trivial types, the count's up-one-down-one canceled exactly, and the assertions still all passed. Until one routine sanitizer regression run, where UBSan printed:

```text
include/.../raw_buffer.hpp:46:22: runtime error:
member call on null pointer of type 'struct Census'
```

Pull the backtrace, and it was pinned down in three minutes. The lesson of this pit is worth memorizing. When you test a resource-owning container, non-trivial types must be driven through the assignment path—trivial types wave this kind of bug through in silence, and you'd never notice. And then there's the contract of swap itself: what it promises is "exchange", while what move assignment promises is "take over the other's, release your own". The two contracts differ by a single word, yet the entire safety argument copy-and-swap builds on the word "exchange" stops holding right here.

## Move Constructor and Move Assignment

```cpp
    // noexcept guarantees nothing goes wrong while we relocate memory: move_if_noexcept!
    Vector(Vector&& other) noexcept
        : buffer_(std::move(other.buffer_)), current_cnt_(other.current_cnt_) {
        other.current_cnt_ = 0;
    }
    Vector& operator=(Vector&& other) noexcept {
        // the copied-over trick: move and swap — steal the other's contents first, then swap identities;
        Vector looter(std::move(other));
        swap(looter);
        return *this;
    }
```

What the move constructor does is plain, look: take over the other's memory, copy the count over, then zero the other out. The moved-from side gets definite semantics: an empty array. The standard only promises "valid but unspecified" for std containers; a teaching library can say it outright: a moved-from object is an empty array. Verified in the tests: after the move, `donor` is empty and still usable for another `push_back(42)`.

Our move assignment is move-and-swap: first steal the other into our hands (after the move constructor, the other has become a determinately empty object), then swap identities with `looter`, and the old data dies with `looter`'s destructor. Self-move is incidentally immune too: with `a = std::move(a)`, a is first stolen empty, then swapped back out of `looter`, returned intact—not even a `this` check needed. It runs one extra move constructor compared with copy-and-swap's dual, but for a Vector type like "two pointers plus a count", that cost is negligible.

## The Zero Copies That noexcept Buys

That "0 times" from the opening of this part can now be explained. The scenario: we have an array of arrays, the outer one being `Vector<Vector<Census>>`; when the outer one grows, the inner Vectors inside have to relocate with it. There are two ways to relocate an inner one. Moving just passes the pointer along—dirt cheap. Copying has to re-copy every element inside. Anyone would choose moving, and the standard library wants to choose it too—but it thinks one step further: what if, halfway through the relocation, some element's move constructor throws? At that point the new array is half-migrated and the old array is already torn up—there's no going back. The copy constructor has no such trouble: when it throws, the old array is still untouched; just roll back. That's why `std::vector` growth goes through `std::move_if_noexcept`: only when your move constructor carries `noexcept`—a promise never to throw—does it dare to move; otherwise it would rather be slower and copy. That clunky function name is clunky precisely because of this trade-off.

Both our move constructor and move assignment carry `noexcept`, and with the two `static_assert`s at the top of the test file we wrote that promise into compile time. Now an experiment—the example lives in `example/vector_move_if_noexcept.cpp`: add another counter to the census type that only counts copy constructions, move 20 Vectors of 5 elements each into the store, let the outer one grow through several rounds, and see where the counter stops:

```text
outer=20 个,元素活着 100 个,元素级拷贝 0 次
```

From insertion through every growth round, not a single element-level copy happened. If you're curious, run the control experiment: temporarily delete the `noexcept` from the move constructor and run it again—that 0 is no longer a 0, and growth switches to copying. One keyword decides which route the whole relocation takes when capacity grows.

## resize and concepts

```cpp
    void resize(std::size_t new_size)
        requires(std::default_initializable<Sources>)
    {
```

What resize does is straightforward: shrinking destructs the surplus elements at the tail; growing default-constructs new slots at the back. But the grow direction has a precondition—`Sources` must be default-constructible. We write that precondition right in the function signature, so whoever calls it sees it at first glance. The older style tucks the check inside the function body, and only when the template actually instantiates does it detonate a full screen of errors—enough to make your scalp tingle. Let's try it: call it with a type that has no default constructor, and see what GCC reports:

```text
error: no matching function for call to
'tamcpp::ministl::Vector<main()::NoDefault>::resize(int)'
  • candidate 1: 'void tamcpp::ministl::Vector<Sources>::resize(std::size_t)
                  requires  default_initializable<Sources> [with Sources = ...]'
      • constraints not satisfied
```

Which candidate, which constraint it got stuck on, and why—the error spells it all out line by line. The constraint becomes part of the interface documentation, and a violation lands the error right in your face; that is the felt difference concepts gives you over SFINAE. The example `example/vector_resize_concepts.cpp` keeps three commented-out lines; open them up, and you can watch the interception with your own eyes.

## Acceptance

In the second half of `tests/test_vector.cpp`, we verify each piece of this part: swap exchanges, deep copy (after copying, modify the copy—the original must not move), copy-and-swap together with aliased self-assignment, moved-from objects staying reusable, move-and-swap together with self-move, in-bounds reads and writes through visit_at, and the four resize scenarios. All these tests pass:

```text
$ ./build/tests/test_vector
VECTOR ALL GREEN
```

At this point our `Vector` has the full Rule of Five fitted out, and `stage1_rawbuf_vector` wraps up. In the next part we leave contiguous memory for the ring: subscript arithmetic has to wrap around for the first time, and on the trade-off behind that `% N` step, the Chromium folks have even left comments grumbling that a single extra modulo is too expensive.

## Building and Reproducing

```bash
cd code/volumn_codes/vol8-labs/ministl/stage1_rawbuf_vector
cmake -B build . && cmake --build build
(cd build && ctest --output-on-failure)          # 4/4
./build/example/vector_move_if_noexcept          # zero-copy evidence
./build/example/vector_resize_concepts           # concepts interception demo
```

## References

- Companion code: `code/volumn_codes/vol8-labs/ministl/stage1_rawbuf_vector/`
- [cppreference: `std::move_if_noexcept`](https://en.cppreference.com/w/cpp/utility/move_if_noexcept)
- [Move Semantics in Practice: From STL to Custom Types](../../vol2-modern-features/ch00-move-semantics/05-move-in-practice.md) (vol2 covers the language-feature layer; this part is the implementation layer)
- [Comprehensive Project: A mini-STL Algorithm Library with Concepts](../../vol4-advanced/vol3-metaprogramming-cpp20-23/09-mini-stl-with-concepts.md) (vol4; the other face of concepts)
