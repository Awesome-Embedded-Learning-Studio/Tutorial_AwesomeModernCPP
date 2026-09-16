---
chapter: 0
cpp_standard:
- 11
- 14
- 17
- 20
description: 编写、编译和运行你的第一个 C++ 程序，理解 main 函数、输入输出和编译流程
difficulty: beginner
order: 3
platform: host
prerequisites:
- Linux 或 Windows 环境搭建（任选一条）
reading_time_minutes: 19
tags:
- cpp-modern
- host
- beginner
- 入门
- 基础
title: 第一个 C++ 程序
---
# 第一个 C++ 程序

环境搭好了，编译器也装好了，接下来该干正事了——写我们的第一行 C++ 代码。

笔者学过的好几门语言，第一课都是 Hello, World。这个例子小归小，却刚好能把编辑、编译和运行串起来：我们敲进去的文字，到底什么时候才会变成屏幕上的输出？这一篇就把这条路走通。但我可以保证，如果我们把这个小程序拆清楚，后面很多概念都会变得顺理成章。所以别急着跳过，我们一行一行地把它吃透。

## 从零开始——hello.cpp 的骨架

打开你喜欢的编辑器，新建一个文件叫 `hello.cpp`，把下面这段代码原封不动地敲进去。注意是敲进去，不是复制粘贴（我们常常调侃，程序员只有三个按键，Ctrl, C, V，我澄清一下，真学习的时候别这样做。这种事情留到任何你不感兴趣但是不得不做的工作上，比如说写我不感兴趣的业务代码）

肌肉记忆这个东西，在学编程的时候真的很重要。

```cpp
// Platform: host | Standard: C++17
#include <iostream>

int main()
{
    std::cout << "Hello, C++!" << std::endl;
    return 0;
}
```

这一篇统一使用 `hello.cpp` 这个文件名，以 Linux / WSL 下的 GCC 命令演示；代码使用 C++17。Windows 原生环境请沿用上一篇配置好的工具链，生成的程序通常叫 `hello.exe`，在 PowerShell 中用 `./hello.exe` 启动。先别急着切换构建工具，我们把这个单文件例子跑通，后面再让 CMake 帮我们管理更多文件。

### 包含头文件：`#include <iostream>`

这行告诉编译器：我们需要用到"输入输出流"这个功能模块。你可以把它理解成在工具箱里拿出一个叫做 iostream 的工具包。里面有 `std::cout`（用来输出）和 `std::cin`（用来输入），这些是我们和程序打交道的最基本手段。C++ 标准库里有大量的这样的工具包，比如 `<vector>`、`<string>`、`<cmath>`，需要什么就包含什么。

### 程序入口：`int main()`

这是本篇这种宿主环境程序的指定入口。`int` 表示这个函数返回一个整数作为终止状态；返回 0 表示成功，在本篇的 Linux 命令行中，非零状态通常用来报告失败。这个返回值在 Linux 脚本里可以通过 `$?` 拿到，CI/CD 流水线也经常依赖它来判断程序是否执行成功。

### 函数体：输出与返回

```cpp
std::cout << "Hello, C++!" << std::endl;
return 0;
```

`std::cout` 是标准输出流；在这里的终端运行方式下，输出通常显示在屏幕上，也可以被重定向到文件。`<<` 运算符在这里被重新定义了，它的作用是把右边的内容"推送"到左边的输出流里。所以 `std::cout << "Hello, C++!"` 就是把这段文字推送到屏幕上。

`std::endl` 是"end line"的缩写，它做了两件事：输出一个换行符，然后刷新缓冲区——请求把流缓冲区里的内容向底层输出设备提交。它并不保证文字一定出现在屏幕上，因为输出也可能被重定向。只需要换行时可以用 `'\n'`，不必每行都主动刷新。

最后 `return 0` 告诉操作系统：我正常结束了，没什么好担心的。

> 有些教程或者老代码里你会看到 `void main()` 这种写法。**这是错的。这是错的。这是错的！不符合ISO C++的标准规范！！！**
>
> 对于本篇的宿主环境（C++11–C++23），`main` 的返回类型必须是 `int`，虽然某些老旧编译器可能不会报错，但这不代表它是对的。养成习惯，永远写 `int main()`。

你可能注意到 `std::cout` 和 `std::endl` 前面都有一个 `std::` 前缀。`std` 是"标准"（standard）的缩写，它是一个**命名空间**。

