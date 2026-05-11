# std::array

Do C-style arrays get the job done? Absolutely, and we covered this in the previous section—in fact, we've been using them since our very first day of learning C. (If you're not familiar with C, the repository also includes a detailed C tutorial!)

However, C arrays are notoriously easy to shoot yourself in the foot with: they decay into pointers when passed to functions, lose their length information, cannot be directly assigned, cannot be returned from functions, and have no bounds checking. These issues can't be avoided simply by "being careful when writing code"—they are inherent design flaws of C arrays.

`std::array` was created to solve these problems. It allocates memory on the stack, making it just as compact and efficient as a C array, but it provides true value semantics—it can be copied, assigned, passed as arguments, and returned, and it always knows its own size. Let's look at why, starting with C++11, we should prefer `std::array` for fixed-size arrays.

> **Learning Objectives**
> After completing this chapter, you will be able to:
>
> - [ ] Correctly declare and use `std::array`
> - [ ] Understand the difference between value semantics and array decay
> - [ ] Use `std::array` with STL algorithms for common operations
> - [ ] Obtain the underlying pointer when interacting with C APIs

## Environment Setup

- Platform: Linux x86\_64 (WSL2 is also fine)
- Compiler: GCC 13+ or Clang 17+
- Compiler flags: `-Wall -Wextra -std=c++17`

## std::array Basics

`std::array` is defined in the `<array>` header and requires two template parameters: the element type and the fixed size. The size must be a compile-time constant—just like a C array, `std::array` does not grow dynamically; it is simply a contiguous block of memory with a fixed size.

```cpp
#include <array>
#include <iostream>

int main()
{
    std::array<int, 5> arr = {1, 2, 3, 4, 5};

    std::cout << "大小:     " << arr.size() << "\n";
    std::cout << "为空?     " << (arr.empty() ? "是" : "否") << "\n";
    std::cout << "最大大小: " << arr.max_size() << "\n";

    return 0;
}
```

```text
大小:     5
为空?     否
最大大小: 5
```

Functions like `size()`, `max_size()`, and `empty()` might seem redundant for a fixed-size `std::array`. Their purpose lies in providing a unified interface—ensuring that `std::array` and `std::vector` share the same access methods, so generic code doesn't need to care whether the underlying container is fixed-size or dynamic.

> `std::array<int, 0>` is legal, in which case `empty()` returns `true`. But honestly, a zero-size `std::array` rarely appears in real code. If you need a container that "might be empty," use `std::vector`.

## Accessing Elements

`std::array` provides multiple ways to access elements. The most common are `[]` and the safe `at()`, along with convenient interfaces for directly accessing the first and last elements and obtaining the underlying pointer:

```cpp
#include <array>
#include <iostream>

int main()
{
    std::array<int, 5> arr = {10, 20, 30, 40, 50};

    std::cout << "arr[0]     = " << arr[0] << "\n";      // 无边界检查
    std::cout << "arr.at(2)  = " << arr.at(2) << "\n";   // 越界抛异常
    std::cout << "front      = " << arr.front() << "\n";
    std::cout << "back       = " << arr.back() << "\n";

    int* p = arr.data();                                   // 获取裸指针
    std::cout << "data()[3]  = " << p[3] << "\n";

    return 0;
}
```

```text
arr[0]     = 10
arr.at(2)  = 30
front      = 10
back       = 50
data()[3]  = 40
```

The difference between `[]` and `at()` is crucial: `arr[10]` on this five-element array is undefined behavior (UB)—it might read garbage, crash, or seemingly work fine while silently corrupting data. In contrast, `arr.at(10)` throws a `std::out_of_range` exception, giving you a chance to handle the error gracefully.

> A quick side note: during development, we recommend using `at()` for indexes that might go out of bounds. In release builds, you can switch back to `[]` to avoid exception overhead—though modern compilers impose virtually zero overhead for `at()` in non-out-of-bounds cases. Alternatively, use `[]` consistently and rely on AddressSanitizer to catch out-of-bounds bugs.

`data()` returns a raw pointer to the underlying element storage, which can be passed directly when interacting with C library functions that accept `int*` parameters.

## Value Semantics—The Killer Advantage of std::array

After covering all those basics, here is where `std::array` truly leaves C arrays behind: it has **value semantics**. You can work with it just like an `int` or `std::string`—copying, assigning, passing as arguments, and returning all work flawlessly.

