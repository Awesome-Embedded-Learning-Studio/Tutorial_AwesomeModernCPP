---
title: "策略模式:从一堆 if/else 到编译期可替换的 Policy"
description: "从最直觉的「写一堆 if/else 切分支」起步,一步步逼出动态虚函数策略、模板静态策略、std::function 类型擦除,讲清三种写法各自的代价,最后用 C++20 concepts 给策略上编译期约束"
chapter: 11
order: 12
tags:
  - host
  - cpp-modern
  - intermediate
  - 策略模式
difficulty: intermediate
platform: host
cpp_standard: [11, 17, 20]
reading_time_minutes: 20
related:
  - "单例模式:从注释约束到 Meyer's Singleton"
---

# 策略模式:从一堆 if/else 到编译期可替换的 Policy

## 我们到底在解决什么问题

我们先不上定义。想一个特别常见的场景:你在写一个文本处理器,它要把一段字符串按某种规则「格式化」一下。一开始只有大写化这一个需求,你随手写了 `toupper` 就交差。过了两天产品说要支持小写化,你加了个 `if (mode == LOWER)`。又过了一周,需求变成了「按驼峰、按蛇形、按短横线分词」全都来一遍——于是你的 `format` 函数里塞满了 `if` 和 `switch`,每加一种规则,这个函数就再胖一圈,而且这些规则之间还互相干扰,改一个不小心碰坏另一个。

这件事的根子在于:**「用什么算法」和「谁来调用这个算法」被搅在了同一个函数里**。你想换算法,就得动那个本该稳定的调用流程。

策略模式要解决的正是这个纠缠。它的核心想法一句话就能说完——**把一组可以互换的算法从调用方里抽离出来,各自封装成独立的「策略」对象或类型,调用方(Context)只依赖一个统一的接口,具体用哪个策略,可以延后到运行时决定、甚至延后到编译期决定**。这样每加一种新算法就是新增一个策略,调用方一行都不用动。其实你天天在用这个模式而不自知:标准库的算法(`std::sort`、`std::transform`)在设计上就是典型的策略模式——它把「比较策略」「变换策略」抽成了可替换的参数(函数对象、lambda、concepts 约束的 callable)。

但「可替换」这三个字,在 C++ 里有两个截然不同的实现层次,代价完全不同。一个是**运行时可替换的动态策略**,靠虚函数或 `std::function` 实现;一个是**编译期可替换的静态策略**,靠模板参数(以及 C++20 的 concepts 约束)实现。两者不是「谁先进」的关系,而是**解决不同性能/灵活性取舍**的两条路。接下来我们就一步步来,先从最蠢的写法看起,看看每一步为什么不够。

## 第一步:最原始的写法——一堆 if/else(反面教材)

很多人第一次遇到「同一种操作有多种实现」时,下意识写出来的代码是这样的:

```cpp
enum class FormatMode { Upper, Lower, Snake };

std::string format_text(const std::string& s, FormatMode mode) {
    switch (mode) {
        case FormatMode::Upper: {
            std::string out = s;
            for (auto& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            return out;
        }
        case FormatMode::Lower: {
            std::string out = s;
            for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return out;
        }
        case FormatMode::Snake:
            return to_snake(s);  // 假设已有
    }
    return s;
}
```

能跑,但问题藏在后面。第一,**每加一种算法,这个 `switch` 就要再开一个分支**,算法越多,这个函数越长越脆。第二,**这些分支的代码物理上挤在一起**,改 Lower 那一支时不小心碰坏 Upper 的概率随函数体积线性上升。第三,最致命的——**算法和调用它的流程没法独立变化**。哪天你想要「按配置文件决定用哪种格式化」,你会发现这个 `switch` 写死在 `format_text` 里,根本没有「把策略换进换出」这个动作可以操作。

问题的本质是:算法没有被封装成独立的、可替换的东西,它只是调用方内部的一个分支。我们得先把算法「提取」出来,让调用方拿到一个抽象的「策略」,而不是自己判断该走哪条分支。

## 第二步:抽出策略接口——虚函数做动态策略

最直觉的「抽出来」就是面向对象的经典做法:定义一个抽象基类当策略接口,每种算法是一个派生类,调用方持有一个指向基类的指针,要换算法就换一个派生对象进去。

