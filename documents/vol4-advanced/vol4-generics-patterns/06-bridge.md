---
title: "桥接模式:把抽象和实现拆成两条腿,顺手引出 pImpl"
description: "从最直白的「一个类把活全干了」开始,一步步逼出桥接模式,再用它讲透 pImpl 为什么能让头文件不再连坐重编译"
chapter: 11
order: 6
tags:
  - host
  - cpp-modern
  - intermediate
  - 桥接模式
difficulty: intermediate
platform: host
cpp_standard: [11, 14, 17, 20]
reading_time_minutes: 22
related:
  - "单例模式:从注释约束到 Meyer's Singleton"
prerequisites:
  - "Chapter 6: 类与对象"
---

# 桥接模式:把抽象和实现拆成两条腿,顺手引出 pImpl

## 我们到底在解决什么问题

我们还是先别急着上定义。想一个最经典的场景:你在做一个跨平台图形库,要画各种形状——圆、方、三角,而每个形状真正画到屏幕上时,底下可能是 OpenGL,也可能是 DirectX。如果按最直白的写法,你会很自然地写出一棵继承树:`OpenGLCircle`、`DirectXCircle`、`OpenGLSquare`、`DirectXSquare`……两个维度(形状 × 后端)一组合,类的数量是**乘法**的关系。每加一种形状,就得把所有后端再实现一遍;每加一个后端,又得把所有形状再实现一遍。这就是教材里常说的**类爆炸**(class explosion)。

桥接模式要解决的就是这种「两个维度同时扩展」的困境,它的核心一句话就够:**把「抽象」(Abstraction,上层逻辑)和「实现」(Implementor,底层细节)拆成两条独立的继承链,让抽象里持有一个实现接口的引用,用这个引用把两条链「桥接」起来**。形状那条链只管「我是谁、我该怎么算几何」,后端那条链只管「怎么把像素真正打上屏」,两个维度各自独立扩展,运行时随便组合(`Circle + OpenGL` 或者 `Circle + DirectX`),类的数量从乘法退化成加法。

这种「把抽象和实现分离」的思路,一旦你尝到甜头,就会到处想用。接下来我们一步步看,它怎么从一个图形库的设计,一路延伸到 C++ 工程里那个无人不知的 pImpl——后者本质就是桥接模式在「接口类 vs 实现类」这一对维度上的特例。

## 第一步:最直白的写法——一个类把活全干了

我们先看「没桥接」时长什么样。假设只有一个维度:画圆,底下用 OpenGL。最直觉的写法是把几何参数和绘图代码一股脑塞进一个类:

```cpp
class OpenGLCircle {
public:
    OpenGLCircle(double x, double y, double r) : x_(x), y_(y), r_(r) {}
    void draw() {
        // 几何信息 + OpenGL 调用 + 渲染细节,全在一起
        std::cout << "[OpenGL] draw circle at (" << x_ << "," << y_
                  << ") r=" << r_ << "\n";
    }
private:
    double x_, y_, r_;
};
```

这种写法在「需求就这一种」的时候完全没问题,甚至可以说是最清晰的。但事情到这里显然不会结束——产品同学过来说:我们要支持 DirectX。你下意识的反应可能是,再写一个 `DirectXCircle`:

```cpp
class DirectXCircle {
public:
    DirectXCircle(double x, double y, double r) : x_(x), y_(y), r_(r) {}
    void draw() {
        std::cout << "[DirectX] draw circle at (" << x_ << "," << y_
                  << ") r=" << r_ << "\n";
    }
private:
    double x_, y_, r_;
};
```

你发现没有,这两个类除了 `draw()` 里那行 `cout`,其余成员、构造、几何逻辑**一模一样**。现在再让你加一个 `Square`,你又要写 `OpenGLSquare`、`DirectXSquare`,几何算两遍。两个维度一旦各自扩张,重复代码就开始以乘法的速度堆积。这就是没有分离抽象和实现的代价:每一条「组合路径」都是一根独立的树枝。

## 第二步:抽出实现接口——两条腿走路

