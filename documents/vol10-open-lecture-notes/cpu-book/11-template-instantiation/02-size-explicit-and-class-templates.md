---
title: "模板实例化的对照：体积、显式开关与类模板"
chapter: 11
order: 2
tags: [host, cpp-modern, intermediate]
difficulty: intermediate
platform: host
cpp_standard: [11, 17]
description: "类型从 1 加到 8，text 从 263 涨到 1108 字节。显式实例化与 extern template 管印不印，类模板的按需细到成员一级"
reading_time_minutes: 15
current_status:
  title: 正在打磨
  detail: 本批刚写完,正在通读打磨,表述与实测口径可能随时调整
  variant: polishing
---

# 模板实例化的对照：体积、显式开关与类模板

环境、工具和贴文的读法都跟[上一篇](01-on-demand-copies-and-linker-dedup.md)相同，编译命令咱们照旧带 `-O0`，您要复跑就照它来。上一篇的结尾，两个编译单元印重的 `max<int>` 在链接器手里只留了一份。这一篇咱们接着往下看，份数多起来体积怎么涨，显式实例化与 `extern template` 这对手动开关怎么拨，类模板又按什么单位印。

## 印了多少，体积就写多少

复印是按型来的，每个类型都会有一份独立的机器码，那么类型多了起来，目标文件就该一段一段地涨。咱们把同一个模板分别对 1、2、4、8 个类型实例化，用 `size` 量了真实数字。四个变体的差别完全机械：模板的部分一字不动，`main` 里放的是按类型顺序排的变量和调用，编号为 n 的变体就放到第 n 个类型为止，类型取用的顺序是 int、double、char、long，后面接着的是 float、short、unsigned，以及最后的 long long。两类型版的全文长这样：

```cpp
// m5_2.cpp —— 同一模板，实例化两个类型
template <typename T>
T max(T& a, T& b) {
    return a < b ? b : a;
}

int main() {
    int x0 = 1, y0 = 2;
    (void)max(x0, y0);
    double x1 = 1, y1 = 2;
    (void)max(x1, y1);
    return 0;
}
```

咱们让四类型和八类型版照同样的顺序继续往下加，变量和调用一路加满了八个类型，八类型版的全文如下：

```cpp
// m5_8.cpp —— 同一模板，实例化八个类型
template <typename T>
T max(T& a, T& b) {
    return a < b ? b : a;
}

int main() {
    int x0 = 1, y0 = 2;
    (void)max(x0, y0);
    double x1 = 1, y1 = 2;
    (void)max(x1, y1);
    char x2 = 1, y2 = 2;
    (void)max(x2, y2);
    long x3 = 1, y3 = 2;
    (void)max(x3, y3);
    float x4 = 1, y4 = 2;
    (void)max(x4, y4);
    short x5 = 1, y5 = 2;
    (void)max(x5, y5);
    unsigned x6 = 1, y6 = 2;
    (void)max(x6, y6);
    long long x7 = 1, y7 = 2;
    (void)max(x7, y7);
    return 0;
}
```

咱们把编译和度量的命令与输出一起贴出来：

```text
$ for n in 1 2 4 8; do g++ -O0 -c m5_${n}.cpp -o m5_${n}.o; done
$ size m5_1.o m5_2.o m5_4.o m5_8.o
   text	   data	    bss	    dec	    hex	filename
    263	      0	      0	    263	    107	m5_1.o
    410	      0	      0	    410	    19a	m5_2.o
    633	      0	      0	    633	    279	m5_4.o
   1108	      0	      0	   1108	    454	m5_8.o

$ for n in 1 2 4 8; do printf 'm5_%d.o: ' $n; nm m5_${n}.o | grep -c ' W _Z3max'; done
m5_1.o: 1
m5_2.o: 2
m5_4.o: 4
m5_8.o: 8

$ stat -c '%s  %n' m5_1.o m5_2.o m5_4.o m5_8.o
1792  m5_1.o
2360  m5_2.o
3136  m5_4.o
4760  m5_8.o
```

咱们把符号数了一遍，个数是严格跟着类型数走的，实例化了几个类型，`nm` 里对应的就是那几行 `W`，体积也跟着涨了：代码段的 text 从 263 涨到 1108 字节，整个文件的字节从 1792 涨到 4760。增量还能落到每一份的头上，咱们把八类型版按节看一遍：

```text
$ size -A m5_8.o | grep -E 'section|\.text\._Z3max'
section                    size   addr
.text._Z3maxIiET_RS0_S1_     44      0
.text._Z3maxIdET_RS0_S1_     54      0
.text._Z3maxIcET_RS0_S1_     48      0
.text._Z3maxIlET_RS0_S1_     49      0
.text._Z3maxIfET_RS0_S1_     53      0
.text._Z3maxIsET_RS0_S1_     49      0
.text._Z3maxIjET_RS0_S1_     44      0
.text._Z3maxIxET_RS0_S1_     49      0
```

