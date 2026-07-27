---
chapter: 13
cpp_standard:
- 11
- 14
- 17
description: 模板默认隐式实例化,每个翻译单元各自生成一份同样的代码。讲清显式实例化定义、extern template 声明怎么把实例化集中起来,并诚实评估它对编译时间的真实收益(小项目测不出来,大项目累积才有)
difficulty: intermediate
order: 7
platform: host
prerequisites:
- TMP 核心技巧:concepts 之前的世界
- Concepts:把模板约束写进签名
reading_time_minutes: 14
related:
- 静态反射基础:反射运算符与 splice 重组
- 模板与异常安全:move_if_noexcept 与扩容
tags:
- host
- cpp-modern
- intermediate
- 模板
- 编译期计算
- 工具链
title: 模板实例化控制:extern template 与编译时间
---
# 模板实例化控制:extern template 与编译时间

上一篇结尾说,这一篇回到今天就能用的东西。模板给 C++ 带来了零成本抽象,但也带来一个不那么光鲜的副作用:编译时间。一个模板如果在十几个翻译单元里被用同样的类型参数用到,编译器可能老老实实地在这十几个单元里各实例化一遍。C++11 给了一个能管这件事的工具——`extern template`。这一篇讲清模板实例化的两种控制手段(显式实例化定义、extern template 声明),并且诚实评估它们对编译时间到底有多大帮助。

## 隐式实例化:用到才生成,每个翻译单元各生成一份

模板默认走的是**隐式实例化**:您在某处用了 `Heavy<int>`,编译器就在那个翻译单元里把 `Heavy<int>` 用到的成员现场生成出来。这个机制是「按需」的,用不到的成员不生成,挺好。问题在于它是**每个翻译单元各自一遍**的。

设想一个项目里有 `use_a.cpp` 和 `use_b.cpp`,两处都用了 `Heavy<int>`。编译 `use_a.cpp` 时,编译器实例化一份 `Heavy<int>` 的代码塞进 `use_a.o`;编译 `use_b.cpp` 时,又实例化一份塞进 `use_b.o`。链接的时候,链接器看到两个 `.o` 里都有 `Heavy<int>::compute` 的定义,靠 ODR(单一定义规则)和模板的「弱符号」把它们合并成一份。最后运行时只有一份代码,没问题,但**编译阶段的工作做了两遍**。这就是 extern template 想治的那个症结。

## 显式实例化定义:把实例化集中到一个地方

要管住这件事,得先用上**显式实例化定义**(explicit instantiation definition)。语法是 `template` 开头,跟一个具体的模板实例:

```cpp
#include "heavy_template.h"

template struct Heavy<int>;   // 在这个翻译单元里实例化 Heavy<int> 的全部成员
```

这一行的意思是:「请在这个 `.cpp` 里,把 `Heavy<int>` 的所有成员函数都老老实实实例化出来」。它通常单独放在一个叫 `explicit_inst.cpp` 之类的文件里,专门承担「集中实例化」的职责。

## extern template:告诉其他翻译单元「别再生成」

光有集中实例化还不够,别的翻译单元不知道这回事,还是会自己隐式实例化一份。所以要配上**显式实例化声明**,也就是 `extern template`:

```cpp
#include "heavy_template.h"

extern template struct Heavy<int>;   // Heavy<int> 别处已经实例化,这里别再生成
```

这一行告诉编译器:「`Heavy<int>` 在别的翻译单元里已经实例化好了,你在这里别再生成代码,只管用」。于是这个翻译单元省掉了实例化的活,链接的时候去 `explicit_inst.o` 里找定义就行。

两者配套,就把「每个 TU 各实例化一遍」收敛成了「只在一个 TU 实例化,其他 TU 直接引用」。下面咱们实测验证这套机制。

## 实测:机制怎么跑起来

一套最小的多文件工程。`heavy_template.h` 定义模板,`use_a.cpp` 走老办法隐式实例化,`use_b.cpp` 用 extern template,`explicit_inst.cpp` 提供显式实例化定义,`main.cpp` 串起来:

```cpp
// heavy_template.h
#pragma once
template <typename T>
struct Heavy {
    T value;
    explicit Heavy(T v) : value(v) {}
    T compute(T x) const {
        T acc = value;
        for (int i = 0; i < 10; ++i) acc = acc * x + value;
        return acc;
    }
};
```

```cpp
// use_b.cpp —— extern template 抑制实例化
#include "heavy_template.h"
#include <iostream>
extern template struct Heavy<int>;   // 别处已实例化,这里别再生成
void use_b() {
    Heavy<int> h{99};
    std::cout << "use_b: " << h.compute(3) << "\n";
}
```

```cpp
// explicit_inst.cpp —— 集中显式实例化
#include "heavy_template.h"
template struct Heavy<int>;
```

