---
title: "模板实例化的对照：按需复印与链接器去重"
chapter: 11
order: 1
tags: [host, cpp-modern, intermediate]
difficulty: intermediate
platform: host
cpp_standard: [11, 17]
description: "同一份 max 模板，int 与 double 各得一份机器码。没人调用就一份不印，两个编译单元印重了，弱符号加 COMDAT 让链接器只留一份"
reading_time_minutes: 17
current_status:
  title: 正在打磨
  detail: 本批刚写完,正在通读打磨,表述与实测口径可能随时调整
  variant: polishing
---

# 模板实例化的对照：按需复印与链接器去重

[上一篇](../10-new-in-assembly/03-array-new-cookie-and-reverse-destruction.md)咱们跟着一句 new 走完了内存与构造的交接，全程看的都是咱们自己写下的代码。这一篇咱们换一个视角，看的换成了模板。您写 `max` 的时候只写了一份，调用它的时候既给了 `int` 又给了 `double`，程序照样跑得好好的，中间没有任何运行期的魔法在替您挑函数。挑选发生在编译期：编译器按每个用到的类型，把模板重新写成了普通函数，`T` 是 `int` 就写一份整型的机器码，`T` 是 `double` 就另写一份浮点的。这个按模板加具体类型生成实体的动作，标准里的名字叫实例化，咱们这一篇管它叫复印。

前面十篇咱们问的都是“我的代码落成了什么指令”，今天的问题换成了“编译器替我写的代码，落成了什么”。看的地方还是老几样，`nm` 帮咱们翻符号表，`objdump` 帮咱们翻汇编。模板的源码只有一份，落到目标文件里的却是一摞，咱们这一篇就把这一摞的来历整个看一遍：它是怎么出现的，怎么随类型多起来的，重复了又怎么合成一份的。

复印的比喻咱们立在开头，后面的行文全靠它带路。模板的定义是一张不出代码的原稿。哪个类型用到了，编译器就照着原稿给这个类型印了一份。印出来的份数一多，重名的事就得交给链接器，同名的只留一份。按需、按型、去重的三条规则，留到下一篇收尾的时候咱们再对着实测点齐。

## 咱们的实验环境

咱们照惯例把环境摆在前面，后面的每一块输出，您都可以在同样配置的机器上对照复现：

```text
$ g++ --version
g++ (GCC) 16.2.1 20260810
Copyright (C) 2026 Free Software Foundation, Inc.
This is free software; see the source for copying conditions.  There is NO
warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

$ uname -a
Linux DESKTOP-65DBAA7 6.18.33.2-microsoft-standard-WSL2 #1 SMP PREEMPT_DYNAMIC Thu Jun 18 21:54:43 UTC 2026 x86_64 GNU/Linux
```

咱们用的机器是 x86-64，编译出来的目标文件是 ELF 64 位的 relocatable 格式，也就是还没链接的中间产物。这一篇请出来的工具比前几篇多：`nm` 读符号表，`objdump` 给咱们做反汇编，`readelf` 读目标文件的节信息，`size` 帮咱们量体积，都是 binutils 一家的工具。这些工具多数是老熟人：`objdump` 从 04 起咱们就在用，`nm` 在 08 里跟咱们翻过符号，`readelf` 在 09 跟咱们一起读过节信息，只有 `size` 是今天头一回请出来的。

汇编的降噪统一用 `objdump -d -M att --no-show-raw-insn`，去掉了机器码的字节列，这一做法沿用 04 起的老办法，`-M att` 显式点明的是 AT&T 语法，这套语法从 02 起就是系列的默认。汇编行里 `#` 起头的注释都是咱们的解读，指令咱们逐字照贴，个别做了节选的地方会注明。

