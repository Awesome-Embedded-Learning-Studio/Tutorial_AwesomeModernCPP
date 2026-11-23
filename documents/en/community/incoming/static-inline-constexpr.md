---
title: "static, inline, and constexpr"
description: "From the shared and storage semantics of static, the ODR and linkage semantics of inline, to the compile-time evaluation of constexpr, clarify the effects and boundaries when using these three individually and in combination"
chapter: 1
order: 1
tags:
  - host
  - cpp-modern
  - intermediate
  - constexpr
  - compile-time computation
  - basics
difficulty: intermediate
platform: host
cpp_standard: [11, 14, 17]
reading_time_minutes: 13
community_category: learning-notes
contributors:
  - github: xiaoshuaijie
    roles: [author]
translation:
  source: documents/community/incoming/static-inline-constexpr.md
  source_hash: 53c7d39c587729391ada8a441a7ba0d875bf8d9fff4ebaab25309c0276ab1260
  translated_at: '2026-11-22T13:15:06+00:00'
  engine: qwen3:14b
  token_count: 8294
---
# static, inline, and constexpr

## `static`

> In C++, variables written inside `struct` and `class` are called member variables; similarly, functions written inside `struct` and `class` are called member functions, but they have different default access permissions:
>
> - `class`: default is `private`
> - `struct`: default is `public`

### `static` as a Member Variable

```cpp
class A {
public:
    static int count;
};

struct B {
    static int count;
};

int A::count = 0;
int B::count = 0;
```

Both behave exactly the same:

- `static` variables belong to the entire class or structure, not to a specific object.
- All objects share the same static member variable.
- It can be accessed via `A::count` or `B::count`.
- It can be accessed without creating an object, provided it has access permissions.
- Static member variables typically have static storage duration, with their lifetime spanning the entire program execution.

> Note: `A::count` and `B::count` are two separate variables, and they do not share data.

### Difference Between `static` Member Variables and Regular Variables

```cpp
#include <iostream>

using namespace std;

struct Student {
    int age = 0;      // 普通成员变量
    static int count; // 静态成员变量
};

int Student::count = 0;

int main() {
    Student s1;
    Student s2;

    s1.age = 18;
    s2.age = 20;

    s1.count = 10;

    cout << s1.age << endl;         // 18
    cout << s2.age << endl;         // 20
    cout << s2.count << endl;       // 10
    cout << Student::count << endl; // 10
}
```

Regular member variables: each object has its own regular member variable, `s1.age` and `s2.age` are two separate variables, modifying a member variable of one object does not affect other objects.

Static member variables: `s1` and `s2` share the same variable, which does not belong to a specific object but to the class. Modifying this variable affects the value seen by other objects through this member.

### `static` as a Member Function

```cpp
#include <iostream>

using namespace std;

class A {
public:
    static void print() {
        cout << "Hello" << endl;
    }
};

struct B {
    static void print() {
        cout << "Hello" << endl;
    }
};

int main() {
    A::print();
    B::print();
}
```

Features:

- Static member functions belong to the class and do not depend on a specific object.
- Static member functions do not have a `this` pointer.
- They can directly access static members with accessible permissions.
- They cannot directly access non-static member variables or call non-static member functions without an object.
- They can be called via `class name::function name()`, without needing to create an object first.

At the same time, `static` member functions have many limitations. Since static member functions do not depend on objects:

- They cannot use the `this` pointer.
- They cannot be declared as `virtual` functions.
- They cannot use the `const` or `volatile` qualifiers of non-static member functions.
- They cannot use the `&` or `&&` reference qualifiers that apply only to non-static member functions.

### Difference Between `static` Member Functions and Regular Functions

```cpp
#include <iostream>

using namespace std;

struct Student {
    int age = 0;
    static int count;

    static void printCount() {
        cout << count << endl;
    }

    void printInfo() const {
        cout << age << endl;
        cout << count << endl;
    }
};

int Student::count = 0;

int main() {
    Student student;

    Student::printCount(); // 静态成员函数：可以通过类名调用
    student.printCount();  // 也可以使用对象语法，但不依赖 student
    student.printInfo();   // 普通成员函数：通过对象调用
}
```

`static` member functions: do not require an object to be called, can be used as `Student::printCount()`. Since they do not belong to a specific object, they have no `this` pointer, can directly access static members, but cannot access non-static members. They cannot be declared as `virtual`, nor can they use `const`, `volatile`, or reference qualifiers.

