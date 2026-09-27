---
chapter: 12
cpp_standard:
- 11
- 14
- 17
- 20
description: 掌握 new/delete 使用与陷阱，理解 RAII 核心地位
difficulty: intermediate
order: 2
platform: host
prerequisites:
- 内存布局
reading_time_minutes: 10
tags:
- cpp-modern
- host
- intermediate
- 进阶
title: 动态内存管理
---
# 动态内存管理：new 和 delete 背后做了什么

上一章咱们把程序的内存空间拆成了栈、堆、静态区、代码段四大块，搞清楚了数据"住在哪里"和"活多久"。但有一个悬念没有展开：堆上的动态内存到底怎么管？`new` 和 `delete` 背后做了什么？为什么前面几乎所有章节都在念叨"用智能指针，别裸写 `delete`"？

这一章咱们来正面回答。动态内存是 C++ 给咱们的最大自由度：可以在运行时按需申请任意大小的内存，完全不受栈空间限制。但这份自由也带来了最沉重的责任：每一块 `new` 出来的内存都必须被正确地 `delete`，否则就是泄漏；每一次 `delete` 都必须对应正确的 `new`，否则就是未定义行为。

## 从 new/delete 说起

咱们看 C++ 用 `new` 和 `delete` 接替了 C 的 `malloc` 和 `free`。`new` 做两件事：先找一块内存（底层通常走 `malloc` 这条路），再**在上面调用构造函数**；`delete` 反过来，**先调用析构函数，再归还内存**。

> 您可以粗暴的认为，**粗暴的认为！**，new 是 **`malloc + T()`**的组合，delete是 **`~T() + free()`**的组合
> 区别我记得有语法大佬科普过，这里因为是入门，不说了，吓跑萌新不行。
>
> Q: 这里穿插一个好玩的话题，new出来的内存需要判定NULL嘛？
>
> A: 一眼就是 C 出身的，因为我就是问过这个问题，`new` 失败是不返回空指针的，它的选择是直接抛一个叫做 `std::bad_alloc`的臭鸡蛋给您, 接住混蛋！这里的 `bad_malloc` 就是异常那一章刚学的知识，这儿就用上了。哈哈！

咱们分配单个对象时，对于类类型，`new` 会自动调用构造函数，`delete` 会自动调用析构函数：

```cpp
class Sensor {
public:
    Sensor()  { std::cout << "Sensor 初始化\n"; }
    ~Sensor() { std::cout << "Sensor 关闭\n"; }
    void read() { std::cout << "读取数据\n"; }
};

Sensor* s = new Sensor();  // 输出: Sensor 初始化
s->read();                  // 输出: 读取数据
delete s;                   // 输出: Sensor 关闭
```

咱们分配数组时必须用 `new[]`，释放时必须用对应的 `delete[]`：

```cpp
int* arr = new int[10];
for (int i = 0; i < 10; ++i) {
    arr[i] = i * i;
}
delete[] arr;  // 注意：是 delete[]，不是 delete
```

`delete` 和 `delete[]` 不匹配是经典中的经典错误。用 `delete` 去释放 `new[]` 分配的数组，行为是未定义的。对于 `int` 这类基本类型，某些平台可能"碰巧"不出问题；但对于类类型的数组，`delete`（不带 `[]`）只会调用第一个元素的析构函数，其余元素的析构函数根本不会被调用，如果析构函数负责释放嵌套的动态内存，后果就是资源泄漏。咱们要养成雷打不动的习惯：`new` 对 `delete`，`new[]` 对 `delete[]`，宁可多写一个 `[]`，也不要心存侥幸。

## 内存泄漏——不报错的失败

内存泄漏到底有多阴险？咱们来看一个最简单的场景：

```cpp
void leak_example()
{
    int* p = new int(42);
    if (some_condition()) {
        return;  // 提前返回，delete 永远不会执行
    }
    delete p;
}
```

咱们看函数在中途 `return` 了，`delete` 被跳过，那 4 个字节的内存就永远丢失了。但更阴险的场景是异常：代码在 `new` 和 `delete` 之间抛出了异常，控制流直接跳转到 `catch` 块，`delete` 被完全绕过。这种泄漏在测试阶段往往不暴露，但在生产环境中，某个罕见条件触发异常，内存就开始一点一点流失。

### 用 AddressSanitizer 抓泄漏

好消息是，现代编译器给咱们提供了强大的运行时检测工具。AddressSanitizer（ASan）是 GCC 和 Clang 内置的内存错误检测器，编译时加上 `-fsanitize=address` 就能自动检测泄漏、越界、use-after-free 等问题。

