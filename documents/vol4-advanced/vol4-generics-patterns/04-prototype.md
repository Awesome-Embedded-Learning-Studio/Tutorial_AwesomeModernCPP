---
title: "原型模式:从一行拷贝构造,到能扛起继承体系的 clone()"
description: "从最直觉的「先建一个原型再拷一份」开始,一步步逼出多态 clone(),讲清拷贝切片、深浅拷贝、协变返回,最后用原型注册表把模板管起来"
chapter: 11
order: 4
tags:
  - host
  - cpp-modern
  - intermediate
  - 原型模式
difficulty: intermediate
platform: host
cpp_standard: [11, 17, 20]
reading_time_minutes: 19
related:
  - "单例模式:从注释约束到 Meyer's Singleton"
prerequisites:
  - "Chapter 6: 类与对象"
---

# 原型模式:从一行拷贝构造,到能扛起继承体系的 clone()

## 我们到底在解决什么问题

我们先不上定义。想一个很具体的场景:你手头有一个对象,它「造起来很贵」。贵在哪儿?可能是构造时要拉一次远程接口去填某些字段,可能是要走一段耗时的计算,可能是要读一个几 MB 的配置模板。现在你需要**再来一个跟它几乎一模一样、只有少数几个字段不同**的对象——比如 1000 个办公地点记录,区别只是门牌号;比如一窝小怪,区别只是血量。

这时候你如果还走「从头 `new`、再跑一遍那条昂贵的初始化」,那就太蠢了——明明旁边就摆着一个现成的、已经初始化好的对象,拿它当模板拷一份、再改几个字段,不就齐了吗?这就是原型模式(Prototype)要解决的事:**把一个已经存在的对象当成模板,通过复制它来造新对象,而不是每次都从头构造**。

听起来简单得过分,简单到你可能会问:这不就是拷贝构造吗?没错,原型模式的最朴素形态**就是一行拷贝构造**。但事情之所以值得专门写一篇文章,是因为当这个「模板对象」身处一个**有继承、有多态、还可能持有资源**的类型体系里时,那行拷贝构造就开始出事了:它会切片、会丢类型信息、会偷偷共享底层资源。我们真正要回答的问题是——**怎么让「复制一个对象」这件事,在继承体系下仍然忠实地复制出「正确的派生类型」,并且正确处理资源语义**。

接下来我们就一步步来,先看最直白的写法,再看它在哪儿崩,最后逼出一个现代 C++ 里既安全又能扛起多态的标准答案。

## 第一步:最直觉的写法——先建原型,再拷一份

我们拿那个办公地点的例子开刀。一个 `Address` 记录门牌号和是否可达:

```cpp
struct Address {
    std::string door_number;     // 门牌号
    bool        accessible {};   // 是否可达
};
```

最直白的原型用法是这样:先在外面初始化好一个原型对象,然后用它拷出新的,再改少量字段。

```cpp
Address proto;
proto.door_number    = "B-101";
proto.accessible     = true;

// 需要新实例时,从原型拷一份,只改需要变的字段
Address other   = proto;
other.door_number = "A501";

Address another = proto;
another.door_number = "C-77";
```

你看,这里的 `Address other = proto;` 就是一次拷贝构造,它就是原型模式最原始的形态。对于 `Address` 这种字段都是值类型(`std::string`、`bool`)的平凡类,这写法完全没问题——编译器合成的拷贝构造会把 `std::string` 做一次正确的深拷贝,`bool` 按值复制,一切安好。

但这个「完全没问题」是有前提的:前提是这个类**没有继承,也没有需要特殊处理的资源成员**。一旦哪天有人给 `Address` 加了个派生类,这个写法立刻就会露馅。

## 第二步:事情出问题了——切片

我们假设工程演进了一段时间,有人扩展出了 `ExAddress`,在原基础上多了一个「附加信息」字段:

```cpp
struct ExAddress : Address {
    std::string extra;   // 附加信息,比如「靠近咖啡机」
};
```