咱们看下来，八个实例各占一个自己的小节，单份的尺寸是 44 到 54 字节，合计占了 390 字节，剩下的增量来自 `main` 本身变长，变量和调用都多了。还有一个细节跟前面的汇编对得上：浮点版的小节是 53 和 54 字节，比整型版的 44 大，因为 `movsd` 和 `comisd` 的指令编码就是比 `mov` 和 `cmp` 长。模板做的正是每个类型一份独立机器码的编译期展开，不是一份带类型参数的代码在运行期分派，类型翻了几倍，代码段也就大致按份数涨了。

这里必须回到上一篇环境一节埋下的那个条件，把优化档位的事说完整。上面所有数字都是 `-O0` 的直译口径，量的是复印这件事本身的形状。同一份八类型源码咱们换 `-O2` 编一遍，实测的 text 只剩 99 字节，而 m1 的代码在 `-O2` 下的目标文件里，`nm` 里连一个 `max` 的符号都没有，剩下的只有 `T main`，小函数被整体内联进了调用点。所以笔者得把话说诚实：44 字节是直译档下一份复印的成本，咱们不能拿它外推“模板让程序变大多少百分比”，外推的结果就站不住。这两组 `-O2` 的实测也只覆盖咱们这几个小实例，大模板在优化档位下的去留咱们没有测过。去重的机制不挑实例的大小，弱符号加 COMDAT 的安排对哪一份都一样，前面链接器只留一份的实测证据不受这里的影响。

## 显式实例化：要它印，与不许它印

复印什么时候开机、什么时候停，C++ 给了两个手动的开关。咱们摆两个编译单元，一个写显式实例化的定义，一个写 `extern template` 的声明：

```cpp
// m6_def.cpp —— 显式实例化定义所在 TU:max<int> 的机器码在这里生成
template <typename T>
T max(T& a, T& b) {
    return a < b ? b : a;
}

template int max<int>(int&, int&);   // 显式实例化定义
```

```cpp
// m6_use.cpp —— 声明 extern template 的 TU:不在本地生成,用别处的实例
template <typename T>
T max(T& a, T& b) {
    return a < b ? b : a;
}

extern template int max<int>(int&, int&);   // 显式实例化声明

int main() {
    int x = 1, y = 2;
    return max(x, y);   // 有模板定义、也有调用,但 extern template 压住了本地实例化
}
```

```text
$ g++ -O0 -c m6_def.cpp -o m6_def.o
$ g++ -O0 -c m6_use.cpp -o m6_use.o
$ g++ m6_def.o m6_use.o -o m6
$ nm m6_def.o
0000000000000000 W _Z3maxIiET_RS0_S1_

$ nm m6_use.o
0000000000000000 T main
                 U __stack_chk_fail
                 U _Z3maxIiET_RS0_S1_

$ nm m6 | grep -E 'max|main'
                 U __libc_start_main@GLIBC_2.34
0000000000001165 T main
0000000000001139 W _Z3maxIiET_RS0_S1_
```

咱们把 m6_use 和前面的 m3 放在一起看，这是这两篇里最划算的一组对照。两份源码的内容几乎一样，都是模板的定义加一行调用，唯一的差别是多出来的 `extern template` 那一行，符号就从 `W` 变成了 `U`。`W` 的意思是本地有定义，`U` 的意思是本地没有、等链接器从别的编译单元找。一行声明就把本地的隐式实例化压住了。另一头的 m6_def 里，显式实例化定义让这个编译单元无条件生成了实例，它里面连一行调用的代码都没有。最后链接出来的是一份符号，程序跑起来的退出码是 2，正好是 `max(1,2)` 的结果。

这里有一个连不少老资料都会说错的点，咱们拿实测把它说准。显式实例化定义生成的符号也是 `W`，而不是强符号 `T`，GCC 16.2.1 下它和隐式实例化的形态一模一样。所以准确的表述是：显式实例化保证的是生成，`extern template` 保证的是不生成，两个开关管的是有没有定义，符号的形态都是弱符号加 COMDAT。为什么显式实例化也标弱？因为别的编译单元里可能同时存在同名的隐式实例化，只要那些单元没写 `extern template` 的声明，编译器对这类实体的统一做法就是按弱符号发射，重名的时候交给链接器去重。Itanium ABI 把这类允许在多个编译单元里重复出现的实体归成一类，类别的名字叫 vague linkage，弱符号加 COMDAT 就是这类实体在目标文件里的形态。