```cpp
// leak_demo.cpp
// 编译: g++ -std=c++17 -O0 -fsanitize=address -g leak_demo.cpp
#include <iostream>

void create_leak()
{
    int* p = new int(42);
    std::cout << "分配了内存，值为: " << *p << "\n";
    // 故意不 delete
}

int main()
{
    create_leak();
    std::cout << "函数返回了，但内存没有释放\n";
    return 0;
}
```

咱们编译运行后，ASan 在程序退出时报告：

```text
=================================================================
==120445==ERROR: LeakSanitizer: detected memory leaks

Direct leak of 4 byte(s) in 1 object(s) allocated from:
    #0 0x6ffc2d12d2a1 in operator new(unsigned long) (/usr/lib/libasan.so.8+0x12d2a1)
    #1 0x55f4a03ff1da in create_leak() /tmp/leak_demo.cpp:7
    #2 0x55f4a03ff2b6 in main /tmp/leak_demo.cpp:14
    #3 0x6ffc2c827780  (/usr/lib/libc.so.6+0x27780)
    #4 0x6ffc2c8278b8  __libc_start_main (/usr/lib/libc.so.6+0x278b8)
    #5 0x55f4a03ff0f4  _start (/tmp/leak_asan+0x10f4)

SUMMARY: AddressSanitizer: 4 byte(s) leaked in 1 allocation(s).
=================================================================
```

`==120445==` 是进程号，地址、路径在您机器上肯定不同，每次运行也会变。真正要读的是 `leak_demo.cpp:7` 这样的线索：哪一行分配的内存、从哪里调用出去的，栈帧从上往下就是调用链。

ASan 会显著降低程序运行速度（通常慢 2-5 倍）并增加内存占用（大约 3-5 倍），所以只应在调试和测试阶段使用。生产构建中一定要去掉 `-fsanitize=address`。另外，ASan 与某些并行调试工具可能冲突，咱们遇到奇怪的段错误时，试试去掉 ASan 看看是不是工具本身的问题。

MSVC的朋友？别折腾了，不支持的。要搞左转考虑一下clang-cl哦（斜眼笑）。

## RAII 把堆资源绑到栈上

咱们看裸用 `new`/`delete` 的核心问题在于：必须手动保证每一块内存都被恰好释放一次，无论是正常返回、提前 `return` 还是异常退出。C++ 给出的答案是 RAII——Resource Acquisition Is Initialization。核心思路就是把堆资源的生命周期绑定到一个栈对象上：在构造函数里 `new`，在析构函数里 `delete`，利用栈离开作用域时析构函数自动调用的机制来保证释放。

```cpp
class AutoInt {
public:
    explicit AutoInt(int value) : ptr_(new int(value)) {}
    ~AutoInt() {
        delete ptr_;
        std::cout << "AutoInt 析构，内存已释放\n";
    }

    // 禁止拷贝（后面会解释原因）
    AutoInt(const AutoInt&) = delete;
    AutoInt& operator=(const AutoInt&) = delete;

    int& operator*() { return *ptr_; }
private:
    int* ptr_;
};

void safe_function()
{
    AutoInt value(42);
    std::cout << *value << "\n";
    risky_operation();  // 即使这里抛出异常
    // 析构函数也会在栈展开时被自动调用
}
```

`AutoInt` 的析构函数保证了 `delete` 一定会被执行，不管 `safe_function` 是正常返回还是因为异常退出。但现实中咱们不会为每种类型都手写一个 `AutoXxx` 包装类，标准库已经替咱们做好了，而且做得更完善。这就是智能指针。

## 智能指针——标准库替咱们写好的 RAII

现实中咱们不会为每种类型都手写一个 `AutoXxx` 包装类——标准库在 `<memory>` 里早就备好了现成的：`std::unique_ptr` 独占所有权（不能拷贝、只能移动），`std::shared_ptr` 共享所有权（引用计数，拷贝加一、析构减一、归零释放），`std::weak_ptr` 只观察不持有，专门破循环引用。最常用的 `unique_ptr` 长这样：

```cpp
auto p = std::make_unique<int>(42);   // C++14 的 make_unique
std::cout << *p << "\n";              // 42
// 离开作用域，p 析构，delete 自动执行
```

`new` 和 `delete` 都不见了，这就是 RAII 换来的写法。这三种指针每一种都有不少讲究：零开销凭什么、控制块长什么样、循环引用怎么破、自定义删除器怎么玩。咱们不在本卷展开，卷二第一章有整整六篇专门拆它们。眼下记一条行为准则就够用：**能用 `unique_ptr` 就不裸 `new`，能用 `make_*` 就不写 `new`**。

## placement new——在指定地址构造对象

咱们看普通的 `new` 会自动在堆上找内存，而 `placement new` 则是只负责调用构造函数，地址完全由您来指定。

