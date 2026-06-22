---
title: "享元模式:别再给每个字都拷一份字形了"
description: "从一个塞满几千万个字符串的文本编辑器起步,一步步把「会变的」和「不会变的」拆开,逼出享元模式,顺手证明共享池里真的只有一份对象、原版工厂并发下会重复构造、shared_ptr 为什么是现代 C++ 的正确所有权"
chapter: 11
order: 10
tags:
  - host
  - cpp-modern
  - intermediate
  - 对象池
  - 享元模式
difficulty: intermediate
platform: host
cpp_standard: [11, 17, 20]
reading_time_minutes: 20
related:
  - "单例模式:从注释约束到 Meyer's Singleton"
prerequisites:
  - "Chapter 6: 类与对象"
  - "单例模式:从注释约束到 Meyer's Singleton"
---

# 享元模式:别再给每个字都拷一份字形了

## 我们到底在解决什么问题

我们先不谈模式,谈一个具体的场景。想象你在写一个文本编辑器,要打开一本几千万字的大书。我们下意识的写法是这样的——文档里的每一个字都是一个对象,对象里带着这个字的字形数据(笔画、位图、字体度量):

```cpp
struct Glyph {
    std::string content;          // "你"
    std::vector<Stroke> strokes;  // 几十字节的字形笔画数据
    FontMetrics   metrics;
    // ... 真正占内存的是后面这一坨,不是 content
};
std::vector<Glyph> document;      // 几千万份完整的 Glyph
```

写起来直觉、读起来也直观,但稍微算一笔账你就会发现问题:常用汉字也就不到 4000 个,而这本书里有几千万个 Glyph 对象——**同一个「我」字,在内存里被完整地复制了几十万次**,每个副本里那几十字节的笔画数据一模一样。这部分重复的数据才是真正的内存杀手,而 `content` 本身的那点开销相比之下根本不值一提。

享元模式(Flyweight Pattern)要解决的,就是这类 **「大量对象、但其中大部分状态是重复的、可以共享」** 的性能问题。它的思路直白到一句话:别再给每个字都拷一份字形了——把字形抽出来放进一个共享池,文档里只存一个指向这份共享字形的引用。

不过,在动手之前,有一件事我们要先想清楚:既然这个思路这么好,为什么我们平时写 `std::string` 处理 ASCII 文本的时候,从来不搞什么共享池?原因在于**代价**。ASCII 字符本身就只有一个字节,而一个指针或一个引用通常要 8 个字节(64 位下),你为了省那 1 个字节、引入了一个 8 字节的指针,反而把对象搞大了,再加上维护池子的开销,得不偿失。享元模式有它的甜区:**只有当被共享的那部分状态「足够重」、而对象的数量又「足够多」时,这笔交易才划算**。字形、纹理、棋子配置、数据库连接配置——这些都是典型的甜区。一个 1 字节的 char 不是。

接下来我们就一步步来,从最直觉的写法开始,看看每一步为什么还不够,最后逼出一个能跑、线程安全、所有权清晰的现代 C++ 享元。

## 第一步:最原始的写法——每个对象自己带全部状态

我们先用最直白的写法把问题铺开。棋盘上的棋子,每个棋子自己存一份颜色和类型:

```cpp
#include <iostream>
#include <string>
#include <vector>

class ChessPiece {
public:
    ChessPiece(std::string color, std::string type, int x, int y)
        : color_(std::move(color))
        , type_(std::move(type))
        , x_(x)
        , y_(y) {}

    void draw() const {
        std::cout << color_ << type_ << " 放在 (" << x_ << "," << y_ << ")\n";
    }

private:
    std::string color_;  // 黑 / 红
    std::string type_;   // 卒 / 兵 / 车 / 马 ...
    int         x_;      // 棋盘坐标
    int         y_;
};

int main() {
    std::vector<ChessPiece> board;
    board.emplace_back("黑", "卒", 2, 3);
    board.emplace_back("黑", "卒", 2, 4);
    board.emplace_back("黑", "卒", 2, 5);
    // ... 棋盘上 16 个黑卒,每个都完整拷了一份 "黑"+"卒"
    for (const auto& p : board) p.draw();
}
```