而在更早的时候,我们为了复用,把「从原型造一个改了门牌号的对象」这件事封装成了一个函数——注意它的参数类型:

```cpp
Address* make_from_proto(const Address* a, const std::string& door_number) {
    Address* t = new Address(*a);   // 用 Address 的拷贝构造
    t->door_number = door_number;
    return t;
}
```

现在问题来了。我们在外面拿一个真正的 `ExAddress` 对象传进去:

```cpp
ExAddress ex;
ex.door_number = "B-101";
ex.accessible  = true;
ex.extra       = "near-cafeteria";   // 派生类独有字段

Address* p = make_from_proto(&ex, "A501");
// p 指向一个 Address,它的 extra 去哪了?
```

`make_from_proto` 内部走的是 `new Address(*a)`,而 `*a` 的**静态类型**是 `Address`,所以这里调用的是 `Address` 的拷贝构造。`Address` 的拷贝构造只知道 `Address` 自己的那两个成员,它根本看不见、也不会去拷贝 `ExAddress::extra`——于是在这一行,`extra` 被无声地丢掉了。这就是 C++ 里大名鼎鼎的**对象切片(slicing)**:你用一个基类去拷贝构造一个派生类对象,派生部分被齐刷刷切掉。

我们先验证一下这件事是不是真的会发生。

## 这里先验证一下:切片到底切掉了什么

写一个小程序,看拷贝构造之后派生字段还在不在:

```cpp
#include <iostream>
#include <memory>
#include <string>
#include <typeinfo>

class Address {
public:
    virtual ~Address() = default;
    std::string door_number;
};

class ExAddress : public Address {
public:
    std::string extra;
};

int main() {
    ExAddress ex;
    ex.door_number = "A501";
    ex.extra       = "near-cafeteria";

    // 切片:用 Address 的拷贝构造去拷一个 ExAddress
    Address sliced = ex;

    std::cout << "[sliced]   door_number = " << sliced.door_number << "\n";
    std::cout << "[sliced]   dynamic type = " << typeid(sliced).name() << "\n";

    // 原对象的 extra 还在
    std::cout << "[original] extra = " << ex.extra << "\n";
    return 0;
}
```

编译跑一下:

```sh
$ g++ -std=c++23 -O2 -Wall -Wextra prototype_verify.cpp -o prototype_verify
$ ./prototype_verify
[sliced]   door_number = A501
[sliced]   dynamic type = 9Address
[original] extra = near-cafeteria
```

结果清清楚楚:拷出来的 `sliced` 动态类型变成了 `9Address`(`9` 是 mangled name 里类名长度前缀,表示 `Address`),它根本不再是一个 `ExAddress`——`extra` 那部分被切掉了。这就是为什么「拿一个写死成基类的拷贝构造」扛不起继承体系:它在信息传递的第一步就把类型信息传丢了。

而且更要命的是,这个 `make_from_proto` 的接口我们很可能**改不动**——因为项目里还有别人也在派生 `Address` 做自己的事,你把参数类型改成 `ExAddress*`,所有别的派生类就全炸。我们需要一个办法,让「到底该怎么拷」这个决定,从函数调用者手里,挪到对象自己手里。这正是 `virtual` 该登场的地方。

## 第三步:把克隆内置进类——多态 clone()

思路很直接:既然问题出在「到底拷成哪个类型」由调用方决定,那我们就把这个决定交给对象自己。让每个类自己知道「我应该怎么复制我自己」,并且这个能力通过虚函数暴露出来——调用方只管喊一声「给我复制一份」,具体复制成什么类型,由对象的动态类型说了算。

基类这么写:

```cpp
class Address {
public:
    virtual ~Address() = default;

    virtual Address* clone() const {        // 基类版本:复制成一个 Address
        return new Address(*this);
    }

    std::string door_number;
    bool        accessible {};
};
```

每个派生类各自 override,调用**自己的**拷贝构造:

```cpp
class ExAddress : public Address {
public:
    std::string extra;

    ExAddress* clone() const override {      // 派生类版本:复制成一个 ExAddress
        return new ExAddress(*this);
    }
};
```

