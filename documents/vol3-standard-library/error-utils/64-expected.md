---
chapter: 7
cpp_standard:
- 23
description: 讲透 std::expected——把错误从异常/返回码提升为类型，构造与访问的几种姿势，and_then/transform/or_else 怎么把"可能失败的操作"串成一条短路链，以及它和 optional/variant 的关系、与异常在不同失败频率下的真实性能差距
difficulty: advanced
order: 64
platform: host
prerequisites:
- optional：把「可能没有」做成类型
- variant：类型安全的联合体与 visit
related:
- filesystem：路径、目录与跨平台文件操作
tags:
- host
- cpp-modern
- advanced
- 类型安全
- expected
title: 'expected：值或错误，C++23 的错误处理新范式'
---

# expected：值或错误，C++23 的错误处理新范式

写到现在,我们对"可能没有"已经有了 `optional`、"可能是 A 或 B"已经有了 `variant`。但还有一个更常见的场景一直绕不过去——**一个函数要么返回一个值,要么返回一个"为什么没成"的错误**。这件事 C++ 历史上处理得一直挺别扭,我们先把痛点摆出来。

最常见的三种写法,每种都有硬伤。第一种是抛异常:`throw std::runtime_error(...)`,控制权瞬间飞走,你在调用点根本看不出来这函数会抛——异常是"隐式控制流",读代码时它不写在签名里。而且即使从来不抛,异常机制本身在某些实现上有表项登记、栈展开的潜在开销,编译器对带异常的代码优化起来也更保守。第二种是返回码:`int rc = foo(); if (rc < 0) ...`。显式是显式了,但错误信息和返回值挤在同一个通道里,你拿到的 `int` 到底是"结果"还是"错误码"全靠约定,而且**太容易忘检查**——一个没写 `if` 的调用点,错误就静默漏掉了。第三种是输出参数 `bool foo(int& out)`,签名里强行塞个引用,返回值位置又被布尔占掉,链式调用彻底没戏。

C++23 给了一个干净答案:**把"值或错误"做成类型**。`std::expected<T, E>` 要么装一个期望值 `T`,要么装一个意外值 `E`。这一篇我们就把它拆透——从构造和访问、到 C++23 的 monadic 链式组合(这是 expected 真正的杀手锏)、再到它和异常的性能实测对比。先说一句结论打底:**expected 不是"取代异常",而是"把错误从隐式控制流提升为显式类型",让编译器逼着你处理它**。

## 一个最小例子:parse 或失败

直接上 cppreference 上 expected 的经典例子——把字符串解析成数字,成功返回 `double`,失败返回一个说明原因的枚举:

```cpp
// Standard: C++23
#include <cmath>
#include <expected>
#include <iostream>
#include <string_view>

enum class parse_error {
    kInvalidInput,
    kOverflow
};

auto parse_number(std::string_view& str) -> std::expected<double, parse_error>
{
    const char* begin = str.data();
    char* end;
    double retval = std::strtod(begin, &end);

    if (begin == end)
        return std::unexpected(parse_error::kInvalidInput);   // 失败:包成 unexpected
    if (std::isinf(retval))
        return std::unexpected(parse_error::kOverflow);

    str.remove_prefix(end - begin);
    return retval;                                            // 成功:直接返回值
}
```

读这段代码,关键就两件事。**成功时,直接 `return retval;`**——`expected` 有个隐式构造,`T` 能直接被包成成功的 `expected<T,E>`。**失败时,`return std::unexpected(错误);`**——`std::unexpected` 是个显式的包装器,专门告诉 `expected`"这是个错误,不是值"。这么设计是有意为之的:`expected<double, parse_error>` 里,`double` 和 `parse_error` 都能隐式构造,如果错误也能直接 `return parse_error{...}`,编译器根本分不清你要的是成功还是失败。所以标准库强制你用 `std::unexpected` 把错误包起来,消除歧义。

调用方怎么用?跑一下看(GCC 16.1.1, `-std=c++23 -O2`):

```cpp
auto process = [](std::string_view str) {
    std::cout << "str: \"" << str << "\", ";
    if (const auto num = parse_number(str); num.has_value())
        std::cout << "value: " << *num << '\n';
    else if (num.error() == parse_error::kInvalidInput)
        std::cout << "error: invalid input\n";
    else if (num.error() == parse_error::kOverflow)
        std::cout << "error: overflow\n";
};

for (auto src : {"42", "42abc", "meow", "inf"})
    process(src);
```

