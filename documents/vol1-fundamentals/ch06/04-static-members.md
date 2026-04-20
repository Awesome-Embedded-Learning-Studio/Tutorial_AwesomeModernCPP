---
title: "static 成员"
description: "掌握类的 static 变量与函数，理解类级别的共享状态与单例模式的初步思想"
chapter: 6
order: 4
difficulty: beginner
reading_time_minutes: 10
platform: host
prerequisites:
  - "析构函数与资源管理"
tags:
  - cpp-modern
  - host
  - beginner
  - 入门
  - 基础
cpp_standard: [11, 14, 17, 20]
---

# static 成员

到现在为止，我们接触的所有成员变量和成员函数都绑定在"对象"上——每创建一个 `Sensor`，就多一份 `pin`、多一份 `cached_value`，它们各自独立、互不干扰。但在实际工程里，有一类数据和操作天然就不属于某个具体对象，而是属于**整个类**。比如：当前系统里到底创建了多少个 `UARTPort` 实例？硬件抽象层有没有初始化过？所有 `Sensor` 共享的默认采样频率是多少？

这些需求的共同特征是：数据只有一份，所有对象共享；或者函数只和类的逻辑相关，不需要依赖任何具体实例的状态。C++ 用 `static` 关键字来满足这类需求——把它加在成员声明前面，这个成员就从"对象级别"变成了"类级别"。

这一章我们把静态成员变量和静态成员函数拆开来讲清楚，顺带实现一个自动 ID 分配器，最后瞥一眼 `static` 是怎么给单例模式铺路的。

## 静态成员变量——属于类的共享数据

声明一个静态成员变量很简单，在类型前面加 `static` 就行：

```cpp
class Employee {
private:
    int id_;
    std::string name_;
    static int next_id_;  // 声明：所有 Employee 共享的计数器
};
```

`next_id_` 在内存中只有一份拷贝。不管你创建了一百个还是零个 `Employee` 对象，`next_id_` 都存在（准确地说，它从程序启动到结束一直存在）。每个 `Employee` 对象有自己的 `id_` 和 `name_`，但所有对象看到的 `next_id_` 是同一个。

这里有一个经典的踩坑点：**静态成员变量必须在类外进行定义**。类里面的 `static int next_id_;` 只是声明，告诉编译器"有这么个东西存在"，但并没有真正分配内存。真正的定义要写在类外面：

```cpp
// Employee.cpp
int Employee::next_id_ = 1;  // 定义并初始化
```

如果你只声明了但不定义，编译是能通过的——因为编译器在处理类的定义时只看到了声明。但到了链接阶段，链接器发现没有任何目标文件里存在 `Employee::next_id_` 的实际存储位置，就会抛出一个 `undefined reference` 错误。这种"编译通过、链接报错"的问题经常让人血压拉满，因为你得在多个文件之间来回找到底忘了定义哪个静态成员。

> **踩坑预警**：C++17 之前，非 `const` 整型的静态成员变量必须在类外定义。如果你在头文件里声明了 `static int count_;` 却忘了在对应的 `.cpp` 文件里写 `int MyClass::count_ = 0;`，每个包含这个头文件的翻译单元都能编译通过，但最终链接时会炸。而且错误信息的措辞往往很抽象，新手根本不知道在说什么。

不过在 C++17 里，这个痛点得到了缓解——`inline static` 允许在类内直接定义静态成员：

```cpp
class Employee {
private:
    int id_;
    std::string name_;
    inline static int next_id_ = 1;  // C++17：类内定义，不需要类外定义
};
```

`inline` 在这里的语义是"允许在头文件中定义而不违反 ODR（One Definition Rule）"，和内联函数的 `inline` 是同一个关键字，但含义不同。如果你的项目可以用 C++17，建议直接用 `inline static`，省去了维护 `.cpp` 文件里一堆 `Type Class::member = value;` 的麻烦。

## 静态成员函数——不需要 this 的类操作

静态成员函数和静态成员变量一样，属于类本身。它的关键特征是**没有 `this` 指针**——因为调用它的时候不需要通过某个具体的对象。没有 `this` 意味着它无法访问任何非静态成员，毕竟编译器根本不知道"你要操作哪个对象的成员"。

```cpp
class Employee {
private:
    int id_;
    std::string name_;
    static int next_id_;

public:
    Employee(const std::string& name)
        : id_(next_id_++), name_(name) {}

    /// @brief 获取下一个将被分配的 ID（静态函数）
    static int peek_next_id() {
        return next_id_;       // OK：访问静态成员
        // return id_;         // 编译错误！静态函数没有 this，无法访问非静态成员
    }
};
```

调用静态成员函数用 `类名::函数名()` 的语法，不需要先创建对象：