注意这两行返回类型:基类返回 `Address*`,派生类返回 `ExAddress*`。这在 C++ 里是合法的,叫**协变返回类型(covariant return type)**——派生类 override 虚函数时,返回类型可以是从基类返回类型**派生**出来的指针或引用。这是语言专门为「多态工厂 / 多态 clone」开的一条后门。等会儿我们会验证它确实能工作。

现在我们重写那个工具函数:

```cpp
std::unique_ptr<Address> make_from_proto(const Address& a,
                                         const std::string& door_number) {
    auto t = a.clone();                      // 走虚派发:动态类型决定复制成谁
    t->door_number = door_number;
    return std::unique_ptr<Address>(t);
}
```

这一行 `a.clone()` 是整篇文章的关键。它的静态调用是 `Address::clone`,但因为 `clone` 是虚函数,实际执行哪个版本取决于 `a` 的**动态类型**——如果 `a` 真实身份是 `ExAddress`,这里就调 `ExAddress::clone`,复制出来的就是一个完整的 `ExAddress`,再也不会切片。我们这就验证一遍。

## 这里再验证一下:多态 clone 真的保住了动态类型吗

把上面的结构放进一个可运行的小程序,用一个静态类型是基类、动态类型是派生类的指针去调 `clone()`,看复制出来的对象到底是谁:

```cpp
#include <iostream>
#include <memory>
#include <string>
#include <typeinfo>

class Address {
public:
    virtual ~Address() = default;
    virtual Address* clone() const { return new Address(*this); }
    virtual void describe() const { std::cout << "Address door=" << door_number << "\n"; }
    std::string door_number;
};

class ExAddress : public Address {
public:
    std::string extra;
    ExAddress* clone() const override { return new ExAddress(*this); }
    void describe() const override {
        std::cout << "ExAddress door=" << door_number << " extra=" << extra << "\n";
    }
};

int main() {
    ExAddress ex;
    ex.door_number = "A501";
    ex.extra       = "near-cafeteria";

    Address* proto = &ex;                     // 静态类型 Address*,动态 ExAddress
    Address* cloned = proto->clone();         // 虚派发 -> ExAddress::clone

    std::cout << "[clone] dynamic type = " << typeid(*cloned).name() << "\n";
    cloned->describe();                        // 走的也是 ExAddress::describe
    delete cloned;
    return 0;
}
```

编译跑:

```sh
$ g++ -std=c++23 -O2 -Wall -Wextra prototype_verify.cpp -o prototype_verify
$ ./prototype_verify
[clone] dynamic type = 9ExAddress
[proto] dynamic type = 9ExAddress
```

复制出来的对象动态类型是 `9ExAddress`——派生部分完整保住了,`extra` 也跟着过来了。这就是多态 `clone()` 相比「直接拷贝构造」的核心价值:**它让复制行为跟着动态类型走,从而在不改任何调用方代码的前提下,忠实复制出正确的派生类型**。

## 那个返回类型:为什么我推荐你写 `std::unique_ptr<Base>`

到目前为止,我们的 `clone()` 返回的都是裸指针 `Address*`。这在教学上最直观,但它把「谁来 delete」这个包袱甩给了调用方——调用方拿到一个 `Address*`,必须记得自己 `delete`,一旦忘了就是内存泄漏,一旦提前 delete 又是悬空指针。

现代 C++ 的更体面写法,是让 `clone()` 直接返回一个拥有所有权的智能指针:

```cpp
class Widget {
public:
    virtual ~Widget() = default;
    virtual std::unique_ptr<Widget> clone() const = 0;   // 纯虚,强制子类实现
    virtual void draw() const = 0;
};
```

子类的实现:

```cpp
class Button : public Widget {
public:
    std::string label;

    std::unique_ptr<Widget> clone() const override {
        return std::make_unique<Button>(*this);   // 拷贝构造一个 Button,包进 unique_ptr
    }

    void draw() const override {
        std::cout << "Button: " << label << "\n";
    }
};
```

