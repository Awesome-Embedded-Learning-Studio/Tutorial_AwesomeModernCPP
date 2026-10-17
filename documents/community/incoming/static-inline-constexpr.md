---
title: "static、inline 与 constexpr"
description: "从 static 的共享与存储语义、inline 的 ODR 与链接语义，到 constexpr 的编译期求值，理清三者单独使用和组合时的效果与边界"
chapter: 1
order: 1
tags:
  - host
  - cpp-modern
  - intermediate
  - constexpr
  - 编译期计算
  - 基础
difficulty: intermediate
platform: host
cpp_standard: [11, 14, 17]
reading_time_minutes: 13
community_category: learning-notes
---

# static、inline 与 constexpr

## `static`

> 在 C++ 中，写在 `struct` 和 `class` 中的变量叫做成员变量；同理，写在 `struct` 和 `class` 中的函数叫做成员函数，只是它们默认的访问权限不一样：
>
> - `class`：默认是 `private`
> - `struct`：默认是 `public`

### `static` 作为成员变量

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

两者行为完全相同：

- `static` 变量属于整个类或结构体，而不是某个对象。
- 所有对象共享同一个静态成员变量。
- 可以通过 `A::count` 或 `B::count` 访问。
- 不需要创建对象就能访问，前提是有访问权限。
- 静态成员变量通常具有静态存储期，其生命周期贯穿整个程序运行期间。

> 需要注意：`A::count` 和 `B::count` 是两个独立的变量，它们并不共享数据。

### `static` 作为成员变量与普通变量的区别

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

普通成员变量：每个对象都有自己的普通成员变量，`s1.age` 和 `s2.age` 是两个独立变量，修改某个对象的成员变量不会对其他对象造成影响。

静态成员变量：`s1` 和 `s2` 共享同一个变量，此变量不属于某个对象，而是属于类，对此变量进行修改会影响其他对象通过该成员看到的值。

### `static` 作为成员函数

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

特点：

- 静态成员函数属于类，不依赖某个具体对象。
- 静态成员函数没有 `this` 指针。
- 可以直接访问类中有权限访问的静态成员。
- 不能在没有对象的情况下直接访问非静态成员变量或调用非静态成员函数。
- 可以通过 `类名::函数名()` 调用，不需要先创建对象。

同时，`static` 成员函数还有许多限制。由于静态成员函数不依赖对象，因此：

- 不能使用 `this` 指针。
- 不能声明为 `virtual` 虚函数。
- 不能使用非静态成员函数的 `const` 或 `volatile` 限定符。
- 不能使用仅适用于非静态成员函数的 `&` 或 `&&` 引用限定符。

### `static` 作为成员函数与普通函数的区别

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

`static` 成员函数：调用时不需要对象，可以使用 `Student::printCount()`。由于不属于某一个对象，所以没有 `this` 指针，可以直接访问静态成员，但是不可以访问非静态成员，同时不能声明为 `virtual`，也不能使用 `const`、`volatile` 和引用限定符。

普通成员函数：调用时需要一个对象、对象引用或对象指针，可以使用 `student.printInfo()`，拥有 `this` 指针。可以直接访问当前对象的非静态成员，也可以直接访问静态成员。可以声明为 `const` 或 `volatile`；也可以使用 `&`、`&&` 引用限定符；还可以声明为 `virtual`，配合继承实现运行时多态。

> 注意：虽然静态成员函数也可以写成 `student.printCount()`，但这种写法不会使用 `student` 对象。为了方便进行区分，笔者建议优先写成 `Student::printCount()`。

## `inline`

### `inline` 的作用

`inline` 主要用于解决 ODR（One Definition Rule，单一定义规则：一个实体在整个程序中通常只有一个定义）带来的重复定义问题。对于具有外部链接的函数或变量，它允许同一个实体在多个翻译单元中出现相同定义，链接后仍然表示同一个实体。

同时，`inline` 建议编译器把函数展开。是否进行函数内联，由编译器决定。对于现代编译器，即使不加 `inline`，编译器仍然可以较好地判断是否进行内联。

### `inline` 作为函数说明符

```cpp
inline int Add(int a, int b) {
    return a + b;
}
```

当 `inline` 函数定义放在头文件中，并被多个源文件包含时，各个翻译单元可以看到同一份定义，不会因为重复定义而产生链接错误。

> 类内定义的成员函数默认具有 `inline` 属性：

```cpp
class Math {
public:
    int Add(int a, int b) {
        return a + b;
    }
};
```

上面的 `Math::Add` 不需要再写 `inline`。如果成员函数在类外定义，并且定义放在头文件中，可以显式写成：

```cpp
class Math {
public:
    int Add(int a, int b);
};

inline int Math::Add(int a, int b) {
    return a + b;
}
```

`inline` 函数仍然需要正常声明或定义，只是为了防止违反 ODR 原则，并不是“完全不需要头文件”。

### `inline` 作为变量说明符

从 C++17 开始，`inline` 也可以修饰变量：

```cpp
inline int global_count = 0;
```

如果这个定义放在头文件中，多个源文件可以包含它；整个程序中使用的是同一个 `global_count`，不会因为多个翻译单元各出现一次定义而违反 ODR。

`inline` 变量仍然是普通变量，可以被修改。它解决的是“允许多个相同定义并保持同一个实体”的问题，不会自动让变量变成 `const`，也不会自动让变量在编译期确定。

> 反过来看，`inline` 单独出现时，基本都用在命名空间作用域的变量上：普通成员变量不涉及跨翻译单元的定义问题，用不上它。

## `constexpr`

### `constexpr` 的作用

`constexpr` 表示变量或函数在编译期可以直接参与常量表达式计算。

