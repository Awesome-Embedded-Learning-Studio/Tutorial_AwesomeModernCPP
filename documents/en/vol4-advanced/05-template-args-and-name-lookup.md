# Modern C++ for Embedded Systems Tutorial — Template Argument Dependence and Name Lookup

Have you ever run into this puzzle: a piece of template code that looks perfectly correct, yet the compiler complains that it cannot find a certain type? Or when calling a function inside a template, the function clearly exists, but the compiler insists it is undefined?

Welcome to the most headache-inducing part of C++ templates — **name lookup**. Understanding two-phase lookup, dependent names, and ADL is a rite of passage for becoming a C++ template expert.

------

## Dependent Names

### What Are Dependent Names?

In template code, a **dependent name** is a name whose meaning depends on a template parameter. When the compiler parses a template, it cannot yet determine what these names represent — because the template parameters are still unknown.

```cpp
template<typename T>
void process(T container) {
    // value_type 是依赖名称——它的类型取决于 T
    typename T::value_type* ptr;

    // clear 是依赖名称——它是否存在取决于 T
    container.clear();
}
```

**Key point**: When parsing the template definition, the compiler does not yet know what type `T` is, so it cannot determine whether `T::value_type` is a type, a member variable, or a static function.

### Categories of Dependent Names

| Category | Description | Example |
|------|------|------|
| Dependent type names | Types that depend on template parameters | `T::value_type`, `T::iterator` |
| Dependent expression names | Expressions that depend on template parameters | `t.get()`, `x + y` |
| Non-dependent names | Names that do not depend on template parameters | `int`, `std::vector` |

```cpp
template<typename T>
void example(T t) {
    int x = 0;              // 非依赖类型
    std::vector<int> v;     // 非依赖类型

    typename T::type y;     // 依赖类型
    t.method();             // 依赖表达式（method是否存在取决于T）
    t + x;                  // 依赖表达式（operator+是否存在取决于T）
}
```

### The Necessity of the typename Keyword

For **dependent type names**, the C++ standard requires the `typename` keyword to explicitly tell the compiler "this is a type."

```cpp
template<typename T>
void func(T container) {
    // ❌ 错误：编译器不知道 T::value_type 是类型还是成员
    T::value_type* ptr;

    // ✅ 正确：用 typename 明确指出这是一个类型
    typename T::value_type* ptr;
}
```

**Why is this keyword needed?**

Because C++ grammar itself is ambiguous. Consider these two cases:

```cpp
// 情况1：value_type 是一个类型
struct MyContainer {
    using value_type = int;
};

// 情况2：value_type 是一个静态成员变量
struct AnotherContainer {
    static int value_type;
};

template<typename T>
void ambiguous() {
    T::value_type * p;
    // 这是声明一个指向 T::value_type 类型的指针 p？
    // 还是将 T::value_type（一个变量）与 p 相乘？
}
```

With `typename` added, the ambiguity is resolved:

```cpp
template<typename T>
void unambiguous() {
    typename T::value_type* p;  // 明确：声明指针
}
```

### Rules for Using typename

**Rule 1**: Whenever you use `T::XXX` in a template and want it to be a type, you must add `typename`

```cpp
template<typename T>
void process(T container) {
    // 获取迭代器类型
    typename T::iterator it = container.begin();

    // 获取值类型
    typename T::value_type val = *it;

    // 嵌套的情况
    typename T::template Inner<int>::type x;
}
```

**Rule 2**: `typename` is only needed in contexts that depend on template parameters

```cpp
template<typename T>
class MyClass {
    // 这里不需要 typename，因为不在模板函数体中
    using value_type = typename T::value_type;

    void method() {
        // 这里需要 typename
        typename T::some_type* ptr;
    }
};
```

**Rule 3**: Certain contexts do not need `typename` (the compiler knows it must be a type)

```cpp
template<typename T>
class Derived : public T::Base {  // 基类列表不需要 typename
    // ...
};

template<typename T>
void func() {
    // 类型转换不需要 typename
    typedef typename T::type Type;
    Type* p = static_cast<typename T::type*>(some_ptr);
}
```

### The Necessity of the template Keyword

Similarly, when accessing a dependent member template, we need to use the `template` keyword:

```cpp
template<typename T>
void process(T container) {
    // ❌ 错误：编译器不知道 < 是小于号还是模板参数列表开始
    auto ptr = container.get_ptr<int>();

    // ✅ 正确：用 template 明确指出这是成员模板
    auto ptr = container.template get_ptr<int>();
}
```

