---
chapter: 0
cpp_standard:
- 11
- 14
- 17
description: 深入理解返回值优化机制，从 C++11 到 C++17 保证消除拷贝
difficulty: intermediate
order: 4
platform: host
prerequisites:
- 'Chapter 0: 移动构造与移动赋值'
reading_time_minutes: 19
related:
- 移动语义实战：标准库容器与性能实测
tags:
- host
- cpp-modern
- intermediate
- 移动语义
title: RVO 与 NRVO：编译器的返回值优化
---
# RVO 与 NRVO：编译器的返回值优化

笔者见过不少从写 C 过来的朋友，尤其是写单片机 C、从 RAM 贼小的片子过来的，编程的时候绝不返回大结构体，也就是绝不写 `struct X GetSth(...)` 这样的东西（栈一不小心就打爆了）。您按值返回一个结构体，等于函数里头构造了一份、又拷贝了一份送出去，开销就这么来了。对动辄几百字节的结构体来说，性能敏感的代码里完全不能接受。所以当年大家发明了各种绕法：**传出指针参数、返回静态局部变量、用 malloc 让调用者自己 free……**

C++ 有了拷贝构造和移动构造之后，按值返回大对象的代价已经大幅降低了，不过编译器还能做得更好。上一篇结尾咱们也打过招呼：它能让函数返回大对象的代价直接归零。它用的手段有两个：一个的名字叫**返回值优化**（Return Value Optimization），平时咱们叫它 RVO。另一个的名字叫**命名返回值优化**（Named Return Value Optimization），咱们平时叫它 NRVO。

咱们把两者的思路翻成大白话：反正最终的对象要落在调用者的栈帧上，咱们何必让函数在内部另造一份、再拷贝或者移动过去？直接在调用者的空间里构造，不就完了？就这么个意思<RefLink :id="1" preview="cppreference Copy elision — RVO / NRVO / mandatory elision in C++17" />。

> 范围咱们交代一句：返回 prvalue（直接写 `return Point(x, y);`）的那一半，第一篇的 `make_tracker("D")` 已经替咱们验证过了，输出里只有一次构造的日志。返回命名局部变量时编译器还能省到什么程度，咱们当时把这个问题留到了今天，本篇要干的正是这件事。

## RVO 和 NRVO 到底做了什么

咱们请出一个简单的 `Point` 类，它的构造、拷贝、移动三个函数都会各打一行日志，谁出了场，您在输出里一眼就能看见。

