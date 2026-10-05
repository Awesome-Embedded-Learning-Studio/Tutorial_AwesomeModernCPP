---
chapter: 3
cpp_standard:
- 11
- 14
- 17
- 20
description: 值捕获、引用捕获、初始化捕获的语义与陷阱
difficulty: intermediate
order: 2
platform: host
prerequisites:
- 'Chapter 3: Lambda 基础'
reading_time_minutes: 15
related:
- 泛型 Lambda 与模板 Lambda
tags:
- host
- cpp-modern
- intermediate
- lambda
title: Lambda 捕获机制深入
---
# Lambda 捕获机制深入

上一篇咱们把 lambda 的基本语法过了一遍，值捕获、引用捕获也各自摸了一下，细节笔者留到了这一篇。`[=]` 和 `[&]` 捕获的到底是什么？值捕获复制进去的，是不是就是变量的一份拷贝？引用捕获在底层是不是只存了一个指针？咱们把这仨问题掰开，答案都藏在编译器生成的代码里。这一篇咱们从头到尾拆清楚，也把会在运行时出事的写法认个明白。

---

## 值捕获——复制一份到闭包对象中

值捕获的语义非常直白：lambda 创建的那一刻，被捕获的变量就被复制了一份，当作闭包类型的成员变量存起来。之后您在外面怎么改，都影响不到 lambda 内部的副本。

```cpp
void demo_value_capture() {
    int threshold = 100;

    // threshold 被复制到闭包对象中
    auto is_high = [threshold](int value) {
        return value > threshold;
    };

    threshold = 200;             // 修改外部变量
    bool result = is_high(150);  // true，lambda 里的 threshold 还是 100 副本
}
```

从编译器的角度看，咱们上面写的这个 lambda，大致会被翻译成这样的闭包类型：

```cpp
struct ClosureType {
    int threshold;  // 被捕获的变量变成了成员

    bool operator()(int value) const {
        return value > threshold;
    }
};

auto is_high = ClosureType{100};  // 构造时复制 threshold
```

咱们来注意那个 `const`：值捕获进来的成员，在 `operator()` 里默认是 `const` 的，您没法修改它们。要是您确实需要在 lambda 内部改捕获进来的副本，那就得把 `mutable` 关键字请出来了：

```cpp
int counter = 0;

// 编译错误：counter 在 lambda 内是 const int
// auto bad = [counter]() { counter++; };

// 加 mutable：允许修改 lambda 内部的副本
auto make_counter = [counter]() mutable {
    return ++counter;   // 修改的是闭包对象自己的 counter，不是外部的
};

std::cout << make_counter() << "\n";  // 1
std::cout << make_counter() << "\n";  // 2
std::cout << counter << "\n";         // 0——外部的 counter 没有被碰过
```

咱们加上 `mutable`，等于跟编译器打了招呼：这个 lambda 的 `operator()` 不再是 `const` 的，每次调用都可能动到闭包对象内部的状态。所以每次调用 `make_counter()` 都会递增，因为闭包对象自己维护了一份独立的状态，外部的 `counter` 从头到尾没被碰过。

---

## 引用捕获——存储原始变量的地址

引用捕获的语义也不难猜：编译器在闭包类型里存的，就是被捕获变量的指针（引用在底层实现上和指针基本等价）。这一点咱们可以用 `sizeof` 验证：引用捕获的闭包对象，大小就等于指针的大小，到了 64 位系统上就是 8 字节。lambda 内部对捕获变量的读写，动的其实就是外面的原始变量。

```cpp
void demo_ref_capture() {
    int sum = 0;

    auto accumulate = [&sum](int value) {
        sum += value;   // 直接修改外部的 sum
    };

    accumulate(10);
    accumulate(20);
    accumulate(30);
    // sum == 60
}
```

咱们照样把它翻译成闭包类型：

```cpp
struct ClosureType {
    int& sum;  // 存储的是引用

    void operator()(int value) const {
        sum += value;  // 通过引用修改外部变量
    }
};
```

这里有个很有意思的细节：`operator()` 明明是 `const` 的，咱们却通过 `sum` 改掉了外部变量。这是因为引用本身（存下来的地址）是 `const` 的，您不能让它改指另一个对象，可它绑定的那个对象的值，您随便改。这跟咱们熟悉的 `int* const ptr` 是一个道理：指针本身动不了，`*ptr` 那边却随您折腾。

咱们光说机制不算数，验证程序笔者就放在下面了，lambda 和上面手写的 `ClosureType` 并排跑，您点“动手试一试”直接看：