`constexpr` 的特点是编译期能算就算，不行就运行时算，同时 `constexpr` 修饰的变量本身具有 `const` 语义，不能再通过该变量修改对象。

```cpp
constexpr int Square(int value) {
    return value * value;
}

constexpr int size = Square(4);
```

`constexpr` 的重点是常量求值和编译期检查，不是保证一定更快，也不是保证一定减少内存占用。

命名空间作用域下只写 `constexpr` 的变量默认是内部链接，每个包含它的翻译单元各有一份；叠上 `inline`（C++17 起）才变成所有翻译单元共享的同一个实体，值仍然是编译期常量：

```cpp
inline constexpr int kMaxSize = 128; // 头文件里
```

`constexpr` 函数从 C++11 开始隐含具有 `inline` 属性，因此下面的写法通常没有必要重复写 `inline`：

```cpp
constexpr int SquareA(int value) {
    return value * value;
}

inline constexpr int SquareB(int value) {
    return value * value;
}
```

`SquareA` 和 `SquareB` 都可以放在头文件中被多个翻译单元使用；`inline constexpr` 只是把两个属性显式写出来。

### `constexpr` 与普通成员变量

普通非静态成员变量不能直接声明为 `constexpr`，因为它属于对象，需要在对象构造时拥有自己的存储。它也不像命名空间作用域的变量那样会在每个翻译单元各有一份定义，用不着为它考虑 `inline` 和 ODR，直接使用 `const` 或类内默认成员初始化器就够了：

```cpp
struct Config {
    const int mode = 1;
};
```

如果需要一个由整个类共享、并且在编译期确定的成员，应使用 `static constexpr`。

### `static constexpr` 成员变量

```cpp
class Config {
public:
    static constexpr int max_size = 128;
};

static_assert(Config::max_size == 128);
```

`static constexpr` 同时表达了两层含义：

- `static`：这个成员属于类，所有对象共享同一个成员。
- `constexpr`：这个值必须在编译期确定，并且不能通过它修改。

在 C++17 及之后，类内定义的 `static constexpr` 数据成员隐含具有 `inline` 属性，因此不需要再写类外定义：

```cpp
// C++17 及之后不需要再写：
// constexpr int Config::max_size;
```

在 C++17 之前，如果 `static constexpr` 成员被以需要存储地址的方式（指针或者引用）使用，还需要在类外提供定义，否则一旦有代码需要它的存储，链接时通常会报 `undefined reference`：

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

## `static`、`inline` 与 `constexpr` 的组合

### `inline static` 成员变量

从 C++17 开始，`inline static` 可以让静态成员变量直接在类内定义并初始化：

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

这里的 `inline` 解决静态成员变量需要类外定义的问题，`static` 表示所有对象共享同一个变量。C++17 的 `inline static` 允许非 `const` 静态数据成员直接在类内定义并初始化；`inline` 解决的是静态数据成员的单一定义和类内定义问题。普通非静态成员变量可以从 C++11 开始使用类内默认成员初始化器，不需要 `inline`：

```cpp
struct User {
    int id = 0; // 普通非静态成员变量的类内默认初始化
};
```

### `inline static` 普通变量

对于命名空间作用域下的普通变量，`static` 和 `inline` 并不存在语义冲突，但组合使用通常没有必要。C++17 引入的 `inline` 变量允许同一个变量在多个翻译单元中定义，并在具有外部链接时保持实体的唯一性；而 `static` 使变量具有内部链接，每个翻译单元都会拥有独立的变量实体。`inline` 想让所有包含这个头文件的翻译单元看到同一个变量，`static` 却让每个翻译单元各自留一份。

因此，当 `static` 与 `inline` 同时修饰命名空间作用域的变量时，`static` 所赋予的内部链接属性使 `inline` 用于跨翻译单元共享同一实体的机制不再发挥作用，此时 `inline` 通常是冗余的：

```cpp
// header.hpp 里
static inline int counter = 0; // 合法，但每个翻译单元各有一份
```

### `inline static` 成员函数

`static` 成员函数也可以写上 `inline`：

```cpp
class Logger {
public:
    inline static void Print() {
        // 没有 this 指针
    }
};
```

`static` 表示这个函数属于类，不依赖某个对象。这里的`inline`表示此函数具有 `inline` 属性，由于类内定义的成员函数默认具有 `inline` 属性：，因此 `static inline` 中的 `inline` 通常可以省略。

- `inline` 表示这个定义可以安全地放在头文件中，被多个翻译单元使用。
- 如果函数直接定义在类内，它本身已经隐含具有 `inline` 属性，因此 `inline static` 中的 `inline` 通常可以省略，这里的作用只是语义更加明确

下面两种写法等价：

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

如果只在类内声明、在类外定义，并且定义放在头文件中，则可以显式写出 `inline`：

```cpp
class Logger {
public:
    static void Print();
};

inline void Logger::Print() {}
```

### `inline static` 普通函数

普通函数上把 `inline` 和 `static` 写在一起同样没有意义。`inline` 在这里要做的是让同一个定义能被多个翻译单元共用而不违反 ODR；但 `static` 已经把这个函数变成内部链接，每个翻译单元自带一份定义，本来就不会违反 ODR，`inline` 没有发挥的余地。

### `inline static constexpr` 成员变量

下面的写法在 C++17 中合法：

```cpp
class Limits {
public:
    inline static constexpr int max_size = 128;
};
```

但对于 `static constexpr` 数据成员来说，C++17 已经隐含 `inline`，因此下面两种写法的效果相同：

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

`inline static constexpr` 可以用于强调“类内定义、类共享、编译期常量”，但通常写成 `static constexpr` 就足够了。