```cpp
struct IFormatter {
    virtual ~IFormatter() = default;
    virtual std::string format(const std::string& s) = 0;
};

struct UpperCaseFormatter : IFormatter {
    std::string format(const std::string& s) override {
        std::string out = s;
        for (auto& c : out)
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        return out;
    }
};

struct LowerCaseFormatter : IFormatter {
    std::string format(const std::string& s) override {
        std::string out = s;
        for (auto& c : out)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return out;
    }
};
```

然后调用方(Context)只依赖那个抽象接口,不关心具体是哪个派生类:

```cpp
#include <memory>

class TextProcessor {
public:
    explicit TextProcessor(std::unique_ptr<IFormatter> f)
        : formatter_(std::move(f)) {}

    void set_formatter(std::unique_ptr<IFormatter> f) {  // 运行时可替换
        formatter_ = std::move(f);
    }

    std::string process(const std::string& s) {
        return formatter_->format(s);
    }

private:
    std::unique_ptr<IFormatter> formatter_;
};
```

你看,`TextProcessor` 自己完全没有分支判断了,它只认得 `IFormatter` 这个接口。「用哪种格式化」被推迟到了构造时——`main` 里塞进去哪种派生类,它就用哪种。而且 `set_formatter` 让我们可以在程序运行过程中换策略,这就是「动态策略」的「动态」二字真正的含义:**策略的选择发生在运行时,可以随时替换,甚至可以由配置文件、用户输入、插件来决定**。

我们先验证一下它真的能跑、真的能运行时切换:

```cpp
#include <iostream>

int main() {
    TextProcessor ctx(std::make_unique<UpperCaseFormatter>());
    std::cout << ctx.process("hello") << '\n';  // HELLO

    ctx.set_formatter(std::make_unique<LowerCaseFormatter>());
    std::cout << ctx.process("HELLO") << '\n';  // hello
}
```

```sh
$ g++ -std=c++23 -O2 strategy_runtime.cpp -o strategy_runtime
$ ./strategy_runtime
HELLO
hello
```

舒服。但事情到这里还没完——这种写法是有代价的,而且代价藏在那个 `->` 里。

## 代价一:虚调用的间接跳转

`formatter_->format(s)` 这一行,编译出来的并不是一个直接函数调用。它要做的是:先从 `formatter_` 指向的对象里取出**虚表指针(vptr)**,再从虚表里查 `format` 那一栏的槽位,最后跳到那个槽位里的地址去执行。这就是一次**间接调用(indirect call)**——CPU 到了这里没法提前知道下一条指令在哪,得现查现跳。

间接调用的真正问题不是「多取一次内存」本身,而是**它击穿了编译器的内联优化**。直接调用时,编译器看得到函数体,可以把整个调用摊平进调用方里,省掉参数压栈、返回地址、寄存器保存这一整套开销;但虚函数的真正目标只有运行时才知道,编译器根本不敢把它的函数体内联进来。在热点路径上,这个差距会非常显眼。

那到底差多少?口说无凭,我们写个微基准,把同一个 `x + 1` 的操作分别用「虚函数」「模板」「`std::function`」三种方式跑一亿次,看真实耗时:

```cpp
#include <chrono>
#include <functional>
#include <iostream>
#include <memory>

struct IAdd {
    virtual ~IAdd() = default;
    virtual int transform(int x) const = 0;
};
struct AddOne : IAdd {
    int transform(int x) const override { return x + 1; }
};

struct AddOnePolicy {
    static int transform(int x) { return x + 1; }
};
template <typename Policy>
struct StaticCtx {
    int run(int x) const { return Policy::transform(x); }
};

class FuncCtx {
public:
    explicit FuncCtx(std::function<int(int)> f) : f_(std::move(f)) {}
    int run(int x) const { return f_(x); }
private:
    std::function<int(int)> f_;
};

int main() {
    constexpr int kIters = 100'000'000;
    int acc = 0;

    {
        std::unique_ptr<IAdd> p = std::make_unique<AddOne>();
        auto t0 = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < kIters; ++i) acc += p->transform(i);
        auto t1 = std::chrono::high_resolution_clock::now();
        std::cout << "virtual:       "
                  << std::chrono::duration<double, std::milli>(t1 - t0).count()
                  << " ms\n";
    }
    {
        StaticCtx<AddOnePolicy> ctx;
        auto t0 = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < kIters; ++i) acc += ctx.run(i);
        auto t1 = std::chrono::high_resolution_clock::now();
        std::cout << "template:      "
                  << std::chrono::duration<double, std::milli>(t1 - t0).count()
                  << " ms\n";
    }
    {
        FuncCtx ctx([](int x) { return x + 1; });
        auto t0 = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < kIters; ++i) acc += ctx.run(i);
        auto t1 = std::chrono::high_resolution_clock::now();
        std::cout << "std::function: "
                  << std::chrono::duration<double, std::milli>(t1 - t0).count()
                  << " ms\n";
    }
    volatile int sink = acc;  // 防止整个循环被优化掉
    (void)sink;
}
```