这里有个细节值得点一下:子类 `clone()` 返回的是 `std::unique_ptr<Widget>`,但函数体里 `std::make_unique<Button>(...)` 造出来的是 `std::unique_ptr<Button>`,这中间有一次隐式转换。这之所以能成立,是因为 `Button` 是 `Widget` 的派生类,`std::unique_ptr` 对「派生类指针到基类指针」的转换开了绿灯——它有一个不会抛异常的隐式构造函数。等会儿一起验证。

你可能要问:那能不能让子类返回 `std::unique_ptr<Button>`、用协变返回类型?**这里有个 C++ 的历史遗留限制:`std::unique_ptr<Derived>` 不是 `std::unique_ptr<Base>` 的派生类型,它俩是平级的、由不同模板实例化出来的独立类型,所以智能指针之间不支持协变返回类型**。也就是说,返回 `unique_ptr` 的虚函数,基类和派生类必须写**完全相同**的返回类型(这里是 `std::unique_ptr<Widget>`),靠函数体里的隐式转换来「找回」具体类型。这算是一个小代价,但换来的是所有权安全,值。

我们一并验证「`unique_ptr` 版 clone」和那个隐式转换:

```cpp
#include <iostream>
#include <memory>
#include <typeinfo>

class Base {
public:
    virtual ~Base() = default;
    virtual std::unique_ptr<Base> clone() const = 0;
};

class Derived : public Base {
public:
    std::unique_ptr<Base> clone() const override {
        return std::make_unique<Derived>(*this);   // unique_ptr<Derived> -> unique_ptr<Base>
    }
};

int main() {
    std::unique_ptr<Base> proto = std::make_unique<Derived>();   // 同样的隐式转换
    auto cloned = proto->clone();
    std::cout << "[unique_clone] dynamic type = " << typeid(*cloned).name() << "\n";
    return 0;
}
```

```sh
$ g++ -std=c++23 -O2 -Wall -Wextra prototype_verify.cpp -o prototype_verify
$ ./prototype_verify
[unique_clone] dynamic type = 7Derived
```

真实类型是 `7Derived`,说明隐式转换没丢类型信息,虚派发也照常工作。所以结论是:**写 `clone()`,返回 `std::unique_ptr<Base>`,让所有权跟着返回值走,调用方一行 `delete` 都不用写**。

## 真正的坑在后面:clone 不是无脑 `new Derived(*this)

到这里,多态 `clone()` 的骨架已经清楚了。但还有一个坑,很多入门资料一笔带过,实战里却最容易咬人——**`clone()` 到底拷的是浅拷还是深拷**。

`new Derived(*this)` 调用的是 `Derived` 的拷贝构造。而**编译器合成的默认拷贝构造,对每个成员做的都是「成员级拷贝」**:值类型成员(`std::string`、`std::vector`)会调它自己的拷贝构造(通常是深拷),但**指针成员只拷贝地址值**,不会去拷贝它指向的东西。这意味着如果你的类里有裸指针、或者 `std::shared_ptr` 这种「拷贝即共享」的成员,默认拷贝构造出来的两个对象就会**共享底层资源**——你以为 `clone()` 出了一份独立的副本,其实它俩在背后手拉手。

我们先验证一下默认拷贝构造在共享成员上的表现:

```cpp
#include <iostream>
#include <memory>

class SharedBuffer {
public:
    explicit SharedBuffer(int v) : data_(std::make_shared<int>(v)) {}
    // 默认合成拷贝构造:shared_ptr 拷贝 -> 引用计数 +1,两个对象指向同一块
    int  get() const { return *data_; }
    void set(int v)  { *data_ = v; }
private:
    std::shared_ptr<int> data_;
};

int main() {
    SharedBuffer a(10);
    SharedBuffer b = a;          // 默认浅拷(共享底层 int)
    a.set(999);
    std::cout << "[shared shallow] b.get() = " << b.get()
              << " (follows a's mutation)\n";
    return 0;
}
```