笔者建议理解为一个工具包的品牌标签。本篇用到的 `cout`、`cin` 和 `endl` 都属于 `std` 命名空间，用来避免名字冲突。比如你自己写了一个叫 `cout` 的函数，它不会和标准库的 `std::cout` 打架，因为它们在不同的命名空间里。有些教程会在开头加一行 `using namespace std;` 然后直接写 `cout`，这样确实省打字，但在大型项目里容易引发命名冲突，所以我们从一开始就习惯带上 `std::` 前缀。

## 编译和运行

代码写好了，接下来让它跑起来。打开终端，进入 `hello.cpp` 所在的目录，执行：

```bash
g++ -std=c++17 -Wall -Wextra hello.cpp -o hello
```

这条命令做了两件事：用 `g++` 编译器把 `hello.cpp` 编译成可执行文件，`-o hello` 指定输出文件名叫 `hello`（如果不指定，默认会叫 `a.out`，这个名字没什么意义）。编译成功后，当前目录下会出现一个 `hello` 文件，直接运行它：

```bash
./hello
```

输出：

```text
Hello, C++!
```

很好，你的第一个 C++ 程序已经成功运行了。

如果你之前看过了环境搭建那一章，可能还记得 CMake 的用法。对于这种单文件的小程序，直接用 `g++` 命令是最快的。但随着项目变大、文件变多，每次手动敲编译命令会让人崩溃——那时候 CMake 的价值就体现出来了。我们这里先用 `g++`，等后面的章节再正式引入 CMake。

## 幕后发生了什么——编译流水线

如果你一直在 IDE 里点“运行”，很容易把构建程序和启动程序看成同一件事。其实按钮只是替我们串起了命令。现在我们把它拆开，看看源码、中间文件和最终输出分别在什么时候出现；以后报错，也就知道该从哪一段查起。

这里按 GCC 常见工具链来讲四个构建阶段。它是帮助我们理解工具工作的划分，不是 C++ 标准要求实现必须生成四份磁盘文件。

整个流程可以简化为四个步骤。第一步是**预处理**，编译器处理所有以 `#` 开头的指令——把 `#include <iostream>` 替换成 iostream 头文件的实际内容，展开宏定义，处理条件编译。第二步是**编译**，把预处理后的 C++ 代码翻译成汇编语言——这时候编译器会做语法检查、类型检查，你写的语法错误在这一步就会被抓住。第三步是**汇编**，把汇编代码翻译成机器码，生成目标文件（`.o` 文件）。第四步是**链接**，把目标文件和需要用到的库文件（比如 C++ 标准库）组合在一起，生成最终的可执行文件。

![GCC 分步构建：hello.cpp 经预处理得到 hello.ii，编译得到 hello.s，汇编得到 hello.o，链接得到 hello；运行 ./hello 后才产生输出](./assets/03-first-program/compilation-pipeline.drawio)

嘿嘿，下面还有小动画~，各位看看哇！

<Anim id="compilation-pipeline" />

### 把动画里的命令亲手跑一遍

我们在 `hello.cpp` 所在目录依次执行下面的命令，明确保留每个中间文件。以下流程使用 GCC、Linux 和 C++17：

```bash
g++ -std=c++17 -E hello.cpp -o hello.ii
g++ -std=c++17 -S hello.ii -o hello.s
g++ -c hello.s -o hello.o
g++ hello.o -o hello
./hello
```

`-E` 表示预处理后停止，`-S` 表示输出汇编后停止，`-c` 表示生成目标文件后停止、暂不链接。这里每条命令接着上一条的产物继续处理；最后一条构建命令用 `g++` 驱动链接，带上通常需要的 C++ 库。库可以采用静态或动态链接，不是说所有库代码都会被复制进 `hello`。

本次在 Linux 上用 GCC 16.1.1 运行这组命令，最终标准输出是：

```text
Hello, C++!
```

再回头看一行版的 `g++ -std=c++17 -Wall -Wextra hello.cpp -o hello`：它把构建步骤串起来了，通常不会在当前目录留下 `hello.ii` 和 `hello.s`。这个小实验的目的，是让我们看清中间产物，不是要求以后每个程序都手动敲四遍命令。

你可能会问：为什么要知道这些？因为以后你一定会遇到各种编译报错——有些是预处理阶段的问题（头文件找不到），有些是编译阶段的问题（语法错误、类型不匹配），有些是链接阶段的问题（重复定义、找不到符号）。知道错误出在哪个阶段，排查起来就有方向了。

> 编译器报错时，**一定要看第一条错误信息**。很多新手习惯从最后一条看起，但实际上 C++ 编译器有一个"级联报错"的特性——**一个错误可能导致后面几十个"假阳性"的错误。修复了第一条，后面那些可能就自动消失了。所以养成习惯：看第一条，修第一条，重新编译，再看。**

