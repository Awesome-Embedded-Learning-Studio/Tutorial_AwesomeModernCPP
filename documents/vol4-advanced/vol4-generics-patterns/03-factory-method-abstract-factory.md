---
title: "工厂方法与抽象工厂:从一个 switch 到一族产品的创建"
description: "从最直觉的「在调用点写 switch new 出不同对象」起步,一步步逼出简单工厂、工厂方法,再到抽象工厂,看清它们各自到底在解决什么问题,最后用函数式工厂给出一个更轻的现代替代"
chapter: 11
order: 3
tags:
  - host
  - cpp-modern
  - intermediate
  - 工厂模式
difficulty: intermediate
platform: host
cpp_standard: [11, 17, 20]
reading_time_minutes: 24
related:
  - "单例模式:从注释约束到 Meyer's Singleton"
prerequisites:
  - "Chapter 6: 类与对象"
---

# 工厂方法与抽象工厂:从一个 switch 到一族产品的创建

## 我们到底在解决什么问题

我们先不急着上类图。想一个你大概率写过的一个类似的场景，笔者有点饿，咱们拿汉堡举例子：

程序里有一个抽象基类 `Burger`,底下挂着好几个具体子类——`CheeseBurger`、`BeefBurger`、`ChickenBurger`。现在业务层要根据某个偏好(用户选的、配置文件读的、数据库查出来的)造出一个具体的汉堡,然后吃掉它。最直觉的写法长这样:

```cpp
void enjoy_our_meals(std::vector<Person>& crowds) {
    for (auto& each_person : crowds) {
        Burger* p = nullptr;
        switch (each_person.prefer_type) {
            case BurgerType::Cheese:  p = new CheeseBurger;  break;
            case BurgerType::Beef:    p = new BeefBurger;    break;
            case BurgerType::Chicken: p = new ChickenBurger; break;
            // oh shit, 还有几十种汉堡要加
            // 有人会问我缺的智能指针这块谁给我补啊，我说别急，讲设计模式呢。
        }
        each_person.enjoy_burger(p);
        delete p;
    }
}
```

能跑,但你看一眼就知道哪里不对劲——**「到底 new 哪个具体子类」这件事,被焊死在了「吃饭」这个跟它八竿子打不着的函数里**。吃饭的函数本来只该关心「拿到一个汉堡、吃掉它」,现在它却要知道每一种汉堡的存在、要维护一个 `switch`、要管 `new` 和 `delete`。产品一加,这条 `switch` 就得动;产品换个构造方式(比如突然要传个参数),这条 `switch` 也得动;哪天你想给创建过程插一段日志,这个日志逻辑会被复制到每一个写了 `switch` 的地方。

事情的本质矛盾在于:**「使用一个对象」和「创建一个对象」是两件耦合方向完全相反的事**。使用方只想依赖一个稳定的抽象(`Burger`),它希望具体类型越少出现在自己视野里越好;而创建方必须知道每一个具体类型,因为「new 出哪个」恰恰是它的本职工作。把这两件事混在一起,就等于让使用方被迫继承创建方的所有易变性——产品越多,使用方越肿。

工厂模式要解决的就是这件事。它的核心一句话:**把「创建哪个具体对象」这件事,从使用方剥离出来,交给一个专门的对象/函数负责,让使用方只面对抽象**。接下来我们就一步步走,从最蠢的写法开始,看每一步为什么还不够,最后逼出 GoF 经典的「工厂方法」和「抽象工厂」,以及一个现代 C++ 里更轻的函数式替代。

## 第一步:最原始的剥离——简单工厂(静态 switch)

我们很快看出了上面那段代码的猫腻:`switch` 这块逻辑跟「吃饭」没关系,那把它抽出来不就行了?

```cpp
struct SimpleBurgerFactory {
    static std::unique_ptr<Burger> create(BurgerType t) {
        switch (t) {
            case BurgerType::Cheese:  return std::make_unique<CheeseBurger>();
            case BurgerType::Beef:    return std::make_unique<BeefBurger>();
            case BurgerType::Chicken: return std::make_unique<ChickenBurger>();
        }
        return nullptr;
    }
};

void enjoy_our_meals(std::vector<Person>& crowds) {
    for (auto& each_person : crowds) {
        auto burger = SimpleBurgerFactory::create(each_person.prefer_type);
        each_person.enjoy_burger(*burger);
    }
}
```

