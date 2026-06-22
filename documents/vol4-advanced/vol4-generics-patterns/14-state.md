---
title: "状态机模式:从一坨 if/else 到类型安全的状态对象"
description: "从最直觉的「状态用一个 enum 存着,行为用 switch 切」起步,一步步逼出面向对象的 State 模式、variant+visit 的类型安全状态机、再到表驱动转换,讲清三种写法各自的代价,最后看清楚什么时候该上状态机、什么时候别上"
chapter: 11
order: 14
tags:
  - host
  - cpp-modern
  - intermediate
  - 状态机
difficulty: intermediate
platform: host
cpp_standard: [11, 17, 20]
reading_time_minutes: 22
related:
  - "单例模式:从注释约束到 Meyer's Singleton"
  - "策略模式:从一堆 if/else 到编译期可替换的 Policy"
---

# 状态机模式:从一坨 if/else 到类型安全的状态对象

## 我们到底在解决什么问题

我们先不上定义。想一个特别常见的场景:你在写一个媒体播放器,它能被 `play()`、`pause()`、`stop()`。听起来没什么,但你坐下来一写就会发现问题——同样是 `play()`,在「已停止」时它该开始播放,在「正在播放」时它是个空操作,在「已暂停」时它该恢复播放。同一个事件,落到不同状态下,行为完全不同。

很多人第一反应是写一个 `enum` 把状态存起来,然后在每个方法里 `switch`:

```cpp
enum class PlayerState { Stopped, Playing, Paused };

class MediaPlayer {
public:
    void play() {
        switch (state_) {
            case PlayerState::Stopped:
                std::cout << "start playing\n";
                state_ = PlayerState::Playing;
                break;
            case PlayerState::Playing:
                std::cout << "already playing\n";  // 空操作
                break;
            case PlayerState::Paused:
                std::cout << "resume\n";
                state_ = PlayerState::Playing;
                break;
        }
    }
    void pause() { /* 又一个 switch,把上面再抄一遍 */ }
    void stop()  { /* 再抄一遍 */ }

private:
    PlayerState state_ = PlayerState::Stopped;
};
```

能跑。但状态一多,这种写法就会露出本来面目。假设你后来又加了「快进」「缓冲中」「出错」三个状态,加上原本三个事件,你的 `play()`、`pause()`、`stop()` 每一个都要在原来的 `switch` 上再开几个 `case`,而且**每加一个状态,每一个事件处理函数都得同时改**——你改漏一个 `case` 就是一个 bug,编译器还不会提醒你。更要命的是,「在 Playing 状态下收到 stop」这条转换规则,它的代码物理上散落在 `stop()` 里,和「在 Playing 状态下收到 pause」隔了十万八千里。你读代码的时候,想知道一个状态会怎么走,得把所有方法翻一遍;你改一条转换,又怕碰坏别的方法。

状态机模式要解决的就是这个纠缠。它的核心想法一句话就能说完——**把「状态」从一坨散落的 `case` 里抽出来,变成一个个独立的对象/类型,每个状态只关心自己怎么响应事件,转换规则被局部化在「从这个状态出发」的代码里**。这样「Paused 状态怎么响应事件」就全部待在 `PausedState` 这一个类里,你改它不会碰 `PlayingState` 一根毫毛;新增一个状态,就是新增一个类,别的状态一行都不用动。

但「把状态抽成对象」这件事,在 C++ 里有几条实现路径,代价完全不同。面向对象的 State 模式靠虚函数分派,运行时灵活但每次转换可能要堆分配;`std::variant` + `std::visit` 靠类型安全的闭集分派,没有虚调用也没有堆分配,但状态集合得在编译期定死;最朴素的 `enum` + `switch` 则干脆把状态全塞进一个整数,最快但最不抗变化。接下来我们就一步步来,先从那坨 `switch` 看起,看看它为什么不够,再一步步逼出后面的写法。

## 第一步:最原始的写法——enum + switch(反面教材)

我们把上面那个 `MediaPlayer` 写完整,带上三个事件,看看它到底哪里别扭:

```cpp
enum class PlayerState { Stopped, Playing, Paused };

class MediaPlayer {
public:
    void play() {
        switch (state_) {
            case PlayerState::Stopped:
                std::cout << "[Stopped] start playing\n";
                state_ = PlayerState::Playing;
                break;
            case PlayerState::Playing:
                std::cout << "[Playing] play() already playing\n";
                break;
            case PlayerState::Paused:
                std::cout << "[Paused] resume\n";
                state_ = PlayerState::Playing;
                break;
        }
    }
    void pause() {
        switch (state_) {
            case PlayerState::Stopped:
                std::cout << "[Stopped] pause() ignored\n";
                break;
            case PlayerState::Playing:
                std::cout << "[Playing] pausing\n";
                state_ = PlayerState::Paused;
                break;
            case PlayerState::Paused:
                std::cout << "[Paused] pause() already paused\n";
                break;
        }
    }
    void stop() {
        switch (state_) {
            case PlayerState::Stopped:
                std::cout << "[Stopped] stop() already stopped\n";
                break;
            case PlayerState::Playing:
                std::cout << "[Playing] stopping\n";
                state_ = PlayerState::Stopped;
                break;
            case PlayerState::Paused:
                std::cout << "[Paused] stop and back to initial\n";
                state_ = PlayerState::Stopped;
                break;
        }
    }

private:
    PlayerState state_ = PlayerState::Stopped;
};
```

我们先把这套代码跑起来,确认它行为是对的。下面这个驱动按 `play → pause → play → stop → pause` 的顺序推一遍状态机,正好走完三条有效转换和两条「该被忽略」的调用:

```sh
$ g++ -std=c++23 -O2 -pthread state_switch_player.cpp -o state_switch_player
$ ./state_switch_player
[Stopped] start playing
[Playing] pausing
[Paused] resume
[Playing] stopping
[Stopped] pause() ignored
```

行为没问题。但代码本身的毛病藏在后面。

第一,**「同一个状态的行为,被劈成了 N 份**」。你想知道「Paused 这个状态都会干嘛」,你得在 `play()`、`pause()`、`stop()` 三个函数里各扫一遍,把里面 `case PlayerState::Paused` 的行挑出来拼到一起。状态越是多、事件越是多,这种割裂越严重。第二,**新增一个状态是 N 处修改**。假设你要加一个 `Buffering` 状态,那么 `play()`、`pause()`、`stop()` 这三个 `switch` 你每一个都得加一个 `case`,漏掉任何一个,那个状态收到那个事件就「沉默地什么都不做」——这在协议解析、设备控制这种场景里就是 bug,而且编译器一声不吭,因为它不知道你「本该」处理。第三,最致命的——**`switch` 上没有一个集中、可读的转换规则表**。整个状态机长什么样,你只能脑补。

问题的本质是:状态没有被封装成独立的、自负其责的东西,它只是一个被 `switch` 反复查询的整数。我们得先把状态「提取」出来,让每个状态自己负责自己的事件响应。

## 第二步:把状态抽成对象——State 模式

我们换一个思路:既然每个状态对事件的响应都不一样,那我们就把「响应」做成一个虚函数接口,每个具体状态去实现自己的版本。持有状态的「上下文」(`MediaPlayer`)只负责把事件转发给当前状态对象,不关心具体是哪个状态。

这里有个先有鸡还是先有蛋的问题要先解决:`State` 的方法签名要接受 `MediaPlayer&` 才能在响应里切换状态,而 `MediaPlayer` 又要持有 `State`。互相依赖。C++ 的标准解法是**前向声明**——先把 `MediaPlayer` 的名字声明出来,`State` 用引用/指针引用它就够用了(引用和指针都只需要一个前向声明,不需要完整定义):

```cpp
class MediaPlayer;  // 前向声明,让 State 能用 MediaPlayer&

struct State {
    virtual ~State() = default;
    virtual void play(MediaPlayer& ctx) = 0;
    virtual void pause(MediaPlayer& ctx) = 0;
    virtual void stop(MediaPlayer& ctx) = 0;
    virtual std::string name() const = 0;  // 只是为了打日志
};
```

然后是上下文。`MediaPlayer` 把事件原样转发给当前状态对象,同时提供一个 `set_state()` 给状态对象用来切换:

```cpp
class MediaPlayer {
public:
    explicit MediaPlayer(std::shared_ptr<State> s) : state_(std::move(s)) {}

    void set_state(std::shared_ptr<State> s) {
        std::cout << "[Context] " << state_->name() << " -> " << s->name() << "\n";
        state_ = std::move(s);
    }

    void play()  { state_->play(*this); }
    void pause() { state_->pause(*this); }
    void stop()  { state_->stop(*this); }

private:
    std::shared_ptr<State> state_;
};
```

你看,`MediaPlayer` 现在干净得几乎只剩壳——它不知道有哪些状态,不知道转换规则,它只做一件事:**把事件转给当前状态,并允许当前状态把自己换成另一个**。所有状态相关的逻辑都搬到了具体状态类里。

接下来是三个具体状态。我们先把声明和「空操作」分支写出来(像 `StoppedState::pause()` 这种本来就没意义的调用),把需要切换状态的分支先只声明、实现留到后面。这样做的道理在于:`PlayingState::pause()` 要构造一个 `PausedState`,`PausedState::play()` 又要构造一个 `PlayingState`,如果两个类的定义互相嵌套,编译器会卡在「还没定义完整」上。把需要切换状态的成员函数实现拆到类外、放在所有具体状态类都声明完之后,就能打破这个死结:

```cpp
struct StoppedState : State {
    void play(MediaPlayer& ctx) override;   // 切到 Playing,实现在下面
    void pause(MediaPlayer& /*ctx*/) override {
        std::cout << "[Stopped] pause() ignored\n";
    }
    void stop(MediaPlayer& /*ctx*/) override {
        std::cout << "[Stopped] stop() already stopped\n";
    }
    std::string name() const override { return "Stopped"; }
};

struct PlayingState : State {
    void play(MediaPlayer& /*ctx*/) override {
        std::cout << "[Playing] play() already playing\n";
    }
    void pause(MediaPlayer& ctx) override;  // 切到 Paused,实现在下面
    void stop(MediaPlayer& ctx) override;   // 切到 Stopped,实现在下面
    std::string name() const override { return "Playing"; }
};

struct PausedState : State {
    void play(MediaPlayer& ctx) override;   // 切到 Playing,实现在下面
    void pause(MediaPlayer& /*ctx*/) override {
        std::cout << "[Paused] pause() already paused\n";
    }
    void stop(MediaPlayer& ctx) override;   // 切到 Stopped,实现在下面
    std::string name() const override { return "Paused"; }
};
```

最后是把切换逻辑填进刚才留空的实现里。这时候三个具体状态类都已经是完整类型了,`make_shared<PlayingState>()` 之类写下去没有障碍:

```cpp
void StoppedState::play(MediaPlayer& ctx) {
    std::cout << "[Stopped] start playing\n";
    ctx.set_state(std::make_shared<PlayingState>());
}

void PlayingState::pause(MediaPlayer& ctx) {
    std::cout << "[Playing] pausing\n";
    ctx.set_state(std::make_shared<PausedState>());
}

void PlayingState::stop(MediaPlayer& ctx) {
    std::cout << "[Playing] stopping\n";
    ctx.set_state(std::make_shared<StoppedState>());
}

void PausedState::play(MediaPlayer& ctx) {
    std::cout << "[Paused] resume\n";
    ctx.set_state(std::make_shared<PlayingState>());
}

void PausedState::stop(MediaPlayer& ctx) {
    std::cout << "[Paused] stop and back to initial\n";
    ctx.set_state(std::make_shared<StoppedState>());
}
```

用起来是这样的,你只跟 `MediaPlayer` 这层壳打交道,内部状态怎么切你完全不用管:

```cpp
MediaPlayer player(std::make_shared<StoppedState>());
player.play();   // Stopped -> Playing
player.pause();  // Playing  -> Paused
player.play();   // Paused   -> Playing
player.stop();   // Playing  -> Stopped
player.pause();  // Stopped: pause() ignored
```

这里我们先验证一下,这套代码真的能跑、转换顺序真的对:

```sh
$ g++ -std=c++23 -O2 -pthread state_verify.cpp -o state_verify
$ ./state_verify
[Stopped] start playing
[Context] Stopped -> Playing
[Playing] pausing
[Context] Playing -> Paused
[Paused] resuming
[Context] Paused -> Playing
[Playing] stopping
[Context] Playing -> Stopped
[Stopped] pause() ignored
```

