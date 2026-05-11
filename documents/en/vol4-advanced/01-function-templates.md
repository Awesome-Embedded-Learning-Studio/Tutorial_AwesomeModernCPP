# Modern C++ for Embedded Systems Tutorial—A Deep Dive into Function Templates

Function templates are the starting point of C++ generic programming, allowing us to write a single piece of code that handles multiple types. But do you truly understand how the compiler deduces template parameters? Why does deduction sometimes fail? What is the difference between `auto` and template argument deduction?

In this chapter, we will explore the internal mechanisms of function templates and implement a type-safe `min/max/clamp` function family.

------

## Function Template Basic Syntax

### Basic Form

A function template begins with `template<...>`, followed by the function declaration:

```cpp
template<typename T>
T max(const T& a, const T& b) {
    return a > b ? a : b;
}

// 使用
int x = max(5, 10);        // T推导为int
double d = max(3.14, 2.71); // T推导为double
```

**Key points**:

- `typename T` declares a type template parameter `T`
- The `typename` keyword can be replaced with `class` (but `typename` is recommended)
- The compiler automatically deduces the type of `T` based on the argument types

### Multiple Template Parameters

```cpp
template<typename T, typename U>
auto add(T a, U b) -> decltype(a + b) {
    return a + b;
}

// 使用
int x = 5;
double y = 3.14;
auto result = add(x, y);  // 返回double
```

**Note**: `T` and `U` are deduced independently and may result in different types.

### Non-Type Template Parameters

In addition to types, template parameters can also be compile-time constants:

```cpp
template<typename T, std::size_t N>
std::size_t array_size(T (&arr)[N]) {
    return N;  // 编译期获取数组大小
}

int data[42];
std::size_t size = array_size(data);  // 返回42，且是编译期常量
```

This is particularly useful in embedded systems—we can safely obtain the size of an array without it decaying into a pointer.

------

## Template Argument Deduction Rules

### Rule 1: Exact Match Principle

The compiler looks for the "best match" template parameter type without considering implicit conversions:

```cpp
template<typename T>
void process(T value);

process(42);      // T推导为int
process(3.14);    // T推导为double
process('a');     // T推导为char

// 但这不会工作：
process(42, 3.14);  // 错误：只有一个T，无法同时匹配int和double
```

### Rule 2: References Are Ignored (By Default)

By default, template argument deduction ignores references and top-level const:

```cpp
template<typename T>
void func(T arg);

int x = 42;
const int& cref = x;

func(x);    // T推导为int
func(cref); // T推导为int（const和引用都被忽略）

// 如果想保留引用和const：
template<typename T>
void func_const(const T& arg);

func_const(cref); // T推导为int，但参数类型是const int&
```

**Remember**:

- `T` deduces the "type after removing references and top-level const"
- `const T&` preserves reference semantics
- `T&&` is a universal reference (detailed later)

### Rule 3: Array Decay to Pointer

```cpp
template<typename T>
void func(T arg);

int arr[10];

func(arr);  // T推导为int*（数组退化为指针）

// 如果想保留数组类型：
template<typename T, std::size_t N>
void func(T (&arr)[N]);

int arr[10];
func(arr);  // T推导为int，N推导为10
```

### Rule 4: Function Decay to Function Pointer

```cpp
template<typename T>
void func(T arg);

void some_func(int);

func(some_func);  // T推导为void(*)(int)

// 保留函数类型：
template<typename T>
void func_ref(T& arg);

func_ref(some_func);  // T推导为void(int)
```

### Practical Deduction Table

| Argument Type | `T` | `const T&` | `T&&` |
|----------|-----|-----------|-------|
| `int` | `int` | `int` | `int&&` |
| `const int` | `int` | `const int` | `const int&&` |
| `int&` | `int` | `const int&` | `int&` |
| `const int&` | `int` | `const int&` | `const int&` |
| `int&&` | `int` | `const int&` | `int&&` |

**Important**: `T&&` deduces to an rvalue reference only when the argument is an rvalue; otherwise, it deduces to an lvalue reference (reference collapsing rules).

------

## Trailing Return Types

