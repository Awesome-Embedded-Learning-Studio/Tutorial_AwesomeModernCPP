---
title: "卷四 · 进阶 课后练习（Homework）"
description: "卷四（C++20/23/26 元编程与泛型模式）的课后练习：42 章每章 2 题（基础+进阶），另加 2 道跨章综合与 1 道 L5 编译期元编程挑战。难度覆盖 L1~L5，题目全部做了变式处理——换场景、换数据、换推理方向，照抄教材例题抄不出答案；参考答案独立成文件、逐步解答附知识点链接，所有输出在 WSL Arch（g++ 16.1.1 / clang++ 22.1.8）真实运行得到。"
chapter: 4
order: 1
tags:
  - host
  - advanced
  - cpp-modern
  - 模板
  - 泛型
  - 模板元编程
  - concepts
difficulty: advanced
platform: host
cpp_standard: [17, 20, 23]
reading_time_minutes: 58
prerequisites: []
related: []
---

# 卷四 · 进阶 课后练习（Homework）

## 引言

这里的题按章组织，每章两道（基础 + 进阶），最后是两道跨章综合和一道 L5 挑战。每题标注难度档位（L1~L5，见[练习总览](../exercises/index.md)）和涉及章节。题目都是「变式」——换场景、换推理方向，照抄教材例题抄不出答案；每道题都要真编译真跑，把输出贴下来才算完。

答案在独立的[参考答案](./02-homework-solutions.md)文件里，按题号对应，每步解答带知识点链接。建议一章做完再看答案。所有代码用 `-std=c++20 -Wall -Wextra` 起步（个别题目要求 C++23 或别的旗标，题面会写明）；需要多线程的加 `-pthread`，需要抓内存问题的上 ASan。

本卷验证环境与教程同款：WSL Arch，g++ 16.1.1 / clang++ 22.1.8。个别题目涉及 C++26 特性（`std::inplace_vector` 等）时，先预测本机 libstdc++ 16 是否已提供、再用正确的方法实测验证（特性宏要 `#include` 对应头文件后打印，别用 `-dM -E` 探测——那只列编译器预定义宏）——如实贴结果，绝不编造编译输出。

## 4.1 协程基础

### 4.1-A {#hw-4-1-a}

难度 **L1** · 涉及[协程基础](../01-coroutine-basics.md)

两道题。①配对填空：把左边三个关键字和右边的接口连起来——`co_await`、`co_yield`、`co_return`；`get_return_object()`、`initial_suspend()`、`final_suspend()`、`return_void()`、`yield_value(T)`、`await_ready()`、`await_suspend(H)`、`await_resume()`。注意有个陷阱：八个接口里有三个不属于 `promise_type`，它们属于谁？②写一个最小生成器 `Generator<int>`（`promise_type` 自己写，对外用 `next()`/`value()` 接口），让协程依次 `co_yield 1/2/3`，在 `main` 里先打印一行 `before first next` 再逐次 `next()` 取值。贴出完整输出，指出哪一行输出证明了这个生成器是「惰性」的、以及惰性来自哪个接口的哪个返回值。

