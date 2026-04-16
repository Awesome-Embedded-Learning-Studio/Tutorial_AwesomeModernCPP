---
title: "模板参数依赖与名字查找"
description: "深入理解C++模板的两阶段查找与依赖名称处理"
chapter: 12
order: 5
tags:
  - cpp-modern
  - host
  - intermediate
difficulty: intermediate
reading_time_minutes: 40
prerequisites:
  - "Chapter 12: 模板入门概述"
  - "Chapter 12: 函数模板详解"
cpp_standard: [11, 14, 17, 20]
platform: host
---

# 嵌入式现代C++教程——模板参数依赖与名字查找

你有没有遇到过这样的困惑：一段看起来完全正确的模板代码，编译器却报错说找不到某个类型？或者在模板内部调用某个函数时，明明这个函数存在，编译器却说它未定义？

欢迎来到C++模板最令人头疼的部分——**名字查找**。理解两阶段查找、依赖名称、ADL这些概念，是成为C++模板高手的必经之路。

------

## 依赖名称（Dependent Names）

### 什么是依赖名称？

在模板代码中，**依赖名称**是指其含义依赖于模板参数的名称。当编译器解析模板时，它还不能确定这些名称代表什么——因为模板参数还未知。

```cpp
template<typename T>
void process(T container) {
    // value_type 是依赖名称——它的类型取决于 T
    typename T::value_type* ptr;

    // clear 是依赖名称——它是否存在取决于 T
    container.clear();
}
```

**关键点**：编译器在解析模板定义时，还不知道`T`是什么类型，所以它无法确定`T::value_type`是一个类型、一个成员变量，还是一个静态函数。

### 依赖名称的分类

| 类别 | 说明 | 示例 |
|------|------|------|
| 依赖类型名称 | 依赖于模板参数的类型 | `T::value_type`、`T::iterator` |
| 依赖表达式名称 | 依赖于模板参数的表达式 | `t.get()`、`x + y` |
| 非依赖名称 | 不依赖模板参数的名称 | `int`、`std::vector` |

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

### typename 关键字的必要性

对于**依赖类型名称**，C++标准要求使用`typename`关键字明确告诉编译器"这是一个类型"。

```cpp
template<typename T>
void func(T container) {
    // ❌ 错误：编译器不知道 T::value_type 是类型还是成员
    T::value_type* ptr;

    // ✅ 正确：用 typename 明确指出这是一个类型
    typename T::value_type* ptr;
}
```

**为什么需要这个关键字？**

因为C++语法本身有歧义。考虑下面两种情况：

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

加上`typename`后，歧义就消除了：

```cpp
template<typename T>
void unambiguous() {
    typename T::value_type* p;  // 明确：声明指针
}
```

### typename 的使用规则

**规则1**：只要在模板中使用`T::XXX`并且希望它是一个类型，就必须加`typename`

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

**规则2**：只有在模板参数依赖的上下文中才需要`typename`

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

**规则3**：某些上下文不需要`typename`（编译器知道必须是类型）

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

### template 关键字的必要性

类似地，当访问依赖的成员模板时，需要使用`template`关键字：

```cpp
template<typename T>
void process(T container) {
    // ❌ 错误：编译器不知道 < 是小于号还是模板参数列表开始
    auto ptr = container.get_ptr<int>();

    // ✅ 正确：用 template 明确指出这是成员模板
    auto ptr = container.template get_ptr<int>();
}
```

**完整的示例**：

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

**何时使用 template 关键字**：

| 情况 | 是否需要 | 示例 |
|------|----------|------|
| 访问成员类型 | 需要`typename` | `typename T::type` |
| 访问成员模板 | 需要`template` | `obj.template foo<int>()` |
| 访问普通成员 | 不需要 | `obj.method()` |
| 访问静态成员 | 不需要 | `T::static_var` |

------

## 两阶段查找（Two-Phase Lookup）

### 什么是两阶段查找？

C++编译器处理模板代码时，分为两个阶段进行名字查找：

| 阶段 | 时机 | 查找内容 | 错误处理 |
|------|------|----------|----------|
| **阶段1** | 解析模板定义时 | 非依赖名称 | 立即报错 |
| **阶段2** | 实例化模板时 | 依赖名称 | 在实例化点报错 |

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