手动开关的实用价值在编译耗时上：大项目里的做法是把实例化集中到一个 .cpp，别的编译单元用 `extern template` 的声明，重复生成的功夫就省下了。这么干的时机怎么挑，[卷四元编程子卷的模板实例化控制一篇](../../../vol4-advanced/vol3-metaprogramming-cpp20-23/07-template-instantiation-control.md)有完整的评估，小项目里的收益量不出来，大项目里的累积才见效，咱们今天只把符号层的机制看清楚。

## 类模板：隐式按成员，显式按整类

类模板的复印比函数模板多一层问题：一个类一堆成员函数，实例化的时候是整类一起印，还是谁被用谁印？咱们造一个带两个成员的 `Box`，`main` 里只调它的 `get()`：

```cpp
// m7.cpp —— 类模板：只调用一个成员
template <typename T>
class Box {
    T v_;
public:
    Box(T v) : v_(v) {}          // 构造函数
    T get() const { return v_; } // main 里调用的成员
    T twice() const { return v_ + v_; }  // main 里一次也不调的成员
};

int main() {
    Box<int> b(40);        // 需要 Box<int> 这个类型
    return b.get();        // 只调用了 get()
}
```

```text
$ g++ -O0 -c m7.cpp -o m7.o
$ nm m7.o
0000000000000000 T main
                 U __stack_chk_fail
0000000000000000 W _ZN3BoxIiEC1Ei
0000000000000000 W _ZN3BoxIiEC2Ei
0000000000000000 n _ZN3BoxIiEC5Ei
0000000000000000 W _ZNK3BoxIiE3getEv

$ nm -C m7.o
0000000000000000 T main
                 U __stack_chk_fail
0000000000000000 W Box<int>::Box(int)
0000000000000000 W Box<int>::Box(int)
0000000000000000 n Box<int>::Box(int)
0000000000000000 W Box<int>::get() const
```

`twice()` 的符号 `_ZNK3BoxIiE6twiceEv` 完全不在。`main` 造了 `Box<int>` 的对象、调了 `get()`，目标文件里就只生成了构造函数和 `get()`。类模板实例化的最小单位是成员函数：需要这个类型，不代表需要它的每一个成员。咱们把调用点换成 `twice()` 再编一回，`m7b.cpp` 与上面的差别只是 `b.get()` 换成了 `b.twice()`：

```text
$ g++ -O0 -c m7b.cpp -o m7b.o
$ nm -C m7b.o
0000000000000000 T main
                 U __stack_chk_fail
0000000000000000 W Box<int>::Box(int)
0000000000000000 W Box<int>::Box(int)
0000000000000000 n Box<int>::Box(int)
0000000000000000 W Box<int>::twice() const
```

符号就换成了 `twice()`，`get()` 反过来缺席了，咱们把两份 `nm` 的输出并排放着，按需这件事在成员这一级也是成立的。

输出里的三行构造函数，咱们花两句把它认清楚，免得它干扰咱们的正题。05 里咱们认过 C1 和 C2 是 ABI 给同一份构造代码起的两个名字，C1 管完整对象的构造，C2 管基类子对象的构造，继承场景之外两者指的是同一份指令。咱们请 `nm -S` 把尺寸量出来看一眼：

```text
$ nm -S m7.o
0000000000000000 000000000000004b T main
                 U __stack_chk_fail
0000000000000000 0000000000000017 W _ZN3BoxIiEC1Ei
0000000000000000 0000000000000017 W _ZN3BoxIiEC2Ei
0000000000000000 n _ZN3BoxIiEC5Ei
0000000000000000 0000000000000010 W _ZNK3BoxIiE3getEv
```

C1 和 C2 的尺寸都是 0x17 字节，同一份代码的两个名字。小写 `n` 的 C5 是不带尺寸的本地别名记号，不参与跨编译单元的链接，您扫输出的时候可以无视它。所以判断成员按需实例化，咱们拿普通成员函数当证据最干净，`get()` 与 `twice()` 的进出就是全部事实。

