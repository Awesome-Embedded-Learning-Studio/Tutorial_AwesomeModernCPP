---
title: "适配器模式:在不改旧代码的前提下让两边对上话"
description: "从一个画线但驱动只会画点的尴尬场景开始,一步步逼出对象适配器,顺带说清楚类适配器为什么不推荐、双向适配器什么时候上、缓存优化该怎么想"
chapter: 11
order: 5
tags:
  - host
  - cpp-modern
  - intermediate
  - 适配器模式
difficulty: intermediate
platform: host
cpp_standard: [11, 17, 20]
reading_time_minutes: 20
related:
  - "单例模式:从注释约束到 Meyer's Singleton"
prerequisites:
  - "Chapter 6: 类与对象"
---

# 适配器模式:在不改旧代码的前提下让两边对上话

## 我们到底在解决什么问题

我们先不上定义,直接看一个特别真实的场景。你在做一个绘图模块,已经辛辛苦苦地抽象出了一组几何图形——`Rectangle`、`Triangle`、`Circle`——它们内部都存成一组 `Line`(线段),`Line` 又由两个 `Point2D` 组成。这套抽象你用得很顺手,所有的业务逻辑都建在它上面。

然后 OLED 驱动的同事走过来,递给你一份头文件,跟你说:"我们这个驱动,只会画点,接口长这样,你把要画的东西喂进来就行。"——它要的是一组 `Point`,不是一组 `Line`。

```cpp
struct Point2D {
    int x;
    int y;
};

struct Line {
    Point2D start;
    Point2D end;
};
```

现在问题来了:**你这边全是 `Line`,驱动那边只认 `Point`,两边接口根本对不上。** 怎么办?

第一种本能反应是"改一边":要么去改驱动,让它支持画 `Line`;要么去改自己的几何抽象,让图形内部存 `Point`。但真实工程里,这两条路通常都堵死——驱动那份代码可能是厂商给的二进制库 + 头文件,你压根动不了源码;而你的几何抽象上面挂着面积计算、碰撞检测、序列化一整套功能,为了适配一个驱动把地基拆了重写,代价根本不可接受。**典型的"两边都不能动,但它们必须合作"的局面。**

适配器模式就是为这个局面生的。它的目标不是"修改"任何一方,而是**在中间塞一层翻译,让两边各自的接口原封不动,却能协同工作**。你可以把它想成那个二孔转三孔的电源转接头:你不会去把宿舍墙上的插座撬掉,也不会把你三头插头的电器剪开一个头,你只是买了个转接头,两边都不动,电就通了。

接下来我们就一步步把这一层翻译逼出来,看看每一步为什么是那个样子,以及真正的坑藏在哪儿。

## 第一步:先看清三件套——Target / Adaptee / Adapter

在动手之前,我们先把术语钉死,不然后面"适配谁""被适配谁"很容易绕晕。GoF 原版的适配器模式有三个固定角色:

**Target**,是**业务侧期望的接口**。也就是"我希望调用的那个东西长什么样"。在我们的故事里,业务代码(几何模块)希望驱动暴露一个"画一组 Line"的接口——那么 Target 就是这个期望。

**Adaptee**,是**接口对不上、但又改不了的那个现成类**。也就是"我手上实际有什么"。OLED 驱动只能画点,它就是 Adaptee。

**Adapter**,是**我们这一节要写的中间层**。它对外实现 Target 的接口,对内持有一个 Adaptee,把 Target 的调用翻译成 Adaptee 能理解的调用。

这三者的关系其实就是:业务代码只跟 Target 说话,Adapter 假装自己是 Target,背地里却把每一次调用转交给 Adaptee。业务代码从头到尾都不知道 Adaptee 存在,这正是适配器最大的价值——**把"接口不兼容"这件事封装在一个类里,不污染任何一方**。

## 第二步:把 Adaptee 摆出来——OLED 驱动只会画点

我们先把 Adaptee 的样子写实,这样后面适配的时候有据可依。驱动对外只有一个接口 `draw_points`,它接受一对迭代器 `[begin, end)`,把每个 `Point` 用 `set_pixel` 画到屏幕上:

```cpp
class OledDriver {
public:
    // Adaptee 侧的接口:只认 Point,不认 Line
    using ConstIter = std::vector<Point2D>::const_iterator;

    void draw_points(ConstIter begin, ConstIter end) {
        for (auto it = begin; it != end; ++it) {
            set_pixel(it->x, it->y);
        }
    }

private:
    void set_pixel(int x, int y) {
        ++pixels_drawn_;  // 这里用计数模拟"画了一个点"
    }

public:
    int pixels_drawn_ = 0;
};
```

接下来我们把业务侧的几何图形也摆出来——`Rectangle` 内部用四条 `Line` 描述自己的边:

```cpp
class Rectangle {
public:
    Rectangle(Point2D left_top, int width, int height) {
        Point2D rt{left_top.x + width, left_top.y};
        Point2D lb{left_top.x, left_top.y + height};
        Point2D rb{left_top.x + width, left_top.y + height};
        lines_ = { {left_top, rt}, {rt, rb}, {rb, lb}, {lb, left_top} };
    }
    const std::vector<Line>& lines() const { return lines_; }

private:
    std::vector<Line> lines_;
};
```

到这里,冲突就摆到桌面上了:`Rectangle` 给你的是 `lines()`,返回 `vector<Line>`;驱动要的是 `vector<Point2D>` 的迭代器区间。一个 `Line` 有两个端点,四条线就是八个端点,中间隔着一道"线到点"的翻译鸿沟。笔者就经常遇到这种情况。

## 第三步:对象适配器——持有一个 Adaptee,实现一个 Target

我们现在要做的是,写一个适配器,它接住业务侧的 `Line` 集合,在内部把它展开成一堆 `Point2D`,然后对外暴露一个"像驱动想要的那样"的迭代器区间。GoF 把这种写法叫**对象适配器(Object Adapter)**,因为它通过**组合**的方式持有被适配的数据:

```cpp
class LineToPointsAdapter {
public:
    using ConstIter = std::vector<Point2D>::const_iterator;

    explicit LineToPointsAdapter(const std::vector<Line>& lines) {
        points_.reserve(lines.size() * 2);
        for (const auto& l : lines) {
            points_.push_back(l.start);
            points_.push_back(l.end);
        }
    }

    // 对外暴露的"Target 接口":一对迭代器,正好喂给 OledDriver
    std::pair<ConstIter, ConstIter> points() const {
        return {points_.begin(), points_.end()};
    }

private:
    std::vector<Point2D> points_;
};
```

你看,这个适配器干的事情非常朴素:构造时把传进来的每条 `Line` 拆成两个端点,塞进自己的 `points_` 里;对外提供一个 `points()` 返回这堆点的迭代器区间。它就是个"翻译机",把"线"的语义翻译成"点"的语义。

用起来几乎是透明的——业务代码完全不需要知道驱动的存在,只要把适配器丢给驱动即可:

```cpp
int main() {
    Rectangle rect({0, 0}, 10, 5);
    LineToPointsAdapter adapter(rect.lines());

    OledDriver driver;
    auto [begin, end] = adapter.points();
    driver.draw_points(begin, end);

    std::cout << "pixels_drawn = " << driver.pixels_drawn_ << " (expect 8)\n";
}
```

我们来验证一下,编译跑一把:

```sh
$ g++ -std=c++23 -O2 -Wall -Wextra adapter_verify.cpp -o adapter_verify
$ ./adapter_verify
pixels_drawn = 8 (expect 8)
```

四条边,每条两个端点,正好八个点。适配器完成了它的本职工作——**`Rectangle` 和 `OledDriver` 谁都没改,却成功地合作画了图。**

这里你可能会问一个特别合理的问题:`LineToPointsAdapter` 构造的时候就把所有线展开成点存起来了,这看起来有点"急"?是的,这是最直白的实现,**在对象构造时就完成全部翻译**,好处是后续访问就是一次普通的内存遍历,没有额外开销;坏处是如果业务侧的 `lines` 后续还会变,适配器里这份 `points_` 就过时了。我们后面会专门聊这个坑。

## 第四步:这里我们先验证一下——Target/Adaptee/Adapter 三件套的透明性

口说无凭,我们把"业务代码完全不知道 Adaptee 存在"这件事用代码钉死。这是一个更经典的例子:我们的业务侧有一个 `Printer` 抽象(就是 Target),它期望一个 `print(string)` 接口;但手头只有一个老的 `LegacyLogger`(Adaptee),它的签名是 `write_line(const char*)`——参数类型、函数名都对不上。