你看,`enjoy_our_meals` 立刻清爽了:它只调用 `create`,拿到一个 `Burger`,然后吃。**具体是哪个子类,「吃饭」的代码再也不关心**。以后要加一种新汉堡,改的只有工厂里那一条 `switch`;要给所有创建插一段日志,改的也只有工厂一处。顺手我们还把裸 `new` 换成了 `std::unique_ptr`,所有权清清楚楚——创建出来就交给调用方,工厂不持有它。

这就是**简单工厂(Simple Factory / 静态工厂)**。它解决了「使用与创建耦合」这个最痛的问题,绝大多数场景下它就够了。我们先把它的行为在编译器里跑通,确认这一步是真的 work:

```cpp
#include <iostream>
#include <memory>
#include <vector>

struct Burger {
    virtual ~Burger() = default;
    virtual std::string name() const = 0;
    virtual int price() const = 0;
};
struct CheeseBurger : Burger { std::string name() const override { return "CheeseBurger"; }
                                int price() const override { return 25; } };
struct BeefBurger   : Burger { std::string name() const override { return "BeefBurger"; }
                                int price() const override { return 32; } };
struct ChickenBurger: Burger { std::string name() const override { return "ChickenBurger"; }
                                int price() const override { return 28; } };

enum class BurgerType { Cheese, Beef, Chicken };

struct SimpleBurgerFactory {
    static std::unique_ptr<Burger> create(BurgerType t) {
        switch (t) {
            case BurgerType::Cheese:  return std::make_unique<CheeseBurger>();
            case BurgerType::Beef:    return std::make_unique<BeefBurger>();
            case BurgerType::Chicken: return std::make_unique<ChickenBurger>();
        }
        return nullptr;
    }
};

int main() {
    for (auto t : {BurgerType::Beef, BurgerType::Cheese, BurgerType::Chicken}) {
        auto b = SimpleBurgerFactory::create(t);
        std::cout << "got " << b->name() << ", price=" << b->price() << "\n";
    }
}
```

编译跑一下(GCC 16.1.1,C++23):

```sh
$ g++ -std=c++23 -O2 -Wall simple_factory.cpp -o simple_factory
$ ./simple_factory
got BeefBurger, price=32
got CheeseBurger, price=25
got ChickenBurger, price=28
```

行为完全正确。但简单工厂有个绕不开的毛病——**它违反开闭原则(OCP)**。那条 `switch` 是写在工厂内部的,工厂对「具体产品的存在」是全知的;每加一种新汉堡(`FishBurger`),你都得**打开工厂这个类、改它的源码**。工厂知道得越多、改得越勤,它就越脆弱。我们想要的是:加一个新产品,最好连工厂都不用动,只新增代码、不修改既有代码。这就是下一步要解决的。

## 第二步:把「创建哪个」交给子类——工厂方法

怎么做到「加产品不改工厂」?答案是**让工厂本身也变成一个抽象,每种产品配一个自己的具体工厂**。GoF 的工厂方法模式就这么来的:

```cpp
// 工厂接口:只定义「能造一个 Burger」,不规定造哪种
struct BurgerCreator {
    virtual ~BurgerCreator() = default;
    virtual std::unique_ptr<Burger> create() const = 0;
};

// 每种产品配一个具体工厂
struct CheeseBurgerCreator : BurgerCreator {
    std::unique_ptr<Burger> create() const override { return std::make_unique<CheeseBurger>(); }
};
struct BeefBurgerCreator : BurgerCreator {
    std::unique_ptr<Burger> create() const override { return std::make_unique<BeefBurger>(); }
};
struct ChickenBurgerCreator : BurgerCreator {
    std::unique_ptr<Burger> create() const override { return std::make_unique<ChickenBurger>(); }
};
```

用起来是这样,客户端手里捏的是 `BurgerCreator&`,完全不知道造出来的具体是哪种汉堡:

```cpp
void enjoy(const std::vector<std::unique_ptr<BurgerCreator>>& creators) {
    for (auto& creator : creators) {
        auto burger = creator->create();
        std::cout << "got " << burger->name() << "\n";
    }
}
```

这里先验证一下它真的能 work,而且客户端拿到的全是 `Burger` 抽象:

```cpp
int main() {
    std::vector<std::unique_ptr<BurgerCreator>> creators;
    creators.emplace_back(std::make_unique<CheeseBurgerCreator>());
    creators.emplace_back(std::make_unique<BeefBurgerCreator>());
    creators.emplace_back(std::make_unique<ChickenBurgerCreator>());

    int total = 0;
    for (auto& creator : creators) {
        auto burger = creator->create();   // 返回 unique_ptr<Burger>,具体类型被擦除
        std::cout << "got " << burger->name()
                  << ", price=" << burger->price() << "\n";
        total += burger->price();
    }
    std::cout << "total = " << total << "\n";
}
```

跑出来:

```sh
$ g++ -std=c++23 -O2 -Wall factory_method.cpp -o factory_method
$ ./factory_method
got CheeseBurger, price=25
got BeefBurger, price=32
got ChickenBurger, price=28
total = 85
```

### 工厂方法到底好在哪:扩展性账本

我们来算它的扩展性账本。**加一种新汉堡 `FishBurger`**:你新增 `FishBurger` 类 + `FishBurgerCreator` 类,然后在使用点把 `FishBurgerCreator` 塞进那个 `vector`——**`BurgerCreator` 接口一行都不用改,既有的任何 `Creator` 子类一行都不用改**。这就是 OCP 想要的「对扩展开放、对修改关闭」。简单工厂做不到这一点,因为它的 `switch` 是集中在工厂内部的;工厂方法把「创建哪个」的决定权下放到了一个个独立的工厂子类,于是新增产品就只是新增一个子类,不再触碰任何既有代码。

这是工厂方法相对简单工厂的本质区别,值得记死:**简单工厂是「一个工厂知道所有产品」,工厂方法是「每个产品有自己的工厂,谁都不必全知」**。前者改一处加一种产品(违反 OCP),后者加一个类加一种产品(符合 OCP)。代价是工厂方法类多——每种产品要配一个 `Creator` 子类,文件和类型数量翻倍。所以它不是免费的午餐,而是在「产品会持续增加、且不想频繁改动既有工厂」这个具体痛点上,用类的数量换 OCP。

### 配套可编译工程:工厂方法的真实长相

::: tip 配套可编译工程
上面这套工厂方法在本仓库里有一个完整的 CMake 工程可以跑。它用了一个比汉堡更贴切的例子——**`BurgerProvider` 是抽象工厂接口,`McBurgerProvider` 和 `BurgerKingProvider` 是两家连锁店的具体工厂**,各自负责造自己品牌的汉堡(同一个 `create_specifiedBurger("normal"/"cheese")` 接口,在不同店里产出完全不同品牌的产品)。clone 下来 cmake 一把就能跑:[FactoryBaseMethod / BurgerCreator](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/tree/main/code/volumn_codes/vol4/design-patterns/Factory/BurgerCreator)。
:::

我们把它的输出摘出来,你看 `McBurgerProvider` 和 `BurgerKingProvider` 虽然接口一模一样,造出来的东西完全不同:

```sh
$ ./BurgerCreator
Grilling McBurger...
Preparing McBurger with lettuce, tomato, and special sauce...
Wrapping McBurger in a paper wrapper...
Grilling McCheeseBurger...
Preparing McCheeseBurger with lettuce, tomato, cheese, and special sauce...
Wrapping McCheeseBurger in a paper wrapper...
Grilling Burger King Cheese Burger...
Wrapping Burger King Cheese Burger in a paper wrapper...
Grilling Burger King Burger...
Wrapping Burger King Burger in a paper wrapper...
```

这里 `BurgerProvider` 的设计点睛之处在于:客户端(`process_burger_session`)只调 `grill()/prepare()/wrap()` 三个抽象方法,它完全不知道手上这个汉堡是 `McBurger` 还是 `BurgerKingBurger`;**品牌差异被工厂彻底吸收了,客户端一个 `if` 都没写**。这就是工厂方法在真实业务里的价值——把「同一套操作、不同的具体实现」这层差异藏进工厂。

## 第三步:一次造一整套——抽象工厂

事情到这里还没完。汉堡店从来不只卖汉堡,它卖的是套餐——一个汉堡 + 一杯饮料,而且这两个产品得**风格一致**:经典套餐是「牛肉汉堡 + 可乐」,健康套餐是「鸡肉汉堡 + 果汁」。你不能让健康套餐里冒出可乐,也不能让经典套餐配果汁——**套餐内部的产品必须属于同一个家族**。

如果你给每个产品单独配工厂方法(`BurgerCreator` + `DrinkCreator`),客户端得自己负责「凑齐一套」,而凑的逻辑就散落在了客户端,「保证家族一致」这件事就没有人盯了。抽象工厂模式就是来堵这个洞的——**它把一族相关产品的创建接口打包在一起,一个具体工厂负责整套家族**:

```cpp
struct Drink {
    virtual ~Drink() = default;
    virtual std::string name() const = 0;
};
struct Cola  : Drink { std::string name() const override { return "Cola"; } };
struct Juice : Drink { std::string name() const override { return "Juice"; } };

// 抽象工厂:一族产品的创建接口打包在一起
struct MealFactory {
    virtual ~MealFactory() = default;
    virtual std::unique_ptr<Burger> create_burger() const = 0;
    virtual std::unique_ptr<Drink>  create_drink()  const = 0;
};

// 经典套餐工厂:整套家族保持「经典」风格
struct ClassicMealFactory : MealFactory {
    std::unique_ptr<Burger> create_burger() const override { return std::make_unique<CheeseBurger>(); }
    std::unique_ptr<Drink>  create_drink()  const override { return std::make_unique<Cola>(); }
};

// 健康套餐工厂:整套家族保持「健康」风格
struct HealthyMealFactory : MealFactory {
    std::unique_ptr<Burger> create_burger() const override { return std::make_unique<ChickenBurger>(); }
    std::unique_ptr<Drink>  create_drink()  const override { return std::make_unique<Juice>(); }
};
```

客户端只要拿到一个 `MealFactory`,就能一次性凑齐一套**风格保证一致**的套餐,完全不用自己去对账:

```cpp
void serve_meal(const MealFactory& factory) {
    auto burger = factory.create_burger();
    auto drink  = factory.create_drink();
    std::cout << "serving " << burger->name() << " + " << drink->name() << "\n";
}

int main() {
    ClassicMealFactory classic;
    HealthyMealFactory healthy;
    serve_meal(classic);
    serve_meal(healthy);
}
```

验证它:

```sh
$ g++ -std=c++23 -O2 -Wall abstract_factory.cpp -o abstract_factory
$ ./abstract_factory
serving CheeseBurger + Cola
serving ChickenBurger + Juice
```

注意两件事。第一,**「家族一致性」是抽象工厂免费送你的强约束**:你只要走 `ClassicMealFactory`,出来的就一定是「`CheeseBurger` + `Cola`」这一套,客户端根本没机会拼错。这件事是工厂方法做不到的——工厂方法只能保证单个产品对客户端隐藏,但「这一组产品属于同一风格」这个约束,它没有结构去表达。

第二,这套机制的代价立刻就能看出来:**加一个新产品族(比如再来个「豪华套餐」)很容易,新增一个 `LuxuryMealFactory` 就行;但加一种新产品类型(比如套餐里突然要再加一个「甜点」),就麻烦了**——你得回去改抽象工厂接口 `MealFactory` 加一个 `create_dessert()`,然后**每一个已有的具体工厂**都得回去补实现。这条账本正好和工厂方法反着来:抽象工厂对「加家族」开放,对「加产品种类」封闭。

### 工厂方法 vs 抽象工厂:别被名字骗了

很多资料把这两个混在一起讲,但它们的区别其实非常干净,一句话能说清:

- **工厂方法**关注的是**造「一个」产品**。抽象接口里只有一个 `create()`,具体工厂决定了造的是哪种具体产品。它解决的是「创建与使用解耦 + OCP」。
- **抽象工厂**关注的是**造「一族」产品**。抽象接口里有多个 `create_xxx()`,具体工厂决定了造的是哪一族。它额外解决的是「这一族产品风格一致」。

还有个结构上的小事实,理解了能帮你彻底分清:**抽象工厂的接口通常就是用工厂方法实现的**——`MealFactory` 里的 `create_burger()` 和 `create_drink()`,单独看每一个都是工厂方法。抽象工厂不是工厂方法的对立面,而是「把若干个工厂方法打包成一个接口」的结果。它们是同一套思想在不同尺度上的应用。

## 第四步:别写那么多类——函数式工厂

到这里你可能会皱眉:工厂方法/抽象工厂每加一种产品/家族就要新增一个类,文件膨胀得厉害。说实话,这些 `Creator` 子类里大多就一行 `return std::make_unique<...>()`,为了这一行去搭一整套继承体系,有点重。

现代 C++ 给了一条更轻的路:**工厂本质上就是个「能造对象的函数」,那就别用类,直接用 `std::function`/lambda**。我们维护一张表,把「key → 造对象的函数」登记进去:

```cpp
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

struct FunctionalBurgerFactory {
    using Creator = std::function<std::unique_ptr<Burger>()>;

    static void register_creator(const std::string& key, Creator c) {
        registry()[key] = std::move(c);
    }

    static std::unique_ptr<Burger> create(const std::string& key) {
        auto it = registry().find(key);
        if (it == registry().end()) return nullptr;   // key 不存在,安全地返回空
        return (it->second)();
    }

private:
    static std::unordered_map<std::string, Creator>& registry() {
        static std::unordered_map<std::string, Creator> r;  // Meyer's Singleton 持有注册表
        return r;
    }
};
```

注册和使用都直接写 lambda,没有继承,没有类爆炸:

```cpp
// 注册阶段:每加一种汉堡,这里登记一条 lambda
FunctionalBurgerFactory::register_creator("cheese",  [] { return std::make_unique<CheeseBurger>(); });
FunctionalBurgerFactory::register_creator("beef",    [] { return std::make_unique<BeefBurger>(); });
FunctionalBurgerFactory::register_creator("chicken", [] { return std::make_unique<ChickenBurger>(); });

// 使用阶段:按 key 拿产品
auto b  = FunctionalBurgerFactory::create("beef");
auto mx = FunctionalBurgerFactory::create("nope");   // 不存在的 key
```

这里先验证一下,重点看「不存在的 key」会怎样:

```cpp
#include <iostream>
// ... FunctionalBurgerFactory 定义 + 三个产品的注册 ...

int main() {
    FunctionalBurgerFactory::register_creator("cheese",  [] { return std::make_unique<CheeseBurger>(); });
    FunctionalBurgerFactory::register_creator("beef",    [] { return std::make_unique<BeefBurger>(); });
    FunctionalBurgerFactory::register_creator("chicken", [] { return std::make_unique<ChickenBurger>(); });

    auto b  = FunctionalBurgerFactory::create("beef");
    auto mx = FunctionalBurgerFactory::create("nope");
    std::cout << "'beef' -> " << (b  ? b->name() : std::string{"null"}) << "\n";
    std::cout << "'nope' -> " << (mx ? mx->name() : std::string{"null"}) << "\n";
}
```

跑出来:

```sh
$ g++ -std=c++23 -O2 -Wall functional_factory.cpp -o functional_factory
$ ./functional_factory
'beef' -> BeefBurger
'nope' -> null
```

`'nope'` 这个不存在的 key,`create` 返回了 `nullptr`,没有崩溃——这是 `std::unordered_map::find` 找不到就返回 `end()` 的标准行为,我们据此决定返回空指针。**这件事是运行时的,不是编译时的**,这是函数式工厂相对工厂方法的一个实实在在的代价,等下专门讲。

### 配套可编译工程:一个更工整的通知系统

::: tip 配套可编译工程
本仓库里有一个用函数式工厂实现的通知系统,值得单独看一眼。它的注册表是 `NocificationCreator` 的成员 `unordered_map<string, std::function<unique_ptr<AbstractNocification>()>>`,初始化时把 `Email/SMS/Push` 三种通知器各自的 lambda 登记进去,`notification_creator("Email")` 一查表就能拿出对应实现。clone 下来一把就能跑:[FactoryBaseMethod / NotificationSystem](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/tree/main/code/volumn_codes/vol4/design-patterns/Factory/NotificationSystem)。
:::

它的输出是:

```sh
$ ./NotificationSystem
[Email]: Welcome to our platform![SMS]: Welcome to our platform![Push]: Hey, New Message here!
```

你看,客户端完全不知道 `Email`、`SMS`、`Push` 这三个具体类存在——它只跟 `AbstractNocification` 的 `send_message` 打交道。加一种新通知器(`Webhook`),只要在注册表里加一条 lambda,**`NocificationCreator` 类本身、`main` 函数里的调用方式,一行都不用改**。这就是函数式工厂把 OCP 做到最轻的样子。

## 这里先验证一下:三种工厂的扩展性差异不是嘴上说说

口说无凭,我们把三种工厂在「加一种新产品」时的改动面摆出来对比。这是个结构事实,但为了让它落地,我们写一个最小的对照:用工厂方法时,加 `FishBurger` 需要动几处既有代码?