标准对成员这一级的按需也有明文。[类模板页面](https://en.cppreference.com/w/cpp/language/class_template)的隐式实例化一节，咱们检索的日期是 2026 年 10 月 13 日：

```text
This applies to the members of the class template: unless the member is used
in the program, it is not instantiated, and does not require a definition.
```

程序里没有用到的成员不会被实例化，连它的定义都可以不给。同一节还交代了触发的条件：构造一个这个类型的对象会触发实例化，声明一个指向它的指针却不会，因为前者要的是完整的类型，后者要的只是个指针。咱们在 m7 里造了对象，所以构造函数和 `get()` 都来了，指针的用法连 `Box<int>` 都不会完整实例化。

隐式的按需讲完了，显式那一侧的类版咱们接着看，两边的输出一比，差别就全出来了：

```cpp
// m6c.hpp
#ifndef M6C_HPP
#define M6C_HPP

template <typename T>
class Widget {
    T v_;
public:
    Widget(T v) : v_(v) {}
    T value() const { return v_; }
    T doubled() const { return v_ + v_; }
};

#endif
```

```cpp
// m6c_def.cpp —— 类模板的显式实例化定义:Widget<int> 的全部成员在这里生成
#include "m6c.hpp"

template class Widget<int>;   // 显式实例化整个类
```

```cpp
// m6c_use.cpp —— extern template 声明的 TU:不在本地生成 Widget<int> 的成员
#include "m6c.hpp"

extern template class Widget<int>;   // 显式实例化声明(整个类)

int main() {
    Widget<int> w(21);
    return w.value();   // 正常调用,但本地不生成机器码
}
```

```text
$ g++ -O0 -c m6c_def.cpp -o m6c_def.o
$ g++ -O0 -c m6c_use.cpp -o m6c_use.o
$ g++ m6c_def.o m6c_use.o -o m6c
$ nm -C m6c_def.o
0000000000000000 W Widget<int>::Widget(int)
0000000000000000 W Widget<int>::Widget(int)
0000000000000000 n Widget<int>::Widget(int)
0000000000000000 W Widget<int>::value() const
0000000000000000 W Widget<int>::doubled() const

$ nm -C m6c_use.o
0000000000000000 T main
                 U __stack_chk_fail
                 U Widget<int>::Widget(int)
                 U Widget<int>::value() const

$ nm -C m6c | grep -i widget
000000000000113a W Widget<int>::Widget(int)
000000000000113a W Widget<int>::Widget(int)
0000000000001152 W Widget<int>::value() const
0000000000001162 W Widget<int>::doubled() const
```

咱们来看显式这一侧的输出，`template class Widget<int>;` 写下去的效果是全部成员都生成了，包括从头到尾没有任何人调用的 `doubled()`。m7 里没人调的 `twice()` 缺席，m6c 里没人调的 `doubled()` 在场，这正是两个方向的差别：隐式实例化问的是这一页用不用，用到的才印，显式实例化不做任何的挑选，整类都印了出来。`extern template` 那边的输出同样干净：本地的成员一个都没有生成，只留用到的成员的 `U` 引用，`doubled()` 连它的引用都没有，因为全程没有它的调用者。链接的产物里每个成员各一份，程序跑起来的退出码是 21，正是 `Widget<int>(21).value()` 的结果。

## 复印机的三条规则

咱们把这两篇的实验收拢一下。复印是从按需开始的：模板定义在场不出代码，调用点出现了才印，m2 与 m3 只差了一行，类模板的按需还要细到成员，没人调的 `twice()` 连符号都不出现。印的时候是按型来的，每个类型一份独立的机器码，修饰名带着类型的尾巴，int 的版本是 `IiE`，double 的版本是 `IdE`，指令也各按各的类型出，整型的 `cmp` 对浮点的 `comisd`，份数多了体积就涨，text 从 263 涨到了 1108。类型多了起来，重名也就躲不开了，好在链接器是认重名的，跨编译单元的重名实例是弱符号加 COMDAT，只留了其中一份，`grep -c` 数出来的 1，显式实例化与 `extern template` 管的是印不印的开关，不改变符号的形态，真正的差别是 `W` 对 `U`。

模板的语言层讲法，函数模板在[卷一的函数模板一篇](../../../vol1-fundamentals/ch09/01-function-templates.md)，类模板与特化在同卷的后面几篇，语义上的为什么那边讲得全。实例化的控制手段什么场合值得上，去[卷四元编程子卷的实例化控制一篇](../../../vol4-advanced/vol3-metaprogramming-cpp20-23/07-template-instantiation-control.md)。咱们系列里的符号与名字修饰，[02 讲函数调用的那一篇](../02-function-calls-and-stack-frames/01-call-and-stack-frame-walkthrough.md)起的头，[05 讲类的那一篇](../05-classes-in-assembly/01-this-pointer-and-member-offsets.md)补了类名的写法，今天咱们把模板这半边接上：一份源码是怎么变成一摞函数的，您从头到尾在 `nm` 和 `objdump` 里亲眼看了一遍。

收尾把最省事的自证留给您。您拿 m3.cpp 编一回，记下 `nm` 里的那行 `W`。接着在模板定义的后面加上 `extern template int max<int>(int&, int&);` 再编一回，注意咱们只编目标文件、不做链接。原来的 `W` 会变成 `U`，本地就不再复印了，一行声明的分量，就写在前后两个字母的差别里。
