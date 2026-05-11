---
title: "异常基础"
description: "掌握 try/catch/throw 语法和标准异常层次"
chapter: 10
order: 1
difficulty: intermediate
reading_time_minutes: 14
platform: host
prerequisites:
  - "模板特化初步"
tags:
  - cpp-modern
  - host
  - intermediate
  - 进阶
cpp_standard: [11, 14, 17, 20]
---

# 异常基础

到目前为止，我们处理错误的方式基本上就两种：要么用返回值表示失败（比如函数返回 `-1` 或者 `nullptr`），要么直接 `assert` 炸掉让程序崩溃。这两种方式在小程序里勉强够用，但一旦项目规模上去了，问题就暴露了——返回值错误码容易被调用者忽略，`assert` 在 Release 构建里直接被编译器删掉。更麻烦的是，如果错误发生在深层嵌套的调用链里，你得一层一层地把错误码往外传，中间每一层都得检查、都得处理，代码很快就变成一棵巨大的 `if (error)` 圣诞树。（见到好多次这个玩意了，真的想吐出来。。。）

C++ 的异常机制就是为了解决这个问题而生的。它提供了一种**结构化的错误传播通道**——函数可以直接抛出异常来报告"出问题了"，而调用链上任何一个有能力的调用者都可以捕获并处理，中间的函数不需要知道也不需要传递。这一章我们从最基础的 `try`/`catch`/`throw` 语法学起，理清标准异常类的层次关系，最后写一段完整的实战代码把所有知识点串起来。

## 点火——throw、try、catch 三件套

异常机制的核心操作只有三个关键字。`throw` 负责抛出异常——它后面的表达式就是一个异常对象，可以是任何可拷贝的类型。`try` 标记一段"可能会出问题"的代码区域。`catch` 负责捕获并处理 `try` 区域内抛出的异常。先看一个最简短的例子：

```cpp
#include <iostream>
#include <stdexcept>

int main()
{
    try {
        throw std::runtime_error("Something went wrong");
    }
    catch (const std::runtime_error& e) {
        std::cout << "Caught: " << e.what() << "\n";
    }
    return 0;
}
```

运行结果是 `Caught: Something went wrong`。`throw` 创建了一个 `std::runtime_error` 对象并抛出，程序立刻中断 `try` 块中 `throw` 之后的执行，跳转到匹配的 `catch` 块。`e.what()` 返回构造时传入的字符串。你可能会问：为什么用 `std::runtime_error` 而不是直接 `throw 42` 或者 `throw "oops"`？技术上确实可以——C++ 允许抛出任何类型——但在实际工程中，使用标准异常类或自定义异常类是更好的做法，因为异常对象可以携带丰富的错误信息，而且可以利用继承体系实现层次化的捕获。

### 栈展开——异常飞过时发生了什么

当异常被抛出后，程序不会直接从 `throw` 蹦到 `catch`——中间发生了一个非常重要的过程叫做**栈展开（stack unwinding）**。从 `throw` 点到最近的匹配 `catch` 之间，所有已经构造的局部对象都会被按构造的**逆序**析构。这个机制是 RAII 能够保证资源不泄漏的基础。

```cpp
#include <iostream>
#include <stdexcept>

struct Trace {
    const char* name_;
    explicit Trace(const char* n) : name_(n)
    { std::cout << "  Constructing: " << name_ << "\n"; }
    ~Trace()
    { std::cout << "  Destroying: " << name_ << "\n"; }
};

void inner()
{
    Trace t3("t3_in_inner");
    throw std::runtime_error("boom from inner");
}

void middle()
{
    Trace t2("t2_in_middle");
    inner();
}

int main()
{
    try {
        Trace t1("t1_in_main");
        middle();
    }
    catch (const std::exception& e) {
        std::cout << "  Caught: " << e.what() << "\n";
    }
    return 0;
}
```

输出：

```text
  Constructing: t1_in_main
  Constructing: t2_in_middle
  Constructing: t3_in_inner
  Destroying: t3_in_inner
  Destroying: t2_in_middle
  Destroying: t1_in_main
  Caught: boom from inner
```

`t3`、`t2`、`t1` 按构造的逆序被析构——这就是栈展开。整个过程不需要我们手动写任何清理代码，语言机制保证了一切。

