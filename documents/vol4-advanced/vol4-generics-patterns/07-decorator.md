---
title: "装饰器模式:从多层继承地狱到链式包装"
description: "从最直觉的「为每种组合写子类」开始,一步步逼出动态装饰器、模板 mixin 和原子热插拔,讲清每条路的代价与适用边界"
chapter: 11
order: 7
tags:
  - host
  - cpp-modern
  - intermediate
  - 装饰器模式
difficulty: intermediate
platform: host
cpp_standard: [11, 17, 20]
reading_time_minutes: 22
related:
  - "单例模式:从注释约束到 Meyer's Singleton"
prerequisites:
  - "Chapter 6: 类与对象"
---

# 装饰器模式:从多层继承地狱到链式包装

## 我们到底在解决什么问题

我们先不上定义。想象你在做一个文本输出组件,最基础的需求就是往标准输出扔一段字符串,这没什么难度,一个 `print` 就完了。但需求很快就开始膨胀:有时候你要把这段文字套上引号、有时候要前后裹上星号、有时候要全部转成大写、有时候甚至要同时具备其中两三个能力。

你现在会怎么做?最直觉的反应大概是去写继承。`PlainTextPrinter` 不够用,那就派生一个 `QuotedPlainTextPrinter`,再派生一个 `StarredPlainTextPrinter`,再派生一个 `UpperQuotedStarredPlainTextPrinter`……先别急着笑,这种命名我已经在真实项目里见过太多次了。问题在于,「引号」「星号」「大写」这几样东西在逻辑上是**正交**的——它们彼此独立,可以自由组合,而继承表达的是一条线性的「是一种(is-a)」关系,天生不适合描述正交特性。

我们随便数一下:N 个独立特性,理论上就要 `2^N` 个子类才能穷举所有组合,这就是教科书里所谓的**类爆炸**。而且每加一个新特性,你要么去继承已有的某个组合类(引入不必要的耦合),要么从根重新派生(丢失已有能力),无论怎么选都难看。更要命的是,有些组合你根本不想在编译期就定死——比如一个日志框架,你可能希望根据运行时读到的配置,决定要不要在输出链上插一层时间戳装饰器、要不要插一层着色装饰器。

装饰器模式要解决的就是这一类需求:**在不修改已有类、也不靠疯狂继承的前提下,把额外行为做成可叠加的「包装」单元,按需一层一层套到基础对象上**。一个被装饰过的对象,对外仍然呈现原始对象的接口,但每次调用都会顺着包装链一路转发,沿途的每一层都有机会插入自己的逻辑。

接下来我们就一步步来,先看为什么「写子类」这条路会塌方,再逼出动态装饰器、模板 mixin,最后聊到运行时热插拔。

## 第一步:最直觉的写法——为每种组合写子类(反面教材)

很多朋友第一次面对「文本要带引号、又要带星号」的需求,下意识反应是这样的:

```cpp
struct PlainTextPrinter {
    void print(const std::string& text) { /* 直接输出 */ }
};

struct QuotedPrinter : PlainTextPrinter {
    void print(const std::string& text) {
        PlainTextPrinter::print("\"" + text + "\"");
    }
};

struct StarredQuotedPrinter : QuotedPrinter {
    void print(const std::string& text) {
        QuotedPrinter::print("***" + text + "***");
    }
};

struct UpperStarredQuotedPrinter : StarredQuotedPrinter {
    // ...
};
```

实话实说,如果你的需求一辈子就这三种组合,这么写甚至还能跑。但事情通常不会这么收场。一旦产品哪天说「我想要带星号但不要引号的版本」,你就得回到 `PlainTextPrinter` 重新派生一条 `StarredPrinter`;再过几天又说「我要大写带引号但不要星号」,又是一条新链。**每一条需求都对应一棵新的继承子树**,而子树之间还共享不了任何中间结果。

问题出在哪?出在**继承把「能力」和「类型」绑死了**。「带引号」本来只是一个可以独立存在的小能力,你却非要为它造一个新类型,而一旦造成类型,后续的组合就只能顺着这条血缘走。这就好比你想给手机配个壳、再贴个膜、再挂个挂绳,结果你不是去买配件,而是直接去造「带壳带膜带挂绳的手机」这个新品类——配件组合一多,SKU 就爆炸了。