还有一个条件得郑重交代：本篇的编译命令一律带 `-O0`，也就是 01 里认过的直译档。咱们这几篇里，模板篇对它的要求是最严的，因为开 `-O2` 的话，咱们这几个小模板会被整体内联，`nm` 里会连一个 `max` 的符号都剩不下，整篇的观察对象都会消失。您自己复现的时候，编译命令请您原样带上 `-O0`，不然看到的会和咱们贴的对不上。优化档位下到底发生了什么，咱们在[下一篇](02-size-explicit-and-class-templates.md)量体积的时候拿实测数字专门说。

## 一份源码，印出两个函数

素材是一份最小模板：`max` 收两个同类型的引用，把大的那个还回去。`main` 里咱们拿 `int` 和 `double` 各调一次：

```cpp
// m1.cpp —— 一份模板，两种类型各调一次
template <typename T>
T max(T& a, T& b) {
    return a < b ? b : a;
}

int main() {
    int    xi = 1,  yi = 2;
    double xd = 1.5, yd = 2.5;

    int    r1 = max(xi, yi);   // 实例化 max<int>
    double r2 = max(xd, yd);   // 实例化 max<double>

    return r1 + static_cast<int>(r2);
}
```

```text
$ g++ -O0 -c m1.cpp -o m1.o
$ nm m1.o
0000000000000000 T main
                 U __stack_chk_fail
0000000000000000 W _Z3maxIdET_RS0_S1_
0000000000000000 W _Z3maxIiET_RS0_S1_
```

`nm` 的输出里，地址后面那个字母是符号的类型。`main` 前面的字母是 `T`，定义在 `.text` 里的普通符号，也就是行话说的强符号。`__stack_chk_fail` 前面的字母是 `U`，未定义的引用，这个文件用到了它但没有它的定义，咱们要等链接器从运行库里把它找来。它是栈保护的桩，跟咱们今天的主题没关系，后面咱们不再提它。两行 `max` 前面的字母是 `W`，weak 弱符号的意思是，这个定义允许和别处的同名定义并存，链接的时候挑一份保留。至于它为什么是弱的，咱们讲到链接器只留一份的时候，再拿实测看它的机制，这里您只需要记下一件事：两行 `W` 就是两个独立的函数。

修饰名的部分咱们拆一遍。名字修饰是编译器把函数名和类型信息编码进符号串的工序，02 里咱们拿 `_Z3addii` 认过套路，05 又认了类名的 `N...E` 写法。今天的新东西在模板上，咱们把 `_Z3maxIiET_RS0_S1_` 一段一段读：

- `_Z`：Itanium C++ ABI 名字修饰的前缀，见到它就是 C++ 的修饰名。GCC 在 Linux x86-64 上实现的就是这套 ABI。
- `3max`：函数名 `max`，3 是字符数，和 `_Z3addii` 里的 `3add` 同一个套路。
- `IiE`：今天的重点，`I` 和 `E` 括起来的是模板实参表，对应源码里的 `<...>`，里面一个 `i` 就是 `int`。整串念出来是“max 的 int 版”。
- `T_`：指第一个模板形参，在这里就是 `int` 本身。函数模板的返回类型依赖模板形参的时候，它会在这个位置被编码。
- `RS0_`：`R` 是左值引用，`S0_` 是替换记号，指回刚出现过的 `T_`，也就是 `int`，连起来是第一个形参 `int&`。
- `S1_`：一号替换，指回 `RS0_`，第二个形参还是 `int&`。

拼起来的结果是 `int max<int>(int&, int&)`。咱们不用背这套编码，拿 `c++filt` 解出来的结果就能互证，`nm -C` 直接给了解码后的样子：

```text
$ nm -C m1.o
0000000000000000 T main
                 U __stack_chk_fail
0000000000000000 W double max<double>(double&, double&)
0000000000000000 W int max<int>(int&, int&)
```

类型不同的时候，实参的字母也就不同，常用的几个咱们一对一对列出来：

- `d` 对应 double
- `c` 对应 char
- `f` 对应 float
- `l` 对应 long
- `s` 对应 short
- `j` 对应 unsigned int
- `x` 对应 long long