## 那些年我们踩过的坑——常见编译错误

光会写正确的代码是不够的，我们还必须学会读错误信息。下面我们故意制造几个经典错误，看看编译器会怎么说。诊断来自 GCC 16.1.1，采用 C++17、英文诊断环境；每次只改动一个地方，试完后先恢复开头的正确程序，再做下一个实验。

### 忘记加分号

把 `hello.cpp` 里的分号去掉：

```cpp
#include <iostream>

int main()
{
    std::cout << "Hello, C++!" << std::endl  // 这里少了分号
    return 0;
}
```

编译一下：

```bash
g++ -std=c++17 -Wall -Wextra hello.cpp -o hello
```

```text
hello.cpp: In function 'int main()':
hello.cpp:5:44: error: expected ';' before 'return'
    5 |     std::cout << "Hello, C++!" << std::endl  // 这里少了分号
      |                                            ^
      |                                            ;
    6 |     return 0;
      |     ~~~~~~
```

编译器告诉你：在 `return` 之前，它期望看到一个分号。报错行号和排版会随源码、GCC 版本变化；关键是回头检查上一条输出语句的末尾。有时标记落在下一行，也别只盯着那个位置看。

### 忘记包含头文件

把 `#include <iostream>` 那行删掉，再编译：

```text
hello.cpp: In function 'int main()':
hello.cpp:5:10: error: 'cout' is not a member of 'std'
    5 |     std::cout << "Hello, C++!" << std::endl;
      |          ^~~~
hello.cpp:1:1: note: 'std::cout' is defined in header '<iostream>'; this is probably fixable by adding '#include <iostream>'
  +++ |+#include <iostream>
    1 | // Platform: host | Standard: C++17
hello.cpp:5:40: error: 'endl' is not a member of 'std'
    5 |     std::cout << "Hello, C++!" << std::endl;
      |                                        ^~~~
hello.cpp:1:1: note: 'std::endl' is defined in header '<ostream>'; this is probably fixable by adding '#include <ostream>'
  +++ |+#include <ostream>
    1 | // Platform: host | Standard: C++17
```

编译器说"cout 不是 std 的成员"——因为它根本不知道 `std::cout` 是什么，没人告诉过它。解决方案就是加回 `#include <iostream>`。有些 GCC 版本还会直接提示缺少哪个头文件，我们顺着这个线索检查就好。

### 拼写错误

把 `std::cout` 写成 `std::couth`：

```text
hello.cpp: In function 'int main()':
hello.cpp:6:10: error: 'couth' is not a member of 'std'; did you mean 'cout'?
    6 |     std::couth << "Hello, C++!" << std::endl;
      |          ^~~~~
      |          cout
```

错误信息很直接——`couth` 不是 `std` 的成员。仔细检查拼写就行。这类错误在初学阶段特别常见，`cout` 和 `cin` 经常被敲成 `couth` 和 `cim` 之类的，多写几遍就熟悉了。

> 如果你用的是 GCC，**建议编译时加上 `-Wall -Wextra` 选项**，即 `g++ -std=c++17 -Wall -Wextra hello.cpp -o hello`。这两个选项会开启大量的警告信息——虽然警告不阻止编译，但它们往往指向潜在的问题。**把警告当错误来对待，是成为合格 C++ 程序员的第一步哦~**。

## 再进一步——和程序对话

光能输出还不够，我们让程序能接收输入。新建一个文件叫 `calc.cpp`，实现一个简单的加法计算器。

我们先写骨架，再逐步填充。首先，我们需要从用户那里读取两个数字，所以要用到 `std::cin`，它是 `std::cout` 的好搭档。

```cpp
#include <iostream>

int main()
{
    int a = 0;
    int b = 0;

    std::cout << "请输入第一个数字: ";
    std::cin >> a;

    std::cout << "请输入第二个数字: ";
    std::cin >> b;

    int sum = a + b;
    std::cout << a << " + " << b << " = " << sum << std::endl;

    return 0;
}
```

编译运行：

```bash
g++ -std=c++17 -Wall -Wextra calc.cpp -o calc
./calc
```

```text
请输入第一个数字: 1
请输入第二个数字: 2
1 + 2 = 3
```

这里有几个值得注意的地方。`int a = 0;` 声明了一个整数类型的变量，并且初始化为 0。`std::cin >> a;` 中的 `>>` 运算符和 `<<` 的方向相反——它从输入流中"抽取"数据放到变量 `a` 里。你可以把 `<<` 理解为"推出去"（输出），`>>` 理解为"拉进来"（输入），箭头的方向就是数据的流向。