编译链接运行(`use_a.cpp` 结构和 `use_b.cpp` 一样,只是没有 extern 那行):

```bash
$ g++ -std=c++20 -Wall -Wextra -c use_a.cpp use_b.cpp explicit_inst.cpp main.cpp
$ g++ use_a.o use_b.o explicit_inst.o main.o -o demo && ./demo
use_a: 85974
use_b: 8768727
```

四个目标文件顺利编译,链接也通过,程序正常运行。机制本身没问题。

更有意思的是看「不提供显式实例化定义会怎样」。把 `explicit_inst.cpp` 拿掉,只剩用了 extern 声明的 `use_b.cpp` 和 `main.cpp`:

```text
/usr/bin/ld: use_b.o: in function `use_b()':
undefined reference to `Heavy<int>::Heavy(int)'
undefined reference to `Heavy<int>::compute(int) const'
```

链接器找不到 `Heavy<int>` 的构造和 `compute` 的定义,报 undefined reference。这条报错把 extern template 的契约讲得很直白:您声明了「别处有定义」,就得真在某个翻译单元里把这个定义显式实例化出来,否则就是空头支票。配套使用时也别忘了:如果哪个翻译单元没用 extern 声明(比如这里的 `use_a.cpp`),它照样会隐式实例化一份——`extern template` 是「我这个 TU 不生成」,不是「全局只生成一份」。

## 编译时间收益:别被「优化编译时间」的口号骗了

讲到 extern template,几乎所有人都会说它「减少编译时间」。这话在原理上成立,但实际能省多少,值得跑跑看。GCC 有个 `-ftime-report` 标志,能在编译完输出各阶段耗时,里面有专门的 `template instantiation` 一项。先看一个小文件:

```text
$ g++ -std=c++20 -c -ftime-report use_b_noextern.cpp   # 隐式实例化版
 template instantiation             :   0.08 ( 26%)    14M ( 23%)
```

模板实例化占了整个编译时间的大约四分之一,听上去 extern template 应该能帮上忙。咱们对比一下:同一个 `use_b.cpp`,一个版本带 extern 声明(不实例化 `Heavy<int>`),一个不带(隐式实例化),各跑三次看 `template instantiation` 那一行。

| 文件 | 3 次 template instantiation |
|---|---|
| `use_b.cpp`(extern,不实例化) | 0.08 / 0.07 / 0.05 |
| `use_b_noextern.cpp`(隐式实例化) | 0.07 / 0.05 / 0.05 |

差异完全在噪声范围里,测不出谁更快。为了排除「模板太轻」的嫌疑,咱们把模板加重——内部塞了三组各 80 层的递归元函数(Fibonacci、三角形数、卢卡斯数),实例化 `Big<int>` 会连带展开约 240 个模板特化。再各跑三次:

| 文件 | 3 次 template instantiation |
|---|---|
| `big_b.cpp`(extern) | 0.08 / 0.07 / 0.05 |
| `big_b_noextern.cpp`(连带 240 个特化) | 0.07 / 0.05 / 0.05 |

还是测不出来。现代编译器实例化这种「纯类型计算」的模板实在太快了,几千万分之一秒的活儿,被解析、优化这些阶段的噪声整个淹没。

那 extern template 到底什么时候才真省时间?答案是**大型项目里,几十个翻译单元重复实例化同一个「重型」模板**。所谓重型,不是咱们这里的纯 TMP 递归,而是那种实例化会连带拉入一大片标准库代码的模板——比如某个泛型组件用了 `std::variant` 加一堆算法,它在二十个 `.cpp` 里被同样的参数用到,二十份重复实例化的成本才累积到肉眼可见。这种场景下,extern template 把二十遍压缩成一遍,收益就实打实了。所以判断要不要上 extern template,看的是「实例化的绝对成本」乘以「重复的翻译单元数」,两个都大才有意义。给一个只在小项目里用两三次的轻量模板加 extern,纯属徒增样板代码,该叫停。

## 顺带说一句别的治编译时间的路子

如果您的目标是「让编译更快」,extern template 往往不是杠杆最大的那个。更常奏效的几招:用前向声明代替不必要的 `#include`、把模板的声明和定义分开到不同头里减少实例化入口、上预编译头(PCH),以及 C++20 的 modules——modules 从机制上重新定义了「翻译单元之间怎么共享代码」,是治本的路子,虽然工具链支持到今天还在打磨。extern template 是这套工具箱里的一把小扳手,有它的用武之地,但不是主力。

下一篇咱们看模板和异常怎么纠缠在一起:为什么 `vector` 扩容时要关心元素类型的 `noexcept`,以及 `move_if_noexcept` 这套机制怎么在「性能」和「异常安全」之间打圆场。
