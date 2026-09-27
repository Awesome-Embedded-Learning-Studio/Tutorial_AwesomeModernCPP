---
chapter: 2
cpp_standard:
- 20
- 23
description: C++20 的立即函数和编译期初始化，与 constexpr 的精确区分与选择策略
difficulty: intermediate
order: 3
platform: host
prerequisites:
- 'Chapter 2: constexpr 基础'
- 'Chapter 2: constexpr 构造函数与字面类型'
reading_time_minutes: 15
related:
- constexpr 构造函数与字面类型
tags:
- host
- cpp-modern
- intermediate
- consteval
- constinit
- 编译期计算
title: consteval 与 constinit：编译期保证的新工具
---
# consteval 与 constinit：编译期保证的新工具

咱们前两篇把 `constexpr` 从变量讲到了函数，又接着讲到了构造函数。嘴边一直挂着的字眼，其实就是"可以"：`constexpr` 承诺的是"可以在编译期求值"，而"必须"，压根就不在它的承诺里。您声明一个 `constexpr` 函数，说的是"这个函数能在编译期求值"，编译器可没答应每一次都真的这么做。

编译器甚至比咱们想的还要主动。开了优化以后，哪怕接收返回值的只是个普通变量，只要参数全是常量、调用的函数也足够简单，它照样可能在编译期就把结果算好了。可反过来的情形也是真实存在的：场景一旦复杂了，或者优化被关掉了（比如 `-O0`），`constexpr` 函数就可能真的退化成运行时调用。咱们拿不准它什么时候算、什么时候不算，标准里也没有硬性的承诺。`consteval` 要解决的，恰恰就是这个没保证的地方。

大多数的时候，咱们是欢迎这个弹性的：一份代码能接编译期和运行时两边的活儿，谁不爱呢。不过有些计算就不同了，您对它们的要求是硬的：非在编译期执行完不可。咱们最熟悉的编译期哈希、编译期配置校验，都是这一类的典型。它们要是不小心退到了运行时，代码审查时多半是看不出来的，得等性能分析或者运行时错误冒了头，您才知道出了事。

`consteval` 的做法，就是让编译器做强制性的检查，把这类问题的暴露时机直接提前到编译阶段。C++20 为此引入了两个新关键字，`consteval` 声明的函数叫"立即函数"：必须在编译期求值。`constinit` 管的是静态变量，保证它的初始化在编译期完成。它们不是 `constexpr` 的替代品，更像是补上了 `constexpr` 管不到的两个位置。两个关键字都到齐了，咱们挨个把它们看清楚。

## 第一步——consteval：强制编译期求值

### 立即函数：从"可以"到"必须"

`consteval` 声明的函数，标准里给的名字是"立即函数"（immediate function）。它的语义很直白：您对这个函数的每一次调用，都必须产出一个编译期的常量。编译器发现某个调用的上下文在编译期完不成求值，当场就把错误报出来了，没有一点商量的余地。

```cpp
consteval int square(int x)
{
    return x * x;
}

// OK：参数是常量，上下文是 constexpr 变量初始化
constexpr int kResult = square(8);  // 编译通过，kResult == 64

// OK：参数是常量字面量
int arr[square(5)];  // OK，square(5) == 25，数组大小

// 错误！参数来自运行时
int runtime_val = 42;
// int bad = square(runtime_val);  // 编译错误：不是常量表达式
```

咱们把 `constexpr` 版本摆出来，同一个函数只换了一个关键字：

```cpp
constexpr int square_maybe(int x)
{
    return x * x;
}

int runtime_val = 42;
int ok = square_maybe(runtime_val);  // OK！退化为运行时调用
```

咱们把两段摆在一起，差别就看出来了：`constexpr` 函数遇到运行时参数，会安安静静地退回运行时执行。而 `consteval` 函数遇到运行时参数，编译就直接失败了。您可以把 `consteval` 看成 `constexpr` 的强化版，多出来的部分，就是编译期的强制担保，说的就是那个"必须"。

### consteval 的适用场景

哪些计算，是值得交给 `consteval`？咱们挑场景的标准很朴素：这个计算要是在运行时做，要么没意义，要么有风险。往下要讲的三个场景，全都符合这个朴素的标准。

头一个场景是编译期 ID 和哈希的生成。咱们在协议处理、命令分派里经常要把字符串映射成整数 ID，哈希计算要是留到了运行时去做，CPU 就白花了一遍，编译期的冲突检测也跟着丢了。FNV-1a 是这类用途里常见的简单哈希算法，咱们直接看：

