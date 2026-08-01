---
chapter: 12
cpp_standard:
- 20
description: C++20 指定初始化器用 .field = value 按名字初始化聚合成员,必须按声明顺序(这点和 C99 不同),未指定的成员走默认;只能用于聚合类型,不支持数组的 [index] 语法
difficulty: intermediate
order: 6
platform: host
prerequisites:
- 'CTAD:类模板参数推导'
reading_time_minutes: 12
related:
- 'if constexpr:编译期分支'
- 'CTAD:类模板参数推导'
tags:
- host
- cpp-modern
- intermediate
- 基础
title: '指定初始化器'
---
# 指定初始化器

配置结构体一长,按位置初始化就开始吓人。一个 UART 配置七八个字段,挨个填:

```cpp
UartConfig cfg = {115200, 8, 0, 1, 0, 1, 1};   // 谁记得住第几个是啥
```

这种写法的毛病咱们都踩过:字段一多就得对着声明数位置,中间插一个新成员,所有初始化全得跟着挪,而且编译器不会拦您——错了只在运行时表现成怪样子。C99 给过一个解法叫指定初始化器(designated initializer),C++20 把它收进了标准,让咱们能 `.field = value` 按名字初始化。不过 C++20 这版和 C99 有两处关键差别,咱们边用边说,这两处恰好是最容易踩的坑。

## 按 `.field = value` 指定,而且必须按声明顺序

最基本的用法,大括号里 `.字段 = 值` 一个个点名:

```cpp
struct UartConfig {
    std::uint32_t baudrate = 0;
    std::uint8_t data_bits = 8;
    std::uint8_t parity = 0;      // 0=None 1=Odd 2=Even
    std::uint8_t stop_bits = 1;
};

UartConfig cfg{
    .baudrate = 115200,
    .data_bits = 8,
    .parity = 0,
    .stop_bits = 1,
};
```

每个值都标了名字,不再靠位置,这是它最大的好处。但 C++20 这里有个硬规矩,和 C99 不一样:**designator 的顺序必须和成员声明顺序一致**。您要是图顺手把 `stop_bits` 写到 `baudrate` 前面:

```cpp
UartConfig bad{.stop_bits = 1, .baudrate = 115200};   // C++20 编不过
```

GCC 直接拒收:

```text
error: designator order for field 'UartConfig::baudrate' does not match declaration order in 'UartConfig'
```

C99 里这种乱序是合法的,C++20 把它拿掉了。原因和 C++ 的初始化规则有关:大括号初始化本来就按声明顺序走成员,designator 只是个「给当前位置贴个名字」的标注,不是「跳着指定」的通行证。所以您写 designator 可以省略后面的成员,但顺序不能乱。

<OnlineCompilerDemo allow-run
  title="基本语法:按名字指定、必须按声明顺序、部分初始化走默认"
  source-path="code/examples/vol4/vol2-modern-cpp17/designated_basics.cpp"
  description="按声明顺序逐字段指定;部分初始化时未指定的成员用默认成员初始化器;加 -DOUT_OF_ORDER 复现乱序编译报错。"
/>

运行结果:

```text
cfg: baud=115200 bits=8 parity=0 stop=1
partial: baud=921600 data_bits=8(默认8) parity=2 stop=1(默认1)
```

## 只能用于聚合类型

指定初始化器只认**聚合类型(aggregate)**。简单说,一个类要是聚合,得满足:没有用户声明的构造函数、没有私有或受保护的非静态成员、没有虚函数、没有虚基类。普通的结构体、满足条件的类,都是聚合。

一旦您给它加了构造函数,它就不是聚合了,指定初始化器立刻用不了:

```cpp
struct WithCtor {
    int a;
    int b;
    WithCtor(int, int) {}
};

WithCtor x{.a = 1, .b = 2};   // 编不过:不是聚合
```

报错也直白:

```text
error: designated initializers cannot be used with a non-aggregate type 'WithCtor'
```

这条限制其实划清了职责:有构造函数的类,初始化逻辑归构造函数管(那里可以校验、可以默认值、可以抛异常);聚合类型没有构造函数,初始化靠大括号,指定初始化器只是让大括号更清楚。想兼得?给结构体配一个静态工厂函数,工厂内部再用指定初始化器:

```cpp
struct Config {
    int baudrate;
    int data_bits;
    static Config standard() { return {.baudrate = 115200, .data_bits = 8}; }
};
```

## 没指定的成员怎么办

部分初始化时,没被点名的成员按两条规则走:有默认成员初始化器(`int data_bits = 8;`)的用默认值;没有的就值初始化,对内置类型就是零。上面 `partial` 那行 `{.baudrate = 921600, .parity = 2}` 里,`data_bits` 和 `stop_bits` 没写,分别落回默认的 `8` 和 `1`。

::: warning 留意隐式零初始化,也别被 -Wmissing-field-initializers 吓到
没有默认成员初始化器时,漏写的成员会被零初始化。对一个 `bool auto_reload` 来说,零初始化成 `false` 未必是您想要的,关键成员最好显式写全。

