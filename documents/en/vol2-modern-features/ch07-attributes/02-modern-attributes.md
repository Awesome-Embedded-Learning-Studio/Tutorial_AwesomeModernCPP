# C++20-23 New Attributes: Performance-Oriented Compiler Hints

In the previous chapter, we looked at C++11-17 standard attributes, which primarily addressed "code correctness" — enforcing return value checks, suppressing warnings, and marking deprecated APIs. The new attributes in C++20 and C++23 shift focus: they are more concerned with performance, providing optimization hints to the compiler. `[[likely]]` and `[[unlikely]]` help the compiler with branch prediction optimization (ah, I remember first encountering this when reading GNU C feature code), `[[no_unique_address]]` saves redundant space in memory layouts, and `[[assume]]` lets the compiler make more aggressive optimizations based on assumptions.

When used correctly, these attributes can deliver tangible performance gains, but when misused, they can be counterproductive. Let's break them down one by one.

> In a nutshell: **C++20-23 attributes shift from "helping the compiler find bugs" to "helping the compiler optimize code." Using them in the right scenarios and verifying the results is the right approach.**

------

## [[likely]] and [[unlikely]] (C++20): Branch Prediction Hints

### Why Manual Hints Are Needed

Modern CPUs have dynamic branch predictors that guess branch directions based on runtime history. In most cases, the CPU's guesses are smart enough. However, manual hints are still valuable in the following scenarios: first, when a function is called for the first time and the branch predictor has no historical data yet; second, on embedded systems where some CPUs have relatively simple branch predictors; and third, because the compiler can improve instruction cache hit rates by adjusting code layout (keeping hot paths together).

`[[likely]]` tells the compiler "this branch is more likely to be executed," while `[[unlikely]]` means "this branch is rarely executed."

### Syntax and Placement

This pair of attributes can be placed inside the branch body of an `if` statement, or on the `case` label of an `switch`:

```cpp
// 放在 if 分支中
if (error == ErrorCode::Ok) [[likely]] {
    // 正常路径——大概率执行
    process_data();
} else {
    // 错误路径——小概率执行
    handle_error();
}

// 放在 switch case 上
switch (status) {
    [[likely]] case Status::Running:
        run_task();
        break;
    case Status::Error:
        recover();
        break;
    default:
        break;
}
```

⚠️ Note the placement of the attribute: `[[likely]]` goes before the `{` of the branch body, not on the condition expression. This is mandated by the C++20 standard.

### Analyzing the Actual Effect: Let's Look at the Assembly First

Many articles will tell you "adding `[[likely]]` makes the compiler optimize code layout," but what exactly is optimized? Talk is cheap, so let's look at the assembly directly. The following tests were compiled with GCC 15 at `-O2 -std=c++20`:

```cpp
// 不加提示
int process_no_hint(int value) {
    if (value > 0) {
        return value * 2;
    } else {
        return -value;
    }
}

// 加 [[likely]]
int process_likely(int value) {
    if (value > 0) [[likely]] {
        return value * 2;
    } else {
        return -value;
    }
}
```

The assembly generated for both functions is **exactly the same**:

```asm
process_no_hint:
process_likely:
    movl    %edi, %eax
    leal    (%rdi,%rdi), %edx
    negl    %eax
    testl   %edi, %edi
    cmovg   %edx, %eax
    ret
```

The compiler didn't generate a conditional branch at all — it used `cmovg` (conditional move) to compute both paths, then selected one based on the result of `testl`. Branch prediction? Nonexistent. `[[likely]]` has no effect here because the compiler already found a better approach than branching.

This isn't an isolated case. Modern compilers at `-O2` or even `-O1` often optimize simple conditional branches into `cmov`, bitwise operations, or mathematical formulas, rendering `[[likely]]` a pure "code comment." The scenarios where you can actually see `[[likely]]` affecting code layout are typically: when the branch body is fairly long (more than a few instructions), when the branch contains function calls or memory operations, or when the compiler faces complex logic that cannot be replaced by `cmov`.

### When It's Worth Using

So `[[likely]]` is not a magic switch where "adding it makes things faster." The correct approach is: first, use profiling (such as `perf stat -e branch-misses`) to confirm that a specific branch has a high misprediction rate, then consider adding a hint. Before adding it, compare the assembly to confirm the compiler actually changed the code layout. If the assembly hasn't changed, it means the compiler already optimized it in a better way, and `[[likely]]` is just redundant information noise.