> **踩坑预警**：栈展开过程中，如果某个析构函数本身又抛出了异常（在异常处理期间又产生了新异常），程序会直接调用 `std::terminate` 终止，没有任何挽回余地。所以析构函数**绝对不能**抛出异常。C++11 起，所有析构函数默认被标记为 `noexcept`，但如果你自己显式写了 `~MyClass() { throw ...; }`，编译器不会阻止你，运行时直接炸。这一点务必牢记。

## 标准异常层次——exception 家族

C++ 标准库定义了一套以 `std::exception` 为根的异常类层次。了解这个层次有两个好处：一是你可以选择最合适的标准异常类来表达错误语义，二是你可以用基类引用来捕获一整族异常。

`std::exception` 是所有标准异常的基类，定义了虚函数 `what()` 返回 `const char*` 描述信息。它的直接派生类分成两大分支。`std::logic_error` 表示"程序逻辑上有错误"——理论上在程序运行之前就能被检测出来，比如传了无效的参数；它的子类包括 `std::invalid_argument`（非法参数）、`std::out_of_range`（下标越界）、`std::domain_error`（定义域错误，实践中几乎没人用）。`std::runtime_error` 表示"运行时才暴露的问题"——只有程序跑起来之后才会出现，比如文件不存在、网络超时；它的子类包括 `std::overflow_error` 和 `std::underflow_error`（算术溢出）。另外 `std::bad_alloc` 直接继承自 `std::exception`，在 `new` 无法分配内存时被抛出。

利用这个继承层次，我们可以做**层次化捕获**：

```cpp
#include <iostream>
#include <stdexcept>
#include <vector>

int main()
{
    try {
        std::vector<int> v = {1, 2, 3};
        std::cout << v.at(10) << "\n";  // at() 越界抛出 out_of_range
    }
    catch (const std::out_of_range& e) {
        std::cout << "Out of range: " << e.what() << "\n";
    }
    catch (const std::logic_error& e) {
        std::cout << "Logic error: " << e.what() << "\n";
    }
    catch (const std::exception& e) {
        std::cout << "Exception: " << e.what() << "\n";
    }
    return 0;
}
```

输出：

```text
Out of range: vector::_M_range_check: __n (which is 10) >= this->size() (which is 3)
```

`catch` 块的匹配规则是从上到下的：第一个类型匹配的 `catch` 会被执行，后面的就跳过了。

> **踩坑预警**：`catch` 的顺序很重要，永远把最具体的异常类型放在前面，最通用的放在后面。如果把 `catch (const std::exception&)` 放在第一个，所有标准异常都会被它截获，后面的 `catch` 全部变成死代码。更糟糕的是，这种错误编译器不会报任何警告，只有运行时才会暴露。

## 按值抛出，按 const 引用捕获

C++ 社区广泛认可的最佳实践：**throw by value, catch by const reference**。按值抛出是因为 `throw` 表达式的值会被拷贝（或移动）到一个由编译器管理的特殊存储区域中，即使原来的对象在栈展开时被析构了，异常对象本身仍然有效。按 `const` 引用捕获则避免了**对象切片（object slicing）**——如果按值捕获 `std::exception`，而你实际抛出的是 `std::runtime_error`，派生类部分会被切掉，`what()` 调用的是基类版本而非派生类版本。

```cpp
// 错误：按值捕获会切片
catch (std::exception e) {           // runtime_error 部分丢失！
    std::cout << e.what() << "\n";   // 错误信息可能完全不对
}

// 正确：按 const 引用捕获
catch (const std::exception& e) {    // 多态完整保留
    std::cout << e.what() << "\n";   // 正确输出原始信息
}
```

> **踩坑预警**：`what()` 返回的 `const char*` 指针指向异常对象内部存储的字符串，一旦异常对象被销毁，这个指针就悬空了。所以在 `catch` 块内使用 `e.what()` 是安全的，但如果你把返回值存下来在 `catch` 块外面使用——祝你好运。正确的做法是在 `catch` 块内把内容拷贝到 `std::string` 里。

## 多个 catch 块与重新抛出

一个 `try` 块后面可以跟多个 `catch` 块，分别处理不同类型的异常。另外有时候 `catch` 块捕获了异常之后发现自己处理不了，或者需要做些善后工作然后继续往外抛，这时候就用到了**重新抛出**——单独写一个 `throw;`（不带任何表达式）：

