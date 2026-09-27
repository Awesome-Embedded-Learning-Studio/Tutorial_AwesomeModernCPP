---
chapter: 2
cpp_standard:
- 11
- 14
- 17
description: 从 constexpr 变量到 constexpr 函数，理解编译期求值的机制与各标准陆续放宽的限制
difficulty: intermediate
order: 1
platform: host
reading_time_minutes: 19
related:
- constexpr 构造函数与字面类型
- 编译期计算实战
tags:
- host
- cpp-modern
- intermediate
- constexpr
- 编译期计算
title: constexpr 基础：把计算搬进编译期
---
# constexpr 基础：把计算搬进编译期

我说，咱们可以把话，说的更直白一点.png

其实，constexpr 可以用来使用的场景并不是快不快，当然快是其中的一个优化作用。但最主要我们了解 constexpr 和它的好朋友们的，比如说 consteval, constinit 等等，是为了**将可以把编译期间就能搞定的内容** 给他算好。

咱们把话说直接一点：`constexpr` 解决的问题，其实不在快不快，而在还需不需要算。当您在代码里写下 `constexpr int kBufferSize = 256;`，就是在告诉编译器：这个值在编译阶段就已经确定了，直接写进二进制文件里就完事了。运行时呢，连一条指令都省了。

比如说，当我们愉快的写下 `constexpr int kBufferSize = 256;`的时候，我们就是在告诉编译器：这个值在编译阶段就已经确定了，直接写进二进制文件里就完事了。这样的方式，可以让我们在运行的时候就直接以 CPU 产生一次立即数脉冲的时候就把数字带出来，而不用使用若干次寄存器，甚至可能是几次内存访问，我们才把 256 这个数字辛苦的憋出来（哎，真是不容易）

说了半天不如跑一遍，咱们直接看一段测试代码的汇编输出（GCC 15.2.1、-O2 优化）：

```cpp
constexpr int kBufferSize = 256;

int get_buffer_size()
{
    return kBufferSize;
}
```

编译出来的汇编，笔者已经验证过了：

```asm
get_buffer_size():
    movl    $256, %eax
    ret
```

您看，函数直接返回了立即数 256，没有任何内存访问的动作，也没有任何计算的步骤。值是编译器在编译期就算好、写死在指令里的，这比任何文字上的解释都直接。

## 从 constexpr 变量说起

### 编译期常量 vs const

咱们从最容易混的一对概念入手：`const` 和 `constexpr`。`const` 的语义是"这个变量在初始化之后不能被修改"，至于初始值嘛，拖到运行时才算也是允许的。`constexpr` 就不一样了：它要求的初始值必须能在编译期确定。它们其实长得像，承诺的东西却完全不同，咱们得趁早把它们分开。

```cpp
// const：运行时常量，初始值可以来自运行时
int get_runtime_value();
const int kSize = get_runtime_value();     // OK，kSize 是 const 但不是编译期常量

// constexpr：编译期常量，初始值必须能在编译期算出来
constexpr int kBufferSize = 256;           // OK，256 是字面量
constexpr int kMask = kBufferSize - 1;     // OK，由编译期常量计算而来

// constexpr int kBad = get_runtime_value(); // 编译错误！初始值不是常量表达式
```

`kSize` 呢，编译器不允许您修改，但它的值是运行时才确定的。所以您不能拿它声明数组大小（C 风格数组在 C++ 中需要编译期常量作为长度），也做不了非类型模板参数。`kBufferSize` 就没有这些限制——它在编译期就有确定的值。

标准里确实有这么一条规则，和很多人的直觉相反，值得咱们停下来看一眼：C++ 标准规定，只要 `const` 整型变量用了常量表达式来初始化，它本身也就成了一个常量表达式。所以在全局或命名空间作用域里，`const int kSize = 256;` 这样的声明，是可以用作数组大小和非类型模板参数的。这么一来，"const 一律进不了编译期上下文"这个印象，到了整型这里就不成立了。

> 链接性方面笔者再补一句：咱们看全局或命名空间的作用域，`const` 变量和 `constexpr` 变量默认带的都是内部链接性（跟 `static` 一样）。这是链接层面的属性，和上面聊的"能不能当常量表达式用"是两码事，您把两件事分开记就好。