符号名也就跟着类型变了。“每个实例一份独立符号”，在名字层面说的就是这个尾巴。另外有一处容易让咱们起疑心的地方得交代：形参的写法会跟着进符号串。咱们的 `max` 形参是 `T&`，印出来的串是 `_Z3maxIiET_RS0_S1_`，要是改成传值的版本，串就变成了 `_Z3maxIiET_S0_S0_`。您在别的资料里见到的串要是跟这里对不上，多半是签名本身不一样造成的，咱们拿 `c++filt` 一解就能对证。

符号认完了，咱们看汇编。下面的输出做了节选，咱们只贴两个 `max` 函数，`main` 与栈保护的桩都略去了：

```asm
0000000000000000 <_Z3maxIiET_RS0_S1_>:
   0:	push   %rbp
   1:	mov    %rsp,%rbp
   4:	mov    %rdi,-0x8(%rbp)
   8:	mov    %rsi,-0x10(%rbp)
   c:	mov    -0x8(%rbp),%rax
  10:	mov    (%rax),%edx
  12:	mov    -0x10(%rbp),%rax
  16:	mov    (%rax),%eax
  18:	cmp    %eax,%edx
  1a:	jge    24 <_Z3maxIiET_RS0_S1_+0x24>
  1c:	mov    -0x10(%rbp),%rax
  20:	mov    (%rax),%eax
  22:	jmp    2a <_Z3maxIiET_RS0_S1_+0x2a>
  24:	mov    -0x8(%rbp),%rax
  28:	mov    (%rax),%eax
  2a:	pop    %rbp
  2b:	ret

0000000000000000 <_Z3maxIdET_RS0_S1_>:
   0:	push   %rbp
   1:	mov    %rsp,%rbp
   4:	mov    %rdi,-0x8(%rbp)
   8:	mov    %rsi,-0x10(%rbp)
   c:	mov    -0x8(%rbp),%rax
  10:	movsd  (%rax),%xmm1
  14:	mov    -0x10(%rbp),%rax
  18:	movsd  (%rax),%xmm0
   1c:	comisd %xmm1,%xmm0
   20:	jbe    2c <_Z3maxIdET_RS0_S1_+0x2c>
  22:	mov    -0x10(%rbp),%rax
  26:	movsd  (%rax),%xmm0
   2a:	jmp    34 <_Z3maxIdET_RS0_S1_+0x34>
  2c:	mov    -0x8(%rbp),%rax
  30:	movsd  (%rax),%xmm0
  34:	pop    %rbp
  35:	ret
```

两个函数的骨架是一模一样的：进门保存 rbp，把 rdi 和 rsi 送来的两个地址存进局部变量，做一次大小的比较，再按比较的结果挑一个返回。03 里咱们认过前两个参数走 rdi 和 rsi，这里多看一眼就能认出引用的真身：引用形参在机器上就是地址，rdi、rsi 送来的正是两个对象自己的地址。差别全在用什么指令搬数据、用什么指令比大小。`int` 版用 `mov (%rax),%eax` 把 32 位的整数取进通用寄存器，比较用的是 `cmp %eax,%edx`，分支用的是 `jge`，有符号的大于等于才跳，要交的返回值放在 `%eax` 里。`double` 版换了一整套：取数用的是 `movsd (%rax),%xmm1`，64 位双精度进的是 XMM 寄存器，比较用的是 `comisd %xmm1,%xmm0`，它把比较的结果写进 EFLAGS，后面的 `jbe` 拿这个结果决定跳不跳，返回值放的是 `%xmm0`。

源码里的 `a < b ? b : a` 咱们只写了一遍，编译器按 `T` 的实际类型把这一行写成了两遍机器码。整型比较和浮点比较在 x86-64 上用的是两套指令、两套寄存器，模板在编译期就把分派做完了，运行的时候每处 `call` 调的都是一个具体的函数，没有任何在运行期查类型的东西。咱们标题里问的那句“编译器替你写的代码”，它的实物就是这个。

## 没人用，就不印