这段代码功能上完全正确,问题藏在内存里:棋盘上摆满了 16 个黑卒,这 16 个对象里 `color_` 全是 `"黑"`、`type_` 全是 `"卒"`,这部分数据被原样复制了 16 份。这 16 份副本之间没有任何区别,但每一份都在老老实实地占着内存。

问题出在哪?出在**我们把「会变的状态」和「不变的状态」揉在了同一个对象里**。颜色和类型,对一个具体的「黑卒」来说,从它被创建的那一刻起到棋局结束都不会变;唯一会变的是它此刻在棋盘上的位置 `(x_, y_)`。我们把这两种性质完全不同的状态用同样的方式存储了,于是那些不变的状态就被迫跟着对象的数量一起膨胀。

## 第二步:把状态拆开——内部状态 vs 外部状态

享元模式的核心动作,就是把这堆状态按「会不会变」一刀切成两类:

内部状态(intrinsic state),是对象自身**不变、可复用、可以被多个使用者共享**的那部分。对棋子来说就是「黑卒」这个身份——颜色加类型。对字形来说是字形数据本身。这部分我们抽出来,放进共享池,全局只存一份。

外部状态(extrinsic state),是**随上下文变化、每次使用时才确定**的那部分。对棋子来说是它此刻在棋盘上的坐标;对字形来说是它出现在文档里的第几行第几列。这部分不能共享,因为它本就因时因地而异,所以它**不进享元对象**,而是由调用方在使用时临时传进来。

这个拆分是享元模式的灵魂。一旦你想清楚哪些是内部状态、哪些是外部状态,后面所有的代码都只是这个拆分的工程化落地。我们先把这个心智模型写成最直白的一版:把 `(color, type)` 这一对不变的内部状态抠出来,单独做成一个可共享的小对象,坐标这种外部状态留在调用方,用的时候传进去。

```cpp
#include <iostream>
#include <string>

// 享元对象:只装内部状态(颜色 + 类型)
class ChessPiece {
public:
    ChessPiece(std::string color, std::string type)
        : color_(std::move(color))
        , type_(std::move(type)) {}

    // 外部状态(x, y)作为参数传进来,不存进对象
    void draw(int x, int y) const {
        std::cout << color_ << type_ << " 放在 (" << x << "," << y << ")\n";
    }

private:
    std::string color_;
    std::string type_;
};
```

你看,`ChessPiece` 瘦身了——它只认得自己是谁(颜色 + 类型),完全不知道自己站在棋盘的哪个位置。位置这个外部状态,作为参数在 `draw` 调用的一瞬间才传进来,用完即弃,不占对象一丁点内存。

但这里还差一环:我们现在有了一个「可以共享」的对象,可还没人保证它真的被共享。要是调用方想用「黑卒」,随手就 `ChessPiece p("黑", "卒")` 新建一个,那我们和没拆又有什么区别?我们需要一个**入口**,专门负责「同样的内部状态只造一份,后面再来要,直接把那一份还回去」。这个入口有个专门的名字,叫享元工厂(Flyweight Factory)。

## 第三步:享元工厂——find-or-insert 共享池

工厂的活儿很简单:你来要一个「黑卒」,我先看看池子里有没有,有就把已有的那份还给你,没有才造一份新的、塞进池子、再还给你。这个套路有个名字叫 **find-or-insert**,本质就是缓存:

```cpp
#include <memory>
#include <string>
#include <unordered_map>

class ChessFactory {
public:
    std::shared_ptr<ChessPiece> get_chess(const std::string& color,
                                          const std::string& type) {
        std::string key = color + type;
        auto it = pool_.find(key);
        if (it != pool_.end()) {
            return it->second;            // 已有,直接复用
        }
        auto piece = std::make_shared<ChessPiece>(color, type);
        pool_[key] = piece;
        return piece;
    }

private:
    std::unordered_map<std::string, std::shared_ptr<ChessPiece>> pool_;
};
```

池子是一个 `unordered_map`,key 是「颜色+类型」拼出来的字符串,value 是 `shared_ptr<ChessPiece>`。用 `shared_ptr` 是有讲究的,我们后面专门讲,这里先记住一点:**池子和调用方可以同时持有同一份棋子,池子负责「保证唯一」,调用方负责「用到」,各司其职**。