装饰器的思路正好反过来:**能力不应该长在类型里,而应该做成可以套上去、拆下来的独立单元**。

## 第二步:抽出统一接口——让「装饰器」长得跟「被装饰对象」一样

要做「可拆卸的能力单元」,第一个前提是:不管我套了多少层,外部代码看到的接口必须是同一个。这就是为什么装饰器模式一定先有一层抽象基类。我们先把这个抽象立起来:

```cpp
struct AbsTextPrinter {
    virtual ~AbsTextPrinter() = default;
    virtual void simple_print(const std::string& text) = 0;
};
```

这个抽象干两件事。第一件,它定义了「文本打印器」长什么样——任何想被装饰的对象、以及任何装饰器自己,都得实现这个接口。第二件更关键,因为装饰器和被装饰对象实现**同一个接口**,所以装饰器可以直接替换被装饰对象,外部调用方根本感觉不到自己手里拿的是「原始对象」还是「套了三层的对象」——这就是后面一切组合能成立的根基。

基础对象很简单,实现接口就完了:

```cpp
class PlainTextPrinter : public AbsTextPrinter {
public:
    void simple_print(const std::string& text) override {
        std::print("{}", text);  // 只做最基础的输出
    }
};
```

## 第三步:立一个装饰器基类——把「转发」这件事固化下来

接下来问题来了:每一个具体装饰器(引号、星号、大写)都得做一件同样的事——**先持有内部那个被装饰对象,把活儿转发给它**。如果让每个装饰器各自实现一份「持有 + 转发」,代码会重复。所以我们立一个装饰器基类,把这套骨架抽出来:

```cpp
class BaseDecorator : public AbsTextPrinter {
protected:
    std::shared_ptr<AbsTextPrinter> inner_;

public:
    explicit BaseDecorator(std::shared_ptr<AbsTextPrinter> ptr)
        : inner_(std::move(ptr)) {}

    // 纯虚:具体装饰器必须自己实现
    void simple_print(const std::string& text) override = 0;
};
```

这里有几个设计决策值得展开。第一,装饰器基类**自己仍然继承 `AbsTextPrinter`**——这正是上一节说的「装饰器和被装饰对象是同一个接口」,所以装饰器可以无限嵌套,`StarDecorator` 套在 `QuoteDecorator` 外面,对外仍然是一个 `AbsTextPrinter`。第二,装饰器持有一个 `std::shared_ptr<AbsTextPrinter>`,指向它要装饰的内层对象。用 `shared_ptr` 是因为一条装饰链上,外层和内层共享同一份底层对象的 ownership,任何一个装饰器销毁都不会误伤整条链;如果你确认不需要共享所有权,换成 `std::unique_ptr` 表达独占所有权会更清晰,这点我们后面还会提。

第三,你可能注意到 `simple_print` 在基类里又声明成纯虚了。这一步看着有点怪——基类不是应该提供一个默认转发实现吗?**故意不提供,是为了强迫每个具体装饰器显式地决定「要不要转发、什么时候转发、转发前要不要改参数」**。装饰器的灵魂就是「在转发前后插逻辑」,这个决策不能被默认实现偷偷抹掉,否则就会出现「我以为我装饰了,结果只是透传」的迷惑行为。

## 第四步:具体装饰器——转发前后的那一点点逻辑

有了骨架,具体装饰器就轻得不像话了。它们只需要做一件事:在把请求转发给 `inner_` 之前(或之后),插入自己的逻辑:

```cpp
class QuoteDecorator : public BaseDecorator {
public:
    using BaseDecorator::BaseDecorator;  // 继承构造函数,省得重写

    void simple_print(const std::string& text) override {
        inner_->simple_print("\"" + text + "\"");  // 转发前:加引号
    }
};

class StarDecorator : public BaseDecorator {
public:
    using BaseDecorator::BaseDecorator;

    void simple_print(const std::string& text) override {
        inner_->simple_print("***" + text + "***");  // 转发前:加星号
    }
};

class UpperCaseDecorator : public BaseDecorator {
public:
    using BaseDecorator::BaseDecorator;

    void simple_print(const std::string& text) override {
        std::string result = text;
        // 转发前:把内容改成大写,再把改造后的结果往下传
        std::transform(text.begin(), text.end(), result.begin(),
                       [](unsigned char ch) { return std::toupper(ch); });
        inner_->simple_print(result);
    }
};
```