`std::cout << a << " + " << b << " = " << sum << std::endl;` 这一行连续用了多个 `<<` 运算符，它们从左到右依次执行：先输出 `a` 的值，再输出字符串 `" + "`，再输出 `b` 的值，依此类推。这种"链式"写法在 C++ 里非常常见，习惯就好。

变量声明那里，我们用了 `int a = 0;` 而不是 `int a;`，这是故意的。对这里的普通局部 `int` 而言，单写 `int a;` 不会给它一个确定的初值；在本篇采用的 C++17 中，赋值前读取这样的不确定值会导致未定义行为，不能把它当成“随便读到一个随机数”。虽然在输入成功时 `std::cin` 会写入它，但养成"声明即初始化"的习惯非常重要，这能帮你避开一大类难以调试的问题。

## 把 std:: 说清楚——命名空间与 `::`

骨架里咱们交代过一句「std 是个命名空间」，一笔带过。到现在为止您已经写了一路的 `std::cout`、`std::cin`，是时候把这个前缀讲透了——后面每一卷您都得跟它打交道，而且自己写工程时，命名空间是组织名字的第一件工具。

先说它解决的问题。C 项目里名字冲突是老大难：链了三个第三方库，每家都有一个 `init()`，链接阶段当场一堆 `multiple definition`。C 的惯例是给名字加前缀——`sensor_init()`、`uart_init()`、`display_init()`，能跑，但写起来又长又臭，而且挡不住两个库都叫 `network_buffer_create()` 这种撞车。C++ 的命名空间从语言层面解决这件事：它本质上是在编译期给每个名字自动加上「姓」，替换发生在编译期，**没有任何运行时开销**——最终生成的符号和手写长前缀没区别，只是这个又长又臭的限定名不用您自己搓。

定义用 `namespace` 关键字，咱们拿一个传感器模块当例子：

```cpp
// sensor.hpp —— 声明
namespace sensor {
    const int MAX_READINGS = 100;

    struct Reading {
        float temperature;
        float humidity;
    };

    void init();
    Reading get_reading();
}

// sensor.cpp —— 实现放回同一个命名空间，编译器自动合并两处
namespace sensor {
    void init()
    {
        // 初始化传感器硬件
    }

    Reading get_reading()
    {
        Reading r{};
        // 读取传感器数据
        return r;
    }
}
```

同一个命名空间可以分布在多个文件里，头文件声明、`.cpp` 实现，各自用 `namespace sensor { ... }` 包住就行。使用的时候有三种姿势，从最明确到最宽松：

```cpp
int main()
{
    // 姿势一：完全限定名——打字多，但绝不会歧义
    sensor::init();
    sensor::Reading data = sensor::get_reading();

    // 姿势二：using 声明——只引入特定的名字
    using sensor::Reading;
    Reading data2 = sensor::get_reading();

    // 姿势三：using 指令——整个命名空间倒进来
    using namespace sensor;
    init();
    Reading data3 = get_reading();

    return 0;
}
```

在 `.cpp` 文件的函数体内用姿势三（比如 `using namespace std`），大多数人不会说什么。但有一条必须遵守的规则：**头文件里绝对不要写 `using namespace`**。它是不可撤销的——头文件一旦全局引入一个命名空间，所有 `#include` 它的代码都被迫接受这个命名空间的全部符号，哪怕它们完全不知情。等两个库的同名符号在您最想不到的地方撞上，歧义报错能让您查到怀疑人生。

命名空间可以嵌套，用层级反映模块的层级很自然，比如一个硬件抽象层：

```cpp
namespace hardware {
    namespace gpio {
        enum PinMode { INPUT, OUTPUT, ALTERNATE };
        void set_mode(int pin, PinMode mode);
    }
    namespace uart {
        void init(int baudrate);
        void send(const char* data);
    }
}

// 用起来
hardware::gpio::set_mode(5, hardware::gpio::OUTPUT);
hardware::uart::init(115200);

// 嫌长？起个别名
namespace hw = hardware;
hw::gpio::set_mode(5, hw::gpio::OUTPUT);
```

别名只在当前作用域有效，所以不同函数里给同一个命名空间起不同的短名，互不干扰。C++17 还提供了一个更省事的嵌套写法：

```cpp
// C++17 起，等价于上面的两层嵌套
namespace hardware::gpio {
    void set_mode(int pin, PinMode mode);
}
```

