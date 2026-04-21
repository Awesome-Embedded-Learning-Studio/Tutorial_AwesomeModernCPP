---
title: "RVO 与 NRVO：编译器的返回值优化"
description: "深入理解返回值优化机制，从 C++11 到 C++17 保证消除拷贝"
chapter: 0
order: 3
tags:
  - host
  - cpp-modern
  - intermediate
  - 移动语义
difficulty: intermediate
platform: host
cpp_standard: [11, 14, 17]
reading_time_minutes: 15
prerequisites:
  - "Chapter 0: 移动构造与移动赋值"
related:
  - "移动语义实战"
---

# RVO 与 NRVO：编译器的返回值优化

笔者以前写 C 的时候，有一个根深蒂固的习惯：绝不返回大结构体。因为按值返回一个结构体，意味着函数里构造一份，再把这份拷贝给调用者——对于那些动辄几百字节的结构体来说，这个开销在性能敏感的代码里完全不可接受。所以 C 程序员发明了各种绕法：传出指针参数、返回静态局部变量、用 malloc 让调用者自己 free……

C++ 有了拷贝构造和移动构造之后，按值返回大对象的代价已经大幅降低了——但编译器还能做得更好。它有一个"零成本"的秘密武器：**返回值优化**（Return Value Optimization，RVO）和**命名返回值优化**（Named Return Value Optimization，NRVO）。这两者的核心思路是：既然最终的对象要放在调用者的栈帧上，那何必在函数内部先构造一份再拷贝/移动过去？直接在调用者的空间里构造不就完了？这篇我们就把 RVO 和 NRVO 的机制、C++17 的保证消除、以及常见的失效场景全部讲清楚。

## RVO 和 NRVO 到底做了什么

假设我们有一个简单的 `Point` 类，带一个会打印日志的拷贝构造函数：

```cpp
#include <iostream>

struct Point {
    double x, y;

    Point(double x, double y) : x(x), y(y)
    {
        std::cout << "  构造 Point(" << x << ", " << y << ")\n";
    }

    Point(const Point& other) : x(other.x), y(other.y)
    {
        std::cout << "  拷贝 Point(" << x << ", " << y << ")\n";
    }

    Point(Point&& other) noexcept : x(other.x), y(other.y)
    {
        std::cout << "  移动 Point(" << x << ", " << y << ")\n";
    }
};
```

然后我们写两个工厂函数，一个返回临时对象，一个返回命名局部变量：

```cpp
// RVO 场景：返回 prvalue（临时对象）
Point make_point_rvo(double x, double y)
{
    return Point(x, y);   // 返回一个临时对象
}

// NRVO 场景：返回命名局部变量
Point make_point_nrvo(double x, double y)
{
    Point p(x, y);        // 命名局部变量
    // ... 可能还有一些对 p 的操作 ...
    return p;             // 返回命名变量
}
```

在没有优化的情况下，`make_point_rvo` 会先在函数内部构造 `Point(x, y)`，然后拷贝（或移动）到调用者的空间。`make_point_nrvo` 也是一样：构造 `p`，然后拷贝/移动 `p` 到调用者。但有了 RVO/NRVO 之后，编译器直接在调用者的栈帧上分配空间，让函数内部的构造操作直接发生在这个空间里——**根本不存在中间对象，拷贝和移动都无从谈起**。

我们来验证一下：

```cpp
int main()
{
    std::cout << "=== RVO ===\n";
    Point a = make_point_rvo(1.0, 2.0);

    std::cout << "\n=== NRVO ===\n";
    Point b = make_point_nrvo(3.0, 4.0);

    return 0;
}
```

用 GCC 编译，默认优化级别：

```bash
g++ -std=c++17 -Wall -Wextra -o rvo_test rvo_test.cpp
./rvo_test
```

输出：

```text
=== RVO ===
  构造 Point(1, 2)

=== NRVO ===
  构造 Point(3, 4)
```

每个 `Point` 只构造了一次——没有拷贝，没有移动。这就是 RVO/NRVO 在工作：编译器把构造操作直接"搬"到了调用者的空间里。

## 用编译器开关验证——关闭消除看看会发生什么

GCC 和 Clang 提供了一个编译器选项 `-fno-elide-constructors`，可以强制关闭拷贝消除。我们来看看关闭之后的行为：

```bash
g++ -std=c++17 -Wall -fno-elide-constructors -o rvo_no_elide rvo_test.cpp
./rvo_no_elide
```

输出变成了：