那您可能会问，`constexpr` 图什么？图它把意图写在了明面上：标上 `constexpr`，您就是在明确要求一个编译期常量。它的适用范围是所有字面类型，不只是整型而已。而且编译器会强制要求初始值必须是常量表达式，做不到的话就直接报错。`const` 就没有这层强制了，一个 `const` 的初始值究竟算不算常量表达式，全看您初始化时给了什么，编译器不替您把关。所以需要编译期常量的时候，咱们明写 `constexpr`，不靠"恰好用常量初始化的 const"碰运气。

### constexpr 变量的要求

那么您要是想声明一个 `constexpr` 变量，得满足什么样的条件？咱们来确认一下，条件呢一共三条。它的类型必须是字面类型（literal type），它的初始化必须立即完成，而且初始表达式必须是一个常量表达式。

字面类型是个什么概念？您现在只需要知道：标量类型（`int`、`float`、指针这些）、引用类型，还有带 `constexpr` 构造函数的类类型，字面类型说的就是这些。同章的下一篇讲 `constexpr` 构造函数与字面类型，会把这个词完整地展开。本篇您按上面的范围理解，就够用了。

## constexpr 函数：两种求值时机

`constexpr` 函数是这套机制里最有意思的部分。它可以在两种场景下工作：当参数全是编译期常量、而且上下文要求编译期求值的时候，您拿到的就是编译期的执行结果。不然的话，它其实就像普通函数一样留到运行时才执行。什么时候走哪条路，编译器看的是上下文，咱们看例子。

### 基本形态

```cpp
constexpr int square(int x)
{
    return x * x;
}

// 编译期求值：参数是字面量，上下文是 constexpr 变量初始化
constexpr int kResult = square(8);  // 编译器直接把 kResult 替换为 64

// 运行时求值：参数来自运行时
int runtime_input = 42;
int result = square(runtime_input);  // 普通函数调用，在运行时执行
```

您看，同一个函数走出了两条路。这就是 `constexpr` 函数的设计意图：您只写一份代码，什么时候执行，编译器根据的就是上下文。上下文要的是常量，它当然就在编译期把结果算好。上下文要的是运行时的值，它呢就老老实实当普通函数跑。这样的能力让 `constexpr` 函数比单纯的编译期工具（比如模板元编程）灵活得多，后文咱们会把它们放在一起比。

咱们把同一个函数的两条求值路径，做成了动画。您可以播放、暂停，也可以按步进键一步步地看，把两边的差别看清楚了：

<Anim id="constexpr-two-worlds" />

### static_assert 与 constexpr 的配合

`static_assert` 是编译期的断言，它的第一个参数必须是一个常量表达式。这就跟 `constexpr` 函数天然搭上了：您可以用 `static_assert` 去验证 `constexpr` 函数在编译期的行为。

```cpp
constexpr int factorial(int n)
{
    return n <= 1 ? 1 : n * factorial(n - 1);
}

static_assert(factorial(0) == 1, "factorial(0) should be 1");
static_assert(factorial(1) == 1, "factorial(1) should be 1");
static_assert(factorial(5) == 120, "factorial(5) should be 120");
static_assert(factorial(10) == 3628800, "factorial(10) should be 3628800");
```

如果您在 `factorial` 的实现里埋了个 bug（比如把 `n <= 1` 误写成 `n < 1`），`static_assert` 会在编译期立刻炸给您看，直接指出哪里出了问题。错误在编译期就被抓住了，这样的能力在大型项目里非常值钱。而且这样的测试零成本——它们不会生成任何运行时代码。

## 各标准的能力差异：C++11 到 C++17

`constexpr` 在不同 C++ 标准里的能力差别非常大。咱们要是分不清手里的工具在哪个标准里能干什么，代码就可能在这边编译得好好的，换个标准就可能翻车了。下面按版本挨个看放宽了什么。

### C++11：极其严格的限制

C++11 引入了 `constexpr`，但限制极其严格：函数体里只允许出现唯一的一条 `return` 语句，外加 `static_assert`、`using` 声明这类不产生代码的语句。结果呢，您不能写循环，局部变量声明不了，`if-else` 也写不了，所有的逻辑都得压进一个三元运算符表达式或者递归调用里。