**Complete example**:

```cpp
template<typename T>
struct Allocator {
    template<typename U>
    struct rebind {
        using other = Allocator<U>;
    };
};

template<typename T>
void example(Allocator<T> alloc) {
    // 获取 rebind<int>::other
    // 需要 typename（因为 rebind<int>::other 是依赖类型）
    // 需要 template（因为 rebind 是成员模板）
    using ReboundAlloc = typename T::template rebind<int>::other;
}
```

**When to use the template keyword**:

| Scenario | Required? | Example |
|------|----------|------|
| Accessing a member type | Requires `typename` | `typename T::type` |
| Accessing a member template | Requires `template` | `obj.template foo<int>()` |
| Accessing an ordinary member | Not required | `obj.method()` |
| Accessing a static member | Not required | `T::static_var` |

------

## Two-Phase Lookup

### What Is Two-Phase Lookup?

When a C++ compiler processes template code, it performs name lookup in two phases:

| Phase | Timing | What Is Looked Up | Error Handling |
|------|------|----------|----------|
| **Phase 1** | When parsing the template definition | Non-dependent names | Immediate error |
| **Phase 2** | When instantiating the template | Dependent names | Error at the point of instantiation |

```cpp
template<typename T>
void func(T t) {
    // 阶段1检查：非依赖名称 foo 必须可见
    foo(t);

    // 阶段2检查：依赖名称 t.bar() 在实例化时才检查
    t.bar();
}

void foo(int);  // 必须在模板定义之前可见

// 使用
func(42);  // 阶段2：检查 int 是否有 bar() 方法
```

### Phase 1: Template Definition Time

During this phase, the compiler:

1. Checks that the template syntax is correct
2. Looks up all **non-dependent names**
3. Validates the use of `typename` and `template` keywords

```cpp
// 正确示例
template<typename T>
void example1(T t) {
    std::cout << t;  // OK：std::cout 是非依赖名称，必须可见
}

// 错误示例
template<typename T>
void example2(T t) {
    // 错误：print 未声明（即使 T 有 print 方法也不行）
    // print 是非依赖名称，必须在定义时可见
    print(t);

    // 如果想调用 T 的方法，必须用 t.print()
    t.print();  // OK：依赖名称，阶段2检查
}
```

### Phase 2: Template Instantiation Time

During this phase, the compiler:

1. Substitutes concrete template arguments for `T`
2. Looks up all **dependent names**
3. Checks that dependent operations are valid

```cpp
template<typename T>
void process(T t) {
    // 阶段1：operator<< 非依赖，必须有 std::operator<< 可见
    std::cout << t;

    // 阶段2：clear 是依赖名称，实例化时才检查
    t.clear();
}

struct A { void clear() {} };
struct B { };  // 没有 clear 方法

process(A{});  // OK：A 有 clear 方法
process(B{});  // 错误：B 没有 clear 方法（阶段2错误）
```

### Practical Example of Two-Phase Lookup

```cpp
#include <iostream>

// 函数必须在模板定义之前定义
void helper() {
    std::cout << "Helper called\n";
}

template<typename T>
void wrapper(T t) {
    helper();  // 非依赖：必须在定义时可见
    t.method();  // 依赖：实例化时检查
}

// 如果把 helper 放在这里，编译失败！
// void helper() { }

int main() {
    wrapper(42);  // 阶段2：检查 int 是否有 method()
}
```

**Common error**:

```cpp
// ❌ 错误示例
template<typename T>
void func(T t) {
    using namespace std;  // 在模板内部 using namespace
    cout << t;  // 依赖名称？非依赖名称？不明确！
}

// ✅ 正确做法
template<typename T>
void func(T t) {
    std::cout << t;  // 明确的非依赖名称
}

// 或者
template<typename T>
void func(T t) {
    using std::cout;  // 明确引入
    cout << t;
}
```

### Interaction Between Two-Phase Lookup and ADL

```cpp
namespace MyNS {
    struct A {};
    void foo(A);  // ADL 候选
}

template<typename T>
void call_foo(T t) {
    foo(t);  // 阶段1：检查 foo 是否存在
             // 阶段2：ADL 可能找到 MyNS::foo
}

int main() {
    MyNS::A a;
    call_foo(a);  // 通过 ADL 找到 MyNS::foo
}
```

