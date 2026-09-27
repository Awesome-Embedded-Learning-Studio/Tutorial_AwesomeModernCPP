---
chapter: 1
cpp_standard:
- 11
- 14
- 17
description: 把实现细节搬进 cpp 的惯用法：不完整类型的语言规则、unique_ptr 与 shared_ptr 的析构差异、编译防火墙实测与 const 失效
difficulty: intermediate
order: 8
platform: host
prerequisites:
- 'Chapter 1: unique_ptr 详解'
- 'Chapter 1: shared_ptr 详解'
- 'Chapter 1: 自定义删除器与侵入式引用计数'
reading_time_minutes: 16
related:
- '桥接模式:把抽象和实现拆成两条腿,顺手引出 pImpl'
- 'scope_guard 与 defer'
tags:
- host
- cpp-modern
- intermediate
- 智能指针
- unique_ptr
- shared_ptr
title: PIMPL 惯用法：unique_ptr、shared_ptr 与不完整类型
---
# PIMPL 惯用法：unique_ptr、shared_ptr 与不完整类型

pimpl法，笔者认为是一个标准的，跨协作的工程中最被广泛使用的一个设计模式了。笔者也建议，各位如果是做大作业的大学生，或者是做一个联合项目（也就是一大堆人对接的项目），请务必重视pimpl法。他如此的流行，一些朋友甚至跟我说设计模式其实就只需要学习pimpl法就好（虽然我开玩笑说我是做GUI框架的，我还是要观察者模式的，哈哈！）

好了！回来吧！上一篇咱们聊 scope_guard，把“作用域退出时干一件事”从资源释放推广到了状态回滚。这一篇是本章的最后一篇，咱们换一种收法：不再学新工具了，而是把整章攒下的工具全部拉到一个工程惯用法里过一遍。unique_ptr 的删除器、shared_ptr 的控制块，咱们全都要用上。它的名字叫 PIMPL（Pointer to Implementation，指向实现的指针），Qt 里那个著名的 d-pointer（Qt 给这套手法起的内部名字），就是它的变体。

问题本身您一定碰见过。您在头文件里写一个 `class Widget`，把 `std::vector<std::string> cache_` 老老实实摆在 `private:` 的底下。咱们在逻辑上碰不着它，可在物理上它就藏不住了，咱们随便挑一个包含它的编译单元（一个 .cpp 连同它包含的所有头，最终编出的就是一个目标文件）来看，成员定义全都摆在眼前。咱们想让编译器算 `sizeof(Widget)`、给栈上的对象排布局，它就必须看到每个成员的完整定义，于是 `<vector>`、`<string>` 这些重型头文件也就被带给了每一个包含者。您哪天在 `cache_` 旁边加一个 `int`，全项目包含过 widget.h 的文件，陪着全部重编了一遍。

咱们的解法，是pimpl法（斜眼笑）。好了不卖关子了。这玩意的做法一句话就说完了。

> 私有成员整个搬进一个叫 `Impl` 的结构体，头文件里只留一个前向声明（只报类型名字、不给定义的声明），加一根指向它的指针。

没了！如果想了解细节的朋友，可以参考这个设计模式视角的完整演化（从裸指针换成 `unique_ptr`、用 `clone()` 补拷贝、给 move 标上 `noexcept`），vol4 讲[桥接模式](/vol4-advanced/vol4-generics-patterns/06-bridge)的那篇一步步走过，咱们这里不重走了。

## 他用起来，看起来是什么样的

```cpp
// widget.h
#pragma once
#include <memory>

class Widget {
public:
    Widget();
    ~Widget();                    // 只声明，定义放在 widget.cpp
    int  value() const;
    void bump();
private:
    struct Impl;                  // 前向声明：成员细节全在 widget.cpp
    std::unique_ptr<Impl> impl_;
};
```

```cpp
// widget.cpp
#include "widget.h"

struct Widget::Impl {
    int count = 0;
    int padding[7]{};
};

Widget::Widget() : impl_(std::make_unique<Impl>()) {}
Widget::~Widget() = default;
int  Widget::value() const { return impl_->count; }
void Widget::bump()        { ++impl_->count; }
```