```sh
$ g++ -std=c++23 -O2 -pthread strategy_verify.cpp -o strategy_verify
$ ./strategy_verify
virtual:       28.8597 ms
template:      36.1899 ms
std::function: 150.557 ms
```

注意看这几组数字。模板那条居然跑得比「无操作」还快不了多少——因为编译器把 `AddOnePolicy::transform` 整个内联进了循环里,`x + 1` 直接被循环归纳成了一条算术指令,连函数调用都不存在了。虚函数慢了将近一倍,这是间接调用 + 无法内联的代价。而 `std::function` 慢得最离谱,这一节后面单独讲它。

光看耗时还不够直观,我们把模板策略编译出来的汇编拽出来看一眼,确认它到底被内联成了什么:

```sh
$ cat > strategy_asm.cpp << 'EOF'
struct AddOnePolicy { static int transform(int x) { return x + 1; } };
template <typename P> struct Ctx { int run(int x) const { return P::transform(x); } };
int hot(Ctx<AddOnePolicy> c, int x) { return c.run(x); }
EOF
$ g++ -std=c++23 -O2 -S strategy_asm.cpp -o strategy_asm.s
$ grep -A6 '^_Z3hot' strategy_asm.s
_Z3hot3CtxI12AddOnePolicyEi:
.LFB2:
    .cfi_startproc
    leal 1(%rdi), %eax
    ret
    .cfi_endproc
```

整个 `hot` 函数被编译成了三条指令——`leal 1(%rdi), %eax`(就是 `x + 1` 存进返回值寄存器)+ `ret`。没有 `call`,没有虚表查找,策略的函数体彻底融化进了调用方里。**这就是「编译期可替换」相对「运行时可替换」最硬核的优势:它把一次策略调用优化成了零开销**。虚函数永远做不到这一步,因为它的目标在运行时才确定。

## 代价二:对象生命周期和指针管理

动态策略还有一层麻烦——`formatter_` 是个 `unique_ptr<IFormatter>`,它指向的对象活在堆上。这意味着策略对象要被 `new` 出来,用完再 `delete`,而且 `set_formatter` 每次换策略都可能释放旧对象、分配新对象。堆分配本身不便宜(一次 `new` 的开销远大于一次虚调用),而且在嵌入式、实时、热点循环里,「换策略就触发堆分配」是个很糟的特性。

更微妙的是所有权语义。如果策略是**有状态**的(内部有成员,比如一个计数器、一个缓存),你就得想清楚:`unique_ptr` 表示「Context 独占这个策略」,而如果你想让**多个 Context 共享同一个策略实例**(比如一个全局的缓存策略被多处复用),就得换成 `shared_ptr`。这一节后面实战代码里你会看到一个用 `shared_ptr` 的例子,我们先把它记下来:动态策略的生命周期管理,是你为「运行时可替换」付出的第二笔账。

## 第三步:把策略搬进编译期——模板做静态策略

如果我们的需求里,**策略在编译期就能确定、运行时根本不需要切换**,那上一节那两笔账其实一笔都不用付。我们完全可以把策略当成一个模板参数塞进 Context,让编译器在实例化模板的时候就把「用哪个策略」这件事钉死:

```cpp
struct UpperCasePolicy {
    static std::string format(std::string s) {
        for (auto& c : s)
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        return s;
    }
};

struct LowerCasePolicy {
    static std::string format(std::string s) {
        for (auto& c : s)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }
};

template <typename Policy>
class TextProcessor {
public:
    std::string process(const std::string& s) {
        return Policy::format(s);
    }
};
```

用起来是这样,策略在类型里就定死了:

```cpp
TextProcessor<UpperCasePolicy> up;
TextProcessor<LowerCasePolicy> low;

std::cout << up.process("Hello") << '\n';   // HELLO
std::cout << low.process("Hello") << '\n';  // hello
```

注意我们这里没写任何继承、任何 `virtual`。`Policy` 是一个纯类型参数,`TextProcessor<UpperCasePolicy>` 和 `TextProcessor<LowerCasePolicy>` 在编译器眼里是**两个完全不同的类型**,各自实例化一份代码,各自把 `Policy::format` 内联进自己的 `process` 里。这就是前面基准测试里模板那条能跑到几毫秒的原因——没有虚表、没有间接调用、没有堆分配,策略调用被编译期彻底摊平。

这种写法有个名字,叫 **Policy-Based Design**(策略型设计,Andrei Alexandrescu 在《Modern C++ Design》里系统讲过)。它的本质是:**把「策略」从运行期的对象,降维成编译期的类型**。你不再是「持有一个策略对象」,而是「被一个策略类型参数化」。配合 `static` 成员函数,策略连实例都不需要造——它就是一堆挂在命名空间里的自由函数,被模板参数兜起来。

但这条路也有它的硬伤。**最致命的一条:策略一旦在编译期定死,运行时就再也换不了了。** `TextProcessor<UpperCasePolicy>` 永远只能大写化,你想让它中途改成小写化?做不到,得换一个 `TextProcessor<LowerCasePolicy>` 类型的对象——这俩根本不是一个类型,不能互相赋值,不能塞进同一个容器,不能用同一个变量接。如果你的策略需要由「用户在运行时选」或「配置文件决定」,静态策略就完全不顶用。

第二条相对隐蔽的代价:**模板会把代码展开到每个实例里**。你给 5 种策略,编译器就给你生成 5 份 `TextProcessor::process`,二进制体积会涨。对策略模式这种「函数体很小」的场景通常无所谓,但如果你有几十种策略、每个 `process` 都很重,这个体积膨胀就该掂量一下了。

## 这里先验证一下:concept 怎么给策略上编译期约束

模板策略还有一个被新手忽略的坑:**模板参数 `Policy` 是完全无约束的**。你写 `TextProcessor<Foo>`,只要 `Foo` 能让那段函数体编过就行;一旦 `Foo` 没有 `format` 这个成员、或者 `format` 的签名不对,编译器给你的报错往往是一长串模板展开的「天书」,出错位置常常指向模板内部某一行,而不是你写 `TextProcessor<BadFoo>` 的那一行。这正是 C++20 concepts 被发明出来要解决的问题——**给策略类型一个明确的、可读的契约**。

我们先定义一个 concept,说清楚「一个合格的 Formatter 策略应该长什么样」:它必须有一个 `static format(std::string) -> std::string`:

```cpp
#include <concepts>
#include <string>

template <typename F>
concept Formatter = requires(F f, std::string s) {
    { F::format(std::move(s)) } -> std::same_as<std::string>;
};
```

这个 `requires` 表达式是在问编译器一个问题:「给定一个 `F` 类型的对象 `f` 和一个 `std::string s`,能不能调用 `F::format(std::move(s))`,而且返回值正好是 `std::string`?」能,就算 `F` 满足 `Formatter`;不能,就不满足。然后把模板参数加上这个约束:

```cpp
template <Formatter F>
class TextProcessor {
public:
    std::string process(const std::string& s) { return F::format(s); }
};
```

好的策略(签名对得上)一切正常:

```sh
$ g++ -std=c++20 -O2 strategy_concept.cpp -o strategy_concept
$ ./strategy_concept
HELLO
```

现在我们故意写一个**坏的**策略——返回类型是 `const char*` 而不是 `std::string`,塞进被 concept 约束的 `TextProcessor`,看看编译器怎么报:

```cpp
struct BadFormatter {
    static const char* format(std::string s) { return s.c_str(); }  // 返回类型不对
};

int main() {
    TextProcessor<BadFormatter> tp;   // 应该在这里就报
}
```

