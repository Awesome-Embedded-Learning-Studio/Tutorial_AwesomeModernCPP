---
title: "外观模式:把一坨子系统的协作塞进一个入口"
description: "从最原始的「客户端自己编排一堆子系统」开始,一步步逼出外观模式,讲清它和封装的边界,顺手用多态+shared_ptr 把一个家庭影院 facade 跑起来,最后拆穿 God Object 这个最常见的滥用"
chapter: 11
order: 9
tags:
  - host
  - cpp-modern
  - intermediate
  - 外观模式
difficulty: intermediate
platform: host
cpp_standard: [11, 17, 20]
reading_time_minutes: 18
related:
  - "单例模式:从注释约束到 Meyer's Singleton"
prerequisites:
  - "Chapter 6: 类与对象"
  - "Chapter 9: 智能指针与所有权"
---

# 外观模式:把一坨子系统的协作塞进一个入口

## 我们到底在解决什么问题

先别急着上定义。想一个你会觉得很亲切的场景:你给一块开发板写一个多媒体播放器,底层拆了五个子系统——`NetworkStream` 负责拉流、`VideoDecoder` 和 `AudioDecoder` 各管一路解码、`Renderer` 负责把画面怼到屏幕上、`SubtitleEngine` 负责加载和同步字幕。这些子系统本身都写得挺干净,每个只干一件事。但真正让你头疼的不是它们中的任何一个,而是**「让它们按正确顺序协作起来」这件事**。

如果没有任何封装,你的客户端代码(可能是某个 `MainWindow::on_play_clicked` 槽函数)就会长成这样:

```cpp
// 客户端直接编排子系统,没有任何中间层
NetworkStream stream;
stream.open(url);
auto raw_packet = stream.read_packet();

VideoDecoder vdec;
vdec.init(stream.get_video_params());
vdec.send_packet(raw_packet);

AudioDecoder adec;
adec.init(stream.get_audio_params());
adec.send_packet(raw_packet);

Renderer renderer;
renderer.setup(window);
renderer.render_frame(vdec.get_frame(), adec.get_frame());

SubtitleEngine sub;
sub.load(sub_url);
sub.sync(renderer.get_timestamp());
```

看起来能跑,但你心里其实清楚这里埋着一堆问题。首先,客户端必须**记得**这套顺序——必须先开流、再初始化解码器、再 setup 渲染器、最后同步字幕,顺序一旦搞错(比如先 `render_frame` 再 `setup`),行为要么崩要么花屏。其次,这套时序知识是**写死在调用方脑子里的**,换个调用方(比如一个测试用例想测播放失败、一个命令行工具想批量播放),又得把这一长串原样抄一遍。更糟的是错误处理:如果 `vdec.init` 失败了,客户端得记得回头去关掉已经打开的 `stream`,这种「成功路径走到一半失败、要做反向清理」的逻辑,每多一个子系统就翻一倍,漏掉一个就是资源泄漏。

外观模式(Facade)就是冲着这件事来的。GoF 给它的定义是:**为子系统中的一组接口提供一个一致的界面**。但说实话,这个定义读起来有点绕,我们用大白话翻译一下:**外观不是发明新能力,而是把一堆你已经有的、各干各的子系统,按职责有目的地封装成一个统一的入口**。客户端从「我得记得怎么编排五个子系统」退化成「我就喊一声 `player.play(url)`」,至于开流、解码、渲染、字幕、出错回滚——全都是门面自己的事。

接下来我们就一步步来,看看这个「入口」是怎么一步步从薄到厚长出来的。

## 第一步:最薄的门面——一个会编排的入口

我们先不追求一步到位,先做最薄的一层:写一个类,把上面那一长串时序原样搬进来,对外只暴露两个动作——「开始播放」和「停止播放」。这样客户端至少不用再记顺序了。

```cpp
// MediaPlayerFacade.h
#pragma once
#include <memory>
#include <string>

class MediaPlayerFacade {
public:
    MediaPlayerFacade();
    ~MediaPlayerFacade();

    bool play(const std::string& url);  // 编排整套子系统,成功返回 true
    void stop();

private:
    std::unique_ptr<NetworkStream> stream_;
    std::unique_ptr<VideoDecoder> vdec_;
    std::unique_ptr<AudioDecoder> adec_;
    std::unique_ptr<Renderer> renderer_;
    std::unique_ptr<SubtitleEngine> subtitle_;
};
```