```cpp
#include <cstdint>
#include <cstddef>

consteval std::uint32_t fnv1a32(const char* str, std::size_t len)
{
    std::uint32_t hash = 0x811c9dc5u;
    for (std::size_t i = 0; i < len; ++i) {
        hash ^= static_cast<std::uint8_t>(str[i]);
        hash *= 0x01000193u;
    }
    return hash;
}

template <std::size_t N>
consteval std::uint32_t command_id(const char (&s)[N])
{
    return fnv1a32(s, N - 1);
}

// 所有 ID 都在编译期生成，没有任何运行时开销
constexpr auto kIdStart = command_id("START");
constexpr auto kIdStop  = command_id("STOP");
constexpr auto kIdReset = command_id("RESET");

// 编译期验证：确保没有哈希冲突
static_assert(kIdStart != kIdStop);
static_assert(kIdStart != kIdReset);
static_assert(kIdStop != kIdReset);
```

还请您移步到末尾那三行 `static_assert`：ID 在编译期生成，顺手还验证了它们互不相等。真要是撞了哈希，编译当场就停下来了，程序连一条指令都还没执行呢。

痛快啊！错误要是溜到了运行时，咱们就得从一堆诡异现象里往回猜了。

第二个场景是编译期的配置校验。您要让某个配置值满足约束，咱们就让校验直接在编译期跑完，运行时连"发现配置错了"的机会都不留。

```cpp
consteval int validate_buffer_size(int size)
{
    // 如果约束不满足，直接编译错误
    return size > 0 && size <= 4096 && (size & (size - 1)) == 0
        ? size
        : throw "Buffer size must be a power of 2 between 1 and 4096";
    // 在 consteval 上下文中，throw 会导致编译错误
}

constexpr int kBufferSize = validate_buffer_size(1024);  // OK
// constexpr int kBadSize = validate_buffer_size(1000);  // 编译错误！不是 2 的幂
```

第三个场景是编译期的类型标签和元数据。您要在类型系统里嵌一份编译期信息（比如外设描述、协议字段定义），`consteval` 能保证它们不会悄悄地变成运行时对象。

```cpp
struct PeripheralTag {
    const char* name;
    std::uint32_t base_address;
    std::uint32_t clock_mask;

    consteval PeripheralTag(const char* n, std::uint32_t addr, std::uint32_t clk)
        : name(n), base_address(addr), clock_mask(clk) {}
};

consteval PeripheralTag make_usart1_tag()
{
    return PeripheralTag{"USART1", 0x40013800, 0x00004000};
}

constexpr auto kUsart1Tag = make_usart1_tag();
static_assert(kUsart1Tag.base_address == 0x40013800);
```

### consteval 的传播规则

`consteval` 有一条需要您留神的传播规则，写组合代码的时候得心里有数。一个 `consteval` 函数要是被外层函数调用了，对外层是有要求的。咱们手里的出路有两条：外层函数自己也标 `consteval`，或者让这次调用本身落在常量求值的上下文里。两头都不沾的话，等来的就是编译错误。

```cpp
consteval int forced_compile_time(int x) { return x * x; }

// 错误！constexpr 函数中调用 consteval 函数，
// 但该调用的结果不是常量表达式
constexpr int wrapper(int x)
{
    // return forced_compile_time(x);  // 编译错误
    return x * x;  // 需要自己实现逻辑
}

// OK：consteval 函数中可以调用 consteval 函数
consteval int double_square(int x)
{
    return forced_compile_time(x) * 2;
}

constexpr auto kVal = double_square(3);  // OK，kVal == 18
```

规则后来又松了一格（C++23 的 P2564R3，P 加数字是标准委员会的提案编号）。松动的范围咱们得看清楚：能享受放宽的，是模板实例化出来的 `constexpr` 函数，还有未标 `consteval` 的 lambda 调用运算符这类。它们里面调用 `consteval` 函数的时候，只要这次调用最终落在了常量求值的上下文里，您就不会收到报错。咱们自己写的非模板 `constexpr` 函数不在名单上，上面代码里的那个 `wrapper` 恰好就是非模板，您把那行注释取消试试，照样是编不过的。标准其实还列了第三类，就是 `= default` 合成出来的函数，只是函数体不由您写、咱们平常碰不上。`consteval` 和 `constexpr` 在模板里的组合，从此灵活了不少。