```cpp
// C++11 风格：只能用递归和三元运算符
constexpr int fibonacci_cxx11(int n)
{
    return n <= 1 ? n : fibonacci_cxx11(n - 1) + fibonacci_cxx11(n - 2);
}
```

这段代码的写法看着简洁，隐患偏偏藏在递归深度上。编译器对 `constexpr` 求值的递归深度设有默认限制，GCC 的默认值是 512 层，笔者拿一个最笨的办法把它量了出来：写一个每次减 1、递归到 0 为止的线性递归 `linear_recursive(N)`，深度就等于参数本身的数值，咱们把每个深度单独编一个程序去测。N 为 511 的时候能编过，改成 512 编译器就报错了，实测在 Compiler Explorer 的 GCC 15.2 和笔者本地的 16.2.1 上行为一致：

```shell
> g++ -std=c++17 -O2 depth512.cpp
```

然后我们就拿到了:

```text
depth512.cpp:3:45: error: 'constexpr' evaluation depth exceeds maximum of 512
    (use '-fconstexpr-depth=' to increase the maximum)
```

> 您要是想知道 512 这个数是哪来的：C++ 标准建议实现方至少支持 512 层的递归调用，GCC 取的恰好就是标准建议的下限。有意思的是，GCC 手册给另外两个限制写明了默认值，总步数的 `-fconstexpr-ops-limit` 默认 33554432（约 3355 万次运算）、循环迭代的 `-fconstexpr-loop-limit` 默认 262144，偏偏深度这项没有文档默认值，报错信息里的 512 才是实际生效的那个数。

接下来的这段翻车经历，笔者得自己交代。本篇旧版在这里贴过一张顺序测试的表：咱们在同一个程序里从 100 到 600 由浅到深挨个求值，结果是 512 过、520 过、600 编译错误，于是写下「GCC 的限制约为 520-600 层」。真的吗？这回咱们把旧测试程序里注释掉的那行 600 求值放开、重新编译，它居然也过了，旧表量出来的边界其实来自测试顺序本身，编译器的真实上限另有其数。

假边界是怎么来的？GCC 会按函数缓存 `constexpr` 求值的结果，浅的参数哪怕只算过一次 `linear_recursive(1)`，后面再算深的，已经算过的那些层就不再计入新深度。顺序测试从浅的往深的走，每一步都把浅层结果提前存进了缓存，轮到 512 那行的时候，浅的那些层已经在缓存里了，深度计数自然就攒不起来了。缓存的存在也能直接量出来：咱们提前在同一个文件里算过 `linear_recursive(100)` 之后，600 也过得去了，得等 612 的时候才报错，边界恰好抬到了 100 + 512。换成另外的函数去预热就完全无效了，缓存认的就是函数本身。

Clang 那边没有这层按函数的缓存，咱们把同一份顺序测试拿过去，512 那行当场就报了错，您想复现笔者的旧表都复现不出来。配套的 `constexpr_limits_test.cpp` 保留的就是由浅到深的顺序测法：它按由浅到深的顺序求值，输出里 512、520、600 三行全标成了 `cache hit`，个个看着都编过了，其实都是缓存撑出来的。所以编译器的真实上限是多少，咱们得一个深度单独编一个程序去问，顺序测试给的数不作数。

那 `fibonacci(50)` 呢？咱们别为它的深度担心，50 层离 512 的上限还远着呢。旧版的文章在这里写过一句「通常都触发不了限制」，这话只对了一半：触发不了的确实只有深度限制，可调用树的膨胀是指数级的，总步数限制赶在深度前面把它拦下了，一样是编不过的。GCC 这边 `fibonacci(30)` 约 270 万次调用的规模还能正常编译，到了 50 那里，就换成另一句报错了：

```text
$ g++ -std=c++17 -O2 fib50.cpp
fib50.cpp:3:12: error: 'constexpr' evaluation operation count exceeds limit of 33554432
    (use '-fconstexpr-ops-limit=' to increase the limit)
```