这里有几个细节值得停一下。第一,门面**自己持有所有子系统的所有权**(`unique_ptr` 成员),这是它和「把五个子系统直接塞进客户端」最大的区别——客户端不再持有任何子系统,生命周期被门面统一管理,门面析构的时候,五个 `unique_ptr` 会按声明逆序自动释放掉对应的子系统,客户端一行清理代码都不用写。第二,`play` 返回 `bool` 而不是 `void`,这意味着我们把「播放这件事可能失败」这件事显式地暴露给调用方,但又**不暴露失败的具体原因**(具体原因被门面吞掉、记进日志了),这是一个很关键的取舍,后面我们会回来讲它合不合理。

实现里,`play` 就是把那串时序原样搬进来:

```cpp
bool MediaPlayerFacade::play(const std::string& url) {
    stream_ = std::make_unique<NetworkStream>();
    stream_->open(url);

    vdec_ = std::make_unique<VideoDecoder>();
    vdec_->init(stream_->get_video_params());

    adec_ = std::make_unique<AudioDecoder>();
    adec_->init(stream_->get_audio_params());

    renderer_ = std::make_unique<Renderer>();
    renderer_->setup(window_);

    // 主循环(简化):持续读包、解码、渲染……
    return true;
}
```

现在客户端爽了,它只需要这样写:

```cpp
MediaPlayerFacade player;
if (!player.play("https://example.com/stream.m3u8")) {
    std::cerr << "播放失败\n";
}
```

从「客户端得记得 8 步顺序」退化到「一行 `play`」,这一步本身已经值回票价。但如果你现在就停手,这个门面其实还缺一块非常重要的东西——**错误处理和清理**。

## 第二步:把错误处理和资源清理也收进来

事情到这里还没完。上一版的 `play` 假设了所有子系统都不会失败,这在演示里能跑,在生产里一定炸。真实情况是:`stream_->open` 可能因为网络抖动失败、`vdec_->init` 可能因为编解码参数不合法失败、`renderer_->setup` 可能因为窗口拿不到失败。一旦中间某一步失败,**前面已经初始化好的子系统必须被反向关掉**,否则就是泄漏。

这种「成功路径走到一半失败、需要反向回滚」的逻辑,恰恰是门面最该承担的职责——因为只有门面知道完整的时序,只有它知道「失败时该关哪些、按什么顺序关」。我们把清理逻辑写进一个 `stop()` 里,然后在 `play` 里用异常或提前返回来触发它。这里我用异常 + `catch` 来演示这套「统一清理」的写法:

```cpp
bool MediaPlayerFacade::play(const std::string& url) {
    try {
        stream_ = std::make_unique<NetworkStream>();
        stream_->open(url);

        vdec_ = std::make_unique<VideoDecoder>();
        if (!vdec_->init(stream_->get_video_params())) {
            throw std::runtime_error("video decoder init failed");
        }

        adec_ = std::make_unique<AudioDecoder>();
        if (!adec_->init(stream_->get_audio_params())) {
            throw std::runtime_error("audio decoder init failed");
        }

        renderer_ = std::make_unique<Renderer>();
        renderer_->setup(window_);
        return true;
    } catch (const std::exception& e) {
        std::cerr << "play failed: " << e.what() << '\n';
        stop();   // 统一清理:谁先开的谁后关
        return false;
    }
}
```

你会发现在这套写法里,`catch` 块根本不关心是哪一步抛的——它只管调用 `stop()` 做一次彻底的清理,然后返回 `false`。**门面把「错误从哪来」的细节全部抹平,对外只汇报「成了 / 没成」**,这正是门面在错误处理上的价值:客户端不需要为每种失败写一套不同的清理分支,门面替它把这条路铺平了。

`stop()` 本身就是把已经持有的子系统按「谁先开的谁后关」的原则释放掉,实现起来就是给每个 `unique_ptr` 成员 `reset()`:

```cpp
void MediaPlayerFacade::stop() {
    subtitle_.reset();   // 字幕最先被关(它是最后开的)
    renderer_.reset();   // 渲染器关
    adec_.reset();       // 音频解码器关
    vdec_.reset();       // 视频解码器关
    stream_.reset();     // 流最后关(它是最先开的)
}
```

这套「反向关闭」的顺序是有讲究的:渲染器还指着解码器输出的帧,你先关解码器、再关渲染器,渲染器析构时如果还想去碰那块帧内存就是悬空访问。所以一定要**后开的先关**。其实更省心的做法是干脆不写 `stop()`,直接靠成员析构顺序:成员变量是按声明逆序析构的,只要你在头文件里把成员声明顺序排成「先开的排在后面」,析构时就天然是反向关闭。但这要求你时刻记得「声明顺序 = 关闭顺序」,一旦哪天有人重排了成员顺序,这套隐式约定就悄悄破了——所以我还是更倾向于显式写一个 `stop()`,意图一目了然。

## 实战:一个能跑的家庭影院 facade

光讲媒体播放器太抽象,我们来个真能跑、真能编译的最小例子。下面这个 `HomeTheater` 是一个典型的 facade:它管着四个子系统——`LightController`(灯光)、`DVDPlayer`(影碟机)、`SoundSystem`(音响)、`Projector`(投影仪)。客户端想看电影,只要喊一声 `watch_movie()`,门面负责把这四个子系统**按正确顺序开机**;看完了喊一声 `close_movie()`,门面负责按正确顺序关机。客户端完全不知道这四个子系统的存在。

我们先定义子系统。这里有一个很重要的设计决定:四个子系统虽然各自干的事不同,但它们**都共享「能开 / 能关」这个接口**。这是一个典型的「一群同类对象」的场景,我们用多态把它统一掉——抽一个 `HomeTheaterBaseComponents` 基类,提供纯虚的 `on()` / `off()`:

```cpp
#pragma once
#include <memory>
#include <print>
#include <vector>

struct HomeTheaterBaseComponents {
    virtual ~HomeTheaterBaseComponents() = default;   // 多态基类,虚析构不能省
    virtual void on() noexcept = 0;
    virtual void off() noexcept = 0;
};
```

::: warning 这里千万别手滑
基类的析构函数**必须**是 `virtual` 的(哪怕写成 `= default`,只要带了 `virtual` 也行)。一旦你忘了加 `virtual`,后面通过基类指针 delete 派生对象就是未定义行为——派生部分的析构函数不会被调用,资源悄悄泄漏。这条规则在 facade 场景里特别容易踩,因为门面通常会用一个「装满基类指针的容器」去管理一堆派生对象,正是 UB 的高发区。
:::

然后四个子系统各自实现这套接口。每个子系统就是 `on` / `off` 各打印一行,加上构造析构的日志(后面我们要靠这些日志验证门面的编排顺序):

```cpp
struct DVDPlayer : public HomeTheaterBaseComponents {
    DVDPlayer() { std::print("DVDPlayer created\n"); }
    ~DVDPlayer() override { std::print("DVDPlayer destroyed\n"); }
    void on() noexcept override { std::print("DVDPlayer is now ON\n"); }
    void off() noexcept override { std::print("DVDPlayer is now OFF\n"); }
};

struct Projector : public HomeTheaterBaseComponents {
    Projector() { std::print("Projector created\n"); }
    ~Projector() override { std::print("Projector destroyed\n"); }
    void on() noexcept override { std::print("Projector is now ON\n"); }
    void off() noexcept override { std::print("Projector is now OFF\n"); }
};

struct LightController : public HomeTheaterBaseComponents {
    LightController() { std::print("LightController created\n"); }
    ~LightController() override { std::print("LightController destroyed\n"); }
    void on() noexcept override { std::print("LightController is now ON\n"); }
    void off() noexcept override { std::print("LightController is now OFF\n"); }
};

struct SoundSystem : public HomeTheaterBaseComponents {
    SoundSystem() { std::print("SoundSystem created\n"); }
    ~SoundSystem() override { std::print("SoundSystem destroyed\n"); }
    void on() noexcept override { std::print("SoundSystem is now ON\n"); }
    void off() noexcept override { std::print("SoundSystem is now OFF\n"); }
};
```