头文件里 `Impl` 只剩下了个名字，咱们算不出它的 `sizeof`、也不用算——`Widget` 本身就是一根指针的大小。真正的成员、真正的逻辑，全被咱们关进了 widget.cpp。您看到 `~Widget()` 只声明、不定义，大概会问：咱们把 `= default` 直接写在类内行不行，反正析构也是默认的？vol4 那篇已经用报错回答了“不行”，咱们这里把问题再往下追一层：标准凭什么要拦咱们？

## delete 要知道被删的东西长多大

咱们把反面教材单独拎出来编译，析构呢，就写成了类内 `= default`，那一刻 `Impl` 还只是个前向声明的名字：

```cpp
#include <memory>

class Widget {
public:
    Widget();
    ~Widget() = default;   // 类内默认析构：此刻 Impl 只被前向声明
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

int main() {
    Widget w;   // 必须真的构造对象，析构被用到，编译器才会生成它
}
```

GCC 16（x86_64 Linux）拒绝了咱们，下面的报错咱们只节选头几行：

```text
/usr/include/c++/16/bits/unique_ptr.h: In instantiation of 'void std::default_delete<_Tp>::operator()(_Tp*) const [with _Tp = Widget::Impl]':
/usr/include/c++/16/bits/unique_ptr.h:408:17:   required from 'std::unique_ptr<_Tp, _Dp>::~unique_ptr() [with _Tp = Widget::Impl; _Dp = std::default_delete<Widget::Impl>]'
a_unique_err.cpp:6:5:   required from here
/usr/include/c++/16/bits/unique_ptr.h:90:23: error: invalid application of 'sizeof' to incomplete type 'Widget::Impl'
   90 |         static_assert(sizeof(_Tp)>0,  <- 就看这里！！！
```

报错最后一行落在 libstdc++ 的 `default_delete` 里，那句 `static_assert(sizeof(_Tp)>0)` 就是它安的检查。咱们往下挖：`delete p` 要干的事有两件，调 `Impl` 的析构函数，再按对象的大小把内存还回去。这两件事都离不开类型的完整定义——析构函数的定义、`sizeof` 的数值，前向声明一概给不了。

标准凭什么拦咱们？咱们看条款怎么说：只要被 delete 的类型不完整，而它的完整定义又带非平凡析构，或声明了自己的释放函数（类内的 `operator delete`），行为就是未定义的（C++11 起 [expr.delete] 的条款，更早的标准同样有类似要求）。麻烦的地方咱们也看到了：偏偏类型还不完整，编译器连“它的析构平不平凡”都问不出来，想放行也查不了。libstdc++ 干脆一律要求完整的类型，拿 `sizeof` 一查就拦下了，把一份未定义行为换成一句咱们能读懂的编译错误。

还有个细节值得咱们停下来看看。您把 main 里那句 `Widget w;` 删掉再编译，它就真的 0 error 0 warning 了，好像什么都没发生过。这个待遇不是所有函数都有：模板的成员函数按需实例化，没被调用就不生成代码；类内 `= default` 的特殊成员也一样，用到它的地方才定义。普通函数恰恰相反，哪怕没人调用，函数体也照样编译，错都藏不住。咱们这个例子恰好两头都占了：`unique_ptr` 的析构是模板成员，`Widget` 的析构是类内 `= default`，咱们不构造 `Widget`，两个析构就都没人调用，那段带 `static_assert` 的代码根本不会被生成。这也解释了为什么有人照着博客把 PIMPL 敲进自己的空项目，编译得好好的，一搬进真实代码就翻了车：写法其实没错，只是还没到逼编译器出手的那一步。

cppreference 的措辞很直白：`unique_ptr` 允许咱们用不完整类型构造，标准库留这个口子就是给 pImpl 用的，但使用默认删除器时，删除器被调用的地方，T 必须是完整的。咱们能在三个地方撞见删除器：`unique_ptr` 的析构、移动赋值和 `reset()`。析构咱们刚见过，另外两处呢？咱们拿移动赋值再试一次。