Typical effective scenarios include: error-checking branches (normal path `[[likely]]`, error path `[[unlikely]]`), boundary condition handling, and logic where the branch body is complex enough that the compiler cannot replace it with `cmov`.

### Comparison with Compiler Builtins

Before `[[likely]]` appeared, GCC/Clang used `__builtin_expect` for branch prediction hints:

```cpp
// 旧写法
if (__builtin_expect(error == ErrorCode::Ok, 1)) {
    process_data();
}

// 新写法
if (error == ErrorCode::Ok) [[likely]] {
    process_data();
}
```

`[[likely]]` is much more readable, and the standardized attribute means it works on all compilers supporting C++20.

------

## [[no_unique_address]] (C++20): Empty Base Optimization

### The Problem: Empty Classes Still Take 1 Byte

The C++ standard requires every complete object to have a unique address, which means that even "empty classes" with no data members have an `sizeof` of at least 1. When you use an empty class as a member of another class, it wastes a byte for nothing:

```cpp
struct Empty {
    void foo() {}   // 只有成员函数，没有数据成员
};

struct Container {
    Empty e;        // sizeof(Empty) == 1，浪费
    int x;
};

static_assert(sizeof(Empty) == 1);
static_assert(sizeof(Container) == sizeof(int) + 1);  // 可能有 padding
```

For most applications, wasting 1 byte is negligible, but in generic programming, policy classes (allocators, mutex policies, etc.) are often empty. If multiple policy classes are members simultaneously, each taking 1 byte, the cumulative waste becomes significant. More critically, this can cause `sizeof` results to deviate from expectations, affecting optimizations like cache line alignment.

### The Traditional EBO Approach

The traditional solution is Empty Base Optimization (EBO) — holding empty classes through inheritance rather than as members, so the compiler doesn't need to allocate independent space for them:

```cpp
struct Empty {};

// 传统 EBO：通过继承
struct Container : private Empty {
    int x;
};

static_assert(sizeof(Container) == sizeof(int));  // Empty 不占空间
```

