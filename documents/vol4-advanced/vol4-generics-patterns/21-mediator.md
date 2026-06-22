---
title: "中介者模式:把蛛网拆成星形"
description: "从最直觉的「控件互相持引用」开始,一步步逼出 Mediator 接口,用聊天室和对话框讲清星形耦合,再用 std::any 事件总线收尾"
chapter: 11
order: 21
tags:
  - host
  - cpp-modern
  - intermediate
  - 中介者模式
difficulty: intermediate
platform: host
cpp_standard: [11, 17, 20, 23]
reading_time_minutes: 22
related:
  - "单例模式:从注释约束到 Meyer's Singleton"
prerequisites:
  - "Chapter 6: 类与对象"
---

# 中介者模式:把蛛网拆成星形

## 我们到底在解决什么问题

我们先不上定义。想一个最常见的场景:你在写一个图书检索的对话框,上面有四个控件——标题输入框、作者输入框、候选列表、确认按钮。产品同学告诉你三条规则:用户在标题或作者框里打字时,列表要按输入实时过滤;标题和作者都填了,确认按钮才能点;点确认的时候,把当前列表里选中的那一条提交上去。

听起来一点都不复杂。于是你下意识地让这四个控件互相认识:`Textbox` 持有 `Listbox*` 和 `Button*`,`Listbox` 又持有两个 `Textbox*`,`Button` 持有 `Listbox*` 和两个 `Textbox*`。每个控件的回调里都直接伸手去改别人。写完第一天你觉得自己效率挺高,第二天产品同学加了第五个控件「高级筛选」,你发现四个老控件的构造函数签名全要改——因为新控件也得被它们看见。一周以后控件涨到八个,这张依赖图已经变成一团谁都剪不开的蛛网。

中介者模式要解决的就是这类需求:**把一组对象之间互相直接调用、互相持有引用的「网状耦合」,收拢成一个「星形耦合」——所有对象只跟一个中介者说话,由中介者负责把消息路由、把规则协调掉**。聊天室是它的招牌例子:用户不直接给别的用户递条子,而是把消息丢给聊天室,聊天室决定投给谁。GUI 对话框是 GoF 原书里的经典例子:每个控件只把「我变了」告诉对话框,对话框决定其它控件要不要跟着动。

但「加一个中间层」这五个字,在 C++ 里并不是「new 一个新类」那么简单。这里有几个非常容易踩的坑:很多人第一次写中介者,会让中介者反过来 `#include` 所有同事类,结果中介者自己变成了一个什么都知道的「上帝对象」,网状耦合只是从同事之间搬到了中介者肚子里;还有人为了让事件总线支持任意 payload,随手用 `void*` 或字符串来擦除类型,把编译期类型检查彻底丢了,运行期才炸。所以这一篇我们真正要回答的问题是——**怎么让同事对象只依赖一个抽象中介者接口、谁也不认识谁,同时不让中介者退化成上帝对象、不让类型擦除牺牲掉类型安全**。

接下来我们就一步步来,从最直觉的写法开始,看看每一步为什么不够,最后逼出一个现代 C++ 的标准答案。

## 第一步:最直觉的写法——控件互相持引用(反面教材)

很多人第一次遇到「四个控件要联动」,下意识写出来的代码是这样的:每个控件都把别的控件指针塞进自己,回调里直接伸手改。

```cpp
class Button;   // 互相前向声明,真到 .cpp 里就要互相 #include
class Textbox;
class Listbox;

class Listbox {
public:
    void bind(Button* b, Textbox* t) { button_ = b; textbox_ = t; }
    void on_selection_changed();
private:
    Button*  button_ = nullptr;
    Textbox* textbox_ = nullptr;
};

class Textbox {
public:
    void bind(Button* b, Listbox* l) { button_ = b; listbox_ = l; }
    void on_text_changed();
private:
    Button*  button_ = nullptr;
    Listbox* listbox_ = nullptr;
};

class Button {
public:
    void bind(Textbox* t, Listbox* l) { textbox_ = t; listbox_ = l; }
    void on_click();
private:
    Textbox* textbox_ = nullptr;
    Listbox* listbox_ = nullptr;
};
```