真需要更深的递归，咱们有两条路可走。C++14 起咱们可以把递归改写成循环，深度就恒为 1 了，600 层的量也碰不到限制的边。不想改写的话，咱们也可以拿 `-fconstexpr-depth=` 把上限调高，不过它动的只有深度，总步数的限制原样还在，别指望调了一个参数就一劳永逸。

这些行为您不用光听笔者的一面之词，测试程序就在下面（`linear_recursive` 和它的循环版本都在同一个源文件里），您点「动手试一试」直接跑。`kDepth` 按默认的 511 能编过，您把它改成 512 再点「运行」，结果区会给出报错的原文。然后您解注释 `kWarm` 那行、把 `kDepth` 改成 600，浅层的求值进了缓存，程序又能过了。末尾的 `linear_loop(600)` 演的是循环写法，600 层的量照样编得轻轻松松。在线环境用的是 Compiler Explorer 的 GCC 15.2：

<OnlineCompilerDemo
  title="动手验证：constexpr 递归深度上限"
  source-path="code/examples/vol2/51_constexpr_depth_limit.cpp"
  description="在线实测 GCC 的上限：kDepth 保持 511 能编过；改成 512 再点「运行」，结果区直接给出报错原文。解注释 kWarm 那行、把 kDepth 改成 600，浅层求值进了缓存，又能过。在线环境是 Compiler Explorer 的 GCC 15.2。"
  run-options="-O2 -std=c++17"
  allow-run
/>

递归帧一层层压下去、在 512 处报错、循环写法把深度恒定在 1、`-fconstexpr-depth=` 抬高上限的整个过程，咱们做成了动画，您可以按步进键逐段看：

<Anim id="constexpr-depth-limit" />

### C++14：大幅放宽

C++14 是 `constexpr` 真正变得实用的转折点，咱们看看放宽了什么：函数体中可以使用局部变量、`if-else` 语句、`for`/`while` 循环了。还不允许的有这么几样：`goto` 和 `label` 语句，`static` 或 `thread_local` 的局部变量，非字面类型的局部变量，还有咱们写不了的 `try` 块。

```cpp
// C++14 风格：自然得多的写法
constexpr int factorial_cxx14(int n)
{
    int result = 1;
    for (int i = 2; i <= n; ++i) {
        result *= i;
    }
    return result;
}

static_assert(factorial_cxx14(6) == 720);
```

这下咱们终于不用把所有逻辑塞进递归里了。对嵌入式开发者而言，这意味着您可以用更自然的方式实现 CRC 计算、查表生成这类逻辑，不用再绞尽脑汁地拿模板元编程或递归绕限制。

咱们再看一个容易被忽略的变化：`constexpr` 成员函数不再隐式地带 `const`。C++11 的时候，`constexpr` 成员函数会被隐式地加上 `const` 限定符，这意味着它改不了任何成员变量。C++14 把这个限制取消了，`constexpr` 成员函数可以修改成员了（在编译期上下文中），编译期对象的行为因此灵活了不少。

标准库也有了动静：被标成 `constexpr` 的函数从 C++14 起逐渐多了起来，咱们常用的 `std::min`、`std::max` 就是 C++14 标上的。lambda 在 C++14 里还参与不了常量表达式，闭包类型当时还没被划进字面类型的范围，`constexpr` 说明符也是后来才有的，等到了 C++17 才正式进来。所以「部分支持」的准确口径是：lambda 本身 C++14 就已经有了，参与 `constexpr` 的资格要等 C++17。

### C++17：更多实用特性

C++17 继续扩展 `constexpr` 的能力：lambda 这边正式落了地，`if constexpr` 也成了标配。标准库这边呢，被标成 `constexpr` 的函数越来越多，咱们日常能摸到的就有 `std::array`、`std::tuple` 的各种操作。

```cpp
// C++17：constexpr lambda
constexpr auto add = [](int a, int b) constexpr { return a + b; };
static_assert(add(3, 4) == 7);

// C++17：constexpr std::array
#include <array>
constexpr std::array<int, 5> kArr = {1, 2, 3, 4, 5};
static_assert(kArr.size() == 5);
static_assert(kArr[2] == 3);
```

三个标准的能力差异，咱们用一张表收拢一下：

