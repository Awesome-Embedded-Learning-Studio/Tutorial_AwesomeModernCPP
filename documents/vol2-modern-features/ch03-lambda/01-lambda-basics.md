---
chapter: 3
cpp_standard:
- 11
- 14
- 17
description: 从语法要素到 STL 配合，掌握 C++ lambda 表达式的核心用法
difficulty: intermediate
order: 1
platform: host
prerequisites:
- 'Chapter 0: 移动构造与移动赋值'
reading_time_minutes: 13
related:
- Lambda 捕获机制深入
- std::function、std::invoke 与可调用对象
tags:
- host
- cpp-modern
- intermediate
- lambda
title: Lambda 基础：匿名函数的优雅表达
---
# Lambda 基础：匿名函数的优雅表达

上一章咱们把 constexpr 的本事盘了个遍，这一章回到每天都要写的代码上。笔者在写排序逻辑的时候，一直觉得 C 的函数指针和 C++98 的仿函数(functor，就是重载了 `operator()` 的类)都有点别扭。函数指针要么定义在全局的作用域里，弄脏了命名空间，要么靠 `static` 成员函数和 `void*` 的上下文传来传去，两条路笔者都领教过。仿函数倒是能把状态封装进类里，可为了一个两行的比较逻辑，就郑重其事地定义一个完整的类，这个代价笔者觉得完全不值(哇! 最OOP的一集)。C++11 带来的 lambda 表达式把这件事变简单了。它本质上是一个在使用处就地定义的匿名函数对象，咱们不用跳到文件头部去声明，不用让编译器生成额外的符号，逻辑就写在调用点的旁边，读代码的人一眼就能看明白。

---

## Lambda 的语法拆解

Lambda 表达式的完整语法看起来有点唬人，咱们把它拆开来看，其实每一部分都很直觉：

```cpp
[capture](parameters) -> return_type { body }
```

咱们挨个看：`capture` 是捕获列表，它决定了 lambda 怎么访问外层作用域的变量。`parameters` 则和普通函数的参数列表完全一致。`-> return_type` 这里写的是尾置返回类型，C++11 里省略它是有条件的，条件满足了编译器才肯替咱们推导(下一节展开)。最后的 `body` 就是函数体。咱们从一个最简单的 lambda 开始逐步往上加料：

```cpp
// 什么都不做的 lambda,纯摆烂的
auto do_nothing = []() {};

// 简单返回一个值
auto forty_two = []() { return 42; };

// 带参数
auto double_it = [](int x) { return x * 2; };

// 实际使用：像普通函数一样调用
int result = double_it(21);  // result == 42
```

您会注意到，上面接收 lambda 的变量清一色用了 `auto`。原因在类型上：每个 lambda 表达式都会生成一个独一无二的、没有名字的类类型，也就是所谓的闭包类型(closure type)，您没办法直接写出这个类型的名字。`auto` 在这里就是最自然的选择。

---

## 返回类型推导

C++11 阶段的返回类型推导，条件是比较严格的。编译器肯替咱们自动推导的场合只有下面两种：

1. 函数体里只有一条 `return` 语句，或者
2. 所有 `return` 语句返回的表达式推导出相同的类型

条件满足了，咱们就可以把 `-> return_type` 省掉：

```cpp
// 自动推导为 int
auto square = [](int x) { return x * x; };

// 自动推导为 double（因为有 static_cast<double>）
auto divide = [](int a, int b) {
    return static_cast<double>(a) / b;
};
```

咱们的 lambda 体一旦写得复杂，比如多个分支走着不同的返回路径，编译器可能就推不动了，或者推出来的结果跟您预期的不一致。这时候您把返回类型显式写出来，是最稳妥的做法：

```cpp
auto classify = [](int x) -> int {
    if (x > 0) {
        return x * 2;
    } else if (x < 0) {
        return -x;
    }
    return 0;   // 如果没有这条，某些编译器可能报警告
};
```

笔者的建议是：**简单 lambda 省略返回类型，复杂的 lambda 就显式写出来。省略之后的代码更紧凑，但前提是咱们别让读代码的人猜半天返回值到底是什么类型。**