## 移动赋值：同一份要求的另一个入口

咱们这回把工程摆成真实的样子：析构老实挪进了 widget.cpp，只把移动赋值留在了类内。

```cpp
// widget.h
#pragma once
#include <memory>

class Widget {
public:
    Widget();
    ~Widget();                              // 析构挪出去了
    Widget& operator=(Widget&&) = default;  // 移动赋值还留在类内
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
```

```cpp
// widget.cpp
#include "widget.h"

struct Widget::Impl { int count = 0; };

Widget::Widget() : impl_(std::make_unique<Impl>()) {}
Widget::~Widget() = default;
```

```cpp
// main.cpp
#include "widget.h"
#include <utility>

int main() {
    Widget a, b;
    a = std::move(b);   // 类内 = default 的移动赋值，在这里被实例化
}
```

咱们单独编译 widget.cpp，顺顺当当编过了。可等咱们编 main.cpp，报错又来了，还是熟悉的那一句：

```text
/usr/include/c++/16/bits/unique_ptr.h: In instantiation of 'void std::default_delete<_Tp>::operator()(_Tp*) const [with _Tp = Widget::Impl]':
/usr/include/c++/16/bits/unique_ptr.h:204:16:   required from 'void std::__uniq_ptr_impl<_Tp, _Dp>::reset(pointer) [with _Tp = Widget::Impl; _Dp = std::default_delete<Widget::Impl>; pointer = Widget::Impl*]'
/usr/include/c++/16/bits/unique_ptr.h:184:2:   required from 'std::__uniq_ptr_impl<_Tp, _Dp>& std::__uniq_ptr_impl<_Tp, _Dp>::operator=(std::__uniq_ptr_impl<_Tp, _Dp>&&)'
/usr/include/c++/16/bits/unique_ptr.h:236:24:   required from here
/usr/include/c++/16/bits/unique_ptr.h:90:23: error: invalid application of 'sizeof' to incomplete type 'Widget::Impl'
   90 |         static_assert(sizeof(_Tp)>0,
```

咱们顺着调用链走一遍。`Widget` 的移动赋值，搬的是 `impl_` 这根 `unique_ptr`，而 `unique_ptr` 的移动赋值在接管新指针之前，会把手里的旧指针 reset 掉，reset 内部调用的正是删除器。您看报错第二行，required from 的正是 `reset`。所以这不只是“析构函数特殊”——只要删除器可能被调用的地方，咱们就得让 `Impl` 在场。您看同一个类，在 widget.cpp 里 `Impl` 是完整的，编译也就过了，而 main.cpp 里 `Impl` 只是个名字，当场就被拦下了，谁在完整的地方实例化，谁就平安编译过去了。

## 同一行 = default，shared_ptr 版全部通过

好玩的来了。咱们把 `std::unique_ptr<Impl>` 换成 `std::shared_ptr<Impl>`，别的咱们什么都不动：构造照旧只声明，定义挪到 `Impl` 完整了之后，析构照样大胆写在类内。然后咱们把工程拆成两个编译单元，让 main 所在的那一侧从头到尾没见过 `Impl` 的定义：

```cpp
// sh.h
#pragma once
#include <memory>

class Widget {
public:
    Widget();
    ~Widget() = default;   // 析构留头文件：本编译单元里 Impl 从未完整
    int  value() const;
    void bump();
private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};
```

```cpp
// sh.cpp —— Impl 在这里才完整
#include "sh.h"

struct Widget::Impl { int count = 0; };

Widget::Widget() : impl_(std::make_shared<Impl>()) {}
int  Widget::value() const { return impl_->count; }
void Widget::bump()        { ++impl_->count; }
```

```cpp
// main.cpp
#include "sh.h"
#include <cstdio>

int main() {
    Widget w;   // w 的析构在本编译单元被用到，此处 Impl 不完整
    w.bump();
    w.bump();
    std::printf("count = %d\n", w.value());
}
```