这里有个咱们真跑出来的细节：按标准这是 C++17 的能力，但本机 GCC 16 连 `-std=c++14` 都放行（当扩展处理），要加 `-pedantic-errors` 才拦，报错原话是 `nested namespace definitions only available with '-std=c++17' or '-std=gnu++17'`。所以别拿「能不能编译过」当判断依据——项目锁在 C++11/14 的话，自觉一层一层嵌套写。

还有一个容易被忽视的实用特性：**匿名命名空间**。`namespace { ... }` 里定义的东西只有当前 `.cpp` 文件可见——效果就是 C 里 `static` 的文件级可见性，但覆盖面广得多：

```cpp
namespace {
    const int BUFFER_SIZE = 256;

    void internal_helper()
    {
        // 内部辅助函数，别的翻译单元看不见
    }
}
```

`static` 连类的定义都套不上——`static class Foo { ... };` 在 GCC 下的报错原话是 `a storage class can only be specified for objects and functions`（咱们实测过）；匿名命名空间则类、结构体、枚举、模板统统能包。所以新代码建议一律用匿名命名空间，老代码里的 `static` 看到了也不用急着改。

最后把 `::` 本身说透。它的语义就一句话：**从左边的作用域里，取右边的名字**。左边可以是命名空间（`math::PI`）、类（`UARTConfig::DEFAULT_BAUDRATE`，类的静态成员，讲到类的那章见）、甚至为空——`::value` 表示全局作用域里那个 `value`，在局部变量把同名全局遮住时用来翻案：

```cpp
int value = 100;  // 全局

void function()
{
    int value = 50;  // 局部，遮蔽了全局的 value
    printf("Local: %d\n", value);     // 50
    printf("Global: %d\n", ::value);  // 100
}
```

C 里一旦局部遮蔽全局，函数内就再也没法访问全局版本了，C++ 用 `::` 补上了这个洞。当然，最好的做法仍然是避免同名遮蔽——`::` 能解决语法问题，解决不了可读性。

<OnlineCompilerDemo
  title="命名空间与作用域解析"
  source-path="code/examples/vol1/14_namespace_reference.cpp"
  description="在线运行并观察命名空间嵌套、引用传参和 :: 作用域解析的实际行为。"
  allow-run
/>

## 动手试试

到这里，我们已经能把代码写出来、编译、运行、读错误信息了。接下来是检验你学习成果的时候了——光看不练等于没学。以下是三个练习，难度递增，建议每个都动手写一遍。

### 练习一：输出你的名字

修改 `hello.cpp`，让程序输出你的名字而不是 "Hello, C++!"。比如输出 "大家好啊！我是说的道理！"。

### 练习二：读取年龄并问候

写一个新程序 `age.cpp`，用 `std::cin` 读取用户的年龄，然后输出一段包含年龄的问候语。预期交互效果如下：

```text
请输入你的年龄: 24
你好！你今年 24 岁了，是个学生。
```

### 练习三：摄氏转华氏

写一个 `convert.cpp`，读取一个摄氏温度，转换为华氏温度后输出。转换公式是 `F = C * 9 / 5 + 32`。预期交互效果：

```text
请输入摄氏温度: 25
25°C = 77°F
```

这三个练习覆盖了本章的几个核心知识点：变量声明、输入输出、基本运算。如果三个都能独立完成，可以再用自己的话解释一遍“编译成功”和“程序运行”的区别。

## 在线运行

试着在线编辑并运行这段代码，修改输出内容看看效果：

<OnlineCompilerDemo
  title="第一个 C++ 程序：Hello World 与简单计算"
  source-path="code/examples/vol1/01_first_program.cpp"
  description="在浏览器中编辑并运行你的第一个 C++ 程序，观察输出结果。"
  allow-run
/>

## 参考资源

- [GCC：Overall Options](https://gcc.gnu.org/onlinedocs/gcc/Overall-Options.html)：`-E`、`-S`、`-c`、`-o` 的含义。
- [GCC：头文件搜索路径](https://gcc.gnu.org/onlinedocs/cpp/Search-Path.html)：尖括号与双引号形式的差别。
- [cppreference：main 函数](https://en.cppreference.com/w/cpp/language/main_function.html)：本篇讨论宿主环境，示例采用 C++17。
- [cppreference：std::endl](https://en.cppreference.com/w/cpp/io/manip/endl.html)：换行与刷新流。
- [cppreference：默认初始化](https://en.cppreference.com/w/cpp/language/default_initialization.html)：本篇局部 `int` 的说明限定在 C++17。