```cpp
// Target:业务期望的接口
class Printer {
public:
    virtual ~Printer() = default;
    virtual void print(const std::string& msg) = 0;
};

// Adaptee:旧类,签名不兼容,而且改不了
class LegacyLogger {
public:
    void write_line(const char* content) {
        std::cout << "[legacy] " << content << "\n";
    }
};

// 对象适配器:实现 Target,内部持有 Adaptee
class LoggerAdapter : public Printer {
public:
    explicit LoggerAdapter(std::unique_ptr<LegacyLogger> adaptee)
        : adaptee_(std::move(adaptee)) {}

    void print(const std::string& msg) override {
        adaptee_->write_line(msg.c_str());  // 翻译:std::string -> const char*
    }

private:
    std::unique_ptr<LegacyLogger> adaptee_;
};

// 业务代码:只依赖 Target 抽象,完全不知道 LegacyLogger 存在
void greet(Printer& p) {
    p.print("hello from adapter");
}
```

`greet` 这个函数只认识 `Printer&`,它甚至不知道这个 `Printer` 背后藏着一个 `LegacyLogger`。这就是适配器模式把"接口不兼容"封装起来的直接收益——**业务侧依赖的是一个干净的抽象,适配细节被关在 `LoggerAdapter` 这一个类里**。我们编译验证一下:

```sh
$ g++ -std=c++23 -O2 -Wall -Wextra adapter_verify.cpp -o adapter_verify
$ ./adapter_verify
[legacy] hello from adapter
```

业务函数一句"hello from adapter",经过适配器翻译,被旧日志器原样吐了出来。适配器做的事情,就是把"叫一声"翻译成"写一行"。

## 类适配器:为什么不推荐私有继承那一版

GoF 原版里,适配器其实有两副面孔。上面这种"组合一个 Adaptee"的叫**对象适配器**,还有一副叫**类适配器(Class Adapter)**,写法是**私有继承 Adaptee + 公有继承 Target**:

```cpp
// 类适配器:私有继承拿实现,公有继承满足接口
class ClassAdapter : private LegacyLogger, public Printer {
public:
    void print(const std::string& msg) override {
        write_line(msg.c_str());  // 直接复用 Adaptee 的成员
    }
};
```

私有继承在这里的语义是 "implemented in terms of"——Adapter 想借用 `LegacyLogger` 的实现,但不想暴露"is-a"关系,所以用 `private` 继承把继承关系对外关掉,只把 `write_line` 留给自己内部用。它确实能编译能跑,GoF 当年 C++ 示例里也常用这种写法。

说实话,在现代 C++ 里我几乎不会这么写,原因有几条。**第一,类适配器把 Adaptee 写死在继承链里**——你只能在编译期决定适配谁,运行时想换一个 Adaptee 是不可能的,而对象适配器手里攥着一个指针/引用,运行时换实现是举手之劳。**第二,类适配器要求你能继承 Adaptee**,如果 Adaptee 是 `final` 的、或者它的接口本来就是非虚的自由函数(常见于 C 风格的第三方库),私有继承这条路直接走不通;对象适配器只要能"持有一个对象或一个引用"就能干活,适用面宽得多。**第三,多重继承一旦上多了,代码的耦合和可读性都会受影响**——组合在 C++ 里几乎总是比继承更灵活、更少惊喜。

所以记一条:**现代 C++ 里,对象适配器(组合)是默认选择,类适配器(私有继承)只有在 Adaptee 必须被当父类来用、并且运行时不换实现的窄场景下才考虑。** 能组合就别继承,这条原则在适配器这里同样成立。

## 双向适配器:两边都要互相用对方的接口

事情到这里还没完。前面所有的例子都是"单向适配"——业务侧产生 `Line`,驱动侧消费 `Point`,数据是单向流动的。但在实际系统里你会遇到一种更头疼的局面:**两个子系统都改不了,而且它们都需要互相用对方的数据结构。**

举个具体的场景。我们的几何模块除了"画图",又接了一个**几何计算引擎**,这个引擎的接口要求吃 `Line` 来算长度、交点、面积:

```cpp
class GeometryEngine {
public:
    // 这个引擎要的是 Line
    double total_length(std::vector<Line>::const_iterator begin,
                        std::vector<Line>::const_iterator end);
};
```