现在我们把这版完整跑一遍。棋盘上摆 3 个位置,但黑卒只在内存里存在一份:

```cpp
int main() {
    ChessFactory factory;
    auto black_pawn = factory.get_chess("黑", "卒");
    auto red_pawn   = factory.get_chess("红", "兵");

    // 同一份 black_pawn,被画在三个不同的坐标上
    black_pawn->draw(2, 3);
    red_pawn->draw(5, 6);
    black_pawn->draw(2, 4);
}
```

```sh
$ g++ -std=c++23 -O2 -pthread flyweight_chess.cpp -o flyweight_chess
$ ./flyweight_chess
黑卒 放在 (2,3)
红兵 放在 (5,6)
黑卒 放在 (2,4)
```

输出上和「每子一份」的版本看不出任何区别——这是对的,享元模式对功能完全透明,它只动内存,不动行为。真正的区别在内存里:即使棋盘上有 16 个黑卒,池子里也永远只有一份「黑卒」对象,16 个位置各自存着自己当前的外部状态坐标,画的时候一起传进来。

## 这里先验证一下:共享是真的吗

口说无凭,我们写个小程序证明「同一个 key 取两次,拿到的真的是同一份对象,而不是两份内容一样的拷贝」。判据很简单——`shared_ptr::get()` 返回的是底层裸指针,如果两次取到的指针相等,那就是同一个对象:

```cpp
#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>

class Glyph {
public:
    explicit Glyph(std::string content) : content_(std::move(content)) {}
    const std::string& content() const { return content_; }
private:
    std::string content_;
};

class GlyphFactory {
public:
    std::shared_ptr<Glyph> get(const std::string& content) {
        auto it = pool_.find(content);
        if (it != pool_.end()) return it->second;
        auto g = std::make_shared<Glyph>(content);
        pool_[content] = g;
        return g;
    }
    std::size_t size() const { return pool_.size(); }
private:
    std::unordered_map<std::string, std::shared_ptr<Glyph>> pool_;
};

int main() {
    GlyphFactory factory;
    auto a1 = factory.get("你");
    auto a2 = factory.get("你");   // 同一个字取两次
    auto b1 = factory.get("好");

    std::cout << "a1.get() == a2.get() : " << std::boolalpha
              << (a1.get() == a2.get()) << "\n";   // 期待 true:同一份对象
    std::cout << "a1.get() == b1.get() : " << std::boolalpha
              << (a1.get() == b1.get()) << "\n";   // 期待 false:不同字
    std::cout << "pool size : " << factory.size() << "\n";
}
```

```sh
$ g++ -std=c++23 -O2 -pthread flyweight_verify.cpp -o flyweight_verify
$ ./flyweight_verify
a1.get() == a2.get() : true
a1.get() == b1.get() : false
pool size : 2
```

两次 `get("你")` 拿到的是同一个指针,池子大小是 2——「你」和「好」各一份。共享是真的,不是幻觉。

我们再顺手量一笔内存账。把一份一百万字的文档按「享元」和「暴力」两种方式存,看看指针数组比字符串数组省了多少(这里的「字形」用单个 char 模拟,真正的字形数据会重得多,享元的优势只会更明显):

```sh
$ ./flyweight_mem
sizeof(std::string)            = 32 bytes
sizeof(std::shared_ptr<Glyph>) = 16 bytes
doc_fly  pointer array 概算    = 15625 KB
doc_naive string  array 概算   = 31250 KB
pool 去重后对象数              = 15
```

`shared_ptr` 比一个 `std::string` 还小(16 vs 32 字节),更关键的是——一百万个位置,真正的字形数据(用 char 模拟)只在池子里存了 15 份。如果把 `Glyph` 换成真实字形那种几十上百字节的重对象,享元省下的就不是一倍,而是几百万倍量级。这就是「用引用取代值」的回报。

## 一个更直观的例子:文本里的字与词

棋子的例子把内部/外部状态拆得很干净,我们再换一个更接近原书场景的例子,顺便演示一个享元的进阶用法——**被共享的不一定是单个对象,也可以是常见组合**。

假设我们在渲染一份大文档,里面「你」「好」「吧」这些高频字反复出现,「你好」「谢谢」这类常见词组也反复出现。我们完全可以把字和词都丢进同一个共享池,文档里只存引用序列,渲染时按顺序取出即可:

```cpp
#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class Glyph {
public:
    explicit Glyph(std::string content) : content_(std::move(content)) {}
    void draw() const { std::cout << content_; }
private:
    std::string content_;
};

class GlyphFactory {
public:
    std::shared_ptr<Glyph> get(const std::string& content) {
        auto it = pool_.find(content);
        if (it != pool_.end()) return it->second;
        auto g = std::make_shared<Glyph>(content);
        pool_[content] = g;
        return g;
    }
private:
    std::unordered_map<std::string, std::shared_ptr<Glyph>> pool_;
};

// 文档:只持有指向共享字形的引用,不持有字形数据本身
class Document {
public:
    void add_word(const std::shared_ptr<Glyph>& glyph) {
        text_.push_back(glyph);
    }
    void render() const {
        for (const auto& g : text_) g->draw();
        std::cout << "\n";
    }
private:
    std::vector<std::shared_ptr<Glyph>> text_;
};

int main() {
    GlyphFactory factory;
    Document doc;
    auto ni = factory.get("你");
    auto hao = factory.get("好");
    auto ba = factory.get("吧");
    doc.add_word(ni); doc.add_word(hao); doc.add_word(ba);
    doc.add_word(ni); doc.add_word(hao);   // 第二次「你好」,完全复用
    doc.render();
}
```

```sh
$ ./flyweight_source_example
你好吧你好
```

文档里出现了 5 个字,但池子里只有 3 份字形。如果你愿意,还可以把「你好」整个作为一个享元 key 塞进同一个池子——下次再遇到「你好」,直接命中,连两次查找都省了。享元的粒度是可以按场景调的:对象越小、出现越频繁,共享的收益越明显,就越值得往池子里塞。

## 踩坑预警:这个工厂不是线程安全的

到这一步,我们已经有了功能正确、能省内存的享元。先别急着用——**原版工厂有一个藏得很深的坑,它在并发下会重复构造对象**。

你看 `get` 这个函数:它先 `find`,没找到才 `make_shared` 再 `insert`。这三步之间没有任何同步。单线程下这毫无问题,但一旦多个线程同时来要同一个 key,就会撞上一个经典的 **TOCTOU(Time-of-Check-to-Time-of-Use)竞态**:线程 A 检查发现没有「你」,正准备去造;线程 B 也检查发现没有,也去造;两个人都造完、都往池子里 `insert`,结果「你」这个对象被造了两遍。享元「全局唯一」的承诺,在并发下就这么悄悄破功了。

我们故意把构造函数拖慢一点,把这个竞态窗口放大,跑给你看:

```cpp
class Glyph {
public:
    explicit Glyph(const std::string& content) : content_(content) {
        ++kConstruct;
        std::this_thread::sleep_for(std::chrono::microseconds(100));  // 放大竞态窗口
    }
    static inline std::atomic<int> kConstruct{0};
    std::string content_;
};

class NaiveFactory {                          // 笔记原版的工厂,无锁
public:
    std::shared_ptr<Glyph> get(const std::string& content) {
        auto it = pool_.find(content);
        if (it != pool_.end()) return it->second;
        auto g = std::make_shared<Glyph>(content);
        pool_[content] = g;
        return g;
    }
    std::size_t size() const { return pool_.size(); }
private:
    std::unordered_map<std::string, std::shared_ptr<Glyph>> pool_;
};
```

让 64 个线程同时去要同一个字「你」,数一下它到底被构造了几次:

```sh
$ g++ -std=c++23 -O2 -pthread flyweight_race2.cpp -o flyweight_race2
$ for i in 1 2 3; do echo "--- run $i ---"; ./flyweight_race2; done
--- run 1 ---
构造次数 = 4 (理想 1)
pool 终态大小 = 1 (理想 1)
--- run 2 ---
构造次数 = 4 (理想 1)
pool 终态大小 = 1 (理想 1)
--- run 3 ---
构造次数 = 5 (理想 1)
pool 终态大小 = 1 (理想 1)
```