> P2564R3 的放宽，后来就是按 DR20 处理的：DR20 是缺陷报告的意思，效力也就回溯到了 C++20。咱们以后见到 DR 打头的编号，往回溯上想就对了。

### if consteval：编译期/运行期分派

C++23 还引入了 `if consteval`，配套的否定写法是 `if !consteval`。它让函数按"当前是不是常量求值上下文"的标准从两条路里挑一条走，咱们看例子：

```cpp
#include <cstdio>
#include <cstddef>

constexpr std::size_t compute_hash(const char* str, std::size_t len)
{
    if consteval {
        // 编译期路径：使用纯 constexpr 的算法
        std::size_t hash = 0xcbf29ce484222325ull;
        for (std::size_t i = 0; i < len; ++i) {
            hash ^= static_cast<std::size_t>(str[i]);
            hash *= 0x100000001b3ull;
        }
        return hash;
    } else {
        // 运行时路径：可以使用其他实现策略
        std::size_t hash = 0xcbf29ce484222325ull;
        for (std::size_t i = 0; i < len; ++i) {
            hash ^= static_cast<std::size_t>(str[i]);
            hash *= 0x100000001b3ull;
        }
        // 运行时路径中，如果编译器支持内联 SIMD 指令，
        // 可能会自动向量化这段循环；也可以显式调用 SIMD 库
        return hash;
    }
}

constexpr auto kCompileTimeHash = compute_hash("test", 4);  // 走编译期路径
```

跟您交个底：例子里两条路写的还是同一套算法，差别留在注释里了。真到了运行时，其中一边可以换成向量化或者别的优化策略。`if consteval` 管的只是"走哪条路"，至于路上的代码怎么写，它是不干涉的。

咱们得把 `if consteval` 和 `if constexpr` 分开，它们实在太像了。`if constexpr` 看的是模板参数，编译期就把分支给选定了。`if consteval` 看的是"这次调用在不在常量求值里"。您要是想给编译期和运行时各留一套实现，用的是后者。

## 第二步——constinit：解决静态初始化问题

### 静态初始化顺序灾难

轮到 `constinit` 了。咱们得把它要解决的那个老问题摆出来，才好懂它在做什么。C++ 里的静态存储期对象，也就是咱们常写的全局变量、`static` 类成员这些，初始化是分成两个阶段的。

第一阶段咱们叫它静态初始化（static initialization），装的是零初始化和常量初始化两样，后者的英文是 constant initialization。它们在程序加载阶段就完成了，连 `main` 都还没开始跑呢。顺序也是定的：零初始化在前面、常量初始化在后面。

第二阶段的动态初始化（dynamic initialization），需要运行时代码的参与。咱们要说的麻烦就在这一步：不同翻译单元之间，动态初始化的顺序是未定义的。您要是让 `a.cpp` 里的全局对象，去依赖 `b.cpp` 里某个全局对象的值，那您就可能撞上"静态初始化顺序灾难"，正式的英文名是 Static Initialization Order Fiasco，咱们在行话里管它叫 SIOF，后文就这么叫它了。

```cpp
// a.cpp
#include <vector>
std::vector<int> g_data{1, 2, 3};  // 动态初始化：调用 vector 的构造函数

// b.cpp
extern std::vector<int> g_data;
int g_first_element = g_data[0];  // 可能读到未初始化的 g_data！
```

这一类 bug 最让人头疼的地方，就是它的不稳定：某些链接顺序下一切正常，换个链接顺序就出事了，而且偏偏只在程序启动那一下发作。您真遇上了，手里的线索也少，排查起来是非常费劲的。

咱们把两种链接顺序下的启动过程做成了动画，您可以按步进键单步地看 g_data 的构造和 b 的读取谁排在前面：

<Anim id="constinit-siof" />

### constinit 的语义

`constinit` 的语义，一句话就能说清：它用在具有静态或线程存储期的变量声明上，断言的就是这个变量必须做常量初始化。编译器要是发现它需要的是动态初始化，二话不说就报错了。咱们看三组声明，能编的和不能编的都在：

```cpp
#include <array>

// OK：std::array 的聚合初始化是常量初始化
constinit std::array<int, 4> g_table = {1, 2, 3, 4};

// OK：用 constexpr 函数的返回值初始化
constexpr int compute_value() { return 42; }
constinit int g_value = compute_value();

// 错误！get_runtime_value 不是常量表达式，需要动态初始化
// int get_runtime_value();
// constinit int g_bad = get_runtime_value();  // 编译错误
```