<OnlineCompilerDemo
  title="动手验证：引用捕获的 const 语义"
  source-path="code/examples/vol2/58_ref_capture_const.cpp"
  description="在线对比 lambda 与手写闭包类型：两种写法都把外部变量累加到 60，const operator() 照改不误；引用捕获的闭包大小就是一个指针的 8 字节。"
  run-options="-std=c++17"
  allow-run
/>

引用捕获最大的好处是零拷贝：像 `std::vector`、`std::string` 这样的大对象，复制一次的代价不小，引用捕获直接把它省掉了。您要是只读不写，按引用捕获再配上 `const` 就够了，一个字节的复制都不用付。可风险也跟着来了：**您必须保证，被引用的变量活得比 lambda 久**。

咱们把两种捕获方式的内存布局放在一起对比，悬垂引用（dangling reference）的风险也就看得见了：说的就是引用还指着，对象却已经销毁了。

![值捕获与引用捕获的闭包对象内存布局](./02-lambda-capture-layout.drawio)

---

## 默认捕获——`[=]` 和 `[&]` 的隐患

要捕获的变量一多，咱们挨个列就列不过来了。所以 C++ 干脆给了两种默认捕获：`[=]` 表示用到的外部变量统统按值捕获，`[&]` 表示大家都按引用来。

```cpp
void demo_default_capture() {
    int a = 1, b = 2, c = 3;

    // 全值捕获
    auto sum = [=]() { return a + b + c; };   // 6

    // 全引用捕获
    auto increment = [&]() { a++; b++; c++; };
    increment();   // a=2, b=3, c=4
}
```

您也可以在默认捕获的基础上给个别变量换个方式，咱们管它叫混合捕获：

```cpp
void demo_mixed_capture() {
    int threshold = 100;
    int count = 0;
    double factor = 1.5;

    // 默认值捕获，但 count 按引用捕获
    auto process = [=, &count](int value) {
        if (value > threshold) {
            count++;
            return static_cast<int>(value * factor);
        }
        return value;
    };
}
```

咱们用着确实是方便，可 `[=]` 和 `[&]` 也藏着几个不显眼的问题。咱们拿 `[=]` 捕获 `this` 这件事举例。您可能听过一种说法，说 `[=]` 是不捕获 `this` 的。欸等等，这话放到 C++20 之前就不对了：`[=]` 其实是可以隐式捕获 `this` 的。经典的误会，就是从这里长出来的。您以为复制进来的是成员变量的值，其实进来的只有 `this` 指针。lambda 内部走 `this->member` 访问的，仍然是原始对象的成员。到了 C++20，这个写法被正式弃用了，编译器会冲您发一句警告，行为倒是暂时兼容。您想显式捕获就写 `[=, this]` 或 `[=, *this]`。

这句警告长什么样，您点下面的“动手试一试”就能亲眼看到，诊断区里躺着一句 `-Wdeprecated` 的警告，程序倒是照常跑出 42。您要是想看 C++17 下它安静的样子，咱们就点“在 Godbolt 中打开”，把选项里的 `-std=c++20` 改成 `c++17` 再编译一次，警告就没了。

<OnlineCompilerDemo
  title="动手验证：C++20 弃用 [=] 隐式捕获 this"
  source-path="code/examples/vol2/57_default_capture_this.cpp"
  description="在线编译（GCC 15.2，-std=c++20）即见 -Wdeprecated 警告与修改建议；[=]、[=, this]、[*this] 三种写法都返回 42——警告只关乎写法，不改变行为。"
  run-options="-std=c++20"
  allow-run
/>

笔者的建议是：**生产代码里尽量把要捕获的变量名一个个写出来**，`[=]` 和 `[&]` 咱们能不用就不用。显式列出来的好处，是代码审查的时候一眼就能看出 lambda 依赖了哪些外部状态，也不会顺手把不该捕获的东西裹进来。

> 当然，咱们不是说默认捕获绝对不能写，前提是您的代码足够平凡简单。要不然您根本不知道 lambda 最后拿到了什么，出了问题，您都不好往回查。

---

## C++14 初始化捕获——lambda 拥有自己的状态

C++14 引入了初始化捕获（init capture），咱们有时候也管它叫广义 lambda 捕获，英文的原名是 generalized lambda capture。咱们要做的，只是在捕获列表里写 `name = expression`：`name` 是一个新的变量名，`expression` 则是拿去初始化它的表达式。这个新变量完全是闭包对象自己的，和外部变量的值没有关系：