复印的第一个特征是按需。咱们做一个最省事的对照，模板的定义原样放着，`main` 里的调用一个也没有：

```cpp
// m2.cpp —— 模板定义在场，但一次也没调用
template <typename T>
T max(T& a, T& b) {
    return a < b ? b : a;
}

int main() {
    return 0;   // 模板定义在,但一次也没调用
}
```

```text
$ g++ -O0 -c m2.cpp -o m2.o
$ nm m2.o
0000000000000000 T main
```

目标文件里就只剩一个 `main` 了。模板的定义只是一张不出代码的原稿，没有调用的场合，编译器就不出任何机器码了。然后咱们只加一行调用，别的什么都不动：

```cpp
// m3.cpp —— 在 m2 的基础上只加了一行调用
template <typename T>
T max(T& a, T& b) {
    return a < b ? b : a;
}

int main() {
    int x = 1, y = 2;
    return max(x, y);   // 就加了这一行调用
}
```

```text
$ g++ -O0 -c m3.cpp -o m3.o
$ nm m3.o
0000000000000000 T main
                 U __stack_chk_fail
0000000000000000 W _Z3maxIiET_RS0_S1_

$ nm -C m3.o
0000000000000000 T main
                 U __stack_chk_fail
0000000000000000 W int max<int>(int&, int&)
```