转换链 `Stopped → Playing → Paused → Playing → Stopped`,最后停着的时候按 `pause()` 被安静地忽略。完全符合预期。

到这里你已经能看出 State 模式比那坨 `switch` 强在哪了。**首先,「Paused 状态会怎么响应所有事件」全都在 `PausedState` 这一个类里**——你改它碰不到别的状态。**其次,新增一个状态就是新增一个类**:`Buffering` 来了,你写一个 `BufferingState`,在其中决定自己怎么响应 `play`/`pause`/`stop`,别的类一行都不用动。这恰恰就是开闭原则想要的样子——对扩展开放(加状态不用改老的),对修改关闭(老状态类的代码不需要被碰)。**再者,单元测试变得非常顺手**:你单独构造一个 `PausedState`,喂它事件,看它有没有正确地切换,完全不用起一个完整的 `MediaPlayer`。

## 踩坑预警:shared_ptr 每次转换都分配

::: warning 每次转换都堆分配
上面这套 State 模式有个很容易被忽略的代价:**`set_state(std::make_shared<XxxState>())` 这一行,每次状态转换都做一次堆分配**。`make_shared` 要把对象和控制块(引用计数)一起造出来,如果状态机落在某个高频路径上——比如协议解析里每收到一个字节就推一次状态机——这点分配开销会迅速放大成性能瓶颈。

一个更隐蔽的问题是状态对象的「身份」是变的。每次进入 `Playing` 状态,你拿到的都是一个全新的 `PlayingState` 实例。如果状态本身需要记数据(比如「进入 Playing 后已经播了多少秒」),你把数据存在状态对象里是行不通的,前一次 `Playing` 记的数据在切走时就被销毁了——这种状态相关数据只能挂在上下文 `MediaPlayer` 上。

解决办法有两条。一是如果你的状态对象**没有成员、是纯粹的行为分派**(像上面的例子),那就把它做成共享实例——状态类无状态,`StoppedState` 全程序一个实例就够了,转换时 `set_state(StoppedState::instance())` 复用同一个 `shared_ptr`,不再分配。二是状态集合在编译期就能定死、又对性能敏感时,直接放弃 `shared_ptr` 那条路,用下一节的 `std::variant`,从根本上消除堆分配。
:::

说白了,`shared_ptr` 在这里是**图它接口顺手**——状态对象是多态的、可以跨编译单元传递、生命周期引用计数自动管理。代价就是那一次分配。对于一个播放器 UI 这种低频状态机,这点开销你根本测不出来;但如果你在写一个每秒处理几十万包的网络协议解析器,`shared_ptr` 这条路就得换掉。

## 第三步:状态集合编译期定死——variant + visit

C++17 给了我们 `std::variant`,它是一个**类型安全的、闭集的 union**——「闭集」是关键字:一个 `variant` 能装哪些类型,在你定义它的那一刻就写死了,运行时不能再加。这恰好和「状态机的状态集合是有限的、可枚举的」这件事严丝合缝。我们把状态用一个 `variant` 表示:

```cpp
struct Stopped {};
struct Playing {};
struct Paused {};

using PlayerState = std::variant<Stopped, Playing, Paused>;
```

这里那三个 `struct` 是空的,因为我们的播放器状态本身不带数据。但你要明白 `variant` 的真正威力在于:**每个状态可以带自己的数据,而且编译器会强制你正确处理**。比如给 `Paused` 加一个 `int resume_position;`,你想 `visit` 这个 `variant` 就必须在自己的分派逻辑里把这个字段处理掉,否则编译过不去。这是 `enum + switch` 给不了的强保证——`enum` 的状态和一个普通的整数没两样,「该带的数据」只能额外塞进上下文里,编译器没法替你检查完整性。

上下文 `MediaPlayer` 这回把状态直接按值存着,`visit` 接受一个「对每种状态都有重载」的访问者,返回新的状态:

```cpp
class MediaPlayer {
public:
    MediaPlayer() : state_{Stopped{}} {}

    struct Play {
        PlayerState operator()(const Stopped&)  { std::cout << "[Stopped] start playing\n";  return Playing{}; }
        PlayerState operator()(const Playing&)  { std::cout << "[Playing] play() already playing\n"; return Playing{}; }
        PlayerState operator()(const Paused&)   { std::cout << "[Paused] resume\n";          return Playing{}; }
    };
    struct Pause {
        PlayerState operator()(const Stopped&)  { std::cout << "[Stopped] pause() ignored\n";  return Stopped{}; }
        PlayerState operator()(const Playing&)  { std::cout << "[Playing] pausing\n";          return Paused{};  }
        PlayerState operator()(const Paused&)   { std::cout << "[Paused] pause() already paused\n"; return Paused{}; }
    };
    struct Stop {
        PlayerState operator()(const Stopped&)  { std::cout << "[Stopped] stop() already stopped\n"; return Stopped{}; }
        PlayerState operator()(const Playing&)  { std::cout << "[Playing] stopping\n";        return Stopped{}; }
        PlayerState operator()(const Paused&)   { std::cout << "[Paused] stop and back to initial\n"; return Stopped{}; }
    };

    void play()  { state_ = std::visit(Play{},  state_); }
    void pause() { state_ = std::visit(Pause{}, state_); }
    void stop()  { state_ = std::visit(Stop{},  state_); }

    std::string current() const {
        return std::visit([](const auto& s) -> std::string {
            using T = std::decay_t<decltype(s)>;
            if constexpr (std::is_same_v<T, Stopped>)  return "Stopped";
            else if constexpr (std::is_same_v<T, Playing>) return "Playing";
            else return "Paused";
        }, state_);
    }

private:
    PlayerState state_;
};
```

有两件事值得停下来讲清楚。第一,**`std::visit` 的「闭集」在这里变成了一条编译期契约**:你的访问者(像 `Play`、`Pause`、`Stop` 这几个 `struct`)必须为 `variant` 里的每一种类型都提供一个可调用重载,漏掉任何一种状态,代码就编译不过。这把你从「`switch` 漏了一个 `case` 沉默地什么都不做」的坑里彻底救了出来——在 State 模式里,「忘掉处理某个状态」从运行时 bug 变成了编译错误。第二,**`variant` 是按值存的,转换是「算出下一个 `variant` 再赋值」,没有任何堆分配**。这把上一节 `shared_ptr` 那条路每转换一次分配一次的开销直接抹掉了,代价是状态集合必须在编译期写死,运行时想加状态得改源码。

这里先验证一下,variant 版本和前面两版的转换链是一致的:

```sh
$ g++ -std=c++23 -O2 -pthread state_variant_verify.cpp -o state_variant_verify
$ ./state_variant_verify
now: Stopped
[Stopped] start playing
now: Playing
[Playing] pausing
now: Paused
[Paused] resume
now: Playing
[Playing] stopping
now: Stopped
[Stopped] pause() ignored
now: Stopped
```

同样的转换链,同样的语义。但请注意,**`variant + visit` 不是零成本抽象**。我们来做个对比,把 `variant` 版本和一个朴素 `switch` 版本各自跑五千万次转换计时:

```sh
$ g++ -std=c++23 -O2 -pthread state_bench.cpp -o state_bench
$ ./state_bench
variant: 240 ms  (sink=0)
switch:  8 ms  (sink=2)
```

这里的差距很大,我们需要诚实地说清楚它来自哪里。`switch` 那版编译器一眼就能看穿(`enum` 就是整数,跳表一条 `jmp`),所以快到几乎只剩循环本身的开销;`variant` 那版每次都要构造一个新的 `variant` 并走 `visit` 分派表,加上我那个访问者用 `if constexpr` 链把三个分支写在一起、不利于编译器折叠,所以慢了大约三十倍。**真实的结论不是「variant 一定慢」**,而是「variant + visit 把代价从『虚函数 + 堆分配』换成了『闭集分派表 + 按值构造』,在大多数业务状态机的频次下这点开销可以忽略,但和最朴素的 switch 比仍然不是零」。如果你手头的状态机真的落在每个纳秒都计较的热路径上,那答案就是回到 `enum + switch`。

## 第四步:把转换写成数据——表驱动

到这里我们已经有了两种「行为驱动的状态机」(State 模式、variant),它们的共同特点是:**转换规则和动作代码是混在状态类的成员函数里的**。这在状态行为复杂时很合适。但有时候你的状态机本质上就是一张「从哪、收到啥、去哪、顺便干点啥」的表,规则非常规则、动作非常简单,这时候把转换规则写成**数据**比写成代码要清楚得多——你可以一眼扫完所有转换,甚至把它序列化进配置文件热加载。