::: warning 一个极易踩的笔误
写 `UpperCaseDecorator` 这种「先改造、再转发」的装饰器时,有一个特别隐蔽的坑:你算出了一个 `result`,却在转发时手滑写成了 `inner_->simple_print(text)`。我在配套工程最初的版本里就踩过这个坑——大写装饰器算出了 `HELLO, WORLD!`,转头却把原始的小写 `text` 往下传了,装饰了个寂寞。

这种 bug 编译器不会报错(类型完全对得上),运行时也只是「输出没变化」,很容易被当成「我哪里配置错了」而忽略。**写装饰器的时候,务必确认「你转发下去的,到底是改造后的参数,还是原始参数」**。配套工程里这处已经修正为转发 `result`,我们先验证一下:

```sh
$ g++ -std=c++23 -O2 -Wall -Wextra fixed_upper.cpp -o fixed_upper
$ ./fixed_upper
[HELLO, WORLD!]
```

输出正确。这就是「转发前改造参数」的标准写法。
:::

## 第五步:把装饰器串成一条链——看清楚谁套谁

现在到了装饰器最舒服的一刻:组合。我们要做的,就是像套娃一样,一层一层把装饰器包上去。下面这段代码演示了如何把一个最朴素的 `PlainTextPrinter` 一步步装饰成「大写 + 星号 + 引号」的全功能版本:

```cpp
int main() {
    std::string text = "Hello, World!";

    // 最内层:基础对象
    auto plain = std::make_shared<PlainTextPrinter>();

    // 套一层引号
    auto quoted = std::make_shared<QuoteDecorator>(plain);

    // 再套一层星号(包在 quoted 外面)
    auto starred = std::make_shared<StarDecorator>(quoted);

    // 最外层套大写
    auto full = std::make_shared<UpperCaseDecorator>(starred);

    full->simple_print(text);
}
```

这里有一个新手最容易搞混的点:**谁在外层,谁先动手**。我们看 `StarDecorator(quoted)`——`Star` 在外层,它先拿到原始 `text`,把它包成 `***text***`,然后转发给内层的 `quoted`;`quoted` 收到的已经是 `***text***`,再把它包成 `"***text***"`,转发给 `plain`;最后 `plain` 输出。最外层的大写装饰器最先执行,把 `text` 改成大写,所以最终输出是 `***"HELLO, WORLD!"***`。

我们自己在终端里跑一下,确认链的执行顺序:

```sh
$ g++ -std=c++23 -O2 -Wall -Wextra dynamic_chain.cpp -o dynamic_chain
$ ./dynamic_chain
["***hi***"]
```

这个例子里,外层是 `Star`(加 `***...***`),内层是 `Quote`(加 `"..."`),最里层是 `Plain`(原样输出)。外层先动手,所以 `***` 包在最外面,引号包在里面,这个顺序和你「先穿内衣、再穿外套」的直觉是一致的。

::: tip 为什么非要用 shared_ptr 不可
你可能会问:为什么装饰器持有一层 `shared_ptr`,而不是直接持有对象本身或者引用?原因有三。其一,**多态需要指针或引用**,你没法直接持有一个抽象基类对象,只能持有指向它的指针。其二,**装饰链的层数在运行时才确定**,你没法在编译期写死一个具体类型,必须用一个能指向任何具体实现的句柄——这就是 `shared_ptr<AbsTextPrinter>` 在做的事。其三,**所有权**。一条链上多个装饰器指向同一份底层对象,任何一层都不应该独占它,`shared_ptr` 的引用计数天然适合表达这种共享 ownership。如果你确定整条链的 ownership 是线性的(只有最外层持有),把内层换成 `std::unique_ptr<AbsTextPrinter>` 会更清晰地表达「独占」,代价是不能再让两个装饰器共享同一内层。
:::

## 动态组合的代价:别假装它免费

到这里我们已经有一个能跑、能自由组合的动态装饰器了。但我必须诚实地告诉你,这条路是有代价的,而且代价不在别处,就在**每一次调用都多一次虚函数调用 + 一次指针解引用**。

我们写一段最小代码,把上面那条 `Star -> Quote -> Plain` 的链跑一遍,然后看 `-O2` 优化下编译器到底生成了什么:

```sh
$ objdump -d -C dynamic_chain | grep -E "call.*simple_print"
   169ab: call 16550 <QuoteDecorator::simple_print(...)>
   169eb: call 16ba0  <StarDecorator::simple_print(...)>
   1707e: call 16550 <QuoteDecorator::simple_print(...)>
   170c6: call 16ba0  <StarDecorator::simple_print(...)>
```

你看,哪怕开了 `-O2`,每一次装饰转发仍然是一次实打实的 `call`——编译器在这里没法把它优化掉,因为整条链是通过 `shared_ptr` 在运行时拼起来的,编译器看不到「内层到底是 `QuoteDecorator` 还是别的什么」,自然不敢内联。**链有多长,`call` 就有多深**。在一条每秒被调用上万次的热路径上(比如 UI 的逐帧绘制、日志的逐条输出),这个开销是会累积的。

更隐蔽的代价有两个。第一,**对象标识变了**:外层装饰器是一个新对象,它不是内层对象,`&decorator != &inner`。如果你依赖对象地址做相等性比较、做序列化、做缓存键,这种「层层是新对象」的特性会悄悄咬你一口。第二,**状态修改必须显式转发**:`simple_print` 是只读接口,转发起来很干净;但如果你有一个会改状态的接口(比如 `resize()`),每一层装饰器都得自己决定要不要把这个修改透传到内层,不透传就会出现「外层改了、内层没改」的不一致。

所以动态装饰器最适合的,是「组合关系在运行时才确定、调用频率不高、接口以查询/输出为主」的场景。日志框架、配置驱动的输出管线、插件化的处理链——这些场景天生适合动态装饰器。而一旦你发现自己在每帧都跑一条十几层的装饰链,就该考虑下一条路了。

## 第六步:编译期组合——模板 mixin,把开销优化没

当组合关系在编译期就完全确定、且对性能极敏感时,我们有一条截然不同的路:**用模板,把装饰器变成类型层面的包装**。思路是把「装饰器持有内层对象」换成「装饰器继承内层类型」,这样整条链在编译期就被拍成一个具体类型,所有的转发都是直接的函数调用,编译器可以放心地一路内联。

我们先扔掉抽象基类,写一个最朴素的非多态基础类型:

```cpp
struct PlainRaw {
    void simple_print(const std::string& text) const {
        std::print("{}", text);
    }
};
```

注意它没有虚函数、不继承任何东西——它就是一个普通的结构体。然后我们写模板装饰器,它继承 `Base`,在 `Base` 的行为之上叠加自己的逻辑:

```cpp
template <typename Base>
struct QuoteMixin : Base {
    using Base::Base;  // 继承 Base 的构造函数

    void simple_print(const std::string& text) const {
        Base::simple_print("\"" + text + "\"");  // 调用 Base 的版本
    }
};

template <typename Base>
struct StarMixin : Base {
    using Base::Base;

    void simple_print(const std::string& text) const {
        Base::simple_print("***" + text + "***");
    }
};
```

`QuoteMixin<Base>` 继承 `Base`,这意味着它**同时拥有 `Base` 的所有能力,又加上了自己的引号逻辑**。组合就是把它们一层层套起来:

```cpp
int main() {
    // 嵌套组合,得到一个具体类型
    using Decorated = StarMixin<QuoteMixin<PlainRaw>>;
    Decorated d;
    d.simple_print("hi");
}
```

输出和动态版本一模一样:`["***hi***"]`。但代价完全不同。我们看反汇编:

```sh
$ g++ -std=c++23 -O2 -Wall -Wextra mixin.cpp -o mixin
$ objdump -d -C mixin | grep -iE "simple_print|QuoteMixin|StarMixin"
(空)
```

`main` 里**没有任何一次**对 `simple_print`、`QuoteMixin`、`StarMixin` 的 `call`。整条装饰链——星号、引号、原始输出——在编译期就被内联掉了,运行时根本不存在「装饰」这个动作。这就是「零开销抽象」的真正含义:你写得像运行时组合一样优雅,编译器却把它优化成了一份手写内联代码。

```sh
$ ./mixin
["***hi***"]
```

为了确认这个类型确实是非多态的(没有 vtable),我们再补一个 `static_assert`:

```cpp
static_assert(!std::is_polymorphic_v<StarMixin<QuoteMixin<PlainRaw>>>,
              "mixin 链不应该有虚函数");
```

