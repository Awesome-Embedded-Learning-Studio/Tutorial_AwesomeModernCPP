---
title: "智能指针预告"
description: "了解为什么需要智能指针，初步认识 unique_ptr 如何自动管理内存，为卷二的深入学习埋下伏笔"
chapter: 4
order: 4
difficulty: beginner
reading_time_minutes: 10
platform: host
prerequisites:
  - "引用"
tags:
  - cpp-modern
  - host
  - beginner
  - 入门
  - 基础
cpp_standard: [11, 14, 17, 20]
---

# 智能指针预告

到目前为止，我们已经和裸指针打了好几章的交道了。指针确实强大，但也确实危险——每次 `new` 了一块内存，就得时刻记着 `delete` 掉它，中间任何路径漏掉了就是内存泄漏。现代 C++ 给出了一套系统性的解决方案：**智能指针（smart pointer）**。这一章我们先不深入，只是带你认识一下它解决什么问题、基本用法长什么样。真正的全面讲解放在卷二，和移动语义、RAII 一起系统展开。

> **学习目标**
> 完成本章后，你将能够：
>
> - [ ] 理解裸指针在内存管理上的三大经典问题
> - [ ] 掌握 RAII 的基本思想——构造时获取、析构时释放
> - [ ] 会用 `std::unique_ptr` 和 `std::make_unique` 进行基本的动态内存管理
> - [ ] 知道 `unique_ptr` 相比裸指针的零开销优势

## 裸指针的三宗罪

裸指针在内存管理上有三个经典问题（感觉有点控诉的味道了）

**内存泄漏**是最常见的情况：`new` 了但忘了 `delete`。更危险的是在某个异常退出的路径上忘了——正常流程下 `delete[]` 能执行到，但一旦错误条件触发、函数提前返回，内存就永远收不回来了。（欸我去，已经头大了）

```cpp
void process_data()
{
    int* data = new int[1000];

    if (some_error_condition()) {
        return;  // 直接 return 了，delete 呢？？？
    }

    delete[] data;
}
```

> 这里的关键是：**每一行可能提前退出的代码（return、throw）都是潜在的泄漏点**。在一个有十几个出口的函数里，你需要在每个出口前都确保资源被正确释放。哪天加了一个新的 return，忘写 delete 就又漏了。

**重复释放（double free）** 会导致程序直接崩溃——两个指针指向同一块内存，各自 `delete` 了一次。运行时通常报 `double free or corruption`，在多人协作的项目里尤其常见。

**悬空指针（dangling pointer）** 是 `delete` 之后继续通过原指针访问。这种 bug 最恶心：开发阶段可能完全不暴露（刚 `delete` 的内存内容往往还没被覆盖，`*p` 恰好还能读出原值），但放到生产环境、运行时间一长就会出随机问题，排查极其痛苦。

## RAII——一把钥匙开一把锁

三种问题的根源一样：**资源的获取和释放被分散在了代码的不同位置**。解决它的核心思想叫 **RAII（Resource Acquisition Is Initialization）**——在构造函数里获取资源，在析构函数里释放资源。C++ 保证对象离开作用域时析构函数**一定会被调用**，不管正常退出还是异常退出，这个保证由**栈展开（stack unwinding）**机制提供。

你可以把它想象成一把自动归还的钥匙：拿到钥匙（构造时获取），走出房间（离开作用域），钥匙自动归还（析构时释放）。

```cpp
#include <iostream>

struct IntHolder
{
    int* ptr;

    explicit IntHolder(int val) : ptr(new int(val))
    {
        std::cout << "分配内存，值 = " << *ptr << "\n";
    }

    ~IntHolder()
    {
        std::cout << "释放内存，值 = " << *ptr << "\n";
        delete ptr;
    }
};

void demo()
{
    IntHolder holder(42);
    std::cout << "内部值: " << *holder.ptr << "\n";
    if (true) {
        return;  // 即使提前 return，holder 的析构函数也会被调用
    }
}
```

运行结果：

