---
title: "组合模式:把一棵树伪装成一个对象"
description: "从最原始的「手写一堆 getter/setter」开始,一步步逼出组合模式,讲清透明式与安全式的取舍,顺手用 std::array 和 std::invoke 把同类对象的聚合也一起解决"
chapter: 11
order: 8
tags:
  - host
  - cpp-modern
  - intermediate
  - 组合模式
difficulty: intermediate
platform: host
cpp_standard: [11, 17, 20]
reading_time_minutes: 20
related:
  - "单例模式:从注释约束到 Meyer's Singleton"
prerequisites:
  - "Chapter 6: 类与对象"
  - "Chapter 9: 智能指针与所有权"
---

# 组合模式:把一棵树伪装成一个对象

## 我们到底在解决什么问题

先别急着上定义。想一个游戏里最常见的场景:你给一个角色(`Creature`)设计了三项基本属性——力量(`strength`)、敏捷(`agility`)、智力(`intelligence`)。一开始你觉得没什么,这无非就是一个 struct 加几个 getter/setter:

```cpp
class Creature {
    int strength;
    int agility;
    int intelligence;

public:
    int get_strength() const { return strength; }
    void set_strength(int v) { strength = v; }
    // agility / intelligence 依样画葫芦……
};
```

写到这里其实你已经有点烦了——三个字段,六个方法,纯纯的体力活。但真正让你血压拉满的是接下来这件事:策划说,我们想分析一下这个角色的能力分布,看看总和、平均值、最出色的那一项是多少。于是你下意识写出这样的代码:

```cpp
int sum() const {
    return strength + agility + intelligence;
}

double avg() const {
    return sum() / 3.0;
}

int max() const {
    return std::max(std::max(strength, agility), intelligence);
}
```

看起来能跑,但你心里清楚这里埋着一颗雷:**哪天策划说"再加一条体质(constitution)属性"**,你就要把 `sum`、`avg`、`max` 全部翻一遍,手动把 `constitution` 加进每一处运算。字段少的时候尚能应付,一旦属性变成十个、二十个,你就只能满世界搜字段名、祈祷别漏。这就是问题的本质——**当一堆「本质上是同类的东西」被你拆成一堆命名各异的独立变量时,任何「对全体做一件事」的操作都得手写循环展开**。

组合模式(Composite)就是冲着这件事来的。GoF 给它的定义是这样的:**把对象组合成树形结构以表示「部分-整体」的层次结构,使得调用方对单个对象和组合对象的使用具有一致性**。这句话的关键不在「组合」两个字(它和 UML 里那个表示 has-a 的「组合关系」是两回事,别搞混),而在「一致性」——**调用方根本不应该关心它手上拿的是一个叶子,还是一棵装满叶子的子树**。

下面我们分两条路走。第一条路最贴近刚才那个 `Creature` 的痛点:**把同类的属性聚合成一个数组**,让标准库的算法直接能用上;第二条路才是 GoF 经典的树形 Composite:**用 Group 模拟一棵多叉树**,让叶子和组合对外长得一模一样。两条路背后是同一个动机,只是落点不同。

## 第一条路:把同类对象聚合成数组

我们回头看那个 `sum`/`avg`/`max` 的痛点。问题的根源不在算法,而在于 `strength`、`agility`、`intelligence` 是三个**散落的、彼此无关的**成员变量。但如果你换个角度想——在「分析能力分布」这件事的上下文里,它们**其实是同一个东西**:都是「一项能力的数值」。既然是同一个东西,我们完全可以在分析时把它们**逻辑上**聚合成一个数组,而不是物理上各自为政。

GoF 的书里也用到了这个例子,不过它把分析逻辑直接揉进了 `Creature` 本身。说实话我不太赞同这个写法——「分析能力」是调用方的事,不属于角色自己的职责,把它塞进 `Creature` 是职责越界。所以我们把分析仪拆出来,做成一个独立的 `CreatureAnalyzer`:

```cpp
#include <array>
#include <cstddef>

class Creature {
    int strength;
    int agility;
    int intelligence;

public:
    Creature(int s, int a, int i) : strength(s), agility(a), intelligence(i) {}
    int get_strength() const { return strength; }
    int get_agility() const { return agility; }
    int get_intelligence() const { return intelligence; }
};

class CreatureAnalyzer {
public:
    explicit CreatureAnalyzer(const Creature& c)
        : abilities_{c.get_strength(), c.get_agility(),
                     c.get_intelligence()} {}

    int sum() const;
    double avg() const;
    int max() const;

private:
    static constexpr std::size_t kAbilityCount = 3;
    std::array<int, kAbilityCount> abilities_;
};
```

这里有几个细节值得停下来想一想。第一,我们把三项能力在构造时一次性拷进了一个 `std::array`,这个数组就是「能力的集合」。从这一刻起,`strength`、`agility`、`intelligence` 不再是三个孤零零的变量,而是被我们**重新建模成一个整体**——这正是 Composite 的精神,只不过这次的「容器」是数组,而不是树。第二,能力数量 `kAbilityCount` 我刻意写成了一个独立的 `constexpr` 常量。这一点等会儿专门说,因为原书在这里踩了一个坑。

既然数据已经是个标准容器了,接下来 `sum`/`avg`/`max` 就完全是标准库的活了,一行手写循环都不需要:

```cpp
#include <algorithm>
#include <numeric>

int CreatureAnalyzer::sum() const {
    return std::accumulate(abilities_.begin(), abilities_.end(), 0);
}

double CreatureAnalyzer::avg() const {
    return sum() / static_cast<double>(kAbilityCount);
}

int CreatureAnalyzer::max() const {
    return *std::max_element(abilities_.begin(), abilities_.end());
}
```

你看,聚合之后,所有「对全体做一件事」的操作都自动变成了对数组的操作。现在策划再要加 `constitution`,改动只在构造函数里多塞一个值、`kAbilityCount` 改成 4,三个算法**一个字都不用动**。这就是把同类对象聚合成容器的回报。

::: warning 别用 enum 的最后一个值来当数量
原书在数能力个数时,用了这么个写法:`enum Abilities { strength, agility, intelligence };` 然后用 `static_cast<int>(intelligence) + 1` 当数组长度。这是个老 C 时代留下来的技巧——把「数量」塞进枚举的尾元素。但它有两个问题:一是**语义错误**,枚举的是「能力种类」,数量是另一回事,把数量伪装成一种能力会让读到这段代码的人一脸懵;二是**和 enum class 不兼容**,`enum class` 不允许隐式转成 `int`,你还得再 `static_cast`,代码越来越脏。现代 C++ 的做法就是上面这样——数量是数量,单写一个 `constexpr std::size_t kAbilityCount`,清清爽爽。永远别把「容器大小」这种元信息塞进「元素集合」里。
:::

### 再进一步:让 Analyzer 自己挑字段

上面这版 `CreatureAnalyzer` 还有个小遗憾:它写死了 `Creature`,而且构造时手动列出了三个 getter。如果我们想复用同样的「聚合一组同类型值」的能力,去分析别的对象、别的字段组合呢?这里 C++17 引入的 `std::invoke` 配合变参模板可以帮我们写出一个通用版:

```cpp
#include <array>
#include <cstddef>
#include <functional>
#include <numeric>
#include <algorithm>

template <typename T, typename... Getters>
class Analyzer {
public:
    static constexpr std::size_t N = sizeof...(Getters);
    std::array<int, N> vals;

    Analyzer(const T& obj, Getters... getters)
        : vals{std::invoke(getters, obj)...} {}

    int sum() const {
        return std::accumulate(vals.begin(), vals.end(), 0);
    }
    double avg() const {
        return sum() / static_cast<double>(N);
    }
    int max() const {
        return *std::max_element(vals.begin(), vals.end());
    }
};
```

`std::invoke(getters, obj)...` 这一行是整套设计的核心。`Getters...` 是一包「可调用物」,它可以是成员指针(`&Creature::strength`),也可以是 lambda,也可以是任何能对 `obj` 调用的东西;`std::invoke` 统一把它们都变成「对 `obj` 取一个 int」。构造时我们把对象和这一包 getter 一起传进来,展开成数组 `vals`。用起来是这样的:

```cpp
Creature hero{10, 20, 30};
Analyzer an(hero, &Creature::get_strength,
            &Creature::get_agility, &Creature::get_intelligence);
an.sum();  // 60
an.avg();  // 20
an.max();  // 30
```