### 阶段1：模板定义时

在这个阶段，编译器：

1. 检查模板语法是否正确
2. 查找所有**非依赖名称**
3. 验证`typename`和`template`关键字的使用

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

### 阶段2：模板实例化时

在这个阶段，编译器：

1. 用具体的模板参数替换`T`
2. 查找所有**依赖名称**
3. 检查依赖的操作是否有效

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

### 两阶段查找实战示例

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

**常见错误**：

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

### 两阶段查找与ADL的交互

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

**关键点**：依赖名称的查找会考虑ADL，而非依赖名称不会。

### 嵌入式开发中的注意事项

在嵌入式开发中，两阶段查找可能影响编译时间和错误诊断：

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

## ADL（Argument-Dependent Lookup）详解

### 什么是ADL？

**参数依赖查找**（Argument-Dependent Lookup，ADL），也叫**Koenig查找**，是一种特殊的名字查找规则。它允许编译器在参数类型的命名空间中查找函数。

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

### ADL的基本规则

**规则1**：函数调用时，编译器会在以下位置查找：

1. 当前作用域
2. 外层作用域（普通查找）
3. 参数类型的命名空间（ADL）

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

**规则2**：ADL 只对函数有效，对类模板无效

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

**规则3**：ADL 忽略 using 指令

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

### ADL与运算符重载

ADL最重要的应用是运算符重载：

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

**这就是为什么我们可以直接写`a + b`而不需要写`operator+(a, b)`或`SomeNS::operator+(a, b)`。**

### ADL在模板中的特殊规则

在模板中，ADL规则变得更加复杂：

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

### ADL与友元函数

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

### ADL陷阱与注意事项

**陷阱1：名字隐藏**

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

**解决方案**：显式使用命名空间

```cpp
void test() {
    Lib::X x;
    Lib::process(x);  // 明确调用
}
```

**陷阱2：无限递归**

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

**陷阱3：std 命名空间的特殊性**

标准库的某些函数不参与ADL：

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

**重要**：永远不要向`std`命名空间添加内容！

### ADL实战：自定义迭代器

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

### ADL总结表

| 场景 | ADL是否工作 | 示例 |
|------|-------------|------|
| 普通函数 | 是 | `func(obj)` 找到 `NS::func` |
| 运算符 | 是 | `a + b` 找到 `NS::operator+` |
| 类模板 | 否 | `MyClass<T>` 需要完整路径 |
| 成员函数 | N/A | `obj.method()` 不涉及ADL |
| 命名空间别名 | 是 | 通过别名类型仍能触发 |

------

## 实战：正确编写泛型迭代器代码

让我们综合运用所学知识，编写一个正确、健壮的泛型迭代器。

### 需求分析

我们的迭代器需要：

1. 支持任意容器类型
2. 正确处理依赖类型名称
3. 支持ADL以便于使用
4. 提供类型安全的访问

### 基础实现

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

### 增强版：支持没有标准 typedef 的容器

某些容器可能不提供`value_type`等标准typedef，我们需要使用SFINAE：

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

### 完整的泛型访问函数

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

### 嵌入式应用：通用的寄存器访问迭代器

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

### 带约束的迭代器（C++20 Concepts）

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

### 完整示例：泛型打印函数

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

<details>
<summary>完整可运行示例：泛型迭代器工具包</summary>

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

</details>

------

## 常见陷阱：为什么 `t.clear()` 有时不工作？

### 陷阱1：依赖名称与两阶段查找

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

**解决方案**：使用SFINAE或Concepts约束

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

### 陷阱2：ADL与名字隐藏

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

**为什么会这样？**

因为普通名字查找在当前作用域找到了`App::process`，即使它不匹配，ADL也不会继续查找。

**解决方案1**：显式调用命名空间

```cpp
void test() {
    Lib::Data d;
    Lib::process(d);  // 明确指定
}
```

**解决方案2**：引入到当前作用域

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

### 陷阱3：typename 位置错误

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

### 陷阱4：operator+ 与 ADL

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

但如果这样写：

```cpp
template<typename T>
void broken_add(T a, T b) {
    using std::operator+;  // 错误做法！
    auto c = a + b;        // 可能找不到 MyMath::operator+
}
```

