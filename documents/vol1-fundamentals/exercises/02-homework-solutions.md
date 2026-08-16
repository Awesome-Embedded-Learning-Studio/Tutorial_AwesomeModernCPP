---
title: "卷 1 · 基础 课后练习参考答案（Homework）"
description: "卷 1（C++ 基础入门）课后练习的逐题详细解答：每道题给出解题思路、逐步解答（每步标注知识点链接）与真实验证输出。全部输出在 WSL Arch（g++/gcc 16.1.1，与教程同款工具链）真编译真跑得到，UB 与内存类题目附 ASan/UBSan 实测报告。"
chapter: 1
order: 2
tags: [host, beginner, cpp-modern, 基础]
difficulty: beginner
platform: host
cpp_standard: [11, 14, 17]
reading_time_minutes: 45
prerequisites: []
related: []
---

# 卷 1 · 基础 课后练习参考答案（Homework）

> 所有命令与输出在 WSL Arch（g++ 16.1.1 / gcc 16.1.1）下真实运行得到。UB 类题目的输出「只是这台机器这一次的选择」——换编译器、换优化级别都可能不同，这正是这类题要你亲手体会的东西。地址、PID 与 ASan 报告的堆栈十六进制细节已略去，其余照实。

## 0.1-A {#hw-0-1-a}