尴尬的事情来了:这个几何引擎有时候会从别处接收到一组 `Point`(比如从某个传感器读回来的一堆点),它需要把这些点还原成 `Line` 才能算;而 OLED 驱动那一边,有时候拿到的又是一组 `Line`,需要展开成 `Point` 才能画。也就是说,**两个方向(`Line -> Point` 和 `Point -> Line`)都需要翻译**。

这种"双向依赖"的场景下,单向适配器就不够用了,我们要上**双向适配器(Bidirectional Adapter)**:它在内部同时持有两份数据(`points` 和 `lines`),并且对外同时提供两个方向的访问接口。你从 `Line` 构造它,它顺带把 `Point` 也展开好;反过来你从 `Point` 构造它,它顺带把 `Line` 配对好:

```cpp
class BidirectionalAdapter {
public:
    // 方向一:从 Line 进来,顺带展开成 Point
    explicit BidirectionalAdapter(std::vector<Line> lines)
        : lines_(std::move(lines)) {
        points_.reserve(lines_.size() * 2);
        for (const auto& l : lines_) {
            points_.push_back(l.start);
            points_.push_back(l.end);
        }
    }

    // 对外两个方向都能取:要 Line 给 Line,要 Point 给 Point
    const std::vector<Line>& lines() const { return lines_; }
    const std::vector<Point2D>& points() const { return points_; }

private:
    std::vector<Line> lines_;
    std::vector<Point2D> points_;
};
```

这里我刻意只写了"从 `Line` 构造"这一个方向,这是因为 `Line -> Point` 的语义是确定的——一条线两个端点,展开就完事。但反过来 `Point -> Line` 的语义其实**不唯一**:四个点可以配成两条线,也可以首尾相连配成四条线,甚至可以是两个独立线段。到底怎么配,完全取决于业务约定,所以双向适配器里 `Point -> Line` 的转换逻辑必须由你根据具体场景写死,不存在"通用答案"。

我们来验证一下双向适配器,用一个矩形(四条线、八个点):

```sh
$ g++ -std=c++23 -O2 -Wall -Wextra bidi_cache_verify.cpp -o bidi_cache_verify
$ ./bidi_cache_verify
bidi points = 8 (expect 8)
bidi lines  = 4 (expect 4)
```

构造一次,两个方向的数据都备好了——OLED 驱动要 `points()` 就给它点,几何引擎要 `lines()` 就给它线。一个适配器同时服务两个子系统,这就是双向适配器的核心价值。当然代价也很明显:**它内部要同时维护两份数据,内存翻倍**,而且如果数据会变,你得同步更新两份,或者干脆标记其中一份为"惰性展开"。所以别一上来就上双向适配器,**只有当两个方向都真的会被消费时,它才值得这份额外的复杂度**。

## 缓存优化:反复画同一组线时怎么办

接下来问题来了。想象一下,屏幕每秒要刷新几十次,你的 `Rectangle` 每一帧都被画一遍。如果按我们前面那版 `LineToPointsAdapter`,每一帧构造一个新适配器,把同样的四条线重新展开成八个点——**这个展开动作会被重复执行成百上千次,而输入根本没变**。

这是个典型的"可缓存转换"。在内存越来越便宜的今天,**用空间换时间**是个很划算的策略:我们维护一个"源数据 -> 展开结果"的缓存,第一次展开的时候把结果记下来,后续发现源数据没变就直接返回缓存的结果,跳过展开。这和 HTTP 请求的缓存、CPU 的指令缓存是同一个思路——**只要转换有成本、而输入会重复,就值得缓存**。

实现的关键是怎么判断"输入是不是同一份"。最直白的办法是用源数据的地址(或者身份)当 key:

```cpp
class CachedLineToPointsAdapter {
public:
    explicit CachedLineToPointsAdapter(std::vector<Line>* key) : key_(key) {}

    const std::vector<Point2D>& get_points() {
        auto found = cache_.find(key_);
        if (found != cache_.end()) {
            return found->second;  // 命中缓存,跳过展开
        }
        // 未命中:第一次展开,并存入缓存
        ++expand_calls_;
        std::vector<Point2D> pts;
        pts.reserve(key_->size() * 2);
        for (const auto& l : *key_) {
            pts.push_back(l.start);
            pts.push_back(l.end);
        }
        return cache_[key_] = std::move(pts);
    }

    static std::size_t expand_calls_;  // 统计真实展开次数(演示用)

private:
    std::vector<Line>* key_;
    static inline std::unordered_map<std::vector<Line>*, std::vector<Point2D>>
        cache_;
};
std::size_t CachedLineToPointsAdapter::expand_calls_ = 0;
```