```cpp
#include <iostream>
#include <memory>
#include <vector>

struct Burger {
    virtual ~Burger() = default;
    virtual std::string name() const = 0;
};
struct CheeseBurger : Burger { std::string name() const override { return "CheeseBurger"; } };
struct BeefBurger   : Burger { std::string name() const override { return "BeefBurger"; } };
// 关键:新增 FishBurger 时,下面这行是「新增」,不是「修改既有」
struct FishBurger   : Burger { std::string name() const override { return "FishBurger"; } };

struct BurgerCreator {
    virtual ~BurgerCreator() = default;
    virtual std::unique_ptr<Burger> create() const = 0;
};
struct CheeseBurgerCreator : BurgerCreator { std::unique_ptr<Burger> create() const override { return std::make_unique<CheeseBurger>(); } };
struct BeefBurgerCreator   : BurgerCreator { std::unique_ptr<Burger> create() const override { return std::make_unique<BeefBurger>(); } };
// 新增 FishBurgerCreator:依然是「新增」,既有的 Creator 子类和 BurgerCreator 接口都没动
struct FishBurgerCreator   : BurgerCreator { std::unique_ptr<Burger> create() const override { return std::make_unique<FishBurger>(); } };

int main() {
    std::vector<std::unique_ptr<BurgerCreator>> creators;
    creators.emplace_back(std::make_unique<CheeseBurgerCreator>());
    creators.emplace_back(std::make_unique<BeefBurgerCreator>());
    creators.emplace_back(std::make_unique<FishBurgerCreator>());   // 装配点加一行
    for (auto& c : creators) std::cout << c->create()->name() << "\n";
}
```

编译跑一下:

```sh
$ g++ -std=c++23 -O2 -Wall ocp_check.cpp -o ocp_check
$ ./ocp_check
CheeseBurger
BeefBurger
FishBurger
```

这一小段把工厂方法的 OCP 账本坐实了:**加 `FishBurger` 这个过程里,`BurgerCreator` 抽象接口没改、`CheeseBurgerCreator` 和 `BeefBurgerCreator` 这两个既有工厂没改**——我们只新增了 `FishBurger`、`FishBurgerCreator` 两个类,以及在 `main` 的装配点加了一行。对比简单工厂,加 `FishBurger` 必须去改工厂类内部那条 `switch`;对比抽象工厂,如果 `FishBurger` 是新增的「产品种类」而不是新增「家族」,抽象工厂还要去改接口、改所有具体工厂。三者在「加产品」这件事上的改动面,差异就是这么具体、这么可量化。

## 工厂模式的另一面:集中追踪创建

到这里我们讲的都是「解耦」。但工厂模式还有个常被忽略的好处:**既然所有创建都集中在工厂里,工厂就是天然的对象审计切点**。给创建插日志、计数、计时,改工厂一处就够,不会散落到每一个 `switch` 里:

```cpp
struct TracingBurgerFactory {
    static std::unique_ptr<Burger> create(BurgerType t) {
        auto start = std::chrono::steady_clock::now();
        auto burger = SimpleBurgerFactory::create(t);   // 委托真实工厂
        auto end   = std::chrono::steady_clock::now();
        auto us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        std::cerr << "[factory] created " << burger->name()
                  << " in " << us << " us\n";
        return burger;
    }
};
```

这种「在工厂外面再包一层做横切关注点」的写法,本质上就是装饰器/代理模式叠在工厂上。它的前提恰恰是「创建已经被集中」——如果你还在调用点到处写 `switch`,这种统一的追踪根本无从下手。所以工厂模式不只是「让客户端少写 `switch`」,它还顺手给了你一个**所有对象诞生时都会经过的咽喉要道**,权限校验、监控、缓存、对象池,都可以挂在这里。

## 选哪个:一张决策表

我们一路从简单工厂走到函数式工厂,现在把它们摆在一起,看清楚各自适合什么:

| 维度 | 简单工厂 | 工厂方法 | 抽象工厂 | 函数式工厂 |
|---|---|---|---|---|
| 造的产品 | 单一产品 | 单一产品 | **一族产品** | 单一产品(按 key) |
| 加新产品 | 改工厂里的 `switch`(违反 OCP) | 新增一个 `Creator` 子类(符合 OCP) | 改接口 + 所有具体工厂(代价高) | 注册表里加一条 lambda |
| 加新家族 | — | — | 新增一个具体工厂类(符合 OCP) | — |
| 保证家族一致性 | 不能 | 不能 | **能(结构性强约束)** | 不能 |
| 类型安全 | 编译期(`switch` 漏 case 会警告) | 编译期(纯虚强制实现) | 编译期(纯虚强制实现) | **运行时**(key 拼错才在运行时炸) |
| 类的负担 | 一个工厂类 | 每产品一个工厂子类 | 每家族一个工厂子类 | 几乎无新增类 |