**Key point**: Lookup for dependent names considers ADL, while lookup for non-dependent names does not.

### Considerations for Embedded Development

In embedded development, two-phase lookup can affect compilation time and error diagnostics:

```cpp
// 嵌入式场景：外设访问模板
template<typenamePeriph>
void init_peripheral() {
    // 阶段1检查：必须能看到这些
    using namespace MCU::Registers;

    // 阶段2检查：Periph 必须有这些成员
    Periph::enable_clock();
    Periph::reset();
}

// 使用
struct UART1 {
    static void enable_clock();
    static void reset();
};

init_peripheral<UART1>();  // 阶段2验证
```

------

## ADL (Argument-Dependent Lookup) in Detail

### What Is ADL?

**Argument-Dependent Lookup** (ADL), also known as **Koenig lookup**, is a special name lookup rule. It allows the compiler to search for functions in the namespaces of the argument types.

```cpp
namespace MyNS {
    struct A {};
    void do_something(A);  // 定义在 MyNS 中
}

int main() {
    MyNS::A a;
    do_something(a);  // 无需前缀！ADL 自动找到 MyNS::do_something
}
```

### Basic Rules of ADL

**Rule 1**: When making a function call, the compiler searches in the following places:

1. The current scope
2. Enclosing scopes (ordinary lookup)
3. The namespaces of the argument types (ADL)

```cpp
namespace NS1 {
    struct X {};
    void func(X);  // NS1::func
}

namespace NS2 {
    struct Y {};
    void func(Y);  // NS2::func
}

void test() {
    NS1::X x;
    NS2::Y y;
    func(x);  // ADL 找到 NS1::func
    func(y);  // ADL 找到 NS2::func
}
```

**Rule 2**: ADL only works for functions, not for class templates

```cpp
namespace MyNS {
    struct A {};
    template<typename T> void func(T);
    template<typename T> class MyClass;  // 类模板不支持 ADL
}

int main() {
    MyNS::A a;
    func(a);  // OK：ADL 工作于函数模板

    // MyClass<int> obj;  // 错误：必须写 MyNS::MyClass<int>
}
```

**Rule 3**: ADL ignores using directives

```cpp
namespace Lib {
    struct X {};
    void lib_func(X);
}

namespace App {
    using namespace Lib;  // using 指令

    void test() {
        X x;
        lib_func(x);  // ADL 找到 Lib::lib_func
    }
}
```

### ADL and Operator Overloading

The most important application of ADL is operator overloading:

```cpp
namespace Math {
    struct Vector {
        double x, y;
    };

    Vector operator+(Vector a, Vector b) {
        return {a.x + b.x, a.y + b.y};
    }
}

int main() {
    using Math::Vector;

    Vector v1{1, 2};
    Vector v2{3, 4};

    // v1 + v2 实际上调用 operator+(v1, v2)
    // 通过 ADL 找到 Math::operator+
    Vector v3 = v1 + v2;  // 无需 Math::operator+！
}
```

**This is why we can write `a + b` directly without needing to write `operator+(a, b)` or `SomeNS::operator+(a, b)`.**

### Special ADL Rules in Templates

In templates, ADL rules become more complex:

```cpp
namespace NS {
    struct A {};
    void foo(A);
}

template<typename T>
void call_foo(T t) {
    foo(t);  // 阶段1：foo 必须可见（至少声明）
             // 阶段2：通过 ADL 查找
}

// 在模板定义点，foo 必须至少被声明
void foo(...);  // 前向声明即可

int main() {
    NS::A a;
    call_foo(a);  // 通过 ADL 找到 NS::foo
}
```

### ADL and Friend Functions

```cpp
class MyClass {
    friend void friend_func(MyClass);  // 友元声明

private:
    int secret = 42;
};

// 定义（可以是外部的）
void friend_func(MyClass obj) {
    std::cout << obj.secret;  // 可以访问私有成员
}

int main() {
    MyClass obj;
    friend_func(obj);  // ADL 找到友元函数
}
```

### ADL Pitfalls and Considerations

**Pitfall 1: Name Hiding**

```cpp
namespace Lib {
    struct X {};
    void process(X);
}

namespace App {
    void process(void*);  // 不同签名

    void test() {
        Lib::X x;
        process(x);  // 错误：只找到 App::process，不匹配 Lib::X
                     // ADL 找到的 Lib::process 被隐藏
    }
}
```