| 能力                 | C++11                    | C++14    | C++17    |
| -------------------- | ------------------------ | -------- | -------- |
| 局部变量             | 仅 `return`              | 允许     | 允许     |
| 循环 (`for`/`while`) | 禁止                     | 允许     | 允许     |
| `if-else` 语句       | 禁止（只能用三元运算符） | 允许     | 允许     |
| 成员函数修改成员     | 禁止（隐式 `const`）     | 允许     | 允许     |
| Lambda               | 不支持                   | 部分支持 | 正式支持 |
| 标准库 constexpr     | 极少                     | 增多     | 大量增加 |

## constexpr 与模板元编程：怎么选

咱们把两条路摆在一起看看。`constexpr` 和模板元编程（template metaprogramming）其实都能实现编译期计算，但它们的定位截然不同。模板元编程是图灵完备的，理论上什么计算都能在编译期做，代价是咱们写得痛苦、读起来更痛苦，编译出来的错误信息跟天书一样。`constexpr` 走的是另一条路，它能覆盖绝大多数的编译期计算需求，写起来跟普通函数没什么两样。

```cpp
// 模板元编程版本：计算阶乘（C++98 风格）
template <int N>
struct Factorial {
    static constexpr int value = N * Factorial<N - 1>::value;
};
template <>
struct Factorial<0> {
    static constexpr int value = 1;
};
static_assert(Factorial<5>::value == 120);

// constexpr 版本：清晰得多
constexpr int factorial(int n)
{
    int result = 1;
    for (int i = 2; i <= n; ++i) {
        result *= i;
    }
    return result;
}
static_assert(factorial(5) == 120);
```

从笔者的经验来看，原则很简单：咱们能用 `constexpr` 函数解决的，就不必上模板元编程了。模板元编程适合那些需要在类型层面做计算的场景（比如根据类型选择不同的实现策略），而 `constexpr` 适合在值层面做编译期计算。两者经常配合使用——模板做类型层面的分派，`constexpr` 函数做具体的值计算。

## 实战：编译期生成查找表

阶乘、斐波那契的经典例子，前面咱们都见过了。接下来上点更实用的：让咱们用 `constexpr` 函数在编译期生成查找表。

### 编译期 CRC-32 查找表

咱们在通信协议和存储系统里到处能见到 CRC 校验。传统的做法是在运行时用循环生成 CRC 查表，或者用 Python 之类的工具生成表再 `#include` 进来。有了 `constexpr`，咱们可以直接让编译器把整张表在编译期生成好。

```cpp
#include <array>
#include <cstdint>

constexpr std::array<std::uint32_t, 256> make_crc32_table()
{
    std::array<std::uint32_t, 256> table{};
    constexpr std::uint32_t kPolynomial = 0xEDB88320u;

    for (std::size_t i = 0; i < 256; ++i) {
        std::uint32_t crc = static_cast<std::uint32_t>(i);
        for (int j = 0; j < 8; ++j) {
            if (crc & 1) {
                crc = (crc >> 1) ^ kPolynomial;
            } else {
                crc >>= 1;
            }
        }
        table[i] = crc;
    }
    return table;
}

// 编译期生成完整的 CRC-32 查找表
constexpr auto kCrc32Table = make_crc32_table();

// 运行时使用：只需要做查表操作
constexpr std::uint32_t crc32_compute(const std::uint8_t* data, std::size_t len)
{
    std::uint32_t crc = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < len; ++i) {
        crc = (crc >> 8) ^ kCrc32Table[(crc ^ data[i]) & 0xFF];
    }
    return crc ^ 0xFFFFFFFFu;
}
```

`kCrc32Table` 早在编译期就完整生成了，最终被直接写入目标文件的只读数据段（`.rodata`）。运行时不需要任何的初始化代码，咱们直接拿来用。这个模式还有个实际的好处：表怎么生成、表怎么用，咱们把两段逻辑写在同一个源文件里，省掉了额外的代码生成工具，也省掉了往构建步骤里添东西的麻烦。

咱们把整张表从编译期生成到运行时查表的过程做成了动画，您可以按步进键逐段看：

<Anim id="constexpr-crc-rodata" />

### 编译期 vs 运行期性能对比

咱们用一个简单的对比实验，看看编译期生成和运行时生成的差别在哪。