接下来就是门面本身。门面持有一个装满基类智能指针的容器——这里我们用 `std::vector<std::shared_ptr<HomeTheaterBaseComponents>>`。为什么用 `shared_ptr` 而不是 `unique_ptr`?我们马上会专门讲,先看门面长什么样:

```cpp
class HomeTheater {
public:
    HomeTheater() {
        // 装配子系统,顺序就是「开机时希望它们被点亮的顺序」
        components_.push_back(std::make_shared<LightController>());
        components_.push_back(std::make_shared<DVDPlayer>());
        components_.push_back(std::make_shared<SoundSystem>());
        components_.push_back(std::make_shared<Projector>());
    }

    void watch_movie() {
        for (auto& each : components_) {
            each->on();   // 多态调用:点谁就亮谁
        }
    }

    void close_movie() {
        for (auto& each : components_) {
            each->off();
        }
    }

private:
    std::vector<std::shared_ptr<HomeTheaterBaseComponents>> components_;
};
```

你看门面做了什么:它在构造函数里**装配**了整套子系统,把它们按希望的顺序塞进容器;`watch_movie` 就一个循环,挨个 `on()`,根本不关心 `each` 具体是灯光还是投影仪——多态替它分派了;`close_movie` 同理。客户端代码干净到令人发指:

```cpp
#include "HomeTheater.h"

int main() {
    HomeTheater theater;
    theater.watch_movie();   // 一行,四个子系统全亮
    theater.close_movie();   // 一行,四个子系统全灭
}
```

### 先验证一下:顺序对不对

我们先别急着信,把这段编译跑跑,看看 `on()` 和 `off()` 是不是真的按我们塞进去的顺序、析构又是按什么顺序:

```sh
$ g++ -std=c++23 -O2 -pthread HomeTheaterMain.cpp -o HomeTheater
$ ./HomeTheater
LightController created
DVDPlayer created
SoundSystem created
Projector created
LightController is now ON
DVDPlayer is now ON
SoundSystem is now ON
Projector is now ON
LightController is now OFF
DVDPlayer is now OFF
SoundSystem is now OFF
Projector is now OFF
LightController destroyed
DVDPlayer destroyed
SoundSystem destroyed
Projector destroyed
```

开机构造的顺序是 Light→DVD→Sound→Projector,`watch_movie` 的开机顺序和它一致,`close_movie` 的关机顺序也一致(从第一个到最后一个)。析构顺序看起来「也是从第一个到最后一个」——这里有个值得展开的细节,`std::vector` 析构时是**从前到后**逐个销毁元素,我们另外写个最小例子验证一下这个顺序:

```cpp
#include <iostream>
#include <memory>
#include <vector>

struct Comp {
    int id;
    explicit Comp(int i) : id(i) { std::cout << "  Comp(" << id << ") ctor\n"; }
    ~Comp() { std::cout << "  Comp(" << id << ") dtor\n"; }
};

int main() {
    std::vector<std::shared_ptr<Comp>> v;
    v.reserve(4);
    v.push_back(std::make_shared<Comp>(1));
    v.push_back(std::make_shared<Comp>(2));
    v.push_back(std::make_shared<Comp>(3));
    v.push_back(std::make_shared<Comp>(4));
    std::cout << "  (leaving scope)\n";
}
```

```sh
$ g++ -std=c++23 -O2 vec_dtor_order.cpp -o vec_dtor_order && ./vec_dtor_order
  Comp(1) ctor
  Comp(2) ctor
  Comp(3) ctor
  Comp(4) ctor
  (leaving scope)
  Comp(1) dtor
  Comp(2) dtor
  Comp(3) dtor
  Comp(4) dtor
```

确认了:`vector` 析构是从前到后销毁元素(FIFO,按插入顺序),所以家庭影院里 Light 先析构、Projector 后析构。如果你需要的是「后开的先关」(反向关闭),那就得在 `close_movie` 里手动逆序遍历,或者干脆别用 vector 的隐式析构顺序——意图要写明白,别让读者去猜。

## 为什么用 shared_ptr,以及一个有点反直觉的点