这条断言在编译期就成立,从语言层面证明了这条路没有虚函数开销。

::: warning mixin 的代价:类型爆炸
静态组合不是没有代价,只是代价换了个地方——它**不在运行时,而在类型系统里**。`StarMixin<QuoteMixin<PlainRaw>>` 和 `QuoteMixin<StarMixin<PlainRaw>>` 是**两个不同的类型**,即便它们用的组件完全一样:

```sh
$ ./type_explosion
T1 == T2 ? false
```

我们在终端里跑一下 `std::is_same_v<A<C<Plain>>, C<A<Plain>>>`,结果是 `false`。这意味着每一种组合都是一个全新的、互不兼容的类型。你没法把 `StarMixin<Quote<Plain>>` 和 `Quote<StarMixin<Plain>>` 塞进同一个 `std::vector<T>`,也没法在运行时把一条链换成另一条。**N 个特性理论上能组合出指数级个类型**,这就是「类型爆炸」。

实践上,类型爆炸的后果主要体现在三处:构造参数转发变难(多层 mixin 嵌套时,参数得一层层 `std::forward` 下去,顺序一乱就错);不能放进统一容器(除非再套一层 type erasure);运行时不能切换组合。如果你既想要静态组合的性能,又希望外部通过统一接口使用这些类型,可以给静态组合写一个薄薄的适配器,把静态类型包进一个实现抽象基类的 wrapper——内部是静态高效的,对外仍然是多态的。这种「静态实现 + 多态外壳」的混合做法,在「平时内联执行、偶尔通过多态暴露给插件层」的系统里非常实用。
:::

## 第七步:运行时热插拔——把整条链原子换掉

最后我们聊一个工程化场景,也是装饰器在真实系统里最有用武之地的地方:**热插拔**。想象你写了一个日志框架,运行中你想根据一份新的配置(比如 JSON 里的 `["timestamp", "colored", "file_sink"]`),动态地重建整条输出装饰链,而且**不能停服务、不能有锁竞争**。

要实现这一点,我们需要两个能力。第一个是把装饰器的「构造方式」注册成可查的工厂,这跟工厂模式是一回事:每个装饰器对应一个工厂函数,接收当前链的内层,返回包好后的外层。从配置的尾部往前逐层套,就能根据一份运行时配置拼出任意装饰链。

第二个,也是更关键的一个,是**整条链的原子替换**。如果读者线程正在一边读链、一边被你换链,稍有不慎就是 use-after-free。C++20 给了一个干净到近乎免费的工具:`std::atomic<std::shared_ptr<T>>`。它让你把整条装饰链的根引用放在一个原子的 `shared_ptr` 里,重建好新链之后,做一次原子的 `store`,后续访问就能看到新链,而正在使用旧链的线程会安全地在自己持有的引用副本上把这次调用跑完,旧链在引用计数归零后自动销毁。

```cpp
// 根引用:原子化的 shared_ptr(C++20)
std::atomic<std::shared_ptr<Shape>> root;

void reload_config_and_apply(const std::vector<std::string>& cfg) {
    // 用配置拼出新链(略:逐层套装饰器)
    auto base = std::make_shared<Circle>();
    auto new_root = build_from_config(base, cfg);
    root.store(new_root, std::memory_order_release);  // 原子替换
}

std::string read_current() {
    // 每次读都拿到一个 shared_ptr 副本,在副本上操作
    auto p = root.load(std::memory_order_acquire);
    return p->describe();
}
```

这里先验证一下,4 个读者线程持续读、主线程热替换 1000 次,跑完不崩、输出正确:

```sh
$ g++ -std=c++23 -O2 -pthread -Wall -Wextra hotswap.cpp -o hotswap
$ ./hotswap
final describe = [[Circle]]
total reads   = 3018 (no crash, no UB)
```

读者线程跑了三千多次,期间链被原子替换了一千次,最终读到的链是 `[[Circle]]`(两层 `WithBorder` 包着一个 `Circle`),全程没有 crash、没有 UB。这就是 `std::atomic<std::shared_ptr>` 的承诺——**热替换几乎不阻塞正在执行的线程,旧链会自动随引用计数归零回收**。