```cpp
void demo_init_capture() {
    int base = 10;

    // 捕获 base + 5 的结果，而不是 base 本身
    auto lam = [value = base + 5]() {
        return value * 2;   // value == 15
    };
}
```

初始化捕获最有用的场景是**移动捕获**，咱们可以把 `std::unique_ptr`、`std::thread` 这类只能移动的类型移进闭包对象：

```cpp
#include <memory>

auto make_handler() {
    auto ptr = std::make_unique<int>(42);

    // 把 unique_ptr 移入 lambda
    return [p = std::move(ptr)]() {
        return *p;   // p 是 lambda 独占的
    };
}
```

在 C++11 里您要实现同样的效果就得手写一个仿函数类，把 `unique_ptr` 写成它的成员变量。到了 C++14 的初始化捕获，这件事就写得非常自然了。

另一个常见的用法，是咱们拿初始化捕获来替代 `mutable` 计数器，语义倒是清楚多了：

```cpp
// C++11 风格：需要 mutable
int x = 0;
auto counter_old = [x]() mutable { return ++x; };

// C++14 风格：初始化捕获，语义更明确
auto counter_new = [count = 0]() mutable { return ++count; };
```

第二个版本的好处是 `count` 完全属于 lambda 自己、跟外部变量 `x` 不相干，您光看名字也能看出这是个独立计数器。

---

## C++17 的 `*this` 捕获——按值捕获整个对象

在成员函数里写 lambda 的时候，您想捕获当前对象，传统上咱们写 `[this]`。可 `[this]` 捕获的是指针：lambda 的生命周期要是比对象本身长，您手里就只剩一个悬垂的 `this` 指针了。C++17 引入了 `[*this]`，它把整个对象按值捕了进来，在闭包类型里存一份对象的副本：

```cpp
#include <iostream>
#include <string>
#include <functional>

class Sensor {
    std::string name_;
    int reading_ = 0;

public:
    explicit Sensor(std::string name) : name_(std::move(name)) {}

    std::function<int()> make_reader() {
        // [*this]：复制整个 Sensor 对象到闭包中
        // 即使原始 Sensor 被销毁，lambda 仍然安全
        return [*this]() mutable {
            return ++reading_;
        };
    }

    std::function<int()> make_reader_unsafe() {
        // [this]：只存指针，对象销毁后变成悬垂指针
        return [this]() {
            return ++reading_;   // 危险！
        };
    }
};

void demo_star_this() {
    std::function<int()> reader;

    {
        Sensor s("temperature");
        reader = s.make_reader();      // [*this]：安全
        // reader_unsafe = s.make_reader_unsafe();  // [this]：危险
    }
    // s 已经销毁

    std::cout << reader() << "\n";     // 安全：lambda 持有 s 的副本
    std::cout << reader() << "\n";     // 2
}
```

`[*this]` 的代价是复制整个对象。对象要是很大（装着 `std::vector`、大 `std::array` 的类型），复制开销就不小了。不过对象小的时候（配置对象、值对象这类），您多付一份复制换来安全，笔者认为很值。

**注意**：咱们用 `[*this]` 有个前提，当前 lambda 所在的位置得是能解引用 `this` 的成员函数。到了静态成员函数或者非成员函数里，咱们是用不了 `[*this]` 的。

---

## 悬垂引用与生命周期

捕获机制最常见、也最让人头疼的 bug 来源就是生命周期问题：lambda 还活着呢，它引用的变量却已经没了。咱们直接看几个容易出问题的写法。

### 返回引用捕获的 lambda

```cpp
// 经典陷阱：返回引用了局部变量的 lambda
auto make_dangling() {
    int count = 0;
    return [&count]() { return ++count; };
    // count 在函数返回后销毁，lambda 持有的是悬垂引用
}

auto bad = make_dangling();
// bad() 是未定义行为！
```

修复的办法很直白，咱们用值捕获或者初始化捕获顶替引用捕获：

```cpp
auto make_safe() {
    int count = 0;
    return [count]() mutable { return ++count; };    // 值捕获：安全
}

auto make_safe2() {
    return [count = 0]() mutable { return ++count; }; // 初始化捕获：更清晰
}
```

### 循环中的引用捕获

咱们在异步编程和事件系统里就特别容易写出这个错误：