怎么选?我把判断逻辑浓缩成几句话。**绝大多数「按条件 new 不同子类」的需求,简单工厂就够**,别过度设计。**当产品会持续增加、且你不希望每加一种产品都要回头改工厂源码时,上工厂方法**,用类的数量换 OCP。**当你要造的是一族必须风格一致的产品(套餐、跨平台 UI 控件、多数据库方言)时,上抽象工厂**,它的杀手锏是「家族一致性」这个结构性约束。**当你的创建逻辑很轻、又想要 OCP 还不想养一堆单行子类时,用函数式工厂**,把工厂退化成一张 `key → lambda` 的注册表,但要清醒接受它的类型安全降级——key 错误只能运行时发现。

::: warning 函数式工厂的类型安全是降级的
工厂方法/抽象工厂的类型安全是编译期的:`BurgerCreator::create()` 是纯虚函数,你忘了在某个具体工厂里实现它,那个工厂就变成抽象类、根本实例化不了,编译器当场拦住你。函数式工厂不是这样——它的 key 是个字符串,你 `create("beef")` 拼成了 `create("beed")`,编译期完全查不出来,要等到运行时 `find` 查不到、返回 `nullptr`,再等你去解引用它时崩给你看。所以函数式工厂在「类型安全」这一项上是**实打实地退了一档**,用它就要在调用侧老老实实处理「key 可能不存在」(检查返回的 `unique_ptr` 是否为空,或干脆抛异常)。如果你的 key 来源是外部输入(配置文件、网络请求),这一步检查绝对不能省。
:::

## 踩坑预警:几个写法上的细节坑

::: warning 工厂必须返回 `unique_ptr<基类>`,而不是裸指针或 `unique_ptr<派生类>`
工厂方法返回 `std::unique_ptr<Burger>`(基类),这是有讲究的。第一,**别返回裸 `Burger*`**——调用方拿到裸指针就得自己记得 `delete`,一旦忘了就是内存泄漏,而且返回裸指针等于把所有权语义搞模糊了(谁拥有这个对象?)。`std::make_unique` + `unique_ptr<Burger>` 把所有权干净地移交给了调用方,RAII 自动回收。第二,**`std::make_unique<CheeseBurger>()` 能隐式转换成 `unique_ptr<Burger>`**,是因为 `unique_ptr` 有针对兼容指针类型的构造模板——但反过来(`unique_ptr<Burger>` 转成 `unique_ptr<CheeseBurger>`)是不行的,工厂返回的必须是基类指针。第三,**基类 `Burger` 的析构函数必须是 `virtual`**(`virtual ~Burger() = default;`),否则通过基类指针 `delete` 派生对象是未定义行为——这一点我们在单例和访问者里反复强调过,工厂模式下同样适用,因为 `unique_ptr<Burger>` 析构时正是通过基类指针销毁对象。
:::

::: warning 抽象工厂的「家族一致性」不等于「产品组合自由」
抽象工厂把一族产品的创建打包在一起,好处是「拿到一个具体工厂,出来的整套就一定是同一风格」,但代价是**它锁死了产品组合的自由度**。假设你想「经典套餐的汉堡 + 健康套餐的饮料」这种混搭,抽象工厂的结构是不支持的——你只能选一个工厂,拿它那一整套。如果你的业务需要产品级别的自由组合,抽象工厂就不是合适的选择,你该回到「每个产品一个工厂方法」,让客户端自己组合,代价是「家族一致性」这个约束你得不靠结构、改靠纪律去保证。别一看到「一族产品」就上抽象工厂,先问自己:我要的是「整套一致」还是「自由混搭」?
:::

::: tip 把工厂注册表和静态局部变量一起用,初始化是线程安全的
函数式工厂那张注册表,上面我们用了 `static std::unordered_map<...>& registry()` 配 Meyer's Singleton 的写法(函数内 `static` 局部变量)。这意味着注册表本身的初始化是线程安全的——C++11 的 magic statics 保证了「多个线程同时首次进入这条声明,只有一个会初始化」。但请注意:**注册表的初始化线程安全,不等于注册表内容的读写线程安全**。如果注册发生在程序启动后、且有多个线程并发往里 `register_creator`,你仍然要给注册表加锁(`std::shared_mutex` 适合「读多写少」的场景)。绝大多数工厂注册发生在 `main` 启动阶段(单线程),这时候不用管锁;一旦注册延后到运行时,锁就得补上。这条和单例那篇讲的 magic statics 是同一件事的两面。
:::