我们先定义一条转换的数据结构,它有 `from`(当前状态)、`on`(事件)、可选的 `guard`(守卫条件)、可选的 `action`(副作用)、`to`(目标状态):

```cpp
struct Transition {
    State from;
    Event on;
    std::function<bool()> guard;   // 可选,返回 false 则这条规则不生效
    std::function<void()> action;  // 可选,转换时执行
    State to;
};
```

状态机本身持有一张这样的表和一个当前状态,收到事件时,沿着表往下找第一条 `from == current && on == event && guard()` 成立的规则,执行它的 `action`、把当前状态改成 `to`:

```cpp
class TrafficLight {
public:
    TrafficLight() : current_{State::Red} {
        table_ = {
            {State::Red,    Event::Timer,     {}, {}, State::Green},
            {State::Green,  Event::Timer,     {}, {}, State::Yellow},
            {State::Yellow, Event::Timer,     {}, {}, State::Red},
            {State::Green,  Event::Emergency, {},
                []{ std::cout << "  [action] force red\n"; }, State::Red},
        };
    }

    bool on_event(Event ev) {
        for (const auto& t : table_) {
            if (t.from == current_ && t.on == ev && (!t.guard || t.guard())) {
                std::cout << "  " << name(current_) << " -> " << name(t.to)
                          << " on " << name(ev) << "\n";
                if (t.action) t.action();
                current_ = t.to;
                return true;
            }
        }
        std::cout << "  " << name(current_) << " ignores " << name(ev) << "\n";
        return false;
    }

    State current() const { return current_; }

private:
    static const char* name(State s);
    static const char* name(Event e);
    State current_;
    std::vector<Transition> table_;
};
```

跑一下,看看表里的每一条规则是不是按预期被匹配:

```sh
$ g++ -std=c++23 -O2 -pthread state_table_verify.cpp -o state_table_verify
$ ./state_table_verify
  Red -> Green on Timer
  Green -> Red on Emergency
  [action] force red
  Red ignores Emergency
  Red -> Green on Timer
  Green -> Yellow on Timer
  Yellow -> Red on Timer
```

注意第三个事件:`Green` 收到 `Emergency`,匹配到表里第四条规则,`action` 被触发打印出 `force red`,状态变成 `Red`。紧接着的第四个事件是 `Red` 又收到 `Emergency`——表里没有任何 `from==Red && on==Emergency` 的规则,于是它被「忽略」(`on_event` 返回 `false`)。这就是表驱动的好处:**整个状态机的所有规则,你扫一遍 `table_` 就全看见了**,哪个状态忽略哪个事件一目了然,而且加一条规则就是往表里加一行,核心分派逻辑(`on_event` 那个循环)一行都不用改。

表驱动特别适合「规则多、动作简单、需要可视化或可配置」的场景,比如工作流引擎、正则引擎、通信协议的状态跃迁。它的代价同样要诚实说清楚:`std::function` 本身要存一个可调用对象、通常有一次堆分配(尤其是捕获了大对象的 lambda),而且 `for` 循环线性扫描整张表,状态数一多、事件一密,匹配成本会上去。如果你需要更快,可以把表做成 `std::unordered_map<std::pair<State, Event>, Transition>` 之类按 `(from, on)` 直接哈希查,把线性扫描换成 O(1) 查找。

## 三种写法怎么选

我们把这条演进路径捋一遍,看清楚每一步到底在为什么取舍:

| 写法 | 代价 | 长处 | 适合谁 |
|---|---|---|---|
| `enum` + `switch` | 状态多就臃肿,加状态 N 处改,漏 `case` 没人管 | 最快、零分配、一目了然 | 状态少、性能敏感、转换简单的场景(嵌入式中断处理、协议头解析) |
| State 模式(`shared_ptr`) | 每次转换堆分配,虚分派,状态身份不稳 | 状态行为高度局部化,开闭原则最好,易测 | 状态行为复杂、转换低频、需要运行时新增状态的场景(GUI、播放器) |
| `variant` + `visit` | 状态集合编译期定死,闭集分派有非零开销 | 类型安全(漏状态=编译错),无堆分配,可带状态相关数据 | 状态集合固定、带状态相关数据、性能有要求的场景(编译器前端、解析器) |
| 表驱动 | `std::function` 可能分配,线性扫描 | 规则集中可读、可序列化、可热插拔 | 规则多而动作简单、需要可视化/可配置的场景(工作流、协议状态机) |