```text
分配内存，值 = 42
内部值: 42
释放内存，值 = 42
```

即便函数提前 `return` 了，`holder` 的析构函数还是被调用了。这就是 RAII 的威力——你不需要在每个出口手动写 `delete`，C++ 的作用域规则会帮你自动管理。

> 注意 `explicit` 关键字——它防止了 `IntHolder holder = 42;` 这种隐式转换。对于单参数构造函数，加 `explicit` 是好习惯。

## unique_ptr——独占所有权的智能指针

理解了 RAII，智能指针就很好理解了——它就是帮你把 `new` 和 `delete` 包装成 RAII 的工具类。最基础也最常用的就是 `std::unique_ptr`，核心语义是**独占所有权**：一块内存同一时刻只能被一个 `unique_ptr` 持有，不能复制，但可以**移动**。

### 创建与基本操作

C++14 引入了 `std::make_unique`，这是创建 `unique_ptr` 的推荐方式。我们用一个自定义类型来演示完整生命周期：

```cpp
#include <iostream>
#include <memory>
#include <string>

struct Player
{
    std::string name;
    int level;

    Player(const std::string& n, int lv) : name(n), level(lv)
    {
        std::cout << name << " 登场！\n";
    }

    ~Player() { std::cout << name << " 退场。\n"; }

    void show_status() const
    {
        std::cout << name << " Lv." << level << "\n";
    }
};

int main()
{
    {
        auto hero = std::make_unique<Player>("Alice", 5);
        hero->show_status();   // -> 访问成员，和裸指针一样
        std::cout << (*hero).name << "\n";  // * 解引用也行
    }
    // hero 在这里离开作用域，自动 delete

    std::cout << "继续执行...\n";
    return 0;
}
```

运行结果：

```text
Alice 登场！
Alice Lv.5
Alice
Alice 退场。
继续执行...
```

"Alice 退场。"出现在"继续执行..."之前——析构函数在花括号作用域结束时自动调用了。`unique_ptr` 的基本操作就三个：`*p` 解引用，`p->member` 访问成员，`p.get()` 获取裸指针（传给 C 接口时有用）。

> 为什么推荐 `make_unique` 而不是 `unique_ptr<int>(new int(42))`？第一更简洁，不需要写 `new`。第二在涉及函数参数组合时直接写 `new` 可能因求值顺序未指定而导致泄漏，这个细节卷二会展开。

### 不能复制，只能移动

`unique_ptr` **不能复制**——`auto p2 = p1;` 会直接编译报错。这是刻意的设计：允许复制意味着两个 `unique_ptr` 指向同一块内存，离开作用域时就会重复 delete。如果你需要转移所有权，用 `std::move`：

```cpp
auto p1 = std::make_unique<int>(42);
auto p2 = std::move(p1);  // 所有权从 p1 转移到 p2
// p1 变成 nullptr，p2 持有那块内存
```

`std::move` 的详细机制会在卷二系统讲解，现在只需记住它是转移 `unique_ptr` 所有权的标准方式。

### 零开销——安全不花性能代价

`unique_ptr` 在运行时**没有额外的性能开销**——内部就存了一个指针，没有虚函数，编译器优化后生成的代码和手动 `new/delete` 几乎完全一致。现代 C++ 有一条明确规则：**能用 `unique_ptr` 就不要用裸 `new/delete`**。

## 实战：裸指针 vs unique_ptr

我们把内存泄漏场景用两种方式实现。核心对比很直观：裸指针版本在错误路径上泄漏，`unique_ptr` 版本则自动免疫。

```cpp
#include <iostream>
#include <memory>

void raw_version(bool error)
{
    int* data = new int[100];
    data[0] = 42;

    if (error) {
        return;  // 泄漏！忘记 delete[]
    }

    delete[] data;
}

void smart_version(bool error)
{
    auto data = std::make_unique<int[]>(100);
    data[0] = 42;

    if (error) {
        return;  // 不泄漏——析构函数自动调用 delete[]
    }
}

int main()
{
    std::cout << "=== 错误场景 ===\n";
    raw_version(true);    // 泄漏 400 字节
    smart_version(true);  // 安全

    std::cout << "=== 正常场景 ===\n";
    raw_version(false);   // 正常释放
    smart_version(false); // 正常释放
    return 0;
}
```