回到刚才那个问题:为什么门面里用的是 `std::vector<std::shared_ptr<...>>` 而不是 `std::vector<std::unique_ptr<...>>`?在家庭影院这个例子里,从所有权角度讲,`unique_ptr` 其实就够了——门面对子系统是独占的,没有共享需求,`unique_ptr` 更轻、更贴切。

但 `shared_ptr` 在「装进容器」这件事上有一个很多人不知道的优势,我们要单独拎出来讲。当你写 `std::shared_ptr<Base> sp = std::make_shared<Derived>();` 的时候,`shared_ptr` 内部会**在构造的那一刻就把派生类型的销毁方式记下来**(它存了一个类型擦除的 deleter)。这意味着,**即使 `Base` 的析构函数不是 `virtual` 的,`shared_ptr` 析构时也会正确调用 `~Derived()`**。我们写个最小例子对比 `shared_ptr` 和 `unique_ptr` 在「基类析构非 virtual」时的行为:

```cpp
#include <iostream>
#include <memory>

struct Base {
    Base() { std::cout << "  Base()\n"; }
    ~Base() { std::cout << "  ~Base()\n"; }   // 注意:不是 virtual
    virtual void noop() {}
};

struct Derived : Base {
    Derived() { std::cout << "  Derived()\n"; }
    ~Derived() { std::cout << "  ~Derived()\n"; }
};

int main() {
    std::cout << "=== shared_ptr<Base> (base dtor non-virtual) ===\n";
    { std::shared_ptr<Base> sp = std::make_shared<Derived>(); }

    std::cout << "=== unique_ptr<Base> (base dtor non-virtual) ===\n";
    { std::unique_ptr<Base> up(new Derived); }
}
```

```sh
$ g++ -std=c++23 -O2 sp_vs_up.cpp -o sp_vs_up && ./sp_vs_up
=== shared_ptr<Base> (base dtor non-virtual) ===
  Base()
  Derived()
  ~Derived()
  ~Base()
=== unique_ptr<Base> (base dtor non-virtual) ===
  Base()
  Derived()
  ~Base()
```

差别一目了然:`shared_ptr` 版本的 `~Derived()` 被正确调用了,而 `unique_ptr<Base>` 版本**只调用了 `~Base()`,`~Derived()` 直接漏掉了**(技术上这是未定义行为,通常表现为派生部分的资源泄漏)。原因就在于 `shared_ptr` 的 deleter 是构造时类型擦除的,它在 `make_shared<Derived>()` 那一刻就记住了「要调 `~Derived()`」;而 `unique_ptr<Base>` 的 deleter 是静态绑定的,它只知道自己是 `Base*`,销毁时就是 `delete (Base*)`,基类析构非 virtual 就只能调到 `~Base()`。

::: tip 该选哪个
这条规则不是让你无脑用 `shared_ptr`。绝大多数 facade 场景,**基类给个 `virtual ~Base() = default;`,然后用 `unique_ptr` 就对了**——它更轻、所有权语义更清晰(独占)、没有原子引用计数的开销。只有在一种情况你需要意识到 `shared_ptr` 的这个差异:当你无法修改基类(比如基类来自第三方库、析构函数不是 virtual)、又非要把派生对象塞进 `unique_ptr<Base>` 容器时,`shared_ptr` 是一个能救命的 workaround。但更正派的解法仍然是「想办法让基类析构 virtual」,或者干脆别用多态,改用 `std::variant`。
:::

## 别走极端:facade 不是用来重写子系统的

到这里我们已经有了一个能跑、能正确开关机、客户端只写两行的门面。但事情到这里还没完——我得诚实地告诉你,**外观模式是所有 GoF 模式里最容易用滥的一个**。原因很简单:它太顺手了。你只要发现「客户端要调好几个类」,就顺手包一层 facade,包着包着,你收获的不是一个门面,而是一个**上帝对象(God Object)**。

什么意思? facade 的本职是**「编排」和「抽象流程」**,不是「重新实现业务逻辑」。一个好的 facade 长这样:它知道先开谁后开谁、失败时怎么回滚,但它**不替子系统做决策**。比如家庭影院的 `watch_movie` 只做「挨个 on」,它不去判断「投影仪该不该先预热 30 秒」——预热是 `Projector` 自己的职责,门面只是按顺序喊它一声。