而在 main.cpp 里，`Widget w` 的析构被真实用到，那个头文件里的 `Impl` 在这一侧自始至终只是个名字。咱们按上一节的逻辑推，这儿该死定了？咱们把它编译、链接、运行了一遍，全都过了：

```text
$ g++ -std=c++17 -Wall -Wextra sh.cpp main.cpp -o app && ./app
count = 2
```

单文件版咱们也放在了下面，您点“动手试一试”就能直接跑，构造函数和析构函数的位置都替您摆好了：

<OnlineCompilerDemo
  title="动手验证：shared_ptr 版析构留在类内"
  source-path="code/examples/vol2/49_pimpl_shared_destructor.cpp"
  description="在线验证同一行类内 = default 析构：构造挪到 Impl 完整之后，析构留在类内，编译运行全过，输出 count = 2。"
  run-options="-std=c++17"
  allow-run
/>

咱们写下的都是同一行 `~Widget() = default`，unique_ptr 版被编译器拒掉了，而 shared_ptr 版，编译、链接、运行全都过了。差异不在头文件的写法，而在两种智能指针存放删除器的方式上。[自定义删除器](06-custom-deleter.md)那篇咱们分过家：`unique_ptr` 的删除器是类型的一部分，靠空基类优化（EBO）塞进了对象本身，调用是编译期直连的。`shared_ptr` 的删除器被类型擦除，藏进了堆上的控制块。

两种智能指针存放删除器的位置与析构路径对照如下：

![unique_ptr 与 shared_ptr 的删除器存放位置与析构路径对照](./08-pimpl-deleter-path.drawio)

直连意味着什么呢？咱们看 `unique_ptr` 析构里那句 `delete impl_`，它要在实例化自己的那个编译单元里生成机器码。机器码的生成要算 `sizeof`、找析构函数，`Impl` 不完整就生成不出来了，编译器只好拒绝了咱们。而 `shared_ptr` 析构时干的事少得多：控制块的引用计数减一，归零了就调控制块里备好的销毁函数。那套销毁代码在构造那一刻就生成了，`make_shared<Impl>()` 所在的位置 `Impl` 完整，删除动作在那里被记进了控制块，析构处做的只是转交。咱们从头到尾找一遍，需要 `sizeof(Impl)` 的地方一处都没有，当然也轮不到要求 `Impl` 完整。

所以两种 pImpl 的写法可以这样记。咱们把 unique_ptr 版里会触发删除的成员函数（析构、移动赋值），全部挪进 cpp 就齐了。shared_ptr 版呢，构造的定义挪到 cpp，析构就可以留在头文件了。您听下来是不是 shared_ptr 版更省事？咱们接着看它省不省得起。

## shared_ptr 也有过不去的地方：构造

您要是把构造也图省事写进头文件，shared_ptr 立刻就拒绝了。咱们把两个类写进同一个文件：头一个 `Widget`，构造和析构全都留在类内；第二个 `BadWidget`，把 `make_shared` 写进了 `Impl` 还不完整的地方：

```cpp
#include <memory>

class Widget {
public:
    Widget() = default;   // 类内默认构造：只造一个空的 shared_ptr，不碰 Impl
    ~Widget() = default;  // 析构也类内，上一节验证过，这条过得去
    void show() const;
private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};

class BadWidget {
public:
    BadWidget() : impl_(std::make_shared<Impl>()) {}  // Impl 不完整处 make_shared
    ~BadWidget() = default;
private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};

int main() {
    Widget w;
    BadWidget b;   // 迫使 BadWidget 的构造函数被实例化
}
```

GCC 又把咱们拦下了。报错咱们节选了头尾几行，源码回显里那个行号 15，对得上您刚看的清单里 `BadWidget` 构造函数那一行：