我们把那条「重复的部分」和「变化的部分」拆开。变化的部分是「用什么后端画」,那就把它抽成一个接口;不变的部分是「形状的几何逻辑」,留在抽象层。抽象层不自己画,它**持有一个后端接口的指针**,画的时候委托给后端:

```cpp
// Implementor(实现接口):只描述「画的能力」,不管形状
struct DrawingAPI {
    virtual ~DrawingAPI() = default;
    virtual void draw_circle(double x, double y, double r) = 0;
};

// ConcreteImplementor:真正的后端实现
struct OpenGLApi : DrawingAPI {
    void draw_circle(double x, double y, double r) override {
        std::cout << "[OpenGL]  绘制圆 中心(" << x << "," << y
                  << ") 半径 " << r << "\n";
    }
};

struct DirectXApi : DrawingAPI {
    void draw_circle(double x, double y, double r) override {
        std::cout << "[DirectX] 绘制圆 中心(" << x << "," << y
                  << ") 半径 " << r << "\n";
    }
};
```

这条 `DrawingAPI` 继承链就是「实现」那一维,它只管一件事:给我坐标和半径,我画。接下来是「抽象」那一维:

```cpp
// Abstraction:持有实现接口,自己不画,委托给后端
class Shape {
public:
    explicit Shape(std::unique_ptr<DrawingAPI> api)
        : api_(std::move(api)) {}
    virtual ~Shape() = default;
    virtual void draw() = 0;
protected:
    std::unique_ptr<DrawingAPI> api_;
};

// RefinedAbstraction:具体形状,只负责自己的几何
class Circle : public Shape {
public:
    Circle(double x, double y, double r, std::unique_ptr<DrawingAPI> api)
        : Shape(std::move(api)), x_(x), y_(y), r_(r) {}
    void draw() override { api_->draw_circle(x_, y_, r_); }
private:
    double x_, y_, r_;
};
```

你看,`Circle` 现在完全不知道自己被哪种后端画——它只知道「我有一个能画圆的东西」,运行时是什么由外部注入决定。两个维度彻底解耦:形状链上可以再叠 `Square`、`Triangle`,后端链上可以再叠 `VulkanApi`、`MetalApi`,两边各自演化,互不打扰。类的数量也回到了加法——你要的只是 `形状数 + 后端数`,而不是 `形状数 × 后端数`。

我们把它跑起来,验证一下「同一个圆,注入不同后端,行为确实不同」:

```sh
$ g++ -std=c++23 -O2 bridge_shape.cpp -o bridge_shape
$ ./bridge_shape
a 用 OpenGL 后端:
[OpenGL]  绘制圆 中心(1,2) 半径 3
b 用 DirectX 后端:
[DirectX] 绘制圆 中心(4,5) 半径 6
```

同一个 `Circle`,传不同的 `DrawingAPI` 进去,出来的就是不同后端的输出。这就是桥接:那条 `api_` 指针,就是连接两条继承链的「桥」。

::: tip 什么时候该用桥接
判断标准很简单:**当你发现自己在一个「二维甚至多维」的扩展空间里打转——比如「形状 × 后端」「消息类型 × 传输协议」「窗口部件 × 主题皮肤」——而且每加一个维度就要把已有代码全部复制一遍时,就该把其中一个维度抽成实现接口,让另一个维度持有它**。桥接是「从设计一开始就有意识地分离两个维度」,这一点和后面会讲到的适配器(Adapter)完全不同,适配器是事后给现成类糊一层壳,我们在文末还会专门对比。
:::

## 事情到这里还没完:同一个思想,换个维度就是 pImpl

图形库这个例子能让你看懂「抽象 × 实现」的双维度分离,但它离我们日常写 C++ 还差一步。现在我们把维度换一下,换成更常见的一对:**「对外暴露的接口」×「藏在内部的实现细节」**。

回到一个你在真实项目里一定踩过的痛:头文件膨胀。你写了一个 `Widget`,头文件里老老实实声明了私有成员:

```cpp
// widget.h
#include <string>
#include <vector>

class Widget {
public:
    void do_work();
private:
    std::vector<int> data_;
    std::string name_;
};
```