理想情况下「你」应该只被构造 1 次,实际上构造了 4 到 5 次。这里有个特别坑的地方——`pool_.size()` 跑完还是 1,看起来「没事」。这是因为 `operator[]` 最终把多次构造的结果互相覆盖了,池子的终态收敛到了一个条目。**所以你光看池子大小,根本发现不了问题**:对象的终态是共享的,但构造的副作用(去加载资源、去分配内存、去初始化状态)已经实实在在发生了好几遍。在真实场景里,享元对象的构造往往就是那个「重」操作——加载一份纹理、解析一份配置——重复构造几遍,你费尽心机省下的内存可能还不够这次重复加载浪费的。

::: warning 享元工厂的并发陷阱
原版的 find-or-insert 工厂**只在单线程下成立**。一旦享元对象可能被多线程并发获取,`unordered_map` 既不是线程安全的容器,find-or-insert 本身又有 TOCTOU 竞态,必须显式加锁。别被「终态 pool 大小正常」骗了——那只是 `operator[]` 互相覆盖的假象,构造的副作用该重复还是重复了。
:::

## 改对:加一把锁的线程安全工厂

修起来其实很直接——把整个 find-or-insert 用 `std::mutex` 包起来。同一时刻只有一个线程能进临界区,`find` 和 `insert` 就成了原子的整体,竞态自然消失:

```cpp
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

class ThreadSafeGlyphFactory {
public:
    std::shared_ptr<Glyph> get(const std::string& content) {
        std::lock_guard<std::mutex> lk(mtx_);
        auto it = pool_.find(content);
        if (it != pool_.end()) return it->second;
        auto g = std::make_shared<Glyph>(content);
        pool_[content] = g;
        return g;
    }

private:
    std::mutex mtx_;
    std::unordered_map<std::string, std::shared_ptr<Glyph>> pool_;
};
```

同样 64 个线程并发,这次构造次数稳稳地是 1:

```sh
$ g++ -std=c++23 -O2 -pthread flyweight_threadsafe.cpp -o flyweight_threadsafe
$ for i in 1 2 3; do echo "--- run $i ---"; ./flyweight_threadsafe; done
--- run 1 ---
构造次数 = 1 (理想 1)
--- run 2 ---
构造次数 = 1 (理想 1)
--- run 3 ---
构造次数 = 1 (理想 1)
```

你可能听说过「双重检查锁(DCLP)」这条路——在锁外先无锁 `find` 一次,命中就直接返回,没命中再进锁。我必须提醒你,这条路在 C++ 里非常难写对:`unordered_map` 本身的读写在多线程下没有数据竞争保证,你在锁外读一个可能正被另一个线程写的 map,本身已经是未定义行为。要想安全地「锁外读」,得换成并发安全的哈希表、或者用 `std::atomic<std::shared_ptr>` 的原子 load/store(C++20 起),复杂度立刻上来。对绝大多数场景,**一把互斥锁包住整个 find-or-insert 是最划算、最不容易写错的选择**——享元工厂的争用通常很低(热点 key 第一次构造完之后基本都是 `find` 命中),锁的代价远小于你瞎优化引入的 bug。

## 为什么用 shared_ptr,而不是裸指针或 weak_ptr

前面我们一直用 `shared_ptr`,这里得说清楚为什么,因为享元模式在 GoF 原版书里用的可是裸指针——而那个写法在现代 C++ 里是个坑。

享元的所有权模型有点特殊:**工厂「管理」享元对象,调用方「使用」享元对象,两边都需要持有它,但谁都不该独占**。这正好是 `shared_ptr` 的语义——共享所有权,最后一个持有者析构时对象才回收。

我们先验证一个关键性质:**调用方拿到的 `shared_ptr`,在工厂的池子被清空之后,对象依然存活**。这件事是共享所有权之所以成立的地基,我们跑一下确认:

```cpp
int main() {
    std::shared_ptr<Glyph> outer;
    {
        std::unordered_map<std::string, std::shared_ptr<Glyph>> pool;
        auto g = std::make_shared<Glyph>("你");
        pool["你"] = g;
        outer = g;                                  // 调用方也持有一份
        std::cout << "池子活着时 use_count = " << g.use_count() << "\n";
    }                                               // pool 析构,但 outer 还在
    std::cout << "池子死后   use_count = " << outer.use_count() << "\n";
    std::cout << "outer 还能用吗? content = " << outer->content() << "\n";
}
```