```cpp
#include <iostream>

struct Point {
    double x, y;

    Point(double x, double y) : x(x), y(y)
    {
        // 我知道好像在这里塞中文可能会造成问题，但是怕啥，demo而已
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

接着咱们写两个工厂函数：一个返回的是临时对象，一个返回的是命名局部变量。

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

咱们照最坏的设想走一遍：什么优化都没有的时候，咱们在函数内部构造临时对象，再把它拷贝（或者移动）到调用者的空间，这就是 `make_point_rvo` 的路径。`make_point_nrvo` 走的路也一样：构造好了 `p`，再把 `p` 拷贝或移动到调用者的空间。那有了 RVO/NRVO 之后呢？编译器直接在调用者的栈帧上把空间分出来，让函数内部的构造操作就发生在这块空间里。中间的对象根本不存在，拷贝和移动也就无从谈起了。

咱们把两种情况画成图摆在一起：

![您看 RVO 与 NRVO 的拷贝消除对比](./04-rvo-nrvo-elision.drawio)

咱们把程序跑起来看看：

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

下面就是完整的验证程序，`Point` 类和两个工厂函数咱们都塞进了同一个源文件，您点「动手试一试」直接跑：

<OnlineCompilerDemo
  title="动手验证：rvo_nrvo_probe.cpp"
  source-path="code/examples/vol2/17_rvo_nrvo_probe.cpp"
  description="在线验证 RVO 与 NRVO：留意两段各打了几行日志、有没有拷贝或移动。下一节您还会改编译选项再看一遍。"
  run-options="-std=c++17"
  allow-run
/>

您数一数输出：每个 `Point` 只打了一行构造日志，拷贝和移动的日志一行都没有。RVO/NRVO 正是在这儿干活的，编译器把构造操作直接放进了调用者的空间。

## 用编译器开关验证——关闭消除看看会发生什么

GCC 和 Clang 提供了一个编译器选项 `-fno-elide-constructors`，咱们可以用它强制关闭拷贝消除。您回到上面那个 demo 里，点开它的编译选项，您把 `-fno-elide-constructors` 追加进去再跑一遍，输出立刻变了样：`=== RVO ===` 那段的输出纹丝不动，还是只有一行构造的日志。`=== NRVO ===` 那段则多出了一行 `移动 Point(3, 4)`，消除被咱们关掉之后，命名局部变量的返回退回了一次移动构造。

这里有个细节值得您停下来看：RVO 那部分偏偏**没有变化**。`-fno-elide-constructors` 都加上了，`make_point_rvo` 仍然只构造了一次，也没有出现移动的日志。为什么开关管不着它？因为 C++17 把返回 prvalue 的拷贝消除写成了语言语义的保证，也就是咱们第一篇提过的保证消除（guaranteed copy elision），不是编译器想开就开、想关就关的优化。真正被开关影响的是 NRVO：`make_point_nrvo` 从零成本退化到了一次移动构造。保证消除的来龙去脉，咱们后面单独一节展开。

您再留意一个点：NRVO 退化之后走的路是移动而非拷贝。这是 C++11 定下的隐式移动规则在起作用：编译器把 `return local_var;` 里的 `local_var` 当作右值处理，哪怕它在函数内部明明是个如假包换的左值。这是很实在的一个保证：就算拷贝消除没生效，您也至少能拿到移动语义的性能。

> 您要是想看"全退化"的样子，也就是连 RVO 也一起退化成移动的样子，可以用 C++14 的模式编译：`g++ -std=c++14 -fno-elide-constructors`。在 C++14 的规则下，开关对 RVO 和 NRVO 的消除都生效、两个函数都会多出移动操作。

## C++17 的保证消除——从"允许"到"必须"

在 C++17 还没来的日子里，RVO 和 NRVO 都是编译器**被允许做、但不是必须做**的优化。标准的话是"编译器可以省略这次拷贝/移动"，"必须省略"倒是没提。实践中主流编译器开了优化基本都会做，不过咱们把话说严格些，那也只是惯例而非保证。

C++17 把其中一种情况改成了硬性的：**返回值是 prvalue（纯右值）的时候，拷贝消除从"允许"变成了"必须"**<RefLink :id="2" preview="Richard Smith, P0135R1: Wording for guaranteed copy elision through simplified value categories, WG21, 2016" />。它不再是可选的优化，而是语言语义的一部分。所以 `return Point(x, y);` 这样的写法，在 C++17 里是**绝对不会**触发拷贝或移动构造函数的，您可以放心写。

凭什么敢这么保证？咱们得往下挖一层，看 prvalue 的语义被重新定义成了什么。咱们把时间拨回 C++17 以前，prvalue 被理解成的是"临时对象"：函数返回 `Point(x, y)` 的时候，就得创建一个临时的 `Point`，再把它拷贝或移动到调用者的空间。到了 C++17，prvalue 变成了"初始化的配方"：`Point(x, y)` 不再是一个对象了，而是一组构造指令，它告诉编译器的东西变成了"在这个位置用这些参数构造一个 `Point`"。您体会一下前后的差别。既然 prvalue 根本不是什么对象、也就谈不上"拷贝对象"这回事，您想拷贝一个不存在的东西，连下手的对象都找不到，所以消除天生就是保证的一部分。

```cpp
// C++17 之前：Point(x,y) 是一个临时对象
// C++17 之后：Point(x,y) 是一个"构造配方"
Point make_point(double x, double y)
{
    return Point(x, y);  // C++17 保证不触发拷贝/移动
}
```

不过适用范围咱们要划清楚：保证消除只管返回 prvalue 的场景，也就是 `return Type(args...);` 一类直接返回临时对象的写法。返回命名局部变量的 NRVO，在 C++17 里仍然是"允许但非必需"的优化。所以 `return p;` 里的 `p` 到底消不消除，还是得看编译器的实现。NRVO 会在什么情况下失效，咱们接着往下捋。

## NRVO 什么时候失效

NRVO 大部分的时候都能生效，可有些代码模式偏偏会让它失效。咱们值得把这些模式认全：失效意味着您可能从零成本退化到一次移动的成本。失效倒是不致命，但搁在性能敏感的热路径上一次一次地攒、就可能攒成瓶颈。

咱们最典型的失效场景，是**多个返回分支返回不同的命名对象**。咱们回想 NRVO 要做的事：在调用者的空间里提前把内存分好，让函数内部的命名变量直接构造在这块内存上。不过要是 `a` 和 `b` 两个变量都有可能被返回，编译器就犯难了：它俩各有各的地址，谁也没法塞进调用者给返回值留的那块空间。

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

编译器定不了 `a` 和 `b` 哪个会被返回，就没法提前把哪一个放进调用者的空间。结果就是 `a` 和 `b` 都正常构造，返回的时候按条件移动其中一个，返回值里躺的是移动构造的结果。您想恢复 NRVO 也有办法：改用同一个命名变量的写法，在不同分支里给它不同的值。

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

另一个常见的失效场景，咱们看**返回函数参数**。NRVO 只认函数内部的局部变量，参数一传进来就已经是构造好的对象，编译器没法把它"搬"到返回值的空间里。

```cpp
Point return_param(Point p)
{
    // 对 p 做一些操作 ...
    return p;   // 无法 NRVO，但 C++11 会隐式移动
}
```

`p` 的身份是参数而不是局部变量，NRVO 也就不会为它生效了。不过有个好消息：C++11 的隐式移动规则照样管用，`return p;` 里的 `p` 会被当作右值，去调的还是移动构造函数。所以您退化到的是移动而非拷贝。

还有一个值得认下的场景，严格说倒是不算失效，咱们也顺手把它认了：**返回全局或静态变量**。全局、静态变量有固定的存储位置，从头到尾不可能被搬进调用者的空间，NRVO 在这儿本来就没有用武之地。

```cpp
Point global_point(1.0, 2.0);