但如果你滥用 facade,把所有细节都塞进一个大门面,它会慢慢吞掉子系统的职责,最后变成这样一个怪物:

```cpp
// 反面教材:门面在「重写」子系统的业务逻辑
class MegaHomeTheaterFacade {
public:
    void watch_movie() {
        // 灯光渐暗、投影仪预热 30s、音响切影院模式、DVD 跳轨、字幕拉伸……
        // 全塞在门面里,各子系统退化成「只会开关的傻盒子」
        lights_.set_brightness(0);
        std::this_thread::sleep_for(std::chrono::seconds(30));  // 预热?这该是投影仪的事
        sound_.set_mode(kCinema);
        dvd_.seek_chapter(1);
        // ……
    }
};
```

这个门面有几个明显的坏味道。第一,它**抢了子系统的职责**——投影仪预热 30 秒明明是 `Projector::warm_up()` 该干的事,现在门面替它 `sleep` 30 秒,等于把业务知识从子系统里搬到了门面里,子系统退化成了只听摆布的傻盒子。第二,它**变得没法单测**——你想测「投影仪预热」这条逻辑,结果发现它被焊死在门面的 `watch_movie` 里,你不得不构造一整套家庭影院才能测一个小小的预热。第三,门面**变得难维护**——任何子系统的细节变动,都得改门面;门面不再是「流程的编排者」,而成了「所有业务逻辑的聚集地」,这正好是 facade 想消灭的问题,只不过换了个地方重新出现。

判断 facade 有没有变质的简单标准是这一条:**门面里不应该出现任何「具体业务决策」的代码,只该出现「按什么顺序、调谁、怎么回滚」的代码**。一旦你发现自己在门面里写「投影仪该预热多久、字幕该拉伸几倍」这种参数,就该警觉——这些是子系统的职责,门面只是喊它们一声。

## 进阶:用 std::variant 替代多态容器

家庭影院的例子里,四个子系统其实**类型在编译期就完全确定**了(就这四个,不会动态新增)。这种「闭集」场景,我们其实不一定要上多态 + 堆分配那一套,可以用 C++17 的 `std::variant` 把它们装进一个编译期就能枚举的容器里,**完全避开虚函数调用和堆分配**:

```cpp
#include <variant>
#include <vector>
#include <print>

using Component = std::variant<
    LightController, DVDPlayer, SoundSystem, Projector>;

class HomeTheaterVariant {
public:
    HomeTheaterVariant() {
        components_.reserve(4);  // 先 reserve,避免扩容时触发 variant 的拷贝/移动
        components_.emplace_back(std::in_place_type<LightController>);
        components_.emplace_back(std::in_place_type<DVDPlayer>);
        components_.emplace_back(std::in_place_type<SoundSystem>);
        components_.emplace_back(std::in_place_type<Projector>);
    }

    void watch_movie() {
        for (auto& c : components_) {
            std::visit([](auto& comp) { comp.on(); }, c);  // 编译期分派,无虚函数
        }
    }

    void close_movie() {
        for (auto& c : components_) {
            std::visit([](auto& comp) { comp.off(); }, c);
        }
    }

private:
    std::vector<Component> components_;
};
```

这种写法的好处是实打实的:没有堆分配(`variant` 是值类型,直接内联在 `vector` 的连续内存里),没有虚函数表查找(`std::visit` 在编译期就生成了所有分支),缓存友好度比一堆散落的堆对象高得多。代价是四个子系统必须各自定义一个 `on()` / `off()` 成员(不需要继承同一个基类、不需要 `virtual`),而且**这套容器的类型集合在编译期就钉死了**——你想运行时插一个第五种 `Amplifier` 进来,就得改 `Component` 这个 `variant` 的类型列表,重新编译。

所以取舍很清晰:**子系统的种类在编译期固定、且数量不多,优先 `std::variant`(值语义、零虚函数开销);子系统的种类要运行时动态扩展、或者来自插件,就只能用多态 + 基类指针**。家庭影院、状态机里固定那几个状态、编译流水线那几个固定阶段——都属于前者,`std::variant` 往往是更现代、更合身的工具。

