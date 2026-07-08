---
chapter: 0
cpp_standard:
- 11
- 17
- 20
description: 讲清静态存储期、三种静态初始化、静态初始化顺序灾难(SIOF)、析构顺序问题、
  magic statics 与 constinit,为 NoDestructor 打底
difficulty: intermediate
order: 0
platform: host
prerequisites:
- WeakPtr 前置知识（零）：弱引用与生命周期难题
reading_time_minutes: 11
related:
- NoDestructor 实战（一）：动机与接口设计
- NoDestructor 前置知识（一）：placement new 与对齐存储
tags:
- host
- cpp-modern
- intermediate
- 内存管理
- RAII
title: "NoDestructor 前置知识（零）：静态存储期、初始化与析构"
---
# NoDestructor 前置知识（零）：静态存储期、初始化与析构

您在程序里随手写一行 `std::map<int, Config> global_table = load();`,大概觉得没什么——map 启动时构造,退出时析构,天经地义。可 Chromium 的 `//base` 风格指南明令禁止全局构造函数和析构函数。笔者第一次看到这条规则也愣了一下:这么自然的写法,凭什么不让用?

愣完去翻标准,才发现这一条禁令背后压着一串 C++ 老坑——静态存储期对象有个奇怪的三段式初始化、跨翻译单元的初始化顺序根本不受您控制(这就是 SIOF)、析构顺序同样失控(关停竞态)。咱们这一篇就专门把这些地基翻出来晒晒,把每一块都讲透。等您真看明白这些,Chromium 为什么下这种禁令、以及接下来几篇 `NoDestructor` 到底在解决什么,自然就清楚了。

---

## 静态存储期

C++ 把对象按**存储期(storage duration)**分类,storage duration 决定一个对象什么时候生、什么时候灭。咱们日常打交道的主要是三种。局部变量进函数创建、出函数销毁,这是自动存储期,栈上的常客。`new` 出来的对象归您手动管,创建销毁全凭您一句话,这是动态存储期。还有一种是这一篇的主角——静态存储期,全局变量和 `static` 变量都归这一类,程序启动时创建、程序退出时销毁,生命周期跟整个程序一样长。

`NoDestructor` 服务的对象就是这一类。您写一句 `static std::string s = "...";` 或一个全局 `std::map g_table;`,里面的 `std::string`、`std::map` 都是静态存储期,活得跟程序一样久。问题是,静态存储期的对象有它自己一套初始化和析构规则,跟普通局部变量不一样——所有麻烦就是从这套规则里长出来的。

---

## 三段式静态初始化

静态存储期对象的初始化,标准给它分了三个阶段。先说前两个,这俩是乖孩子,不惹事。

第一段是零初始化(zero initialization),就是把这块内存整个清零。内置类型和有零初始化语义的类,走到这一步初始化就算完了。第二段是常量初始化(constant initialization),如果初始化表达式是个编译期常量——比如您写了个 `constexpr`——那编译期就把它做完,这一段也属于"静态初始化",不产生任何运行时代码。这两段合起来就是"静态的",编译期完成,零开销。

真正的麻烦在第三段——动态初始化(dynamic initialization)。如果初始化得算一算才知道结果,比如 `static std::string s = "x";` 得调 `std::string` 的构造函数、`static int n = rand();` 得在运行期调 `rand()`,编译器就只能把这件事推迟到运行期去办。这一段是"动态的",有运行期开销。咱们下面要讲的坑,根全扎在这一段上。

```cpp
// 静态初始化(编译期,无开销):
constexpr int kMax = 100;            // 常量初始化
static int zero;                      // 零初始化

// 动态初始化(运行期,要执行代码):
std::string g_name = "chromium";      // 要调 std::string 构造函数
static int g_seed = rand();           // 要调 rand()
std::map<int,int> g_table;            // 要调 std::map 构造函数
```

每一个走动态初始化的全局或静态变量,编译器都得额外给它生成一段"程序启动时调它构造函数"的代码,塞进 `.init_array` 段里。这段代码有个名号——全局构造器(global constructor),`main` 之前由运行时统一拉着跑一遍。