说实话,这种写法在「控件少、规则不会再变」的玩具场景里一点毛病没有,你别一看到模式就反射性地往上套。问题出在**需求开始扩张的那一刻**——当你加第六个控件,上面三个老控件的 `bind()` 签名全要改,因为新控件也得被它们看见;当你想改一条联动规则(比如「只有勾选了同意条款才能提交」),你会发现这条规则散落在 `Textbox::on_text_changed`、`Listbox::on_selection_changed`、`Button::on_click` 三个地方,改一处要确认另外两处不会跟着崩。

更糟的是编译期的问题。`Textbox` 要拿 `Button*` 就得在实现里看到 `Button` 的完整定义,`Button` 又要看到 `Textbox` 的完整定义——前向声明只能撑住指针的声明,撑不住成员函数里调用对方方法的需求,于是头文件之间就互相 `#include`,极易绕成循环依赖。我们在 /tmp 实测一下这种「互相持引用」的最小骨架能不能编过:

```sh
$ g++ -std=c++23 -O2 -Wall -Wextra -pthread /tmp/mediator_circular.cpp -o mediator_circular
OK: compiles, but bind() signatures are a combinatorial nightmare
```

能编过,但代价是——每加一个控件,N 个老控件的 `bind()` 全要扩参数,这是组合爆炸;每条规则改动,要在 N 个类里翻一遍。这张蛛网一旦织起来,后续每一步需求都在给它加丝,而不是在加功能。

所以这条路走不通。我们得让控件之间谁也别认识谁。

## 第二步:引入中介者接口——星形耦合

把问题收敛一下:既然控件之间互相直接引用是万恶之源,那我们就规定一条规矩——**控件只允许认识一个东西,那就是「中介者」**。至于中介者背后怎么路由、把消息转给谁,控件一概不管。

我们先定义一个抽象中介者接口。这里用最直白的「事件名 + 字符串」协议:控件发生变化时,告诉中介者「是我(id)变了」,具体怎么联动由中介者决定:

```cpp
struct IMediator {
    virtual ~IMediator() = default;
    virtual void notify(const std::string& sender, const std::string& event) = 0;
};
```

然后定义抽象同事基类。注意这里的关键一步——**控件只持有一个 `IMediator*`,完全不出现其它控件类型的名字**。这一行就是「星形耦合」的全部秘密:

```cpp
class Widget {
public:
    Widget(std::string id, IMediator* mediator)
        : mediator_(mediator), id_(std::move(id)) {}

    const std::string& id() const { return id_; }

protected:
    void notify(const std::string& event) {
        mediator_->notify(id_, event);   // 只跟中介者说话
    }

    IMediator* mediator_;
    std::string id_;
};
```

你看,`Widget` 现在只依赖 `IMediator` 这个抽象接口,既不知道有没有别的控件、也不知道那些控件叫什么。以后无论加多少个控件,`Widget` 这一行的签名一行都不用改。

### 用它搭一个聊天室

聊天室是这套结构最干净的演示。一个 `User` 就是一个同事,`ChatRoom` 就是中介者。`User` 想发消息时,只对中介者喊一嗓子「帮我转给某某 / 帮我广播」,根本不持有任何其它 `User` 的指针:

```cpp
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

struct IMediator {
    virtual ~IMediator() = default;
    virtual void send_message(const std::string& from,
                              const std::string& to,
                              const std::string& msg) = 0;
    virtual void broadcast(const std::string& from, const std::string& msg) = 0;
};

class User {
public:
    User(std::string name, IMediator* mediator)
        : name_(std::move(name)), mediator_(mediator) {}

    const std::string& name() const { return name_; }

    void send_to(const std::string& to, const std::string& msg) {
        mediator_->send_message(name_, to, msg);   // 转交中介者
    }
    void broadcast(const std::string& msg) {
        mediator_->broadcast(name_, msg);          // 转交中介者
    }
    void receive(const std::string& from, const std::string& msg) {
        std::cout << "[" << name_ << "] recv from " << from
                  << ": " << msg << "\n";
    }

private:
    std::string name_;
    IMediator* mediator_;   // ← 只依赖抽象,不知道其它 User 的存在
};
```

注意这里我们根本没出现 `User::send_to(const User&)` 这种签名——`User` 压根不知道世界上还有别的 `User`。所有「发给谁」「找不到怎么办」「要不要存日志」的决策,都被搬到了 `ChatRoom` 这一边:

```cpp
class ChatRoom : public IMediator {
public:
    void register_user(std::shared_ptr<User> user) {
        users_[user->name()] = std::move(user);
    }

    void send_message(const std::string& from,
                      const std::string& to,
                      const std::string& msg) override {
        auto it = users_.find(to);
        if (it != users_.end()) {
            it->second->receive(from, msg);
        } else {
            // 「目标不存在」的处理策略,集中放在中介者里
            std::cout << "[room] " << to << " not online (handled by mediator)\n";
        }
    }

    void broadcast(const std::string& from, const std::string& msg) override {
        for (auto& [name, user] : users_) {
            if (name != from) user->receive(from, msg);
        }
    }

private:
    std::unordered_map<std::string, std::shared_ptr<User>> users_;
};
```

`ChatRoom` 持有一张 `name -> User` 的表,私聊就是查表转发,广播就是遍历表(顺手把自己排除掉),查不到就由中介者自己决定怎么兜底。以后产品同学说要给私聊加敏感词过滤、要给所有消息落审计日志、要支持「对方不在线就存离线消息」——你只需要在 `ChatRoom` 这一个类里改,`User` 一个字都不用动。这就是星形耦合带来的开闭:规则演进只改中心,不波及叶子。

这里我们先验证一下,这套代码真的能跑,而且 `User` 之间确实没有任何直接引用:

```sh
$ g++ -std=c++23 -O2 -Wall -Wextra -pthread /tmp/mediator_verify.cpp -o mediator_verify
$ ./mediator_verify
[Bob] recv from Alice: hi Bob!
[Carol] recv from Bob: hello everyone, I'm Bob
[Alice] recv from Bob: hello everyone, I'm Bob
[room] Dave not online (handled by mediator)
```

跑通了。Alice 私聊 Bob 只命中 Bob;Bob 的广播同时送到 Alice 和 Carol、不回送自己;Carol 想私聊不存在的 Dave,中介者统一兜底成一句「不在线」。整条流程里 `User` 互相之间一行耦合都没有——这就是中介者模式把网状拆成星形之后该有的样子。

## 第三步:GoF 经典场景——对话框控件联动

聊天室是个温和的例子,真正的考验是 GoF 原书那个对话框:多个异构控件(`Textbox`、`Listbox`、`Button`)互相联动,规则还是非平凡的。这正是中介者模式诞生时就瞄准的场景,我们把它补全。

控件类型不同,但都从同一个抽象同事基类 `Widget` 来,统一用「通知中介者」这一个出口。不同控件把通知包进各自的业务方法里,对调用方来说接口仍然干净:

```cpp
class Textbox : public Widget {
public:
    using Widget::Widget;
    void set_text(const std::string& s) {
        text_ = s;
        notify("changed");    // 文本一变,告诉中介者
    }
    const std::string& text() const { return text_; }
private:
    std::string text_;
};

class Listbox : public Widget {
public:
    using Widget::Widget;
    void set_items(std::vector<std::string> v) {
        items_ = std::move(v);
        notify("changed");
    }
    const std::vector<std::string>& items() const { return items_; }
private:
    std::vector<std::string> items_;
};

class Button : public Widget {
public:
    using Widget::Widget;
    void enable()  { enabled_ = true; }
    void disable() { enabled_ = false; }
    bool enabled() const { return enabled_; }
    void click() {
        if (!enabled_) {
            std::cout << "[button] '" << id_ << "' disabled, ignored\n";
            return;
        }
        std::cout << "[button] '" << id_ << "' clicked -> submit\n";
    }
private:
    bool enabled_ = false;
};
```

接下来是具体中介者,把产品同学那三条规则全部集中到这里。注意中介者持有所有控件的指针,负责编排它们之间的联动——这是星形拓扑里「中心」该承担的角色,控件之间依然互不相识:

```cpp
class BookSearchDialog : public IMediator {
public:
    BookSearchDialog() {
        // 控件出生时就把自己交给中介者
        title_tb_  = std::make_unique<Textbox>("title",  this);
        author_tb_ = std::make_unique<Textbox>("author", this);
        list_      = std::make_unique<Listbox>("results", this);
        submit_btn_ = std::make_unique<Button>("submit", this);
    }

    void notify(const std::string& sender, const std::string& event) override {
        if (event != "changed") return;

        // 规则 1:输入变化 -> 按当前输入刷新候选列表
        std::cout << "[dialog] refilter by '"
                  << title_tb_->text() << "' / '"
                  << author_tb_->text() << "'\n";

        // 规则 2:标题和作者都非空,才允许提交
        if (!title_tb_->text().empty() && !author_tb_->text().empty()) {
            submit_btn_->enable();
            std::cout << "[dialog] submit enabled\n";
        } else {
            submit_btn_->disable();
        }
    }

    Textbox& title()  { return *title_tb_; }
    Textbox& author() { return *author_tb_; }
    Button&  submit() { return *submit_btn_; }

private:
    std::unique_ptr<Textbox> title_tb_;
    std::unique_ptr<Textbox> author_tb_;
    std::unique_ptr<Listbox> list_;
    std::unique_ptr<Button>  submit_btn_;
};
```