## 工厂 vs 构造器:别选错

我们这个子卷里还会讲到构造器(Builder)模式。工厂和构造器都在解决「对象创建」的问题,新手很容易选错,这里把区别说死:

- **工厂**关注的是**「造哪个」**——根据条件返回一个**不同**的具体子类,对象本身相对简单,一步造完。
- **构造器**关注的是**「怎么造」**——把一个**复杂**对象的构造过程分步展开(一堆 `set_xxx()` 链式调用,最后 `build()`),对象类型是确定的,但配置项繁多。

一个简单的判断:如果你纠结的是「该 new 哪个子类」,用工厂;如果你纠结的是「这个对象有十几个可选参数怎么配得清楚」,用构造器。两者也能结合——抽象工厂可以返回一个构造器,让客户端既能解耦具体类型、又能分步配置。

## 小结

我们把整条演进路径捋一遍:

| 阶段 | 做法 | 为什么还不够 |
|---|---|---|
| 调用点写 `switch new` | 在使用方直接 `switch` 决定 new 哪个 | 创建与使用耦合,使用方知道所有具体类型 |
| 简单工厂 | 把 `switch` 抽到一个静态工厂方法 | 加产品要改工厂内部(违反 OCP) |
| 工厂方法 | 抽象工厂接口 + 每产品一个具体工厂 | 单产品解耦够好,但造不了一族产品 |
| 抽象工厂 | 把一族产品的创建打包进一个接口 | 加家族容易,但加产品种类要改接口和所有具体工厂 |
| 函数式工厂 | `key → lambda` 注册表 | 类型安全降级(运行时查表) |

记下这几条关键结论:

- 工厂模式解决的是**「创建对象」与「使用对象」耦合**的问题——把「new 哪个具体子类」从使用方剥离,交给专门的工厂,使用方只面对抽象基类。
- **简单工厂**(一个静态方法里写 `switch`)解决最痛的耦合,但违反 OCP——加产品得改工厂内部。
- **工厂方法**(抽象 `Creator` + 每产品一个具体 `Creator`)用类的数量换 OCP——加产品只新增类,不改既有代码。
- **抽象工厂**把一族相关产品的创建打包,杀手锏是**「家族一致性」这个结构性强约束**(一个工厂出来的一整套必定风格统一);代价是加产品种类要改接口和所有具体工厂。
- 现代优先考虑**函数式工厂**:`key → lambda` 注册表,OCP 做到最轻、几乎没有新增类;但类型安全降级为运行时(拼错 key 运行时才炸),调用侧必须处理「key 不存在」。
- 别忘了工厂的另一个红利:**它是所有对象诞生的咽喉要道**,日志、计数、权限、缓存、对象池这些横切关注点,挂在这里一处就够。

::: tip 配套可编译工程
这一节的两个完整 CMake 工程在本仓库里,clone 下来 cmake 一把就能跑:工厂方法实现的 [BurgerCreator](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/tree/main/code/volumn_codes/vol4/design-patterns/Factory/BurgerCreator)(两家连锁店的具体工厂造各自品牌的汉堡)和函数式工厂实现的 [NotificationSystem](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/tree/main/code/volumn_codes/vol4/design-patterns/Factory/NotificationSystem)(`key → lambda` 注册表分发 Email/SMS/Push)。
:::

## 参考资源

- [cppreference:`std::unique_ptr`](https://en.cppreference.com/w/cpp/memory/unique_ptr)(C++11 起,工厂返回值所有权转移的标准载体)
- [cppreference:`std::function`](https://en.cppreference.com/w/cpp/utility/functional/function)(C++11 起,函数式工厂注册表的值类型)
- [cppreference:Virtual destructors](https://en.cppreference.com/w/cpp/language/destructor#Virtual_destructor)(工厂返回基类指针时,基类析构必须 `virtual`)
- [refactoring.guru:Factory Method](https://refactoring.guru/design-patterns/factory-method) / [Abstract Factory](https://refactoring.guru/design-patterns/abstract-factory)(GoF 工厂模式图解)
- GoF,《Design Patterns: Elements of Reusable Object-Oriented Software》—— 工厂方法与抽象工厂的原始定义
- 配套可编译工程:[FactoryBaseMethod](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/tree/main/code/volumn_codes/vol4/design-patterns/Factory)