这看起来毫无毛病,直到有一天你在 `data_` 旁边加了一个 `std::unordered_map<std::string, Config> cache_;`,或者把 `std::string` 换成了 `std::filesystem::path`。你会气恼地发现:**只要头文件动了一丁点,所有 `#include "widget.h"` 的编译单元都得跟着全部重编一遍**。如果这是一个被全项目几百个文件引用的基础类,你的一次小改动就要触发几百个文件的重编译,CI 时间直接翻倍。

问题的根源在于:`data_`、`name_` 这些私有成员的类型,虽然在接口上是「私有的」,但在**头文件层面是公开的**。任何包含这个头文件的编译单元,都得把这些类型的完整定义看一遍,才能算出 `sizeof(Widget)`、才能安排栈布局。于是 `std::vector`、`std::string` 甚至 `<unordered_map>` 这些重型头文件,顺着 `widget.h` 被传染给了全项目。你定义里明明写了 `private:`,编译器却在告诉你:**你只是逻辑上私有,物理上这些细节还暴露在头里**。

桥接模式给了我们出路:把「实现细节」整个挪到一个独立的 `Impl` 类里,对外接口类只持有一个指向 `Impl` 的指针。接口类是「抽象」那一维,`Impl` 是「实现」那一维,中间那根指针就是桥。这个手法有个专门的名字——**pImpl(Pointer to IMPLementation)**,它就是桥接模式在 C++ 工程里最响当当的代表。

## pImpl 的第一步:裸指针 + 手写析构

我们先从最原始的 pImpl 起步,把 `Impl` 前向声明,头文件只留一个裸指针:

```cpp
// widget.h —— 干净得只剩下接口
class Widget {
public:
    Widget();
    ~Widget();
    void do_work();
private:
    struct Impl;       // 前向声明,Impl 的定义挪到 cpp
    Impl* pImpl;       // 裸指针
};
```

```cpp
// widget.cpp —— 实现细节全藏在这里
#include "widget.h"
#include <iostream>
#include <string>
#include <vector>

struct Widget::Impl {
    std::vector<int> data;
    std::string name;
    void do_work_impl() {
        std::cout << "doing heavy work, data.size=" << data.size() << "\n";
    }
};

Widget::Widget() : pImpl(new Impl{}) {}
Widget::~Widget() { delete pImpl; }   // 手写析构
void Widget::do_work() { pImpl->do_work_impl(); }
```

你看,`widget.h` 现在不含 `<vector>`、不含 `<string>`、不含任何重型头文件,`Impl` 的成员再怎么变,都只动 `widget.cpp`,外部世界一无所知。我们以「一次指针解引用」的微小代价,换来了实现细节的彻底隔离。这就是 pImpl 的全部魔法——**它把编译依赖从「头文件层」转移到了「单个编译单元」**。

但裸指针这条路有明显短板。你得手写 `~Widget() { delete pImpl; }`,这一步忘了就是内存泄漏;你要支持拷贝,又得手写拷贝构造和拷贝赋值去 `new` 一份新的 `Impl`;一旦中间抛了异常,资源管理又回到 C++98 那种提心吊胆的状态。我们是写现代 C++ 的,这事儿不能忍。

## pImpl 的第二步:用 `std::unique_ptr` 接管生命周期

我们让 RAII 替我们管指针。把 `Impl*` 换成 `std::unique_ptr<Impl>`,析构、移动语义全部交给智能指针:

```cpp
// widget.h
#include <memory>

class Widget {
public:
    Widget();
    ~Widget();
    void do_work();
private:
    struct Impl;
    std::unique_ptr<Impl> pImpl;
};
```

```cpp
// widget.cpp
#include "widget.h"
#include <iostream>
#include <string>
#include <vector>

struct Widget::Impl {
    std::vector<int> data;
    std::string name;
    void do_work_impl() { /* ... */ }
};

Widget::Widget() : pImpl(std::make_unique<Impl>()) {}
Widget::~Widget() = default;   // 关键:这里只能写 = default,见下文
void Widget::do_work() { pImpl->do_work_impl(); }
```

