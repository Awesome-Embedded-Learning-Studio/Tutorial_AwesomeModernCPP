---
chapter: 13
cpp_standard:
- 11
description: 'The line where templates and exceptions tangle. The three exception-safety levels, noexcept as a contract, how move_if_noexcept falls back to copy when rollback may be needed, how vector reallocation preserves the strong guarantee, and how conditional noexcept propagates the truth about whether something throws.'
difficulty: intermediate
order: 8
platform: host
prerequisites:
- 'TMP Core Techniques: The World Before Concepts'
- 'Template Instantiation Control: extern template and Compile Time'
reading_time_minutes: 14
related:
- 'Template Instantiation Control: extern template and Compile Time'
- 'Comprehensive Project: A mini-STL Algorithm Library with Concepts'
tags:
- host
- cpp-modern
- intermediate
- 模板
- 类型安全
- 内存管理
title: 'Templates and Exception Safety: move_if_noexcept and Reallocation'
---
# Templates and Exception Safety: move_if_noexcept and Reallocation

The last piece ended on a teaser: why does `vector` reallocation care about the element type's `noexcept`. This piece takes that line all the way. Templates and exceptions look unrelated, but the moment you write a container or a generic algorithm they collide in places like "should reallocation move or copy." The core tools are two: `std::move_if_noexcept` and conditional `noexcept`. Understand them, and you understand why everyone insists "mark your move constructor `noexcept` if you possibly can."

## First, the exception-safety levels

Exception safety has a few conventional guarantees. Quick pass, since we mostly use the first two.

- **Basic guarantee**: the function either succeeds or throws, but even if it throws it leaks no resources and leaves no object in a corrupted state.
- **Strong guarantee**: the function either succeeds or "acts as if it was never called," rolling the whole program state back to before the call.
- **No-throw guarantee** (`noexcept`): the function promises not to throw.

The strong guarantee is much stricter than the basic one. It demands "rollback." The thread of this piece is that `vector` reallocation wants to keep the strong guarantee, and that goal directly decides whether it moves or copies elements.

## noexcept is a contract to the caller

The `noexcept` keyword is often misread as "I'll try not to throw." What it really means is "I promise not to throw," and if the function does throw, the program goes straight to `std::terminate`, no unwinding, no propagation. So `noexcept` isn't written for the function itself. It's a contract for **the caller**. The caller sees `noexcept` and can optimize freely.

The optimization that matters most for this piece is that a `noexcept` move can be used where "no mid-step failure" is required. `vector` reallocation is exactly such a case. If your move constructor is marked `noexcept`, `vector` dares to move during reallocation. If it isn't, `vector` won't risk it and will copy instead. We'll run this difference below.

## move_if_noexcept: fall back to copy when rollback may be needed

C++11 ships a tool, `std::move_if_noexcept`, that folds the tension between "move" and "the strong guarantee" into one function. The behavior is plain. If `T`'s move constructor is `noexcept`, it returns an rvalue reference (move). Otherwise it returns a const lvalue reference (copy).

```cpp
// The gist (the standard library's real implementation is equivalent to this check)
template <typename T>
conditional_t<is_nothrow_move_constructible_v<T> && !is_lvalue_reference_v<T>,
              T&&, const T&>
move_if_noexcept(T& x) noexcept;
```

Why do we need this? Picture an operation "that may need to roll back after moving." Move a batch of elements from old memory to new memory, and halfway through, a move throws. If you were moving, the elements being moved may already be half-pulled-out of the old memory, the state is wrecked, and rollback is impossible. The strong guarantee is gone. If you were copying, when a copy throws, the original elements in the old memory were never touched. Rollback is "free the new memory," clean. So in "rollback-possible" situations, if move isn't safe enough you fall back to copy. That's what `move_if_noexcept` is for. Let's measure it:

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
  title="move_if_noexcept: move or copy based on noexcept"
  source-path="code/examples/vol4/vol3-metaprogramming-cpp20-23/move_if_noexcept_demo.cpp"
  description="NothrowMove moves, ThrowingMove falls back to copy. See how move_if_noexcept decides based on noexcept."
/>

Run it:

```text
is_nothrow_move_constructible:
  NothrowMove:  true
  ThrowingMove: false
move_if_noexcept on NothrowMove (should move):
    [NothrowMove] moved
move_if_noexcept on ThrowingMove (should copy):
    [ThrowingMove] copied
```