```cpp
#include <vector>
#include <functional>

std::vector<std::function<void()>> handlers;

void demo_loop_trap() {
    for (int i = 0; i < 5; ++i) {
        // 错误：所有 lambda 引用同一个 i，i 已随循环结束销毁
        handlers.push_back([&i]() {
            std::cout << i << " ";   // 此时再调用是悬垂引用，实测常见输出垃圾值
        });
    }

    handlers.clear();

    for (int i = 0; i < 5; ++i) {
        // 正确：每个 lambda 有自己的 i 副本
        handlers.push_back([i]() {
            std::cout << i << " ";   // 输出 0 1 2 3 4
        });
    }
}
```

咱们把循环里 `[&i]` 和 `[i]` 的差别拍成了动画，`[&i]` 的五个闭包引用的是同一个 `i`，循环结束后您再调用，输出的就是垃圾值，`[i]` 给每个闭包复制了自己的 `i`，输出的是 0 1 2 3 4。

<Anim id="lambda-loop-capture" />

### 捕获 `this` 的隐患

咱们把同一个需求的三种写法摆在一起，从最悬的到最稳的：

```cpp
class Device {
    std::string name_ = "sensor";

public:
    auto get_handler() {
        // 如果 Device 对象在 lambda 执行前被销毁，this 就悬垂了
        return [this]() { return name_; };
    }

    // 更安全的做法：捕获需要的成员，而不是 this
    auto get_handler_safe() {
        return [name = name_]() { return name; };
    }

    // C++17 最安全：按值捕获整个对象
    auto get_handler_safest() {
        return [*this]() { return name_; };
    }
};
```

---

## Lambda 对象的大小分析

底层怎么存搞清楚了，lambda 对象的大小也就好算了：它就是所有被捕获变量的大小之和，中间可能还得垫上一些对齐的填充。标准的 lambda 没有虚函数表指针，闭包类型就是一个普通的类类型。咱们可以用 `sizeof` 验证：

```cpp
#include <iostream>

void demo_closure_size() {
    int a = 0;
    double b = 0.0;
    int& ref = a;

    auto no_capture = []() {};
    auto capture_int = [a]() { return a; };
    auto capture_ref = [&a]() { return a; };
    auto capture_both = [a, &b]() { return a + b; };

    std::cout << "no_capture:    " << sizeof(no_capture) << " bytes\n";
    // 通常 1 byte（空类特例）

    std::cout << "capture_int:   " << sizeof(capture_int) << " bytes\n";
    // 通常 4 bytes（一个 int）

    std::cout << "capture_ref:   " << sizeof(capture_ref) << " bytes\n";
    // 通常 8 bytes（一个指针，64 位系统）

    std::cout << "capture_both:  " << sizeof(capture_both) << " bytes\n";
    // 通常 16 bytes（int + double 引用/指针，考虑对齐）
}
```

验证程序笔者就放在下面了，您点“动手试一试”直接跑（64 位系统上）：

<OnlineCompilerDemo
  title="动手验证：lambda 闭包的大小"
  source-path="code/examples/vol2/34_lambda_capture_size.cpp"
  description="在线查看闭包大小：无捕获 1 字节，值捕获 int 4 字节，引用捕获是指针 8 字节，混合捕获考虑对齐到 16 字节。"
  run-options="-std=c++17"
  allow-run
/>

无捕获 lambda 的 1 字节，咱们顺便多问一句：为什么偏偏不是 0？因为 C++ 不允许大小为 0 的对象，不然数组里各个元素的地址就没办法区分了。引用捕获存的是指针，到了 64 位系统上就占 8 字节。

您要是把 lambda 存进 `std::function`，占的地方就不只这些了：`std::function` 对象本身通常就有 32-64 字节，上一篇提过的小对象优化（SBO）就藏在这里面，还要再加上类型擦除的管理开销。所以那一篇的“存储 lambda”小节，咱们才劝您能用 `auto` 就用 `auto`。

---

## 性能考量——何时内联，何时不能

lambda 的快慢，和它的捕获方式、存储方式都密切相关，咱们分两档看。当 lambda 以编译期已知的类型（`auto` 或模板参数）被调用时，编译器能看到完整的闭包类型和 `operator()` 实现，可以放心地把它整个内联掉。这时候值捕获和引用捕获的差异就基本归零了，即使值捕获多了一次复制，编译器在优化后通常能把这次复制的开销消掉。

可您要是把 lambda 存进了 `std::function`，情况就变了。类型擦除给它加了一层间接调用，编译器看不穿这层间接、也没法内联了。而且一旦捕获的内容超出 SBO 缓冲区的大小，堆分配就来了。