两份源码的唯一差别是 `main` 里那一行调用，`max<int>` 的符号就从无到有了。没有人写过 `template int max<int>(int&, int&);` 这样的指令，是调用点让编译器自己推出了 `T = int`，生成了实例。由使用触发的实例化就叫隐式实例化，隐式的意思是没有人明说，用法自己就把指令下了。cppreference 的[函数模板页面](https://en.cppreference.com/w/cpp/language/function_template)把这个条件写得很清楚，咱们检索的日期是 2026 年 10 月 13 日：

```text
When code refers to a function in context that requires the function definition
to exist, or if the existence of the definition affects the semantics of the
program(since C++11), and this particular function has not been explicitly
instantiated, implicit instantiation occurs.
```

咱们把它翻过来：当代码在需要函数定义存在的场合提到了这个函数，而它又没有被显式实例化过，隐式实例化就发生了。同一页还有一句更干脆的，正是 m2 的写照：

```text
A function template by itself is not a type, or a function. No code is
generated from a source file that contains only template definitions.
```

咱们把这句也翻成大白话：函数模板自己既不是类型也不是函数，只有模板定义的源文件不出任何代码。咱们在 m2 的 `nm` 里看到的那片空，就是这句话照进现实的样子。

## 各印一份之后，链接器只留一份

按需复印还有一个绕不开的后果。模板的定义要放在头文件里，这件事您在卷一大概当成惯例记下来了，原因在编译的流程里：每个 .cpp 连同它包含的头文件是一趟独立的编译，它的行话名字是编译单元，后面代码注释里写的 TU 说的也是它。比如 `a.cpp` 要实例化 `max<int>` 的时候，编译器就得在这一趟里见得到模板的全文，在见不到全文的场合，复印也就无从谈起了。那么两个编译单元都用 `max<int>`，两边各印了一份，链接的时候重名了怎么办？咱们做一遍：

```cpp
// m4.hpp —— 模板定义进头文件
#ifndef M4_HPP
#define M4_HPP

template <typename T>
T max(T& a, T& b) {
    return a < b ? b : a;
}

#endif
```

```cpp
// m4_a.cpp —— 这个编译单元实例化 max<int>
#include "m4.hpp"

int side_a() {
    int x = 10, y = 20;
    return max(x, y);   // 这个 TU 实例化 max<int>
}
```

```cpp
// m4_b.cpp —— 这个编译单元也实例化 max<int>
#include "m4.hpp"

int side_a();

int main() {
    int x = 1, y = 2;
    return max(x, y) - side_a();   // 这个 TU 也实例化 max<int>
}
```

```text
$ g++ -O0 -c m4_a.cpp -o m4_a.o
$ g++ -O0 -c m4_b.cpp -o m4_b.o
$ g++ m4_a.o m4_b.o -o m4
$ nm m4_a.o
                 U __stack_chk_fail
0000000000000000 W _Z3maxIiET_RS0_S1_
0000000000000000 T _Z6side_av

$ nm m4_b.o
0000000000000000 T main
                 U __stack_chk_fail
0000000000000000 W _Z3maxIiET_RS0_S1_
                 U _Z6side_av
```

两个目标文件各自带了一个 `W _Z3maxIiET_RS0_S1_`，两份机器码都实实在在生成了。咱们接着看链接的产物：

```text
$ nm m4 | grep -E 'max|main|side_a'
                 U __libc_start_main@GLIBC_2.34
00000000000011b3 T main
0000000000001187 W _Z3maxIiET_RS0_S1_
0000000000001139 T _Z6side_av

$ nm m4 | grep -c '_Z3maxIiET_RS0_S1_'
1
```

链接出来的程序里，这个符号就只出现了一次，`grep -c` 数出来的是 1。两份重名的定义没有引发任何报错，链接器留下了一份，丢弃了一份。输出里还有一处值得指给您看的细节：目标文件里符号的地址清一色是 0，链接的产物里 `max<int>` 分到了 0x1187，`main` 分到了 0x11b3，把地址归位本来就是链接器的活，您自己机器上跑出来的数值会不一样。机制藏在目标文件的节信息里，咱们把 `m4_a.o` 按节看一遍：

```text
$ readelf -SW m4_a.o | grep -E 'Name|\.text\._Z3max|\.group'
  [Nr] Name              Type            Address          Off    Size   ES Flg Lk Inf Al
  [ 1] .group            GROUP           0000000000000000 000040 000008 04     12   5  4
  [ 6] .text._Z3maxIiET_RS0_S1_ PROGBITS        0000000000000000 000096 00002c 00 AXG  0   0  1

$ readelf -g m4_a.o | head -20

COMDAT group section [    1] `.group' [_Z3maxIiET_RS0_S1_] contains 1 sections:
   [Index]    Name
   [    6]   .text._Z3maxIiET_RS0_S1_
```

两处证据咱们都拿到了。头一个是节的划分：`max<int>` 的机器码没有混进 `.text` 大节，它有自己的小节，节名用的就是修饰名本身。第二个是节名后面的 `G` 标志与相应的 COMDAT 分组：COMDAT 是目标文件格式里的分组记号，分组名用的也是修饰名，链接器们的约定是同名的分组只保留一份，其余的整组丢弃。弱符号 `W` 与 COMDAT 是配套的，符号标成了弱的，链接器才会接受多处定义、择一保留的安排，两边说的是同一件事。

这个设计的分量，咱们拿一个反例来看最清楚。咱们写两个普通的非模板函数，故意给它们起了同一个名字：

```cpp
// n1.cpp
int shared() { return 1; }
// n2.cpp
int shared();
int main() { return shared(); }
int shared() { return 2; }
```

```text
$ nm n1.o
0000000000000000 T _Z6sharedv
$ nm n2.o
0000000000000000 T main
000000000000000c T _Z6sharedv

$ g++ n1.o n2.o -o n_dup
/usr/bin/ld: n2.o: in function `shared()':
n2.cpp:(.text+0xc): multiple definition of `shared()'; n1.o:n1.cpp:(.text+0x0): first defined here
collect2: error: ld returned 1 exit status
```

两边的 `T` 都是强符号，同名又成了两份，链接器当场就把它们拒绝了，报的就是您平时偶尔会见到的 `multiple definition`。模板实例要是也走强符号的路子，头文件被两个编译单元包含的场景根本链接不过去。弱符号加 COMDAT 是模板能跨编译单元工作的前提：印重了没关系，去重的活儿链接器全包了。至于印的份数多起来之后体积怎么涨，咱们下一篇接着量。