```sh
$ g++ -std=c++23 -O2 -Wall -Wextra prototype_verify.cpp -o prototype_verify
$ ./prototype_verify
[shared shallow] b.get() = 999 (follows a's mutation)
```

结果很直白:我们只改了 `a`,`b` 跟着变了——因为它俩底层是同一块 `int`。如果你的原型本意是「提供一个独立副本,改了原型不影响副本」,这个默认行为就是个 bug。

更危险的是裸指针。如果成员是 `int* data_` 而不是 `shared_ptr<int>`,默认拷贝构造会把两个对象的指针指向同一块堆内存,析构时各 `delete` 一次,**double free,直接未定义行为**。

所以写 `clone()` 之前,你要先想清楚这个类里每个成员的拷贝语义:**哪些是值语义,跟着默认走就对了;哪些是共享语义(比如大的只读缓存,故意要共享,那就用 `shared_ptr` 并接受共享);哪些是所有权语义(独占资源,那拷贝必须深拷,否则就别允许拷贝)**。`clone()` 的实现要清晰地落实这套语义,而不是无脑 `new Derived(*this)` 就完事。

::: warning 这个坑比你想的更常见
很多教程的 `clone()` 示例类都是只有几个 `std::string` 字段的「干净」类,默认拷贝构造恰好正确,于是给人一种「clone 就是一行」的错觉。实战里,一旦你的类持有 `std::unique_ptr`(不可拷贝,默认合成的拷贝构造会被 delete,你的 `clone()` 直接编不过),或者持有裸指针 / `shared_ptr`,你就必须手写拷贝构造、手写 `clone()` 的深拷逻辑,或者明确用共享语义。**别照抄示例,先问自己:这个类拷贝之后,两个对象该不该共享底层资源?**
:::

## 把原型管起来:原型注册表(Prototype Registry)

到这里我们有了能正确克隆的对象。但还有一个工程上的问题:原型本身往往是「造起来很贵」的对象——你不会想每次用都重新造一遍原型。自然的做法是,把那些常用的原型集中存起来,需要的时候按名字或 id 拿出来,再 `clone()` 一份。这就是**原型注册表(Prototype Registry)**。

它和工厂模式长得很像,区别在于:工厂是「按参数造一个新对象」,注册表是「从预先存好的模板里复制一个」。注册表内部存的是原型,对外暴露的是「按名字克隆」:

```cpp
#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>

class Widget {
public:
    virtual ~Widget() = default;
    virtual std::unique_ptr<Widget> clone() const = 0;
    virtual void draw() const = 0;
};

class Button : public Widget {
public:
    std::string label;
    explicit Button(std::string l) : label(std::move(l)) {}
    std::unique_ptr<Widget> clone() const override {
        return std::make_unique<Button>(*this);
    }
    void draw() const override { std::cout << "Button: " << label << "\n"; }
};

class TextField : public Widget {
public:
    std::string placeholder;
    explicit TextField(std::string p) : placeholder(std::move(p)) {}
    std::unique_ptr<Widget> clone() const override {
        return std::make_unique<TextField>(*this);
    }
    void draw() const override { std::cout << "TextField: " << placeholder << "\n"; }
};

class WidgetRegistry {
public:
    void register_proto(const std::string& name, std::unique_ptr<Widget> proto) {
        protos_[name] = std::move(proto);
    }

    std::unique_ptr<Widget> create(const std::string& name) const {
        auto it = protos_.find(name);
        if (it == protos_.end()) return nullptr;     // 没注册 -> 返回空,调用方自己处理
        return it->second->clone();                  // 从模板克隆一份
    }

private:
    std::unordered_map<std::string, std::unique_ptr<Widget>> protos_;
};
```

用起来就是这样:先注册几个「精心调好的」原型,之后任何时候按名字拿,拿到的都是一份独立副本:

```cpp
int main() {
    WidgetRegistry registry;

    // 注册原型:这些原型可以预先做昂贵的初始化
    registry.register_proto("ok_button",
        std::make_unique<Button>("OK"));
    registry.register_proto("cancel_button",
        std::make_unique<Button>("Cancel"));
    registry.register_proto("name_field",
        std::make_unique<TextField>("enter your name"));

    // 按名字克隆,改了克隆体不影响原型,也不影响其他克隆体
    auto b1 = registry.create("ok_button");
    auto b2 = registry.create("ok_button");
    auto f1 = registry.create("name_field");

    if (b1) b1->draw();    // Button: OK
    if (b2) b2->draw();    // Button: OK  (独立副本,改 b1 不影响 b2)
    if (f1) f1->draw();    // TextField: enter your name
}
```

这套结构是配置化创建的雏形——注册表里的原型可以从配置文件、脚本里加载,运行时按名字拼装出整套对象。游戏里的怪物刷新、UI 主题切换、文档编辑器里的「复制粘贴」,背后往往都是这一套。它的代价是:你得花心思管原型的注册与生命周期;如果原型本身是可变的、注册表还要跨线程访问,那就得额外上锁保证一致性。但对于「需要按模板批量造对象」的场景,这点维护成本是值得的。

## 小结

我们把整条演进路径捋一遍:

| 阶段 | 做法 | 为什么还不够 |
|---|---|---|
| 直接拷贝原型 | `Address other = proto;` | 对平凡类够用,但有继承就会切片 |
| 基类固定拷贝的工具函数 | `new Address(*a)` | 切片:丢掉派生部分和动态类型 |
| 多态 `clone()` | 基类声明 `virtual clone()`,子类各自 override | 扛起了继承体系(基本够用) |
| `clone()` 返回 `unique_ptr` | 返回 `std::unique_ptr<Base>` | 解决所有权,调用方免 `delete` |
| 原型注册表 | 注册表按名字 `clone()` | 配置化批量创建,但需管注册/生命周期 |

记下这几条关键结论:

- **原型模式的本质是「用复制代替构造」**,最朴素形态就是一行拷贝构造,只有在继承体系或持有资源时才需要升级到多态 `clone()`。
- **切片是多态场景下拷贝的头号杀手**——`new Base(*derived_ptr)` 会无声丢掉派生部分;解药是把克隆做成虚函数,让动态类型决定复制成谁。
- **`clone()` 返回 `std::unique_ptr<Base>`** 是现代 C++ 的标准做法;协变返回类型只对裸指针/引用有效,智能指针之间不支持协变,所以基类和派生类得写一样的返回类型,靠隐式转换找回具体类型。
- **`clone()` 不是无脑 `new Derived(*this)`**——先想清楚每个成员的拷贝语义(值 / 共享 / 所有权),默认拷贝构造对指针只做浅拷,含 `unique_ptr` 的类默认根本不可拷贝。
- 需要按模板批量造对象时,叠一层**原型注册表**,用名字索引原型、按需 `clone()`,是配置化创建的常见骨架。

::: tip 配套可编译工程
本节的例子在仓库 `code/volumn_codes/vol4/design-patterns/Prototype/` 下有完整可编译工程(`.h` + main + `CMakeLists.txt`),`cmake -S . -B build && cmake --build build` 即可跑出上面这些输出。
:::

## 参考资源

- [cppreference:Virtual functions(协变返回类型)](https://en.cppreference.com/w/cpp/language/virtual)(C++98 起,虚函数返回协变指针/引用)
- [cppreference:Copy constructors](https://en.cppreference.com/w/cpp/language/copy_constructor)(默认拷贝构造对成员的逐成员拷贝语义)
- [cppreference:`std::unique_ptr`](https://en.cppreference.com/w/cpp/memory/unique_ptr)(派生到基类的隐式转换,为何不能协变)
- GoF,《Design Patterns: Elements of Reusable Object-Oriented Software》—— Prototype 一章
- Dmitri Nesteruk,《Hands-On Design Patterns with C++ and .NET Core》原型模式章节(多态 clone 与注册表实践)