Regular member functions: require an object, object reference, or object pointer to be called, can be used as `student.printInfo()`, have a `this` pointer. They can directly access non-static members of the current object and also directly access static members. They can be declared as `const` or `volatile`; they can use `&` or `&&` reference qualifiers; they can also be declared as `virtual`, enabling runtime polymorphism with inheritance.

> Note: Although static member functions can also be written as `student.printCount()`, this usage does not utilize the `student` object. For clarity, it is recommended to prefer writing them as `Student::printCount()`.

## `inline`

### Purpose of `inline`

`inline` is mainly used to solve the problem of repeated definitions caused by the ODR (One Definition Rule: an entity in the entire program usually has only one definition) for functions or variables with external linkage. It allows the same definition of an entity to appear in multiple translation units, and after linking, it still represents the same entity.

At the same time, `inline` suggests the compiler to expand the function. Whether the function is inlined is decided by the compiler. For modern compilers, even without `inline`, the compiler can still make a good judgment on whether to inline the function.

### `inline` as a Function Specifier

```cpp
inline int Add(int a, int b) {
    return a + b;
}
```

When the `inline` function definition is placed in a header file and included by multiple source files, each translation unit can see the same definition, and no linking errors will occur due to repeated definitions.

> Member functions defined inside a class default to having `inline` properties:

```cpp
class Math {
public:
    int Add(int a, int b) {
        return a + b;
    }
};
```

The above `Math::Add` does not need to be written as `inline` again. If the member function is defined outside the class and placed in a header file, it can be explicitly written as:

```cpp
class Math {
public:
    int Add(int a, int b);
};

inline int Math::Add(int a, int b) {
    return a + b;
}
```

`inline` functions still need to be normally declared or defined, just to prevent violating the ODR principle, not because they "completely don't need a header file."

### `inline` as a Variable Specifier

Starting from C++17, `inline` can also be used to modify variables:

```cpp
inline int global_count = 0;
```

If this definition is placed in a header file, multiple source files can include it; the entire program uses the same `global_count`, and it does not violate the ODR because multiple translation units each have one definition.

`inline` variables are still regular variables and can be modified. They solve the problem of "allowing multiple identical definitions while maintaining the same entity," but they do not automatically make the variable `const`, nor do they automatically make the variable determined at compile time.

> Conversely, when `inline` appears alone, it is mostly used on variables in the namespace scope: regular member variables do not involve definition issues across translation units, so they don't need it.

## `constexpr`

### Purpose of `constexpr`

`constexpr` indicates that a variable or function can directly participate in constant expression calculations at compile time.

The key feature of `constexpr` is that it calculates at compile time if possible, otherwise at runtime. At the same time, variables modified by `constexpr` have `const` semantics and cannot be used to modify objects through them.

```cpp
constexpr int Square(int value) {
    return value * value;
}

constexpr int size = Square(4);
```

The focus of `constexpr` is on constant evaluation and compile-time checking, not on guaranteeing faster performance or guaranteed reduction in memory usage.

Variables with `constexpr` in the namespace scope default to internal linkage, each translation unit containing it has its own copy; only when `inline` is added (starting from C++17) does it become a shared entity across all translation units, and the value is still a compile-time constant:

```cpp
inline constexpr int kMaxSize = 128; // 头文件里
```

`constexpr` functions implicitly have `inline` properties starting from C++11, so the following writing is usually unnecessary to repeat `inline`:

```cpp
constexpr int SquareA(int value) {
    return value * value;
}

inline constexpr int SquareB(int value) {
    return value * value;
}
```

Both `SquareA` and `SquareB` can be placed in a header file and used by multiple translation units; `inline constexpr` is just explicitly writing out the two attributes.

### `constexpr` and Regular Member Variables

Regular non-static member variables cannot be directly declared as `constexpr` because they belong to an object and need their own storage during object construction. They are not like namespace-scope variables that have a definition in each translation unit, so there is no need to consider `inline` and ODR for them; just using `const` or default member initializers inside the class is sufficient:

```cpp
struct Config {
    const int mode = 1;
};
```

If you need a member that is shared by the entire class and determined at compile time, you should use `static constexpr`.

### `static constexpr` Member Variables

```cpp
class Config {
public:
    static constexpr int max_size = 128;
};

static_assert(Config::max_size == 128);
```

`static constexpr` expresses two meanings at the same time:

- `static`: this member belongs to the class, and all objects share the same member.
- `constexpr`: this value must be determined at compile time and cannot be modified through it.

From C++17 onwards, `static constexpr` data members defined inside the class implicitly have `inline` properties, so there is no need to provide an external definition:

```cpp
// C++17 及之后不需要再写：
// constexpr int Config::max_size;
```

Before C++17, if a `static constexpr` member is used in a way that requires storing an address (pointers or references), an external definition is still needed, otherwise, when the code needs its storage, a link-time error of `undefined reference` will usually occur:

```cpp
class Config {
public:
    static constexpr int max_size = 128;
};

// C++14 及之前的常见写法
constexpr int Config::max_size; // 类外定义
int a = Config::max_size; // 通常没问题
const int& b = Config::max_size; // 可能需要类外定义
```

## Combination of `static`, `inline`, and `constexpr`

### `inline static` Member Variables

Starting from C++17, `inline static` allows static member variables to be directly defined and initialized inside the class:

```cpp
#include <iostream>

using namespace std;

class Counter {
public:
    inline static int value = 0;
};

int main() {
    Counter a;
    Counter b;

    a.value = 10;
    cout << b.value << endl; // 10，所有对象共享同一个 value
}
```

Here, `inline` solves the problem of needing an external definition for static member variables, and `static` indicates that all objects share the same variable. `inline static` in C++17 allows non-`const` static data members to be directly defined and initialized inside the class; `inline` solves the problem of single definition and class definition for static data members. Regular non-static member variables can use class default member initializers starting from C++11, and do not need `inline`:

```cpp
struct User {
    int id = 0; // 普通非静态成员变量的类内默认初始化
};
```

### `inline static` Regular Variables

For regular variables in the namespace scope, `static` and `inline` do not have semantic conflicts, but using them together is usually unnecessary. The `inline` variable introduced in C++17 allows the same variable to be defined in multiple translation units and maintains the uniqueness of the entity when it has external linkage; while `static` makes the variable have internal linkage, with each translation unit having its own independent variable entity. `inline` wants all translation units that include this header file to see the same variable, but `static` makes each translation unit keep its own copy.

Therefore, when `static` and `inline` are used together to modify a variable in the namespace scope, the internal linkage property assigned by `static` makes the mechanism of `inline` for sharing the same entity across translation units ineffective, and at this point, `inline` is usually redundant:

```cpp
// header.hpp 里
static inline int counter = 0; // 合法，但每个翻译单元各有一份
```

### `inline static` Member Functions

`static` member functions can also be written with `inline`:

```cpp
class Logger {
public:
    inline static void Print() {
        // 没有 this 指针
    }
};
```

`static` indicates that this function belongs to the class and does not depend on a specific object. The `inline` here indicates that this function has `inline` properties. Since member functions defined inside the class default to having `inline` properties, the `inline` in `static inline` can usually be omitted.

- `inline` indicates that this definition can be safely placed in a header file and used by multiple translation units.
- If the function is directly defined inside the class, it already implicitly has `inline` properties, so the `inline` in `inline static` can usually be omitted, and its role is just to make the semantics clearer.

The following two writings are equivalent:

```cpp
class LoggerA {
public:
    static void Print() {}
};

class LoggerB {
public:
    inline static void Print() {}
};
```

If the function is declared inside the class and defined outside the class, and the definition is placed in a header file, `inline` can be explicitly written:

```cpp
class Logger {
public:
    static void Print();
};

inline void Logger::Print() {}
```

### `inline static` Regular Functions

Putting `inline` and `static` together on regular functions is also meaningless. `inline` here is intended to allow the same definition to be used by multiple translation units without violating ODR; but `static` has already made this function internal linkage, with each translation unit having its own definition, which does not violate ODR in the first place, and `inline` has no room to play.

### `inline static constexpr` Member Variables

The following writing is legal in C++17:

```cpp
class Limits {
public:
    inline static constexpr int max_size = 128;
};
```

But for `static constexpr` data members, C++17 already implicitly includes `inline`, so the following two writings have the same effect:

```cpp
class LimitsA {
public:
    static constexpr int max_size = 128;
};

class LimitsB {
public:
    inline static constexpr int max_size = 128;
};
```

`inline static constexpr` can be used to emphasize "defined inside the class, shared by the class, compile-time constant," but writing it as `static constexpr` is usually sufficient.