我们连画五帧,看看展开到底触发了几次:

```sh
$ ./bidi_cache_verify
expand_calls = 1 (expect 1)
```

五次请求,展开只发生了一次,剩下四次全部命中缓存。这就是缓存优化的直接收益。**当然,这里用裸指针当 key 有一个前提——源数据对象本身的生命周期要盖过缓存的生命周期**,否则地址被复用了,缓存就会串味。真实工程里,更稳妥的做法是用对象的内容哈希(比如把每条 `Line` 的坐标喂进一个 hash 函数)当 key,代价是每次查缓存都要算一次哈希。具体怎么权衡,取决于你的数据规模和变更频率,这一层属于工程取舍,不属于模式本身,我们点到为止。

::: warning 踩坑预警
缓存适配器里最容易翻车的不是"缓存命中率",而是**缓存失效**。一旦你缓存了展开结果,源数据后续又被改了,你的缓存就会返回过期的点。上面这份用指针当 key 的实现,完全不会感知到 `*key_` 内容的变化——如果有人改了 `Rectangle` 里的坐标,缓存里那八个点还是旧的,屏幕就会画出错位的图形。**凡是带缓存的适配器,必须想清楚"源数据什么时候会变、变了之后缓存怎么失效",这一步不想清楚,早晚炸在生产环境。**
:::

## 对象适配器的生命周期坑:构造时复制 vs 持有引用

我们把目光从缓存挪回适配器本身,再说一个特别容易踩的坑。前面那个 `LineToPointsAdapter` 的构造函数长这样:

```cpp
explicit LineToPointsAdapter(const std::vector<Line>& lines) {
    points_.reserve(lines.size() * 2);
    for (const auto& l : lines) {
        points_.push_back(l.start);
        points_.push_back(l.end);
    }
}
```

注意它是**按 `const&` 接收,然后在构造函数里把内容复制了一份**存进 `points_`。这个选择是安全的——适配器持有的是自己的一份拷贝,源数据的生命周期跟适配器解耦了,源数据销毁了也不影响适配器。代价是构造时有一次完整复制。

但你有时候会想偷懒,觉得"我明明只是临时用一下,复制一份太亏了,直接持引用不就行了?":

```cpp
// ⚠️ 危险:持有引用,源数据失效后引用悬垂
class RefAdapter {
public:
    explicit RefAdapter(const std::vector<Line>& lines) : lines_(lines) {}
    // ...
private:
    const std::vector<Line>& lines_;  // 悬垂引用高发区
};
```

这种写法能编过,运行起来大部分时候也没问题——直到某一天,有人把一个临时对象(比如函数返回的 `vector`、或者 `std::move` 走的源)喂给了 `RefAdapter`,引用瞬间悬垂,你拿到的就是一堆野指针。**持有引用的适配器把"源数据生命周期"这个隐式约束强加给了每一个调用方,而 C++ 编译器对此毫无检查。** 我的建议是:**默认就走"构造时复制"这条安全路;只有当你能用文档/类型系统明确保证源数据生命周期盖过适配器时(比如源数据本身是个长寿单例),才考虑持引用。** 这跟单例篇里讲的"全局状态穿透接口"是同一类毛病——把生命周期约束藏在注释里,迟早会有人踩。

## 适配器和它那些表兄弟的边界

到这里你大概发现了,适配器模式其实挺"朴素"的——它不发明新机制,只是把两个不兼容的接口接起来。但也正因为朴素,它特别容易和别的结构型模式混在一起。我们把几条边界划清楚:

**适配器 vs 桥接(Bridge)。** 适配器是事后补救——两个已经存在的、接口不兼容的类,你没法改它们,只能在中间塞一层翻译。桥接是事前设计——你从一开始就把"抽象"和"实现"拆成两条独立的继承轴,让它们可以各自演化、任意组合。换句话说,**适配器解决的是"已经对不上",桥接解决的是"提前防止对不上"**。

**适配器 vs 装饰器(Decorator)。** 装饰器**不改变接口**,它实现的是和被装饰对象同一个接口,只是在调用前后加点新行为(加日志、加缓存、加权限检查)。适配器**改变接口**——它对外的接口和内部持有的对象接口不一样,它的工作就是翻译。**装饰器是"同一个接口,加料";适配器是"换个接口,翻译"**。