```cpp
#include <array>
#include <iostream>

// 直接返回 std::array——C 数组做不到这一点
std::array<int, 5> make_array()
{
    std::array<int, 5> result = {1, 2, 3, 4, 5};
    return result;
}

// 按值传参——不会丢失大小信息
void print_array(std::array<int, 5> arr)
{
    for (int x : arr) {
        std::cout << x << " ";
    }
    std::cout << "\n函数内大小: " << arr.size() << "\n";
}

int main()
{
    auto arr1 = make_array();
    auto arr2 = arr1;  // 直接拷贝——C 数组做不到

    arr2[0] = 99;
    std::cout << "arr1[0] = " << arr1[0] << "\n";  // 1，不受 arr2 影响
    std::cout << "arr2[0] = " << arr2[0] << "\n";  // 99

    print_array(arr1);
    print_array(arr2);

    return 0;
}
```

```text
arr1[0] = 1
arr2[0] = 99
1 2 3 4 5
函数内大小: 5
99 2 3 4 5
函数内大小: 5
```

Every line above is something a C array cannot do. C arrays cannot be directly assigned (`a = b` won't even compile), cannot be returned from functions, and decay into pointers when passed as function arguments, losing their length. `std::array` can do all of this because it is a class that encapsulates an internal C array and provides a copy constructor and copy assignment operator. The compiler knows how to copy this object and knows its size—fundamentally eliminating the array decay problem.

> Passing an `std::array` by value copies the entire array contents. If the array is large (e.g., `std::array<int, 10000>`), you should use a `const` reference: `void process(const std::array<int, 10000>& arr)`. For small arrays, the overhead of passing by value is negligible.

## C Array vs std::array: A Head-to-Head Comparison

Let's do a direct comparison of C arrays and `std::array` across common operations:

| Operation | C Array | std::array |
|-----------|---------|------------|
| Declaration | `int arr[5];` | `std::array<int, 5> arr;` |
| Get size | `sizeof(arr)/sizeof(arr[0])` (fails after passing to function) | `arr.size()` (always valid) |
| Assignment | Not supported | `arr2 = arr1` |
| Copying | Manual `memcpy` | `auto copy = arr;` |
| Passing as argument | Decays to pointer, loses size | Preserves size by value, or pass by reference |
| Return value | Impossible | Possible |
| Bounds checking | None | `arr.at(i)` throws exception |
| Get raw pointer | Decays automatically | `arr.data()` (explicit) |
| Zero overhead | Yes | Yes |

The last row is key: **`std::array` and C arrays are completely equivalent in memory layout and runtime performance**. All the extra capabilities—`size()`, `at()`, `data()`, and value semantics—are zero-overhead abstractions at compile time. There is no additional memory allocation or function call overhead at runtime.

> If you're curious, try adding `-O2` when compiling a traversal program that uses a C array and `std::array` respectively, and compare the assembly output—the generated instructions are almost identical. Zero-overhead abstraction is not just an empty phrase.

## Filling, Swapping, and Traversing

`std::array` also provides several practical operations, and when combined with STL algorithms, it becomes even more powerful:

```cpp
#include <algorithm>
#include <array>
#include <iostream>

int main()
{
    std::array<int, 5> a = {1, 2, 3, 4, 5};
    std::array<int, 5> b = {10, 20, 30, 40, 50};

    // fill —— 全部设为同一值
    a.fill(0);
    std::cout << "fill 后: ";
    for (int x : a) { std::cout << x << " "; }
    std::cout << "\n";

    // swap —— 交换两个 array 的内容
    a = {1, 2, 3, 4, 5};
    a.swap(b);
    std::cout << "swap 后 a: ";
    for (int x : a) { std::cout << x << " "; }
    std::cout << "\n";

    // 配合 <algorithm>
    std::array<int, 5> c = {5, 3, 1, 4, 2};
    std::sort(c.begin(), c.end());
    std::cout << "排序后: ";
    for (int x : c) { std::cout << x << " "; }
    std::cout << "\n";

    return 0;
}
```

```text
fill 后: 0 0 0 0 0
swap 后 a: 10 20 30 40 50
排序后: 1 2 3 4 5
```

`fill()` is extremely convenient when you need to reset a buffer, getting it done in a single line. The underlying mechanism of `swap()` is element-by-element swapping, with a time complexity of O(n). `std::sort`, `std::find`, `std::reverse`—the entire STL algorithm library can be used directly on `std::array` by passing `begin()` and `end()`.

## Hands-on: Rewriting C Array Code with std::array

Let's re-implement the operations we previously did with C arrays using `std::array`, so we can intuitively see where the improvements lie:

```cpp
#include <algorithm>
#include <array>
#include <iostream>

// 函数签名清晰——类型和大小一目了然，不需要额外传长度
void print_stats(const std::array<int, 5>& data)
{
    std::cout << "元素个数: " << data.size() << "\n";

    auto [min_it, max_it] = std::minmax_element(data.begin(), data.end());
    std::cout << "最小值: " << *min_it << "\n";
    std::cout << "最大值: " << *max_it << "\n";

    int sum = 0;
    for (int x : data) { sum += x; }
    std::cout << "平均: " << static_cast<double>(sum) / data.size() << "\n";
}

int main()
{
    std::array<int, 5> scores = {85, 92, 78, 96, 88};

    std::cout << "原始数据: ";
    for (int x : scores) { std::cout << x << " "; }
    std::cout << "\n\n";

    print_stats(scores);

    std::sort(scores.begin(), scores.end());
    std::cout << "\n排序后: ";
    for (int x : scores) { std::cout << x << " "; }

    auto it = std::find(scores.begin(), scores.end(), 88);
    if (it != scores.end()) {
        std::cout << "\n找到 88，下标: " << (it - scores.begin());
    }

    std::reverse(scores.begin(), scores.end());
    std::cout << "\n反转后: ";
    for (int x : scores) { std::cout << x << " "; }
    std::cout << "\n";

    return 0;
}
```

Compile and run:

```bash
g++ -Wall -Wextra -std=c++17 std_array.cpp -o std_array && ./std_array
```

```text
原始数据: 85 92 78 96 88

元素个数: 5
最小值: 78
最大值: 96
平均: 87.8

排序后: 78 85 88 92 96
找到 88，下标: 2
反转后: 96 92 88 85 78
```

The improvements are comprehensive: the parameter for `print_stats` is `const std::array<int, 5>&`, making the type and size clear at a glance; all STL algorithms are used directly—sorting, searching, reversing, and finding min/max values are all one-liners; and you will never encounter the problem of array decay losing length information.

> If you see C-style signatures like `void func(int arr[], int n)` in your code, we recommend changing them to `void func(const std::array<int, N>& arr)` (fixed size) or `void func(std::span<int> arr)` (runtime-determined size). Both approaches preserve length information and are much safer than manually passing `n`.

## Summary

- `std::array<T, N>` allocates on the stack and is just as compact as a C array, but without the array decay problem
- Use `[]` (no checking) or `at()` (throws on out-of-bounds) to access elements, and use `data()` when interacting with C APIs
- It has true value semantics—it can be copied, assigned, passed as arguments, and returned, which is its biggest advantage over C arrays
- `fill()`, `swap()`, and iterator interfaces allow it to work seamlessly with STL algorithms
- Zero-overhead abstraction—runtime performance is completely equivalent to C arrays

### Common Mistakes

| Mistake | Cause | Solution |
|---------|-------|----------|
| Reading without initializing | Element values are undefined for an uninitialized local `std::array` | Use `std::array<int, N> arr = {};` or `arr.fill(0)` |
| `arr[arr.size()]` out of bounds | Index range is `[0, size())` | Use `arr.at()` for bounds checking |
| Passing large arrays by value | Copies the entire array contents | Pass using a `const` reference |
| Trying to change size dynamically | `std::array` size is fixed at compile time | Use `std::vector` if you need dynamic sizing |

## Exercises

### Exercise 1: Rewrite C Array Exercises

Rewrite all the previous exercises written with C arrays using `std::array`: declaration, initialization, traversal, passing as arguments, and finding the maximum value. Experience the differences in clarity and safety between the two approaches.

### Exercise 2: Grade Sorting and Statistics

Create a `std::array<int, 8>` to store a set of grades, use `std::sort` to sort them, and then output the highest score, lowest score, and average score. All statistical operations must use functions from `<algorithm>`.

### Exercise 3: Check if an Element Exists

Write a `bool contains(const std::array<int, 5>& arr, int value)` that uses `std::find` to determine whether an array contains a specified value. Test both existing and non-existing values in `main`.

---

> **Next up**: `std::array` solved the problem of fixed-size containers, but what about strings? The pitfalls of C-style strings—manually managing `'\0'`, easy out-of-bounds access, and lacking value semantics—are exactly the same as those of C arrays. Next, we'll get to know `std::string` and see how modern C++ elegantly handles strings.