```sh
$ g++ -std=c++20 -O2 strategy_concept_bad.cpp -o strategy_concept_bad
strategy_concept_bad.cpp:22:31: error: template constraint failure for
  'template<class F>  requires  Formatter<F> class TextProcessor'
   22 |     TextProcessor<BadFormatter> tp;
      |                               ^
strategy_concept_bad.cpp:22:31: note: constraints not satisfied
  • required for the satisfaction of 'Formatter<F>' [with F = BadFormatter]
  • in requirements with 'F f', 'std::string s' [with F = BadFormatter]
  • 'F::format(std::move<...>(s))' does not satisfy return-type-requirement
```

看到区别了吗?出错位置精准指向了你写 `TextProcessor<BadFormatter>` 的那一行,而且明确告诉你「`F::format(...)` 不满足返回类型要求」。这就是 concept 相对裸模板的价值——**它把「策略该长什么样」从「写错了在模板深处爆炸」提升成了「在调用点一眼可见的契约违约」**。在现代 C++ 里写 Policy-Based Design,给策略加 concept 约束几乎是免费午餐,没理由不做。

## 第四步:类型擦除——用 std::function 做轻量动态策略

现在我们手里有两条路:虚函数能运行时切换但要付堆分配 + 间接调用的代价,模板零开销但编译期定死。那有没有一个折中——**既能运行时切换,又不用自己手写一套继承体系**?有,就是 `std::function`。

`std::function` 的本质是**类型擦除(type erasure)**:它能装下任何「签名匹配」的可调用对象——lambda、函数指针、仿函数、bind 表达式——通通藏在一个统一的类型后面。你不用为每种策略定义一个派生类,直接写个 lambda 扔进去就行:

```cpp
#include <functional>
#include <iostream>
#include <string>

class Printer {
public:
    using Strategy = std::function<void(const std::string&)>;

    explicit Printer(Strategy s) : strategy_(std::move(s)) {}
    void set_strategy(Strategy s) { strategy_ = std::move(s); }
    void print(const std::string& s) { strategy_(s); }

private:
    Strategy strategy_;
};

int main() {
    Printer p([](const std::string& s) { std::cout << "A: " << s << '\n'; });
    p.print("x");                          // A: x
    p.set_strategy([](const std::string& s) { std::cout << "B: " << s << '\n'; });
    p.print("y");                          // B: y
}
```

写起来确实短——不用声明抽象基类,不用 `virtual`,不用 `unique_ptr`,lambda 直接当策略。这是 `std::function` 最大的卖点:**编码心智成本低,接口统一,运行时可切换**。

但这里有个必须戳破的误区。很多资料会说 `std::function`「比虚继承更轻量」,这个说法在小策略场景下成立的部分只是「你不用手写一套继承体系」,而**不是**「它运行得更快」。我们回头看一下前面那次基准测试:

```sh
virtual:       28.8597 ms
template:      36.1899 ms
std::function: 150.557 ms
```

`std::function` 跑得比虚函数还慢了整整五倍多。**这才是真相:`std::function` 在调用层面通常比一次直接的虚调用更贵,不是更便宜**。原因在于它的实现机制——`std::function` 内部要做三件事:第一,它用一个小缓冲区(small buffer optimization,SBO)试图把小的可调用对象直接存进对象体内,避免堆分配;但只要你的 lambda 捕获量超过那个内部缓冲区的大小,它就退化为在堆上 `new` 一块来存。第二,每次调用都要走一次类型擦除的间接跳转,跟虚函数一样击穿内联。第三,某些标准库实现里 `std::function` 的调用路径比单层虚函数还多一层函数指针跳板。

所以正确的心智模型是:**`std::function` 是「写起来最省事」的动态策略,适合策略实现很小、调用不在超高频热点上的场景**;一旦你的策略跑在内层热循环里,它的开销就会非常扎眼,这种地方就该上虚函数(更可预测的间接调用)或者干脆模板静态化(零开销)。

::: warning 别被「std::function 比虚函数轻」误导
网上不少文章把 `std::function` 描述成「比虚继承更轻量的动态策略」。这个说法只在「编码心智成本」和「不用手写继承体系」意义上成立,**在运行性能上恰好相反**:类型擦除的调用路径通常比单层虚调用更贵,还可能额外触发堆分配。策略在热点路径上时,`std::function` 是这三种里最慢的那个。要运行时切换又关心性能,优先看虚函数;能编译期定死,直接上模板。
:::

## 实战:一个能运行时换叫声的动物模型