**适配器 vs 外观(Facade)。** 外观是给一个复杂的子系统**简化**出一个新的、更容易用的入口——它通常是一对多,把子系统的十几个类收拢成一个清爽的接口。适配器是一对一,把一个已有的接口**转换**成另一个形状。**外观是"减法",适配器是"转换"**。

记住这三条边界,你拿到一个需求时就能很快判断:我要的是翻译(适配器)、加料(装饰器)、简化(外观)、还是拆维度(桥接)?这几件事长得像,意图完全不同。

## 适配器模式的代价

最后我们诚实地聊聊代价。适配器模式最大的优点是**符合开闭原则**——你不动旧代码,只加一个新类,就能让两个不兼容的系统协作,这对那些"碰不得"的遗留系统(厂商驱动、第三方库、跨团队接口)是救命稻草。它也让原本没法复用的代码重新可用,代价被关在一个类里,不污染业务侧。

但代价是真实的。**第一,它增加了一层间接**——每一次调用都要经过适配器中转,理论上多一次函数调用的开销,虽然实践中这点开销通常可以忽略,但在高频路径(比如每帧画图的循环里)值得留意。**第二,它可能掩盖真实的复杂度**——尤其是双向适配器、带缓存的适配器,内部状态一多,调试时反而难追,因为你得透过适配器这层"翻译"去理解到底发生了什么。**第三,适配器会膨胀**——如果你给每一对不兼容的接口都写一个适配器,系统里迟早会"适配器满天飞",这时候真正的信号是"你的抽象设计本身有问题",而不是"再多写几个适配器"。

所以用适配器要有个度:**它是"接口对不上但又改不了"时的对症药,不是"接口设计得乱七八糟"时的遮羞布。** 真正健康的做法是在源头把接口设计得一致,适配器只在你确实无法控制某一方时才出手——比如对接第三方库、遗留代码、跨语言绑定这些场景。

## 小结

我们把适配器模式的整条脉络捋一遍:

| 阶段 | 做法 | 为什么需要 / 为什么还不够 |
|---|---|---|
| 对象适配器 | 组合一个 Adaptee,实现 Target 接口 | 默认选择,运行时可换实现,不要求继承 |
| 类适配器 | 私有继承 Adaptee + 公有继承 Target | 编译期写死,要求可继承,现代 C++ 不推荐 |
| 双向适配器 | 内部同时维护两份数据,两个方向都能导出 | 双方互相需要对方接口时才上,内存翻倍 |
| 缓存优化 | 按 key 记忆转换结果,跳过重复转换 | 高频重复转换时省时间,但要解决缓存失效 |

记下这几条关键结论:

- **适配器解决的是"两边接口对不上、又都不能改"的事后补救**,不是事前的接口设计;后者属于桥接。
- **现代 C++ 里默认选对象适配器(组合)**,它运行时可换实现、不要求继承 Adaptee、适用面最宽;类适配器(私有继承)几乎总是可以用组合替代。
- **双向适配器只在两个方向都真的被消费时才值得**,它内存翻倍、内部状态多,别一上来就用。
- **缓存优化是双刃剑**:省时间的前提是你想清楚了"源数据什么时候变、缓存怎么失效",否则就是定时炸弹。
- **适配器是"碰不得的遗留代码"的对症药**,不是"接口设计混乱"的遮羞布;接口对不上的根源该治还得治。

::: tip 配套可编译工程
本节的例子在仓库 `code/volumn_codes/vol4/design-patterns/Adapter/` 下有完整可编译工程(`.h` + main + `CMakeLists.txt`),`cmake -S . -B build && cmake --build build` 即可跑出上面这些输出。
:::

## 参考资源

- [cppreference:`std::unique_ptr`](https://en.cppreference.com/w/cpp/memory/unique_ptr)(对象适配器持有 Adaptee 的首选方式,C++11 起)
- [cppreference:`std::unordered_map`](https://en.cppreference.com/w/cpp/container/unordered_map)(缓存适配器的 key->result 映射)
- Erich Gamma 等,《设计模式:可复用面向对象软件的基础》第 4 章(适配器模式的 GoF 原版,对象适配器 vs 类适配器)
- Fedor G. Pikus,《C++20 Design Patterns》(几何图形 `Line`/`Point` 适配场景的原始灵感)