```text
str: "42", value: 42
str: "42abc", value: 42
str: "meow", error: invalid input
str: "inf", error: overflow
```

对照返回码的写法想想,差别在哪:**返回类型 `expected<double, parse_error>` 直接把"可能失败、失败原因有哪几种"写进了签名**。调用方拿到一个 `expected`,不检查 `has_value()` 就用值,编译器也帮不了你太多(下面会说),但至少类型在那摆着,你就知道这是个"需要判一下的东西",不像裸 `int` 返回码那样装得像个普通结果。`"42abc"` 能解析出 42 是因为 `strtod` 是前缀解析,读到一个数字就返回,剩下的留在字符串里——这跟 expected 无关,是 `strtod` 的语义。

## 构造与访问:几种姿势

先把日常会用到的构造和访问方式集中过一遍,免得后面讲 monadic 时再卡在基础语法上。

### 构造

成功的 `expected` 直接塞值,失败的用 `std::unexpected` 包:

```cpp
// Standard: C++23
std::expected<int, std::string> ok = 7;                       // 成功
std::expected<int, std::string> bad = std::unexpected("disk full");  // 失败
```

注意 `bad` 这一行——`std::unexpected("disk full")` 里是个字符串字面量,但最终 `E` 是 `std::string`,这里走的是 `std::unexpected` 的隐式构造,字面量被转成了 `std::string`。这一点很重要:`unexpected` 自己也会把传进去的东西转发给 `E` 构造,所以你不用每次都手写 `std::string("...")`。

### 访问:`operator*` / `value()` / `value_or()`

有三种拿值的方式,语义各不同,这点和 `optional` 几乎一模一样:

```cpp
// Standard: C++23
std::expected<int, std::string> ok = 7;
std::expected<int, std::string> bad = std::unexpected("disk full");

std::cout << "*ok = " << *ok << '\n';              // 解引用:不检查,失败时是 UB
std::cout << "ok.value_or(-1) = " << ok.value_or(-1) << '\n';   // 7

std::cout << "bad.value_or(-1) = " << bad.value_or(-1) << '\n'; // -1
std::cout << "bad.error() = " << bad.error() << '\n';           // disk full
```

```text
*ok = 7
ok.value_or(-1) = 7
bad.value_or(-1) = -1
bad.error() = disk full
```

三者的区别,一句话概括:**`*x` 是"我保证它有值,直接拿"(失败是未定义行为,零开销);`x.value()` 是"我希望它有值,没有就抛异常"(失败抛 `bad_expected_access<E>`);`x.value_or(默认)` 是"有就拿,没有就用默认"(永远不抛,适合有合理兜底的场景)**。

::: warning 解引用不检查,别在失败态上用
`operator*` 和 `operator->` 是**不检查**的。在一个失败态的 `expected` 上调 `*x` 是未定义行为,标准库里它就是"我相信你,直接给你内部存的值的地址"。如果你不确定它有没有值,要么先 `has_value()` 判一下,要么用会抛异常的 `value()`,要么用兜底的 `value_or()`。这一点 `optional` 和 `expected` 完全一致,踩过一次就长记性了。
:::

`value()` 抛的异常类型值得单独记一下——它不是 `std::runtime_error`,而是 `std::bad_expected_access<E>`,一个把那个错误值 `E` 也带上的异常类。所以 `catch` 住之后还能从异常里把错误值取出来:

```cpp
// Standard: C++23
std::expected<int, std::string> bad = std::unexpected("disk full");
try {
    (void)bad.value();                          // 失败 -> 抛异常
} catch (const std::bad_expected_access<std::string>& e) {
    std::cout << "value() threw, error=" << e.error() << '\n';
}
```

```text
value() threw, error=disk full
```

这就是 expected 和异常之间一个很妙的桥:平时用 `value()` 走 happy path 零开销,真出错了又能拿到结构化的错误信息,而不是只剩一个 `what()` 字符串。

### 错误侧的对称:`error()` 和 `error_or()`

值那边有的,错误那边几乎都有。`error()` 拿错误值(成功态上调是 UB),`error_or(默认)` 在成功态下给一个默认错误。这组对称性后面写 monadic 时会反复用到:

```cpp
// Standard: C++23
std::expected<int, int> ok = 5;
std::expected<int, int> bad = std::unexpected(7);
std::cout << "ok.error_or(-99) = " << ok.error_or(-99) << '\n';   // -99(成功态)
std::cout << "bad.error_or(-99) = " << bad.error_or(-99) << '\n'; // 7
```

```text
ok.error_or(-99) = -99
bad.error_or(-99) = 7
```

## C++23 的 monadic:把"可能失败的操作"串起来

到这里你可能还在想:`if (has_value()) ... else ...` 写多了也挺啰嗦的,跟检查返回码也没差多少嘛。真正让 expected 甩开返回码的,是 C++23 给它配的四个 monadic 操作——`and_then` / `transform` / `or_else` / `transform_error`。它们能让你把一串"每一步都可能失败"的操作链成一条流水线,**任何一步失败就自动短路,后面的全不执行,错误一路传到底**。

这是 expected 的核心威力,我们认真讲。

### 一条真实的链:解析 → 换算 → 格式化

假设要把用户输入的字符串,先解析成美元数,再换算成"分"(整数,避免浮点),再格式化成展示字符串。每一步都可能失败:解析失败、金额为负、格式化……传统写法就是一层套一层的 `if`:

```cpp
// 传统写法:层层嵌套 if
auto s = std::string_view{"42.5"};
auto parsed = parse_number(s);
if (!parsed) return error(parsed.error());
auto cents = to_cents(*parsed);
if (!cents) return error(cents.error());
auto text = format_amount(*cents);
```

三步还好,五步、十步呢?这就是经典的"回调地狱"的同步版。用 `and_then` 串起来,变成一条线性表达式:

```cpp
// Standard: C++23
auto to_cents(double dollars) -> std::expected<long, parse_error> {
    if (dollars < 0)
        return std::unexpected(parse_error::kInvalidInput);  // 金额为负 -> 失败
    return static_cast<long>(dollars * 100.0);
}

auto format_amount(long cents) -> std::string {
    return std::to_string(cents / 100) + "." +
           (cents % 100 < 10 ? "0" : "") + std::to_string(cents % 100) + " USD";
}

auto run = [](std::string_view s) {
    std::cout << "\"" << s << "\" -> ";
    auto result = parse_number(s)
        .and_then(to_cents)         // expected<double,E> -> expected<long,E>
        .transform(format_amount);  // expected<long,E>   -> expected<string,E>
    if (result)
        std::cout << "OK: " << *result << '\n';
    else
        std::cout << "ERR: " << static_cast<int>(result.error()) << '\n';
};

run("42.5");   // 成功一路走到底
run("meow");   // parse 失败,后面两步根本不执行
run("-1");     // parse 成功(=-1),但 to_cents 拒绝负数 -> 失败
```

```text
"42.5" -> OK: 42.50 USD
"meow" -> ERR: 0
"-1" -> ERR: 0
```

重点体会两件事。**第一,类型在链上自然流动**:`parse_number` 返回 `expected<double,E>`,`and_then(to_cents)` 接受一个 `double`、返回 `expected<long,E>`,整条表达式的类型就变成 `expected<long,E>`;再 `transform(format_amount)` 把里面的 `long` 变成 `string`,得到 `expected<string,E>`。每一步的值类型在变,但错误类型 `E` 全程一致,所以错误能一路传到底。**第二,短路是自动的**:`run("meow")` 里 `parse_number` 失败,`and_then` 和 `transform` 发现自己拿到的 `expected` 是失败态,直接把错误原样透传,`to_cents` 和 `format_amount` 压根没被调用。这种"成功才继续、失败直接旁路"的语义,就是你本来要用一堆 `if` 手写的东西,现在压成了一条链。

### 四个操作到底各自干什么

把这四个操作的语义对照记一下,选的时候就不会乱:

- **`and_then(f)`**——`f` 接受值,返回一个**新的 `expected`**。这是串"下一步也可能失败"的操作用的(上面 `to_cents` 就是)。`f` 的返回类型必须是 `expected<U, E>`,E 要一致。
- **`transform(f)`**——`f` 接受值,返回一个**普通值**(不是 expected)。这是串"只改值、不会失败"的操作用的(上面 `format_amount` 就是)。返回 `expected<U, E>`。注意它和 `and_then` 的区别就在 `f` 的返回类型:能失败用 `and_then`,不会失败用 `transform`。
- **`or_else(f)`**——和 `and_then` 反过来,**失败时**才调用 `f`,`f` 拿到错误、返回一个新的 `expected`。成功时原样透传。这是"失败补救/兜底"用的。
- **`transform_error(f)`**——和 `transform` 反过来,**失败时**才调用 `f`,`f` 拿到错误、返回一个**新的错误值**(可以是新类型)。成功时原样透传。这是"改写/翻译错误信息"用的。