```cpp
#include <chrono>
#include <iostream>

// 运行时版本的 CRC 表生成
std::array<std::uint32_t, 256> make_crc32_table_runtime()
{
    std::array<std::uint32_t, 256> table{};
    constexpr std::uint32_t kPolynomial = 0xEDB88320u;
    for (std::size_t i = 0; i < 256; ++i) {
        std::uint32_t crc = static_cast<std::uint32_t>(i);
        for (int j = 0; j < 8; ++j) {
            if (crc & 1) {
                crc = (crc >> 1) ^ kPolynomial;
            } else {
                crc >>= 1;
            }
        }
        table[i] = crc;
    }
    return table;
}

int main()
{
    // 运行时生成
    auto start = std::chrono::high_resolution_clock::now();
    auto runtime_table = make_crc32_table_runtime();
    auto end = std::chrono::high_resolution_clock::now();
    std::cout << "Runtime generation: "
              << std::chrono::duration<double, std::micro>(end - start).count()
              << " us\n";

    // constexpr 版本：直接使用 kCrc32Table，耗时为 0
    std::cout << "CRC table first entry: " << kCrc32Table[0] << "\n";
    std::cout << "Runtime table first entry: " << runtime_table[0] << "\n";

    return 0;
}
```

对比程序就在下面（`kCrc32Table` 的编译期生成和运行时版本都在同一个源文件里），您点「动手试一试」直接跑。Runtime generation 那行的具体数值取决于硬件和编译器优化，两行 first entry 的值则恒为 0：

<OnlineCompilerDemo
  title="动手验证：CRC 表的编译期 vs 运行期生成"
  source-path="code/examples/vol2/32_crc32_runtime_vs_constexpr.cpp"
  description="在线对比：运行时生成一张 CRC-32 表只要几微秒，而 constexpr 版本的表在编译期就已就位，运行时零成本。"
  run-options="-O2 -std=c++17"
  allow-run
/>

不过，这个对比实验也有它的局限。现代的编译器非常聪明，哪怕您声明的是运行时版本，只要它发现函数的输入是常量、又没有副作用，就可能在优化阶段自动把它提升成编译期的计算，大伙管它叫"常量折叠"、也顺口叫常量传播。所以真要测量 `constexpr` 的优势，您得确保编译器不会对运行时版本做这样的优化。而在实际项目里，`constexpr` 的真正价值也不在于省这几微秒。咱们明确要求计算在编译期完成，不指望编译器那天的心情好不好，算出来的结果还能用在数组大小、模板参数这类需要常量表达式的地方。逻辑错误要是混了进来，`static_assert` 会在编译期就把它拦了下来，当场改掉就是了，省得等到运行时才发现什么问题。

不过对嵌入式系统来说，更快的启动时间确实是实打实的收益，`constexpr` 版本的表直接放在只读数据段里，咱们一行初始化代码都不用跑。

### 编译期数学查表

咱们再看一个常见场景：三角函数查表。信号处理和电机控制的场景里经常要快速拿 `sin`/`cos` 值，您直接调用 `std::sin`，在没有浮点运算单元（FPU）的 MCU（单片机）上可能太慢，查表是经典的优化手段。

```cpp
#include <array>
#include <cmath>

template <std::size_t N>
constexpr std::array<float, N> make_sin_table()
{
    std::array<float, N> table{};
    for (std::size_t i = 0; i < N; ++i) {
        // 将 [0, N-1] 映射到 [0, 2π)
        constexpr double kPi = 3.14159265358979323846;
        double angle = 2.0 * kPi * static_cast<double>(i) / static_cast<double>(N);
        // 注意：C++26 之前 std::sin 不保证是 constexpr
        // 在不支持 constexpr std::sin 的编译器上，可以用泰勒展开近似
        double x = angle;
        double sin_val = x - x*x*x/6.0 + x*x*x*x*x/120.0;
        table[i] = static_cast<float>(sin_val);
    }
    return table;
}

constexpr auto kSinTable256 = make_sin_table<256>();

// 快速查表获取 sin 值（输入为 0-255 的索引）
inline float fast_sin(std::size_t index)
{
    return kSinTable256[index & 0xFF];
}
```