**Solution**: Explicitly use the namespace

```cpp
void test() {
    Lib::X x;
    Lib::process(x);  // 明确调用
}
```

**Pitfall 2: Infinite Recursion**

```cpp
namespace NS {
    struct X;
    void to_string(X);

    struct X {
        // 错误：无限递归
        std::string str() {
            return to_string(*this);  // 调用自己
        }
    };
}
```

**Pitfall 3: Special Nature of the std Namespace**

Certain standard library functions do not participate in ADL:

```cpp
namespace std {
    struct string {};
    void some_func(string);  // 假设的函数
}

int main() {
    std::string s;
    some_func(s);  // 不一定通过 ADL 找到
                   // 添加到 std 是未定义行为！
}
```

**Important**: Never add anything to the `std` namespace!

### ADL in Practice: Custom Iterators

```cpp
namespace MyContainer {
    template<typename T>
    class Container {
    public:
        class iterator {
            T* ptr;
        public:
            iterator(T* p) : ptr(p) {}
            iterator& operator++() { ++ptr; return *this; }
            T& operator*() { return *ptr; }
        };

        iterator begin() { return iterator(data_); }
        iterator end() { return iterator(data_ + size_); }

    private:
        T data_[100];
        std::size_t size_ = 0;
    };

    // 自定义 begin/end 以支持 ADL
    template<typename T>
    typename Container<T>::iterator begin(Container<T>& c) {
        return c.begin();
    }

    template<typename T>
    typename Container<T>::iterator end(Container<T>& c) {
        return c.end();
    }
}

int main() {
    using namespace MyContainer;
    Container<int> c;

    // 通过 ADL 找到 begin/end
    for (auto& x : c) {
        // ...
    }
}
```

### ADL Summary Table

| Scenario | Does ADL Work? | Example |
|------|-------------|------|
| Ordinary functions | Yes | `func(obj)` finds `NS::func` |
| Operators | Yes | `a + b` finds `NS::operator+` |
| Class templates | No | `MyClass<T>` requires a fully qualified path |
| Member functions | N/A | `obj.method()` does not involve ADL |
| Namespace aliases | Yes | Can still be triggered through an aliased type |

------

## In Practice: Writing Correct Generic Iterator Code

Let's put our knowledge to use and write a correct, robust generic iterator.

### Requirements Analysis

Our iterator needs to:

1. Support arbitrary container types
2. Correctly handle dependent type names
3. Support ADL for ease of use
4. Provide type-safe access

### Basic Implementation

```cpp
template<typename Container>
class GenericIterator {
public:
    // 依赖类型：必须使用 typename
    using value_type = typename Container::value_type;
    using reference = typename Container::reference;
    using pointer = typename Container::pointer;
    using difference_type = typename Container::difference_type;

private:
    Container& container_;
    std::size_t index_;

public:
    explicit GenericIterator(Container& c, std::size_t index = 0)
        : container_(c), index_(index) {}

    // 解引用
    reference operator*() {
        return container_[index_];
    }

    // 箭头运算符
    pointer operator->() {
        return &container_[index_];
    }

    // 前置递增
    GenericIterator& operator++() {
        ++index_;
        return *this;
    }

    // 后置递增
    GenericIterator operator++(int) {
        auto temp = *this;
        ++index_;
        return temp;
    }

    // 比较
    bool operator==(const GenericIterator& other) const {
        return index_ == other.index_;
    }

    bool operator!=(const GenericIterator& other) const {
        return !(*this == other);
    }
};
```

### Enhanced Version: Supporting Containers Without Standard Typedefs

Some containers might not provide standard typedefs like `value_type`, so we need to use SFINAE (Substitution Failure Is Not An Error):

```cpp
// 类型萃取辅助
template<typename T, typename = void>
struct container_traits {
    // 默认情况下，假设有标准 typedef
    using value_type = typename T::value_type;
};

// 针对 C 数组的特化
template<typename T, std::size_t N>
struct container_traits<T[N]> {
    using value_type = T;
    using reference = T&;
    using pointer = T*;
};

template<typename Container>
class AdvancedIterator {
    // 使用萃取获取类型，提供合理的默认值
    using traits = container_traits<Container>;
    using value_type = typename traits::value_type;

private:
    Container& container_;
    std::size_t index_;

public:
    explicit AdvancedIterator(Container& c, std::size_t index = 0)
        : container_(c), index_(index) {}

    // 类型安全的访问
    value_type& operator*() {
        return container_[index_];
    }

    value_type* operator->() {
        return &container_[index_];
    }

    // ... 其他运算符
};
```