一句话记忆:**`and_then`/`transform` 走成功分支(`and_then` 返回 expected、`transform` 返回值);`or_else`/`transform_error` 走失败分支(`or_else` 返回 expected、`transform_error` 返回错误值)**。两个轴:走哪条路 × 返回什么。

### `or_else` 和 `transform_error`:失败侧的链

成功侧好理解,失败侧的这两个更值得专门看一下,因为传统错误处理里"失败兜底"和"错误信息加工"是最啰嗦的。

`or_else` 是兜底——失败时换个新的 `expected` 顶上:

```cpp
// Standard: C++23
auto with_fallback = parse_number("meow")
    .or_else([](parse_error) {
        return std::expected<double, parse_error>(0.0);   // 解析失败就给 0
    });
std::cout << "fallback value = " << *with_fallback << '\n';
```

```text
fallback value = 0
```

`transform_error` 是改写错误——失败时把错误 `E` 转成另一种(通常是更可读的)形式,返回类型里 `E` 都会跟着变。下面把枚举错误翻译成带编号的字符串,顺便演示一下 `E` 的类型在链上可以变:

```cpp
// Standard: C++23
auto reworded = parse_number("meow")
    .transform([](double d) { return d + 1000.0; })       // 成功分支,但现在是失败态 -> 不执行
    .transform_error([](parse_error e) {
        return std::string("bad number, code=") +
               std::to_string(static_cast<int>(e));
    });
// 注意:到这里 E 已经从 parse_error 变成了 std::string
std::cout << "reworded error = " << reworded.error() << '\n';
```

```text
reworded error = bad number, code=0
```

那条 `transform` 看着有点多余——输入是失败态,它当然不执行。放这儿是想说清楚一件事:**链上每一步都各自看当前 `expected` 的状态决定执不执行,你可以把成功侧和失败侧的操作都串在一起,它们互不干扰**。成功侧 `transform` 在失败态下是透明的,失败侧 `transform_error` 在成功态下也是透明的。

## 和 optional / variant 是什么关系

把 expected 放回标准库的类型工具家族里,它的位置就很清楚了。还记得我们讲 `optional` 时说的"一个可能没有的值"、讲 `variant` 时说的"类型安全的联合体"吗?expected 正好卡在两者之间。

### expected ≈ optional + 错误信息

`optional<T>` 只告诉你"有没有",没有的时候啥也不说。`expected<T, E>` 在没有的时候多塞了一个 `E` 告诉你**为什么没有**。从存储上看,`expected<T,E>` 约等于"一个 `optional<T>` 加一个错误槽"。当 `E` 是个轻量类型(比如枚举、`int`)时,expected 和 optional 的开销几乎一样——实测一把(GCC 16.1.1, `-std=c++23 -O2`):

```cpp
// Standard: C++23
std::cout << "sizeof optional<int>     = " << sizeof(std::optional<int>) << '\n';
std::cout << "sizeof expected<int,int> = " << sizeof(std::expected<int,int>) << '\n';
std::cout << "sizeof variant<int,int>  = " << sizeof(std::variant<int,int>) << '\n';
```

```text
sizeof optional<int>     = 8
sizeof expected<int,int> = 8
sizeof variant<int,int>  = 8
```

三者都是 8 字节。为什么这么整齐?因为它们底层都是"一个值 + 一个判别位"的结构。`optional<int>` 是 `int` 加一个 `bool`(这里用 `int` 的某个不可能值或者额外字节,实现里通常 `optional<int>` 借用了一个高位字节,所以还是 8)。`expected<int,int>` 和 `variant<int,int>` 都是"两个互斥成员 + 一个 tag"——两个 `int` 共用一份存储(union 语义),再加一个很小的 tag 区分现在存的是哪个,所以 `int + int` 还是 8。**当 `E` 是个需要独立存储的大类型(比如 `std::string`)时,expected 才会比 optional 大**:

```cpp
// Standard: C++23
std::cout << "sizeof expected<double,std::string> = " << sizeof(std::expected<double,std::string>) << '\n';
std::cout << "sizeof expected<int,std::string>    = " << sizeof(std::expected<int,std::string>) << '\n';
```

```text
sizeof expected<double,std::string> = 40
sizeof expected<int,std::string>    = 40
```

40 字节,因为 `std::string`(libstdc++ 的 SSO 实现)本身就 32 字节,加上 `double`/`int` 那一份和 tag,凑齐了对齐。这里的教训是:**选 `E` 的时候留意它的大小**,`std::string` 当错误类型虽然信息丰富,但每个 `expected` 都要多背一个字符串的体积——错误类型小而精,是 expected 用得经济的要点。

### expected 是 variant<T,E> 的语义特化

往深看一层,`expected<T,E>` 在结构上就是 `variant<T, E>`,只是**给两个成员赋予了语义**:第一个是"期望的值",第二个是"意外的错误"。`variant` 是中性的"A 或 B",`expected` 是有立场的"成功或失败"。这个语义上的区别,带来的是一整套贴合错误处理的接口:`value()`/`error()` 的不对称(值失败抛异常、错误失败不抛)、`value_or`/`error_or` 的兜底、还有上面那套 monadic。这些都是 `variant` 没有的——`variant` 要做同样的事,你得自己写一串 `visit`。

还有一个结构上的细节值得点一下:**`expected` 永远不会"无值"**。cppreference 原话就是 "expected is never valueless"。而 `variant` 在某些极端情况下(带值的状态抛异常、且没有 nothrow 移动构造)理论上可能进入 `valueless_by_exception` 状态。expected 因为只装值或错误、不依赖类型列表,这个坑它天生没有。

### `expected<void, E>`:只要错误,不要值

有一类操作它根本不返回值,只关心"成没成":关文件、刷新缓冲、提交事务。这种用 `expected<T,E>` 就尴尬——`T` 填啥?标准库给了一个偏特化 `expected<void, E>`,专门表示"成功了(无值)或失败了(带错误)":

```cpp
// Standard: C++23
std::expected<void, int> vok;                              // 成功
std::expected<void, int> vbad = std::unexpected(42);       // 失败,错误 42
std::cout << "vok.has_value = " << vok.has_value()
          << "  vbad.has_value = " << vbad.has_value()
          << "  vbad.error = " << vbad.error() << '\n';
```

```text
vok.has_value = 1  vbad.has_value = 0  vbad.error = 42
```

`void` 特化没有 `operator*` / `value()`(没有值可取),但 `has_value()`、`error()`、monadic 那套都在,用起来跟普通的 expected 一致。

## 和异常的性能对比:真的零开销吗

讲 expected 一句绕不开的口号是"零开销"。但"零开销"具体指什么,得拆清楚。我们的意思是**预期成功路径(happy path)上,expected 不引入异常那种表项登记/栈展开的机制,控制流是普通的分支和返回**。这件事到底成不成立,不能拍脑袋,跑跑看。

::: warning 实验口径说明
下面的微基准测的是"错误处理机制本身"的开销。为了让这个信号不被淹没,函数体故意做得极轻(一次乘加),并标了 `[[gnu::noinline]]` 防止编译器把整个循环优化没、跨调用边界摊平成本。编译参数 `-std=c++23 -O2 -funwind-tables`(打开异常展开表,这是多数发行版默认的 ABI 配置)。绝对数字会随机器、频率、缓存状态波动,我们看的是**不同失败频率下两者的相对差距**,那个趋势是稳健的。
:::

先看 happy path——**全部成功、一个错误都不发生**时,两种风格谁快:

```cpp
// Standard: C++23
[[gnu::noinline]] std::expected<int, int> compute_expected(int x) {
    if (x < 0) return std::unexpected(-1);
    return x * 7 + 3;
}
[[gnu::noinline]] int compute_throw(int x) {
    if (x < 0) throw std::runtime_error("neg");
    return x * 7 + 3;
}
// 各跑 200'000'000 次,全部传非负数(永不失败/永不抛)
```

实测(三次取代表值,机器不同绝对值会差,看趋势):

```text
expected happy : 343 ms
throw    happy : 350 ms
```