还有个细节得跟您交底：C++ 标准并不保证 `std::sin` 是 `constexpr` 的函数，得等到 C++26 才把它正式标成了 `constexpr`，所以 C++17 及之前，咱们得自己用近似方法把表算出来。代码里选的是三项泰勒展开，可三项展开只在 0 附近的区间收敛得像样，直接铺满 0 到 2π 的整个区间就出大问题了：咱们拿索引 128（正好 180°）验一下，表里算出来的值约 0.52，而正弦的真值是 0。真要产一张能用的表的话，范围缩减免不了：把角度折叠到 0 到 π/2 的区间里，再靠对称性补全四个象限的值。所以您把它当个机制演示来看就够了，`constexpr` 在编译期把表算了出来。您真要直接拿来用，咱们还欠一步。

## 容易误判的几件事

### constexpr 不是"强制编译期求值"

这是最容易犯的错误。`constexpr` 函数给的承诺是"可以"在编译期求值，而不是"必须"。您要是把一个 `constexpr` 函数的返回值赋给一个普通变量（不是 `constexpr` 变量），编译器倒是完全可能拖到运行时才调用它。咱们真需要强制编译期求值的话，就用 `constexpr` 变量去接住它的返回值。或者用 C++20 的 `consteval`，同章后面讲 `consteval` 与 `constinit` 的那一篇会专门展开它。

### 编译器的递归深度限制

GCC 的递归深度限制，咱们在 C++11 那里已经实测过了。这里再补两件相关的事。一件是各家默认值的差异：GCC 默认的递归深度限制就是 512 层，咱们前面单独编译量到的正是它，Clang 的默认值也是 512 层（文档值），笔者在 clang 22 上实测同样如此，MSVC 也有类似的限制。另一件是总步数的限制：换成 C++14 的迭代写法能绕开深度，不过总步数还是躲不掉。GCC 的默认值是 33554432 步（约 3355 万），Clang 的 `-fconstexpr-steps` 默认只有约 100 万，比 GCC 紧了几十倍。咱们拿前面 `fibonacci(30)` 那 270 万次调用一对照，差距就露出来了：它在 GCC 编得好好的，到 Clang 就直接超限了。总步数管的是编译期求值的总运算量，您要是真在编译期做大量计算（比如生成一张非常大的查表），就可能撞上这道限制而编译失败。

真遇到了，可以用编译器选项提高限制（比如 GCC 的 `-fconstexpr-depth=` 和 `-fconstexpr-ops-limit=`），或者考虑把大表的生成拆分成更小的片段。不过在笔者看来，一个 `constexpr` 计算要是复杂到了触发限制的地步，通常就该重新审视设计了。编译期计算虽然运行时的成本为零，编译时间却会实打实地涨上去。

### constexpr 函数中的未定义行为

`constexpr` 函数在编译期求值的时候，要是触发了未定义行为（UB），编译器会直接报错——笔者觉得这是白送的好事。数组越界、有符号整数溢出、除以零之类的问题，在运行时可能悄悄地给出错误结果，到了 `constexpr` 求值这里，全都让编译器在编译期拦下了。

```cpp
constexpr int bad_divide(int a, int b)
{
    return a / b;  // 如果 b == 0，编译期求值时直接编译错误
}

// constexpr int kBoom = bad_divide(10, 0);  // 编译错误：除以零
```

所以咱们在编译期能算出来的东西，编译器都会替您把合法性查一遍。

## 在线运行

您还可以在线运行 constexpr 基础示例，观察编译期求值与运行时求值的差异：

<OnlineCompilerDemo
  title="constexpr 基础：编译期阶乘与 CRC-32 查找表"
  source-path="code/examples/vol2/05_constexpr_basics.cpp"
  description="在线运行并观察 constexpr 函数的编译期和运行时行为，以及 static_assert 校验。"
  allow-run
  allow-x86-asm
/>

## 参考资源

- [cppreference: constexpr specifier](https://en.cppreference.com/w/cpp/language/constexpr)
- [cppreference: constant expressions](https://en.cppreference.com/w/cpp/language/constant_expression)
- [C++ Feature-test macro `__cpp_constexpr`](https://en.cppreference.com/w/cpp/feature_test)