```text
/usr/include/c++/16/ext/aligned_buffer.h: In instantiation of 'struct __gnu_cxx::__aligned_buffer<BadWidget::Impl>':
/usr/include/c++/16/bits/shared_ptr_base.h:658:50:   required from 'class std::_Sp_counted_ptr_inplace<BadWidget::Impl, std::allocator<void>, __gnu_cxx::_S_atomic>::_Impl'
  658 |         __gnu_cxx::__aligned_buffer<__remove_cv_t<_Tp>> _M_storage;
/usr/include/c++/16/bits/shared_ptr_base.h:722:13:   required from 'class std::_Sp_counted_ptr_inplace<BadWidget::Impl, std::allocator<void>, __gnu_cxx::_S_atomic>'
  722 |       _Impl _M_impl;
b2_shared_ctor_err.cpp:15:47:   required from here
   15 |     BadWidget() : impl_(std::make_shared<Impl>()) {}
/usr/include/c++/16/ext/aligned_buffer.h:99:58: error: invalid application of 'sizeof' to incomplete type 'BadWidget::Impl'
   99 |       alignas(__alignof__(_Tp)) unsigned char _M_storage[sizeof(_Tp)];
```

等咱们的还是 `sizeof`，换了个地方等您。咱们在[shared_ptr 详解](04-shared-ptr.md)那篇讲过，`make_shared` 的卖点是对象和控制块一次分配——对象直接内嵌在控制块的内存里，报错里那个 `_M_storage` 就是给对象预留的空间。而内嵌就得预留空间，空间的大小也就得算出来，偏偏 `Impl` 不完整，咱们就卡在了这里。cppreference 对 shared_ptr 的措辞也正好对称：shared_ptr 允许咱们和不完整类型一起用，但从裸指针构造、reset 这些真正创建或接管对象的入口，都要求类型是完整的。这个要求不挑指针的值，您传一个空裸指针过去，它照样拦。真正没有这个要求的是默认构造和空指针构造：这两个入口压根不创建对象，咱们默认构造出的 `shared_ptr<Impl>` 谁也没创建，`Impl` 完不完整也就无所谓了。清单里头一个 `Widget`，构造和析构全留类内，报错里却没有它半行。构造能过关，靠的正是这一点。

代价咱们也要摆在桌面上。shared_ptr 版的对象 16 字节，比 unique_ptr 版翻了一倍，咱们每次拷贝、析构，付出的都是原子操作，外加控制块的一次堆分配，这些开销咱们在 04 篇逐项量过。换来的只有“析构可以留在头文件”这一点小小的方便。而 PIMPL 的 `Impl` 天生该独占，一个 `Widget` 拥有的就是一个 `Impl`，而它不跟任何人分，所以工程里的 pImpl 几乎都由独占指针持有。咱们要把 shared_ptr 版用在 pImpl 上也不是不行，只是您要拿翻倍的大小、原子计数和一次控制块分配，去换那一点点的省事，多数场合咱们算下来都不划算。

## 编译防火墙：用 g++ 量一遍

规则聊完了，咱们回到构建目录，做一次增量编译的实测。工程就是本篇开头那几份文件，再添上一个 main.cpp：

```cpp
// main.cpp
#include "widget.h"

int main() {
    Widget w;
    w.bump();
    return w.value() == 1 ? 0 : 1;
}
```

这次咱们不打 Makefile、不开 CMake，直接拿 g++ 一条条编。谁需要重编、谁可以跳过，判断全在咱们自己手里。头一次自然是全量，两份 cpp 各编出一个目标文件，再链接成 app：

```text
=== 第一次构建：全部编译 ===
g++ -std=c++17 -O2 -c main.cpp -o main.o
g++ -std=c++17 -O2 -c widget.cpp -o widget.o
g++ -std=c++17 -O2 main.o widget.o -o app
```

然后咱们改 `Impl` 内部，添一个 `int new_field = 0;`。这一步动的只有 widget.cpp，widget.h 一个字节没变，而 main.cpp 看得见的又只有 widget.h，咱们就没有任何理由重跑那条编译 main.cpp 的命令，重编 widget.o、重新链接就够了：

```text
=== 给 Impl 添了新成员后：只有 widget.o 需要重编 ===
g++ -std=c++17 -O2 -c widget.cpp -o widget.o
g++ -std=c++17 -O2 main.o widget.o -o app
```