The trailing return type introduced in C++11 solves the problem of "return types depending on parameter types":

### Problem Scenario

```cpp
// ❌ 错误：T在返回类型时还未推导
template<typename T, typename U>
T add(T a, U b) {
    return a + b;  // 如果T是int，U是double，返回值截断
}

// ✅ 正确：使用尾随返回类型
template<typename T, typename U>
auto add(T a, U b) -> decltype(a + b) {
    return a + b;  // 返回decltype(a + b)的类型
}
```

### C++14 Simplification: Return Type Deduction

C++14 allows using `auto` directly as the return type, with the compiler deducing it automatically:

```cpp
template<typename T, typename U>
auto add(T a, U b) {
    return a + b;  // 推导为decltype(a + b)
}
```

### Advantages of Trailing Return Types

1. **Can access function parameters**:

    ```cpp
    template<typename T>
    auto deref(T iter) -> decltype(*iter) {
        return *iter;  // 返回解引用结果的类型
    }
    ```

2. **Better suited for complex expressions**:

    ```cpp
    template<typename T, typename U>
    auto multiply(T t, U u) -> decltype(t * u) {
        return t * u;
    }
    ```

3. **Cleaner syntax** (for complex return types):

    ```cpp
    // 传统写法（难读）
    std::map<int, std::string>::iterator func(int x);

    // 尾随返回类型（清晰）
    auto func(int x) -> std::map<int, std::string>::iterator;
    ```

### decltype(auto): Perfect Forwarding of Return Values

`decltype(auto)` introduced in C++14 combines the conciseness of `auto` with the precision of `decltype`:

```cpp
template<typename T>
struct Container {
    T data[100];

    // auto：返回T（拷贝）
    auto get1(std::size_t i) {
        return data[i];
    }

    // decltype(auto)：返回T&（引用）
    decltype(auto) get2(std::size_t i) {
        return (data[i]);  // 注意括号！
    }
};
```

**Key difference**: Parentheses cause `decltype` to return a reference type!

```cpp
int x = 42;
decltype(x) a = 10;      // int
decltype((x)) b = x;     // int&（括号让表达式变成引用）
```

------

## Template Overloading and Specialization

### Function Template Overloading

Function templates can be overloaded with regular functions or other templates:

```cpp
// 模板版本
template<typename T>
T max(T a, T b) {
    return a > b ? a : b;
}

// 针对const char*的特化（实际上是重载）
const char* max(const char* a, const char* b) {
    return std::strcmp(a, b) > 0 ? a : b;
}

// 使用
max(5, 10);           // 调用模板，T=int
max("hello", "world"); // 调用const char*重载
```

### Overload Resolution Order

The compiler selects in the following order:

1. **Exact match regular function**
2. **Exact match template function**
3. **Conversion required regular function**
4. **Conversion required template function**

```cpp
template<typename T>
void func(T t);

void func(int t);

func(42);  // 调用普通函数void func(int)，优先级更高
func(3.14); // 调用模板void func<double>
```

### The Truth About Function Template "Specialization"

**Important**: Function templates do not support true specialization; it can only be achieved through overloading!

```cpp
// 主模板
template<typename T>
void process(T t) {
    std::cout << "Generic: " << t << '\n';
}

// ❌ 这不是特化，是重载！
template<>
void process<int>(int t) {
    std::cout << "Int: " << t << '\n';
}

// ✅ 正确的"特化"方式：使用SFINAE或重载
void process(int t) {
    std::cout << "Int (overload): " << t << '\n';
}
```

**Recommendation**: Prefer overloading over specialization for function templates. Specialization is primarily intended for class templates.

------

## Universal References and Perfect Forwarding

### Universal Reference

When `T&&` appears in a template argument deduction context, it can be either an lvalue reference or an rvalue reference:

```cpp
template<typename T>
void wrapper(T&& arg) {  // 万能引用
    // ...
}

int x = 42;
wrapper(x);   // T推导为int&，参数类型为int&（左值引用）
wrapper(42);  // T推导为int，参数类型为int&&（右值引用）
```