到这里第一条路就走通了。它的本质是:**当一组对象在你关心的上下文里可以视作同一个东西时,就别让它们各自为政,把它们聚合进一个标准容器,剩下的交给算法**。模板和 `std::invoke` 让这套聚合可以脱离具体的类型,真正变成可复用的工具。下面我们进入第二条路——这才是 GoF 那张经典 UML 图的本体。

## 第二条路:经典树形 Composite

第一个例子里的「同类对象」是三个 int,聚合它们很简单,塞进数组就行。但现实里更多的情况是这样:你面对的不是三个同类型值,而是一堆**共同基类的不同子类对象**,而且它们之间还有**层级**关系。最经典的就是图形界面——你有一堆基本图元(矩形、圆、文字),又想把几个图元打包成一个组,组里还能再套组,最后形成一棵树。

我们先从最直觉的写法起步,看看它差在哪儿。

### 第一步:朴素的多态——没有「组」的概念

```cpp
class Graphic {
public:
    virtual ~Graphic() = default;
    virtual void draw() const = 0;
};

class Rectangle : public Graphic {
public:
    void draw() const override { /* 画矩形 */ }
};

class Circle : public Graphic {
public:
    void draw() const override { /* 画圆 */ }
};
```

这套多态没有任何问题,但它只支持「画一个图元」。一旦你想把三个图元当作一个整体来移动、来绘制,调用方就不得不自己维护一个 `vector<Graphic*>`,自己循环调 `draw`。换句话说,**「整体」这个概念在这套类型系统里根本不存在**,调用方每次都得手动拆解层级。当层级只有一层时还能忍,一旦图元要嵌套进组、组再嵌套进大组,调用方就得递归遍历这棵自己手搭的树,代码迅速失控。

我们真正想要的是:**让「一组图元」本身也是一个 `Graphic`**,这样调用方就能对它一视同仁地 `draw`,根本不用管它内部是一个图元还是一整棵子树。

### 第二步:Group 自己也是个 Graphic

让 `Group` 继承 `Graphic`、自己实现 `draw`,而 `draw` 的实现就是「遍历所有孩子、对每个孩子调 `draw`」。这一步的妙处在于:**递归是隐式的**——`Group::draw` 调用孩子的 `draw`,如果孩子本身又是一个 `Group`,它就会再次展开,层层递归下去,直到所有的叶子。调用方对此一无所知,它只管调一次 `draw`。

```cpp
#include <iostream>
#include <memory>
#include <string>
#include <vector>

class Graphic {
public:
    virtual ~Graphic() = default;
    virtual void draw() const = 0;
};

class Circle : public Graphic {
public:
    explicit Circle(std::string name) : name_(std::move(name)) {}
    void draw() const override {
        std::cout << "  Circle[" << name_ << "] drawn\n";
    }

private:
    std::string name_;
};

class Group : public Graphic {
public:
    explicit Group(std::string name) : name_(std::move(name)) {}

    void draw() const override {
        std::cout << "Group[" << name_ << "] (\n";
        for (const auto& child : children_) {
            child->draw();
        }
        std::cout << ") end Group[" << name_ << "]\n";
    }

    void add(std::unique_ptr<Graphic> g) {
        children_.push_back(std::move(g));
    }

private:
    std::string name_;
    std::vector<std::unique_ptr<Graphic>> children_;
};
```

这里有几个用现代 C++ 写 Composite 时值得说一说的选择。首先是所有权——我们用 `std::vector<std::unique_ptr<Graphic>>` 来装孩子,`add` 收一个 `std::unique_ptr`,所有权随调用转交进 `Group`。这一步非常关键,因为 GoF 原版书里写的还是裸指针(`GeoObject*`),那套代码加上 `new Rectangle()` 之后,`Group` 的析构根本不管这些孩子的释放,直接内存泄漏。用 `unique_ptr` 之后,`Group` 析构时所有孩子自动递归析构,整棵树的资源管理是 RAII 兜底的,你一行手动 `delete` 都不用写。

其次是 `draw` 里的那行 `child->draw()`。它看起来平淡无奇,但正是这一行让整棵树「活」了起来——它通过基类指针虚调用,孩子是叶子就画叶子,孩子是 `Group` 就递归画它的整棵子树。**调用的统一性就在这一行里**。