### Complete Generic Access Function

```cpp
// 获取容器大小（支持数组和容器）
template<typename T, std::size_t N>
constexpr std::size_t size(T (&)[N]) noexcept {
    return N;
}

template<typename Container>
constexpr auto size(const Container& c) noexcept -> decltype(c.size()) {
    return c.size();
}

// 获取迭代器
template<typename Container>
auto begin(Container& c) -> decltype(c.begin()) {
    return c.begin();
}

template<typename T, std::size_t N>
T* begin(T (&arr)[N]) {
    return arr;
}

template<typename Container>
auto end(Container& c) -> decltype(c.end()) {
    return c.end();
}

template<typename T, std::size_t N>
T* end(T (&arr)[N]) {
    return arr + N;
}
```

### Embedded Application: Generic Register Access Iterator

```cpp
namespace MCU {
    // 寄存器访问基类
    template<typename AddrType, typename RegType>
    class RegisterIterator {
        volatile RegType* base_;
        std::size_t offset_;

    public:
        using value_type = RegType;
        using reference = RegType&;
        using pointer = volatile RegType*;

        RegisterIterator(volatile RegType* base, std::size_t offset)
            : base_(base), offset_(offset) {}

        reference operator*() const {
            return const_cast<reference>(base_[offset_]);
        }

        pointer operator->() const {
            return base_ + offset_;
        }

        RegisterIterator& operator++() {
            ++offset_;
            return *this;
        }

        RegisterIterator operator++(int) {
            auto temp = *this;
            ++offset_;
            return temp;
        }

        bool operator==(const RegisterIterator& other) const {
            return offset_ == other.offset_;
        }

        bool operator!=(const RegisterIterator& other) const {
            return !(*this == other);
        }
    };

    // GPIO 端口访问
    template<std::size_t PortBase>
    class GPIOPort {
    public:
        using iterator = RegisterIterator<std::uint32_t, std::uint32_t>;

        static constexpr std::size_t register_count = 16;

        static volatile std::uint32_t* base() {
            return reinterpret_cast<volatile std::uint32_t*>(PortBase);
        }

        iterator begin() {
            return iterator(base(), 0);
        }

        iterator end() {
            return iterator(base(), register_count);
        }
    };
}

// 使用示例
void clear_all_gpio() {
    constexpr std::size_t GPIOA_BASE = 0x40020000;
    MCU::GPIOPort<GPIOA_BASE> port;

    for (auto& reg : port) {
        reg = 0;  // 清零所有寄存器
    }
}
```

### Constrained Iterators (C++20 Concepts)

```cpp
template<typename T>
concept Iterable = requires(T t) {
    typename T::value_type;
    { t.begin() } -> std::same_as<typename T::iterator>;
    { t.end() } -> std::same_as<typename T::iterator>;
};

template<Iterable Container>
class SafeIterator {
    using value_type = typename Container::value_type;
    // ...
};

// 使用
SafeIterator<std::vector<int>> it1;  // OK
// SafeIterator<int> it2;  // 编译错误
```

### Complete Example: Generic Print Function

```cpp
#include <iostream>
#include <vector>
#include <array>

// 检查是否有 value_type
template<typename T, typename = void>
struct has_value_type : std::false_type {};

template<typename T>
struct has_value_type<T, std::void_t<typename T::value_type>>
    : std::true_type {};

// 泛型打印函数
template<typename Container>
auto print_container(const Container& c)
    -> std::enable_if_t<has_value_type<Container>::value, void>
{
    std::cout << "[";
    bool first = true;
    for (const auto& item : c) {
        if (!first) std::cout << ", ";
        std::cout << item;
        first = false;
    }
    std::cout << "]\n";
}

// C数组特化
template<typename T, std::size_t N>
void print_container(const T (&arr)[N]) {
    std::cout << "[";
    for (std::size_t i = 0; i < N; ++i) {
        if (i > 0) std::cout << ", ";
        std::cout << arr[i];
    }
    std::cout << "]\n";
}

// 使用
int main() {
    std::vector<int> vec = {1, 2, 3, 4, 5};
    std::array<double, 3> arr = {1.1, 2.2, 3.3};
    int c_arr[] = {10, 20, 30};

    print_container(vec);    // [1, 2, 3, 4, 5]
    print_container(arr);    // [1.1, 2.2, 3.3]
    print_container(c_arr);  // [10, 20, 30]
}
```