**Identification rule**: It is a universal reference only when `T` is a deduced template parameter and the type is `T&&`.

```cpp
template<typename T>
class MyClass {
    void func1(T&& arg);      // ❌ 不是万能引用（T是类模板参数）
    void func2(auto&& arg);    // ✅ 是万能引用（C++20）
};

void func(auto&& arg);  // ✅ 是万能引用（C++20）
```

### Reference Collapsing Rules

When template argument deduction involves multiple layers of references, reference collapsing rules apply:

| T | arg declaration | Final type |
|---|---------|----------|
| `int` | `T&&` | `int&&` |
| `int&` | `T&&` | `int&` |
| `int&&` | `T&&` | `int&&` |

Simple memory aid: **The result is an rvalue reference only when both are rvalue references; otherwise, it is an lvalue reference.**

### std::forward: Preserving Value Categories

```cpp
template<typename T>
void wrapper(T&& arg) {
    target(std::forward<T>(arg));  // 完美转发
}

template<typename T>
void target(T&& arg);

int x = 42;
wrapper(x);   // 转发为左值
wrapper(42);  // 转发为右值
```

The implementation principle of `std::forward`:

```cpp
template<typename T>
T&& forward(std::remove_reference_t<T>& arg) {
    return static_cast<T&&>(arg);
}

// 当T=int&时：返回int&
// 当T=int时：返回int&&
```

------

## Hands-on: Implementing the min/max/clamp Function Family

Let's use what we've learned to implement a type-safe function family:

### Basic Version

```cpp
template<typename T>
constexpr T min(const T& a, const T& b) {
    return a < b ? a : b;
}

template<typename T>
constexpr T max(const T& a, const T& b) {
    return a > b ? a : b;
}

template<typename T>
constexpr T clamp(const T& value, const T& low, const T& high) {
    return (value < low) ? low : (value > high) ? high : value;
}
```

### Initializer List Version (Handling Multiple Arguments)

```cpp
template<typename T>
constexpr T min(std::initializer_list<T> list) {
    T result = *list.begin();
    for (auto item : list) {
        if (item < result) result = item;
    }
    return result;
}

// 使用
int m = min({5, 2, 8, 1, 9});  // 返回1
```

### Comparator Support Version (Similar to std:: Versions)

```cpp
template<typename T, typename Compare>
constexpr const T& min(const T& a, const T& b, Compare comp) {
    return comp(a, b) ? a : b;
}

// 使用
auto greater_min = min(5, 10, std::greater<>{});  // 返回10
```

### Embedded Optimized Version

In embedded systems, we might need to avoid branches to improve performance:

```cpp
template<typename T>
constexpr T min_branchless(const T& a, const T& b) {
    // 注意：这只对整数类型有效，且假设没有溢出
    return a < b ? a : b;  // 编译器通常能优化为cmov指令
}

// 或者使用位运算（仅无符号整数）
template<typename T>
constexpr T min_bitwise(const T& a, const T& b) {
    static_assert(std::is_unsigned_v<T>, "Only for unsigned types");
    return b ^ ((a ^ b) & -(a < b));
}

// 使用场景：信号处理、实时控制
uint16_t sample = min_bitwise(raw_sample, threshold);
```

### Type-Safe clamp (With Compile-Time Checks)

```cpp
template<typename T>
constexpr T clamp(const T& value, const T& low, const T& high) {
    static_assert(low <= high, "clamp: low must be <= high");
    return (value < low) ? low : (value > high) ? high : value;
}

// 编译期检查
constexpr auto result = clamp(5, 0, 10);  // OK
// constexpr auto error = clamp(5, 10, 0); // 编译错误！
```

### Complete Implementation (Comprehensive Version)

```cpp
template<typename T>
constexpr const T& clamp(const T& value, const T& low, const T& high) {
    static_assert(low <= high, "clamp: low must be <= high");
    return (value < low) ? low : (value > high) ? high : value;
}

// 版本2：支持自定义比较器
template<typename T, typename Compare>
constexpr const T& clamp(const T& value, const T& low, const T& high, Compare comp) {
    return comp(value, low) ? low : comp(high, value) ? high : value;
}

// 版本3：返回值而非引用（避免临时对象问题）
template<typename T>
constexpr T clamp_value(T value, T low, T high) {
    return (value < low) ? low : (value > high) ? high : value;
}
```