```cpp
std::cout << Employee::peek_next_id() << std::endl;  // 不需要任何 Employee 实例
```

当然，通过对象来调用静态函数在语法上也是合法的（`emp.peek_next_id()`），但这只是语法糖——编译器还是会把它翻译成 `Employee::peek_next_id()`，对象实例在运行时根本不参与。笔者的建议是尽量用 `ClassName::function()` 的方式调用，语义更清晰，读者一眼就知道这是个静态函数。

## 实战：自动 ID 分配器

把上面的碎片拼起来，我们写一个完整版的 `Employee` 类，它能在创建时自动分配唯一 ID，并统计当前共有多少个员工对象：

```cpp
class Employee {
private:
    int id_;
    std::string name_;
    static int next_id_;
    static int active_count_;

public:
    explicit Employee(const std::string& name)
        : id_(next_id_++), name_(name)
    {
        ++active_count_;
    }

    ~Employee() { --active_count_; }

    int id() const { return id_; }
    const std::string& name() const { return name_; }

    static int get_active_count() { return active_count_; }
    static int peek_next_id() { return next_id_; }
};

// 静态成员定义
int Employee::next_id_ = 1;
int Employee::active_count_ = 0;
```

这里的设计思路是：`next_id_` 是一个只增不减的计数器，每构造一个对象就递增并取当前值作为该对象的 ID；`active_count_` 在构造时加一、析构时减一，实时反映当前存活的对象数量。

## static 与 const 的组合

当 `static` 和 `const`（或 `constexpr`）组合在一起时，情况又有所不同。C++ 允许 `static constexpr` 整型成员在类内直接初始化，不需要类外定义：

```cpp
class Config {
public:
    static constexpr int kMaxRetries = 3;       // OK：const 整型，类内初始化
    static constexpr double kPi = 3.14159265;   // C++11 起也允许浮点类型类内初始化
};
```

这种写法从 C++11 开始就广泛使用了。`constexpr` 隐含了 `const`，而且要求值在编译期就能确定，所以编译器可以直接把值内联到使用处，不需要为它分配实际的存储空间——除非你取了它的地址（`&Config::kMaxRetries`），此时 ODR 使用规则会要求你提供一份类外定义。

不过这里有一个容易搞混的历史遗留问题：C++03 时代，只有 `static const int`（以及 `short`、`char`、`long` 等整型）才能在类内初始化。如果你写了 `static const double pi = 3.14;`，在 C++03 编译器上直接报错。C++11 引入 `constexpr` 之后，这个限制基本消失了——现在推荐统一用 `static constexpr`，语义更明确，也不会踩老标准的坑。

如果你需要在运行时才能确定初始值的静态成员（比如从配置文件读取），那就不能用 `constexpr`，只能用普通的 `static` 成员加一个初始化函数来赋值。

## 单例模式的雏形

提到 `static`，就不能不提它和单例模式（Singleton Pattern）的关系。单例模式的核心需求是：一个类在整个程序中只有一个实例，并提供全局访问点。它的实现离不开 `static`——用静态成员函数来提供访问入口，用静态成员变量来持有那个唯一的实例。

我们只看一个最简化的雏形，点到为止，不展开完整的实现细节：

```cpp
class SystemClock {
private:
    SystemClock() = default;  // 构造函数 private：阻止外部创建实例

    static SystemClock& instance() {
        static SystemClock clock;  // C++11 保证线程安全的局部静态变量
        return clock;
    }

public:
    // 删除拷贝和赋值，确保唯一性
    SystemClock(const SystemClock&) = delete;
    SystemClock& operator=(const SystemClock&) = delete;

    /// @brief 获取全局唯一的时钟实例
    static SystemClock& get() { return instance(); }

    uint64_t now() const {
        // 返回当前时间戳
        return 0;  // 简化
    }
};

// 使用
uint64_t t = SystemClock::get().now();
```

这个模式叫 Meyers' Singleton，利用了 C++11 的一个重要保证：函数内的 `static` 局部变量在首次执行到声明处时初始化，且初始化是线程安全的。我们这里不深入讨论单例的优缺点——只需要记住：`static` 成员 + `private` 构造函数是单例的基石。后续讲到设计模式的时候我们会正式展开。

## 实战演练——static_demo.cpp

把这一章的知识点整合成一个完整的程序：