## 何时用外观,何时别用

我们把这套取舍捋一遍。外观最适合的场景是「你需要把一组复杂子系统的协作逻辑,封装成一个稳定、简单的入口」——比如这里的家庭影院(一套开关机流程)、媒体播放器(一套拉流解码渲染流程)、数据库连接池(一套建连/还连/健康检查流程)、编译器前端(一套预处理/词法/语法流程)。在这些场景里,门面减少客户端与子系统的直接耦合、集中处理错误和生命周期、让你修改底层实现时不必惊动使用者,价值非常实在。

但有几个信号说明你不该上 facade,或者你已有的 facade 该拆了。第一,**子系统本来就很薄**(只有一两个、且接口已经很干净),这时候包一层 facade 纯属多此一举,只会增加一个没有实际收益的间接层。第二,**门面开始吞业务逻辑**(像前面那个 `MegaHomeTheaterFacade`),这时候你需要的不是更多 facade,而是把职责还回子系统。第三,**不同客户端对同一套子系统的编排需求差异巨大**(A 客户端要全套、B 客户端只要其中两个、C 客户端要完全不同的顺序),这时候一个大门面装不下所有用法,你应该考虑**为每类客户端写一个专门的小 facade**,或者干脆让客户端自己直接挑子系统调用——强迫所有客户端走同一个大门面,只会让门面里堆满「如果 A 路径就这么做、如果 B 路径就那么做」的分支,又开始滑向 God Object。

## 小结

我们把整条思路捋一遍:

| 阶段 | 做法 | 为什么还不够 |
|---|---|---|
| 客户端直接编排子系统 | 把 5 个子系统的调用原样写在客户端 | 时序知识写死在调用方,换调用方就得抄一遍,失败回滚散落各处 |
| 薄门面 | 一个类持有所有子系统,对外 `play()`/`stop()` | 假设了不会失败,没有错误处理和清理 |
| 带清理的门面 | `play` 里 try/catch,失败调 `stop()` 反向关闭 | 够用了,客户端从此只关心「成 / 没成」 |
| 多态容器门面 | `vector<shared_ptr<Base>>` + 虚函数 `on/off` | 闭集场景下堆分配和虚函数是多余开销 |
| `std::variant` 门面 | 值类型容器 + `std::visit` 编译期分派 | 子系统种类要动态扩展时不适用 |

记下这几条关键结论:

- **外观模式的本质不是发明新能力,而是把已有子系统按职责有目的地封装成一个统一入口**,它解决的是「客户端不该知道子系统的编排细节」这件事。
- **门面的本职是「编排流程 + 处理错误 + 管理生命周期」**,不是重写业务逻辑;一旦门面里出现了具体的业务决策(预热几秒、字幕拉伸几倍),它就开始滑向 God Object。
- **多态基类的析构函数必须 virtual**,否则通过基类指针 delete 派生对象是 UB;`vector<shared_ptr<Base>>` 析构时元素从前到后销毁(按插入顺序)。
- **`shared_ptr` 的 deleter 构造时类型擦除,即使基类析构非 virtual 也能正确销毁派生**;但更正派的做法仍是给基类一个 `virtual` 析构,再用更轻的 `unique_ptr`。
- **子系统种类在编译期固定时,优先 `std::variant` + `std::visit`,能避开虚函数和堆分配**;只有需要运行时动态扩展才用多态。

## 参考资源

- cppreference:[`std::shared_ptr` 的 destructor 语义](https://en.cppreference.com/w/cpp/memory/shared_ptr/~shared_ptr)(C++11 起,构造时类型擦除 deleter)
- cppreference:[`std::unique_ptr` 与不完整类型 / 虚析构](https://en.cppreference.com/w/cpp/memory/unique_ptr)(C++11 起)
- cppreference:[`std::variant` 与 `std::visit`](https://en.cppreference.com/w/cpp/utility/variant)(C++17 起)
- GoF,《Design Patterns: Elements of Reusable Object-Oriented Software》—— Facade 一章
- 配套可编译工程:[Facade / HomeTheater](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/tree/main/code/volumn_codes/vol4/design-patterns/Facade/HomeTheater)