看起来美好得近乎免费,但真正的坑就藏在这一步。你可能会问:既然 `unique_ptr` 会自动析构,那我把 `~Widget() = default;` 直接写到头文件里行不行?反正它只是个默认析构。**不行,这一步写错一定编译不过**,我们先验证一下。

## 这里先验证一下:为什么 `~Widget()` 必须挪到 cpp

口说无凭,我们把 `~Widget() = default;` 放回头文件,故意让它在 `Impl` 不完整的地方被编译器看到:

```cpp
// widget.h —— 反面教材
#pragma once
#include <memory>

class Widget {
public:
    Widget() = default;
    ~Widget() = default;   // ← 故意写在头里,此时 Impl 不完整
    void do_work();
private:
    struct Impl;
    std::unique_ptr<Impl> pImpl;
};
```

编译一把,gcc 给出的报错非常直白:

```sh
$ g++ -std=c++23 -O2 widget.cpp main.cpp -o wtest
In file included from /usr/include/c++/16.1.1/memory:80,
                 from widget.h:2,
                 from main.cpp:1:
/usr/include/c++/16.1.1/bits/unique_ptr.h: In instantiation of
'constexpr void std::default_delete<_Tp>::operator()(_Tp*) const
[with _Tp = Widget::Impl]':
widget.h:6:5: required from here
unique_ptr.h:90:23: error: invalid application of 'sizeof' to
incomplete type 'Widget::Impl'
   90 |         static_assert(sizeof(_Tp)>0,
      |                       ^~~~~~~~~~~
```

事情的原委是这样的。`std::unique_ptr<Impl>` 的析构函数需要调用 `delete` 去销毁 `Impl` 对象,而 `delete` 在内部会做一个 `static_assert(sizeof(Impl) > 0)`——它必须确认 `Impl` 是个完整类型、有确定的 `sizeof`,否则编译器没法生成正确的析构调用。可一旦你把 `~Widget() = default;` 写在头文件里,编译器在**头文件被包含的那一刻**就要实例化 `Widget` 的析构(进而实例化 `unique_ptr<Impl>` 的析构),而那一刻 `Impl` 还只是个前向声明,`sizeof(Impl)` 根本算不出来,于是炸了。

解决办法就一条:**把 `~Widget()` 的定义挪到 `widget.cpp`**。在 cpp 里 `Impl` 已经被完整定义过了,那时再写 `Widget::~Widget() = default;` 就能正常生成析构代码。这个坑看起来简单,却是 pImpl 模式里第一个、也是最容易把人劝退的坎——你照着博客抄了一版,然后就是一串看不懂的 `incomplete type` 报错,血压一下就上来了。

::: warning 这个坑必须记牢
只要你的类里有 `std::unique_ptr<IncompleteType>`,那么**这个类的析构函数、以及任何会触发 `unique_ptr` 析构的特殊成员函数(比如 move ctor/assign 如果是 `= default` 的),定义都必须挪到那个不完整类型已经被完整定义的编译单元里**。具体到 pImpl,就是全部挪到 `widget.cpp`。不止析构,下面讲到移动构造时这个规矩同样成立。
:::

## pImpl 的第三步:补上拷贝语义(clone + copy-and-swap)

到这里我们有了一个能正确析构、能移动的 pImpl 类,但它还**不能拷贝**。原因很直接:`std::unique_ptr<Impl>` 是只移动的,编译器为 `Widget` 自动生成的拷贝构造和拷贝赋值会被删掉。你若硬写 `Widget b = a;`,会得到一个被 delete 的报错。

但「pImpl 类」在工程里恰恰经常需要拷贝——它是值语义的对外类型,放进容器、按值传参都要求它能拷。怎么补?最稳的做法是把拷贝逻辑下沉到 `Impl`,再让 `Widget` 通过 `clone()` 转发:

```cpp
// widget.cpp 里 Impl 增加一个 clone
struct Widget::Impl {
    std::vector<int> data;
    std::string name;
    void do_work_impl() { /* ... */ }
    std::unique_ptr<Impl> clone() const {
        return std::make_unique<Impl>(*this);   // 深拷贝
    }
};
```