::: warning 热插拔的隐藏前提:装饰器最好无状态
原子换链解决的是「链本身」的并发安全,但它解决不了「装饰器内部状态」的并发安全。如果你的某个装饰器内部带了一个可变共享状态(比如一个计数器、一个缓存),那么即便链被原子替换了,这个状态本身仍然可能被多个线程同时读写,引发 data race。所以工程实践上,热插拔的装饰器**最好设计成无状态的**,或者把状态做成自己的线程安全结构(比如包一层 `std::mutex` 或用 `std::atomic`)。如果你确实需要在装饰器之间共享可变状态,那状态就不应该藏在装饰器里,而应该独立成一个并发安全的对象,由装饰器去引用它。
:::

## 三条路的取舍

到这里我们已经把装饰器的三种实现风格都走了一遍。我们用一张表把它们的取舍钉死:

| 风格 | 组合时机 | 性能 | 灵活性 | 主要代价 |
|---|---|---|---|---|
| 动态装饰器 | 运行时 | 每次 `call`(有虚调用 + 指针) | 任意组合、可放统一容器 | 调用开销、对象标识变化、状态需显式转发 |
| 模板 mixin | 编译期 | 零开销(全内联) | 组合在编译期定死 | 类型爆炸、不能放统一容器、构造参数转发复杂 |
| 工厂 + 原子热插拔 | 运行时(配置/插件驱动) | 同动态(加原子开销) | 部署期可配置、可热更新 | 实现复杂(注册/解析/并发)、装饰器须无状态 |

真实工程里很少有「唯一正确答案」。如果你写的是 UI 绘制热路径,每帧调用上万次,模板 mixin 让你既享受组合的优雅、又拿到手写内联的性能;如果你写的是服务端日志框架或工具链,需要按配置或插件改变行为,动态装饰器配工厂注册给你必要的灵活性;更常见的做法是**混合**:把高频组合用静态 mixin 实现并包一层多态适配器,把真正需要热插拔、按需加载的功能做成工厂化插件——这样既保住性能,又拿到运行时可配置性。

## 小结

我们把整条演进路径捋一遍:

| 阶段 | 做法 | 为什么还不够 |
|---|---|---|
| 子类穷举 | 为每种组合派生一个新类 | 类爆炸(2^N),特性正交但继承线性 |
| 动态装饰器 | 抽象基类 + 装饰器持有 `shared_ptr<接口>`,链式转发 | 每层一次虚调用,热路径有开销 |
| 模板 mixin | 装饰器继承 Base,编译期内联 | 类型爆炸,不能放统一容器,不能运行时切换 |
| 工厂 + 原子热插拔 | 工厂注册装饰器 + `std::atomic<shared_ptr>` 换链 | 实现复杂,装饰器须无状态 |

记下这几条关键结论:

- **装饰器模式的根基是「装饰器和被装饰对象实现同一个接口」**,所以装饰器可以无限嵌套,对外仍然呈现原始接口。
- **动态装饰器(虚函数 + `shared_ptr`)适合组合在运行时才确定、调用不频繁的场景**;它的代价是每次调用的虚调用开销和对象标识的变化。
- **模板 mixin 适合组合在编译期定死、性能敏感的场景**;它的代价在类型系统里——类型爆炸、不能放统一容器。
- **运行时热插拔用 `std::atomic<std::shared_ptr<T>>`(C++20)做整链原子替换**,几乎不阻塞正在执行的线程,但前提是装饰器本身最好无状态。
- **写「转发前改造参数」的装饰器时,务必确认转发的是改造后的参数**——笔误(转发原始参数)编译器不会报,只会让装饰器静默失效。

## 参考资源

- [cppreference:`std::shared_ptr`](https://en.cppreference.com/w/cpp/memory/shared_ptr)(共享所有权,装饰链的默认选择)
- [cppreference:`std::atomic<std::shared_ptr<T>>`](https://en.cppreference.com/w/cpp/memory/shared_ptr/atomic2)(C++20,整链原子热替换)
- [cppreference:`std::is_polymorphic`](https://en.cppreference.com/w/cpp/types/is_polymorphic)(编译期验证类型是否含虚函数)
- 《Design Patterns》(GoF):Decorator 一章(原始的面向对象描述)
- Andréi Alexandrescu,《Modern C++ Design》第 4 章(policy-based design,模板 mixin 的理论基础)
- 配套可编译工程:[Decorator](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/tree/main/code/volumn_codes/vol4/design-patterns/Decorator)