> 我遇到这种真需要我知道你要干啥的，却送我一个auto的，PR审查的时候就会：“嘻嘻，小朋友可不可以告诉我你的工位鸭”.png，问就是懒得拉你的分支下来看了。

---

## 在 STL 算法里用 lambda

咱们平时用 lambda，用得最多的场合就是给 STL 算法当谓词或者操作函数。谓词(predicate)是什么?就是拿到一个元素、回答是或者否的函数，`std::find_if` 和 `std::count_if` 吃的就是这类东西。以前您手里只有两个选项：传一个全局的函数指针，或者专门写了个仿函数类。现在直接在调用处写 lambda 就行了：

```cpp
#include <algorithm>
#include <vector>
#include <iostream>

void process_data() {
    std::vector<int> readings = {12, 45, 23, 67, 34, 89, 56};

    // 找出第一个超过阈值的读数
    auto it = std::find_if(readings.begin(), readings.end(),
                          [](int value) { return value > 50; });

    // 统计有多少个异常值
    int anomaly_count = std::count_if(readings.begin(), readings.end(),
                                     [](int value) { return value > 80; });
    std::cout << "Anomalies: " << anomaly_count << "\n";

    // 原地翻倍
    std::transform(readings.begin(), readings.end(), readings.begin(),
                  [](int value) { return value * 2; });

    // 自定义排序：降序
    std::sort(readings.begin(), readings.end(),
             [](int a, int b) { return a > b; });
}
```

您回想一下以前的做法：`is_above_threshold()` 之类的函数得定义在别处，读代码的人要翻半天才能对上号。现在判断条件就写在 `find_if` 的实参里，咱们扫一眼就知道它在筛什么。

咱们把 find_if 与 count_if 的逐格判断做成了动画，您可以一步一步看 find_if 停在哪、count_if 数到哪。

<Anim id="lambda-predicate-scan" />

---

## 捕获列表：lambda 怎么用上外部变量

默认的 lambda 是个封闭的东西，访问不到外层作用域里的局部变量。这是有意设计成这样的：不写捕获列表，lambda 体里能直接用的是参数、自己定义的局部量，以及全局与静态的变量，唯独外层的局部变量是想碰也碰不着的。您确实要用这些外层局部变量的时候，就得靠捕获列表做显式的声明：

```cpp
int threshold = 50;

// 编译错误：threshold 不在 lambda 的作用域内
// auto check = [](int value) { return value > threshold; };

// 值捕获：复制一份 threshold 到闭包对象中
auto by_value = [threshold](int value) { return value > threshold; };

// 引用捕获：直接引用外部的 threshold
auto by_ref = [&threshold](int value) { return value > threshold; };
```

值捕获发生在 lambda 创建的那一刻：变量被复制了一份，之后外部的修改影响不到闭包里的副本。引用捕获省掉了复制这一步，lambda 直接操作的就是外面的原始变量。两种方式各有适用的场合，也各有要小心的地方，笔者把细节留到下一篇《Lambda 捕获机制深入》专门展开。眼下您只需要认准一条：**只读不写的时候，值捕获是最安全的默认选择**。

常用的默认捕获写法也有两种：`[=]` 表示值捕获所有用到的外部变量，`[&]` 表示引用捕获所有用到的外部变量。两种写法图的都是一个省事，不过在生产代码里，笔者还是建议尽量显式列出要捕获的变量名，避免无意间捕获了不该捕获的东西。

```cpp
int a = 1, b = 2, c = 3;

// 全值捕获
auto sum_all = [=]() { return a + b + c; };  // 6

// 全引用捕获——可以修改外部变量
auto increment_all = [&]() { a++; b++; c++; };
increment_all();  // a=2, b=3, c=4

// 混合捕获：a 值捕获，b 引用捕获
auto mixed = [a, &b]() { return a + b; };
```

---

## Lambda 的类型——闭包类型

前面咱们提过，每个 lambda 表达式都会产生一个唯一的、匿名的类类型(闭包类型)。`operator()` 是它的成员函数，参数和返回值就是您在 lambda 里写的那些。标准只规定了行为，具体的实现由编译器决定。概念上您可以把 lambda 理解成编译器生成的这样一个类：