然后 `Widget` 的拷贝构造和拷贝赋值手动写出来,转交给 `clone()`:

```cpp
// widget.cpp
Widget::Widget(const Widget& other)
    : pImpl(other.pImpl ? other.pImpl->clone() : nullptr) {}

Widget& Widget::operator=(const Widget& other) {
    Widget tmp(other);          // 先拷一份临时对象
    swap(*this, tmp);           // 再和*this交换
    return *this;               // tmp 析构时自动释放旧 Impl
}
```

这里用了经典的 **copy-and-swap** 惯用法:先用拷贝构造造一个临时对象 `tmp`,再把它和 `*this` 交换,函数返回时 `tmp` 离开作用域,旧的 `Impl` 被自动析构。这样做的好处是**强异常安全**——`clone()` 抛异常时,`*this` 还没被碰过,状态完全不变;如果 `clone()` 成功了,交换只是一个不抛异常的指针搬运,随后释放旧资源也不会出问题。

注意那个 `swap`,它是个 `friend` 函数,只是把两边的 `unique_ptr` 换一下,这也是 `noexcept` 的:

```cpp
// widget.h 里,作为 Widget 的友元
friend void swap(Widget& a, Widget& b) noexcept {
    using std::swap;
    swap(a.pImpl, b.pImpl);
}
```

::: warning 别把 swap 的 inline 放错位置
有些老笔记会把友元 `swap` 写成 `friend void inline swap(...)`,这个写法虽然能过,但 `inline` 出现在返回值后面是一种很老的、不推荐的写法,而且容易和 `using std::swap;` 这套惯用法混淆。规范写法是 `friend void swap(...) noexcept`——`friend` 函数定义在类体内时本来就是 `inline` 的,不用再画蛇添足。
:::

我们把这一整套(深拷贝 + move)放一起验证一下,看看拷贝出来的对象是不是真的独立、move 之后源对象是不是真的被掏空:

```sh
$ g++ -std=c++23 -O2 -pthread bridge_verify.cpp -o bridge_verify
$ ./bridge_verify
a.size after move = 0 (expect 0,被 move 走了)
b.size = 100, b[0] = 42 (expect 100, 42,深拷贝独立)
c.size = 100 (expect 100,move 接管)
```

`a` 被 `std::move` 给了 `c` 之后,`a` 自己的 `size` 是 0——源对象被掏空了,这正是 move 的语义;而 `b` 是从 `a` 拷贝出来的,它有自己独立的 100 个 `42`,改 `b` 不会影响 `c`,反之亦然——深拷贝是真正独立的。pImpl 配合 `clone()` 和 copy-and-swap,把值语义该有的行为全补齐了。

## pImpl 的第四步:给 move 标上 noexcept,让 vector 扩容走移动

你现在已经有一个能拷、能移的 pImpl 类了。但它离「直接塞进 `std::vector` 不掉链子」还差最后一步,这一步很多人会漏掉——**移动构造和移动赋值必须标 `noexcept`**。

为什么这一个关键字这么关键?原因是 `std::vector` 在扩容(比如 `push_back` 触发了重分配)时,要把旧内存里的元素搬到新内存里。这时它有个选择:能 move 就 move,不能 move 就 copy。但 vector 的强异常安全承诺要求「搬家过程中如果抛异常,原 vector 必须保持不变」——而 move 一旦抛异常,原对象已经被掏空,异常安全就破了。所以 vector 的策略是:**只有当元素的 move 构造是 `noexcept` 时,它才敢用 move 搬家;否则宁可走 copy**(copy 抛异常时原对象还在,可以回滚)。

你的 pImpl 类的 move 其实只是搬一个 `unique_ptr`,绝对不会抛异常,但**如果你不写 `noexcept`,vector 不知道这一点,就会老老实实走 copy**——一份份地 `clone()`,每一次都是一次堆分配。我们直接验证一下,差距是量级的:

```sh
$ ./bridge_verify
[noexcept move]    copy=0 move=1020 (扩容应纯走 move,copy=0)
[non-noexcept move] copy=1020 move=0 (无 noexcept,扩容走 copy,move=0)
```

同样是往 `vector` 里塞 1000 个元素、同样会触发多次扩容,标了 `noexcept` 的那一组**纯走 move,零次 copy**;没标的那一组反过来——vector 不信任你的 move,每一次扩容都走 copy,1020 次深拷贝。对一个 pImpl 类来说,这意味着 1020 次额外的堆分配。一个关键字,性能差出一个数量级。

所以 pImpl 类的特殊成员函数,该有的样子是这样:

```cpp
class Widget {
public:
    Widget();
    ~Widget();                          // 类外定义(Impl 完整)
    Widget(const Widget&);              // clone 深拷贝
    Widget& operator=(const Widget&);   // copy-and-swap
    Widget(Widget&&) noexcept;          // move 也必须类外定义 + noexcept
    Widget& operator=(Widget&&) noexcept;
    // ...
};
```

```cpp
// widget.cpp
Widget::Widget(Widget&&) noexcept = default;
Widget& Widget::operator=(Widget&&) noexcept = default;
```

move 的 `= default` 同样要写在 cpp 里,道理和析构一模一样——它内部要搬 `unique_ptr<Impl>`,也得等 `Impl` 完整。

## 收口:pImpl 到底换来了什么

我们把上面几步拼起来,得到一个生产可用的 pImpl 范式:头文件只留前向声明和一个 `unique_ptr`,析构和 move 全挪到 cpp,拷贝通过 `clone()` + copy-and-swap 实现,move 统一 `noexcept`。它换来三件实打实的好处。

第一件是**编译依赖骤降**。头文件不再包含 `<vector>`、`<string>` 这类重型头,`Impl` 的任何成员变动都被关在 `widget.cpp` 一个编译单元里,外部世界完全不需要重编。在大型项目里,这一个好处就足以让你爱上 pImpl。

第二件是**ABI 稳定**。我们来验证一个很直观的事——pImpl 之后,对象到底有多大:

```sh
$ ./bridge_verify2
sizeof(Widget)           = 8
sizeof(NaiveWidget)      = 56
sizeof(void*)            = 8
说明:Widget 压成一个指针大小,Impl 再怎么长,Widget 的 ABI 不变
```

naive 写法下 `Widget` 是 56 字节(三个指针:`vector` 三个、`string` 一个 libstdc++ 实现下占 32 字节,合计 56);pImpl 之后 `Widget` 压成 8 字节——就是一个指针。这意味着只要你**对外接口不变**(`Impl` 里随便加成员、换成员类型),使用方编译出来的二进制可以照常链接,不需要重编——这就是 ABI 稳定。这对发布动态库(.so / .dll)的团队是刚需:你不能要求用户每次你改个内部成员就重新链接一遍。

第三件是**真正的封装**。`private:` 在头文件里只挡住了「访问」,没挡住「可见」——私有成员的类型对所有人都是公开的,你用了 `std::vector` 还是自研容器,一览无余。pImpl 把这些细节物理上挪进了 cpp,连「可见」都挡住了,这才叫封装到家。

代价也要说清楚:每次访问成员多一次指针解引用(pImpl 的那根桥);`Impl` 必须在堆上分配一次;头文件里的内联优化对实现函数失效(实现在 cpp,跨编译单元内联需要 LTO);拷贝走 `clone()` 是深拷贝,有分配开销。这些代价在「编译/发布」侧换来的收益面前,绝大多数时候是值得的。

::: tip 何时该上 pImpl
不是所有类都值得 pImpl。给一个简单的判别准则:**当这个类是要被全项目大量 include 的「接口/门面」、当你希望它的二进制 ABI 跨版本稳定、当你想彻底隐藏某组重型第三方依赖(`<unordered_map>`、`<boost/...>`)时,上 pImpl**。反之,一个只在单个 cpp 内部用的实现类、或对性能极致敏感且成员频繁被内联访问的小类型,pImpl 的间接开销就不划算。先测量,再决定。
:::

## Bridge vs Adapter:长得像,意图完全不同