两个几乎打平,差距在测量噪声里——expected 稍微多一点分支判断,throw 在现代"零成本异常表"实现下不抛时也几乎没有额外指令。所以**happy path 上 expected 不比 throw 贵,两者是一个量级**,这一点坐实了。

真正的差距在**失败频率**上。expected 的失败就是一次普通的"返回 unexpected 值",代价和一个正常的 return 没差;而 throw 的失败要走"抛出 → 查异常表 → 栈展开 → 析构沿途对象 → 进 catch",这一套代价远高于一次普通返回,而且**失败越频繁,这个差距越夸张**。固定 happy-path 那个轻量函数,人为控制失败频率跑出来:

```text
--- 1/100000 fail rate (罕见失败) ---
expected fail-every-100000: 273 ms
throw    fail-every-100000: 216 ms
--- 1/1000 fail rate (中等) ---
expected fail-every-1000: 225 ms
throw    fail-every-1000: 469 ms
--- 1/10 fail rate (频繁失败) ---
expected fail-every-10: 491 ms
throw    fail-every-10: 38037 ms
```

三个区间,讲三件事:

1. **罕见失败(十万分之一)**:两者相当,throw 甚至略快——因为几乎不抛,异常表那套根本没触发,而 expected 多了一次 `has_val` 分支。这就是为什么"异常适合真正异常的情况"这句老话有道理:几乎不发生的错误用异常,在 happy path 上确实没负担。
2. **中等失败(千分之一)**:throw 开始明显慢了——每次抛的栈展开成本累计起来,已经是 expected 的两倍。
3. **频繁失败(十分之一)**:throw 直接爆炸,38037 ms 对 491 ms,**慢了将近两个数量级**。这就是 expected 该上的场景:当"失败"是常规情况而不是异常情况(比如解析用户输入、网络重试、缓存未命中)时,异常模型的失败代价根本扛不住,expected 用一次普通返回替代整套栈展开,差距被拉到天差地别。

所以结论不是"expected 总比异常快"——happy path 它们持平、罕见失败异常也不亏。结论是**错误的"频率"决定该用哪个**:几乎不发生的用异常(栈展开的潜在成本换来写代码时不用层层判),经常发生的用 expected(每次失败的代价是常数级的一次返回)。把这个判断想清楚,你才知道什么场景 expected 真的能帮你,什么场景它只是多打字。

## 错误类型 E 怎么选

最后一个实战问题:`E` 填什么?前面例子用过枚举,也用过 `std::string`,还有 `std::error_code`。选型上有个简单的层次。

**最轻:`enum class`**。一个枚举值通常 4 字节,带不带额外信息看你需要。适合"错误种类就那几种、不需要附带数据"的场景,比如协议解析、状态机:

```cpp
// Standard: C++23
enum class io_error { kOk, kTimeout, kClosed, kAgain };
std::expected<int, io_error> read(int fd);
```

**标准库友好:`std::error_code`**。这是 C++ 错误码的标准载体,能跟 `<system_error>`、文件系统 API、平台错误打通。第 66 篇我们会专门讲 `error_code` 的机制(怎么判、怎么分类、怎么和 `system_category`/`errc` 配合),这里你只需要知道:用 `error_code` 当 `E`,意味着你的错误能直接对接标准库和系统调用那一大套既有错误码。构造时用 `std::make_error_code(std::errc::...)`:

```cpp
// Standard: C++23
std::expected<int, std::error_code> read_with_ec(bool ok) {
    if (!ok)
        return std::unexpected(std::make_error_code(std::errc::timed_out));
    return 42;
}
auto r = read_with_ec(false);
if (!r) std::cout << r.error().value() << ": " << r.error().message() << '\n';
```

```text
110: Connection timed out
```

那个 `110` 是 POSIX 的 `ETIMEDOUT` 错误号,`message()` 给出可读描述——这就是 `error_code` 直接对接系统错误码的好处,你不用自己维护一套编号到字符串的映射。

**信息最全:自定义错误类型**。当错误需要附带结构化信息(错误码 + 人话消息 + 上下文),就定义一个结构体当 `E`。代价是体积,但如果你的函数返回路径上错误信息确实有用,这点开销值得:

```cpp
// Standard: C++23
struct AppError {
    int code;
    std::string msg;
};
std::expected<int, AppError> read_app(bool ok) {
    if (!ok) return std::unexpected(AppError{5003, "connection reset"});
    return 42;
}
```