我们搭一棵真实的树来验证一下:`root` 里直接挂一个 `Circle("A")`,再挂一个子组 `sub`,`sub` 里挂两个圆 `B` 和 `C`,最后 `root` 再挂一个 `D`:

```cpp
#include <iostream>

int main() {
    Group root("root");
    root.add(std::make_unique<Circle>("A"));

    auto sub = std::make_unique<Group>("sub");
    sub->add(std::make_unique<Circle>("B"));
    sub->add(std::make_unique<Circle>("C"));
    root.add(std::move(sub));

    root.add(std::make_unique<Circle>("D"));

    root.draw();
}
```

编译跑一下:

```sh
$ g++ -std=c++23 -O2 -pthread composite_verify.cpp -o composite_verify
$ ./composite_verify
Group[root] (
  Circle[A] drawn
Group[sub] (
  Circle[B] drawn
  Circle[C] drawn
) end Group[sub]
  Circle[D] drawn
) end Group[root]
```

你看,调用方只调了一次 `root.draw()`,但绘制顺序里 `sub` 那一整组被完整展开——`B` 和 `C` 嵌在 `sub` 里、`sub` 又嵌在 `root` 里,层级完全正确。而这一切对 `main` 来说是透明的,它根本不知道 `root` 里还藏着一棵子树。**这就是 Composite 的价值:一棵任意深度的多叉树,对外伪装成一个对象**。

## 这里先验证一下:递归确实在干活

为了让你确信上面那个输出不是凑出来的,我们把 `draw` 里挂个缩进计数器,看看递归深度有没有按预期走。其实从输出已经能看出来——`Circle[A]`、`Circle[D]` 的缩进是一层(它们直接挂在 `root` 下),`Circle[B]`、`Circle[C]` 是被 `Group[sub]` 包起来的,逻辑层数是两层。`Group::draw` 自己并没有显式去算缩进,但它在调用 `child->draw()` 之前先打了一行 `Group[sub] (`,调用之后又打了一行 `) end Group[sub]`,这两行天然就把子树「夹」在中间,形成了视觉上的层级。真正的递归发生在 `child->draw()` 这一次虚调用上——当 `child` 指向的是另一个 `Group` 时,虚分发会再次进入 `Group::draw`,于是同样的「打印头 → 遍历孩子 → 打印尾」结构被套用一遍。这是 Composite 模式最优雅的地方:**整棵树的遍历逻辑被压缩进了同一个 `draw` 函数,没有 if 判断类型,没有手写栈,全靠多态分发**。

## 透明式 vs 安全式:那个 `add` 到底放哪儿

事情到这里还没完。回看刚才的代码,你会发现一个不太对劲的地方:`add` 这个方法**只定义在 `Group` 上**,叶子 `Circle` 压根没有 `add`。这意味着如果调用方手里拿的是一个 `Graphic&`(基类引用),它想往里加东西是加不进去的——因为基类接口里压根没声明 `add`。

这就引出了 Composite 模式一个绕不开的设计抉择:**管理孩子的方法(`add`/`remove`/`get_child`)到底要不要放进基类 Component 里**?GoF 给出了两种经典做法,分别叫**透明式(Transparent)**和**安全式(Safe)**。

### 透明式:add 进基类,叶子抛异常

所谓透明式,就是把 `add`、`remove` 这些方法直接声明在基类 `Graphic` 里,让叶子也「看起来」拥有这些方法,从而**调用方拿到一个 `Graphic&` 时完全无需关心它是叶子还是组**——这就是「透明」二字的来历。代价是叶子必须给出一个**毫无意义**的实现:通常是一个空操作,或者直接抛异常。

```cpp
class Graphic {
public:
    virtual ~Graphic() = default;
    virtual void draw() const = 0;
    // 透明式: 基类就声明 add, 叶子实现成抛异常
    virtual void add(std::unique_ptr<Graphic>) {
        throw std::logic_error("add() not supported on a leaf");
    }
};

class Circle : public Graphic {
    // ... 不 override add, 继承基类那个抛异常的版本
};
```

这种写法的好处是接口彻底统一,任何 `Graphic&` 都能调 `add`,调用方代码干净;坏处同样明显——**类型安全被破坏了**。你现在可以对一个 `Circle` 调 `add`,编译器一句废话都不说,直到运行时才抛异常。这等于把一个本该在编译期挡住的错误,推迟到了运行期。