### Usage Examples

```cpp
// 传感器数值限制
int16_t sensor_value = read_sensor();
int16_t limited = clamp(sensor_value, -1000, 1000);

// PWM占空比限制
uint8_t duty = clamp<uint8_t>(calculated_duty, 0, 255);

// 浮点数限制
float frequency = clamp(target_freq, 1000.0f, 5000.0f);
```

------

## Embedded Tip: Avoiding Code Bloat

The main issue with templates in embedded development is code bloat. Every template instantiation generates a copy of the code, causing Flash usage to grow rapidly.

### Technique 1: Use a Common Base Class

```cpp
// ❌ 代码膨胀：每个类型都生成完整代码
template<typename T>
class Buffer {
    T data[100];
    void clear() { /* 100行代码 */ }
    void process() { /* 50行代码 */ }
};

// ✅ 优化：将类型无关部分提取到基类
class BufferBase {
protected:
    void clear_impl(void* data, std::size_t size);
    void process_impl(void* data, std::size_t size);
};

template<typename T>
class Buffer : private BufferBase {
    T data[100];
public:
    void clear() { clear_impl(data, sizeof(data)); }
    void process() { process_impl(data, sizeof(data)); }
};
```

### Technique 2: extern template Explicit Instantiation

C++11 allows declaring templates in header files and explicitly instantiating them in source files:

```cpp
// header.h
template<typename T>
void heavy_function(T t);

// header.tpp（实现）
template<typename T>
void heavy_function(T t) {
    /* 大量代码 */
}

// header.cpp（显式实例化）
extern template void heavy_function<int>;
extern template void heavy_function<float>;
extern template void heavy_function<double>;

template void heavy_function<int>;
template void heavy_function<float>;
template void heavy_function<double>;
```

This prevents other translation units from repeatedly instantiating these types.

### Technique 3: Type Erasure

For scenarios that do not require compile-time type information, use type erasure:

```cpp
// ❌ 每种传感器类型都生成一份代码
template<typename Sensor>
void process_sensor(Sensor& s) {
    s.read();
    s.calibrate();
    // ... 大量代码
}

// ✅ 使用接口+虚函数
class ISensor {
public:
    virtual void read() = 0;
    virtual void calibrate() = 0;
    // ...
};

void process_sensor(ISensor& s) {
    s.read();
    s.calibrate();
    // 只有一份代码
}
```

### Technique 4: Limit the Number of Template Specializations

```cpp
// ❌ 对每种配置都生成代码
template<typename T, std::size_t Size>
class Config;

// ✅ 只对常用配置特化
extern template class Config<uint8_t, 8>;
extern template class Config<uint8_t, 16>;
extern template class Config<uint16_t, 8>;
```

### Technique 5: Use `constexpr` + Type Selection

```cpp
// 只在编译期生成需要的版本
template<typename T, std::size_t Size>
class FixedBuffer {
    static_assert(Size <= 256, "Buffer too large");
    // ... 编译期确定大小
};

// 而不是运行时分支
void buffer(size_t size);  // 需要处理所有大小
```

### Code Bloat Detection Tools

- **Compiler output**: Check the generated assembly or object file sizes
- **map file**: Analyze the symbol table to find duplicate code
- **nm/size commands**: Compare binary sizes across different configurations

```bash
# 查看符号大小
nm --size-sort output.elf | head -20

# 查看段大小
size output.elf
```

------

## Common Pitfalls and Solutions

### Pitfall 1: Deduction Failure

```cpp
template<typename T>
void func(T a, T b);

func(42, 3.14);  // ❌ 错误：T无法同时匹配int和double

// 解决方案1：显式指定
func<double>(42, 3.14);

// 解决方案2：两个模板参数
template<typename T, typename U>
void func(T a, U b);

// 解决方案3：使用通用类型
template<typename T>
void func(T a, decltype(T{} + b) b);
```

### Pitfall 2: Returning a Reference to a Temporary Object

