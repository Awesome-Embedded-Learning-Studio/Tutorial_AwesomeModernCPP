---
chapter: 13
cpp_standard:
- 11
description: 'The line where templates and exceptions tangle: the three exception-safety levels, noexcept as a contract, how move_if_noexcept falls back to copy when rollback might be needed, how vector reallocation keeps the strong guarantee, and how conditional noexcept in templates faithfully reports whether the underlying operation throws.'
difficulty: intermediate
order: 8
platform: host
prerequisites:
- 'TMP Core Techniques: The World Before Concepts'
- 'Template Instantiation Control: extern template and Compile Times'
reading_time_minutes: 12
related:
- 'Template Instantiation Control: extern template and Compile Times'
- 'Capstone Project: A mini-STL Algorithm Library Constrained by Concepts'
tags:
- host
- cpp-modern
- intermediate
- 模板
- 类型安全
- 内存管理
title: 'Templates and Exception Safety: move_if_noexcept and Reallocation'
translation:
  source: documents/vol4-advanced/vol3-metaprogramming-cpp20-23/08-templates-and-exception-safety.md
  source_hash: 3b3ac4f349bb0081ace0b94aa94d1723c3825279c61f223c804068c1b006de80
  translated_at: '2026-09-26T04:50:24+00:00'
  engine: anthropic
  token_count: 1700
---
# Templates and Exception Safety: move_if_noexcept and Reallocation

The last piece ended on a teaser: why should `vector` reallocation care about the element type's `noexcept`. This piece follows that line all the way through. Templates and exceptions look like two unrelated topics, but the moment you have written a container or a generic algorithm, they collide in places like "should reallocation move or copy." Two tools sit at the core: `std::move_if_noexcept` and conditional `noexcept`. Once you understand them, you understand why everyone keeps repeating "if a move constructor can be `noexcept`, it must be `noexcept`."

## Getting the exception-safety levels straight

Exception safety has a few conventional levels of guarantee. A quick pass first, since it is mostly the first two we will use later.

- **Basic guarantee**: the function either succeeds or throws, but even if it throws, it leaks no resources and leaves no object in a corrupted state.
- **Strong guarantee**: the function either succeeds or is "as if it was never called," with the whole program state rolled back to before the call.
- **No-throw guarantee** (`noexcept`): the function guarantees it does not throw.

The strong guarantee is far stricter than the basic one; it demands "rollback capability." The thread of this piece: `vector` reallocation wants to keep the strong guarantee, and that goal directly determines whether it moves or copies elements when relocating them.

## noexcept is a contract to the caller

The `noexcept` keyword is often misread as "I'll try my best not to throw." What it really means is "I guarantee I won't throw," and if the function does throw anyway, the program goes straight to `std::terminate` — no unwinding, no propagation. So `noexcept` isn't written for the function itself; it is a contract for **the caller** — once the caller sees `noexcept`, it can optimize with confidence.

The optimization that matters most in this piece: only a `noexcept` move may be used in a scenario that "cannot fail halfway." `vector` reallocation is exactly such a scenario. If your move constructor is marked `noexcept`, `vector` dares to use it when reallocating; if it isn't marked, `vector` doesn't dare and would rather copy. We will run this difference for real below.

## move_if_noexcept: falling back to copy when rollback might be needed

C++11 gave us a tool, `std::move_if_noexcept`, which folds the tension between "move" and "the strong exception guarantee" into a single function. The behavior is plain: if `T`'s move constructor is `noexcept`, it returns an rvalue reference (move); otherwise it returns a const lvalue reference (copy).

```cpp
// The gist (the standard library's real implementation is equivalent to this check)
template <typename T>
conditional_t<is_nothrow_move_constructible_v<T> && !is_lvalue_reference_v<T>,
              T&&, const T&>
move_if_noexcept(T& x) noexcept;
```

Why do we need such a thing? Picture an operation that "might have to roll back after moving": move a batch of elements from old memory to new memory, and halfway through, one of the moves throws. If you were moving, the element being moved may already be half-pulled-out of the old memory — its state is a mess, and there is no rolling back; the strong guarantee is gone. If you were copying, when a copy throws, the original elements in the old memory were never touched at all; rollback is just "free the new memory," clean as a whistle. So in situations where rollback might be needed, if move isn't safe enough you fall back to copy — that is what `move_if_noexcept` is for. Let's measure it for real:

```cpp
struct NothrowMove {
    int* p;
    explicit NothrowMove(int v) : p(new int(v)) {}
    NothrowMove(const NothrowMove& o) : p(new int(*o.p)) { std::cout << "    copy\n"; }
    NothrowMove(NothrowMove&& o) noexcept : p(o.p) { o.p = nullptr; std::cout << "    move\n"; }
};

struct ThrowingMove {
    int* p;
    explicit ThrowingMove(int v) : p(new int(v)) {}
    ThrowingMove(const ThrowingMove& o) : p(new int(*o.p)) { std::cout << "    copy\n"; }
    ThrowingMove(ThrowingMove&& o) noexcept(false) : p(o.p) { o.p = nullptr; std::cout << "    move\n"; }
};
```

<OnlineCompilerDemo allow-run
  title="move_if_noexcept: picking between move and copy by noexcept"
  source-path="code/examples/vol4/vol3-metaprogramming-cpp20-23/move_if_noexcept_demo.cpp"
  description="NothrowMove gets moved, ThrowingMove falls back to copy — watch how move_if_noexcept decides based on noexcept."
/>

Run it:

```text
is_nothrow_move_constructible:
  NothrowMove:  true
  ThrowingMove: false
move_if_noexcept 对 NothrowMove(应为 move):
    [NothrowMove] 被移动
move_if_noexcept 对 ThrowingMove(应为 copy):
    [ThrowingMove] 被拷贝
```

`NothrowMove`'s move is `noexcept`, so `move_if_noexcept` hands back an rvalue reference and the move happens; `ThrowingMove`'s move is marked `noexcept(false)` — it may throw — so `move_if_noexcept` falls back to a const reference and the copy happens. That is its "decide move or copy by looking at noexcept" behavior.

## How vector reallocation keeps the strong exception guarantee

When a `std::vector` is full and you `push_back` again, it allocates a larger block of memory, relocates the old elements over, and then frees the old memory. That "relocation" uses `move_if_noexcept`. Let's look directly at the copy/move calls during reallocation for different element types:

<OnlineCompilerDemo allow-run
  title="vector reallocation: move vs copy, and the strong guarantee"
  source-path="code/examples/vol4/vol3-metaprogramming-cpp20-23/vector_realloc.cpp"
  description="During reallocation NothrowMove gets moved and ThrowingMove gets copied — hard evidence of the price vector pays to keep the strong guarantee."
/>

Run it:

```text
vector<NothrowMove> 预留 2,再 push 第三个触发扩容:
  >>> 扩容时(move noexcept,应为 move):
    move
    move

vector<ThrowingMove> 预留 2,再 push 第三个触发扩容:
  >>> 扩容时(move 可能抛,应为 copy 保强异常保证):
    copy
    copy
```

`NothrowMove`'s move is `noexcept`, so `vector` reallocates with moves without worry — fast, and it cannot throw; `ThrowingMove`'s move may throw, so `vector` doesn't dare use it and dutifully copies. Copy is slower than move (it has to deep-copy what the `int*` points to), but that is the price of keeping the strong guarantee: should a copy throw midway, the elements in the old memory are untouched, and `vector` can roll back to its pre-reallocation state.

::: warning If your move constructor isn't marked noexcept, vector will copy
This is the most practical lesson of this piece. Many beginners finish writing a class's move constructor, figure "it won't throw anyway," and skip the `noexcept`. The consequence: put that class into a `vector`, and every reallocation copies it, silently throwing away move's performance — and the compiler gives you no hint at all. Burn the rule in: for move construction and move assignment, whenever you are sure they cannot throw (usually they just shuffle pointers and a few primitives), always add `noexcept`. This isn't just style; it is a real, physical performance switch.
:::

Of course, not every move can be `noexcept`. If a move has to allocate memory inside (say moving a `std::vector`'s elements might require allocating a new `vector`), allocation can throw, and then you can't mark it blindly. The standard library's `std::vector` has a `noexcept` move constructor of its own, because it merely steals the other side's internal pointers — no allocation. The criterion is plain: look at what the move actually does, and whether any of it could throw.

## Conditional noexcept: faithfully reporting whether the underlying operation throws

A template function has no way to know whether the type `T` it operates on might throw, but it can "inherit" that information, thanks to **conditional noexcept**: `noexcept(noexcept(expression))`. The outer `noexcept` is the specifier; the inner `noexcept(...)` is the operator (evaluated at compile time, asking "is this expression `noexcept`"). Together they say: "my function's noexcept-ness equals the noexcept-ness of that underlying expression."

```cpp
// No noexcept marked: the caller can only conservatively assume it may throw
template <typename T>
void uncond_op(T& x) {
    T tmp(std::move(x));
    x = std::move(tmp);
}

// Conditional noexcept: inherit whether T's move construction is noexcept
template <typename T>
void cond_op(T& x) noexcept(noexcept(T(std::move(x)))) {
    T tmp(std::move(x));
    x = std::move(tmp);
}
```

Run it, and see the noexcept-ness of both versions through a caller's eyes:

<OnlineCompilerDemo allow-run
  title="Conditional noexcept: propagating whether the underlying operation throws"
  source-path="code/examples/vol4/vol3-metaprogramming-cpp20-23/noexcept_propagation.cpp"
  description="A template with no noexcept is conservatively assumed to possibly throw; noexcept(noexcept(...)) lets noexcept-ness follow the underlying operation."
/>

Run it:

```text
uncond_op<NoThrowMove> noexcept: false
cond_op<NoThrowMove> noexcept:   true
cond_op<ThrowMove> noexcept:     false
```

`uncond_op`, even when the underlying type's move is noexcept, isn't marked itself, so a caller's check still reports `false` — it threw the information away for nothing. `cond_op` uses conditional noexcept to pass the underlying truth out: it is `noexcept` when the underlying move is noexcept, and honestly `false` when the underlying move may throw. This one matters a lot when writing generic containers and algorithms: if your `swap`, `move`, `emplace` and friends don't use conditional noexcept, operations that are "actually noexcept" get misreported as "may throw" all along the call chain; a downstream container checks once, falls back to copy, and the move optimization is lost entirely.

## Tying it all together

The things in this piece are strung together by one logic. `noexcept` is a contract to the caller saying "I don't throw"; `move_if_noexcept` uses that contract to pick move or copy in "rollback might be needed" situations; `vector` reallocation is exactly such a situation, so it decides move vs copy by whether the element's move is noexcept; and conditional `noexcept` lets a template function pass the underlying noexcept-ness up truthfully. So a `noexcept` annotation that looks like "just a matter of style" is in fact the key to whether move performance gets delivered at all. In the next piece we pool these metaprogramming skills together and build a mini-STL algorithm library constrained by concepts, as the closing project.