::: details Complete runnable example: generic iterator toolkit

```cpp
#include <iostream>
#include <vector>
#include <array>
#include <type_traits>

namespace generic {

// ============ 类型萃取 ============
template<typename T, typename = void>
struct container_traits {
    using value_type = typename T::value_type;
    using size_type = typename T::size_type;
    using reference = typename T::reference;
    using const_reference = typename T::const_reference;
};

// C数组特化
template<typename T, std::size_t N>
struct container_traits<T[N]> {
    using value_type = T;
    using size_type = std::size_t;
    using reference = T&;
    using const_reference = const T&;
};

// ============ 容器访问函数 ============

// size 函数
template<typename T, std::size_t N>
constexpr std::size_t size(T (&)[N]) noexcept {
    return N;
}

template<typename Container>
constexpr auto size(const Container& c) noexcept -> decltype(c.size()) {
    return c.size();
}

// begin 函数（ADL 友好）
template<typename Container>
auto begin(Container& c) -> decltype(c.begin()) {
    return c.begin();
}

template<typename Container>
auto begin(const Container& c) -> decltype(c.begin()) {
    return c.begin();
}

template<typename T, std::size_t N>
T* begin(T (&arr)[N]) {
    return arr;
}

template<typename T, std::size_t N>
const T* begin(const T (&arr)[N]) {
    return arr;
}

// end 函数（ADL 友好）
template<typename Container>
auto end(Container& c) -> decltype(c.end()) {
    return c.end();
}

template<typename Container>
auto end(const Container& c) -> decltype(c.end()) {
    return c.end();
}

template<typename T, std::size_t N>
T* end(T (&arr)[N]) {
    return arr + N;
}

template<typename T, std::size_t N>
const T* end(const T (&arr)[N]) {
    return arr + N;
}

// ============ 泛型遍历 ============

template<typename Container, typename Func>
void for_each(Container&& c, Func&& func) {
    using std::begin;
    using std::end;
    auto first = begin(c);
    auto last = end(c);
    for (; first != last; ++first) {
        func(*first);
    }
}

// ============ 累加器 ============

template<typename Container>
auto sum(const Container& c) -> typename container_traits<Container>::value_type {
    using value_type = typename container_traits<Container>::value_type;
    value_type result{};
    for_each(c, [&result](const value_type& v) { result += v; });
    return result;
}

// ============ 打印器 ============

namespace detail {
    struct print_fn {
        template<typename T>
        void operator()(const T& v) const {
            std::cout << v;
        }
    };
}

template<typename Container>
void print(const Container& c) {
    std::cout << "[";
    bool first = true;
    for_each(c, [&](const auto& v) {
        if (!first) std::cout << ", ";
        detail::print_fn{}(v);
        first = false;
    });
    std::cout << "]";
}

} // namespace generic

// ============ 使用示例 ============
int main() {
    using namespace generic;

    std::vector<int> vec = {1, 2, 3, 4, 5};
    std::array<double, 3> arr = {1.1, 2.2, 3.3};
    int c_arr[] = {10, 20, 30};

    std::cout << "Vector: ";
    print(vec);
    std::cout << ", size=" << size(vec) << ", sum=" << sum(vec) << "\n";

    std::cout << "Array: ";
    print(arr);
    std::cout << ", size=" << size(arr) << ", sum=" << sum(arr) << "\n";

    std::cout << "C-array: ";
    print(c_arr);
    std::cout << ", size=" << size(c_arr) << ", sum=" << sum(c_arr) << "\n";

    // 使用 for_each
    std::cout << "Squared: ";
    for_each(vec, [](int x) { std::cout << x * x << " "; });
    std::cout << "\n";

    return 0;
}

```

:::

------

## Common Pitfalls: Why Doesn't `t.clear()` Work Sometimes?

### Pitfall 1: Dependent Names and Two-Phase Lookup

```cpp

template<typename T>
void process(T t) {
    // 阶段1：编译器检查 clear 是否作为非依赖名称存在
    // 阶段2：编译器检查 T 是否有 clear 方法

    t.clear();  // 如果 T 没有 clear()，阶段2报错
}

struct HasClear {
    void clear() { std::cout << "Cleared!\n"; }
};

struct NoClear {};  // 没有 clear 方法

int main() {
    process(HasClear{});  // OK
    process(NoClear{});   // 错误：NoClear 没有 clear 成员
}
```