```sh
$ ./flyweight_refcount
construct 你 use_count=0
池子活着时 use_count = 3
池子死后   use_count = 1
outer 还能用吗? content = 你
destruct  你
```

`use_count` 在池子活着时是 3(map 一份、局部变量 `g` 一份、`outer` 一份),池子析构后降到 1,但 `outer` 仍然能安全地访问对象,直到 `outer` 自己析构时对象才被销毁。**这就是 `shared_ptr` 给享元带来的最关键的正确性保证:享元对象的生命周期跟着引用走,而不是跟着工厂走。**

对比一下 GoF 原版的裸指针方案:工厂里 `pool_[key] = new Glyph(...)`,返回 `Glyph*`。调用方拿到的只是一个裸指针,它既不知道这个指针归谁管,也无法阻止工厂某天把对象 `delete` 掉。原书的示例代码甚至连 `delete` 都没写,跑起来就是实打实的内存泄漏。现代 C++ 写享元,**池子里放 `shared_ptr`、返回 `shared_ptr`,所有权自动协调**,既不会泄漏,也不会悬空。

那为什么不用 `weak_ptr`?有一种说法是「工厂只持有 `weak_ptr`,没人用了对象就自动回收,池子不占内存」。听起来很美,但用 `weak_ptr` 意味着每次 `get` 都要 `lock()` 一次,`lock` 失败(对象真被回收了)还得重新构造——享元的核心收益恰恰是「构造一次、反复复用」,你要是让享元对象动不动就被回收再重建,共享池就退化成了普通缓存,失去了「省构造」的意义。**享元对象一旦构造,就该在工厂生命周期内长期存活**,这正是 `shared_ptr` 的强引用表达的语义。`weak_ptr` 适合「偶尔用、用完就忘」的缓存,不适合享元。

## 一个更贴近实战的例子:配置共享

我们把前面几条结论收拢到一个更像生产代码的例子里。假设程序里有一堆地方要连数据库,连接配置由 `(host, port)` 决定,相同的配置完全可以共享同一份对象,不同的地方各用各的连接次数、超时这种外部状态:

```cpp
#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>

class DbConfig {
public:
    DbConfig(std::string host, int port)
        : host_(std::move(host)), port_(port) {}
    void show() const {
        std::cout << "DbConfig: " << host_ << ":" << port_ << "\n";
    }
private:
    std::string host_;
    int         port_;
};

class DbConfigFactory {
public:
    std::shared_ptr<DbConfig> get(const std::string& host, int port) {
        std::lock_guard<std::mutex> lk(mtx_);        // 并发安全
        std::string key = host + ":" + std::to_string(port);
        auto it = pool_.find(key);
        if (it != pool_.end()) return it->second;
        auto cfg = std::make_shared<DbConfig>(host, port);
        pool_[key] = cfg;
        return cfg;
    }
private:
    std::mutex    mtx_;
    std::unordered_map<std::string, std::shared_ptr<DbConfig>> pool_;
};

int main() {
    DbConfigFactory factory;
    auto c1 = factory.get("127.0.0.1", 3306);
    auto c2 = factory.get("127.0.0.1", 3306);        // 和 c1 同一份
    auto c3 = factory.get("192.168.1.10", 5432);     // 另一份
    c1->show(); c2->show(); c3->show();
    std::cout << "c1 和 c2 是同一份? " << std::boolalpha
              << (c1.get() == c2.get()) << "\n";
}
```

```sh
$ ./flyweight_dbconfig
DbConfig: 127.0.0.1:3306
DbConfig: 127.0.0.1:3306
DbConfig: 192.168.1.10:5432
c1 和 c2 是同一份? true
```

这就是一个完整、可直接用到生产里的享元:内部状态 `(host, port)` 抽出来共享,外部状态(连接次数、超时、事务状态)留在连接对象那边,工厂加了锁保证并发安全,`shared_ptr` 保证所有权清晰。你想要的连接数、超时这种「每次不一样」的东西,不进享元,用的时候传进去就行——和棋子坐标是同一个套路。

## 享元模式不讨喜的地方

到这里我们有了一个正确、线程安全、所有权清晰的享元。和单例一样,享元也得诚实地说说它的代价,不能只讲好话。