Point return_global()
{
    return global_point;   // 拷贝构造，没有 NRVO，也没有隐式移动
}
```

您猜怎么着，这次连隐式移动的待遇都没有。`global_point` 的身份也不是局部变量，C++11 的隐式移动规则管不到它，所以返回它走的确实是拷贝构造。您真想移动它，那就得显式写 `return std::move(global_point);` 了。

## 用汇编看 RVO 的效果

道理讲到了这儿，咱们再补一级证据：直接看汇编。咱们写两个函数，一个吃得着 RVO 的好处、另一个吃不着，把两者的编译输出摆在一起比。

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

咱们在 x86-64 上用 `g++ -std=c++17 -O2` 编译（GCC 16.1.1），`with_rvo` 的汇编长这样：

```asm
// GCC 16.1.1, -O2 -std=c++17
with_rvo(int):
    movd    %esi, %xmm1         ; 参数 v 加载到 SSE 寄存器
    movq    %rdi, %rax          ; rdi = 调用者提供的返回值地址
    leaq    1024(%rdi), %rdx    ; 循环终止地址 = 起始 + 1024
    pshufd  $0, %xmm1, %xmm0   ; 将 v 广播到 xmm0 的全部 4 个 int
.L2:
    movups  %xmm0, (%rax)      ; 每次写入 16 字节
    addq    $32, %rax
    movups  %xmm0, -16(%rax)
    cmpq    %rdx, %rax
    jne     .L2
    movq    %rdi, %rax
    ret