选型的权衡其实就是一句话:**E 越小越快(每个 expected 都背一份 E 的体积),E 越丰富越好用(调用方能拿到更多排错信息)**。本地小工具、嵌入式、热路径,优先枚举或 `int`;跨模块、对外 API、需要和系统错误打通,优先 `error_code`;需要带上下文的结构化错误,用自定义类型。

## 几个真实容易踩的点

把这一路容易翻车的位置集中收一下:

::: warning 别忘了 `std::unexpected`
构造失败的 `expected` 必须用 `std::unexpected(e)` 包,不能直接 `return e;`。原因前面讲过:`T` 和 `E` 都能隐式构造,直接返回 `E` 编译器分不清你要的是值还是错误,要么编译不过、要么语义错位。记住"成功裸返回值、失败包 unexpected"这个对称口诀。
:::

::: warning `operator*` 不检查
`*x` 和 `x->` 在失败态上是未定义行为,它们是"我相信你"的零开销访问。不确定有没有值就别用,改用 `value()`(失败抛 `bad_expected_access<E>`)或 `value_or(默认)`(永不抛)。
:::

::: warning `and_then` 的回调必须返回 expected
`and_then(f)` 要求 `f` 返回 `expected`,因为它表达的是"下一步也可能失败"。如果你的 `f` 不会失败、只做值变换,用 `transform(f)`,它的 `f` 返回普通值。搞反了——`and_then` 里塞个返回普通值的 lambda、或者 `transform` 里塞个返回 expected 的 lambda——编译期就会报错,这是类型系统在帮你把关,看懂错误信息即可。
:::

::: warning E 的大小会渗进每个 expected
`E` 是 `std::string` 这种大类型时,每个 `expected<T, std::string>` 都要多背一个字符串体积。热路径上大批量 expected 排成数组,这个开销会放大。能接受就接受(信息丰富),接受不了就换小 E。
:::

::: warning monadic 的 E 要对齐
`and_then` 链上所有 `expected` 的 `E` 必须一致(或能隐式转换),否则类型接不上。跨子系统、错误类型不一样时,要么统一 E,要么在接缝处用 `transform_error` 把 E 显式翻译成下一个环节期望的类型。
:::

## 小结

`std::expected<T, E>` 把"值或错误"做成了类型,核心价值是把错误从隐式控制流(异常)或含糊通道(返回码)提升为**编译期可见、类型安全、显式处理的值**。几条关键结论收一下:

- 构造上,成功裸返回 `T`(隐式包装),失败必须 `std::unexpected(e)`(消除成功/失败的歧义);访问上,`*x`/`x->` 不检查(失败 UB)、`x.value()` 失败抛 `bad_expected_access<E>`、`x.value_or(默认)` 永不抛、`x.error_or(默认)` 是错误侧的兜底。
- C++23 的四个 monadic 是真正的杀手锏:`and_then`(下一步可能失败)、`transform`(只改值不失败)、`or_else`(失败兜底)、`transform_error`(改写错误),把"层层 if 判失败"压成一条**自动短路**的链。记法:成功分支 `and_then`/`transform`,失败分支 `or_else`/`transform_error`;返回 expected 的用 `and_then`/`or_else`,返回普通值的用 `transform`/`transform_error`。
- 和 optional/variant 的关系:`expected` ≈ `optional + 错误信息`,结构上是 `variant<T,E>` 的"成功/失败"语义特化,且永远不进入 valueless 状态;`expected<void,E>` 偏特化专门服务"不返回值、只关心成没成"的操作。
- 性能(实测 GCC 16.1.1):happy path 上 expected 和 throw 持平、都是零开销量级;**差距由失败频率决定**——罕见失败异常不亏、频繁失败 expected 快出近两个数量级。错误越"常规",越该用 expected。
- `E` 的选型是小与丰富的权衡:枚举最轻、`error_code` 对接标准库和系统错误(第 66 篇详讲)、自定义类型信息最全。热路径优先小 E,跨模块/对外 API 优先 `error_code`。

下一篇我们进 `error_code` 那条线——把上面 `E` 是 `error_code` 时背后那套 `category`/`errc`/`system_category` 的机制拆透,看标准库是怎么用一个轻量对象把"错误码 + 分类 + 可读消息"三件事打包的。