```cpp
#include <cstdio>
#include <iostream>
#include <stdexcept>

void wrapper()
{
    try {
        throw std::runtime_error("Runtime failure");
    }
    catch (const std::exception& e) {
        std::fprintf(stderr, "[wrapper] Logging: %s\n", e.what());
        throw;  // 重新抛出原始异常，保持完整类型信息
    }
}

int main()
{
    try {
        wrapper();
    }
    catch (const std::runtime_error& e) {
        std::cout << "Caught: " << e.what() << "\n";
    }
    catch (...) {
        // 捕获所有其他类型的异常
        std::cout << "Caught unknown exception\n";
    }
    return 0;
}
```

输出：

```text
[wrapper] Logging: Runtime failure
Caught: Runtime failure
```

`throw;` 和 `throw e;` 有本质区别——前者重新抛出的是**原始异常对象**，保留完整的动态类型信息；后者会拷贝一份新的异常对象，静态类型是 `catch` 参数的类型，派生类信息会被切片掉。所以除非你确实想改变异常的类型，否则永远用 `throw;`。`catch (...)` 意思是"捕获任何类型的异常"，在析构函数或库的边界处偶尔会用到，但日常代码中不要滥用——吞掉异常却不做任何处理是调试噩梦的根源。

## noexcept——承诺不抛异常

从 C++11 开始，`noexcept` 关键字用于声明一个函数**不会抛出异常**。这不仅仅是一个给程序员看的注释——编译器会根据这个承诺做优化（比如省略栈展开相关的登记代码），标准库的一些组件也会根据操作是否 `noexcept` 来选择实现路径。

```cpp
int safe_computation(int a, int b) noexcept
{
    return a + b;  // 纯计算，确实不会抛异常
}
```

如果标记了 `noexcept` 的函数内部真的抛出了异常，程序立刻调用 `std::terminate`——没有任何栈展开，没有任何 `catch` 的机会，直接死亡。所以 `noexcept` 不是随便加的，你得确定这个函数确实不会抛异常，或者它内部用了 `try-catch` 把所有可能的异常都吞掉了。`noexcept` 也可以接受一个布尔参数——`noexcept(true)` 等价于 `noexcept`，`noexcept(false)` 等价于不加，标准库的 `std::swap` 就是根据元素类型的 `noexcept` 特性来决定自身的异常规格。

## 实战——exceptions.cpp

现在我们把前面的知识点整合到一个完整的程序里，实现安全整数除法和文件内容解析器。

```cpp
// exceptions.cpp
// 演示 try/catch/throw、标准异常层次、noexcept 的综合应用

#include <cstdio>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

/// @brief 安全的整数除法，除数为零时抛出异常
int safe_divide(int dividend, int divisor)
{
    if (divisor == 0) {
        throw std::invalid_argument("Division by zero is not allowed");
    }
    return dividend / divisor;
}

/// @brief 解析文件中的整数行
/// @throws std::runtime_error 文件无法打开
std::vector<int> parse_int_file(const std::string& path)
{
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open file: " + path);
    }

    std::vector<int> result;
    std::string line;
    int line_num = 0;

    while (std::getline(file, line)) {
        ++line_num;
        try {
            std::size_t pos = 0;
            int value = std::stoi(line, &pos);
            if (pos != line.size()) {
                throw std::invalid_argument("Trailing characters");
            }
            result.push_back(value);
        }
        catch (const std::exception& e) {
            std::cerr << "[parse_int_file] Error at line "
                      << line_num << ": " << e.what() << "\n";
            throw;  // 重新抛出，让调用者决定怎么处理
        }
    }
    return result;
}

/// @brief 格式化并打印解析结果（noexcept 示例）
void print_results(const std::vector<int>& values) noexcept
{
    std::cout << "Parsed " << values.size() << " values: ";
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i > 0) std::cout << ", ";
        std::cout << values[i];
    }
    std::cout << "\n";
}

int main()
{
    // 安全除法演示
    std::cout << "=== Safe Divide Demo ===\n";
    struct { int a, b; const char* label; } cases[] = {
        {10, 3, "normal"}, {7, 0, "zero"}, {-20, 4, "negative"},
    };
    for (const auto& tc : cases) {
        try {
            std::cout << "  " << tc.a << " / " << tc.b
                      << " = " << safe_divide(tc.a, tc.b) << "\n";
        }
        catch (const std::invalid_argument& e) {
            std::cout << "  " << tc.label << ": " << e.what() << "\n";
        }
    }

    // 文件解析演示
    std::cout << "\n=== File Parser Demo ===\n";
    const char* test_path = "/tmp/exception_test_data.txt";
    {
        std::ofstream out(test_path);
        out << "42\n100\nnot_a_number\n7\n";
    }
    try {
        auto values = parse_int_file(test_path);
        print_results(values);
    }
    catch (const std::exception& e) {
        std::cout << "  Caught: " << e.what() << "\n";
    }

    // catch-all 演示
    std::cout << "\n=== Catch-all Demo ===\n";
    try { throw 42; }
    catch (const std::exception&) { std::cout << "  Standard\n"; }
    catch (...) { std::cout << "  Unknown exception\n"; }

    return 0;
}
```