讲完桥接,有一个绕不开的对比:它和适配器(Adapter)模式看起来太像了——两者都有一个「委托指针」,都把一个对象包一层转给另一个。但两者的**意图和设计时机**完全不同,这是区分它们的唯一可靠标准。

桥接是**从设计一开始就有意识地拆维度**:你预见到「形状」和「后端」都要各自演化,所以一开始就把它们拆成两条继承链,让抽象持有实现。它的关键词是「**预先分离,双向独立扩展**」。而适配器是**事后的补救**:你手上已经有一个现成的类(比如一个老的网络库),接口和新系统期望的对不上,你只好给它包一层壳,把旧接口「翻译」成新接口。它的关键词是「**事后粘合,单方向转换**」。

一个简单的判别准则:**如果你是为了「让两个维度都能各自扩展」而设计,用 Bridge;如果你是为了「把一个已有类接到新接口上」而打补丁,用 Adapter**。两者外观相似(都有委托指针),但设计动机南辕北辙,别混为一谈。

## 小结

我们把这条演进路径捋一遍:

| 阶段 | 做法 | 为什么还不够 |
|---|---|---|
| 单类全包 | `OpenGLCircle` 把几何和后端写在一起 | 加一个后端就要复制整个类,类爆炸 |
| 抽实现接口 | `Shape` 持有 `DrawingAPI`,两条链独立扩展 | **桥接模式成立**,二维各自演化 |
| 裸指针 pImpl | `Impl*` + 手写 `delete` | 内存管理繁琐,拷贝/异常易错 |
| `unique_ptr<Impl>` | RAII 接管,析构/move 类外定义 | 不能拷贝,缺值语义 |
| clone + copy-and-swap | 深拷贝下沉到 `Impl`,强异常安全 | move 没 `noexcept`,vector 扩容退化为 copy |
| move 标 noexcept | 让 vector 敢用 move 搬家 | **生产可用**,编译依赖与 ABI 都稳 |

记下这几条关键结论:

- **桥接的本质是「把两个会各自扩展的维度拆成两条继承链,用一根指针桥接」**,类数量从乘法降为加法,pImpl 是它在「接口 × 实现」这一对维度上的特例。
- **pImpl 的头文件只留前向声明和 `unique_ptr<Impl>`**,所有实现细节(含重型头文件)挪进 cpp,换来编译依赖骤降和 ABI 稳定。
- **析构、移动构造、移动赋值的定义必须挪到 cpp**——因为 `unique_ptr<IncompleteType>` 的析构需要完整类型,写在头里一定编译失败。
- **拷贝靠 `Impl::clone()` + copy-and-swap,移动务必标 `noexcept`**,否则 `vector` 扩容不敢用 move,退化成深拷贝,性能差一个数量级。
- **Bridge 和 Adapter 的区别在意图**:前者预先分离双维度,后者事后单方向转接接口。

::: tip 配套可编译工程
本节的例子在仓库 `code/volumn_codes/vol4/design-patterns/Bridge/` 下有完整可编译工程(`.h` + main + `CMakeLists.txt`),`cmake -S . -B build && cmake --build build` 即可跑出上面这些输出。
:::

## 参考资源

- [cppreference:`std::unique_ptr`](https://en.cppreference.com/w/cpp/memory/unique_ptr)(C++11 起,不完整类型与析构要求见 *Notes*)
- [cppreference:`std::make_unique`](https://en.cppreference.com/w/cpp/memory/unique_ptr/make_unique)(C++14 起)
- [cppreference:`std::move` 与 `noexcept` 移动语义](https://en.cppreference.com/w/cpp/utility/move)(`vector` 扩容的 `move_if_noexcept` 机制见 [vector](https://en.cppreference.com/w/cpp/container/vector) 注释)
- Herb Sutter,GotW #28/100:「The Fast PImpl Idiom」与「Compilation Firewalls」(pImpl 的编译防火墙动机与最佳实践)
- ISO C++ Core Guidelines:[C.133–C.139 / R.20–R.23](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines)(类布局与智能指针所有权)