```text
=== RVO ===
  构造 Point(1, 2)
  移动 Point(1, 2)

=== NRVO ===
  构造 Point(3, 4)
  移动 Point(3, 4)
```

现在我们看到了"真实"的行为：每个函数都先构造了一次 `Point`，然后做了一次移动构造把它搬到调用者的空间。注意这里用的是移动而不是拷贝——因为 C++11 之后，当编译器遇到 `return local_var;` 的时候，会自动把 `local_var` 当作右值来处理（隐式移动），即使 `local_var` 在函数内部是一个左值。这是一个很重要的保证：即使拷贝消除没生效，你也至少能获得移动语义的性能。

## C++17 的保证消除——从"允许"到"必须"

在 C++17 之前，RVO 和 NRVO 都是编译器**被允许做但不是必须做**的优化。也就是说，标准说"编译器可以省略这次拷贝/移动"，但没有说"编译器必须省略"。在实践中，主流编译器在开启优化后基本都会做，但严格来说这不是保证。

C++17 改变了其中一种情况的规则：**当返回值是 prvalue（纯右值）时，拷贝消除变成了保证**。这不是一个可选的优化——它是语言的语义保证。这意味着 `return Point(x, y);` 这种写法在 C++17 中**绝对不会**触发拷贝或移动构造函数。

这个保证的底层原理是 C++17 对 prvalue 语义的重新定义。在 C++17 之前，prvalue 被理解为"临时对象"——函数返回 `Point(x, y)` 时，先创建一个临时 `Point` 对象，然后把它拷贝/移动到调用者的空间。C++17 之后，prvalue 被重新定义为"初始化的配方"——`Point(x, y)` 不再是一个对象，而是一组构造指令，告诉编译器"在这个位置用这些参数构造一个 `Point`"。既然 prvalue 不是对象，那就不存在"拷贝对象"这件事，拷贝消除自然就是保证的了。

```cpp
// C++17 之前：Point(x,y) 是一个临时对象
// C++17 之后：Point(x,y) 是一个"构造配方"
Point make_point(double x, double y)
{
    return Point(x, y);  // C++17 保证不触发拷贝/移动
}
```

> ⚠️ **踩坑预警**：C++17 的保证消除只适用于返回 **prvalue** 的场景——也就是 `return Type(args...);` 这种直接返回临时对象的写法。返回**命名局部变量**（NRVO）的情况仍然是"允许但非必需"的优化，C++17 并没有把 NRVO 也变成保证。所以 `return p;` 中的 `p` 是否被消除，仍然取决于编译器的实现。

## NRVO 什么时候失效

NRVO 虽然大部分时候都能生效，但有一些代码模式会让它失效。理解这些模式很重要——因为失效意味着你可能从"零成本"退化到"移动成本"，虽然不致命，但在性能敏感的热路径上可能成为瓶颈。

最典型的失效场景是**多个返回分支返回不同的命名对象**。编译器要做 NRVO，需要在调用者的空间里预先分配好内存，然后让函数内部的命名变量直接构造在这个空间里。但如果有两个不同的命名变量可能被返回，编译器就没法把两个变量都放在同一块空间里——它们各有各的地址。

```cpp
Point bad_nrvo(bool flag)
{
    Point a(1.0, 2.0);
    Point b(3.0, 4.0);
    if (flag) {
        return a;   // 可能阻止 NRVO
    }
    return b;       // 返回不同的命名对象
}
```

这种情况下，编译器没法确定 `a` 和 `b` 哪个会被返回，所以无法提前把其中一个放在调用者的空间里。结果就是：先正常构造 `a` 和 `b`，然后根据条件移动其中一个到返回值。你可以通过修改代码来恢复 NRVO：使用同一个命名变量，在不同分支里给它不同的值。

```cpp
Point good_nrvo(bool flag)
{
    Point result(0.0, 0.0);
    if (flag) {
        result = Point(1.0, 2.0);
    } else {
        result = Point(3.0, 4.0);
    }
    return result;   // NRVO 可以生效
}
```

另一个常见的失效场景是**返回函数参数**。NRVO 只针对函数内部的局部变量，函数参数是在调用者的栈帧上已经构造好的对象，编译器没法把它"搬"到返回值的空间里。

```cpp
Point return_param(Point p)
{
    // 对 p 做一些操作 ...
    return p;   // 无法 NRVO，但 C++11 会隐式移动
}
```

这里 `p` 是函数参数，不是局部变量，NRVO 不会生效。但好消息是 C++11 的隐式移动规则仍然适用——`return p;` 会把 `p` 当作右值，调用移动构造函数。所以你不会退化到拷贝，只是退化为移动。