怎么挑?先看「状态集合会不会在运行时变」。会变,或者状态行为特别复杂、要按 OOP 风格组织,选 State 模式,代价接受下来。不会变,那 `variant` 给的类型安全和零分配就几乎总是优于裸 `enum`——除非你真在跑那种「每纳秒都计较」的热路径,那就老老实实 `switch`。规则多得像一张表、动作又简单,直接上表驱动,顺便把表序列化进配置。

## 什么时候别上状态机

说实话,不是所有「带状态」的代码都该上状态机。状态机这套抽象本身有成本——你得定义状态类/variant/表,得写分派逻辑,得为每条转换写测试。这个成本只有在状态足够多、转换足够绕的时候才划算。

如果你只有两三个状态,转换也就三五条,一个 `enum` 加一个 `switch` 完事,别为了「用模式」而硬上 State 模式,那只会让代码绕一圈才回到你能一眼看懂的样子。如果你发现某些状态之间有清晰的父子关系(比如「运行中」下面再分「手动」「自动」,「自动」下面再分「加速」「巡航」),平面状态机就开始吃不消了——把所有父子组合都展开成平面状态,状态数会指数爆炸。这时候你要的不是这一篇讲的平面状态机,而是**层次化状态机(Hierarchical State Machine / Statechart)**:子状态没处理的事件会自动冒泡给父状态,父状态可以统一处理「不管在哪个子状态都该做的事」。C++ 里这类需求通常直接上现成库(Boost.SML、Boost.Statechart),自己手撸层次状态机的投入产出比一般不划算。

最后一条工程直觉:**先用最简单的 `switch` 把状态机写出来跑通,等它真的开始膨胀了、真的开始痛了,再考虑要不要换 variant 或 State 模式**。模式是为解决真实的复杂度准备的,不是为了一开始就摆样子。

## 小结

记下这几条关键结论:

- **状态机要解决的核心问题**是「同一事件在不同状态下行为不同」,把这种「状态依赖行为」从散落的 `switch` 里抽出来,局部化到「每个状态自己负责自己」。
- **`enum` + `switch` 最快但最不抗变化**:加一个状态要改 N 处,漏一个 `case` 编译器不管。
- **State 模式**靠虚函数把每个状态的行为封装成类,开闭原则最好、最易测,但 `shared_ptr` 每次转换都堆分配。状态无成员时做成共享实例能消掉分配。
- **`variant` + `visit`** 用闭集类型把状态集合在编译期定死,漏处理一个状态直接编译失败,无堆分配;代价是运行时不能加状态、`visit` 分派不是零成本。
- **表驱动**把转换写成数据,规则集中可读、可序列化、可热插拔,适合规则多而动作简单的场景,代价是 `std::function` 的潜在分配和匹配开销。
- 别为了「用模式」而上状态机。状态少就用 `switch`,父子状态明显就考虑层次化状态机甚至现成库(Boost.SML)。

::: tip 配套可编译工程
本节的例子在仓库 `code/volumn_codes/vol4/design-patterns/State/` 下有完整可编译工程(`.h` + main + `CMakeLists.txt`),`cmake -S . -B build && cmake --build build` 即可跑出上面这些输出。
:::

## 参考资源

- [cppreference:`std::variant`](https://en.cppreference.com/w/cpp/utility/variant)(C++17 起,类型安全的闭集 union)
- [cppreference:`std::visit`](https://en.cppreference.com/w/cpp/utility/variant/visit)(对 variant 的闭集分派,C++17 起)
- [cppreference:`enum class`](https://en.cppreference.com/w/cpp/language/enum)(强类型枚举,C++11 起)
- Gamma、Helm、Johnson、Vissides,《Design Patterns》State 模式(GoF 原版,面向对象状态机)
- Robert C. Martin,《Clean Architecture》第 22 章「Shaping Architecture」(状态机与有限自动机在业务建模中的应用)