编译运行：

```bash
g++ -std=c++17 -Wall -Wextra exceptions.cpp -o exceptions && ./exceptions
```

验证输出：

```text
=== Safe Divide Demo ===
  10 / 3 = 3
  7 / 0 =   zero: Division by zero is not allowed
  -20 / 4 = -5

=== File Parser Demo ===
[parse_int_file] Error at line 3: stoi
  Caught: stoi

=== Catch-all Demo ===
  Unknown exception
```

逐段验证。安全除法部分：`10 / 3` 正常得到 `3`；`7 / 0` 在 `safe_divide` 抛出异常之前，`std::cout` 已经输出了 `7 / 0 =`，所以错误消息会跟在这个前缀后面；`-20 / 4` 得到 `-5`。文件解析部分：测试文件第三行 `"not_a_number"` 无法被 `std::stoi` 解析，`parse_int_file` 的 `catch` 块打印行号上下文后用 `throw;` 重新抛出，主函数捕获到了它——注意 `print_results` 没有被调用，因为异常在第 3 行就中断了解析循环。`catch(...)` 部分演示了对非标准异常类型的兜底捕获。`stoi` 的 `what()` 消息内容因编译器和标准库版本而异（例如 libstdc++ 可能输出 `stoi` 或 `stoi: no conversion`）。

> **踩坑预警**：`std::stoi` 在解析失败时抛出的是 `std::invalid_argument`（无法转换）或 `std::out_of_range`（数值超出 `int` 范围）。这两个异常都继承自 `std::logic_error`。如果你在 `catch` 块里需要区分这两种情况，应该用两个独立的 `catch` 分别处理，而不是统一用 `catch (const std::exception&)` 吞掉——后者会丢失错误的具体类型信息，增加调试难度。

## 练手时间

### 练习一：安全的数组访问

写一个函数 `int safe_get(const std::vector<int>& v, std::size_t index)`，当 `index` 越界时抛出 `std::out_of_range`，错误信息包含请求的索引和 vector 的实际大小。在 `main` 中测试正常访问和越界访问两种情况。

### 练习二：字符串转数字解析器

写一个函数 `std::vector<double> parse_doubles(const std::string& input)`，把用逗号分隔的字符串（如 `"1.5,2.7,3.14"`）解析为 `double` 向量。要求：无效的数字格式用 `std::invalid_argument` 报告，空输入用 `std::runtime_error` 报告。在调用端用 `try`/`catch` 分别处理两种异常并给出友好提示。

### 练习三：noexcept 运算符

写两个函数：`void safe_calc(int x) noexcept` 做简单计算，`void risky_calc(int x)` 在 `x` 为负数时抛出 `std::invalid_argument`。然后在 `main` 里用 `noexcept(safe_calc)` 和 `noexcept(risky_calc)` 这两个编译期运算符检查它们的 `noexcept` 状态并打印结果。

## 小结

这一章我们从零搭建了 C++ 异常处理的基本框架。`throw` 负责抛出异常对象，`try` 标记监控区域，`catch` 捕获并处理异常——这三件套构成了异常机制的语法核心。栈展开保证了异常飞过时所有局部对象都能被正确析构，标准异常类的继承层次让我们能够用基类引用做多态捕获。"按值抛出、按 const 引用捕获"是避免对象切片的关键惯例，`throw;` 用于重新抛出原始异常，`noexcept` 则用来标记不抛异常的函数——它既是编译器的优化提示，也是给调用者的契约承诺。

不过，知道怎么抛和怎么抓只是第一步。更重要的一个问题是：异常飞过的时候，在此之前已经分配的资源、打开的文件、锁住的互斥量怎么办？下一篇我们就来讨论这个话题——异常安全。我们会学习异常安全的四个等级，看看 RAII 如何在异常发生时保证资源不泄漏，以及怎么用 copy-and-swap 惯用法让操作具备事务级的强安全保证。