咱们看第二次跑的命令，widget.cpp 重编了，编译 main.cpp 的那条命令压根没有再出现的必要。改完的 app 照常运行，main.o 还是第一次构建出来的那份，时间戳都没动过。`Impl` 里添了字段，咱们在外头完全看不出来。这套挡住编译依赖的手法，业界给它的惯用叫法是：编译防火墙，它的英文名是 compilation firewall。它的实在含义就是：实现随咱们怎么变，编译依赖都被挡在了 widget.cpp 一个文件里。工程大了以后，make、CMake 这类增量构建工具替咱们记着这份依赖，哪个文件动过、谁包含了谁，它们一查便知，用的还是咱们刚才手上这套判断。

那成员直出、不加 PIMPL 的版本呢？widget.h 里放的就是 `int count` 和 `std::vector<std::string> cache_`。咱们只在 `private:` 下面加一个 `int`，同样的流程再走一遍：

```text
=== 只在 widget.h 加了一个私有成员后 ===
g++ -std=c++17 -O2 -c main.cpp -o main.o   ← main.o 也得重编
g++ -std=c++17 -O2 -c widget.cpp -o widget.o
g++ -std=c++17 -O2 main.o widget.o -o app
```

这回 main.cpp 陪着重来了一遍。头文件变了，成员布局动没动，编译器证明不了，按旧布局编出来的 main.o 只能作废。两个文件的工程里您可能觉得无所谓，可您这个头文件要是被几百个 .cpp 包着，您加的那个 `int`，带来的就是几百次重编，CI 时间就是这么一秒一秒涨上去的。

咱们回头把两份日志数一遍：成员直出的版本里，加一个 `int`，要重编的是 main.o 和 widget.o 两个目标，链接也得重跑；换成 PIMPL 之后，同样的改动要重编的只剩 widget.o 一个目标，main.o 一个字节都不用再碰。这两幕对照做成了一段动画：

<Anim id="pimpl-rebuild-scope" />

咱们还能拿到一层收益：`sizeof(Widget)` 从此就恒等于一根指针了，咱们在 `Impl` 里怎么加成员、换成员类型，对外的二进制接口（Application Binary Interface，咱们下文简称 ABI，说的是已编好的二进制代码之间的布局与调用约定）也就固定了下来，用方编出来的目标文件照常链接。sizeof 前后的实测，vol4 那篇做过了，咱们不重复。

## const 的约束，在这里断了

咱们再看一个语言层面的副作用，它比编译报错安静了许多，也更容易被咱们漏掉：pImpl 类的 const 成员函数里，照样能改 `Impl` 的成员。您可能不信，咱们写一个：

```cpp
#include <memory>
#include <cstdio>

class Counter {
public:
    Counter() : impl_(std::make_unique<Impl>()) {}

    // inc 声明成 const 成员函数，内部却在改状态，能编译能运行
    void inc() const { ++impl_->count; }

    int get() const { return impl_->count; }

private:
    struct Impl { int count = 0; };
    std::unique_ptr<Impl> impl_;
};

int main() {
    const Counter c;   // const 对象
    c.inc();           // 居然真的改得动
    c.inc();
    std::printf("const 对象 get() = %d\n", c.get());
}
```

咱们把演示放在了下面，您点“动手试一试”直接跑：

<OnlineCompilerDemo
  title="动手验证：const 传播在 pImpl 上断了"
  source-path="code/examples/vol2/50_pimpl_const_leak.cpp"
  description="在线验证 const 失效：const 对象被 const 成员函数改了两次，编译运行全过，输出 const 对象 get() = 2。源文件里还留着成员直写版的对照组报错。"
  run-options="-std=c++17"
  allow-run
/>

咱们拿到的运行输出：

```text
const 对象 get() = 2
```

您看，一个 const 对象就这么被 const 成员函数改了两次，编译和运行给咱们一路放行。而成员直写的类里，咱们写同一句 `inc()`，编译器当场就拦下了：

```text
error: increment of member 'NaiveCounter::count_' in read-only object
```