```

咱们从上往下读。`movq %rdi, %rax` 里的 `rdi` 是一个隐含参数，装着调用者为返回值准备好的空间地址，真正的入参 `v` 反而走在 `%esi` 里。拿到了地址，函数就直接在调用者的内存上开工了。

咱们接着看循环。`pshufd $0, %xmm1, %xmm0` 把 `v` 广播到 SSE 寄存器的全部 4 个 lane，一个寄存器里装的就成了 4 个相同的 `int`。到了循环体这儿，`.L2` 每转一圈写的都是 32 字节，靠的是两条 `movups` 各写 16 字节、`addq $32, %rax` 再把指针往前挪一格。终点由 `leaq 1024(%rdi), %rdx` 定在起始地址加 1024 的地方。咱们拿 1024 除以 32、正好 32 圈，`data[256]` 的 1024 字节就这么填满了。整个函数里找不到 `memcpy` 的调用，也没有额外的内存拷贝。构造和返回合二为一了。

咱们再来看 `without_rvo`，它的汇编就短多了，也直白多了：

```asm
// GCC 16.1.1, -O2 -std=c++17
without_rvo(Heavy):
    movl    $1024, %edx
    jmp     memcpy@PLT
```

您数一下就会发现指令一共就两条：`movl $1024, %edx` 把字节数装进 `%edx`，`jmp memcpy@PLT` 干的是尾调用（tail call）`memcpy` 的活。编译器把"复制 1024 字节"的整件事交给了 libc，自己就不动手了。没有 RVO/NRVO 的时候，代价就摆在眼前了：一次实打实的 1024 字节内存复制。咱们算一下 `int data[256]` 的体量：256 × 4 = 1024 字节。您把这样的返回放到热点路径上，它就真的可能成为瓶颈了。

> 更早的 GCC（比如 15）的做法，跟咱们现在看到的不一样，那时候的编译器会把这段拷贝内联成 `rep movsq` 指令、在函数体里直接循环搬字节。GCC 16 改成了调 `memcpy`，形式变了，本质倒是没有变：搬的都是那 1024 字节。有 RVO 的时候，这 1024 字节的搬运根本不会发生。

## RVO 和移动语义的关系

咱们身边有不少人把 RVO 和移动语义搞混，觉得"反正都有移动了、RVO 无所谓"。咱们把话分清楚：RVO/NRVO 做的是**消除**，连移动那一步都省掉了。移动语义做的才是**降级**，把深拷贝降成浅层的指针转移。消除的收益排在降级前面，两者的关系可以排成一条链：

```text
保证消除（C++17 prvalue） > NRVO（编译器优化）> 隐式移动（C++11）> 拷贝构造
```

编译器的挑法照着链子从左往右：能消除的就直接消除，消除不了的再试 NRVO，再不行的话退到隐式移动，实在没办法的最后一档才是拷贝构造。所以您不用慌：就算 RVO 失效了，后面还有移动语义给您兜底，比 C++03 时代的纯拷贝好得多。

咱们顺着链子往下走，还能引出一条实战里最重要的规则：**永远不要写 `return std::move(local_var);`**。

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

`return std::move(h);` 做的事是把 `h` 显式转换成右值引用，编译器一看就明白了：能走的路只剩移动构造，NRVO 的机会被您亲手掐掉了。您写 `return h;`，咱们给它留的余地就大了：可以走的路有两条，直接消除的 NRVO，或者 C++11 保证的隐式移动。无论选哪条路都比您替它拍板来得好。

## 通用示例——字符串构建工厂

概念都拿到了，咱们放到一个实际的场景里练手。假设您在写一个配置文件解析器、手头需要一个工厂函数来构建配置字符串：

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

咱们挨个看。`format_config_nrvo` 返回的是一个经过复杂构建过程的命名变量，NRVO 能让 `result` 直接在调用者的空间里增长，连一次字符串的移动都省了。`make_config_line` 返回的是表达式结果（prvalue），C++17 给了保证。`format_with_default` 里是有条件分支的，可每个分支交出来的都是 prvalue、照样吃得到保证消除。

## 动手实验——rvo_demo.cpp

最后咱们来跑一个完整的实验程序，RVO、NRVO、失效场景、`std::move` 的误用，咱们一次看全。

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

五个场景都装在同一个程序里了，您把它跑起来，咱们跟着输出一步一步对：

<OnlineCompilerDemo
  title="动手实验：rvo_demo.cpp"
  source-path="code/examples/vol2/03_rvo_nrvo.cpp"
  description="在线运行并观察 RVO、NRVO、NRVO 失效和 std::move 阻止优化的不同行为。"
  run-options="-O2 -std=c++17"
  allow-run
  allow-x86-asm
/>

咱们一段一段对。第 1 步和第 2 步是最舒服的情况：RVO 和 NRVO 都生效了，每个对象都只构造了一次，拷贝和移动的日志一行都没有。

咱们看第 3 步，NRVO 失效了，原因就是前面拆过的：两个分支返回不同的命名对象。编译器选择了隐式移动 `a`，您看输出里 `C_a` 变成了移动构造，`b` 倒是正常析构了。

第 4 步是 `return std::move(t)` 的后果：NRVO 被挡住了，白白多了一次移动构造。编译器其实也会提醒您，它触发的是 `-Wpessimizing-move` 警告（"moving a local object in a return statement prevents copy elision"）<RefLink :id="3" preview="GCC Warning Options — -Wpessimizing-move and -Wredundant-move" />，明说这里的 `std::move` 把消除机会掐了。在 GCC 里这个警告默认是关着的，您得加上 `-Wall` 才看得见它。

咱们觉得第 5 步最有意思。`return_param` 接收参数的时候发生了一次移动构造（`std::move(param)` 触发），返回参数的时候又来一次隐式移动、加起来一共两次移动。您再看析构顺序：`param` 和 `e` 其实同在一个块里，`param` 是更早构造的那个，离开块的时候按构造的逆序析构，`param` 的析构就排在了 `e` 后面。

您要是用 `-fno-elide-constructors` 关掉消除重新编译，就会看到第 2 步多了一次移动构造，第 1 步倒是没有半点变化。这一回您就亲手摸到分界了：保证消除属于语言语义的层面、开关关不掉，NRVO 是编译器提供的优化、开关管得着。

## 实战指导

理论都落地了，咱们收几条能直接用的写法。

咱们按值返回、不用输出参数，`std::string build_message()` 的写法比 `void build_message(std::string& out)` 更利于 RVO/NRVO。现代 C++ 的路数是"写自然的代码，让编译器替您优化"，按值返回就是最自然的写法。

`return std::move(local);` 的写法，咱们前面已经拆过它的后果，但笔者见过太多"好心办坏事"的案例，这里值得笔者再念叨一次。您写 `return local;`，就是把最大的自由留给了编译器、消除和隐式移动随它挑。写成 `std::move` 就属于反优化了：路被您强行压到了移动构造上。

您把返回路径保持简单：函数里要是有多个返回分支的话，尽量让它们走的都是同一个命名变量、或者都是 prvalue。不同分支返回不同的命名对象，NRVO 就没戏了。

性能敏感的代码，您量了再说。RVO/NRVO 终归是编译器的优化、换个编译器、换个版本、换个优化级别，出来的行为就可能不一样了。您真在意某次返回的性能，咱们就动手写个 benchmark 测一测，别把判断交给猜测了。

下一篇咱们处理移动语义里最绕的部分——万能引用与完美转发，看看 `T&&` 在模板里为什么会"万能"，`std::forward` 又是怎么把左值右值的身份原样传下去的。

<ReferenceCard title="参考文献">
  <ReferenceItem
    :id="1"
    author="cppreference.com"
    title="Copy Elision"
    chapter="NRVO; Mandatory Elision (C++17)"
    url="https://en.cppreference.com/w/cpp/language/copy_elision"
  />
  <ReferenceItem
    :id="2"
    author="Richard Smith"
    title="P0135R1: Wording for Guaranteed Copy Elision through Simplified Value Categories"
    publisher="WG21 / ISO C++ Committee"
    :year="2016"
    url="https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2016/p0135r1.html"
  />
  <ReferenceItem
    :id="3"
    author="GCC"
    title="Warning Options (-Wpessimizing-move)"
    publisher="gcc.gnu.org"
    url="https://gcc.gnu.org/onlinedocs/gcc/Warning-Options.html"
  />
</ReferenceCard>