```cpp
#include <vector>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <functional>

void benchmark_lambda_styles() {
    std::vector<int> data(1'000'000);
    int threshold = 50;

    // 风格 1：auto + 算法模板参数——完全内联
    auto start = std::chrono::high_resolution_clock::now();
    auto count1 = std::count_if(data.begin(), data.end(),
                               [threshold](int x) { return x > threshold; });
    auto end = std::chrono::high_resolution_clock::now();
    std::cout << "auto lambda: "
              << std::chrono::duration_cast<std::chrono::microseconds>(end - start).count()
              << " us\n";

    // 风格 2：std::function——有间接调用开销
    std::function<bool(int)> pred = [threshold](int x) { return x > threshold; };
    start = std::chrono::high_resolution_clock::now();
    auto count2 = std::count_if(data.begin(), data.end(), pred);
    end = std::chrono::high_resolution_clock::now();
    std::cout << "std::function: "
              << std::chrono::duration_cast<std::chrono::microseconds>(end - start).count()
              << " us\n";
}
```

所以优化打开的情况下（-O2/-O3），`auto` 版本的速度普遍压 `std::function` 一头，咱们实测下来倍数从两三倍到近十倍不等。咱们看编译器，越新的编译器把内联和向量化做得越激进，`auto` 那一侧的速度优势就越大。上面的示例代码是 100 万个元素，基准程序把数据量放大到了 1000 万，程序就放在下面了，-O3 也已经替您配好了，您点“动手试一试”直接跑：

<OnlineCompilerDemo
  title="动手测量：auto 直传 vs std::function 的开销"
  source-path="code/examples/vol2/55_lambda_perf_benchmark.cpp"
  description="1000 万元素的 count_if（GCC 15.2，-O3）：笔者一次运行 auto 6.3 ms、std::function 20.9 ms（3.3 倍）；一亿次迭代里值捕获与无捕获 269 ms vs 268 ms，复制开销被优化整个消掉。绝对数字随环境浮动，倍数才是重点。"
  run-options="-O3 -std=c++17"
  allow-run
/>

您跑出来的数字会随机器浮动，方向却是稳的。`std::function` 的间接调用挡住了内联，慢出一截是躲不掉的。值捕获多付的那次复制，优化之后基本就被消掉了——一亿次迭代里两边的毫秒数几乎一样，这就是最直接的证据。答案也就不难下了：您不需要运行时多态的时候，拿模板或 `auto` 来传递 lambda 就够了。

---

## 选择哪种捕获方式——决策指南

捕获方式的选择，笔者把它收拢成几条简单的规则：

小型的不可变数据（`int`、`float`、简单结构体）咱们直接值捕获，它是最安全的默认选择：lambda 不依赖外部状态，线程安全、也没有生命周期的问题。轮到大型的对象（`std::vector`、`std::string`），咱们就得问一句：lambda 是只读、还是想把它带走。只读的场合咱们按引用捕获再配上 `const`，就是前面引用捕获小节说好的零拷贝方案。想带走的，咱们就回到初始化捕获的现场、用 `name = std::move(obj)` 把它移进闭包。至于要在 lambda 内部修改的外部变量（累加器、状态更新），咱们用引用捕获最自然，不过您得确保变量的生命周期足够长。

您在成员函数里写 lambda，只要它不逃出对象的生命周期，用 `[this]` 是方便的。您要是拿不准 lambda 会不会比对象活得久，咱们就换 `[*this]`（C++17），或者用初始化捕获把需要的成员变量单独收进来。到了生产代码里，还是默认捕获小节咱们说过的建议：变量名显式列出来。显式的代码让 code review 好做，也少了很多意外的捕获。

---

## 在线运行

文章里的捕获示例笔者打包了一个在线版本，您可以对比不同捕获方式的效果：

<OnlineCompilerDemo
  title="Lambda 捕获机制：值捕获、引用捕获与闭包大小"
  source-path="code/examples/vol2/09_lambda_capture.cpp"
  description="在线运行并对比值捕获、引用捕获、mutable 和初始化捕获的行为差异。"
  allow-run
/>

## 参考资源

- [Lambda capture - cppreference](https://en.cppreference.com/w/cpp/language/lambda#Lambda_capture)
- [C++14 generalized lambda capture](https://en.cppreference.com/w/cpp/language/lambda#Captures)
- [C++17 capture *this](https://en.cppreference.com/w/cpp/language/lambda#Lambda_capture)