But EBO has a few drawbacks: you can only inherit from one empty base class of the same type (you can't simultaneously inherit two `Empty`s); inheritance is a very strong coupling relationship, and modifying inheritance hierarchies just to save memory is unreasonable; and some coding standards prohibit private inheritance.

### The [[no_unique_address]] Approach

C++20's `[[no_unique_address]]` lets you achieve the same optimization through member variables (rather than inheritance):

```cpp
struct Empty {
    void foo() {}
};

struct Container {
    [[no_unique_address]] Empty e;   // 如果 Empty 是空类，e 不占空间
    int x;
};

static_assert(sizeof(Container) == sizeof(int));  // e 被优化掉了
```

### Application in the Strategy Pattern

`[[no_unique_address]]` is particularly useful in the strategy pattern. Suppose you have a container class that accepts an allocator policy and a lock policy as template parameters. In a single-threaded scenario, the lock policy is an empty class (all methods are no-ops), and you don't want it to waste space:

```cpp
struct NullMutex {
    void lock() {}
    void unlock() {}
};

struct StdMutex {
    void lock()   { mtx_.lock(); }
    void unlock() { mtx_.unlock(); }
private:
    std::mutex mtx_;
};

template<typename T, typename Mutex = NullMutex>
class ThreadSafeBuffer {
public:
    void push(const T& item) {
        mutex_.lock();
        // ... 添加元素
        mutex_.unlock();
    }

private:
    [[no_unique_address]] Mutex mutex_;
    T* data_;
    std::size_t size_;
    std::size_t capacity_;
};

// 单线程版本：NullMutex 不占空间
ThreadSafeBuffer<int> single_thread_buf;
static_assert(sizeof(single_thread_buf) == sizeof(void*) + sizeof(std::size_t) * 2);

// 多线程版本：std::mutex 占实际空间
ThreadSafeBuffer<int, StdMutex> multi_thread_buf;
// sizeof 包含 std::mutex 的大小
```

This design lets you flexibly switch policies via template parameters without sacrificing memory efficiency. In single-threaded scenarios, not a single byte is wasted; in multi-threaded scenarios, a real mutex is used.

### Caveats

There are some details to note about `[[no_unique_address]]`. Multiple `[[no_unique_address]]` members of the same type might share the same address (because they are all empty classes and don't need to be distinguished), and the exact behavior depends on the compiler implementation:

```cpp
struct A {
    [[no_unique_address]] Empty e1;
    [[no_unique_address]] Empty e2;
    int x;
};

A a;
// &a.e1 == &a.e2 可能为 true！（GCC 15.2.1 中不一定，但第一个空成员可能与后续非空成员共享地址）
```

> **Verification**: Tested on GCC 15.2.1, multiple `[[no_unique_address]]` empty members don't necessarily share the same address, but the first empty member's address might be the same as a subsequent non-empty member. The optimization effect of `sizeof` is definite and significant.

If you need to take the addresses of these members or point references to them, be extra careful — their addresses might be identical. Additionally, this attribute only works for empty classes. If the class has data members, adding it has no effect:

```cpp
struct NotEmpty { int data; };

struct Test {
    [[no_unique_address]] NotEmpty e;   // e 仍然占 sizeof(int)
    int x;
};
static_assert(sizeof(Test) == 2 * sizeof(int));
```

Furthermore, MSVC has bugs with `[[no_unique_address]]` support in certain versions — even empty classes might not be optimized. This requires special attention in cross-platform projects, and we recommend verifying `sizeof` results on target platforms.

------

## [[assume]] (C++23): Compiler Assumptions

### Semantics

C++23's `[[assume(expression)]]` tells the compiler "please assume `expression` is true," and the compiler can make more aggressive optimizations based on this assumption. If `expression` is actually false at runtime, the behavior is undefined.

This is different from `assert`. `assert` checks the condition at runtime and terminates the program on failure; `[[assume]]` performs no runtime check at all, it simply lets the compiler optimize with confidence.

### Example

```cpp
int divide(int a, int b) {
    [[assume(b != 0)]];
    return a / b;
}
```

In this example, the compiler can theoretically eliminate the divide-by-zero check code path and generate faster division instructions. But if you pass in `b == 0`, the consequences are undefined — it might crash, return garbage values, or appear to work normally while silently causing harm.

> **Verification**: At GCC 15.2.1's `-O2` optimization level, a simple division function generated the same assembly whether or not `[[assume]]` was used. This shows that for such simple scenarios, the compiler already performs sufficient optimization. The value of `[[assume]]` mainly manifests in more complex scenarios where the compiler cannot infer invariants through static analysis.

### Comparison with __builtin_assume

Before `[[assume]]`, MSVC used `__assume`, and GCC used `__builtin_assume` (though GCC's more common approach was `if (cond) __builtin_unreachable()`):

```cpp
// MSVC
__assume(b != 0);

// GCC
if (b == 0) __builtin_unreachable();

// C++23 标准写法
[[assume(b != 0)]];
```

### Use Cases

Typical use cases for `[[assume]]` are: when you have definitive knowledge of certain runtime conditions, but the compiler cannot deduce them through static analysis. For example, if you know an array access will never go out of bounds, or that a pointer will never be null:

```cpp
void process_array(int* data, std::size_t size) {
    [[assume(data != nullptr)]];
    [[assume(size > 0)]];

    for (std::size_t i = 0; i < size; ++i) {
        // 编译器可以省略 null 检查和越界检查
        data[i] *= 2;
    }
}
```

⚠️ Warning: `[[assume]]` is the most dangerous of all attributes. If your assumption is wrong, the program's behavior is completely unpredictable. The author recommends using it only after thorough profiling, confirming the bottleneck, and when you can 100% guarantee the condition always holds. In 99% of your code, you don't need it.

------

## C++20 [[nodiscard]] Enhancements

The previous chapter already mentioned that C++20 added the ability to include custom messages with `[[nodiscard]]`. Here is a brief supplement.

### nodiscard Extensions in the Standard Library

C++20 also expanded the application scope of `[[nodiscard]]` in the standard library. The following standard library functions are marked with `[[nodiscard]]`:

- `std::vector::empty()` (since C++20)
- `std::string::empty()` (since C++20)

> **Verification**: Tested in libstdc++ 15.2.1, the `empty()` method does indeed produce a nodiscard warning. However, the claim in some articles that the `std::unique_ptr` and `std::shared_ptr` types themselves are marked `[[nodiscard]]` is not accurate in the current implementation — at least `std::make_unique()` and its constructors do not produce warnings. Different standard library implementations (libstdc++, libc++, MSVC STL) may have varying levels of support for this.

This means that if you write `vec.empty();` instead of `if (vec.empty())`, a C++20 compiler will issue a warning. This used to be a common source of bugs — `empty()` looks like "clear," but it actually means "check if empty." With `[[nodiscard]]` added, misused code will at least get a warning.

```cpp
std::vector<int> vec = {1, 2, 3};

// C++20 之前：不检查返回值，静默通过
vec.empty();  // 看起来像是清空操作，实际上什么都没做

// C++20：编译器发出 nodiscard 警告
vec.empty();  // warning: ignoring return value of 'empty()'
```

### Using nodiscard Messages in Your Own Code

For library authors, `[[nodiscard("reason")]]` is very practical. You can explain in the message why the return value shouldn't be ignored, along with the correct usage:

```cpp
// 告诉调用方为什么需要检查返回值
[[nodiscard("Memory leak: returned pointer must be freed")]]
void* allocate_buffer(std::size_t size);

// 告诉调用方应该怎么用
[[nodiscard("Store the lock_guard to keep the mutex locked")]]
std::unique_lock<std::mutex> acquire_lock();
```

------

## Comparison with C++11-17 Attributes

Putting C++11-17 attributes and the new C++20-23 attributes side by side reveals a clear development trajectory: early attributes focused on code correctness and maintainability, while later attributes focus more on performance optimization.

| Attribute | Version | Focus | Risk |
|-----------|---------|-------|------|
| `[[noreturn]]` | C++11 | Correctness | Low |
| `[[carries_dependency]]` | C++11 | Performance | Low |
| `[[deprecated]]` | C++14 | Maintainability | Low |
| `[[nodiscard]]` | C++17 | Correctness | Low |
| `[[fallthrough]]` | C++17 | Correctness | Low |
| `[[maybe_unused]]` | C++17 | Readability | Low |
| `[[likely]]/[[unlikely]]` | C++20 | Performance | Low |
| `[[no_unique_address]]` | C++20 | Performance | Low |
| `[[assume]]` | C++23 | Performance | **High** |

Among these, only `[[assume]]` is truly "dangerous" — if the assumption is wrong, the consequence is undefined behavior. For the other attributes, even if the "hint" is wrong, the worst case is slightly worse performance; it won't cause the program to crash.

------

## Practical Performance Testing Recommendations

For performance-oriented attributes like `[[likely]]`/`[[unlikely]]` and `[[assume]]`, the author's recommendation is: always measure after adding them. The optimization effect is highly dependent on the specific hardware, compiler, and code context. Some scenarios show clear benefits, while others show no difference at all.

A simple testing approach is to use `perf stat` or `valgrind --tool=cachegrind` to compare instruction counts, branch misprediction rates, and cache hit rates before and after adding the attributes. If the data doesn't show significant improvement, it's not worth adding — because attributes increase the "information density" of the code, forcing readers to understand one more concept.

For `[[no_unique_address]]`, verification is more straightforward — just look at the `sizeof` results. If the empty policy class truly takes no space, the attribute is working.

------

## Summary

The new attributes in C++20-23 extend the compiler's hinting capabilities from "finding bugs" to "performing optimizations." `[[likely]]` and `[[unlikely]]` help the compiler with branch prediction, `[[no_unique_address]]` eliminates memory waste from empty class members, and `[[assume]]` lets the compiler make more aggressive optimizations based on definitive assumptions.

The three attributes carry different levels of risk. `[[no_unique_address]]` is essentially harmless — the worst case is the optimization doesn't take effect, and `sizeof` remains unchanged. `[[likely]]`/`[[unlikely]]` also carry low risk — the worst case is an incorrect branch prediction hint, resulting in slightly worse performance. `[[assume]]` is the only truly dangerous attribute — a wrong assumption leads to undefined behavior, and it must be used with caution.

In practice, `[[no_unique_address]]` can almost be used unconditionally in generic code (strategy class patterns), `[[likely]]`/`[[unlikely]]` should be added only after profiling confirms a hotspot, and `[[assume]]` should only be used in extremely performance-sensitive scenarios, always accompanied by corresponding assertions or tests to guarantee the assumption always holds.

## References

- [cppreference: assume (C++23)](https://en.cppreference.com/w/cpp/language/attributes/assume)
- [cppreference: likely/unlikely (C++20)](https://en.cppreference.com/w/cpp/language/attributes/likely)
- [cppreference: no_unique_address (C++20)](https://en.cppreference.com/w/cpp/language/attributes/no_unique_address)
- [Don't use [[likely]] or [[unlikely]] - Aaron Ballman](https://blog.aaronballman.com/2020/08/dont-use-the-likely-or-unlikely-attributes/)