---

## 静态初始化顺序灾难(SIOF)

全局构造器的执行顺序,在同一个翻译单元(同一个 .cpp)内部是按声明顺序来的——这条您能掌控。可一旦跨了翻译单元,顺序就变成了未指定的(unspecified),编译器爱怎么排怎么排。

这一下就翻出了 C++ 里那个老得掉渣的坑——Static Initialization Order Fiasco,简称 SIOF。看个最朴素的例子:

```cpp
// a.cpp
extern int b_value;
int a_value = b_value + 1;     // 动态初始化,依赖 b_value

// b.cpp
int b_value = std::rand();     // 动态初始化(rand 非 constexpr,运行期才求值)
```

笔者刚见到这例子的时候没觉得有什么,跑一遍才发现玄机。如果 `a.cpp` 的 `a_value` 抢在前面先初始化,它去读 `b_value` 时,`b_value` 还没轮上动态初始化,只有零初始化的 0,于是 `a_value` 算出来是个 1,而不是 `b_value` 真正求值后该有的结果。这里有个分寸您得记牢:必须是动态初始化才会撞上 SIOF——常量初始化(像 `int b = 42;` 这种)在编译期就办完了,永远排在动态初始化前面,跟 SIOF 无缘。可一旦两个跨 .cpp 的全局对象互相依赖、又都是动态初始化,顺序就完全不受控,结果是未定义行为。这种 bug 折磨人的地方在于它极难复现——您换台机器、换个编译选项,顺序就变了,本地跑得好好的,CI 上偶发抽风。

标准给 SIOF 配的解法叫"首次使用时构造(construct on first use)":把那个全局变量塞进函数里做成局部静态,等函数头一次被调用时才动手初始化。

```cpp
int& a_value() {
    static int v = b_value() + 1;   // 函数局部静态,首次调用时初始化
    return v;
}
int& b_value() {
    static int v = std::rand();     // b_value 也改成函数局部静态
    return v;
}
```

这么一改,`a_value()` 第一次被调到的时候,它自己主动去调 `b_value()`——这一调,`b_value()` 才开始构造。顺序这下由您写的代码决定,不再是编译器拍板。这就是 NoDestructor 推荐用函数局部静态的根本原因,它从根上绕开了 SIOF。

---

## 析构顺序问题(关停竞态)

初始化有顺序的烂账,析构也有一份。静态存储期对象在程序退出时(从 `main` 返回后、`exit` 时)被析构,顺序是初始化顺序的逆序。听上去挺优雅,可问题就藏在"逆序"两个字背后——初始化顺序本来就不受您控制,逆序自然也跟着失控。

举个最常见的情形:某个全局对象在析构时,正好依赖另一个已经被析构掉的对象。比如一个全局 logger 持有一个全局 string 的引用,关停时 string 抢先析构了,轮到 logger 析构再去碰那个引用,直接 UAF。还有更阴的雷埋在析构函数内部——在里头调 `exit` 会跳过其余静态对象的析构,跨线程或嵌套调用场景下是 UB;在里头抛异常,赶上栈展开就会触发 `std::terminate`。关停路径上每一颗都够您喝一壶。

这套毛病统称关停竞态(shutdown race)。Chromium 是个浏览器,关停路径本来就乱:多进程、多线程,任务队列可能还在排空,全局对象的析构竞态在它的 bug 追踪器里是反复出现的老客。

Chromium 给出的解法简单粗暴:干脆不让全局对象析构。这就是 `NoDestructor` 的核心思路。它让对象活得跟程序一样久,但程序结束时不去析构它,代价是进程退出时由操作系统统一回收内存——这件事操作系统本来就会做,白捡。一个手工放弃析构,换掉整类析构顺序的麻烦,这笔账 Chromium 觉得值。

---

## magic statics:C++11 的线程安全保证

刚才那个"函数局部静态"的解法,有个前提您可能没细想:多线程同时头一次调进这个函数,初始化得是安全的吧?C++11 之前这事儿其实没保证,是 C++11 起标准才给它兜了底,江湖人称 magic statics。标准的原话大致是:

> 如果控制流并发地经过一个未初始化的函数局部静态变量的声明,其它线程会**等待**正在进行的初始化完成。

翻译成代码就是这种写法您可以放心用:

```cpp
const std::string& GetDefault() {
    static const std::string s = "default";   // 线程安全:多线程首次并发调用,只有一个会初始化
    return s;
}
```

多少个线程一块儿调 `GetDefault()` 都行,`s` 只会被构造一次,中间不会有数据竞争。这是 C++11 白纸黑字给的保证(GCC/Clang 在底下用 `__cxa_guard_acquire` 给您实现)。笔者要在这儿点一句:NoDestructor 之所以能稳稳当当用作单例,根就扎在 magic statics 上——它自己可没给您加锁,是语言在底下兜的底。这一点想透了,后面看 NoDestructor 的实现就不会被"它怎么不加锁"这种问题绊住。

---

## constinit(C++20):保证零运行时初始化

C++20 给了咱们一个新工具——`constinit`。这玩意儿干的事就一句话:它向您和编译器保证,这个变量的初始化一定是常量初始化、在编译期做完,**绝对不会生成动态初始化代码**。

```cpp
constinit int x = 42;             // OK:常量初始化
constinit int y = compute();      // 编译错:compute() 不是常量表达式 → 拒绝
```

笔者觉得这关键字设计得挺干脆——它不是建议,是断言:您写得出就让您过,初始化表达式要不是常量,编译器当场拒绝,不会留个雷到运行期才炸。它的价值就在"强制保证这个全局变量不生成全局构造器"。对那些 constinit-可构造的类型(比如 `constexpr` 构造的 POD),您直接 `constinit T x` 就两全其美了——既躲开了全局构造器,又用着裸类型,根本不用劳驾 NoDestructor。这恰恰是 NoDestructor 的 static_assert 推荐里那种"平凡情况":T 平凡可构造加平凡可析构,直接 constinit 上场,再套一层 NoDestructor 反而是多此一举。

---

## Chromium 为何禁全局 ctor/dtor

把上面这几块拼起来,Chromium 禁全局构造和析构的动机就摆在眼前了。最直白的一条是启动性能——`main` 之前得把所有 `.init_array` 跑一遍,大型项目里成千上万个全局对象排队构造,启动延迟肉眼可见。再叠加 SIOF 和析构竞态这俩跨翻译单元的老坑,加上浏览器关停路径本身复杂(多进程多线程搅一块),析构竞态尤其要命。三条加起来,Chromium 干脆一刀切。

可光下禁令没用,得有手段逼着大家守规矩。Chromium 搬出来的是 clang 的 `-Wglobal-constructors` 和 `-Wexit-time-destructors` 两个警告,配上 `-Werror` 一锁——您只要写出一个会生成全局 ctor/dtor 的全局对象,编译当场报错,连商量都不带商量的。`NoDestructor` 就是 Chromium 配这条规则一起发的官方逃生口,专门绕开它:

用函数局部静态避开全局构造器(首次使用时才构造,加上 magic statics 保证线程安全);再用 `NoDestructor` 避开全局析构器(压根不注册析构)。两个"避免"凑齐,既守了规则又拿到了全局可见的对象。

零件凑齐了,接下来就看 NoDestructor 具体怎么把这两个"避免"实现到代码里。下一块要补的零件是 placement new 与对齐存储——NoDestructor 实现的核心机制就靠它撑着。

## 参考资源

- [cppreference: storage duration(存储期)](https://en.cppreference.com/w/cpp/language/storage_duration)
- [cppreference: static initialization(静态初始化)](https://en.cppreference.com/w/cpp/language/initialization)
- [cppreference: constinit(C++20)](https://en.cppreference.com/w/cpp/language/constinit)
- [SIOF 经典解释 —— isocpp FAQ](https://isocpp.org/wiki/faq/ctors#static-init-order)
- [Chromium `base/no_destructor.h` 设计注释](https://source.chromium.org/chromium/chromium/src/+/main:base/no_destructor.h)