[参考答案 →](./02-homework-solutions.md#hw-4-1-a)

### 4.1-B {#hw-4-1-b}

难度 **L3** · 涉及[协程基础](../01-coroutine-basics.md)

两道实验。①写一个带 `co_yield` 的生成器，但**故意删掉 `promise_type` 里的 `return_void`**，再在协程体末尾加一句 `co_return;`——编译，贴出真实报错的关键行，说清编译器为什么点名 `return_void`。②写一个 `countdown(int n)` 生成器（`co_yield n, n-1, ..., 1`），打印 `3 2 1` 加一句 `liftoff`；然后回答：协程跑完后处于什么状态（`done()` 是真还是假）？既然 `final_suspend` 返回的是 `suspend_always`，协程帧是谁、在哪个时刻被销毁的？写出析构的代码路径。

[参考答案 →](./02-homework-solutions.md#hw-4-1-b)

## 4.2 协程调度器

### 4.2-A {#hw-4-2-a}

难度 **L2** · 涉及[协程调度器](../02-coroutine-scheduler.md)

实现一个可被 `co_await` 的 `Task<int>`：`co_add(a, b)` 里 `co_return a + b`，`main_task()` 里 `int result = co_await co_add(20, 22);` 打印结果。要求：`initial_suspend` 返回 `suspend_never`（照教材 Task 设计——否则协程体停在初始挂起点不开跑，`result` 取到的是默认初始化的 0 而不是 42）、`await_ready()` 恒返回 `false`、`await_suspend` 里直接 `h.resume()`、`final_suspend` 返回 `suspend_always`、析构函数负责 `destroy()`。跑通后回答两问：①`await_ready` 返回 `false` 到底意味着什么（结合「我要接管等待逻辑」说）？②`final_suspend` 挂起自己，是谁在何时把它收尸？

[参考答案 →](./02-homework-solutions.md#hw-4-2-a)

### 4.2-B {#hw-4-2-b}

难度 **L4** · 涉及[协程调度器](../02-coroutine-scheduler.md)

从零写一个单线程调度器：就绪队列 + 按唤醒时间排序的睡眠优先队列 + `run()` 主循环 + `SleepAwaiter`（`co_await` 它就把自己睡到指定时刻）。然后写两个协程 `print_alternating("A", 5)` 和 `print_alternating("B", 5)`，每个都「打印一次、睡 1ms」循环五次，`start()` 后跑调度器。**先预测**输出序列再真跑贴出来——为什么是交替的而不是先 A 完再 B？

坑位预警：协程参数 `tag` 一定要**按值传**。如果你图省事写成 `const std::string&`，而调用处传的是字符串字面量构造的临时对象，协程在 `initial_suspend` 挂起后参数引用会悬垂——本解答初版就这么栽过，真实输出是「B0 B0 B1 B1……」，A 打印出了 B 的内容。先自己想想为什么会这样，再对照答案里的拆解。

[参考答案 →](./02-homework-solutions.md#hw-4-2-b)

## 4.3 空基类优化

### 4.3-A {#hw-4-3-a}

难度 **L1** · 涉及[空基类优化](../03-empty-base-optimization.md)

写一个空结构 `Empty{}` 和三种容器：`AsMember`（`Empty` 作成员 + `int`）、`AsBase`（私有继承 `Empty` + `int`）、`AsNoUnique`（`[[no_unique_address]] Empty` 作成员 + `int`）。打印四个 `sizeof`。**先预测**每一个再真跑，然后说清：为什么 `Empty` 自己占 1 字节、而 `AsBase` 和 `AsNoUnique` 却只和 `int` 一样大？成员版多出来的字节去了哪？

[参考答案 →](./02-homework-solutions.md#hw-4-3-a)

### 4.3-B {#hw-4-3-b}

难度 **L3** · 涉及[空基类优化](../03-empty-base-optimization.md)、[模板导论](../vol1-basics-cpp11-14/01-templates-introduction.md)

手写一个 `CompressedPair<T, Deleter>`：用**私有继承** `Deleter` 压缩无状态删除器，`first()`/`second()` 两个访问接口，`second()` 里 `delete` 掉 `first()` 指向的对象。同时写一个把 `Deleter` 当普通成员的 `NaivePair<T, Deleter>` 对照组。打印两者与裸 `int*` 的 `sizeof`，运行验证删除正确。回答：①私有继承在这里为什么能白省空间（EBO 的哪条规则）？②`NaivePair` 多出来的字节哪来的？③如果把 `Deleter` 换成带一个 `int` 成员的有状态删除器，两个版本的大小会变成多少？先预测再改代码验证。

[参考答案 →](./02-homework-solutions.md#hw-4-3-b)

## 4.4 三路比较运算符

### 4.4-A {#hw-4-4-a}

难度 **L1** · 涉及[三路比较运算符](../05-spaceship-operator.md)

给 `struct SensorReading { int sensor_id; double value; };` 补上 `auto operator<=>(const SensorReading&) const = default;` 和 `bool operator==(...) const = default;`。验证：①六个比较运算符全部可用（贴输出）；②`std::sort` 一个乱序 `vector<SensorReading>`，贴排序结果并说明「默认按什么顺序比」；③只留 `<=>`、**删掉 `==`** 再编译运行——`==` 和 `!=` 还能用吗？说出这是哪条规则（上游/下游）的体现。

[参考答案 →](./02-homework-solutions.md#hw-4-4-a)

### 4.4-B {#hw-4-4-b}

难度 **L3** · 涉及[三路比较运算符](../05-spaceship-operator.md)

两道题。①写 `struct HasEqualityOnly { int value; bool operator==(...) const = default; };`，然后在 `main` 里写 `a < b`——编译，贴真实报错，说清「只 default `==` 换不来 `<`」的根因。②写一个大小写不敏感字符串 `CIString`：`operator<=>` 返回 `std::weak_ordering`（先都转小写再比），`operator==` 委托给 `<=>`。验证 `"Hello" == "HELLO"` 为真、`s1 <=> s2` 是 `equivalent`；再用 `std::sort` 排 `{"Banana", "apple", "Hello"}`，贴结果。最后答一问：`weak_ordering` 和 `strong_ordering` 的根本差别是什么？举例什么场景必须用 weak 不能用 strong。

[参考答案 →](./02-homework-solutions.md#hw-4-4-b)

## 4.5 C++ Modules（MSVC）

### 4.5-A {#hw-4-5-a}

难度 **L1** · 涉及[C++ Modules（MSVC）](../msvc-cpp-modules.md)

两道题。①连线配对：把 `#include` 的四个问题（编译速度灾难、宏污染、传染式依赖、ODR 隐式规则）与 modules 的对应机制（BMI/IFC 缓存、宏不跨模块传播、接口与实现解耦、编译器理解模块边界）一一对上，每个机制用一句话说明它怎么解决问题。②**本机实测**：教材写的是 MSVC/VS2026 的 `import std;` 流程，本机是 WSL Arch 的 g++ 16——写一个最小命名模块 `mymath`（`export int add(int,int)`），用 `g++ -std=c++20 -fmodules-ts` 编译运行，如实贴出结果。g++ 的模块产物和 MSVC 的 `.ifc` 有什么对应关系？如果你本机 g++ 模块支持不完整编译失败，也如实报告，注明「以教材 MSVC 流程为准」。

[参考答案 →](./02-homework-solutions.md#hw-4-5-a)

### 4.5-B {#hw-4-5-b}

难度 **L2** · 涉及[C++ Modules（MSVC）](../msvc-cpp-modules.md)

纯问答（不用写代码）。①为什么说宏是「无作用域」的？模块为什么天然能挡住宏污染——`import` 一个模块，被 import 的模块里定义的宏会跟着进来吗？说清「模块默认不导出宏」这件事的机制面。②`import std;` 背后四步流程是什么？BMI/IFC 被比作 Java 的 `.class` 文件，这个类比对在哪、又不完全对在哪？③按教材的四档场景（`import std;` / 新项目内部模块化 / 公共跨平台库 API / 高一致性要求项目），各给一个推荐等级和理由；再举一个你在真实工程里见过的 `#include` 宏污染实例（比如 `min`/`max`、`windows.h`），说明它具体怎么咬人。

[参考答案 →](./02-homework-solutions.md#hw-4-5-b)

## 4.6 模板导论

### 4.6-A {#hw-4-6-a}

难度 **L1** · 涉及[模板导论](../vol1-basics-cpp11-14/01-templates-introduction.md)

写一段代码把**四种模板实体**各亮一遍：函数模板 `square`（求平方，给 `int` 和 `double` 各调一次）、类模板 `Holder`（装一个值）、变量模板 `e<T>`（欧拉数 `2.718281828459045...`，C++14 起）、别名模板 `Vec<T> = std::vector<T>`。贴输出。再答一问：变量模板出现之前，「给每个类型配一个常量」是怎么绕的（写出老写法）？为什么标准库的 `numeric_limits<T>::max()` 是函数而不是变量？

[参考答案 →](./02-homework-solutions.md#hw-4-6-a)

### 4.6-B {#hw-4-6-b}

难度 **L2** · 涉及[模板导论](../vol1-basics-cpp11-14/01-templates-introduction.md)

把教材的编译期阶乘换成**编译期斐波那契**：`Fib<N>` 模板递归 + `Fib<0>`/`Fib<1>` 两个特化 + 变量模板 `fib_v`。用 `static_assert` 验 `Fib<10> == 55`、`fib_v<20> == 6765`，运行期再打印一次。回答：①这两个值是在编译期还是运行期算出来的？你的证据是什么（两条）？②「模板机制图灵完备」是什么意思，代价是什么（说两条）？

[参考答案 →](./02-homework-solutions.md#hw-4-6-b)

## 4.7 函数模板深化

### 4.7-A {#hw-4-7-a}

难度 **L2** · 涉及[函数模板深化](../vol1-basics-cpp11-14/02-function-templates-deep.md)

把教材的 `add` 换成 `gcd`（辗转相除）：声明放 `gcd.h`（只有 `template <typename T> T gcd(T, T);`），定义放 `gcd.cpp`，`main.cpp` 里调 `gcd(36, 48)`。编译链接，贴出真实的链接报错（undefined reference 那行）。然后修复：把定义挪进头文件，重新编译运行。回答：①为什么声明和定义分开后链接会挂——两个翻译单元各自发生了什么？②这个「包含模型」是模板的什么性质逼出来的？③`extern template` 和「真正的分离编译」差在哪？

[参考答案 →](./02-homework-solutions.md#hw-4-7-a)

### 4.7-B {#hw-4-7-b}

难度 **L3** · 涉及[函数模板深化](../vol1-basics-cpp11-14/02-function-templates-deep.md)、[类模板](../vol1-basics-cpp11-14/03-class-templates.md)

两道题。①对 `identity` 函数模板写一个 `identity<T*>` 的「偏特化」——编译，贴真实报错（`non-class, non-variable partial specialization` 那行），说清标准为什么只让类模板和变量模板偏特化、函数为什么靠重载。②写三个合法版本各跑一遍：指针重载版（`identity(T*)` 解引用）、`if constexpr` 分流版（指针 / 整型 / 其它三种输出）、以及一个故意用 `std::is_pointer_v` + `if constexpr` 的 `describe` 函数。贴全部输出。

[参考答案 →](./02-homework-solutions.md#hw-4-7-b)

## 4.8 类模板

### 4.8-A {#hw-4-8-a}

难度 **L2** · 涉及[类模板](../vol1-basics-cpp11-14/03-class-templates.md)

写 `template <typename T> struct Box { T value; void show() const; void broken() const { std::cout << value.nonexistent_member; } };`——`broken()` 里写的是 `T=int` 时的胡话。`main` 只调 `show()`，编译运行（能过，贴输出）。然后加一行 `template struct Box<int>;`（显式实例化）重新编译——这回报错了，贴真实报错。回答：①为什么第一次不报？②显式实例化为什么把它逼出来？③这条「错误藏得深」的脾气对写类模板的人意味着什么工程纪律？

[参考答案 →](./02-homework-solutions.md#hw-4-8-a)

### 4.8-B {#hw-4-8-b}

难度 **L3** · 涉及[类模板](../vol1-basics-cpp11-14/03-class-templates.md)、[名字查找与 ADL](../vol1-basics-cpp11-14/06-name-lookup-and-adl.md)

依赖名三连，各写一个最小可编译例子：①`typename` 消歧义——`print_first` 模板里声明 `typename Container::value_type first = *c.begin();`；②`this->` 访问 dependent base——`Derived<T> : Base<T>` 里 `this->data` 和 `this->greet()`；③`using` 声明引入基类名字——`using Base<T>::kDefault;` 后直接裸名访问。三个例子跑通，贴输出。最后答：这三种写法解决的是同一个机制的三个侧面——那个机制是什么（用「第一阶段看不到」解释）？

[参考答案 →](./02-homework-solutions.md#hw-4-8-b)

## 4.9 模板特化与偏特化

### 4.9-A {#hw-4-9-a}

难度 **L2** · 涉及[模板特化与偏特化](../vol1-basics-cpp11-14/04-specialization-partial.md)

把教材的 `is_pointer` 换成 `IsFloating`：主模板 `value = false`，给 `float`、`double`、`long double` 各写一个全特化返回 `true`。验证 `int`、`float`、`double`、`long double`、`int*` 五种输入，贴输出。回答：①主模板 + 特化的这个「默认 false、命中 true」套路是标准库谁的底层？②全特化是模板吗？它对 ODR 有什么特殊要求（和主模板实例化比）？

[参考答案 →](./02-homework-solutions.md#hw-4-9-a)

### 4.9-B {#hw-4-9-b}

难度 **L3** · 涉及[模板特化与偏特化](../vol1-basics-cpp11-14/04-specialization-partial.md)

两道题。①`std::vector<bool> v{true, false};` 之后写 `auto& x = v[0];`——编译，贴真实报错，说清 `vector<bool>` 的 `operator[]` 返回的是什么、为什么绑定不了 `auto&`；这是偏特化带来的什么「反面教材」？②手写 `IsRef`：主模板 `false` + `T&`、`T&&` 两个偏特化。验证 `int`、`int&`、`int&&`、`int*`，贴输出；说说为什么一个 `T&` 偏特化就同时覆盖了 `int&` 和 `const int&`（`T` 可以绑定什么？）。

[参考答案 →](./02-homework-solutions.md#hw-4-9-b)

## 4.10 非类型模板参数

### 4.10-A {#hw-4-10-a}

难度 **L2** · 涉及[非类型模板参数](../vol1-basics-cpp11-14/05-non-type-parameters.md)

写 `template <auto N> struct Constant { static constexpr auto value = N; };`，分别用 `42`、`true`、`'a'` 实例化并打印。贴输出，回答：①`auto` 占位符分别推导成了什么类型？②C++17 之前要表达「一个类型 + 一个值」得写 `template <typename T, T N>` 两个参数，`auto` 省掉了什么？③非类型实参必须满足哪条铁律（说清「编译期确定」的边界，举一个会编译失败的例子场景）？

[参考答案 →](./02-homework-solutions.md#hw-4-10-a)

### 4.10-B {#hw-4-10-b}

难度 **L4** · 涉及[非类型模板参数](../vol1-basics-cpp11-14/05-non-type-parameters.md)

两道题，都要真跑。①定义一个 structural 类 `Point{int x, int y; constexpr Point(int,int);}`，写 `template <Point P> struct Pixel`，验证 `Pixel<Point{1,1}>` 与 `Pixel<Point{1,1}>` 是同一类型、与 `Pixel<Point{1,2}>` 不是；打印 `Pixel<Point{3,4}>::pos`。说清 structural 的三个条件。②浮点 NTTP 的位级等价：`template <double V> struct Tag{};`，用 `std::is_same_v` 验证 `Tag<0.0>` 与 `Tag<-0.0>` **不是**同一类型、`Tag<3.14>` 与 `Tag<3.14000>` **是**。贴输出并解释：等价性的判据是「位级」还是「数值」，为什么这么定？③字符串字面量为什么从来不能直接做 NTTP（C++20 也一样）？C++20 的出路是什么？

[参考答案 →](./02-homework-solutions.md#hw-4-10-b)

## 4.11 名字查找与 ADL

### 4.11-A {#hw-4-11-a}

难度 **L2** · 涉及[名字查找与 ADL](../vol1-basics-cpp11-14/06-name-lookup-and-adl.md)

在 `namespace geo` 里定义 `Point{x,y}`、`draw(const Point&)` 和 `swap(Point&, Point&)`（swap 里打印一行 `geo::swap called`）。`main` 里裸调 `draw(p)`（不写 `geo::`）；再写一个模板 `demo_swap`，里面 `using std::swap; swap(a, b);`，调用后打印 `a` 的新坐标。贴输出，回答：①裸调 `draw` 为什么能找到 `geo::draw`？②`using std::swap` 那行起什么作用、为什么不能省？③这套 swap 惯用法给自定义类型留了什么优化空间？

[参考答案 →](./02-homework-solutions.md#hw-4-11-a)

### 4.11-B {#hw-4-11-b}

难度 **L3** · 涉及[名字查找与 ADL](../vol1-basics-cpp11-14/06-name-lookup-and-adl.md)

把教材的 `helper` 换成 `classify`：模板定义**之前**有 `classify(int)`，定义**之后**有 `classify(double)`，模板 `call_classify(T x)` 里裸调 `classify(x)`，`main` 里 `call_classify(3.14)`。**先预测**走哪个重载再真跑贴输出——如果和直觉相反，用两阶段查找解释（哪一阶段绑定了谁、第二阶段为什么不帮忙）。再答：想让 ADL 介入，得把 `classify` 和实参类型摆成什么关系？给出修改方案并验证。

[参考答案 →](./02-homework-solutions.md#hw-4-11-b)

## 4.12 模板友元与 Barton-Nackman

### 4.12-A {#hw-4-12-a}

难度 **L2** · 涉及[模板友元与 Barton-Nackman](../vol1-basics-cpp11-14/07-friends-and-barton-nackman.md)

给 `template <typename T> class Interval`（私有成员 `lo_`/`hi_`）配两个**隐藏友元**：类内定义的 `operator==` 和 `operator<<`。验证 `a == b`、`a == c`、`cout << a` 三项，贴输出。然后写一个跨类型比较 `Interval<int>` 与 `Interval<double>` 的 `==`——编译，贴真实报错，说清隐藏友元「不跨界」为什么是特性而不是 bug、它靠什么机制（ADL + 精确匹配）做到「该出现才出现」。

[参考答案 →](./02-homework-solutions.md#hw-4-12-a)

### 4.12-B {#hw-4-12-b}

难度 **L3** · 涉及[模板友元与 Barton-Nackman](../vol1-basics-cpp11-14/07-friends-and-barton-nackman.md)、[CRTP](../vol1-basics-cpp11-14/09-crtp.md)

写一个 `Comparable<Derived>` mixin（CRTP + 友元注入）：给任何实现了 `<` 和 `==` 的类型自动补齐 `>`、`<=`、`>=`、`!=`。用 `Version{major, minor}`（`<` 先比 major 再比 minor）做派生类，验证四个补齐的运算符，贴输出。回答：①mixin 里四个 `friend` 函数为什么不是函数模板、而是随 `Comparable<Version>` 实例化生成的普通函数？②这套组合里 CRTP 提供什么、友元注入提供什么？

[参考答案 →](./02-homework-solutions.md#hw-4-12-b)

## 4.13 别名模板与 using 声明

### 4.13-A {#hw-4-13-a}

难度 **L1** · 涉及[别名模板与 using 声明](../vol1-basics-cpp11-14/08-alias-and-using.md)

写 `Vec<T> = std::vector<T>` 和 `Arr<T, N> = std::array<T, N>` 两个别名模板，各用一次；再用 `std::is_same_v` 验证「别名不是新类型」：`Vec<int>` 与 `std::vector<int>` 是否同一类型、`std::remove_reference_t<int&>` 与 `int` 是否同一类型、`std::is_integral_v<int>` 是什么。贴输出，回答：`_t` 别名（C++14 起）和 `_v` 变量（C++17 起）分别是给哪两样东西省了 `typename`/`::value` 的？

[参考答案 →](./02-homework-solutions.md#hw-4-13-a)

### 4.13-B {#hw-4-13-b}

难度 **L3** · 涉及[别名模板与 using 声明](../vol1-basics-cpp11-14/08-alias-and-using.md)、[类模板](../vol1-basics-cpp11-14/03-class-templates.md)

两道题。①写 `template <typename T> using V = T;` 再写 `template <> using V<int> = long;`——编译，贴真实报错，说清别名模板为什么不能特化、真需要「按类型给不同别名」得靠什么包一层。②模板继承里用 `using` 引入 dependent base 的名字：`Base<T>` 里有 `static inline T kDefault{42};` 和 `greet()`，`Derived<T>` 里 `using Base<T>::kDefault; using Base<T>::greet;` 之后裸名访问，跑通贴输出；对比 `this->` 写法，说说两种解法各自的适用场景。

[参考答案 →](./02-homework-solutions.md#hw-4-13-b)

## 4.14 CRTP

### 4.14-A {#hw-4-14-a}

难度 **L2** · 涉及[CRTP](../vol1-basics-cpp11-14/09-crtp.md)

把教材的形状换成动物：`template <typename Derived> struct Animal { const char* sound() { return static_cast<Derived*>(this)->sound_impl(); } };`，`Dog`/`Cat` 各自继承 `Animal<Dog>`/`Animal<Cat>` 并实现 `sound_impl()`。跑通贴输出。回答：①基类里的 `static_cast<Derived*>(this)` 凭什么安全（类型系统帮你拦了什么）？②CRTP 的多态和虚函数多态的根本差别（编译期 vs 运行期、调用形式），CRTP 的边界是什么（什么场景它干不了）？

[参考答案 →](./02-homework-solutions.md#hw-4-14-a)

### 4.14-B {#hw-4-14-b}

难度 **L4** · 涉及[CRTP](../vol1-basics-cpp11-14/09-crtp.md)

两道题。①汇编实证：写 `Base<D>` 的 CRTP 版 `use_crtp()`（返回 42）和虚函数版 `use_virtual(BaseV&)`，各用 `-O2 -c` 编译后 `objdump -d`，把两个函数的反汇编贴出来逐行对比——数一数指令条数、内存访问次数，说清「零开销」到底零在哪。②问答：为什么 CRTP 基类的**构造函数和析构函数里**不能调 `static_cast<Derived*>(this)->派生方法`？（对象成形了吗？）举一个会发生这种调用的真实场景，说明它多危险。

[参考答案 →](./02-homework-solutions.md#hw-4-14-b)

## 4.15 综合项目：fixed_vector

### 4.15-A {#hw-4-15-a}

难度 **L2** · 涉及[综合项目:fixed_vector](../vol1-basics-cpp11-14/10-fixed-vector.md)

实现一个最小 `FixedVector<T, N>`：`push_back`（满了抛 `std::out_of_range`）、`operator[]`、`size()`、裸指针迭代器 `begin()/end()`、静态常量 `capacity_v`。`main` 里塞 1 到 5 的十倍、范围 for 打印、随机访问、打印 `sizeof`。贴输出。回答：①`sizeof(FixedVector<int,8>) == 40` 这 40 字节都是什么（有没有堆指针）？②为什么裸指针 `T*` 天生就是个合格的随机访问迭代器——它满足了哪几个接口？③「零动态分配」在嵌入式/实时场景的价值，说两条。

[参考答案 →](./02-homework-solutions.md#hw-4-15-a)

### 4.15-B {#hw-4-15-b}

难度 **L4** · 涉及[综合项目:fixed_vector](../vol1-basics-cpp11-14/10-fixed-vector.md)、[非类型模板参数](../vol1-basics-cpp11-14/05-non-type-parameters.md)

两道题。①把 `push_back`/`operator[]`/`size` 全标 `constexpr`，写一个 `constexpr FixedVector<int,4> build()` 在编译期塞三个数，用 `static_assert` 验 `size()==3`、`[1]==2`，运行期打印——贴输出，说清「容器在编译期也能用」意味着什么、`throw` 在 constexpr 函数里的规矩是什么。②加一个 `try_push_back`：满了返回 `std::nullopt`，否则塞入并返回指向新元素的指针（包 `std::optional<T*>`）。容量 2 的容器连塞三次，贴输出。最后：教材对比的 `std::inplace_vector` 是哪个标准的？**先预测**本机 libstdc++ 16 是否已提供该组件，再用正确的探测法实测验证——写一个 `#include <inplace_vector>` 的小程序打印 `__cpp_lib_inplace_vector`，如实贴出特性值。注意别用 `echo | g++ -std=c++26 -dM -E -x c++ - | grep inplace_vector` 这种探测：`-dM -E` 只列出编译器预定义宏，库特性宏定义在头文件里，不包含头文件永远看不见；做完实测后解释一下，这个探测法为什么是错的。

[参考答案 →](./02-homework-solutions.md#hw-4-15-b)

## 4.16 指定初始化器

### 4.16-A {#hw-4-16-a}

难度 **L1** · 涉及[指定初始化器](../vol2-modern-cpp17/06-designated-initializers.md)

写 `struct NetConfig { std::string host = "localhost"; uint16_t port = 8080; bool use_tls = false; };`，用指定初始化器**部分初始化**两个实例（只指定部分字段），打印验证「未指定的字段用默认成员初始化器」。再写一个无默认值的 `struct Raw { uint16_t p; bool t; };`，`Raw r{.p = 80};` 打印——`t` 是什么（贴输出，注意编译器给的 `-Wmissing-field-initializers` 提醒正是这件事的注脚）？

**重要变式**：旧版教材示例里曾有一句「乱序也没问题」——那是 **C99** 的语义（现行版教材已改对）。**C++20 规定指定初始化器必须按声明顺序写**。写一个乱序的初始化（比如 `UART c{.parity = 0, .baudrate = 115200, ...}`），编译贴出真实报错。C99/C++20 这条差异是高频踩坑点，你的实测结果才是标准答案——本题就是要你把这条规则记牢。

[参考答案 →](./02-homework-solutions.md#hw-4-16-a)

### 4.16-B {#hw-4-16-b}

难度 **L2** · 涉及[指定初始化器](../vol2-modern-cpp17/06-designated-initializers.md)

两道题。①给 `struct Config` 加一个**用户构造函数**，再对它写 `Config c{.rate = 115200};`——编译，贴真实报错，说清「聚合类型」的定义里哪一条被破坏了。②写带默认成员初始化器的 `Cfg{rate=9600, bits=8}`，用 `{.rate = 115200}` 覆盖 `rate`，验证 `bits` 仍走默认值 8——贴输出，说明「显式指定的值覆盖默认成员初始化器、未指定的走默认」这条规则的完整优先级。

[参考答案 →](./02-homework-solutions.md#hw-4-16-b)

## 4.17 C++20 范围库基础与视图

### 4.17-A {#hw-4-17-a}

难度 **L2** · 涉及[C++20 范围库基础与视图](../vol2-modern-cpp17/07-ranges-basics-and-views.md)

把教材的传感器读数换成**体温列表** `{38, 36, 39, 41, 35, 33, 40, 37}`：①`filter`（35~40 度之间）+ `transform`（减 35 得到偏移量）的视图，遍历打印；②`std::views::iota(0, 10) | take(3)`；③`iota(0, 6) | drop(4)`。贴三组输出。回答：①视图的四个特征（懒、不拥有、可组合、O(1) 拷贝）在这段代码里各对应哪一行/哪个行为？②创建视图时没有发生任何遍历——你的证据是？

[参考答案 →](./02-homework-solutions.md#hw-4-17-a)

### 4.17-B {#hw-4-17-b}

难度 **L3** · 涉及[C++20 范围库基础与视图](../vol2-modern-cpp17/07-ranges-basics-and-views.md)、[管道操作与 Ranges 实战](../vol2-modern-cpp17/08-ranges-pipeline-in-practice.md)

两道题。①写 `auto make_bad_view()`：里面建一个局部 `vector<int>{1..5}`，`return` 一个引用它的 `views::filter` 视图；`main` 里遍历这个返回的视图——用 **ASan** 构建运行，贴真实报告的关键行（`stack-use-after-return`/`heap-use-after-free` 哪个都行，如实贴），说清「视图不拥有数据」和生命周期约束之间的张力。②用 `std::views::iota(1, 10) | filter(偶数) | std::ranges::to<std::vector<int>>()`（注意：`std::ranges::to` 是 **C++23** 的，用 `-std=c++23`）转成真容器，**连续遍历两遍**，贴输出——为什么这一遍能安全反复迭代，而上一个视图不能？

[参考答案 →](./02-homework-solutions.md#hw-4-17-b)

## 4.18 管道操作与 Ranges 实战

### 4.18-A {#hw-4-18-a}

难度 **L2** · 涉及[管道操作与 Ranges 实战](../vol2-modern-cpp17/08-ranges-pipeline-in-practice.md)

把教材的传感器管道换成**原始读数清洗管道**：`{120, 45, 230, 67, 340, 89, 56, 180}`，`filter`（50~300 之间）`| transform`（×2 校准）`| take(4)`，遍历打印。贴输出。回答：①管道运算符 `|` 的左边和右边各是什么（Range、视图适配器）？②「整个过程中没有任何数据拷贝、直到迭代才计算」——这句话的机制依据是什么（惰性求值 + 链式包装）？③先 filter 再 take，和先 take 再 filter，结果一定不同吗？举一组数据说明（注意数据要和本题的 50~300 过滤器自洽——可以直接用题面开头那组读数；可以只推理不写代码）。

[参考答案 →](./02-homework-solutions.md#hw-4-18-a)

### 4.18-B {#hw-4-18-b}

难度 **L4** · 涉及[管道操作与 Ranges 实战](../vol2-modern-cpp17/08-ranges-pipeline-in-practice.md)、[迭代器模式](../vol4-generics-patterns/18-iterator.md)、[使用 Concepts 约束模板](../vol3-metaprogramming-cpp20-23/02-constraining-templates.md)

写一个自定义 `Squares` range：迭代器 `SquareIterator` 持一个 `int`，`operator*` 返回平方，配齐 `operator++` 前后缀、`operator==`、以及 `value_type/difference_type/iterator_concept/iterator_category` 四个关联类型；`Squares{first, last}` 提供 `begin()/end()`。用 `static_assert(std::ranges::input_range<Squares>)` 验证，再把它接上 `views::filter(偶数) | transform(×10)` 管道打印。

坑位预警（真实踩过）：如果迭代器**没有默认构造函数**，`std::ranges::range` 概念的 `sentinel_for` 检查会卡在 `semiregular` 上直接不满足，`static_assert` 挂掉、报错信息深不见底。先按「没有默认构造」写一版，贴出真实的 `static assertion failed` 报错，再补上 `SquareIterator() = default;` 重跑贴正确输出。答一问：为什么一个「只需要构造时给初值」的迭代器，标准库偏偏要求它可默认构造（跟 `end()` 哨兵怎么构造有关）？

[参考答案 →](./02-homework-solutions.md#hw-4-18-b)

## 4.19 Concepts：把模板约束写进签名

### 4.19-A {#hw-4-19-a}

难度 **L1** · 涉及[Concepts:把模板约束写进签名](../vol3-metaprogramming-cpp20-23/01-concepts.md)

定义 `Numeric = std::integral || std::floating_point`，然后**四种语法形式**各写一个函数：`template <Numeric T>` 直接约束、`requires Numeric<T>` 尾置子句、`Numeric auto` 简写、`requires requires(T x) { x + x; }` 内联表达式。分别用 `twice/thrice/quadruple/plus_self` 四个名字，`int` 调三个、`std::string` 调最后一个（字符串有 `operator+`）。贴输出。回答：①四个形式在语义上等价吗？差别只在哪？②`requires requires` 连写时，外层是什么、内层是什么？

[参考答案 →](./02-homework-solutions.md#hw-4-19-a)

### 4.19-B {#hw-4-19-b}

难度 **L3** · 涉及[Concepts:把模板约束写进签名](../vol3-metaprogramming-cpp20-23/01-concepts.md)

报错对比实验：写一个只收数值的 `add`，①用 `std::enable_if_t` 塞进默认模板参数；②用 `Numeric` concept 约束。两个版本都拿 `std::string` 去调，各自编译，把两份真实报错的关键行都贴出来。对比回答：①`enable_if` 版的报错在讲谁的内部机制（哪一行最扎眼）？②concept 版的报错点出了哪两个关键信息（约束名 + 失败类型）？③为什么说「concept 的优势不在报错短了几十行，而在信息直接指向约束本身」？

[参考答案 →](./02-homework-solutions.md#hw-4-19-b)

## 4.20 使用 Concepts 约束模板

### 4.20-A {#hw-4-20-a}

难度 **L2** · 涉及[使用 Concepts 约束模板](../vol3-metaprogramming-cpp20-23/02-constraining-templates.md)

把教材的 Animal/Dog 换成 Vehicle/Car：`Vehicle` 要求 `drive()`，`Car = Vehicle && 要求 honk()`。`describe(Vehicle auto)` 和 `describe(Car auto)` 两个重载，`Bike`（只有 drive）和 `Sedan`（drive+honk）各调一次。**先预测**各走哪个重载再真跑贴输出。回答：①`Car` 为什么「蕴含」`Vehicle`——用原子约束集合解释；②如果两个约束互不蕴含而某个类型都满足，会发生什么（用文字描述编译器的行为，不用写代码）？

[参考答案 →](./02-homework-solutions.md#hw-4-20-a)

### 4.20-B {#hw-4-20-b}

难度 **L4** · 涉及[使用 Concepts 约束模板](../vol3-metaprogramming-cpp20-23/02-constraining-templates.md)

两道题，都要真跑。①写 `C1 = std::is_integral_v<T>`、`C2 = C1<T>`（只改名字），两个 `g(C1 auto)`/`g(C2 auto)` 重载，`g(42)`——编译，贴真实报错（ambiguous），用「原子约束集合相同 → 没有真包含」解释为什么换个名字不会凭空造出「更特定」。②写教材验证过的 A/B/C 三概念（`A` 要 `a()`、`B` 要 `b()`、`C = A && B`），三个 `f` 重载，用同时有 `a()` 和 `b()` 的 `S` 调——贴输出，说明 `&&` 的真实角色是「把两边原子约束**并集**进同一集合」，所以 `C` 才同时蕴含 A 和 B。

[参考答案 →](./02-homework-solutions.md#hw-4-20-b)

## 4.21 Requires 表达式深度解析

### 4.21-A {#hw-4-21-a}

难度 **L2** · 涉及[Requires 表达式深度解析](../vol3-metaprogramming-cpp20-23/03-requires-expressions.md)

写一个 `Container` 概念，**四种成分一次用全**：简单要求（`t.begin(); t.end();`）、类型要求（`typename T::value_type;`）、复合要求（`{ t.size() } -> std::convertible_to<std::size_t>;`）、嵌套要求（`requires std::integral<typename T::value_type>;`）。验证 `vector<int>` 满足、`int` 不满足、`vector<double>` 也不满足（卡在哪一条？）。贴输出并逐一指出每个断言对应的成分。顺带答：`std::integral<char>` 是真是假？这意味着 `vector<char>` 满不满足这个 `Container`？

[参考答案 →](./02-homework-solutions.md#hw-4-21-a)

### 4.21-B {#hw-4-21-b}

难度 **L4** · 涉及[Requires 表达式深度解析](../vol3-metaprogramming-cpp20-23/03-requires-expressions.md)

两道实验，都要真跑。①**不求值坑**：全局 `int counter = 0;`、`increment()` 里 `++counter` 并打印副作用；概念 `MentionsIncrement` 的 requires 表达式里写 `increment();`。`static_assert(MentionsIncrement<int>)` 后打印 `counter`，再真正调一次 `increment()` 打印——贴输出，说清 requires 表达式和 `decltype`/`sizeof` 一样属于什么上下文。②**具体类型硬错误坑**：写 `static_assert(!requires(std::string s) { s.nope(); });`——编译，贴真实硬错误；然后改成「包进概念」（`HasNope` 概念 + 负例断言），贴「全部断言通过」。为什么包进模板上下文（概念）之后负例就从硬错误变成了优雅的 `false`？

[参考答案 →](./02-homework-solutions.md#hw-4-21-b)

## 4.22 单例模式

### 4.22-A {#hw-4-22-a}

难度 **L1** · 涉及[单例模式](../vol4-generics-patterns/01-singleton.md)

写一个 Meyer's Singleton：私有构造 + `static Config& instance()`（函数内 `static` 局部变量）+ 拷贝/赋值 `= delete`；类里给一个 `int next() { return ++counter_; }`。`main` 里拿两个引用 `a`/`b`，打印 `&a == &b`、`a.next()`、`b.next()`。贴输出。回答：①`static` 局部变量的「魔法」从哪个标准版本开始、保证的是什么？②为什么拷贝构造和拷贝赋值必须 delete——不删的话哪个合法的 C++ 动作会悄悄造出第二个实例？③这一行锁都不用的线程安全来自语言的哪条保证？

[参考答案 →](./02-homework-solutions.md#hw-4-22-a)

### 4.22-B {#hw-4-22-b}

难度 **L3** · 涉及[单例模式](../vol4-generics-patterns/01-singleton.md)

并发实证：单例构造器里 `++construct_count`（`static inline std::atomic<int>`），起 **300 个线程**同时抢 `instance()`，join 后打印构造次数。多跑几次，贴输出（应当稳定是 1）。回答：①这个「只构造一次」由什么机制兜底（不是锁、不是 call_once）？②如果构造器里有重活（读文件、初始化连接池），并发场景会怎么样？③手写 DCLP 为什么在现代 C++ 里被劝退（两条理由），`memory_order_consume` 又有什么额外问题？

[参考答案 →](./02-homework-solutions.md#hw-4-22-b)

## 4.23 构建器模式

### 4.23-A {#hw-4-23-a}

难度 **L2** · 涉及[构建器模式](../vol4-generics-patterns/02-builder.md)

把教材的 Task 换成 Pizza：`Pizza{size, base, topping}`（size 和 base 必填），流式 `PizzaBuilder` 用 `with_size/with_base/with_topping` 各返回 `*this`，`build()` 缺必填就抛 `std::runtime_error`，选填字段用 `std::optional` 存。验证：完整链构造一个（贴输出，注意 `operator<<` 打印），缺必填的链被 `build()` 拦住（贴 caught 输出）。回答：①`std::optional` 在这里同时扮演了哪两个角色（比 `bool is_set` 标志位强在哪）？②`build()` 里 `return p;` 为什么是零拷贝（哪个标准、什么名字）？③流式构建器为什么不能跨线程复用？

[参考答案 →](./02-homework-solutions.md#hw-4-23-a)

### 4.23-B {#hw-4-23-b}

难度 **L4** · 涉及[构建器模式](../vol4-generics-patterns/02-builder.md)

写一个两阶段的阶段式构建器：`StageA`（`with_a(int)` 返回 `StageB`）、`StageB`（`with_b(int)` 返回 `StageFinal`）、`StageFinal`（`build()` 返回 $a+b$），草稿 `Draft` 用 `std::optional` 在阶段间 move 传递。验证正常链 `create().with_a(1).with_b(2).build()` 得 3。然后**故意犯两个错**各编译一次，贴真实报错：①漏填必填项直接 build（`create().with_a(1).build()`——`build` 在哪个类型上不存在？）；②顺序写反（`create().with_b(2)`）。回答：为什么这两个错误在流式构建器里只能拖到运行时、而在这里被压成了编译期错误？代价是什么（类型爆炸在哪）？

[参考答案 →](./02-homework-solutions.md#hw-4-23-b)

## 4.24 工厂方法与抽象工厂

### 4.24-A {#hw-4-24-a}

难度 **L2** · 涉及[工厂方法与抽象工厂](../vol4-generics-patterns/03-factory-method-abstract-factory.md)

把教材的汉堡换成图形：`Shape` 抽象基类（`name()`/`area()`，**虚析构**）+ `Circle`/`Square` 两个派生 + `enum class ShapeKind` + 简单工厂 `ShapeFactory::create(ShapeKind)` 返回 `std::unique_ptr<Shape>`。遍历两种类型，打印名字和面积，贴输出。回答：①工厂为什么必须返回 `unique_ptr<基类>` 而不是裸指针或 `unique_ptr<派生类>`（三条理由）？②基类析构为什么必须是 `virtual`——不写会怎样？③简单工厂违反开闭原则具体体现在哪一步（加一种新形状要改哪里）？

[参考答案 →](./02-homework-solutions.md#hw-4-24-a)

### 4.24-B {#hw-4-24-b}

难度 **L3** · 涉及[工厂方法与抽象工厂](../vol4-generics-patterns/03-factory-method-abstract-factory.md)、[单例模式](../vol4-generics-patterns/01-singleton.md)

写一个函数式工厂：`register_creator(string, std::function<unique_ptr<Burger>()>)` 注册表（函数内 `static` 局部变量持有，Meyer's Singleton 套路）+ `create(key)` 查表。注册 `"beef"`、`"fish"` 两个 lambda，取 `"beef"` 和一个**不存在的 key**——贴输出（后者要安全返回空，不崩）。回答：①相比工厂方法，函数式工厂把「加新产品」的成本降到了什么（改哪里）？②它的类型安全降级在哪——拼错 key 在哪个阶段才暴露？调用侧必须做什么检查？③注册表的初始化线程安全由什么保证？如果注册发生在运行期多线程下，还要补什么？

[参考答案 →](./02-homework-solutions.md#hw-4-24-b)

## 4.25 原型模式

### 4.25-A {#hw-4-25-a}

难度 **L2** · 涉及[原型模式](../vol4-generics-patterns/04-prototype.md)

把教材的 Address 换成 Document：`Document{title}` 基类带 `virtual Document* clone()`（返回 `new Document(*this)`），`Spreadsheet` 派生加一个 `int rows` 成员、`clone()` 协变返回 `Spreadsheet*`。用基类指针 `p`（实际指向 Spreadsheet）调 `clone()`，用 `typeid` 打印克隆体的动态类型、`dynamic_cast` 取回 `rows` 验证派生部分没被切掉。贴输出。回答：①如果直接 `new Document(*p)`（静态类型是基类的拷贝构造）会怎样——那个坑叫什么？②协变返回类型允许什么、在这里起了什么作用？

[参考答案 →](./02-homework-solutions.md#hw-4-25-a)

### 4.25-B {#hw-4-25-b}

难度 **L3** · 涉及[原型模式](../vol4-generics-patterns/04-prototype.md)

两道题。①`Widget` 纯虚基类带 `virtual std::unique_ptr<Widget> clone() const = 0;`，`Button{label}` 实现里 `return std::make_unique<Button>(*this);`——注意 `unique_ptr<Button>` 到 `unique_ptr<Widget>` 的隐式转换。用 `typeid` 打印克隆体动态类型、调用 `draw()`，贴输出。回答：为什么 `unique_ptr` 之间不支持协变返回类型（它们的关系是什么）？②浅拷贝陷阱：`SharedBuffer` 内部持 `shared_ptr<int>`，拷贝构造用编译器合成版，`b = a; a.set(999);` 后打印 `b.get()`——贴输出，说清默认拷贝构造对 `shared_ptr` 成员做的是什么；这给 `clone()` 的实现者什么教训（拷贝语义三问）？

[参考答案 →](./02-homework-solutions.md#hw-4-25-b)

## 4.26 适配器模式

### 4.26-A {#hw-4-26-a}

难度 **L2** · 涉及[适配器模式](../vol4-generics-patterns/05-adapter.md)

把教材的 Line→Point 换成**逻辑坐标→屏幕坐标**：业务侧有 `LogicalPoint{double x, y}` 列表，Adaptee `Renderer` 只认 `ScreenPoint{int px, py}` 的迭代器区间（`draw(b, e)` 计数）。适配器 `PointAdapter` 构造时收 `(vector<LogicalPoint>, double scale)`，把每个点乘缩放系数转成屏幕点存起来，对外给一对迭代器。验证两个点缩 2 倍后 `drawn == 2`，贴输出。回答：①GoF 三件套里 Target/Adaptee/Adapter 各是这段代码里的谁？②对象适配器（组合）相比类适配器（私有继承）的三条优势？③构造时翻译（急）和惰性翻译各自的取舍？

[参考答案 →](./02-homework-solutions.md#hw-4-26-a)

### 4.26-B {#hw-4-26-b}

难度 **L3** · 涉及[适配器模式](../vol4-generics-patterns/05-adapter.md)

写两个 `sum()` 适配器：`RefAdapter` **持有 `const vector<int>&` 引用**、`CopyAdapter` 构造时**复制一份**。`main` 里都用**临时对象**构造：`RefAdapter bad(std::vector<int>{1,2,3}); bad.sum();` 和 `CopyAdapter good(std::vector<int>{1,2,3}); good.sum();`。用 **ASan** 构建运行，贴真实报告（引用版应报 `stack-use-after-scope`），说清临时对象的生命周期和引用悬挂的因果；再说明为什么拷贝版安然无恙。最后答：什么场景下持引用是**安全且应该的**（说清前提），为什么默认该走拷贝。

[参考答案 →](./02-homework-solutions.md#hw-4-26-b)

## 4.27 桥接模式（pImpl）

### 4.27-A {#hw-4-27-a}

难度 **L2** · 涉及[桥接模式(pImpl)](../vol4-generics-patterns/06-bridge.md)

把教材的「形状 × 渲染后端」换成「消息 × 传输协议」：`Channel` 抽象（`send(msg)`）+ `TcpChannel`/`UdpChannel` 两个实现；`Message{body, unique_ptr<Channel>}` 的 `deliver()` 委托给通道。同一句话分别用 TCP、UDP 送一遍，贴输出。回答：①两个维度各是什么、类的数量从乘法变加法体现在哪？②「桥」在代码里是哪个成员？③Bridge 和 Adapter 的意图差别一句话（预先分离 vs 事后粘合）？

[参考答案 →](./02-homework-solutions.md#hw-4-27-a)

### 4.27-B {#hw-4-27-b}

难度 **L4** · 涉及[桥接模式(pImpl)](../vol4-generics-patterns/06-bridge.md)

pImpl 完整四步，每一步都真跑。①把 `~Widget() = default;` 写在**头文件**里（`Impl` 只前向声明、从未完整定义），编译，贴真实的 `invalid application of 'sizeof' to incomplete type` 报错，说清 `unique_ptr<不完整类型>` 的析构为什么必须挪到 `Impl` 完整的地方。②完整版：头文件只留前向声明 + `unique_ptr`，析构/移动在 cpp `= default`，拷贝靠 `Impl::clone()` + copy-and-swap。验证 `sizeof(Widget) == 8`、深拷贝独立。③**noexcept 对照实验**：`Widget`（move 标 `noexcept`）和 `WidgetNx`（move 不标）各往 `vector` 里 `push_back` 1000 个（**不要 reserve**，让扩容真实发生），打印 `Impl` 拷贝计数——贴两组真实数字（本机实测一组是 1、一组是 1023），说清 `move_if_noexcept` 和 `vector` 强异常安全保证之间的联动。④答一问：pImpl 换来哪三件实打实的好处？

[参考答案 →](./02-homework-solutions.md#hw-4-27-b)

## 4.28 装饰器模式

### 4.28-A {#hw-4-28-a}

难度 **L2** · 涉及[装饰器模式](../vol4-generics-patterns/07-decorator.md)

动态装饰器链：`Printer` 抽象（`print(const string&) const`）+ `Plain`（原样输出）+ 装饰器基类（持 `shared_ptr<Printer>`）+ `QuoteDecorator`（加双引号）/`StarDecorator`（加 `***`）。链的顺序是 `Star(Quote(Plain))`，调 `print("hi")`——**先预测**输出再真跑贴结果，说清「谁在外层谁先动手」。回答：①装饰器和被装饰对象为什么必须是同一个接口（这给「无限嵌套」提供了什么前提）？②这里为什么用 `shared_ptr`（三条理由）？③写「先改造参数再转发」的装饰器时，最隐蔽的笔误是什么（什么错误编译器抓不到）？

[参考答案 →](./02-homework-solutions.md#hw-4-28-a)

### 4.28-B {#hw-4-28-b}

难度 **L4** · 涉及[装饰器模式](../vol4-generics-patterns/07-decorator.md)、[CRTP](../vol1-basics-cpp11-14/09-crtp.md)

模板 mixin 版装饰器：`PlainRaw`（无虚函数）+ `QuoteMixin<Base>`/`StarMixin<Base>`（继承 Base 并叠加），`using Decorated = StarMixin<QuoteMixin<PlainRaw>>;`。跑通（输出应与动态版一致）并贴。然后做两个实证：①`static_assert(!std::is_polymorphic_v<Decorated>)`——说明这条链上没有虚表；②`-O2 -c` 编译后 `objdump -d`，数一下 `main` 里对 `simple_print`/`Mixin` 的 `call` 有多少次（应贴出真实的 0），解释「整条装饰链在编译期被拍成一个具体类型、全部内联」意味着什么。最后答：mixin 的代价搬到了哪里（类型爆炸的两条具体表现）？

[参考答案 →](./02-homework-solutions.md#hw-4-28-b)

## 4.29 组合模式

### 4.29-A {#hw-4-29-a}

难度 **L2** · 涉及[组合模式](../vol4-generics-patterns/08-composite.md)

把教材的 Graphic 换成**文件系统**：`Node` 抽象（`print(int depth)`）+ `File{name}` 叶子（打印缩进的 `- name`）+ `Dir{name}` 组合（持有 `vector<unique_ptr<Node>>`，`add` 收 `unique_ptr`，`print` 里先打印自己再递归孩子）。搭一棵「root 里有 a.txt、src 子目录（main.cpp、util.cpp）、README」的树，`root.print(0)`，贴输出。回答：①「整棵树对外伪装成一个对象」体现在哪一行（调用方只调了谁）？②所有权为什么必须用 `unique_ptr`（GoF 原版裸指针的坑是什么）？③递归遍历的栈风险在什么极端场景会爆、怎么改？

[参考答案 →](./02-homework-solutions.md#hw-4-29-a)

### 4.29-B {#hw-4-29-b}

难度 **L3** · 涉及[组合模式](../vol4-generics-patterns/08-composite.md)

透明式 vs 安全式，两个都真跑。①透明式：`Graphic` 基类里声明 `virtual void add(unique_ptr<Graphic>)`，默认实现抛 `std::logic_error`；`Circle` 不 override，`main` 里对叶子调 `add` 并 catch——贴输出。②安全式：`add` 只存在于 `Group` 上，`Circle c; c.add(...)`——编译，贴真实报错。回答：①两种写法的代价各是什么（类型安全被推到了哪一步）？②无论选哪种，叶子的 `add` 为什么**绝不能**静默忽略（对比抛异常）？③什么场景选透明、什么场景选安全？

[参考答案 →](./02-homework-solutions.md#hw-4-29-b)

## 4.30 外观模式

### 4.30-A {#hw-4-30-a}

难度 **L2** · 涉及[外观模式](../vol4-generics-patterns/09-facade.md)

把教材的家庭影院换成**咖啡机**：三个子系统 `Heater`/`Pump`/`Lamp`（都实现 `Device` 的 `on()/off()`，各自打印一行），门面 `CoffeeMachine` 构造时按顺序装配进 `vector<shared_ptr<Device>>`，`brew()` 正序全开 + 打印 `brewing...`，`shutdown()` **逆序**全关。跑通贴输出，说明逆序关机的道理（后开的先关，防止谁先死谁悬空）。回答：①外观的本质一句话（不是发明新能力，而是什么）？②门面里不应该出现哪类代码（举「预热 30 秒」为例，它该是谁的职责）？③门面退化成 God Object 的判据是什么？

[参考答案 →](./02-homework-solutions.md#hw-4-30-a)

### 4.30-B {#hw-4-30-b}

难度 **L3** · 涉及[外观模式](../vol4-generics-patterns/09-facade.md)、[访问者模式](../vol4-generics-patterns/16-visitor.md)

把 4.30-A 的门面换成 `std::variant` 版：三个子系统**不继承任何基类**（普通 struct，各带 `on()/off()`），`using Part = variant<Heater2, Pump2, Lamp2>;`，`Machine2` 里 `vector<Part>` 用 `emplace_back(std::in_place_type<...>)` 装配，`all_on()` 用 `std::visit` 编译期分派。跑通贴输出。回答：①variant 版相对多态版省了什么（两样）、代价是什么（什么在编译期被钉死）？②「子系统种类运行时动态扩展」的场景该退回哪条路？

[参考答案 →](./02-homework-solutions.md#hw-4-30-b)

## 4.31 享元模式

### 4.31-A {#hw-4-31-a}

难度 **L2** · 涉及[享元模式](../vol4-generics-patterns/10-flyweight.md)

写 `Glyph` 享元工厂：`get(content)` 走 find-or-insert，池子是 `unordered_map<string, shared_ptr<Glyph>>`。取两次 `"你"`、一次 `"好"`，打印两次 `"你"` 的裸指针是否相等、`"你"` 与 `"好"` 是否不等、池子大小。贴输出。回答：①内部状态和外部状态各是什么（本题里外部状态在哪）？②为什么用 `shared_ptr` 而不是裸指针（GoF 原版的问题）或 `weak_ptr`（省构造收益被谁抹掉了）？③享元的甜区是什么——什么样的对象不该上享元（举 1 字节 char 的例子）？

[参考答案 →](./02-homework-solutions.md#hw-4-31-a)

### 4.31-B {#hw-4-31-b}

难度 **L4** · 涉及[享元模式](../vol4-generics-patterns/10-flyweight.md)、[单例模式](../vol4-generics-patterns/01-singleton.md)

并发竞态实证：`Glyph` 构造函数里 `++kConstruct` 并 `sleep 50us`（放大竞态窗口），64 个线程同时向**无锁**的 find-or-insert 工厂要同一个 `"你"`——多跑几次，贴真实输出（本机实测构造次数是 6，理想是 1）。然后加一把 `mutex` 把整个 find-or-insert 包起来重跑，贴「构造次数 = 1」。回答：①这个竞态叫什么（检查与使用之间的窗口）？②为什么「池子终态大小还是 1」会骗人——重复构造的代价发生在哪？③为什么不用「锁外先无锁 find 一次」的优化（对 `unordered_map` 这么干是什么）？④享元构造通常是重操作（加载纹理/解析配置），重复构造几遍意味着什么？

[参考答案 →](./02-homework-solutions.md#hw-4-31-b)

## 4.32 代理模式

### 4.32-A {#hw-4-32-a}

难度 **L2** · 涉及[代理模式](../vol4-generics-patterns/11-proxy.md)

虚拟代理懒加载：`RealImage` 构造时打印 `Loading ... (expensive)` 并 `++load_count`（`static inline atomic<int>`），`ImageProxy` 只存文件名，第一次 `display()` 才 `make_unique<RealImage>`。`main` 里打印构造前的 `load_count`、`display()` 两次、打印最终计数。贴输出。回答：①代理接口为什么必须和真实对象**同形**（对调用方透明的前提）？②单线程版 `if (!real_)` 在多线程下是什么问题（说清数据竞争）？正确姿势用什么（说一个标准工具名）？

[参考答案 →](./02-homework-solutions.md#hw-4-32-a)

### 4.32-B {#hw-4-32-b}

难度 **L3** · 涉及[代理模式](../vol4-generics-patterns/11-proxy.md)

两道实验。①**use_count 竞态**：一个线程反复拷贝/释放同一个 `shared_ptr`（制造引用计数抖动），另一个线程反复读 `use_count() > 1` 计数——跑完贴真实数字（本机单次观测约三万多，数量级随循环结构浮动、现象本身稳定），说清「`use_count() == 1` 当 COW 判据」为什么是 TOCTOU 竞态、`shared_ptr` 的原子引用计数为什么堵不住它。②**`shared_ptr::unique()` 状态实测**：写 `p.unique()` 分别用 `-std=c++20` 和 `-std=c++23` 编译，**如实贴结果**（本机 libstdc++ 16 把它当扩展保留、能编译通过），并指出：按标准它 C++17 起弃用、C++20 起移除——本机能过不代表可移植，换实现或未来版本就会编译失败，所以现代代码别写它。

[参考答案 →](./02-homework-solutions.md#hw-4-32-b)

## 4.33 策略模式

### 4.33-A {#hw-4-33-a}

难度 **L2** · 涉及[策略模式](../vol4-generics-patterns/12-strategy.md)

动态策略：`IFormatter` 抽象（`format(string) -> string`）+ `Upper`/`Lower` 两个策略，`Processor` 持 `unique_ptr<IFormatter>`，`set()` 运行期换策略。先大写 `"hello"` 再换成小写处理 `"HELLO"`，贴输出。回答：①「运行时切换」体现在哪一行？②动态策略的代价藏在哪个符号里（一次调用要经过什么）？③策略模式相对「if/else switch」的隐藏红利是什么（可测试性怎么说）？

[参考答案 →](./02-homework-solutions.md#hw-4-33-a)

### 4.33-B {#hw-4-33-b}

难度 **L4** · 涉及[策略模式](../vol4-generics-patterns/12-strategy.md)、[Concepts:把模板约束写进签名](../vol3-metaprogramming-cpp20-23/01-concepts.md)

编译期策略 + concept 契约：`Formatter` 概念要求 `F::format(std::string) -> std::string`（返回类型约束必须写成 `-> std::same_as<std::string>`），`Processor2<Formatter F>` 模板类；`UpperPolicy`（静态 `format` 返回大写串）正常跑通贴输出。然后写一个 `BadPolicy`（`format` 返回 `const char*`），实例化 `Processor2<BadPolicy>`——编译，贴真实报错，说明报错为什么点到了**调用点**、点名了哪个要求没满足。（坑位预警：若把返回类型约束写成 `-> std::convertible_to<std::string>`，`const char*` 能隐式转成 `string`、编译直接通过，本题要的报错根本不会出现。）回答：①模板策略零开销的证据（相比虚函数省了什么）？②模板策略的硬伤是什么（编译期定死之后运行期还能换吗）？③concept 在这里把「策略该长什么样」从什么提升成了什么？

[参考答案 →](./02-homework-solutions.md#hw-4-33-b)

## 4.34 命令模式

### 4.34-A {#hw-4-34-a}

难度 **L2** · 涉及[命令模式](../vol4-generics-patterns/13-command.md)

把教材的文本编辑器换成计数器：`Counter` 有 `add/sub/value`，`Command` 抽象带 `execute()/undo()`，`AddCommand{Counter&, int}` 执行加、撤销减，`UndoStack::execute` 先执行再压栈、`undo` 弹栈顶反操作。连做 +5、+3，打印，撤销两次，各打印。贴输出。回答：①`execute()` 为什么**不能**标 `const`（命令对象要记什么）？②命令模式把「动作」从什么提升成了什么（有身份、有状态、可存储——这带来哪三件事）？③命令持有接收者引用，生命周期上有什么铁律？

[参考答案 →](./02-homework-solutions.md#hw-4-34-a)

### 4.34-B {#hw-4-34-b}

难度 **L3** · 涉及[命令模式](../vol4-generics-patterns/13-command.md)、[组合模式](../vol4-generics-patterns/08-composite.md)

两道题。①宏命令：`MacroCommand` 内部 `vector<unique_ptr<Command>>`，`execute` 正序、`undo` **逆序**；包三个 `AddCommand(1/2/3)`，执行后值 6、撤销后回 0，贴输出。说清 undo 为什么必须逆序（栈的哪条性质）、正序 undo 会出什么怪事。②函数式命令（C++23）：`FuncStack::execute(std::move_only_function<void()> do_it, std::move_only_function<void()> undo_it)`——注意是 `std::move_only_function`（`<functional>`，**-std=c++23**）。用两个 lambda 实现「append "World" / 砍掉末尾 5 个字符」，执行和撤销各贴输出。为什么这里用 `move_only_function` 而不是 `std::function`（命令的什么语义是 move-only 的）？

[参考答案 →](./02-homework-solutions.md#hw-4-34-b)

## 4.35 状态机模式

### 4.35-A {#hw-4-35-a}

难度 **L2** · 涉及[状态机模式](../vol4-generics-patterns/14-state.md)、[访问者模式](../vol4-generics-patterns/16-visitor.md)

把教材的播放器换成红绿灯：`Red/Green/Yellow` 三个空 struct，`State = variant<...>`，访问者 `Tick` 提供三个 `operator()` 返回下一状态并打印转换。从 `Red` 起 tick 四次，贴输出（应走出 Red→Green→Yellow→Red→Green 的环）。回答：①`variant + visit` 相对 `enum + switch` 多了哪条编译期强保证（漏一个状态会怎样）？②它相对 `shared_ptr` 版 State 模式省了什么？③状态带数据的能力体现在哪（举一个给 `Yellow` 加剩余秒数的场景）？

[参考答案 →](./02-homework-solutions.md#hw-4-35-a)

### 4.35-B {#hw-4-35-b}

难度 **L4** · 涉及[状态机模式](../vol4-generics-patterns/14-state.md)

穷举检查实证：`V = variant<A, B, C>`，`std::visit` 的访问者（`Overloaded` 那套）**故意只写 A、B 两支、漏掉 C**——编译，贴真实报错的关键行（报错在 `<variant>` 深处，注意它点明了哪个类型没有被覆盖；报错很长，节选关键帧并注明截断）。回答：①为什么这个检查是编译期的、绝不可能漏到运行时？②「想加默认兜底分支」用什么（泛型 lambda 的 `operator()` 是什么）？③对比教材里「经典访问者漏写 visit 让类变抽象」的报错体验，variant 版好在哪里？

[参考答案 →](./02-homework-solutions.md#hw-4-35-b)

## 4.36 备忘录模式

### 4.36-A {#hw-4-36-a}

难度 **L2** · 涉及[备忘录模式](../vol4-generics-patterns/15-memento.md)

黑盒备忘录：`Editor` 里嵌套 `class Memento`（私有 `content_`、私有构造、`friend class Editor`，public 区只有默认构造），`Editor` 提供 `type/snapshot/restore/content`。正常流程：打 "Hello" → 快照 → 打 ", world" → 恢复，贴输出。然后**故意从外部读** `snap->content_`——编译，贴真实报错（`is private within this context`）。回答：①GoF 的「宽接口 / 窄接口」二分在这里对应什么（谁拿宽、谁拿窄）？②为什么「快照不可被外部篡改」是备忘录的核心承诺而不是洁癖？③创建快照为什么必须 `shared_ptr<Memento>(new Memento(...))` 而不能 `make_shared`——下一题实证。

[参考答案 →](./02-homework-solutions.md#hw-4-36-a)

### 4.36-B {#hw-4-36-b}

难度 **L3** · 涉及[备忘录模式](../vol4-generics-patterns/15-memento.md)

两道题。①把 `snapshot()` 里的创建改成 `std::make_shared<Memento>(content_)`——编译，贴真实报错的关键行（注意报错发生在标准库的 `construct`/`allocator_traits` 路径上），说清为什么 `make_shared` 的构造调用**不在** `friend class Editor` 的白名单里。②撤销重做历史栈：`History` 持 `vector<shared_ptr<Memento>>` + 游标，`push/undo/redo`，且 push 时**丢弃 redo 分支**。跑一段「打 Hello → 快照 → 打 , world → 快照 → undo → redo → 打 !!! → 快照」的会话，贴输出，验证 `can_redo` 归零、`can_undo` 保持。说清「在非末尾插入新快照时丢弃之后的分支」为什么是编辑器的正确行为。

[参考答案 →](./02-homework-solutions.md#hw-4-36-b)

## 4.37 访问者模式

### 4.37-A {#hw-4-37-a}

难度 **L2** · 涉及[访问者模式](../vol4-generics-patterns/16-visitor.md)

`variant + visit` 版：`Circle{r}`/`Rect{w,h}` 两个普通 struct（零侵入），`Shape = variant<...>`，用 C++17 的 `Overloaded` helper 把两个 lambda 捏成访问者，对 `{Circle{2.0}, Rect{3.0,4.0}}` 求总面积。贴输出。回答：①`std::visit` 的分发机制是什么（判别式比较/跳转，没有哪样东西）？②`Overloaded` 依赖 C++17 的哪两个特性（`using Ts::operator()...` 和推导指引各是什么）？③类型集合必须「闭合」是什么意思——运行时想动态加一种形状该退哪条路？

[参考答案 →](./02-homework-solutions.md#hw-4-37-a)

### 4.37-B {#hw-4-37-b}

难度 **L4** · 涉及[访问者模式](../vol4-generics-patterns/16-visitor.md)

两道题。①经典双分发：`ShapeVisitor`（纯虚 `visit(Circle&)`/`visit(Rect&)`）+ `Shape`（纯虚 `accept`）+ 两个形状各自 override `accept`（里面 `v.visit(*this)`）+ `PerimeterVisitor` 累加周长。对 `{Circle(2), Rect(3,4)}` 求总周长，贴输出。②把 `accept` 的实现**提到基类**写一次（`virtual void accept(Visitor& v) const { v.visit(*this); }`，`Visitor` 只有派生类的 visit 重载）——编译，贴真实报错，用「`*this` 的静态类型在基类里是谁、重载决议为什么找不到匹配」解释；并说清「accept 必须在每个派生类各自 override」的根本目的（修正 `*this` 的静态类型，而不是多态本身）。

[参考答案 →](./02-homework-solutions.md#hw-4-37-b)

## 4.38 观察者模式

### 4.38-A {#hw-4-38-a}

难度 **L2** · 涉及[观察者模式](../vol4-generics-patterns/17-observer.md)

weak_ptr 防悬挂：`Subject` 持 `vector<weak_ptr<Observer>>`，`subscribe` 收 `shared_ptr`（隐式转 weak），`notify` 里 `it->lock()` 成功才回调、失败就 `erase`。内层作用域里建一个 `Loud` 观察者、订阅、通知一次；出了作用域再通知一次——贴输出（第二次无输出、无崩溃，打印 `done (no crash)`）。回答：①`weak_ptr` 的语义为什么恰好是观察者的正确所有权模型（不拥有、但知道）？②对比 `shared_ptr` 持有会养出什么（僵尸观察者）？③`lock()` 是原子操作，它给的硬保证是什么？

[参考答案 →](./02-homework-solutions.md#hw-4-38-a)

### 4.38-B {#hw-4-38-b}

难度 **L4** · 涉及[观察者模式](../vol4-generics-patterns/17-observer.md)

两道实验。①**裸指针悬垂的 ASan 实证**：`Subject` 持 `Observer*` 数组，栈上 `Loud obs` 订阅后离开作用域，再 `notify`——用 `-fsanitize=address` 构建运行，贴真实报告（`stack-use-after-scope` + 精确到行列），说清普通构建下它为什么可能「看起来没事」。②**snapshot 通知**：`Subject2` 持 `vector<std::function<void(int)>>`，`notify` 先把回调列表**拷贝一份**再在副本上遍历。两个订阅者各打印一行，`total hits == 2`，贴输出。回答：为什么「在回调里增删订阅」会炸掉原地遍历（迭代器失效），而 snapshot 能免疫？它的代价是什么？

[参考答案 →](./02-homework-solutions.md#hw-4-38-b)

## 4.39 迭代器模式

### 4.39-A {#hw-4-39-a}

难度 **L2** · 涉及[迭代器模式](../vol4-generics-patterns/18-iterator.md)

实现二叉树的**中序外部迭代器**：`InorderIterator` 内部用 `stack<Node*>`，构造时 `push_left(root)` 一路压左链，`operator++` 弹栈后转右子树再压左链，默认构造的迭代器当 `end()`（栈空 == 栈空判等）。`BinaryTree` 提供 `begin()/end()`。对一棵 1..7 的满二叉树 range-for，贴输出。回答：①「pop 之后看右子树」为什么恰好是中序遍历的栈模拟？②迭代器模式和「先压平再遍历」相比，把什么从调用方手里解放了出来（惰性/提前终止）？③标准库算法 `find_if`/`count_if` 为什么能免费套在这个自定义树上？

[参考答案 →](./02-homework-solutions.md#hw-4-39-a)

### 4.39-B {#hw-4-39-b}

难度 **L4** · 涉及[迭代器模式](../vol4-generics-patterns/18-iterator.md)、[使用 Concepts 约束模板](../vol3-metaprogramming-cpp20-23/02-constraining-templates.md)

C++20 概念达标实证：写一个只有前缀 `++` 的计数器迭代器（无关联类型），`static_assert`/打印 `std::weakly_incrementable` 和 `std::input_iterator`——贴输出（两个都是 `false`）。然后补上：`void operator++(int)` 后缀、`value_type/difference_type/iterator_concept/iterator_category` 四个关联类型、默认构造，再打印两个概念（变 `true`），并把它的 range 接上 `views::filter` 管道遍历。贴全部输出。回答：①range-for 能过、概念却不过——说明 range-for 是什么（语法糖还是概念检查）？②`weakly_incrementable` 为什么非要后缀 `++`（ranges 内部实现按什么前提写）？③`iterator_concept` 和老的 `iterator_category` 什么关系？

[参考答案 →](./02-homework-solutions.md#hw-4-39-b)

## 4.40 责任链模式

### 4.40-A {#hw-4-40-a}

难度 **L2** · 涉及[责任链模式](../vol4-generics-patterns/19-chain-of-responsibility.md)

指针链：`Handler` 基类把**非虚的 `handle`**（处理或转发、到链尾报 `[chain end] nobody handled`）焊死，子类只实现纯虚 `process`（命中返回 true）。`AuthHandler`（认 `"auth"`）和 `CacheHandler`（认 `"cache"`）串成链，依次处理 `"cache"`、`"auth"`、`"x"`，贴输出。回答：①`handle` 和 `process` 拆开是什么设计意图（哪个是模板方法、它焊死了什么骨架、防了什么漏）？②指针链的耦合搬到了哪里（三条工程痛点）？③节点多、链要动态拼装时该换哪条路？

[参考答案 →](./02-homework-solutions.md#hw-4-40-a)

### 4.40-B {#hw-4-40-b}

难度 **L3** · 涉及[责任链模式](../vol4-generics-patterns/19-chain-of-responsibility.md)

中间件洋葱：`MiddlewareChain` 持 `vector<std::function<void(MiddlewareChain&)>>` + `index_` 游标，`use()` 注册、`next()` 推进。链一：auth（前后各打一行）、log（前后各打一行）、final（不调 next）——**先预测**六行输出的顺序再真跑贴结果，说清「洋葱」怎么靠「调 next 前干前置、next 返回后干后置」实现。链二：认证拒绝的中间件**不调 `c.next()`**，后面的中间件打印一句「不该出现」——贴输出（应只出现拒绝那行），说清短路为什么干净到「什么都不做就实现」。最后答：`index_` 游标的一次性暗坑是什么（复用链会怎样）？

[参考答案 →](./02-homework-solutions.md#hw-4-40-b)

## 4.41 解释器模式

### 4.41-A {#hw-4-41-a}

难度 **L2** · 涉及[解释器模式](../vol4-generics-patterns/20-interpreter.md)

AST 求值（不写解析器，先手搭树）：`Node` 抽象（`evaluate()`）+ `Num{long long}` 叶子 + `Bin{op, unique_ptr 左右子树}`。手搭一棵 $(1+2)\times 3-4$ 的树（四层嵌套），求值贴输出（应得 5）。回答：①为什么 AST 的所有权用 `unique_ptr` 最自然（对应树的什么结构、析构怎么传播）？②「解释器模式的命门是统一接口」——`evaluate()` 这个统一接口给调用方带来了什么（组合模式思想的哪条应用）？③`Bin::evaluate` 的递归形态把「带优先级的求值」拆成了什么（每层只做什么）？

[参考答案 →](./02-homework-solutions.md#hw-4-41-a)

### 4.41-B {#hw-4-41-b}

难度 **L4** · 涉及[解释器模式](../vol4-generics-patterns/20-interpreter.md)

把教材的递归下降计算器**扩展一个 `%` 运算符**（与 `*`、`/` 同级：`term := factor (('*'|'/'|'%') factor)*`）。完整实现三层文法 `expression/term/factor` + 数字解析（含一元负号）+ 括号递归 + 错误抛出。测试 `"1+2*3"`、`"(1+2)*3"`、`"10 - 4 / 2"`、`"7 % 3"`、`"10 % 4 + 1"`、`"(2+3)*(4-1)"`、`"1/0"`（除零错误），贴全部输出。回答：①优先级为什么靠「谁调用谁」的层次自然表达（加减在最外层意味着什么）？②左结合性为什么靠 `while` 循环而不是朴素右递归（用 $1-2-3$ 说）？③`parse_number` 里的一元负号为什么严格说该单独立一层（教材承认的简化）？

[参考答案 →](./02-homework-solutions.md#hw-4-41-b)

## 4.42 中介者模式

### 4.42-A {#hw-4-42-a}

难度 **L2** · 涉及[中介者模式](../vol4-generics-patterns/21-mediator.md)

聊天室：`IMediator` 抽象（`send_message`/`broadcast`），`User` 只持 `IMediator*`（不知道任何别的 User），`ChatRoom` 持 `unordered_map<string, shared_ptr<User>>` 实现路由。会话：Alice 私聊 Bob、Bob 广播、Carol 私聊不存在的 Dave，贴输出。回答：①「星形耦合」体现在哪（User 之间的耦合被搬到了谁身上）？②抽象中介者接口为什么**绝不能**反过来 include 所有同事类（否则会怎样）？③中介者最大的反噬是什么，三条缓解思路各是什么？

[参考答案 →](./02-homework-solutions.md#hw-4-42-a)

### 4.42-B {#hw-4-42-b}

难度 **L3** · 涉及[中介者模式](../vol4-generics-patterns/21-mediator.md)

事件总线：`EventBus` 用 `std::any` 类型擦除 + `std::type_index` 分桶，`subscribe<Event>(handler)` 在内部把强类型 handler 包成吃 `any` 的统一签名，`publish<Event>` 查表分发。定义 `MsgSent`/`UserLogin` 两个事件，给 `MsgSent` 挂两个订阅者（logger + 计数器）、`UserLogin` 挂一个，发布三次，贴输出（计数器应为 2）。然后演示坏味道：`std::any` 存 `MsgSent` 按 `UserLogin` 取——贴 `bad_any_cast` 的 caught 输出。回答：①类型擦除发生在谁内部、发布订阅两端看到的是强类型还是擦除类型？②「新增事件类型不改中介者一行代码」为什么成立（协议从什么降级成了什么）？③代价是什么（类型检查推迟到哪一步）？④发布无人订阅的事件会怎样？

[参考答案 →](./02-homework-solutions.md#hw-4-42-b)

## 4.C 跨章综合与挑战

### 4.C-1 {#hw-4-c-1}

难度 **L3** · 涉及[使用 Concepts 约束模板](../vol3-metaprogramming-cpp20-23/02-constraining-templates.md)、[C++20 范围库基础与视图](../vol2-modern-cpp17/07-ranges-basics-and-views.md)、[迭代器模式](../vol4-generics-patterns/18-iterator.md)

综合题：写一个 `IntegralRange` 概念（`input_range` + `value_type` 是整型）约束的泛型 `mean` 函数（返回 `double` 均值），然后让**三种完全不同的容器**都喂给同一个 `mean`：`std::vector<int>`、`std::array<int, 4>`、以及一个自定义 `Squares` range（迭代器 `operator*` 返回平方，范围 `{1,5}` 即 1²..4²）。贴三组输出（应为 4、25、7.5）。设计要点：①`mean` 里为什么用 `double sum` 累计、`n` 用 `size_t`（整数除法坑在哪）？②`Squares` 的迭代器要满足 `input_range` 至少得配齐哪几样（关联类型、前后缀 `++`、`==`、默认构造）？③这道题把本卷哪些章焊在了一起（concepts、ranges、迭代器各起了什么作用）？

[参考答案 →](./02-homework-solutions.md#hw-4-c-1)

### 4.C-2 {#hw-4-c-2}

难度 **L4** · 涉及[使用 Concepts 约束模板](../vol3-metaprogramming-cpp20-23/02-constraining-templates.md)、[三路比较运算符](../05-spaceship-operator.md)、[模板友元与 Barton-Nackman](../vol1-basics-cpp11-14/07-friends-and-barton-nackman.md)、[综合项目:fixed_vector](../vol1-basics-cpp11-14/10-fixed-vector.md)、[CRTP](../vol1-basics-cpp11-14/09-crtp.md)

泛型设计综合：写一个 concept 约束的 `sorted_copy` 模板——`SortableRange` 要求 `input_range` 且 `range_value_t` 满足 `std::three_way_comparable`；实现把范围拷进 `vector` 后 `std::ranges::sort` 再返回。然后准备两个「来源完全不同」的可比较类型喂给它：①`Score`——通过 `ComparableMixin<Derived>`（CRTP + 隐藏友元 `operator<=>` 转发到派生类的 `cmp()`）获得比较能力；②`Version`——直接 `auto operator<=>(...) const = default;`。最后让**自定义容器** `FixedVector<T, N>`（裸指针迭代器）也进 `sorted_copy`。贴两组排序输出（scores 升序、versions 升序）。设计要点：①`three_way_comparable` 概念替你检查了哪些一致性？②mixin 版的 `<=>` 和 default 版的 `<=>` 在「谁提供比较逻辑」上的差别？③为什么 `FixedVector` 一行不用改就能被 `sorted_copy` 用（它的什么成员让 `input_range` 成立）？

[参考答案 →](./02-homework-solutions.md#hw-4-c-2)

### 4.C-3 {#hw-4-c-3}

难度 **L5** · 涉及[模板导论](../vol1-basics-cpp11-14/01-templates-introduction.md)、[模板特化与偏特化](../vol1-basics-cpp11-14/04-specialization-partial.md)、[非类型模板参数](../vol1-basics-cpp11-14/05-non-type-parameters.md)

挑战题（模板元编程实现编译期排序——typelist 技术源自 Andrei Alexandrescu《Modern C++ Design》第 3 章与 Boost.MPL 的思路；L5 档位口径见[练习总览](../exercises/index.md)）。**只用编译期手段**：定义 `Nil` 和 `Cons<H, T>` 类型列表，实现五个元函数——`Length`、`MaxSize`（按 `sizeof` 求最大）、`MaxType`（挑出最大元素类型）、`RemoveFirstOfSize`、`SortBySize`（选择排序：每轮挑最大放最前，降序按 `sizeof`）；外加 `Reverse`（尾递归 + 累加器）。用 `L = Cons<char, Cons<int, Cons<char, Cons<double, Cons<short, Nil>>>>>` 验证：`static_assert` 断言长度 5、最大元素是 `double`、排序后头三个依次是 `double/int/short`、反转后头是 `short`；运行期打印原始与排序后的 `sizeof` 序列（`1 4 1 8 2` → `8 4 2 1 1`）。贴出完整输出。附加思考：`SortBySize` 是选择排序——它的编译期时间复杂度是多少？如果列表里有两个同 `sizeof` 的元素（两个 `char`），`RemoveFirstOfSize` 每轮只删一个，排序结果稳定吗（同尺寸元素的相对顺序会怎样）？先推理，再用你的输出验证。

[参考答案 →](./02-homework-solutions.md#hw-4-c-3)