```cpp
#include <new>  // placement new 需要这个头文件

alignas(int) unsigned char buffer[sizeof(int)];
int* p = new (buffer) int(42);  // 在 buffer 上构造一个 int
std::cout << *p << "\n";        // 42

// 不能用 delete！因为内存不是 new 分配的
p->~int();  // 显式调用析构函数（对于 int 是空操作）
```

`placement new` 在上位机开发中用得不多，但在嵌入式系统中非常有价值：它允许咱们在预分配的内存池或共享内存中构造 C++ 对象。用它要注意三件事。缓冲区对齐必须满足对象要求，`alignas` 保证了这一点。内存不是 `new` 分配的，不能调用 `delete`，只能显式调用析构函数。而显式调用析构函数这件事在 C++ 中非常罕见，几乎只出现在这个场景中。

## 动手实践——裸指针 vs 智能指针

咱们把前面的内容整合到一个完整示例中：裸指针、智能指针和自定义删除器的对比。最后一段的删除器原理卷二再拆，这里先看现象。

```cpp
// dynamic.cpp
// 编译（泄漏检测）:
//   g++ -std=c++17 -O0 -fsanitize=address -g dynamic.cpp -o dynamic
// 编译（正常）:
//   g++ -std=c++17 -O0 -g dynamic.cpp -o dynamic

#include <iostream>
#include <memory>

void raw_pointer_demo()
{
    std::cout << "=== 裸指针版本 ===\n";
    int* p = new int(42);
    std::cout << "值: " << *p << "\n";

    int* arr = new int[5];
    for (int i = 0; i < 5; ++i) { arr[i] = i * 10; }

    // 模拟提前返回（取消注释以观察泄漏）:
    // if (true) return;

    delete p;
    delete[] arr;
    std::cout << "手动释放完成\n";
}

void smart_pointer_demo()
{
    std::cout << "\n=== 智能指针版本 ===\n";
    auto p = std::make_unique<int>(42);
    std::cout << "值: " << *p << "\n";
    auto arr = std::make_unique<int[]>(5);
    for (int i = 0; i < 5; ++i) { arr[i] = i * 10; }
    // 不管以何种方式离开（正常返回、提前 return、异常）
    // 析构函数都会自动释放内存
    std::cout << "离开作用域时自动释放\n";
}

void custom_deleter_demo()
{
    std::cout << "\n=== 自定义删除器 ===\n";
    auto deleter = [](int* ptr) {
        std::cout << "自定义删除器被调用，值为: " << *ptr << "\n";
        delete ptr;
    };
    std::unique_ptr<int, decltype(deleter)> p(new int(99), deleter);
    std::cout << "值: " << *p << "\n";
}

int main()
{
    raw_pointer_demo();
    smart_pointer_demo();
    custom_deleter_demo();
    std::cout << "\n程序结束\n";
    return 0;
}
```

咱们把完整代码放在下面，点"动手试一试"直接跑（编译条件已设为 -O0）：

<OnlineCompilerDemo
  title="动手实践：dynamic.cpp"
  source-path="code/examples/vol1/28_new_delete.cpp"
  description="在线运行裸指针、智能指针、自定义删除器三段对比。您想看 ASan 报泄漏：解开提前 return 的注释，在编译条件里加上 -fsanitize=address。"
  run-options="-O0 -std=c++17"
  allow-run
/>

咱们要是取消 `raw_pointer_demo` 里的提前返回注释，ASan 会报告两个泄漏点共 24 字节。而 `smart_pointer_demo` 无论如何都不会泄漏，这就是 RAII 的安全感。

## 练习

### 练习 1：换上 unique_ptr，然后被卡住

请您把下面的代码换成 `std::unique_ptr`：`logger` 用 `make_unique` 创建。换到 `backup` 那行您会被卡住——`unique_ptr` 不可拷贝，这正是它在替您追问：`backup` 到底有没有所有权？没有的话，该用什么方式访问对象？答案卷二揭晓，您可以先带着问题过去。

```cpp
class Logger {
public:
    explicit Logger(const std::string& name) : name_(name) {}
    ~Logger() { std::cout << "Logger(" << name_ << ") 析构\n"; }
    void log(const std::string& msg) { std::cout << "[" << name_ << "] " << msg << "\n"; }
private:
    std::string name_;
};

int main()
{
    Logger* logger = new Logger("app");
    logger->log("程序启动");
    Logger* backup = logger;  // 别名，不拥有
    delete logger;
    // backup 此刻是悬空指针！
    return 0;
}
```

### 练习 2：AutoInt 换成 unique_ptr

请您把前面 `safe_function` 里手写的 `AutoInt` 换成 `std::unique_ptr`：功能保持不变，代码少了多少行？再把 `risky_operation()` 换成真的抛异常，验证析构依然会被调用。