**难度 L1** · 题面见 [homework](./01-homework.md#hw-0-1-a)

**思路**：把教材的摄氏转华氏反过来做一遍；真正的整数除法陷阱不在 `double` 输入的换算公式上（整数字面量会被提升），而在 `int` 型的反向换算表上——表里的摄氏温度是 `int`，不先转 `double` 就会在 `m * 9 / 5` 处被截断。

1. 读入 `double f`，`double c = (f - 32.0) * 5.0 / 9.0;`。注意：写成 `(f - 32) * 5 / 9` 的「整数版」结果**完全一样**——`f` 是 `double`，整数字面量 $32$、`5`、`9` 会被提升成 `double` 参与运算，输入 77.5 时两种写法都输出 $25.27777778$。公式里的浮点字面量在这里只是防御性习惯，不是正确性的前提。→ 知识点：[类型转换](../ch01/02-type-conversion.md)「整数提升与算术转换」一节
2. 反向换算表里 `marks[i]` 是 `int`，必须先 `static_cast<double>` 再乘除，否则 `m * 9 / 5` 里混入整数除法（$-10 / 0 / 10 / 25$ 恰好整除看不出坑，换 37 就现形：$\frac{37 \times 9}{5}$ 整数除是 66，$66 + 32 = 98$，而浮点版是 98.6）。→ 知识点：[基本数据类型](../ch01/01-basic-types.md)「选型的智慧」一节

**验证输出**：

```text
$ g++ -std=c++17 -Wall -Wextra fahrenheit_to_celsius.cpp -o fahrenheit_to_celsius && echo "77.5" | ./fahrenheit_to_celsius
请输入华氏温度: 77.5°F = 25.3°C
写法一(浮点字面量): 25.27777778
写法二(整数字面量): 25.27777778  (两者一致)
--- 反向换算表(int 版 vs 先转 double 的版本) ---
-10°C -> int 版 14°F, 转 double 版 14°F
0°C -> int 版 32°F, 转 double 版 32°F
10°C -> int 版 50°F, 转 double 版 50°F
25°C -> int 版 77°F, 转 double 版 77°F
37°C -> int 版 98°F, 转 double 版 98.6°F
```

输入 77.5 时两种写法都输出 $25.27777778$——整数字面量被提升成 `double`，截断并不发生；换算表里 `-10 → 14`、`0 → 32`、`25 → 77` 恰好整除看不出坑，换 $37$ 就现形：int 版 $98°F$ 对转 double 版 $98.6°F$，差掉的 $0.6°F$ 就是整数除法截断的代价。$77.5°F = 25.3°C$（保留 1 位）与教材 $25°C = 77°F$ 是同一套公式的正反两个方向，对得上。

## 0.1-B {#hw-0-1-b}

**难度 L2** · 题面见 [homework](./01-homework.md#hw-0-1-b)

**思路**：四种坏法分别卡在流水线的不同工位——头文件不存在死在预处理，语法错死在编译，名字没见过也死在编译（语义阶段），声明了却没定义死在链接。

1. `bad_preprocess.cpp`：`#include <nonexistent_header_xyz.h>` 在预处理阶段就要把那个头文件的内容文本插进来，找不到文件直接 `fatal error`，连编译都到不了。→ 知识点：[第一个 C++ 程序](../ch00/03-first-program.md)「幕后发生了什么——编译流水线」一节
2. `bad_compile.cpp`：漏分号是语法错误，编译器给出 `expected ';' before 'return'`——注意它把错标记在第 5 行、实际根因在第 4 行末尾，这种「报错行比实际错行差一行」正是教材提醒过的规律。→ 知识点：[第一个 C++ 程序](../ch00/03-first-program.md)「那些年我们踩过的坑」一节
3. `bad_include.cpp`：没 include 时编译器不认识 `std::cout`，报 `'cout' is not a member of 'std'`——g++ 16 还很贴心地建议你加 `#include <iostream>`。→ 知识点：[第一个 C++ 程序](../ch00/03-first-program.md)「第一行：`#include <iostream>`」一节
4. `bad_link.cpp`：声明在编译阶段合法（编译器相信 `helper` 存在），链接器去符号表里找不到 `helper(int)` 的定义，报 `undefined reference to 'helper(int)'`。→ 知识点：[第一个 C++ 程序](../ch00/03-first-program.md)「编译流水线」一节（链接阶段才要求定义兑现）
5. 双错合并实验：同一文件既缺 include 又缺分号，编译器报出的**第一条**错误是 `'cout' is not a member of 'std'`——因为 include 问题先暴露；缺分号的错要等 include 修好后才会登场。先修第一条、重新编译，下一批错误才会浮出来，这就是「级联报错」和「看第一条」的原因。→ 知识点：[第一个 C++ 程序](../ch00/03-first-program.md)「踩坑预警：一定要看第一条错误信息」

**验证输出**：

```text
$ g++ -std=c++17 bad_preprocess.cpp -o bad_preprocess
bad_preprocess.cpp:2:10: fatal error: nonexistent_header_xyz.h: No such file or directory
    2 | #include <nonexistent_header_xyz.h>
      |          ^~~~~~~~~~~~~~~~~~~~~~~~~~
compilation terminated.

$ g++ -std=c++17 bad_compile.cpp -o bad_compile
bad_compile.cpp: In function 'int main()':
bad_compile.cpp:5:38: error: expected ';' before 'return'
    5 |     std::cout << "hello" << std::endl
      |                                      ^
      |                                      ;
    6 |     return 0;

$ g++ -std=c++17 bad_include.cpp -o bad_include
bad_include.cpp: In function 'int main()':
bad_include.cpp:3:10: error: 'cout' is not a member of 'std'
    3 |     std::cout << "hello" << std::endl;
      |          ^~~~
bad_include.cpp:1:1: note: 'std::cout' is defined in header '<iostream>'; this is probably fixable by adding '#include <iostream>'
  +++ |+#include <iostream>
    1 | int main()
bad_include.cpp:3:34: error: 'endl' is not a member of 'std'
    3 |     std::cout << "hello" << std::endl;
      |                                  ^~~~
bad_include.cpp:1:1: note: 'std::endl' is defined in header '<ostream>'; this is probably fixable by adding '#include <ostream>'
  +++ |+#include <ostream>
    1 | int main()

$ g++ -std=c++17 bad_link.cpp -o bad_link
/usr/bin/ld: bad_link.cpp:(.text+0x2b): undefined reference to `helper(int)'
collect2: error: ld returned 1 exit status

$ g++ -std=c++17 double_error.cpp -o double_error
double_error.cpp: In function 'int main()':
double_error.cpp:3:10: error: 'cout' is not a member of 'std'
double_error.cpp:3:34: error: 'endl' is not a member of 'std'
```

双错文件里第一条错误指向 include 缺失——修复它、重新编译，缺分号的错误才会登场。

## 1.1-A {#hw-1-1-a}

**难度 L1** · 题面见 [homework](./01-homework.md#hw-1-1-a)

**思路**：8 个 `sizeof` 背后是三组规则——窄类型参与运算先提升成 `int`、字符字面量在 C++ 里是 `char`、字符串字面量自带一个隐藏的 `'\0'`。

1. `sizeof(c + c) == 4`：`char` 参与算术会先整型提升为 `int`，运算在 4 字节宽度上进行。→ 知识点：[类型转换](../ch01/02-type-conversion.md)「整数提升与算术转换」一节
2. `sizeof('A') == 1`：C++ 里字符字面量 `'A'` 的类型是 `char`——这和 C 完全不同，C 里字符常量是 `int`（所以同样一行代码在 C 里是 4）。这正是教程 ch01 练习二提示过的两门语言的分叉点。→ 知识点：[基本数据类型](../ch01/01-basic-types.md)「字符类型」一节
3. `sizeof("abc") == 4`：字符串字面量的类型是 `const char[4]`——3 个可见字符加 1 个结尾 `'\0'`。→ 知识点：[指针运算与数组](../ch04/02-pointer-arithmetic.md)（字符串字面量的真实类型）
4. 本机 `std::numeric_limits<char>::is_signed == true`、范围 `-128 ~ 127`——这是 x86 平台的事实，但 **ARM 上 char 经常是无符号的**，所以「char 有符号」绝不能写进跨平台假设。→ 知识点：[基本数据类型](../ch01/01-basic-types.md)「字符类型」一节

**验证输出**：

```text
$ g++ -std=c++17 -Wall -Wextra sizeof_quiz.cpp -o sizeof_quiz && ./sizeof_quiz
sizeof(char)      = 1
sizeof(c + c)     = 4
sizeof('A')       = 1
sizeof(true)      = 1
sizeof(3.14)      = 8
sizeof(3.14f)     = 4
sizeof("abc")    = 4
sizeof(flag)      = 1
char 有符号?      = true
char 取值范围:    -128 ~ 127
```

$3.14$ 是 `double`（8 字节）、`3.14f` 是 `float`（4 字节）——不带后缀的浮点字面量默认 `double`，这也是教材「浮点运算用 double」那条建议的字面量基础。

## 1.1-B {#hw-1-1-b}

**难度 L2** · 题面见 [homework](./01-homework.md#hw-1-1-b)

**思路**：三个场景分别演示整数除法截断、有符号转无符号的「巨正数」陷阱、向零取整的除余方向。

1. 错版 $\frac{9999 \times 33}{1000} \times 3$：$9999 \times 33 = 329967$，$\frac{329967}{1000} = 329$（整数除法把 $0.967$ 截掉），$329 \times 3 = 987$；对版在除法前把 `rate_milli` 转成 `double`，$\frac{329967.0}{1000} = 329.967$，乘 3 得 $989.901$——差掉的 $2.901$ 元就是截断的代价。→ 知识点：[类型转换](../ch01/02-type-conversion.md)「整数除法的陷阱」一节
2. `(-1 < 1u)` 为假：混合符号比较时 `int` 装不下 `unsigned` 的全部值域，两边一起转 `unsigned`，$-1$ 变成 $4294967295$，自然不小于 1。`-Wsign-compare` 会精确点名这一行。→ 知识点：[类型转换](../ch01/02-type-conversion.md)「有符号与无符号的碰撞」一节
3. `-7 / 2 == -3`（向零取整，不是向负无穷）、`-7 % 2 == -1`（余数符号跟随被除数）、`7 % -2 == 1`。→ 知识点：[类型转换](../ch01/02-type-conversion.md)「数值精度」一节

**验证输出**：

```text
$ g++ -std=c++17 -Wall -Wextra -Wsign-compare conversion_traps.cpp -o conversion_traps
conversion_traps.cpp: In function 'int main()':
conversion_traps.cpp:19:43: warning: comparison of integer expressions of different signedness: 'int' and 'unsigned int' [-Wsign-compare]
   19 |     std::cout << "(-1 < 1u) 为 " << ((neg < one) ? "真" : "假") << std::endl;
      |                                       ~~~~^~~~~
$ ./conversion_traps
利息(错版): 987
利息(对版): 989.901
(-1 < 1u) 为 假
static_cast<unsigned>(-1) = 4294967295
-7 / 2 = -3, -7 % 2 = -1, 7 / -2 = -3, 7 % -2 = 1
```

## 2.1-A {#hw-2-1-a}

**难度 L1** · 题面见 [homework](./01-homework.md#hw-2-1-a)

**思路**：分诊就是教材成绩分级的「换皮」——关键在边界值 $37.3$ 必须落进「发热」分支，所以用 `>=` 而不是 `>`。

1. `if / else if / else` 链从上到下依次检查，条件一旦成立后面的分支全部跳过——所以 `>= 37.3` 必须放在最前面。→ 知识点：[条件语句](../ch02/01-conditionals.md)「if 和 if-else」一节
2. 三元运算符版适合「一句话在两个值里选一个」的场合，嵌套到第二层可读性就开始崩，所以这里只演示一层嵌套并把它限定在简单的标签选择上。→ 知识点：[条件语句](../ch02/01-conditionals.md)「三元运算符」一节

**验证输出**：

```text
$ g++ -std=c++17 -Wall -Wextra triage.cpp -o triage && ./triage
35.8 度 -> 偏低
36.5 度 -> 正常
37.3 度 -> 发热
38.9 度 -> 发热
36.5 度(三元版) -> 正常
```

边界值 $37.3$ 正确落进「发热」——如果把第一个条件写成 `> 37.3`，37.3 会掉进「正常」分支，这就是「差一个等号差一个世界」。

## 2.1-B {#hw-2-1-b}

**难度 L2** · 题面见 [homework](./01-homework.md#hw-2-1-b)

**思路**：空心菱形的关键是把「第 row 行打几个前导空格、星号层多宽」用 `d = |row - N/2|` 这一个量统一描述。

1. 外层循环走 7 行；内层三个循环分别打前导空格、左边星号、中间空格加右边星号。顶行 `d = 3`、宽度 1（只打一个星号），中线 `d = 0`、宽度 7（两颗星夹 5 个空格）。→ 知识点：[循环语句](../ch02/02-loops.md)「嵌套循环」一节
2. 用 `std::abs` 算 `d` 是因为下半部分是上半部分的镜像——行号距离中线的距离是对称的，这也正是教材菱形练习「下半部分是金字塔的镜像」那条提示的另一种表达。→ 知识点：[循环语句](../ch02/02-loops.md)「第五步——嵌套循环」一节

**验证输出**：

```text
$ g++ -std=c++17 -Wall -Wextra hollow_diamond.cpp -o hollow_diamond && ./hollow_diamond
   *
  * *
 *   *
*     *
 *   *
  * *
   *
```

## 3.1-A {#hw-3-1-a}

**难度 L1** · 题面见 [homework](./01-homework.md#hw-3-1-a)

**思路**：递归幂的基线条件是 `exp == 0`；考拉兹序列之所以用 `long long`，是因为 27 的过程值会先飙到 9232——这个规模 `int` 其实装得下，但再大的数呢？养成「中间量比输入宽一档」的习惯总没错。

1. 递归版 `power`：`exp == 0` 返回 1（基线条件），否则 `base * power(base, exp - 1)`——没有基线条件递归就不会停下来，这正是教材强调的「递归太深就是栈溢出」。→ 知识点：[函数基础](../ch03/01-function-basics.md)「实战演练——functions.cpp」一节
2. 迭代版 `collatz_steps`：`while (n != 1)` 里用三元表达式分奇偶推进，每推进一步计数加一。`collatz_steps(1)` 输出 0——循环条件一开始就不满足，循环体一次都没执行，这正好演示了 `while` 的「先判断后执行」。→ 知识点：[循环语句](../ch02/02-loops.md)「第一步——while 循环」一节

**验证输出**：

```text
$ g++ -std=c++17 -Wall -Wextra collatz.cpp -o collatz && ./collatz
power(2, 10) = 1024
power(5, 0)  = 1
power(2, -1) = -1 (约定值)
collatz_steps(27) = 111
collatz_steps(6)  = 8
collatz_steps(1)  = 0
```

`collatz_steps(27) == 111` 是考拉兹序列的著名测试点，对上了说明推进逻辑没写错。

## 3.1-B {#hw-3-1-b}

**难度 L2** · 题面见 [homework](./01-homework.md#hw-3-1-b)

**思路**：`swap_bad` 改的是形参副本，出了函数就烟消云散；`min_max` 有两个结果要带出来，一个 `return` 装不下，只能走引用输出参数。

1. `swap_bad(x, y)` 之后 `x`、`y` 纹丝不动：值传递把实参拷贝进形参，函数内交换的只是副本。→ 知识点：[参数传递方式](../ch03/02-pass-by-value-ref.md)「值传递——函数拿到的是副本」一节
2. `swap_ok` 用 `int&` 绑定到调用者的变量，交换才真正发生。→ 知识点：[参数传递方式](../ch03/02-pass-by-value-ref.md)「引用传递——直接操作原始数据」一节
3. `min_max` 的两个引用输出参数在函数返回后依然可读——它们是调用者变量的别名；如果试图传字面量 `min_max(1.0, 2.0, 3.0, 0.0, 0.0)`，非 const 引用不能绑定右值，编译直接失败，这也是引用比指针更安全的表现之一。→ 知识点：[值类别简介](../ch01/04-value-categories.md)「引用绑定的规则」一节

**验证输出**：

```text
$ g++ -std=c++17 -Wall -Wextra minmax.cpp -o minmax && ./minmax
swap_bad 后: x=3, y=9
swap_ok 后:  x=9, y=3
min=-1.2, max=8.8
```

## 4.1-A {#hw-4-1-a}

**难度 L2** · 题面见 [homework](./01-homework.md#hw-4-1-a)

**思路**：`*p = *q` 是「把 q 指向的数据拷给 p 指向的位置」，`p = q` 是「让 p 指向 q 指向的地方」——前者动数据，后者动指向。

1. `*p = *q` 之后 `x` 变成 9、`y` 还是 9（x 拿到了 y 的值，y 本身没动）。→ 知识点：[指针基础](../ch04/01-pointer-basics.md)「解引用」一节
2. `p = q` 之后 p 和 q 指向同一个位置；`*p = 20` 改的是 y，于是 `x=9, y=20`，而 `*p` 和 `*q` 都是 20。→ 知识点：[指针基础](../ch04/01-pointer-basics.md)「指针变量」一节
3. 空指针必须先判空再解引用——解引用 `nullptr` 是未定义行为。→ 知识点：[指针基础](../ch04/01-pointer-basics.md)「空指针」一节

**验证输出**：

```text
$ g++ -std=c++17 -Wall -Wextra trace.cpp -o trace && ./trace
初值: x=5, y=9
*p = *q 后:      x=9, y=9
p = q; *p=20 后: x=9, y=20
*p=20, *q=20
null_ptr 是空指针,跳过解引用
```

## 4.1-B {#hw-4-1-b}

**难度 L2** · 题面见 [homework](./01-homework.md#hw-4-1-b)

**思路**：`my_strlen` 是「指针走到 `'\0'` 再相减」，反转是「左右双指针夹逼交换」——两件事都只用指针算术，一个下标都不写。

1. `my_strlen` 用 `s - start` 求元素距离，这正是教材说的指针减法「结果是多少个元素」。→ 知识点：[指针运算与数组](../ch04/02-pointer-arithmetic.md)「指针减法——计算元素距离」一节
2. `reverse_in_place` 的双指针是面试和算法题的常客；`char text[]` 是栈上的可写数组，而字符串字面量在只读段——两个地址的数值量级对比（0x5d... 对 0x7f...）正好印证[内存布局](../ch12/01-memory-layout.md)里「代码/只读段在低地址、栈在高地址」。→ 知识点：[指针运算与数组](../ch04/02-pointer-arithmetic.md)「用指针遍历数组」「实战：综合演示」两节

**验证输出**：

```text
$ g++ -std=c++17 -Wall -Wextra reverse_cstr.cpp -o reverse_cstr && ./reverse_cstr
反转前: modern cpp (长度 10)
反转后: ppc nredom
字面量地址(只读段): 0x5db63e244028
栈数组地址(栈):     0x7ffea9b67afd
```

地址每次运行都会变（ASLR），但「只读段地址远小于栈地址」的相对关系不会变。

## 5.1-A {#hw-5-1-a}

**难度 L1** · 题面见 [homework](./01-homework.md#hw-5-1-a)

**思路**：只读遍历用 `const auto&`，原地修改必须用 `auto&`——按值遍历改的只是副本，编译不报错、结果却纹丝不动。

1. `std::minmax_element` 一趟同时拿到最小和最大两个迭代器；求和用 range-for 累加。→ 知识点：[std::array](../ch05/02-std-array.md)「实战：用 std::array 重写 C 数组代码」一节
2. 修正循环用 `for (auto& t : temps)`——引用形式让修改落到原数组上。如果误用 `for (auto t : temps)`，改的是每次迭代拷出来的副本，原数组「编译通过、运行不报错、但结果不对」。→ 知识点：[range-for 循环](../ch02/03-range-for.md)「搭配 auto 的三种姿势」一节

**验证输出**：

```text
$ g++ -std=c++17 -Wall -Wextra temp_stats.cpp -o temp_stats && ./temp_stats
原始数据: 23.1 19.4 26.7 18.2 25 21.8 27.3 22.6
最低: 18.2, 最高: 27.3
平均: 23.0125
修正后: 23.1 20.9 26.7 19.7 25 21.8 27.3 22.6
修正后最低: 19.7, 最高: 27.3
```

$19.4 + 1.5 = 20.9$、$18.2 + 1.5 = 19.7$，修正后的最低从 18.2 抬到 19.7——输出自洽。注意 $25$ 而不是 $25.0$：默认精度下 $25.0$ 打印成 $25$，想固定小数位要用 `std::fixed` + `std::setprecision`。

## 5.1-B {#hw-5-1-b}

**难度 L2** · 题面见 [homework](./01-homework.md#hw-5-1-b)

**思路**：切词就是「find 下一个空格 → substr 截取 → 越过空格」的循环；替换的循环里，替换完必须把搜索起点挪到替换串之后，否则替换串里含目标串就会死循环。

1. `longest_word` 里每轮用 `find(' ', pos)` 定位空格，`npos` 表示「到串尾了」。`"modern cpp standard library strings"` 里最长的是 `standard`（8 字符），比 `library`（7）长——先猜成 `library` 的朋友，猜错本身就是收获。→ 知识点：[std::string](../ch05/03-std-string.md)「查找与子串」一节
2. `replace_all` 先处理 `from.empty()`：`find("")` 永远返回 0，不防住就是死循环。→ 知识点：[std::string](../ch05/03-std-string.md)「实战演练」一节（find_and_replace 的 `pos += replacement.size()` 模式）

**验证输出**：

```text
$ g++ -std=c++17 -Wall -Wextra words.cpp -o words && ./words
最长单词: standard
替换后: awesome cpp standard library strings
find("xyz"): 未找到(npos)
```

## 6.1-A {#hw-6-1-a}

**难度 L1** · 题面见 [homework](./01-homework.md#hw-6-1-a)

**思路**：`Circle` 就是教材 `Point`/`Rectangle` 的圆化——私有 `radius_` 守住「半径非负」这条不变量，全部修改都从公有接口走。

1. 构造函数把负半径钳成 0：类内不变量（半径非负）在**构造时**就成立，而不是指望调用者自觉。→ 知识点：[类的定义](../ch06/01-class-basics.md)「访问控制：public、private、protected」一节
2. `area()`、`perimeter()`、`contains()` 都只读不改对象，全部加 `const`；`scale()` 要改 `radius_`，不能加。漏掉 `const` 的后果教材讲过：通过 `const Circle&` 拿对象的人将调不了你的只读接口。→ 知识点：[this 指针与链式调用](../ch06/06-this-and-cascading.md)「const 成员函数与 this 的关系」一节
3. `contains` 的判定是「点到圆心距离 ≤ 半径」；`(2.2, 2.2)` 到圆心距离 $\sqrt{9.68} \approx 3.11 > 3$，所以在圆外——选这组数据就是为了让两个点一个在边界内、一个在边界外。→ 知识点：[类的定义](../ch06/01-class-basics.md)「实战演练：point.cpp」一节

**验证输出**：

```text
$ g++ -std=c++17 -Wall -Wextra circle.cpp -o circle && ./circle
Circle(r=3)
面积: 28.2743, 周长: 18.8496
(2,2) 在圆内?   是
(2.2,2.2) 在圆内? 否
放大 2 倍后面积: 113.097
负半径构造: Circle(r=0)
```

核对关键数值：$9\pi \approx 28.2743$、$6\pi \approx 18.8496$，放大 2 倍后 $36\pi \approx 113.097$——面积随半径平方增长，半径翻倍面积翻 4 倍，输出正好印证。

## 6.1-B {#hw-6-1-b}

**难度 L2** · 题面见 [homework](./01-homework.md#hw-6-1-b)

**思路**：编号和存活计数属于「整个类」而不是某个对象，所以进 static；静态成员在类内只是声明，必须类外定义一次（C++17 的 `inline static` 可以省掉这一步）。

1. 构造函数从 `next_id_` 取号再自增、`active_count_` 加一；析构减一。→ 知识点：[static 成员](../ch06/04-static-members.md)「静态成员变量」「实战：自动 ID 分配器」两节
2. 无参版本用委托构造把 `("匿名", 0.0)` 交给主构造函数——委托构造的初始化列表里只能写目标构造函数，不能再混成员。→ 知识点：[构造函数](../ch06/02-constructors.md)「委托构造」一节
3. `withdraw(600)` 余额 500 失败、`deposit(-50)` 金额非法失败——校验逻辑收在公有接口里，余额永不为负。→ 知识点：[类的定义](../ch06/01-class-basics.md)「访问控制」一节

**验证输出**：

```text
$ g++ -std=c++17 -Wall -Wextra bank.cpp -o bank && ./bank
初始: 活户 0, 下一个编号 1001
Alice  #1001 余额 500
Bob    #1002 余额 100
匿名   #1003 余额 0
提款失败(余额不足)
存款失败(金额非法)
作用域内活户数: 4
作用域外活户数: 3
下一个编号(只增不减): 1005
```

注意最后一行：临时账户销毁后 `active_count_` 回落到 3，但 `next_id_` 已经是 1005——编号只增不减，这正是教材强调的「`next_id_` 不随对象销毁回退」。


## 7.1-A {#hw-7-1-a}

**难度 L3** · 题面见 [homework](./01-homework.md#hw-7-1-a)

**思路**：完全照教材 `Fraction` 的实现模式来——复合赋值做成员、二元运算做非成员并按值取左操作数、比较全部锚定同一个「真相源」`total_seconds_`。

1. `operator+=`/`operator-=` 是成员函数、返回 `*this` 引用——链式赋值 `(a += b) = c` 才能和内置类型行为一致。→ 知识点：[算术与比较运算符](../ch07/01-arithmetic-comparison.md)「从 `operator+=` 开始搭建算术运算」一节
2. `operator+` 按值接收左操作数，直接在副本上 `+=` 再返回——既复用了逻辑，又天然避开了「返回局部对象引用」的悬垂。→ 知识点：[算术与比较运算符](../ch07/01-arithmetic-comparison.md)「踩坑预警：二元算术运算符必须按值返回」一节
3. `==` 和 `<` 都只比较 `total_seconds()`：时分秒三个字段是同一个总量的三种拆法，各自比较容易漏对齐；锚定一个字段就是教材说的「单一真相源」。→ 知识点：[算术与比较运算符](../ch07/01-arithmetic-comparison.md)「比较运算符」一节
4. `<<` 按教材模式实现为非成员（左操作数是 `ostream`，不可能是你的类）。→ 知识点：[流与下标运算符](../ch07/02-io-subscript.md)「重载 `<<`」一节

**验证输出**：

```text
$ g++ -std=c++17 -Wall -Wextra timespan.cpp -o timespan && ./timespan
1h1m11s + 0h2m5s = 1h3m16s
1h1m11s - 0h2m5s = 0h59m6s
a 与 b 不等
a 不短于 b
a == c 成立
a += 49s 后: 1h2m0s
```

手算核验：$3671 + 125 = 3796 = 1\times3600 + 3\times60 + 16$ ✓；$3671 - 125 = 3546 = 59\times60 + 6$ ✓；`3671 + 49 = 3720 = 1h2m0s` ✓。

## 7.1-B {#hw-7-1-b}

**难度 L3** · 题面见 [homework](./01-homework.md#hw-7-1-b)

**思路**：`m[1][2]` 分两步走——`m[1]` 调 `Matrix3::operator[]` 返回一个行代理，`[2]` 再调代理自己的 `operator[]` 翻译成一维下标。为什么不能直接返回 `double&`？因为 `operator[]` 每次只拿到一个下标，光凭行号没法定位单个元素，必须把「行」这个中间状态装进代理对象里。

1. `Row` 代理持有 `std::array<double, 9>*` 和行号，`operator[](col)` 算 `row * 3 + col`。→ 知识点：[流与下标运算符](../ch07/02-io-subscript.md)「下标运算符 operator[]」一节（代理是「经典手法」的出处）
2. const 正确性分两层：`Row` 自己要有 const 版 `operator[]`（只读），`Matrix3` 也要有 const 版 `operator[]`。这里选了「两个代理类」的路线——`Row` 可读写、`ConstRow` 只读，`const Matrix3` 的 `operator[]` 返回 `ConstRow`。另一条路线是让代理类同时提供可读写与只读两套接口、靠调用方的 const 性选择，各有利弊。→ 知识点：[流与下标运算符](../ch07/02-io-subscript.md)「踩坑预警：忘了提供 const 版本」一节
3. `trace()` 吃对角线 `data_[0] + data_[4] + data_[8]`。→ 知识点：[C 风格数组](../ch05/01-c-arrays.md)（行优先存储的下标换算）

**验证输出**：

```text
$ g++ -std=c++17 -Wall -Wextra matrix3.cpp -o matrix3 && ./matrix3
矩阵:
1 2 3
4 5 6
7 8 9
迹(trace): 15
const 读取 m[1][2] = 6
改写 m[2][0] 后:
1 2 3
4 5 6
99 8 9
```

$1 + 5 + 9 = 15$ ✓；`const Matrix3&` 走 `ConstRow` 只读路径，改写走 `Row` 可写路径——两条路径互不干扰。

## 8.1-A {#hw-8-1-a}

**难度 L2** · 题面见 [homework](./01-homework.md#hw-8-1-a)

**思路**：构造从基类到派生类、析构严格反过来；按值传参时派生类特有部分被「切掉」，只剩一个 `Vehicle` 骨架。

1. 构造顺序输出 `[Vehicle] 构造 → [Car] 构造`，析构反过来 `[Car] 析构 → [Vehicle] 析构`——先打地基再盖楼、先拆楼上再拆地基。→ 知识点：[单继承](../ch08/01-single-inheritance.md)「构造与析构的顺序」一节
2. `show_by_ref(car)` 输出 `Panda (4 座)`，`show_by_value(car)` 只剩 `Panda`——按值传参把 `Car` 拷贝进一个 `Vehicle` 变量，`seats_` 字面意义上被切掉；而且切片后的副本是货真价实的 `Vehicle` 对象，虚函数查到的是基类的 vtable，所以即便 `describe` 是虚的也调不到 `Car` 版。→ 知识点：[单继承](../ch08/01-single-inheritance.md)「对象切片」一节、[虚函数与多态](../ch08/02-virtual-functions.md)「vtable 揭秘」一节
3. 按值版本输出里多出一句 `[Vehicle] 析构: Panda`——那就是参数副本在函数结束时析构的证据（副本构造走的是隐式拷贝构造，我们没有给它加打印）。→ 知识点：[构造函数](../ch06/02-constructors.md)「拷贝构造」一节

**验证输出**：

```text
$ g++ -std=c++17 -Wall -Wextra vehicles.cpp -o vehicles && ./vehicles
--- 构造顺序 ---
  [Vehicle] 构造: Panda
  [Car] 构造: 4 座
--- 引用传递(不切片) ---
[引用] Panda (4 座)
--- 按值传递(切片!) ---
[按值] Panda
  [Vehicle] 析构: Panda
  [Vehicle] 构造: Forever
  [Bicycle] 构造
[引用] Forever (两轮)
  [Bicycle] 析构
  [Vehicle] 析构: Forever
  [Car] 析构
  [Vehicle] 析构: Panda
--- 作用域结束,析构顺序 ---
```

`Bicycle` 在 `Car` 之后构造却先析构——「后构造的先析构」贯穿整条输出。

## 8.1-B {#hw-8-1-b}

**难度 L3** · 题面见 [homework](./01-homework.md#hw-8-1-b)

**思路**：容器装 `unique_ptr<Employee>` 而不是 `Employee` 值，有两条理由——`Employee` 是抽象类根本没法实例化，而且按值存储会切片；指针容器配合虚函数才有多态。

1. 三个派生类各自重写 `salary()`，主循环只依赖基类接口，加新人不用改循环。→ 知识点：[虚函数与多态](../ch08/02-virtual-functions.md)「virtual 关键字」一节
2. 基类析构必须 `virtual`：`unique_ptr<Employee>` 析构时通过基类指针 `delete` 派生类对象，析构函数不虚就只调基类析构——派生类成员若有资源就直接漏。→ 知识点：[虚函数与多态](../ch08/02-virtual-functions.md)「虚析构函数」一节
3. `std::make_unique` 是推荐的创建方式，比裸 `new` 少一层泄漏风险。→ 知识点：[智能指针预告](../ch04/04-smart-ptr-preview.md)「unique_ptr——独占所有权」一节

**验证输出**：

```text
$ g++ -std=c++17 -Wall -Wextra payroll.cpp -o payroll && ./payroll
Alice: 12000
Bob: 7280
Carol: 5500
总支出: 24780
```

$12000 + 45.5\times160(=7280) + 3000+2500(=5500) = 24780$ ✓。

## 9.1-A {#hw-9-1-a}

**难度 L2** · 题面见 [homework](./01-homework.md#hw-9-1-a)

**思路**：模板参数 `kN` 从「数组引用」里推导出来——这是数组引用参数的独门绝技：编译器看到的不是退化后的指针，而是完整的数组类型。

1. 参数写成 `T (&arr)[kN]`，编译器从实参 `int[5]` 同时推导出 `T = int` 和 `kN = 5`——长度信息在编译期就被「钉」进模板里，这正是教材 `print_array` 用的同款手法。→ 知识点：[函数模板](../ch09/01-function-templates.md)「实战演练」一节（`const T (&arr)[kSize]`）
2. 必须是引用：按值传 `T arr[kN]` 会退化成 `T*`，`kN` 无从推导；引用参数还让反转能改到原数组。→ 知识点：[函数模板](../ch09/01-function-templates.md)「类型推导」一节
3. 三组数据各实例化一个 `array_reverse` 版本（`int[5]`、`double[3]`、`std::string[4]`），外加 `array_print` 的三个版本——这就是模板的「代码配方按需展开」。→ 知识点：[函数模板](../ch09/01-function-templates.md)「模板实例化」一节

**验证输出**：

```text
$ g++ -std=c++17 -Wall -Wextra reverse_tpl.cpp -o reverse_tpl && ./reverse_tpl
[3, 1, 4, 1, 5]
[5, 1, 4, 1, 3]
[22.5, 23.1, 19.8]
[19.8, 23.1, 22.5]
[Mon, Tue, Wed, Thu]
[Thu, Wed, Tue, Mon]
```

## 9.1-B {#hw-9-1-b}

**难度 L3** · 题面见 [homework](./01-homework.md#hw-9-1-b)

**思路**：环形缓冲区的灵魂是「读指针 + 计数」两个量——写位置不需要单独存，因为 `(read_index_ + count_) % kCapacity` 就是下一个空位。

1. `kCapacity` 是非类型模板参数：容量在编译期就定死，底层 `std::array` 零堆分配，正是嵌入式友好的形态。→ 知识点：[类模板](../ch09/02-class-templates.md)「非类型参数」一节
2. 满时覆盖最旧元素的语义：`count_ == kCapacity` 时把 `read_index_` 前移一位，最早的元素被「挤出去」——传感器场景里最新读数永远比最旧的重要。→ 知识点：[类模板](../ch09/02-class-templates.md)「上号——实现一个完整的泛型栈」一节（模板类成员的设计思路）
3. `pop` 从 `read_index_` 取数后前移并计数减一；`print` 从 `read_index_` 出发走 `count_` 步即可完整输出——两个量足以描述整个缓冲区。→ 知识点：[循环缓冲区](../ch09/02-class-templates.md)（模板 + 取模的组合）

**验证输出**：

```text
$ g++ -std=c++17 -Wall -Wextra ringbuf.cpp -o ringbuf && ./ringbuf
[1, 2, 3] size=3 full=否
[2, 3, 4, 5] size=4 full=是
依次弹出: 2 3 4 5
double 缓冲首个弹出: 1.5
```

push 4、5 之后 `1` 被覆盖，弹出序列 `2 3 4 5`——最旧元素被顶掉的语义完全正确。

## 10.1-A {#hw-10-1-a}

**难度 L2** · 题面见 [homework](./01-homework.md#hw-10-1-a)

**思路**：`at()` 与 `[]` 的差别就是「抛异常」与「未定义行为」的差别；catch 块从具体到一般排列，匹配到第一个就停。

1. `safe_divide` 抛 `std::invalid_argument`；`v.at(5)` 在 size 为 3 时抛 `std::out_of_range`，而 `v[5]` 不做任何检查、越界即 UB。→ 知识点：[异常基础](../ch10/01-try-catch.md)「标准异常层次」一节
2. 三个 catch 的顺序：`out_of_range` 是 `logic_error` 的子类，`logic_error` 又是 `exception` 的子类——一般类型放前面会把后面全部变成死代码，因为匹配从上到下、先到先得。→ 知识点：[异常基础](../ch10/01-try-catch.md)「标准异常层次——exception 家族」一节
3. `e.what()` 返回的 `const char*` 指向异常对象内部存储，异常对象随 catch 块结束销毁——把指针存出去用就是悬空。→ 知识点：[异常基础](../ch10/01-try-catch.md)「按值抛出，按 const 引用捕获」一节

**验证输出**：

```text
$ g++ -std=c++17 -Wall -Wextra safe_div.cpp -o safe_div && ./safe_div
--- 安全除法 ---
10 / 4 = 2.5
7 / 0 = 捕获 invalid_argument: 除数为零
-8 / 2 = -4
--- at() 越界(层次化捕获) ---
捕获 out_of_range: vector::_M_range_check: __n (which is 5) >= this->size() (which is 3)
```

`what()` 的原文是 libstdc++ 的实现细节（`vector::_M_range_check: __n (which is 5)...`），换 libc++ 措辞会不同——别把这段文本写进断言里。

## 10.1-B {#hw-10-1-b}

**难度 L3** · 题面见 [homework](./01-homework.md#hw-10-1-b)

**思路**：异常从 `risky_step` 抛出后，控制流直接跳到 catch——坏版的两个 `delete[]` 物理上永远执行不到；好版靠 `unique_ptr` 的析构在栈展开时兜底。

1. 坏版普通构建跑起来「看起来没事」：程序不崩，只是内存悄悄少了 8192 字节。这正是裸指针 + 异常最阴险的地方。→ 知识点：[异常安全](../ch10/02-exception-safety.md)「无保证」一节
2. 坏版 ASan 构建在退出时点名两块泄漏：`process_bad` 第 13、14 行的两个 `new[]`，合计 `8192 byte(s) leaked in 2 allocation(s)`。→ 知识点：[异常安全](../ch10/02-exception-safety.md)「RAII 与异常安全」一节、[动态内存管理](../ch12/02-new-delete.md)「用 AddressSanitizer 抓泄漏」一节
3. 好版零报告：栈展开保证局部对象的析构一定执行，「要 RAII 就 RAII 到底」。→ 知识点：[异常安全](../ch10/02-exception-safety.md)「踩坑预警：RAII 的前提」一节

**验证输出**：

```text
$ ./leak_bad
[坏版] 已分配两块内存
捕获: 中途抛异常
$ g++ -std=c++17 -g -O0 -fsanitize=address,undefined leak_bad.cpp -o leak_bad_asan && ASAN_OPTIONS=detect_leaks=1 ./leak_bad_asan
[坏版] 已分配两块内存
捕获: 中途抛异常

=================================================================
==316==ERROR: LeakSanitizer: detected memory leaks

Direct leak of 4096 byte(s) in 1 object(s) allocated from:
    #1 ... in process_bad(bool) leak_bad.cpp:14
Direct leak of 4096 byte(s) in 1 object(s) allocated from:
    #1 ... in process_bad(bool) leak_bad.cpp:13

SUMMARY: AddressSanitizer: 8192 byte(s) leaked in 2 allocation(s).
=================================================================
$ g++ -std=c++17 -g -O0 -fsanitize=address,undefined leak_good.cpp -o leak_good_asan && ASAN_OPTIONS=detect_leaks=1 ./leak_good_asan
[好版] 已分配两块内存
捕获: 中途抛异常
```

（ASan 报告的堆栈十六进制地址已略去；`int[1024]` 与 `double[512]` 各 4096 字节，正好对上两行 `new[]`。）

## 11.1-A {#hw-11-1-a}

**难度 L2** · 题面见 [homework](./01-homework.md#hw-11-1-a)

**思路**：`std::remove_if` 只「搬」不「删」——它把活下来的元素挪到前面、返回新逻辑末尾，真正的删除交给 `erase`；两步走是 STL「算法不直接操作容器接口」哲学的必然。

1. `remove_if` 的谓词 `r < 0 || r > 60` 把 $-5$ 和 $99$ 两个异常值标记出来，`erase` 真正删掉。→ 知识点：[std::vector 快速上手](../ch11/01-vector.md)「Remove-Erase 惯用法」一节
2. `erase` 之后 `capacity` 仍为 8：`erase` 只析构元素、改 `size`，内存不还——`size` 是元素数、`capacity` 是已分配的槽位，两者从出生就是两回事。→ 知识点：[std::vector 快速上手](../ch11/01-vector.md)「理解 size 和 capacity」一节
3. range-for 里不能直接 `erase`（迭代器失效），先标记再统一 remove-erase 才是正路。→ 知识点：[STL 常用模式](../ch11/04-stl-patterns.md)「坑二：遍历中修改容器」一节

**验证输出**：

```text
$ g++ -std=c++17 -Wall -Wextra pipeline.cpp -o pipeline && ./pipeline
清洗并排序后: 20 21 22 23 24 25
平均: 22.5
size=6, capacity=8
```

## 11.1-B {#hw-11-1-b}

**难度 L3** · 题面见 [homework](./01-homework.md#hw-11-1-b)

**思路**：统计阶段 `++freq[word]` 里 `operator[]` 的「不存在就插入 0」恰好是我们想要的语义；但 Top 3 需要按值排序，而 map 只能按键有序——所以搬进 vector 再排，这就是「map 管统计、vector 管排序」的分工。

1. `++freq[word]`：第一次遇到自动插入 0 再自增。这个场景用 `operator[]` 是**合理**的——我们就是要「访问时自动创建」；换成只读查找就必须 `find`/`count`。→ 知识点：[关联容器快速上手](../ch11/02-map-set.md)「踩坑预警：map[key] 自动插入」一节
2. Top 3 用 `std::stable_sort`：并列次数的词在排序后保持 map 里的字典序，输出可复现——`std::sort` 不保证稳定性，并列项的顺序随实现漂移。→ 知识点：[算法库初见](../ch11/03-algorithms-intro.md)「std::sort 与 std::stable_sort」一节

**验证输出**：

```text
$ g++ -std=c++17 -Wall -Wextra wordfreq.cpp -o wordfreq && ./wordfreq
--- 词频表(map 字典序) ---
  brown: 1
  dog: 1
  fox: 2
  jumps: 1
  lazy: 1
  over: 1
  quick: 1
  the: 3
--- Top 3 ---
  the: 3
  fox: 2
  brown: 1
```

词频表按字典序输出（红黑树有序性）；Top 3 里 `the` 出现 3 次、`fox` 2 次，第三名并列时稳定排序把 `brown` 留在最前。

## 12.1-A {#hw-12-1-a}

**难度 L2** · 题面见 [homework](./01-homework.md#hw-12-1-a)

**思路**：四大区域在地址数值上的相对关系是铁证——代码段最低、数据/BSS 居中、堆在中间往上、栈最高往下；子函数栈变量地址比父函数更小，就是栈向下增长的直接证据。

1. `main` 地址（0x5c...）最小——代码段在最低处；`g_initialized` 与 `s_local` 挤在一起（0x5c...3040/3044）——都在数据段；`g_uninitialized` 略靠后——BSS 段。→ 知识点：[内存布局](../ch12/01-memory-layout.md)「程序的四大内存区域」一节
2. 栈变量（0x7f...）远大于堆变量（0x5c...），且「栈地址 > 堆地址」「栈地址 > 数据段地址」都为真。→ 知识点：[内存布局](../ch12/01-memory-layout.md)「动手验证——打印各区域的地址」一节
3. `probe_stack` 的局部变量地址比 `main` 的栈变量更小——函数调用沿低地址方向开辟新栈帧。→ 知识点：[内存布局](../ch12/01-memory-layout.md)「栈内存」一节
4. 判断题：`msg` 指针本身在数据段，`"error"` 字面量在只读段；函数内 `static int visits` 在数据段。→ 知识点：[内存布局](../ch12/01-memory-layout.md)「静态和全局内存」一节

**验证输出**（地址取自某次真实运行，数值每次不同、相对关系不变）：

```text
$ g++ -std=c++17 -O0 -g layout.cpp -o layout && ./layout
代码段(main 地址):        0x5ce979f301fb
数据段 g_initialized:     0x5ce979f33040
BSS 段 g_uninitialized:   0x5ce979f33194
数据段 static 局部:       0x5ce979f33044
栈 stack_var:             0x7ffdb3c2b98c
堆 heap_var:              0x5ce99f832020
  子函数栈变量 inner: 0x7ffdb3c2b964
栈地址 > 堆地址:     是
栈地址 > 数据段地址: 是
```

## 12.1-B {#hw-12-1-b}

**难度 L3** · 题面见 [homework](./01-homework.md#hw-12-1-b)

**思路**：`SensorRecord` 的 `char` 后面要垫 7 字节才能放下 8 字节对齐的 `double`，尾部还要再垫 2 字节凑 8 的倍数——把大对齐成员放前面，这些垫片大部分消失。

1. `SensorRecord`：`tag@0`、`sample@8`（前 7 字节 padding）、`id@16`、`gain@20`，总大小补到 24。`SensorRecordTight`：`sample@0`、`id@8`、`gain@12`、`tag@14`，补 1 字节尾部垫片到 16——**字段一样，顺序不同，省 8 字节**。→ 知识点：[内存对齐与填充](../ch12/03-alignment-padding.md)「sizeof 的真相」一节
2. `PacketHeader`：`version@0`、`flags@1`、`sequence@4`（前垫 2）、`timestamp@8`，共 16——两个 `char` 后跟 `int` 同样要垫。→ 知识点：[内存对齐与填充](../ch12/03-alignment-padding.md)「对齐规则」一节
3. 网络报文场景：不同编译器/平台的填充规则一致（ABI 规定），但成员顺序改变布局——`memcpy` 进出报文的结构体必须两边用同一份定义，重排字段就是改协议。→ 知识点：[内存对齐与填充](../ch12/03-alignment-padding.md)「踩坑预警：成员声明顺序」一节

**验证输出**：

```text
$ g++ -std=c++17 -O0 -g align.cpp -o align && ./align
SensorRecord:      sizeof=24 tag@0 sample@8 id@16 gain@20
SensorRecordTight: sizeof=16 sample@0 id@8 gain@12 tag@14
PacketHeader:      sizeof=16 version@0 flags@1 sequence@4 timestamp@8
节省: 8 字节
```

## CT-1-A {#hw-ct-1-a}

**难度 L2** · 题面见 [homework](./01-homework.md#hw-ct-1-a)

**思路**：位运算四件套的套路是死的——置位用 `|`、清零用 `& ~`、翻转用 `^`、检查用 `&`；拆包先右移再掩码是为了把目标位段挪到低端、和无关位分开，组包先左移再或是为了把位段放到正确位置。

1. `reg |= (1u << 2)` 置位、`reg ^= (1u << 0)` 翻转、`reg &= ~(1u << 2)` 清零——每一步后打印 `0x%02X` 和 8 位二进制，肉眼对得上。→ 知识点：[位运算与求值顺序](../c_tutorials/03B-bitwise-and-evaluation.md)「四大经典操作」一节
2. `0xB4` 拆包：`(b >> 4) & 0x0F` 得高 4 位 `0xB`，`b & 0x0F` 得低 4 位 `0x4`；组包 `(0x7 << 4) | 0x9` 得 `0x79`。→ 知识点：[位运算与求值顺序](../c_tutorials/03B-bitwise-and-evaluation.md)（移位与掩码的组合）
3. 注意 `1u` 的无符号后缀：移位溢出和符号位是 C/C++ 里最容易翻车的暗坑，位运算一律用无符号操作数。→ 知识点：[位运算与求值顺序](../c_tutorials/03B-bitwise-and-evaluation.md)「移位注意事项」一节

**验证输出**：

```text
$ gcc -std=c11 -Wall -Wextra bitops.c -o bitops && ./bitops
置位后: 0x05 00000101
检查: 第 2 位是 1
检查: 第 1 位是 0
翻转后: 0x04 00000100
清零后: 0x00 00000000
0xB4 拆包: 高 4 位 = 0xB, 低 4 位 = 0x4
组包 0x7|0x9 = 0x79
```

## CT-1-B {#hw-ct-1-b}

**难度 L3** · 题面见 [homework](./01-homework.md#hw-ct-1-b)

**思路**：`strncpy` 在源串不短于 n 时**不会**补 `'\0'`，必须手动补；`snprintf` 的返回值是「本该写入的总长度」，`>= sizeof(buf)` 就是截断判据；`strcpy` 溢出在普通构建下被栈保护器抓住，在 ASan 下被精确点名。

1. `strncpy(dst, src, sizeof(dst) - 1); dst[7] = '\0';` 得到 `"Modern "`（7 字符）——`strlen` 是 7，`my_strlen` 的指针版给出同样的数。→ 知识点：[C 字符串与缓冲区安全](../c_tutorials/11-c-strings-and-buffer-safety.md)「长度与复制」一节
2. `snprintf` 把 28 字符写进 20 字节缓冲：返回值 28 ≥ 20，触发截断警告；gcc 16 在**编译期**就用 `-Wformat-truncation` 提前预警了。→ 知识点：[C 字符串与缓冲区安全](../c_tutorials/11-c-strings-and-buffer-safety.md)「snprintf 安全格式化」一节
3. 埋雷实验：普通构建下 `*** stack smashing detected ***: terminated`（退出码 134）——是 GCC 默认的栈保护器（Stack Protector）抢在崩溃前拦下的，不是 UB 的「正常表现」；换编译器/关掉保护，它可能什么都不报。ASan 构建则给出确定性的 `stack-buffer-overflow`，`WRITE of size 28`，并点名越界变量 `'small'`。→ 知识点：[C 字符串与缓冲区安全](../c_tutorials/11-c-strings-and-buffer-safety.md)「第四步——理解缓冲区溢出为什么这么危险」一节

**验证输出**：

```text
$ gcc -std=c11 -Wall -Wextra safe_str.c -o safe_str && ./safe_str
safe_str.c:23:49: warning: '%s' directive output truncated writing 28 bytes into a region of size 20 [-Wformat-truncation=]
安全拷贝: "Modern " (strlen=7)
snprintf 返回 28, 缓冲内容 "super-long-sensor-i"
警告: 内容被截断(需 28 字节,缓冲只有 20)
my_strlen("hello") == 5

$ ./boom
*** stack smashing detected ***: terminated
(退出码 134)

$ gcc -std=c11 -g -O0 -fsanitize=address,undefined boom.c -o boom_asan && ASAN_OPTIONS=detect_leaks=1 ./boom_asan
=================================================================
==437==ERROR: AddressSanitizer: stack-buffer-overflow
WRITE of size 28 at ... thread T0
    #1 ... in main boom.c:6
  This frame has 1 object(s):
    [32, 37) 'small' (line 5) <== Memory access at offset 37 overflows this variable
SUMMARY: AddressSanitizer: stack-buffer-overflow boom.c:6 in main
```

（ASan 报告的地址与影子字节图已略去。）

## C-1 {#hw-c-1}

**难度 L4** · 题面见 [homework](./01-homework.md#hw-c-1)

**思路**：`Fraction` 照教材实现（`normalize` 约分 + 分母恒正），解析层负责把字符串变分数、把非法输入变成异常，存储层交给 `vector` + 算法——三层各司其职。

1. `parse_fraction` 用 `find('/')` + `substr` 切开分子分母，缺分隔符、分母为零都抛 `std::invalid_argument`——解析与校验在入口处一次性完成，非法数据根本进不了容器。→ 知识点：[std::string](../ch05/03-std-string.md)「查找与子串」一节、[异常基础](../ch10/01-try-catch.md)「throw、try、catch 三件套」一节
2. `std::sort` 用 `operator<`（通分比较），`std::accumulate` 的初值必须是 `Fraction(0, 1)` 且二元操作用 lambda 返回 `acc + f`——初值类型决定累加类型，这是 `accumulate` 的经典坑。→ 知识点：[算法库初见](../ch11/03-algorithms-intro.md)「算一算」一节
3. 平均分 = 总和除以个数：`sum / Fraction(5, 1)`，除法走 `operator/=`。→ 知识点：[算术与比较运算符](../ch07/01-arithmetic-comparison.md)「成员还是非成员——一个影响深远的选择」一节

**验证输出**：

```text
$ g++ -std=c++17 -Wall -Wextra frac_reader.cpp -o frac_reader && ./frac_reader
解析失败: 分母为零: 1/0
排序后: 5/12 1/2 2/3 3/4 7/6
总和: 7/2
平均: 7/10
```

手算核验：通分到 12，五个分数是 $9/12, 6/12, 8/12, 14/12, 5/12$，排序 $5/12 < 1/2 < 2/3 < 3/4 < 7/6$ ✓；总和 $42/12 = 7/2$ ✓；平均 $(7/2) / 5 = 7/10$ ✓。

## C-2 {#hw-c-2}

**难度 L4** · 题面见 [homework](./01-homework.md#hw-c-2)

**思路**：三个陷阱共享一条主线——「引用/指针指向的东西还活着吗」：临时对象靠 const 引用续命、切片让值容器里的对象变成半截货、扩容让旧指针指向已释放的内存。

1. `const std::string& label = std::string("sensor-") + std::to_string(7);` 合法：const 左值引用绑定右值时，编译器把临时对象的生命周期延长到引用作用域结束——这就是 `const T&` 参数能同时吃左值和右值的根基。→ 知识点：[值类别简介](../ch01/04-value-categories.md)「引用绑定的规则」一节
2. 值容器输出 `Shape 面积 0`、指针容器输出 `Circle 面积 12.5664`：`push_back(Circle(2.0))` 进 `vector<Shape>` 时，编译器只拷贝了基类部分，vtable 也随之换成基类的——切片发生在**拷贝进容器**那一刻。→ 知识点：[单继承](../ch08/01-single-inheritance.md)「对象切片」一节
3. 普通构建下扩容后 `*p` 读出一个垃圾值（本机这次是 $-293940770$，每次运行都不同）——旧内存被释放了，但读它「恰好不崩」，这就是 UB 最阴险的样子；ASan 构建直接报 `heap-use-after-free`，点名 `_M_realloc_append` 释放了旧缓冲、你的代码又读它。`reserve(100)` 之后指针全程稳定。→ 知识点：[std::vector 快速上手](../ch11/01-vector.md)「踩坑预警：迭代器失效」一节、[STL 常用模式](../ch11/04-stl-patterns.md)「坑一：迭代器失效」一节

**验证输出**：

```text
$ ./traps
[1] const 引用绑定的临时串: sensor-7
[2] 值容器: Shape 面积 0
[2] 指针容器: Circle 面积 12.5664
[3] 扩容前 *p = 1
[3] 扩容后 *p = -293940770 (悬垂解引用)
[3] reserve 后 *p = 10 (指针稳定)

$ g++ -std=c++17 -g -O0 -fsanitize=address,undefined traps.cpp -o traps_asan && ASAN_OPTIONS=detect_leaks=1 ./traps_asan
[1] const 引用绑定的临时串: sensor-7
[2] 值容器: Shape 面积 0
[2] 指针容器: Circle 面积 12.5664
[3] 扩容前 *p = 1
=================================================================
==490==ERROR: AddressSanitizer: heap-use-after-free
READ of size 4 at ... thread T0
    #0 ... in part3_iterator_invalidation() traps.cpp:48
    #1 ... in main traps.cpp:64
freed by thread T0 here:
    #5 ... in std::vector<int, std::allocator<int> >::_M_realloc_append(int const&) .../vector.tcc:649
    #7 ... in part3_iterator_invalidation() traps.cpp:46
previously allocated by thread T0 here:
    #6 ... in part3_iterator_invalidation() traps.cpp:42
SUMMARY: AddressSanitizer: heap-use-after-free traps.cpp:48 in part3_iterator_invalidation()
```

（ASan 报告中的十六进制地址与影子字节图已略去。）

## C-3 {#hw-c-3}

**难度 L5** · 题面见 [homework](./01-homework.md#hw-c-3)（改编自 LeetCode #42 Trapping Rain Water，Hard）

**思路**：暴力版把「每个位置能接多少水」独立计算——左右各扫一遍拿最大高度；双指针版抓住一个不变量：**较矮一侧的水位已经被该侧最大高度决定**，所以每次推进矮侧，不需要知道另一侧的具体高度。

1. 暴力版：对位置 i，`min(左最大, 右最大)` 是水位上限，减去 `height[i]` 就是接水量，负数取 0。O(n²)，但结论是「金标准」，用来验证双指针。→ 知识点：[循环语句](../ch02/02-loops.md)「嵌套循环」一节
2. 双指针版：`height[left] < height[right]` 时推进左指针——左侧被右侧「挡住」，水位由 `left_max` 决定；反之推进右指针。O(n) 且只扫一遍。→ 知识点：[指针运算与数组](../ch04/02-pointer-arithmetic.md)「实战：综合演示 ptr_arith.cpp」一节
3. `{3,0,1,4}` 手算：位置 1 接 $3-0=3$、位置 2 接 $3-1=2$，共 5——两组数据对上了才能放心。→ 知识点：[std::vector 快速上手](../ch11/01-vector.md)（容器与遍历）

**验证输出**（-O0 与 -O2 各跑一遍）：

```text
$ g++ -std=c++17 -Wall -Wextra rain.cpp -o rain && ./rain
暴力=6 双指针=6 (一致)
暴力=9 双指针=9 (一致)
暴力=2 双指针=2 (一致)
暴力=5 双指针=5 (一致)
$ g++ -std=c++17 -Wall -Wextra -O2 rain.cpp -o rain_o2 && ./rain_o2
暴力=6 双指针=6 (一致)
暴力=9 双指针=9 (一致)
暴力=2 双指针=2 (一致)
暴力=5 双指针=5 (一致)
```

四组数据全部对上：LeetCode 官方样例 6 与 9、对称小例 2、自拟 5。双指针的关键不变量一句话：移动较矮一侧时，这一侧的水位只由**该侧最大高度**决定——因为对侧存在一个更高（或等高）的柱子，把该侧「兜住」了，所以不需要知道对侧的具体数值。

## 收尾

31 道题全部做完，卷 1 的家底你就真的摸过一遍了。有两点值得再咂摸：一是每道 UB/内存题你都亲眼看到了「普通构建装没事、sanitizer 一抓一个准」的对比——以后遇到「有时好有时坏」的 bug，先怀疑 UB；二是参考答案里的输出都是这台机器这一次的真实选择，你的环境数值可能有出入，但规律不会。接下来去[Lab](./03-lab.md)把 6 步流水线跑通，再去[Project](./05-project.md)把 BookShelf 盖起来。