还有一个虽然不是"失效"但值得一提的场景：**返回全局或静态变量**。这种情况下根本没有 NRVO 可言——全局/静态变量有固定的存储位置，不可能被搬到调用者的空间里。

```cpp
Point global_point(1.0, 2.0);

Point return_global()
{
    return global_point;   // 拷贝构造，没有 NRVO，也没有隐式移动
}
```

注意这里连隐式移动都不会发生——`global_point` 不是局部变量，C++11 的隐式移动规则不适用于它。所以这里确实是拷贝构造。如果你想要移动，得显式写 `return std::move(global_point);`。

## 用汇编看 RVO 的效果

理解原理很重要，但没有什么比直接看汇编更能说明问题了。我们来写两个函数，对比有无 RVO 时的编译输出。

```cpp
// rvo_asm.cpp -- 用 Compiler Explorer 查看汇编
// 建议在 https://godbolt.org 上查看完整汇编

struct Heavy {
    int data[256];
    Heavy(int v) { for (auto& d : data) d = v; }
    Heavy(const Heavy& o) { for (int i = 0; i < 256; ++i) data[i] = o.data[i]; }
    Heavy(Heavy&& o) noexcept { for (int i = 0; i < 256; ++i) data[i] = o.data[i]; }
};

Heavy with_rvo(int v)
{
    return Heavy(v);     // C++17 保证消除
}

Heavy without_rvo(Heavy h)
{
    return h;            // 参数返回，无法 NRVO
}
```

在 x86-64 上用 `g++ -std=c++17 -O2` 编译，`with_rvo` 的汇编大致如下：

```asm
with_rvo(int):
    ; 参数 v 在 edi 中
    ; 隐含的返回值地址在 rdi 中（调用者提供的空间）
    ; 直接在 rdi 指向的空间里填充数据
    mov     eax, edi
    movd    xmm0, eax
    pshufd  xmm0, xmm0, 0
    ; ... 用 SIMD 指令快速填充 ...
    ret
```

注意没有 `memcpy` 调用，没有额外的寄存器操作——函数直接在调用者提供的空间里工作。`without_rvo` 的汇编则明显不同：

```asm
without_rvo(Heavy):
    ; 必须把参数 h 移动到返回值空间
    mov     rax, rdi          ; 保存返回值地址
    mov     ecx, 256
    rep movsq                   ; memcpy 256 个四字（2048 字节）
    ret
```

这里有 `rep movsq`——一个 2048 字节的内存复制操作。这就是没有 RVO/NRVO 时的代价。对于大对象来说，这个差异可能是决定性的。

## RVO 和移动语义的关系

很多人会把 RVO 和移动语义搞混，觉得"反正有移动了，RVO 无所谓"。实际上它们是不同层次的优化，而且 RVO 优先级更高。

RVO/NRVO 是**消除**——连移动都省了。移动语义是**降级**——从深拷贝降级为浅层指针转移。两者的关系可以用一个简单的优先级链来表示：

```text
保证消除（C++17 prvalue） > NRVO（编译器优化）> 隐式移动（C++11）> 拷贝构造
```

编译器会从左到右尝试——先看能不能消除，不行就看能不能 NRVO，再不行就隐式移动，最后才用拷贝构造。所以你不需要担心"RVO 失效了是不是性能就崩了"——即使 RVO 失效，你还有移动语义兜底，比 C++03 时代的纯拷贝要好得多。

这也引出了一个非常重要的实战规则：**永远不要写 `return std::move(local_var);`**。

```cpp
Heavy bad_idea()
{
    Heavy h(42);
    return std::move(h);  // 阻止了 NRVO！
}

Heavy good_idea()
{
    Heavy h(42);
    return h;  // 可能触发 NRVO，退一步也是隐式移动
}
```

`return std::move(h);` 把 `h` 显式转换成右值引用，这意味着编译器必须使用移动构造——NRVO 的机会被你亲手掐掉了。而 `return h;` 让编译器有最大的自由度：它可以做 NRVO（直接消除），也可以做隐式移动（C++11 保证），无论哪种都比显式 `std::move` 好。

## 通用示例——字符串构建工厂

让我们把 RVO/NRVO 的知识应用到一个实际的场景中。假设我们在写一个配置文件解析器，需要一个工厂函数来构建配置字符串：