```cpp
// 你写的 lambda
auto greet = [](const std::string& name) -> std::string {
    return "Hello, " + name;
};

// 编译器概念上生成的类（简化版）
struct /* 编译器生成的唯一名字 */ {
    std::string operator()(const std::string& name) const {
        return "Hello, " + name;
    }
};
auto greet = /* 上面那个类的实例 */{};
```

实际实现里编译器会照着捕获列表给闭包类补上相应的数据成员，`mutable` 关键字决定的是 `operator()` 要不要 const。这些类型名怎么起的，咱们说了不算，各编译器自己拿主意：GCC 用的是 `_Z...` 编码，Clang 的则是 `$_0...` 一类，跨了编译器，一致性就没保证了。

所以您没法直接写出 lambda 的类型名：这个名字是编译器内部生成的，不同的编译器、不同的编译单元写出来都不一样。存放 lambda 的时候，咱们手上只有两条路。用 `auto` 的话，类型在编译期就定了。用 `std::function` 的话，靠的是类型擦除。类型擦除的意思，就是把各式各样的闭包类型藏到一个统一接口的后面，让具体类型对外不可见的一类做法。它有运行时的开销，细节咱们甩给第四篇。

这层对应关系做成了动画，您可以播放、暂停，也可以用步进键一步一步地看。例子里的 lambda 没写尾置返回类型，跟着的部件就是三个：捕获列表、参数列表、函数体，看它们各自落到闭包类的哪里：

<Anim id="lambda-anatomy" />

咱们拿模板参数传 lambda，是零开销抽象里常见的做法。编译器看得到完整的 lambda 类型，也就有了内联优化的机会：

```cpp
template<typename Func>
void call_func(Func f) {
    f();
}

call_func([]() { /* ... */ });  // 类型对编译器可见，可能内联
```

咱们要把话说准，上面说的只是"可能"：真的内联与否，取决于编译器的优化策略、lambda 的复杂程度、编译选项这些因素。但相比 `std::function` 的运行时间接调用，模板参数至少给了编译器优化的机会。

---

## 实战：事件处理系统

上一节存放 lambda 的两条路，到事件处理这里正好用上了一条：不同模块写出的回调，各自的闭包类型互不相同，咱们靠 `std::function` 把它们统一到同一个接口上，再一起放进分发器就行了。咱们这就搭一个简单的事件处理系统。注册回调、触发回调在实际项目里都是常见的需求，回调可能来自不同的模块，各有各的上下文：

```cpp
#include <cstdint>
#include <functional>
#include <array>
#include <iostream>

class EventDispatcher {
public:
    using Handler = std::function<void(uint32_t)>;

    void on_event(int id, Handler handler) {
        if (id >= 0 && id < static_cast<int>(handlers_.size())) {
            handlers_[id] = std::move(handler);
        }
    }

    void trigger(int id, uint32_t timestamp) {
        if (id >= 0 && id < static_cast<int>(handlers_.size()) && handlers_[id]) {
            handlers_[id](timestamp);
        }
    }

private:
    std::array<Handler, 8> handlers_;
};

// 使用示例
void setup_system() {
    EventDispatcher dispatcher;
    int press_count = 0;
    uint32_t last_press_time = 0;

    // 注册按键回调：引用捕获 press_count 和 last_press_time
    dispatcher.on_event(0, [&](uint32_t timestamp) {
        if (timestamp - last_press_time > 50) {   // 50ms 防抖
            press_count++;
            last_press_time = timestamp;
            std::cout << "Press #" << press_count
                      << " at " << timestamp << "ms\n";
        }
    });

    // 注册超时回调：值捕获 threshold
    uint32_t threshold = 1000;
    dispatcher.on_event(1, [threshold](uint32_t timestamp) {
        if (timestamp > threshold) {
            std::cout << "Timeout at " << timestamp << "ms\n";
        }
    });

    // 模拟事件触发
    dispatcher.trigger(0, 100);
    dispatcher.trigger(0, 160);   // 距上次 60ms，通过防抖
    dispatcher.trigger(0, 180);   // 距上次 20ms，被防抖过滤
    dispatcher.trigger(1, 1200);
}
```