想亲自验证泄漏？用 AddressSanitizer 编译：`g++ -Wall -Wextra -std=c++17 -fsanitize=address -g unique_ptr_intro.cpp`，ASan 会在程序结束时指出裸指针版本泄漏的内存大小和分配位置。这也是日常开发排查内存问题的标配工具。

## 更多的智能指针——留到卷二

智能指针家族还有 `shared_ptr`（共享所有权，引用计数）和 `weak_ptr`（弱引用，打破循环引用）没出场。`unique_ptr` 也还有自定义删除器等高级用法。这些都需要移动语义和右值引用作为基础，都是卷二的核心内容。现在记住两件事就够了：第一，**尽量不直接写 `new` 和 `delete`**，首选 `std::make_unique`；第二，`unique_ptr` 是零开销的——不会让程序变慢，但能让它免于一大类内存 bug。

## 小结

- 裸指针的三大内存问题：**泄漏**（忘了 delete）、**重复释放**（double free）、**悬空指针**（use-after-free），根源是资源的获取和释放被分散在不同位置
- **RAII** 利用 C++ 析构函数的自动调用机制，将资源的生命周期绑定到对象的作用域
- `std::unique_ptr` 提供独占所有权的智能指针，离开作用域时自动释放内存，不能复制但可以移动
- `std::make_unique<T>(args...)` 是创建 `unique_ptr` 的推荐方式，比直接写 `new` 更安全也更简洁
- `unique_ptr` 相比裸指针是**零开销**的，没有理由不在新代码中使用它

### 常见错误

| 错误 | 原因 | 解决方法 |
|------|------|----------|
| 尝试复制 `unique_ptr` | 独占语义禁止拷贝 | 用 `std::move()` 转移所有权 |
| `make_unique` 在 C++11 下不可用 | C++14 才引入 | 升级标准或用 `unique_ptr<T>(new T(...))` |
| `unique_ptr<int[]>` 用 `*p` 解引用 | 数组版不支持 `*` | 用 `p[i]` 下标访问或 `p.get()` |

## 练习

### 练习一：改造裸指针程序

下面这段代码在 `early_exit` 为 `true` 时会泄漏。请改写为 `unique_ptr` 版本，确保任何路径下都不泄漏。提示：只需把 `Sensor* s = new Sensor(1)` 换成 `auto s = std::make_unique<Sensor>(1)`，删掉 `delete s`，其他不动。

```cpp
struct Sensor
{
    int id;
    Sensor(int i) : id(i) { std::cout << "Sensor " << id << " 初始化\n"; }
    ~Sensor() { std::cout << "Sensor " << id << " 关闭\n"; }
    void read() { std::cout << "Sensor " << id << " 读取数据\n"; }
};

void use_sensor(bool early_exit)
{
    Sensor* s = new Sensor(1);
    s->read();
    if (early_exit) { return; }
    s->read();
    delete s;
}
```

### 练习二：识别内存泄漏模式

下面这段代码有两个泄漏点（`choice == 1` 和 `choice == 2` 两个分支各一个），想想用 `unique_ptr` 包装 `a` 和 `b` 之后，提前 return 和 throw 还是问题吗？

```cpp
void process(int choice)
{
    int* a = new int(10);
    int* b = new int(20);
    if (choice == 1) { return; }
    delete a;
    if (choice == 2) { throw std::runtime_error("error"); }
    delete b;
}
```

---

> **下一站**：到这里，指针和引用这一章我们就全部走完了。从裸指针的基本概念，到指针运算和数组的关系，再到引用和智能指针的预告——我们建立起了对 C++ 内存操作的完整认知框架。接下来进入第五章，认识数组和字符串，看看 C++ 提供了哪些比 C 风格数组更安全、更好用的工具。