另外,部分初始化会触发 GCC 的 `-Wmissing-field-initializers` 警告(它在 `-Wextra` 里),提醒您有成员没写。这是**警告不是错误**,对内置类型漏写多半是安全的(零初始化),但把这个警告当成「我是不是漏了什么」的提醒挺合适,别一看到就以为出了问题。
:::

## 嵌套、位域、联合体都支持

指定初始化器在聚合的几个变体上都好使。嵌套结构体一层层点下去:

```cpp
struct Pin { std::uint8_t port; std::uint8_t pin; };
struct UartCfg { std::uint32_t baud; Pin tx; Pin rx; };

UartCfg u{
    .baud = 115200,
    .tx = {.port = 0, .pin = 9},
    .rx = {.port = 0, .pin = 10},
};
```

位域也能用,联合体也能用(联合体只能初始化一个成员):

```cpp
struct Flags { unsigned a : 1; unsigned b : 1; unsigned c : 6; };
Flags f{.a = 1, .b = 0, .c = 5};

union Value { int i; float f; };
Value v{.f = 3.14f};
```

<OnlineCompilerDemo allow-run
  title="聚合的几种变体:嵌套结构体、位域、联合体"
  source-path="code/examples/vol4/vol2-modern-cpp17/designated_aggregate.cpp"
  description="嵌套结构体逐层指定,位域按名字置位,联合体指定一个成员。加 -DNON_AGGREGATE 复现非聚合类型报错。"
/>

运行结果:

```text
uart: baud=115200 tx=PA9 rx=PA10
flags: a=1 b=0 c=5
union as float: 3.14
```

## 数组的 `[index]` 别想:C++20 不支持

这是第二个容易栽的坑,也是和 C99 的另一处差别。C99 允许数组的指定初始化器用 `[index] = value`:

```c
int pins[5] = {[0] = 1, [2] = 5, [4] = 12};   // C99 合法
```

C++20 **没有**收这条。同样的写法喂给 C++ 编译器:

```cpp
int pins[5] = {[0] = 1, [2] = 5, [4] = 12};   // C++20 编不过
```

GCC 的措辞有点含蓄,报的是 `sorry, unimplemented: non-trivial designated initializers not supported`——它没说「标准不允许」,但效果一样:编不过。C++20 的指定初始化器只管聚合的 `.field` 形式,数组下标这种 designator 不在标准里。数组要部分初始化,老老实实用 `{1, 0, 5, 0, 12}` 这种按位置的大括号就行。

## 嵌入式实战:constexpr 配置表

指定初始化器在嵌入式里最自然的落点,是**编译期配置表**。一堆引脚配置、寄存器映射,用 `.field = value` 写成 `constexpr` 数组,既自解释又在编译期定死,运行时零开销:

```cpp
constexpr std::array<PinCfg, 4> kUartPins = {{
    {.pin = 9,  .mode = GpioMode::Alternate, .pull = GpioPull::None, .alternate = 7},
    {.pin = 10, .mode = GpioMode::Alternate, .pull = GpioPull::Up,   .alternate = 7},
    {.pin = 2,  .mode = GpioMode::Alternate, .pull = GpioPull::None, .alternate = 7},
    {.pin = 3,  .mode = GpioMode::Alternate, .pull = GpioPull::None, .alternate = 7},
}};

constexpr std::array<RegMap, 4> kUartRegs = {{
    {.name = "SR",  .offset = 0x00, .read_only = true},
    {.name = "DR",  .offset = 0x04, .read_only = false},
    {.name = "BRR", .offset = 0x08, .read_only = false},
    {.name = "CR1", .offset = 0x0C, .read_only = false},
}};
```

这份配置表读起来像表格,加一行、改一个字段都不会牵连别的位置。因为它在 `constexpr` 里,编译期就全部算定,生成的代码和手写一组常量没有差别,但可读性高出一截。寄存器映射、引脚表、消息模板、PWM 通道配置,这套写法都直接套用。

<OnlineCompilerDemo allow-run
  title="constexpr 配置表:编译期定死的引脚表与寄存器映射"
  source-path="code/examples/vol4/vol2-modern-cpp17/designated_config_table.cpp"
  description="指定初始化器 + constexpr + std::array 组成嵌入式配置表,运行时零开销,可读性远超按位置的聚合初始化。"
/>

运行结果:

```text
UART 引脚配置表:
  P9  mode=2 pull=0 af=7
  P10 mode=2 pull=1 af=7
  P2  mode=2 pull=0 af=7
  P3  mode=2 pull=0 af=7

UART 寄存器映射:
  SR   @0x00  RO
  DR   @0x04  RW
  BRR  @0x08  RW
  CR1  @0x0C  RW
```

## 几条实务建议

聚合类型用得上,就优先用 `.field = value`,配置代码立刻自解释,日后改结构体定义也不怕。成员多、又想给默认值的,配上默认成员初始化器,部分初始化时省心。

想兼得「校验逻辑」和「字段式初始化」,走静态工厂,别往聚合里硬塞构造函数——塞了构造函数就丢了指定初始化器资格。

最后记牢那两条 C++20 和 C99 的差别:designator 必须按声明顺序、数组没有 `[index]` 语法。这两条是从 C 代码往 C++ 迁移时最容易想当然的地方,踩一次编译器就拦下了,但最好提前知道为什么。