用起来就是这样,谁要联动就只跟对话框打交道:

```cpp
int main() {
    BookSearchDialog dlg;
    dlg.submit().click();                    // 空输入 -> 按钮 disabled,点不动
    dlg.title().set_text("C++");             // 只有标题 -> 仍 disabled
    dlg.submit().click();
    dlg.author().set_text("Stroustrup");     // 两个都有 -> enabled
    dlg.submit().click();                    // 这次能点
}
```

跑一下看看规则有没有真的生效:

```sh
$ g++ -std=c++23 -O2 -Wall -Wextra -pthread /tmp/mediator_dialog.cpp -o mediator_dialog
$ ./mediator_dialog
[button] 'submit' disabled, ignored
[dialog] refilter by 'C++' / ''
[button] 'submit' disabled, ignored
[dialog] refilter by 'C++' / 'Stroustrup'
[dialog] submit enabled
[button] 'submit' clicked -> submit
```

每一步都对上了:两个输入框都空时,点提交被拒;只填标题时,中介者通知规则 2 把按钮重新禁掉;两个都填满,中介者点亮按钮,这次点击才真正提交。整个过程里 `Textbox`、`Listbox`、`Button` 三个具体类互相之间没有任何 `#include`——它们只 `#include` 了 `IMediator` 这个抽象头。原来散落在三个类里的三条规则,现在被整齐地码在 `BookSearchDialog::notify` 一个函数里,以后产品同学要把规则改成「还得勾选同意条款」,你只动这一个函数。

## 第四步:事件总线——类型擦除的中介者

到这一步我们已经有了一个干净的星形结构。但你会发现一个新瓶颈:`BookSearchDialog::notify` 里那串 `if (sender == "title")` 是按字符串路由的,这玩意儿一点都不类型安全,而且每加一种新事件就要在中介者里改一次 `notify` 的分支。换句话说,这版中介者对「新增事件类型」并不开放——它违背了开闭原则的另一面。

更现代的做法是把中介者升级成「事件总线」:让「事件类型」本身成为协议,中介者只负责「谁订阅了哪类事件,我就把那类事件投给谁」。发布者和订阅者只共享事件类型,不共享任何接口。要做到「任意类型都能当 payload 跑」,我们需要类型擦除——C++17 起标准库给的现成工具是 `std::any`,配合 `std::type_index` 当作哈希键来按事件类型分桶:

```cpp
#include <any>
#include <functional>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <vector>

class EventBus {
public:
    template <typename Event>
    void subscribe(std::function<void(const Event&)> handler) {
        // 把「具体类型 handler」擦除成「吃 std::any 的统一 handler」
        handlers_[std::type_index(typeid(Event))]
            .push_back([h = std::move(handler)](const std::any& payload) {
                h(std::any_cast<const Event&>(payload));
            });
    }

    template <typename Event>
    void publish(const Event& event) {
        auto it = handlers_.find(std::type_index(typeid(Event)));
        if (it == handlers_.end()) return;   // 无人订阅 -> 安全忽略
        for (auto& h : it->second) h(event);
    }

private:
    using ErasedHandler = std::function<void(const std::any&)>;
    std::unordered_map<std::type_index, std::vector<ErasedHandler>> handlers_;
};
```

这段代码的妙处在于,类型擦除发生在 `subscribe` 内部:外面注册的是强类型的 `std::function<void(const MessageSent&)>`,被一个 lambda 包住、转型成吃 `std::any` 的统一签名再存进表里;`std::any_cast<const Event&>` 的恢复又发生在同一个 lambda 的闭包里,这个 `Event` 是编译期就定死的。所以发布者、订阅者写代码时看到的都是强类型,擦除是中介者内部的事,对用户不可见。

事件就是普通的值类型,不继承任何接口:

```cpp
struct MessageSent { std::string from; std::string body; };
struct UserLogin   { std::string who; };

int main() {
    EventBus bus;
    int analytics_count = 0;

    bus.subscribe<MessageSent>([&](const MessageSent& e) {
        std::cout << "[logger] " << e.from << " -> " << e.body << "\n";
    });
    bus.subscribe<MessageSent>([&](const MessageSent&) { ++analytics_count; });
    bus.subscribe<UserLogin>([&](const UserLogin& e) {
        std::cout << "[presence] " << e.who << " online\n";
    });

    bus.publish(MessageSent{"Alice", "hello world"});
    bus.publish(UserLogin{"Bob"});
    bus.publish(MessageSent{"Alice", "again"});

    std::cout << "analytics_count = " << analytics_count << " (expect 2)\n";
}
```

跑一下:

```sh
$ g++ -std=c++23 -O2 -Wall -Wextra -pthread /tmp/mediator_eventbus.cpp -o mediator_eventbus
$ ./mediator_eventbus
[logger] Alice -> hello world
[presence] Bob online
[logger] Alice -> again
analytics_count = 2 (expect 2)
```

`MessageSent` 被两个订阅者各收到一次(日志器和分析计数器),`UserLogin` 只被 presence 订阅者收到。注意一个细节——发布一个没人订阅的事件(`UserLogin` 第一次发布前没人订阅,或反过来)不会报错,直接被总线安全忽略,这正是事件总线相对直接调用的弹性所在:发布者不需要知道有没有人关心,只管喊。

事件总线相对前面的 `BookSearchDialog` 有个本质进步:**新增一种事件类型,中介者一行代码都不用改**。你只要 `struct` 一个新事件,`subscribe` / `publish` 就直接能用。这是因为它把「协议」从「中介者的成员函数签名」降级成了「事件值类型」,对扩展彻底开放。

## 这里先验证一下:类型擦除的代价

::: warning 类型擦除不是免费的
事件总线把 payload 用 `std::any` 擦除,代价是**订阅时写错事件类型,编译期发现不了,要等到 `std::any_cast` 那一刻才抛 `std::bad_any_cast`**。我们专门写一段验证一下这个失败模式,免得你在生产里被坑:

```cpp
struct Ping { int x; };
struct Pong { double y; };

int main() {
    std::any a = Ping{42};
    try {
        // 订阅者误以为 payload 是 Pong —— 编译器一声不吭
        const Pong& bad = std::any_cast<const Pong&>(a);
        std::cout << "no throw? y=" << bad.y << "\n";
    } catch (const std::bad_any_cast& e) {
        std::cout << "caught bad_any_cast: " << e.what() << "\n";
    }
    const Ping& ok = std::any_cast<const Ping&>(a);
    std::cout << "ok.x = " << ok.x << "\n";
}
```

```sh
$ g++ -std=c++23 -O2 -Wall -Wextra -pthread /tmp/mediator_any_badcast.cpp -o any_badcast
$ ./any_badcast
caught bad_any_cast: bad any_cast
ok.x = 42
```

实测就是这样:存进去的是 `Ping`,你按 `Pong` 取,运行期抛 `bad_any_cast`,编译期一行警告都没有。所以事件总线换来的是「对事件类型扩展开放」,代价是「订阅端类型匹配的检查从编译期推迟到了运行期」。对低频、可观测的业务事件(登录、发消息、下单)这点开销完全可接受;但对高频热路径或对延迟敏感的路径,你要权衡一下这层 `std::any` 的拷贝和 `type_index` 查表到底值不值。
:::

另外,事件总线有个比类型擦除更隐蔽的坑——**生命周期**。我们在上面用 `[&]` 捕获了局部变量 `analytics_count`,这要求事件发布时 `analytics_count` 还活着。在真实系统里,订阅者往往是个长期对象,而事件可能在订阅者析构之后才被发布,这时 lambda 里捕获的 `this` 或引用就成了悬挂引用。事件总线本身不会替你管这件事,你要么让订阅者在析构时显式反订阅,要么用弱引用机制过滤掉已失效的订阅者——这和观察者模式的生命期问题同构,这里先标记,不展开。

## 中介者什么时候会反过来咬你

到这里我们已经有了一个干净的星形结构,还把它升级成了对扩展开放的事件总线。但事情到这里还没完——我得诚实地告诉你,**中介者模式有一个出了名的反噬:中介者自己会膨胀成上帝对象**。