```cpp
// static_demo.cpp
// static 成员综合演练：自动 ID 分配、实例计数、静态常量

#include <iostream>
#include <string>

class Employee {
private:
    int id_;
    std::string name_;
    static int next_id_;
    static int active_count_;

public:
    static constexpr int kMaxNameLength = 50;

    explicit Employee(const std::string& name)
        : id_(next_id_++), name_(name)
    {
        ++active_count_;
        std::cout << "[construct] Employee #" << id_
                  << " \"" << name_ << "\" created. "
                  << "Active: " << active_count_ << std::endl;
    }

    ~Employee()
    {
        --active_count_;
        std::cout << "[destruct]  Employee #" << id_
                  << " \"" << name_ << "\" destroyed. "
                  << "Active: " << active_count_ << std::endl;
    }

    int id() const { return id_; }
    const std::string& name() const { return name_; }

    static int get_active_count() { return active_count_; }
    static int peek_next_id() { return next_id_; }
};

int Employee::next_id_ = 1;
int Employee::active_count_ = 0;

/// @brief 创建一些临时对象，观察计数变化
void demo_scope()
{
    std::cout << "\n--- Enter demo_scope ---" << std::endl;
    Employee temp1("Zhang San");
    Employee temp2("Li Si");
    std::cout << "Inside scope, active count: "
              << Employee::get_active_count() << std::endl;
    std::cout << "--- Leave demo_scope ---" << std::endl;
    // temp1, temp2 离开作用域，析构
}

int main()
{
    std::cout << "=== Static Member Demo ===" << std::endl;
    std::cout << "Max name length: " << Employee::kMaxNameLength << std::endl;
    std::cout << "Next ID before any creation: "
              << Employee::peek_next_id() << std::endl;

    Employee emp1("Wang Wu");
    Employee emp2("Zhao Liu");

    std::cout << "\nCurrent active count: "
              << Employee::get_active_count() << std::endl;
    std::cout << "Next ID to be assigned: "
              << Employee::peek_next_id() << std::endl;

    demo_scope();

    std::cout << "\nAfter demo_scope, active count: "
              << Employee::get_active_count() << std::endl;
    std::cout << "Next ID to be assigned: "
              << Employee::peek_next_id() << std::endl;

    return 0;
}
```

编译运行：`g++ -std=c++17 -Wall -Wextra -o static_demo static_demo.cpp && ./static_demo`

预期输出：

```text
=== Static Member Demo ===
Max name length: 50
Next ID before any creation: 1
[construct] Employee #1 "Wang Wu" created. Active: 1
[construct] Employee #2 "Zhao Liu" created. Active: 2

Current active count: 2
Next ID to be assigned: 3

--- Enter demo_scope ---
[construct] Employee #3 "Zhang San" created. Active: 3
[construct] Employee #4 "Li Si" created. Active: 4
Inside scope, active count: 4
--- Leave demo_scope ---
[destruct]  Employee #4 "Li Si" destroyed. Active: 3
[destruct]  Employee #3 "Zhang San" destroyed. Active: 2

After demo_scope, active count: 2
Next ID to be assigned: 5
```

验证一下：ID 从 1 开始递增，不重复；进入 `demo_scope` 时 `active_count` 增到 4，出来后降到 2；`next_id_` 只增不减，出来后是 5 而不是 3——这正是我们想要的行为。

> **踩坑预警**：如果你的静态成员涉及拷贝或移动语义，一定要小心。默认的拷贝构造函数会逐成员拷贝，但它不会拷贝静态成员——因为静态成员不属于对象。如果你期望通过"拷贝一个对象来复制整个类的状态"，那这个设计就有问题了。静态成员的值不受任何单个对象的创建、拷贝或销毁影响（除非你在构造/析构函数里显式修改了它）。

## 动手试试

### 练习一：实现 ID 生成器

写一个 `UniqueIdGenerator` 类，它不存储任何对象数据，只通过静态成员提供一个全局递增的 ID。接口设计参考：`static int generate()` 每次调用返回一个新的唯一 ID，`static void reset(int start)` 允许重置起始值。写完后测试：调用三次 `generate()`，确认返回 1、2、3；然后 `reset(100)`，再调用两次，确认返回 100、101。

### 练习二：实例追踪器

写一个 `TrackedObject` 类，它同时维护两个计数器——`active_count`（当前存活对象数）和 `total_created`（总共创建过的对象数，只增不减）。在构造和析构函数中更新这两个计数器，并提供两个静态函数来查询。验证方法：创建 5 个对象，通过花括号作用域销毁其中 3 个，打印两个计数器的值——`active_count` 应该是 2，`total_created` 应该是 5。

## 小结

`static` 成员把数据和函数从对象级别提升到了类级别。静态成员变量在内存中只有一份，所有对象共享，必须在类外定义（C++17 的 `inline static` 除外）；静态成员函数没有 `this` 指针，只能访问静态成员，调用时用 `ClassName::function()` 的语法。`static constexpr` 提供了编译期常量的优雅写法，`static` + `private` 构造函数则是单例模式的基石。

下一章我们来看 `friend`——C++ 提供的"选择性打破封装"机制。