机制咱们一步步看。咱们进到 const 成员函数里，`this` 的类型是 `const Counter*`。咱们顺着 `this` 去读 `impl_`，读到的是 `const std::unique_ptr<Impl>&`，指针本身确实是只读的。可 `unique_ptr` 的 `operator->` 返回的是 `Impl*`，返回的从头到尾就不是 `const Impl*`。指针本身是只读的，指向物却不是只读的——咱们加了一层间接，const 就断在了那层间接上。

断在了这里，接口的承诺就只能靠人守了。您在头文件里声明 `void inc() const`，读代码的人合理期待它不改状态，可 `Impl` 里的 count 照样被它递增。咱们有两条路可以守住它。要么咱们写 pImpl 类时管住手，const 方法不碰 `impl_` 的可变成员。要么咱们给 `Impl` 配上 const 与非 const 两套访问，让 const 的约束顺着传下去。编译器不再替您把关，这一点咱们得心里有数。

## 代价与选择

咱们得在堆上给 `Impl` 分配一次，每次访问成员还多付一次指针解引用，而 `impl_->count` 比直接 `count` 多跑一趟内存。实现还挪进了 cpp，跨编译单元的内联没了，咱们想找回内联，就得开 LTO（Link Time Optimization 的缩写，意思是链接期的优化）。对象的大小呢，咱们在 GCC 16、x86_64 上实测：

```text
sizeof(unique_ptr pImpl Widget) = 8
sizeof(shared_ptr pImpl Widget) = 16
sizeof(NaiveWidget 成员直出头文件) = 192
```

咱们把直出版本和 pImpl 版摆在一起看：这个直出版本带着两个各 96 字节的重型成员，每个是四个 `double` 加一段 64 字节的缓冲区，pImpl 把它们全部换成了一根指针。它跟防火墙节那个 naive 对照不是同一个类：那边直出的是 `int count` 加 `std::vector<std::string> cache_`，轻得多；这边咱们特意挑了一个成员更重的直出版本来量，192 对 8 的差距才够醒目。只在一个 cpp 内部使用的小类，它的头文件压根没人包含，防火墙自然也就无从谈起了，反而白付了一次堆分配。被内联访问的热路径小类型，咱们也不建议上，一次解引用、一次跨单元调用的损失都实打实。反过来呢，被全项目大量 include 的接口类、要藏起 `<unordered_map>` 或第三方重型依赖的门面类、要发动态库且二进制接口得跨版本稳定的类，这些开销就花得值了。您拿不准的时候，咱们量一下改头文件前后的编译时间再拍板。

本章咱们到这里收工。咱们从 RAII 的获取与释放出发，用 unique_ptr、shared_ptr、weak_ptr 把所有权的三种关系落地，让自定义删除器接管了 C API 和硬件句柄，又用 scope_guard 把“退出时干一件事”推广到了更一般的场景，最后轮到的这篇 PIMPL，把咱们整章攒的工具串成了一个工程惯用法。您现在去看库里那些只露一根指针的头文件，应该能认出它了，还能讲清析构落在了哪、const 断在了哪、编译依赖被挡在了哪。下一章咱们聊 constexpr 与编译期计算，把“留到运行时再算”的老习惯改一改，咱们看看哪些计算能提前到编译期做完。

## 参考资源

- [cppreference: std::unique_ptr（Notes 一节讲不完整类型要求）](https://en.cppreference.com/w/cpp/memory/unique_ptr)
- [cppreference: std::shared_ptr（Notes 与 Implementation notes）](https://en.cppreference.com/w/cpp/memory/shared_ptr)
- [cppreference: std::make_shared](https://en.cppreference.com/w/cpp/memory/shared_ptr/make_shared)
- [C++ Core Guidelines: R.20-24](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#ss-smart)
- [C++ Core Guidelines: I.27 For stable library ABI, consider the Pimpl idiom](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#ri-pimpl)
- Herb Sutter, *GotW #100: Compilation Firewalls* 与 *GotW #28: The Fast Pimpl Idiom*