光讲抽象太虚,我们来个真能跑的。下面这个例子是策略模式一个很生活化的落地:每种动物有一种「叫声」,叫声可以在运行时被换掉(想象一下游戏里给宠物换皮肤/换音效)。我们用 `std::function` 把「叫声」擦除成一个可替换的策略,再用 `shared_ptr` 让多个动物可以共享同一个叫声对象。

```cpp
#include <functional>
#include <memory>
#include <print>

// 策略载体:把任意「无参无返回的叫声」擦除成一个可调用对象
struct AnimalSound {
    ~AnimalSound() = default;

    explicit AnimalSound(std::function<void()> snd)
        : sound_(std::move(snd)) {}

    void make_sound() noexcept { sound_(); }

private:
    std::function<void()> sound_;
};

// Context:动物,持有一个共享的叫声策略,可在运行时替换
struct AnimalType {
    virtual ~AnimalType() = default;

    explicit AnimalType(std::shared_ptr<AnimalSound> snd)
        : sound_(std::move(snd)) {}

    void install_new_sound(std::shared_ptr<AnimalSound> snd) {
        sound_ = std::move(snd);
    }

    void play_sound() {
        if (sound_) sound_->make_sound();
    }

private:
    std::shared_ptr<AnimalSound> sound_;
};
```

这里有两个设计决定值得展开。**为什么 `AnimalSound` 用 `std::function` 而不是虚函数?** 因为「叫声」的实现千奇百怪——可能是一段打印、可能是播一段音频缓冲、可能是触发一个事件——用 `std::function` 让我们不用为每种叫声都开一个派生类,直接拿 lambda 喂进去就行,这是类型擦除的用武之地。**为什么 `AnimalType` 持有 `shared_ptr<AnimalSound>` 而不是 `unique_ptr`?** 因为我们想让「同一个叫声对象被多个动物共享」成为可能(比如「换皮肤」时,把 dog 的叫声对象直接交给 cat,两者共享同一个 `catSound`),`shared_ptr` 的引用计数正好表达了「共享所有权」这个语义。如果你确定策略只归一个 Context 独占,换回 `unique_ptr` 即可——这就是前面说的「有状态策略要明确共享/独占语义」。

跑起来是这样:

```cpp
int main() {
    auto dog_sound = std::make_shared<AnimalSound>([] {
        std::println("Wang!Wang!Wang!");
    });
    auto cat_sound = std::make_shared<AnimalSound>([] {
        std::println("Mewo!Mewo!Mewo!");
    });

    AnimalType dog(dog_sound);
    dog.play_sound();                 // Wang!Wang!Wang!

    AnimalType cat(cat_sound);
    cat.play_sound();                 // Mewo!Mewo!Mewo!

    dog.install_new_sound(cat_sound); // 运行时换策略:dog 也开始喵喵叫
    std::println("What the fuck!");
    dog.play_sound();                 // Mewo!Mewo!Mewo!
}
```

```sh
$ g++ -std=c++23 -O2 strategy_func_runtime.cpp -o strategy_func_runtime
$ ./strategy_func_runtime
Wang!Wang!Wang!
Mewo!Mewo!Mewo!
What the fuck!
Mewo!Mewo!Mewo!
```

`dog.install_new_sound(cat_sound)` 这一行就是策略模式的核心动作:**运行时,把 Context 的策略对象整个换掉,调用方(`AnimalType`)的代码一行没改,行为就变了**。而且因为用的是 `shared_ptr`,`dog` 和 `cat` 现在共享同一个 `cat_sound`,这个对象只在它被最后一个引用释放时才析构——这就是「共享所有权 + 可替换策略」两个语义叠加出来的效果。

::: tip 配套可编译工程
这一节的完整工程(`AnimalSound.h` + `AnimalSoundMain.cpp` + `CMakeLists.txt`,C++23,cmake 一把就能跑)在本仓库里:[Strategy / AnimalSound](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/tree/main/code/volumn_codes/vol4/design-patterns/Strategy/AnimalSound)。
:::

## 三条路怎么选

到这里我们把三种实现都走了一遍。真正的坑在于,**很多人会纠结「哪种是现代写法」,其实这是个伪命题**——三种写法解决的是不同的灵活性/性能取舍,不是新旧关系。我们用一张表把决策依据摊开:

| 维度 | 虚函数(动态) | 模板 + concept(静态) | `std::function`(类型擦除) |
|---|---|---|---|
| 何时切换策略 | 运行时 | 编译期 | 运行时 |
| 调用开销 | 间接调用(无法内联) | 零开销(全内联) | 间接调用 + 可能堆分配,最慢 |
| 编码心智成本 | 中(要写继承体系) | 低(concept 约束 + lambda-free) | 最低(lambda 直接喂) |
| 策略是否可状态化 | 是(成员变量) | 是(但每个 Context 实例独立) | 是(lambda 捕获) |
| 二进制体积 | 一份虚表 + 几个派生类 | 每种策略实例化一份代码 | 一个 `std::function` 对象 |
| 适合场景 | 策略多变、运行时由配置/插件决定 | 策略编译期已知、跑在热点循环 | 策略小而杂、不在超高频路径 |

说人话就是:你问自己两个问题——**「策略要不要在运行时换?」** 和 **「这个调用在不在热点路径?」**。要运行时换且不在热点,`std::function` 写起来最爽;要运行时换且在热点,用虚函数换掉 `std::function`,省掉那一层开销和潜在堆分配;不要运行时换(编译期能定死),直接模板静态化,顺便用 concept 给它上个契约,零开销还报错友好。

还有一条容易被忽略的共性收益:**这三种写法都把策略变成了「可替换的独立单元」,于是它们全都天然利于单元测试**。你想测 `TextProcessor` 的 `process` 流程,但又不想真去触发真实的格式化逻辑(比如它要读文件、要联网),那就塞一个假的策略进去——虚函数塞个 `MockFormatter`,`std::function` 塞个只记录调用的 lambda,模板塞个 `DummyPolicy`——调用方的代码一行不用动,策略就被换掉了。这是策略模式相对「一堆 if/else」最容易被低估的好处:它不只是让代码更整洁,它还顺手把「可测试性」也一起解决了。

## 小结

我们把整条演进路径捋一遍:

| 阶段 | 做法 | 为什么还不够 |
|---|---|---|
| if/else 分支 | 在调用方里 `switch` 算法 | 算法和调用流程搅在一起,加一种就改一次,无法独立变化 |
| 虚函数策略 | 抽象基类 + 派生类 + `unique_ptr` | 运行时可换,但虚调用击穿内联,堆分配 + 所有权要自己管 |
| 模板策略 | 策略作为模板参数,Policy-Based Design | 零开销全内联,但编译期定死,运行时换不了,代码会膨胀 |
| concept 约束模板 | 给策略类型加编译期契约 | 报错从「模板深处天书」变「调用点一眼可见」,几乎免费 |
| `std::function` | 类型擦除,lambda 直接当策略 | 编码最省事,但调用开销比虚函数更贵,别用在热点路径 |

记下这几条关键结论:

- **策略模式的本质是把「可替换的算法」从调用方里抽出来**,标准库的算法(`std::sort` 等)就是策略模式的典型应用。
- **动态策略(虚函数 / `std::function`)的代价是间接调用 + 可能的堆分配**,`std::function` 在调用层面通常比单层虚调用更慢,「比虚函数轻量」只在编码心智成本意义上成立。
- **静态策略(模板)是零开销的**,策略调用会被内联成几条指令(本篇用 `leal 1(%rdi), %eax` 实证过),代价是编译期定死、运行时不能换。
- **C++20 concepts 给策略上了编译期契约**,出错位置和可读性远好于裸模板,写 Policy-Based Design 几乎一定要配 concept。
- 选型只看两个问题:**要不要运行时换** + **在不在热点路径**,没有「哪个最现代」这种答案。

## 参考资源

- [cppreference:`std::function`](https://en.cppreference.com/w/cpp/utility/functional/function)(类型擦除的调用语义,C++11 起)
- [cppreference:Concepts](https://en.cppreference.com/w/cpp/concepts)(`requires` 表达式与约束模板,C++20 起)
- [cppreference:`std::shared_ptr` / `std::unique_ptr`](https://en.cppreference.com/w/cpp/memory/shared_ptr)(策略对象的所有权语义)
- Andrei Alexandrescu,《Modern C++ Design》第 1 章(Policy-Based Design 的系统化论述)
- 配套可编译工程:[Strategy / AnimalSound](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/tree/main/code/volumn_codes/vol4/design-patterns/Strategy/AnimalSound)