```cpp
template<typename T>
decltype(auto) get_first(const T& container) {
    return container[0];  // ❌ 返回临时对象的引用！
}

// ✅ 正确做法
template<typename T>
decltype(auto) get_first(T& container) {
    return container[0];  // 返回引用
}
```

### Pitfall 3: `auto` Return Type Losing References

```cpp
template<typename T>
auto get_element(T& container, std::size_t index) {
    return container[index];  // ❌ 返回拷贝而非引用
}

// ✅ 使用decltype(auto)
template<typename T>
decltype(auto) get_element(T& container, std::size_t index) {
    return container[index];  // ✅ 返回引用
}
```

### Pitfall 4: Confusing SFINAE with Hard Errors

```cpp
template<typename T>
auto func(T t) -> decltype(t.some_method()) {
    return t.some_method();
}

func(42);  // ❌ 硬错误：int没有some_method
          // ✅ SFINAE场景：只是移除候选函数
```

Correct SFINAE requires `std::enable_if` or C++17's `if constexpr`:

```cpp
template<typename T>
std::enable_if_t<std::is_integral_v<T>, T> func(T t) {
    return t + 1;
}

// 或C++17风格
template<typename T>
auto func(T t) {
    if constexpr (std::is_integral_v<T>) {
        return t + 1;
    } else {
        return t;
    }
}
```

------

## New Features in C++14/17/20

### C++14: Function Return Type Deduction

```cpp
// C++11需要尾随返回类型
template<typename T, typename U>
auto add(T t, U u) -> decltype(t + u) {
    return t + u;
}

// C++14可以直接用auto
template<typename T, typename U>
auto add(T t, U u) {
    return t + u;
}
```

### C++17: Class Template Argument Deduction (CTAD)

Although primarily used for class templates, this also affects function templates:

```cpp
template<typename T>
void process(std::vector<T> vec);

std::vector v{1, 2, 3};  // C++17 CTAD
process(v);  // T自动推导为int
```

### C++17: if constexpr

Simplifies conditional compilation inside templates:

```cpp
template<typename T>
void process(T t) {
    if constexpr (std::is_integral_v<T>) {
        // 整数分支
    } else if constexpr (std::is_floating_point_v<T>) {
        // 浮点分支
    } else {
        // 其他分支
    }
}
```

### C++20: Constraints and Abbreviated Function Templates

```cpp
// 传统写法
template<typename T>
void func(T t) {
    static_assert(std::is_integral_v<T>);
}

// C++20 Concepts
template<std::integral T>
void func(T t);  // 更清晰的约束

// 缩写函数模板
void func(std::integral auto t);  // 等价于上面
```

### C++20: Template Syntax Improvements

```cpp
// 类模板参数可以作为类型名
template<typename T>
struct Container {
    T value;
    Container(T value) : value(value) {}

    // C++20之前
    // Container<T> operator+(const Container<T>& other);

    // C++20：省略<Container>
    Container operator+(const Container& other);
};
```

------

## Summary

Function templates are the foundation of C++ generic programming:

| Feature | Description | Use Case |
|------|------|----------|
| Template argument deduction | Compiler automatically deduces the type of T | Simplifies function calls |
| Trailing return type | Return type depends on parameter types | Complex type calculations |
| Universal reference | T&& can be an lvalue or rvalue reference | Perfect forwarding |
| Perfect forwarding | std::forward preserves value categories | Forwarding functions |
| Template overloading | Coexists with regular functions | Type-specific handling |

**Practical recommendations**:

1. **Prefer the `auto` return type** (C++14+), unless you need precise control
2. **Use `decltype(auto)` when forwarding is needed** to preserve reference semantics
3. **Use `T&&` + `std::forward` for perfect forwarding**, do not use `T&&` directly
4. **Implement function specializations via overloading**; true specialization is meant for class templates
5. **Watch out for code bloat in embedded systems**, use explicit instantiation or type erasure to control it

**In the next chapter**, we will explore **class templates**, learn how to implement generic containers, understand the special rules for template member functions, and implement a fixed-capacity ring buffer.