```cpp
#include <iostream>
#include <string>
#include <map>

using Config = std::map<std::string, std::string>;

/// @brief 将配置映射转换为可读的字符串
/// NRVO 场景：返回命名局部变量
std::string format_config_nrvo(const Config& cfg)
{
    std::string result;
    result.reserve(256);  // 预分配，避免多次扩容

    for (const auto& [key, value] : cfg) {
        result += key;
        result += " = ";
        result += value;
        result += "\n";
    }

    return result;  // NRVO：result 直接在调用者空间构造
}

/// @brief 构建一条简单的配置行
/// RVO 场景：返回 prvalue
std::string make_config_line(const std::string& key, const std::string& value)
{
    return key + " = " + value + "\n";  // C++17 保证消除
}

/// @brief 条件返回——NRVO 可能失效的例子
std::string format_with_default(
    const Config& cfg,
    const std::string& key,
    const std::string& default_value)
{
    auto it = cfg.find(key);
    if (it != cfg.end()) {
        return it->first + " = " + it->second + "\n";  // prvalue，保证消除
    }
    return key + " = " + default_value + " (default)\n";  // prvalue，保证消除
}

int main()
{
    Config cfg = {
        {"host", "localhost"},
        {"port", "8080"},
        {"debug", "true"},
    };

    std::string formatted = format_config_nrvo(cfg);
    std::cout << formatted;

    std::string line = make_config_line("timeout", "30");
    std::cout << line;

    std::string fallback = format_with_default(cfg, "timeout", "60");
    std::cout << fallback;

    return 0;
}
```

这三个函数分别展示了不同的返回场景。`format_config_nrvo` 返回一个经过复杂构建过程的命名变量，NRVO 可以让 `result` 直接在调用者的空间里增长——连一次字符串移动都省了。`make_config_line` 返回表达式结果（prvalue），C++17 保证消除。`format_with_default` 虽然有条件分支，但每个分支都返回 prvalue，所以仍然能享受保证消除。

## 动手实验——rvo_demo.cpp

我们来写一个完整的实验程序，把 RVO、NRVO、失效场景、以及 `std::move` 的误用全部跑一遍。

```cpp
// rvo_demo.cpp -- RVO / NRVO 完整演示
// Standard: C++17

#include <iostream>
#include <string>
#include <utility>

class Tracker
{
    std::string name_;

public:
    explicit Tracker(std::string name) : name_(std::move(name))
    {
        std::cout << "  [" << name_ << "] 构造\n";
    }

    Tracker(const Tracker& other) : name_(other.name_ + "_copy")
    {
        std::cout << "  [" << name_ << "] 拷贝构造\n";
    }

    Tracker(Tracker&& other) noexcept : name_(std::move(other.name_))
    {
        other.name_ = "(moved-from)";
        std::cout << "  [" << name_ << "] 移动构造\n";
    }

    ~Tracker()
    {
        std::cout << "  [" << name_ << "] 析构\n";
    }

    const std::string& name() const { return name_; }
};

/// @brief RVO：返回 prvalue
Tracker make_rvo(const std::string& name)
{
    return Tracker(name + "_rvo");
}

/// @brief NRVO：返回命名局部变量
Tracker make_nrvo(const std::string& name)
{
    Tracker t(name + "_nrvo");
    return t;
}

/// @brief 失效的 NRVO：两个返回分支返回不同命名对象
Tracker make_bad_nrvo(const std::string& name, bool flag)
{
    Tracker a(name + "_a");
    Tracker b(name + "_b");
    if (flag) {
        return a;
    }
    return b;
}

/// @brief 错误示范：用 std::move 阻止了 NRVO
Tracker make_bad_move(const std::string& name)
{
    Tracker t(name + "_badmove");
    return std::move(t);   // 显式移动，阻止 NRVO
}

/// @brief 返回函数参数——NRVO 不适用，但有隐式移动
Tracker return_param(Tracker t)
{
    return t;
}

int main()
{
    std::cout << "=== 1. RVO（返回 prvalue）===\n";
    {
        auto a = make_rvo("A");
        std::cout << "  结果: " << a.name() << "\n";
    }
    std::cout << '\n';

    std::cout << "=== 2. NRVO（返回命名变量）===\n";
    {
        auto b = make_nrvo("B");
        std::cout << "  结果: " << b.name() << "\n";
    }
    std::cout << '\n';

    std::cout << "=== 3. NRVO 失效（不同命名对象）===\n";
    {
        auto c = make_bad_nrvo("C", true);
        std::cout << "  结果: " << c.name() << "\n";
    }
    std::cout << '\n';

    std::cout << "=== 4. 错误：std::move 阻止 NRVO ===\n";
    {
        auto d = make_bad_move("D");
        std::cout << "  结果: " << d.name() << "\n";
    }
    std::cout << '\n';

    std::cout << "=== 5. 返回参数（隐式移动）===\n";
    {
        Tracker param("E_param");
        auto e = return_param(std::move(param));
        std::cout << "  结果: " << e.name() << "\n";
    }
    std::cout << '\n';

    std::cout << "=== 程序结束 ===\n";
    return 0;
}
```