### constinit 和 constexpr，各承诺什么

`constinit` 和 `constexpr` 都沾编译期的边，可它们承诺的维度不一样。`constexpr` 变量要求的是两件事：值是在编译期确定的，对象本身是 `const` 的，您改不动它。而 `constinit` 变量也要求初始值在编译期确定，但对象本身是可以改的。

```cpp
constexpr int kConstVal = 42;        // 编译期值 + 不可修改
// kConstVal = 100;                  // 错误！constexpr 变量是 const 的

constinit int gMutableVal = 42;      // 编译期初始化 + 可修改
gMutableVal = 100;                   // OK！运行时可以改值
```

这点差别看着是不大的，不过在工程里正好顶用。比如一个全局的配置缓冲区，您想让它的初始值在编译期就定好、避开 SIOF，程序跑起来之后又要更新它的内容。您碰上这样的需求，交给 `constinit` 正好就接得住了。

> 咱们再补一句：`constinit` 和 `constexpr` 是互斥的，一个声明里同时写两个是不行的。`constexpr` 变量本身就把常量初始化和 `const` 语义都包含进去了，您再补一个 `constinit`，那就重复了。

### constinit 与 thread_local

`constinit` 还有一个实用的附带效果：用在 `thread_local` 变量上，能把运行时的初始化检查开销省掉。咱们对比着看：

```cpp
// 没有 constinit：每次访问都需要检查线程局部存储是否已初始化
thread_local int tl_counter = 42;

// 有 constinit：编译器知道初始化在加载时就完成了，
// 不需要运行时守卫变量（guard variable）
constinit thread_local int tl_fast_counter = 42;
```

普通的 `thread_local` 变量，每次访问都得查一遍"初始化过了没有"，编译器为此在背后放了一个守卫变量（guard variable），访问的时候还可能带上原子操作。加了 `constinit`，编译器就知道这个变量在程序加载时就有确定的初始值了，理论上能把运行时的检查拿掉。实际收益嘛，得看编译器的实现。笔者在 GCC 15.2.1（`-O2`）上测过，测出来的结果是优化幅度约 5%，算是比较有限的了。笔者看到这个数的时候，把预期往回收了收，指望它省一大笔的朋友，还是别抱太大的希望。换到别的编译器或场景，改善可能就会明显一些了。

### extern 声明中的 constinit

`constinit` 还能用在非初始化的声明上，`extern` 声明就是典型的用法：告诉编译器，这个变量在别处已经用 `constinit` 定义好了，运行时的初始化检查也就免了。咱们看头文件和源文件各一行：

```cpp
// header.h
extern constinit int g_shared_value;  // 告诉使用者：这是常量初始化的

// source.cpp
#include "header.h"
constinit int g_shared_value = 100;   // 实际定义
```

这一招放到大型项目里是很顺手的。头文件里的 `extern constinit` 声明，本身就把话说明白了：读到这一行的您，就知道这个全局变量的初始化行为是确定的、不掺动态初始化的。

## 第三步——三关键字对比与选择策略

三个关键字咱们都见过了，现在咱们把它们摆到一张表里对齐。对比做成了动画，您可以播放、暂停，也可以按步进键单步地看，把三个关键字各管什么看个清楚：

<Anim id="consteval-constinit" />

| 特性       | `constexpr`              | `consteval`        | `constinit`            |
| ---------- | ------------------------ | ------------------ | ---------------------- |
| 适用对象   | 变量、函数、构造函数     | 函数、构造函数     | 静态/线程存储期变量    |
| 编译期保证 | "可以"在编译期求值       | "必须"在编译期求值 | 初始化必须是常量初始化 |
| 运行时行为 | 可退化为运行时调用       | 不允许运行时调用   | 变量可在运行时修改     |
| 可变性     | 不可修改（隐式 `const`） | N/A                | 可修改                 |
| 解决的问题 | 编译期计算的灵活性       | 强制编译期求值     | 避免 SIOF              |

您要问怎么选，咱们一句话收拢。值永远不变的，`constexpr` 变量就够了。函数非在编译期执行不可的，咱们就交给 `consteval`。至于要在编译期初始化、运行时又能改的全局变量，写的就是 `constinit`。函数这边咱们默认用 `constexpr`，它是最灵活的，真到了需要强制编译期求值的时候，再升级成 `consteval` 就好了。