### 陷阱5：友元函数与ADL

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

但如果把定义放在类内：

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

**解决方案**：类内定义的友元只能通过ADL找到，但类内定义不会在调用点可见。需要前向声明或外部定义。

### 陷阱6：模板基类的成员不可见

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

**解释**：因为`Base<T>`是依赖基类（依赖模板参数），编译器在阶段1不会查找它的成员。必须使用`this->`或完全限定名令其成为依赖名称。

### 陷阱对照表

| 陷阱 | 原因 | 解决方案 |
|------|------|----------|
| `t.clear()` 不工作 | `T` 没有 `clear` 方法 | SFINAE/Concepts 约束 |
| 找不到同名函数 | 名字隐藏 | 显式命名空间或 `using` |
| `typename` 位置错误 | 非函数体的类型不需要 | 只在依赖类型处使用 |
| 成员模板访问 | 缺少 `template` 关键字 | 使用 `obj.template foo<T>()` |
| 基类成员不可见 | 依赖基类查找规则 | 使用 `this->` 或 `using` |
| 友元函数不可见 | ADL 规则限制 | 确保在命名空间作用域定义 |

### 调试模板名字查找问题

当遇到名字查找问题时，按以下步骤排查：

1. **检查是否是依赖名称**：名称是否依赖模板参数？
2. **检查是否需要 `typename`**：是否是依赖类型？
3. **检查两阶段查找**：非依赖名称在定义时是否可见？
4. **检查ADL**：函数是否在参数类型的命名空间中？
5. **检查基类**：是否是依赖基类的成员？
6. **检查约束**：是否需要 SFINAE 或 Concepts？

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

## 小结

模板参数依赖与名字查找是C++模板的核心机制，理解它们对于编写正确的模板代码至关重要。

### 关键概念回顾

| 概念 | 核心要点 | 使用场景 |
|------|----------|----------|
| **依赖名称** | 依赖模板参数的名称 | 在模板中访问`T::XXX` |
| **typename** | 标明依赖类型 | `typename T::value_type` |
| **template** | 标明依赖成员模板 | `obj.template foo<T>()` |
| **两阶段查找** | 定义时查非依赖，实例化时查依赖 | 理解编译错误时机 |
| **ADL** | 在参数类型命名空间查找函数 | 运算符重载、swap 等 |

### 实战建议

1. **始终对依赖类型使用 `typename`**

   ```cpp
   typename T::value_type* ptr;
   ```

2. **对依赖成员模板使用 `template` 关键字**

   ```cpp
   obj.template method<int>();
   ```

3. **理解两阶段查找，合理安排代码**

   ```cpp
   // 非依赖名称：在模板定义前声明
   void helper();

   template<typename T>
   void func(T t) {
       helper();      // 非依赖：定义时检查
       t.method();    // 依赖：实例化时检查
   }
   ```

4. **利用 ADL 简化函数调用**

   ```cpp
   // 用户可以写 swap(a, b) 而非 std::swap(a, b)
   using std::swap;
   swap(a, b);
   ```

5. **使用 Concepts 提供清晰约束**

   ```cpp
   template<typename T>
   concept Clearable = requires(T t) { t.clear(); };

   template<Clearable T>
   void process(T t) { t.clear(); }
   ```

6. **注意依赖基类成员的特殊规则**

   ```cpp
   template<typename T>
   struct Derived : Base<T> {
       void foo() {
           this->method();  // 使用 this-> 令其成为依赖名称
       }
   };
   ```

### C++标准演进

| 标准 | 新特性 | 简化了什么 |
|------|--------|-----------|
| C++11 | `decltype` | 更精确的类型推导 |
| C++14 | `decltype(auto)` | 自动推导引用类型 |
| C++17 | `std::void_t` | 更简单的 SFINAE |
| C++17 | `if constexpr` | 编译期分支 |
| C++20 | Concepts | 清晰的模板约束 |
| C++20 | `requires` | 更好的约束表达 |

**下一章**，我们将探讨**可变参数模板**，学习如何编写接受任意数量参数的模板函数，并实现一个类型安全的嵌入式事件系统。