**Solution**: Use SFINAE (Substitution Failure Is Not An Error) or Concepts constraints

```cpp
// C++17 SFINAE
template<typename T>
auto process_safe(T t) -> decltype(t.clear(), void()) {
    t.clear();
}

// C++20 Concepts
template<typename T>
concept Clearable = requires(T t) { t.clear(); };

template<Clearable T>
void process_concept(T t) {
    t.clear();
}
```

### Pitfall 2: ADL and Name Hiding

```cpp
namespace Lib {
    struct Data {};
    void process(Data d) { std::cout << "Lib::process\n"; }
}

namespace App {
    void process(void*) { std::cout << "App::process\n"; }

    void test() {
        Lib::Data d;
        process(d);  // 错误！只找到 App::process
                     // Lib::process 被"隐藏"了
    }
}
```

**Why does this happen?**

Because ordinary name lookup finds `App::process` in the current scope, and even if it does not match, ADL will not continue searching.

**Solution 1**: Explicitly call the namespace

```cpp
void test() {
    Lib::Data d;
    Lib::process(d);  // 明确指定
}
```

**Solution 2**: Introduce it into the current scope

```cpp
namespace App {
    using Lib::process;  // 引入
    void process(void*) { std::cout << "App::process\n"; }

    void test() {
        Lib::Data d;
        process(d);  // 现在可以找到 Lib::process
    }
}
```

### Pitfall 3: typename in the Wrong Position

```cpp
template<typename T>
struct MyClass {
    // 错误：这里不需要 typename（不在函数体中）
    using type = typename T::value_type;

    void method() {
        // 正确：这里需要 typename
        typename T::some_type* ptr;
    }
};

// 更复杂的错误
template<typename T>
void func() {
    // 错误：嵌套的成员模板需要 template 关键字
    // T::template Inner<int>::type* ptr;

    // 正确写法
    typename T::template Inner<int>::type* ptr;
}
```

### Pitfall 4: operator+ and ADL

```cpp
namespace MyMath {
    struct Vector { double x, y; };
    Vector operator+(Vector a, Vector b) {
        return {a.x + b.x, a.y + b.y};
    }
}

template<typename T>
void add_and_print(T a, T b) {
    // 这里依赖 ADL 找到 operator+
    auto c = a + b;  // OK：通过 ADL 找到 MyMath::operator+
    std::cout << c.x << ", " << c.y << "\n";
}

int main() {
    MyMath::Vector v1{1, 2};
    MyMath::Vector v2{3, 4};
    add_and_print(v1, v2);  // 正常工作
}
```

But if we write it like this:

```cpp
template<typename T>
void broken_add(T a, T b) {
    using std::operator+;  // 错误做法！
    auto c = a + b;        // 可能找不到 MyMath::operator+
}
```

### Pitfall 5: Friend Functions and ADL

```cpp
class SecretHolder {
    int secret = 42;
    friend void reveal(const SecretHolder&);  // 友元声明
};

// 必须在命名空间作用域定义
void reveal(const SecretHolder& s) {
    std::cout << s.secret << "\n";  // OK：友元可以访问
}

int main() {
    SecretHolder s;
    reveal(s);  // 通过 ADL 找到友元函数
}
```

But if we place the definition inside the class:

```cpp
class SecretHolder {
    int secret = 42;
    friend void reveal(const SecretHolder& s) {
        std::cout << s.secret << "\n";  // 不是成员函数！
    }
};

int main() {
    SecretHolder s;
    reveal(s);  // 错误！类内定义的友元不参与普通名字查找
}
```

**Solution**: A friend defined inside a class can only be found via ADL, but an in-class definition is not visible at the call site. We need a forward declaration or an external definition.

### Pitfall 6: Members of Template Base Classes Are Not Visible

```cpp
template<typename T>
struct Base {
    void method() { std::cout << "Base::method\n"; }
    using value_type = T;
};

template<typename T>
struct Derived : Base<T> {
    void use_method() {
        // method();  // 错误！编译器不查找依赖基类

        // 解决方案1：使用 this-> 令其成为依赖名称
        this->method();

        // 解决方案2：使用 using 声明
        using Base<T>::method;
        method();

        // 解决方案3：完全限定名
        Base<T>::method();
    }

    void use_type() {
        // value_type x;  // 错误！

        typename Base<T>::value_type x;  // 正确
    }
};
```