**第一,你得先想清楚状态怎么拆。** 这是享元最大的门槛——把对象的字段划成「内部状态」和「外部状态」并不是总那么显而易见的事。拆错了,要么把本该共享的东西当外部状态传来传去、白白丧失共享收益,要么把本该变化的东西塞进享元、让一份对象被多处共享后互相干扰(这是更糟的 bug)。享元模式用得对不对,90% 取决于这刀切得对不对。

**第二,外部状态的传递是调用方的负担。** 享元把外部状态从对象里抠掉,代价是每次用都得重新传进来。一个 `draw(int x, int y)` 还好,要是外部状态有一大堆(位置、缩放、旋转、颜色 tint),调用点的签名会变得很臃肿,而且这些状态得存在调用方自己的数据结构里——你省了享元对象内部的空间,却在别的地方又存了一份外部状态的表。净收益是不是正的,得算总账。

**第三,它引入了一个全局可见的工厂。** 和单例的问题如出一辙,享元工厂本质上是一个有状态的共享设施,谁都能往里塞、谁都能从里取。一旦工厂的 key 设计得不合理(比如把可变状态拼进了 key),整个系统的行为就会变得难以追踪。测试时也不好替换——你很难给一个依赖全局工厂的模块塞一个假的享元池。

**第四,不是所有「相似对象」都值得享元。** 我们一开始就说过,享元有它的甜区:被共享的状态要「足够重」,对象数量要「足够多」。如果你手里是一堆字段各异、几乎不重复的对象,共享价值不大;又或者对象本身已经很轻(比如一个 `char`),强行套享元只会让代码更复杂、内存更大。上享元之前,先问自己一句:这部分状态,值得为它维护一个池子吗?

## 小结

我们把整条演进路径捋一遍:

| 阶段 | 做法 | 为什么还不够 |
|---|---|---|
| 每对象带全状态 | 字段全揉在一个类里 | 不变的内部状态被跟着对象数量一起复制 |
| 拆内外部状态 | 内部状态抽出,外部状态传参 | 还没人保证「同样的状态只造一份」 |
| 享元工厂 | find-or-insert 共享池 | 功能正确,但**并发下有 TOCTOU 竞态** |
| 线程安全工厂 | mutex 包住 find-or-insert | 够用,且 `shared_ptr` 让所有权清晰 |

记下这几条关键结论:

- **享元的核心是「内部状态共享 + 外部状态传参」**,判断一个对象适不适合享元,第一步永远是问自己:哪些字段是不变、可共享的?哪些是每次都变的?
- **享元工厂只在单线程下成立**,原版 find-or-insert 有 TOCTOU 竞态,并发下要用 mutex 包住。别被「池子终态大小正常」骗了,构造的副作用会重复。
- **用 `shared_ptr`,不要用裸指针**——享元是共享所有权,`shared_ptr` 让工厂和调用方各持一份、自动协调生命周期,既不泄漏也不悬空;`weak_ptr` 会让对象频繁回收重建,反而抹掉了享元省构造的收益。
- **享元有甜区**:共享的状态要足够重、对象数量要足够多,这笔交易才划算。ASCII char 这种已经够轻的,再上享元得不偿失。

::: tip 配套可编译工程
本节的例子在仓库 `code/volumn_codes/vol4/design-patterns/Flyweight/` 下有完整可编译工程(`.h` + main + `CMakeLists.txt`),`cmake -S . -B build && cmake --build build` 即可跑出上面这些输出。
:::

## 参考资源

- [cppreference:`std::shared_ptr`](https://en.cppreference.com/w/cpp/memory/shared_ptr)(共享所有权与引用计数,C++11 起)
- [cppreference:`std::unordered_map`](https://en.cppreference.com/w/cpp/container/unordered_map)(享元工厂常用的底层池)
- [cppreference:`std::mutex` / `std::lock_guard`](https://en.cppreference.com/w/cpp/thread/mutex)(线程安全工厂的同步原语)
- Gamma、Helm、Johnson、Vlissides,《Design Patterns》(GoF)Structural Patterns · Flyweight(原典,注意其示例代码使用裸指针,现代 C++ 应改用 `shared_ptr`)