我们把它跑起来验证,确实会抛:

```sh
$ ./composite_verify
===== leaf.add() throws (transparent) =====
caught: add() not supported on a leaf
```

### 安全式:add 只在 Group 上,叶子根本没有

安全式恰恰相反:**管理孩子的方法只出现在 `Group` 上,基类和叶子都不声明 `add`**。这下叶子压根就没有这个方法,你拿一个 `Circle` 去调 `add` 直接编译失败,错误被挡在了编译期,这就是「安全」。代价则是反过来——调用方拿到一个 `Graphic&` 时,如果想往里加东西,就得先把它 `dynamic_cast` 成 `Group&`,接口不再彻底统一。

```cpp
class Graphic {
public:
    virtual ~Graphic() = default;
    virtual void draw() const = 0;
    // 安全式: 基类里没有 add
};

class Circle : public Graphic { /* 没有 add */ };

class Group : public Graphic {
public:
    // ... draw ...
    void add(std::unique_ptr<Graphic> g) {  // add 只在这里
        children_.push_back(std::move(g));
    }
};
```

这一版里,如果你强行写 `Circle c("x"); c.add(...)`,编译器直接报错:

```sh
$ g++ -std=c++23 -O2 -pthread composite_safe_fail.cpp -o composite_safe_fail
composite_safe_fail.cpp:21:7: error: 'class Circle' has no member named 'add'
   21 |     c.add(std::make_unique<Circle>("y"));
      |       ^~~
```

错误发生在编译期,这正是「安全」二字的全部含义。本文前面那棵能跑的树用的就是安全式——`Group` 有 `add`,`Graphic` 基类没有。

### 怎么选

这俩写法没有绝对的高下,只看你的场景更怕哪种代价。如果调用方绝大多数时候都在**用**这棵树(调 `draw`、调 `render`),极少去**改**结构(调 `add`),那安全式更划算——你拿到了编译期类型安全,偶尔需要改结构时的一次 `dynamic_cast` 完全可以接受。反之,如果调用方频繁地在「叶子还是组」之间来回切换、频繁地动态增删节点,透明式让调用方代码更平整,运行期那点抛异常的风险可以容忍。GoF 那本书倾向于透明式(因为示例里调用方确实在大量增删节点),而现代 C++ 圈子里,看重类型安全的开发者往往更偏爱安全式。**记住这个取舍本身,比记住任何一种「标准答案」更重要**。

## 踩坑预警:这三件事别忘

::: warning 三件事别忘
第一,**所有权要钉死**。GoF 原版书用的是裸指针(`GeoObject*` 配合 `new`),示例代码里 `Group` 既不负责释放、也没有 `delete`,跑起来就是实打实的内存泄漏。现代 C++ 写 Composite,**容器里装的一定是 `std::unique_ptr<Component>`**,所有权随 `add` 转交,`Group` 析构时整棵子树自动回收。如果你需要共享所有权(比如同一个孩子挂在多个组里),再考虑 `std::shared_ptr`,但那会引入环引用的风险,得搭 `std::weak_ptr` 破环,复杂度立刻上来,通常能避免就避免。

第二,**别让透明式的异常悄悄溜走**。透明式里叶子的 `add` 默认实现往往是「抛异常」或「静默忽略」。静默忽略最危险——调用方以为加进去了,其实什么都没发生,bug 极难定位。即便要走透明式,叶子的 `add` 也一定要抛异常(或 `assert`),把问题暴露在最早的时刻。

第三,**递归深了会爆栈**。Composite 的遍历是递归的,深度等于树的层数。绝大多数 UI 树、文件系统树层数都很浅(几十层到头了),完全不是问题;但如果你拿 Composite 去表达某种可能退化为长链的结构(比如一个被恶意构造的深度嵌套 XML),递归 `draw` 就可能把调用栈吃光。遇到这种极端场景,把递归改成显式栈遍历更稳妥。
:::

## Composite 不讨喜的地方

和单例一样,Composite 也得诚实地说说它的代价,不能只讲好话。

**第一,类型安全被「一致性」吃掉了一部分。** 这一点我们前面展开过了——透明式为了接口统一,把叶子和组合揉进同一个接口,代价是无意义的方法(叶子上的 `add`)在编译期不会被拒绝,只能靠运行期抛异常兜底。这是 Composite 为「统一性」支付的硬成本,选透明式就要认这笔账。