### 常见组合模式

实际的项目里，咱们经常把三个关键字搭着用，接着看三种常见的搭配。

咱们从头一种说起：`consteval` 函数生成 `constexpr` 值。它的调用结果天然就是常量表达式，咱们拿 `constexpr` 变量去接，再合适不过了。

```cpp
consteval std::uint32_t hash_string(const char* s)
{
    std::uint32_t h = 0x811c9dc5u;
    while (*s) {
        h ^= static_cast<std::uint8_t>(*s++);
        h *= 0x01000193u;
    }
    return h;
}

constexpr auto kHashStart = hash_string("START");  // 编译期强制求值
constexpr auto kHashStop  = hash_string("STOP");
```

再往下的第二种搭配，是 `constexpr` 函数配上 `constinit` 的全局状态。函数本身倒是不强制编译期求值，可咱们一旦拿它去初始化 `constinit` 变量，编译器就必须在编译期把这次调用给执行掉了。

```cpp
constexpr int lookup_value(int index)
{
    constexpr int kTable[] = {10, 20, 30, 40, 50};
    return index >= 0 && index < 5 ? kTable[index] : 0;
}

constinit int g_first = lookup_value(0);   // 编译期求值
constinit int g_third = lookup_value(2);   // 编译期求值
```

最后一种搭配把 `consteval` 用在编译期的校验上。校验函数本身标上了 `consteval`，它的执行就固定在了编译期。产生编译错误的活儿，交给 `static_assert` 就好了。想让错误信息直接从函数内部冒出来的话，用 `throw` 就可以了，这一招咱们前面在 `validate_buffer_size` 里就见过的。

```cpp
consteval bool check_config(int baud_rate, int data_bits)
{
    if (baud_rate <= 0 || baud_rate > 4000000) return false;
    if (data_bits < 5 || data_bits > 9) return false;
    return true;
}

// 用 static_assert + consteval 函数做编译期配置校验
static_assert(check_config(115200, 8), "Invalid UART config");
// static_assert(check_config(0, 8));  // 编译错误：校验不通过
```

## 几个容易出问题的地方

### consteval 函数的地址不能在运行时使用

咱们在运行时拿不到 `consteval` 函数的函数指针，也就没法调用它了。它的地址可以在编译期用，比如在 `consteval` 的上下文里传递，但不能"逃逸"到运行时：您要是在非常量求值上下文里取它的地址，等来的就是编译错误。原因其实也直白，`consteval` 函数没有运行时的实体，它们在编译期就被完全展开、内联掉了。

### constinit 不意味着 const

最容易搞混的有一处，咱们单拎出来说。`constinit` 说的只是初始化：初始化必须是常量初始化，对象本身不一定是 `const` 的。您要的是一个编译期初始化、又不可修改的全局变量的话，直接用 `constexpr` 就好了，写出来的也干净。`constinit const` 倒是也能工作，不过犯不着绕弯子了。

### consteval 与模板的交互

`consteval` 用在函数模板上也是可以的，不过您得留意一件事。模板实例化之后要是满足不了 `consteval` 的要求，编译器就会报错的。比如内部调用了非 `constexpr` 的函数，就是典型的一种。这和 `constexpr` 函数模板的门槛不一样，`constexpr` 模板只要有一组参数能在编译期工作就行了。`consteval` 面前就没有这个余地了：所有调用都必须在编译期完成。

## 在线运行

您还可以在线运行 consteval 与 constinit 的示例，亲眼看看 C++20 的编译期保证：

<OnlineCompilerDemo
  title="consteval 与 constinit：C++20 编译期保证"
  source-path="code/examples/vol2/06_consteval_constinit.cpp"
  description="在线运行并观察 consteval 强制编译期哈希和 constinit 可变全局变量。"
  allow-run
/>

## 参考资源

- [cppreference: consteval specifier (C++20)](https://en.cppreference.com/w/cpp/language/consteval)
- [cppreference: constinit specifier (C++20)](https://en.cppreference.com/w/cpp/language/constinit)
- [cppreference: constant expressions](https://en.cppreference.com/w/cpp/language/constant_expression)
- [C++ Stories: const vs constexpr vs consteval vs constinit in C++20](https://www.cppstories.com/2022/const-options-cpp20/)