原因很好理解:我们把所有「路由、规则、协调」逻辑都搬进了中介者,初衷是好的,但规则是会涨的。今天的 `BookSearchDialog` 只管三条联动规则,下个月产品加了「高级筛选面板」「最近搜索」「结果分页」「权限校验」,你很自然地又全塞进 `notify()` 里——半年以后,这个中介者持有了十几个控件指针,`notify()` 里挂了几十个 `if` 分支,它自己就变成了那张蛛网的中心,所有耦合都集中在它肚子里。我们辛辛苦苦把网状拆成了星形,结果星形的中心又肿成了新的网。

缓解这个反噬有几条思路。第一,**按领域拆成多个中介者**——不要让一个中介者管天管地,检索面板的联动用一个中介者,权限校验用另一个,事件总线当底座把它们串起来。第二,**把中介者内部的策略抽出去**——`BookSearchDialog` 里那套「两个输入都非空才允许提交」的规则,可以抽成一个 `SubmitPolicy` 策略对象,中介者只负责把控件事件丢给策略、把策略的结论施加回控件。这其实就是中介者和策略模式的合体:中介者管拓扑,策略管规则。第三,**能用事件总线就别用巨型 `notify`**——事件总线把「新增事件」的扩展点开放出去,本身就是抑制中介者膨胀的机制,新规则以「订阅一种新事件」的形式加入,而不是往 `notify()` 里塞新分支。

还有一条要心里有数:**中介者增加了一层转发,极端低延迟场景下是要付代价的**。普通业务事件这层 `std::function` + `std::any` 的开销可以忽略,但如果你在每帧渲染、每包网络收发的热路径上套中介者,那层类型擦除和间接调用的成本就可能显形。这类场景要么用直接的函数调用,要么让中介者退化成编译期路由(比如模板 + `if constexpr` 分发,把擦除省掉)。

## 小结

我们把整条演进路径捋一遍:

| 阶段 | 做法 | 为什么还不够 |
|---|---|---|
| 控件互相持引用 | 每个 `Widget` 持有其它控件指针 | 网状耦合,加控件要改 N 处 `bind`,易循环 `#include` |
| 抽象中介者接口 | 同事只持 `IMediator*`,星形耦合 | 规则集中在 `notify` 里,但按字符串路由、不类型安全 |
| GoF 对话框 | 具体中介者编排异构控件联动 | `notify` 对「新增事件」封闭,中介者易膨胀成上帝对象 |
| 事件总线 | `std::any` + `std::type_index` 类型擦除 | 对事件扩展开放,代价是类型检查推迟到运行期 |

记下这几条关键结论:

- **中介者的核心收益是把网状耦合收成星形**:同事对象只依赖一个抽象中介者接口,谁也不认识谁,新增同事不波及老同事。
- **抽象中介者接口必须只暴露抽象同事能理解的协议**(事件名、消息结构),绝不能让中介者接口反过来 `#include` 所有具体同事类,否则网状耦合只是搬了家。
- **事件总线用 `std::any` + `std::type_index` 做类型擦除**,把「新增事件」做成了对扩展开放,但订阅端类型匹配的检查从编译期推迟到了运行期(`std::bad_any_cast`)。
- **中介者最大的反噬是自己膨胀成上帝对象**,缓解办法是按领域拆多个中介者、把规则抽成策略对象、优先用事件总线而不是巨型 `notify`。
- 热路径慎用运行期擦除的中介者,那层 `std::function` + `std::any` 的间接调用在每帧/每包级别会显形。

::: tip 配套可编译工程
本节的例子在仓库 `code/volumn_codes/vol4/design-patterns/Mediator/` 下有完整可编译工程(`.h` + main + `CMakeLists.txt`),`cmake -S . -B build && cmake --build build` 即可跑出上面这些输出。
:::

## 参考资源

- [cppreference:`std::any`](https://en.cppreference.com/w/cpp/utility/any)(C++17 起,类型擦除容器)
- [cppreference:`std::type_index`](https://en.cppreference.com/w/cpp/utility/type_index)(用作 `unordered_map` 的键,按类型分桶)
- [cppreference:`std::bad_any_cast`](https://en.cppreference.com/w/cpp/utility/any/bad_any_cast)(类型不匹配时抛出)
- GoF,《设计模式:可复用面向对象软件的基础》—— Mediator(中介者)一章,对话框案例的原型