`NothrowMove`'s move is `noexcept`, so `move_if_noexcept` yields an rvalue reference and moves. `ThrowingMove`'s move is marked `noexcept(false)`, it may throw, so `move_if_noexcept` falls back to a const reference and copies. That's the "move or copy based on `noexcept`" behavior.

## How vector reallocation keeps the strong guarantee

When `std::vector` runs out of room and you `push_back` again, it allocates a larger block, moves the old elements over, and frees the old memory. The "move over" uses `move_if_noexcept`. Let's see the copy/move calls during reallocation for different element types:

<OnlineCompilerDemo allow-run
  title="vector reallocation move vs copy, the strong guarantee"
  source-path="code/examples/vol4/vol3-metaprogramming-cpp20-23/vector_realloc.cpp"
  description="During reallocation NothrowMove is moved, ThrowingMove is copied. The cost vector pays to keep the strong exception guarantee."
/>

Run it:

```text
vector<NothrowMove> reserved 2, push a third to trigger reallocation:
  >>> during reallocation (move is noexcept, should move):
    move
    move

vector<ThrowingMove> reserved 2, push a third to trigger reallocation:
  >>> during reallocation (move may throw, should copy to keep the strong guarantee):
    copy
    copy
```

`NothrowMove`'s move is `noexcept`, so reallocation moves, fast and non-throwing. `ThrowingMove`'s move may throw, so `vector` doesn't dare and copies. Copy is slower than move (it deep-copies what `int*` points at), but that's the cost of keeping the strong guarantee: if a copy throws midway, the elements in old memory are untouched, and `vector` can roll back to the pre-reallocation state.

::: warning If your move constructor isn't noexcept, vector will copy
This is the most practical lesson of the piece. Many beginners write a class's move constructor, think "it won't throw anyway," and skip `noexcept`. The result: that class goes into a `vector`, and every reallocation copies it instead of moving. The performance of move is thrown away, and the compiler gives you no warning. Remember the rule: for move construction and move assignment, if you're sure they don't throw (usually they just shuffle pointers and a few primitives), always add `noexcept`. This isn't style. It's a real performance switch.
:::

Not every move can be `noexcept`, of course. If a move has to allocate inside (say, moving an element of a `std::vector`, which may need to allocate a new `vector`), allocation can throw, so you can't blindly mark it. The standard library's `std::vector` has a `noexcept` move constructor because it only steals the other's internal pointers, no allocation. The test is plain: what does the move do, and could any of it throw.

## Conditional noexcept: propagate whether the underlying operation throws

A template function doesn't know whether the type `T` it operates on might throw. But it can "inherit" that information through **conditional noexcept**: `noexcept(noexcept(expression))`. The outer `noexcept` is the specifier. The inner `noexcept(...)` is the operator, evaluated at compile time, asking "is this expression `noexcept`." Together: "my function's `noexcept`-ness equals the underlying expression's `noexcept`-ness."

```cpp
// No noexcept marked: the caller has to assume it may throw
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

Run it and see how both writes look to the caller:

<OnlineCompilerDemo allow-run
  title="Conditional noexcept: propagate whether the underlying throws"
  source-path="code/examples/vol4/vol3-metaprogramming-cpp20-23/noexcept_propagation.cpp"
  description="A template with no noexcept is conservatively assumed to throw. noexcept(noexcept(...)) makes noexcept-ness follow the underlying operation."
/>

Run it:

```text
uncond_op<NoThrowMove> noexcept: false
cond_op<NoThrowMove> noexcept:   true
cond_op<ThrowMove> noexcept:     false
```

`uncond_op`, even with a `noexcept` move underneath, reports `false` because it isn't marked. It threw the information away. `cond_op` uses conditional noexcept to pass the truth up: `noexcept` when the underlying move is, `false` when the underlying move may throw. This matters when writing generic containers and algorithms. If your `swap`, `move`, or `emplace` operations don't use conditional noexcept, the whole call chain misreports "actually `noexcept`" operations as "may throw." A downstream container checks and falls back to copy, and the move optimization is lost.

## Tying it together

The pieces in this article string along one logic. `noexcept` is a contract to the caller that says "I don't throw." `move_if_noexcept` uses that contract to pick move or copy in "rollback-possible" situations. `vector` reallocation is exactly such a situation, so it decides move or copy based on whether the element's move is `noexcept`. Conditional `noexcept` lets a template function propagate the underlying `noexcept`-ness upward. So a `noexcept` annotation that looks like "just a style choice" is actually the key to whether move performance gets delivered. In the next piece we pool these metaprogramming skills and build a mini-STL algorithm library constrained by concepts, as the closing project.