**第二,叶子上加孩子这种「无意义操作」有时不止是抛异常那么轻巧。** 在一些实现里,叶子的 `add` 被写成空操作(什么也不做、也不报错),这种「宽容」会让调用方的代码继续往下跑,产出与预期完全不符的结果却没有任何报错。比起抛异常,静默失败才是 Composite 最阴险的坑,设计时一定要杜绝。

**第三,接口设计的复杂度上升。** 为了让叶子和组合共用一套接口,基类 `Graphic` 不得不既容纳「自描述」的职责(`draw`),又可能容纳「管理孩子」的职责(`add`/`remove`)。一旦决定走透明式,基类接口会膨胀;一旦走安全式,调用方又要承担 `dynamic_cast` 的样板代码。两种选择都有各自的不优雅,这是模式本身的固有张力,不是写法问题。

**第四,深递归的性能与栈风险。** 前面预警里提过,这里从代价角度再强调一次:Composite 的天然遍历方式是递归,这在层数可控的场景里几乎是免费的(虚调用 + 栈帧,现代 CPU 处理起来很轻松),但层数一旦失控,代价就从「可忽略」变成「可能爆栈」。在结构来源不可信的场合,要预设它可能很深。

## 小结

我们把整条演进路径捋一遍:

| 阶段 | 做法 | 为什么还不够 |
|---|---|---|
| 散落的同类变量 | 三个 int 各自为政,手写 `sum`/`avg`/`max` | 加字段要逐处改,容易漏 |
| 聚合成数组 | 把同类值塞进 `std::array`,交给算法 | 只解决「扁平同类」,没有层级 |
| `Analyzer` 模板 | `std::invoke` + 变参,脱离具体类型 | 解决了扁平聚合,但还表达不了树 |
| 经典树形 Composite | `Group` 自己也是 `Graphic`,`draw` 递归 | 这就是 GoF 的标准答案 |
| 透明式 / 安全式 | `add` 放基类 vs 只放 `Group` | 一致性 vs 类型安全的取舍 |

记下这几条关键结论:

- **Composite 的核心是「一致性」,不是「组合」这个词**。它让调用方对叶子和组合一视同仁,和 UML 里表示 has-a 的「组合关系」是两回事,别被名字带偏。
- **同类对象先想数组,有层级再上树**。如果一组东西在你关心的上下文里是同一个东西(几个 int、几个图元),先尝试把它们聚合成标准容器,标准库算法直接就能用;只有当对象之间有真正的层级嵌套时,才值得上完整的树形 Composite。
- **所有权用 `unique_ptr` 钉死**。GoF 原版的裸指针 + `new` 是内存泄漏的温床,现代 C++ 的容器里装 `std::unique_ptr<Component>`,析构自动回收整棵子树。
- **透明式 vs 安全式是核心取舍**。透明式接口统一但牺牲编译期类型安全(叶子上的 `add` 只能运行期抛异常);安全式编译期挡住错误但调用方要 `dynamic_cast`。选哪种,取决于你更怕哪种代价。
- **叶子的 `add` 永远别静默忽略**。要么抛异常,要么 `assert`,静默是 Composite 最阴险的坑。

::: tip 配套可编译工程
本节的例子在仓库 `code/volumn_codes/vol4/design-patterns/Composite/` 下有完整可编译工程(`.h` + main + `CMakeLists.txt`),`cmake -S . -B build && cmake --build build` 即可跑出上面这些输出。
:::

## 参考资源

- [cppreference:`std::invoke`](https://en.cppreference.com/w/cpp/utility/functional/invoke)(C++17 起,统一调用成员指针、lambda、函数对象)
- [cppreference:`std::unique_ptr`](https://en.cppreference.com/w/cpp/memory/unique_ptr)(所有权转移与递归析构)
- Gamma、Helm、Johnson、Vlissides,《Design Patterns: Elements of Reusable Object-Oriented Software》Composite 章节(透明式与安全式的原始论述)
- Dmitri Nesteruk,《C++20 Design Patterns》"Composite" 一节(数组聚合 + 树形 Composite 的现代 C++ 写法,本文 `CreatureAnalyzer` 演进的蓝本)