**Explanation**: Because `Base<T>` is a dependent base class (it depends on a template parameter), the compiler will not look up its members during phase 1. We must use `this->` or a fully qualified name to make it a dependent name.

### Pitfall Reference Table

| Pitfall | Cause | Solution |
|------|------|----------|
| `t.clear()` does not work | `T` has no `clear` method | SFINAE/Concepts constraints |
| Cannot find a function with the same name | Name hiding | Explicit namespace or `using` |
| `typename` in the wrong position | Non-function-body types do not need it | Only use it at dependent types |
| Accessing member templates | Missing the `template` keyword | Use `obj.template foo<T>()` |
| Base class members are not visible | Dependent base class lookup rules | Use `this->` or `using` |
| Friend functions are not visible | ADL rule restrictions | Ensure definition is at namespace scope |

### Debugging Template Name Lookup Issues

When encountering name lookup issues, follow these steps to troubleshoot:

1. **Check if it is a dependent name**: Does the name depend on a template parameter?
2. **Check if `typename` is needed**: Is it a dependent type?
3. **Check two-phase lookup**: Are non-dependent names visible at the point of definition?
4. **Check ADL**: Is the function in the namespace of the argument types?
5. **Check base classes**: Is it a member of a dependent base class?
6. **Check constraints**: Are SFINAE (Substitution Failure Is Not An Error) or Concepts needed?

```cpp
// 调试技巧：static_assert 提供清晰错误
template<typename T>
void debug_process(T t) {
    using value_type = typename T::value_type;  // 如果失败，给出明确错误
    static_assert(std::is_same_v<value_type, int>, "Only int containers supported");
    // ...
}
```

------

## Summary

Template argument dependence and name lookup are core mechanisms of C++ templates. Understanding them is crucial for writing correct template code.

### Key Concepts Review

| Concept | Core Idea | Use Case |
|------|----------|----------|
| **Dependent names** | Names that depend on template parameters | Accessing `T::XXX` in a template |
| **typename** | Indicates a dependent type | `typename T::value_type` |
| **template** | Indicates a dependent member template | `obj.template foo<T>()` |
| **Two-phase lookup** | Look up non-dependent names at definition, dependent names at instantiation | Understanding when compilation errors occur |
| **ADL** | Looks up functions in the namespaces of argument types | Operator overloading, swap, etc. |

### Practical Advice

1. **Always use `typename` for dependent types**

   ```cpp
   typename T::value_type* ptr;
   ```

2. **Use the `template` keyword for dependent member templates**

   ```cpp
   obj.template method<int>();
   ```

3. **Understand two-phase lookup and organize code accordingly**

   ```cpp
   // 非依赖名称：在模板定义前声明
   void helper();

   template<typename T>
   void func(T t) {
       helper();      // 非依赖：定义时检查
       t.method();    // 依赖：实例化时检查
   }
   ```

4. **Leverage ADL to simplify function calls**

   ```cpp
   // 用户可以写 swap(a, b) 而非 std::swap(a, b)
   using std::swap;
   swap(a, b);
   ```

5. **Use Concepts to provide clear constraints**

   ```cpp
   template<typename T>
   concept Clearable = requires(T t) { t.clear(); };

   template<Clearable T>
   void process(T t) { t.clear(); }
   ```

6. **Be aware of the special rules for dependent base class members**

   ```cpp
   template<typename T>
   struct Derived : Base<T> {
       void foo() {
           this->method();  // 使用 this-> 令其成为依赖名称
       }
   };
   ```

### C++ Standard Evolution

| Standard | New Feature | What It Simplified |
|------|--------|-----------|
| C++11 | `decltype` | More precise type deduction |
| C++14 | `decltype(auto)` | Automatic deduction of reference types |
| C++17 | `std::void_t` | Simpler SFINAE (Substitution Failure Is Not An Error) |
| C++17 | `if constexpr` | Compile-time branching |
| C++20 | Concepts | Clear template constraints |
| C++20 | `requires` | Better constraint expression |

**In the next chapter**, we will explore **variadic templates**, learning how to write template functions that accept an arbitrary number of arguments, and implement a type-safe embedded event system.