咱们把演示程序放在了下面，您点「动手试一试」就能直接跑：

<OnlineCompilerDemo
  title="动手验证：lambda 事件处理系统"
  source-path="code/examples/vol2/33_event_dispatcher.cpp"
  description="在线运行事件处理系统。注意第三次按键（180ms）没有输出——距上次只有 20ms，被防抖逻辑过滤了。"
  run-options="-std=c++17"
  allow-run
/>

您可以看到，lambda 当回调用起来是很自然的。看按键回调那一段：`[&]` 把 `press_count` 和 `last_press_time` 都引了进来，函数体里直接改的就是它们，防抖的判断和计数都写在同一处。第三次触发是没有任何输出的，就是那行 50ms 的判断在起作用。

---

## C++14 的泛型 lambda

C++14 给 lambda 带来一个很实用的增强：参数类型可以写 `auto`。lambda 也就此变成了咱们手里的模板函数对象，编译器会为不同的参数类型各自生成一份 `operator()` 的实例：

```cpp
// 泛型 lambda：可以接受任何支持 operator+ 的类型
auto add = [](auto a, auto b) { return a + b; };

int xi = add(3, 4);              // int operator+(int, int)
double xd = add(3.5, 2.5);       // double operator+(double, double)
std::string xs = add(std::string("hello"), std::string(" world"));
```

咱们看编译器在背后生成的闭包类型大致长什么样：

```cpp
struct GenericClosure {
    template<typename T1, typename T2>
    auto operator()(T1 a, T2 b) const {
        return a + b;
    }
};
```

泛型 lambda 最能派上用场的地方就是通用算法和工具函数，您也不用再在 lambda 外面套一层模板函数了。剩下的内容，笔者留给后面的《泛型 Lambda 与模板 Lambda》一篇接着讲。

---

## 几个容易出问题的地方

### Lambda 体不要太长

Lambda 的优势就在就地定义、逻辑紧凑。咱们写的 lambda 一旦超过 5-7 行，就该考虑把它提取成命名函数或者仿函数了。真写长了，读的人就得在算法调用的参数列表里翻好几屏，把一段 lambda 和它的调用处来回对照着看，可读性就这么伤掉了。

### 引用捕获的生命周期陷阱

lambda 最常见的 bug 里有它一份：引用捕获的变量，在 lambda 执行的时候已经销毁了。典型的场景是咱们在函数里创建 lambda 再把它返回出去：

```cpp
// 危险！返回的 lambda 引用了局部变量 local
auto make_bad_lambda() {
    int local = 42;
    return [&local]() { return local; };   // local 在函数返回后销毁
}

// 安全：值捕获
auto make_safe_lambda() {
    int local = 42;
    return [local]() { return local; };    // lambda 持有副本
}
```

引用捕获本身是没有错的，但您必须保证被引用的对象活得比 lambda 久。在事件系统、异步回调这样的场景里，这个约束是特别容易被忽视的。

### 存储 lambda：`auto` 还是 `std::function`

除非您需要运行时多态(比如把不同类型的回调放进同一个容器)，否则就别用 `std::function` 来存 lambda 了。`auto` 拿到的是闭包类型本身，对象大小等于捕获的数据成员大小(无捕获的 lambda 通常只有 1 字节)，还给编译器留了内联优化的机会。`std::function` 内部用的是类型擦除和小对象优化(Small Buffer Optimization)两样机制，带来的是固定开销(32-64 字节)，调用时还要多付一层间接跳转的代价。

```cpp
// 编译期类型已知，大小=1字节（无捕获），可能内联
auto f = [](int x) { return x * 2; };

// 类型擦除，大小=32字节（libstdc++），运行时间接调用
std::function<int(int)> g = [](int x) { return x * 2; };
```

差异摆在性能关键的路径上，就可能很重要了，不过咱们也别急着过早优化：代码不在热点路径上的时候，`std::function` 的便利性可能反而更重要。

---

## 参考资源

- [Lambda expressions (C++11) - cppreference](https://en.cppreference.com/w/cpp/language/lambda)
- [C++14 generic lambdas - cppreference](https://en.cppreference.com/w/cpp/language/lambda#Generic_lambdas)