编译运行：

```bash
g++ -std=c++17 -Wall -Wextra -O2 -o rvo_demo rvo_demo.cpp
./rvo_demo
```

预期输出：

```text
=== 1. RVO（返回 prvalue）===
  [A_rvo] 构造
  结果: A_rvo
  [A_rvo] 析构

=== 2. NRVO（返回命名变量）===
  [B_nrvo] 构造
  结果: B_nrvo
  [B_nrvo] 析构

=== 3. NRVO 失效（不同命名对象）===
  [C_a] 构造
  [C_b] 构造
  [C_a] 移动构造
  [C_b] 析构
  [(moved-from)] 析构
  结果: C_a
  [C_a] 析构

=== 4. 错误：std::move 阻止 NRVO ===
  [D_badmove] 构造
  [D_badmove] 移动构造
  [(moved-from)] 析构
  结果: D_badmove
  [D_badmove] 析构

=== 5. 返回参数（隐式移动）===
  [E_param] 构造
  [E_param] 移动构造
  [(moved-from)] 析构
  [E_param] 移动构造
  [(moved-from)] 析构
  结果: E_param
  [E_param] 析构
```

我们来仔细分析这些输出。第 1 和第 2 步是完美的情况——RVO 和 NRVO 都生效了，每个对象只构造了一次，没有任何拷贝或移动。第 3 步中 NRVO 失效了，因为两个分支返回不同的命名对象，编译器选择了隐式移动 `a`（`C_a` 变成了移动构造），`b` 则正常析构。第 4 步展示了 `return std::move(t)` 的后果——NRVO 被阻止，额外的移动构造发生了。第 5 步比较有意思：`return_param` 接收参数时发生了一次移动构造（`std::move(param)` 触发），返回参数时又发生了一次隐式移动——总共两次移动。

如果你用 `-fno-elide-constructors` 关闭消除重新编译，你会发现第 1 和第 2 步也出现了移动构造——这就是 C++17 保证消除和非保证优化的区别。第 1 步在 C++17 下是保证消除的，`-fno-elide-constructors` 对它其实无效（因为保证消除是语言语义，不是编译器优化开关能控制的）。但实际测试中，部分编译器版本的行为可能有所不同——如果好奇，可以在你的编译器上试试看。

## 实战指导

把理论落实到实际编码中，这里有几条简单的规则可以帮你最大化 RVO/NRVO 的收益。

第一，**按值返回，不要用输出参数**。`std::string build_message()` 比 `void build_message(std::string& out)` 更利于 RVO/NRVO。现代 C++ 的哲学是"写自然的代码，让编译器替你优化"，而按值返回正是最自然的写法。

第二，**不要写 `return std::move(local);`**。这条规则已经强调过好几次了，因为笔者见过太多"好心办坏事"的案例。`return local;` 让编译器有最大的优化空间——它可以做 NRVO、也可以做隐式移动。`return std::move(local);` 强制退化到移动构造，这是反优化。

第三，**保持返回路径简单**。如果你有多个返回分支，尽量让它们返回同一个命名变量，或者都返回 prvalue。避免不同分支返回不同的命名对象——这会阻止 NRVO。

第四，**性能敏感的代码要测量**。RVO/NRVO 是编译器的优化，不同编译器、不同版本、不同优化级别的行为可能不同。如果你真的在意某次返回的性能，写一个 benchmark 来测量，而不是靠猜测。

## 小结

RVO 和 NRVO 是现代 C++ 给我们的免费午餐——在不牺牲代码可读性的前提下，编译器把返回值的开销抹掉了。C++17 进一步把 prvalue 返回的消除提升为语言保证，让我们可以更安心地按值返回大对象。NRVO 虽然不是保证的，但主流编译器在开启优化后几乎都会做。记住最关键的一条规则：**返回局部变量时，直接 `return`，不要加 `std::move`**——让编译器做它最擅长的事。

下一篇我们进入移动语义实战，看看这些理论知识在 STL 容器和自定义类型中是如何发挥作用的。
