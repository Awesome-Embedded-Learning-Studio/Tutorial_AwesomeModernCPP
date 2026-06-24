---
title: 'Facade Pattern: Encapsulating Subsystem Collaboration Behind a Single Entry
  Point'
description: Starting from the most primitive approach where "the client manually
  orchestrates a bunch of subsystems," we progressively derive the Facade pattern.
  We clarify the boundary between it and encapsulation, implement a home theater facade
  using polymorphism and `shared_ptr`, and finally expose the "God Object" as the
  most common form of misuse.
chapter: 11
order: 9
tags:
- host
- cpp-modern
- intermediate
- 外观模式
difficulty: intermediate
platform: host
cpp_standard:
- 11
- 17
- 20
reading_time_minutes: 18
related:
- 单例模式:从注释约束到 Meyer's Singleton
prerequisites:
- 'Chapter 6: 类与对象'
- 'Chapter 9: 智能指针与所有权'
translation:
  source: documents/vol4-advanced/vol4-generics-patterns/09-facade.md
  source_hash: 2107ed9e6fff8b41bc6e5c042b9043655fba2b37cc8d37259a6758ed303f483f
  translated_at: '2026-06-24T00:57:25.016026+00:00'
  engine: anthropic
  token_count: 4312
---
# Facade Pattern: Encapsulating Subsystem Collaboration into a Single Entry Point

## What Problem Are We Actually Solving?

Let's skip the formal definition for a moment. Consider a familiar scenario: you are writing a media player for a development board. Under the hood, you have split the logic into five subsystems—`NetworkStream` handles streaming, `VideoDecoder` and `AudioDecoder` manage their respective decoding paths, `Renderer` pushes frames to the screen, and `SubtitleEngine` handles loading and synchronizing subtitles. These subsystems are well-designed, each focusing on a single responsibility. However, the real headache isn't any individual component, but rather **orchestrating their collaboration in the correct order**.

Without any encapsulation, your client code (perhaps inside a `MainWindow::on_play_clicked` slot) would end up looking like this:

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

It looks like it runs, but deep down you know there are a bunch of issues buried here. First, the client must **remember** this specific sequence—start the stream, initialize the decoder, set up the renderer, and finally sync subtitles. If the order is wrong (for example, calling `render_frame` before `setup`), the behavior is either a crash or corrupted video. Second, this timing knowledge is **hard-coded in the caller's brain**. If you change the caller (like a test case checking for playback failures, or a command-line tool for batch playback), you have to copy this entire sequence verbatim. Even worse is error handling: if `vdec.init` fails, the client must remember to go back and close the already opened `stream`. This "halfway failure on the success path requires reverse cleanup" logic doubles with every subsystem added; miss one, and you have a resource leak.

The Facade pattern is designed exactly for this. The GoF definition is: **Provide a unified interface to a set of interfaces in a subsystem**. But honestly, that definition is a bit convoluted. Let's translate it into plain English: **A facade doesn't invent new capabilities; instead, it wraps a bunch of existing, independent subsystems into a unified entry point based on their responsibilities**. The client devolves from "I need to remember how to orchestrate five subsystems" to "I just call `player.play(url)`". As for streaming, decoding, rendering, subtitles, and error rollback—that's all the facade's business.

Next, let's go step by step and see how this "entry point" grows from thin to thick.

## Step 1: The Thinnest Facade—An Entry Point that Orchestrates

Let's not try to get it perfect in one go. Let's start with the thinnest layer: write a class that moves that long sequence of operations inside, exposing only two actions to the outside—"Start Playback" and "Stop Playback". This way, the client at least doesn't need to memorize the order anymore.

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

There are a few details worth noting here. First, the facade **holds ownership of all subsystems** (via `unique_ptr` members). This is the main difference from "just stuffing five subsystems directly into the client"—the client no longer holds any subsystems. The lifecycle is managed uniformly by the facade. When the facade is destroyed, the five `unique_ptr` members automatically release the corresponding subsystems in reverse order of declaration. The client doesn't need to write a single line of cleanup code. Second, `play` returns `bool` instead of `void`. This means we explicitly expose the possibility that "playing might fail" to the caller, but we **do not expose the specific reason for the failure** (the specific reason is swallowed by the facade and logged). This is a critical trade-off; we will discuss later whether this is reasonable.

In the implementation, `play` simply brings in that sequence of operations verbatim:

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

Now the client code is much simpler; we only need to write:

```cpp
MediaPlayerFacade player;
if (!player.play("https://example.com/stream.m3u8")) {
    std::cerr << "播放失败\n";
}
```

Degenerating from "the client has to remember the 8-step sequence" to "a single `play`" line is already worth the price of admission. But if we stop here, this facade is actually missing a crucial piece—**error handling and cleanup**.

## Step 2: Incorporate error handling and resource cleanup

We aren't done yet. The previous version of `play` assumed that all subsystems would never fail. While this works in a demo, it will inevitably crash in production. The reality is: `stream_->open` might fail due to network jitter, `vdec_->init` might fail due to invalid codec parameters, and `renderer_->setup` might fail because it can't secure a window. Once a step fails in the middle, **the previously initialized subsystems must be shut down in reverse order**, otherwise we have a leak.

This logic—where "we fail halfway through the success path and need to roll back"—is exactly what the facade should be responsible for. Only the facade knows the complete timing, and only it knows "which ones to shut down upon failure, and in what order." We write the cleanup logic into a `stop()` method, and then trigger it in `play` using exceptions or early returns. Here, I use exceptions + `catch` to demonstrate this "unified cleanup" pattern:

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

You will notice that in this approach, the `catch` block does not care which step threw the exception. It simply calls `stop()` to perform a thorough cleanup, and then returns `false`. **The facade smooths over the details of "where the error came from," reporting only "success or failure" to the outside world.** This is the true value of the facade in error handling: the client does not need to write different cleanup branches for every type of failure; the facade paves the way for it.

`stop()` simply releases the subsystems it holds according to the "last opened, first closed" principle. The implementation involves calling `reset()` on each `unique_ptr` member:

```cpp
void MediaPlayerFacade::stop() {
    subtitle_.reset();   // 字幕最先被关(它是最后开的)
    renderer_.reset();   // 渲染器关
    adec_.reset();       // 音频解码器关
    vdec_.reset();       // 视频解码器关
    stream_.reset();     // 流最后关(它是最先开的)
}
```

The order of this "reverse shutdown" is deliberate: the renderer still holds a reference to the frames output by the decoder. If you shut down the decoder first and then the renderer, the renderer's destructor will trigger a dangling access if it attempts to touch that frame memory. Therefore, we must strictly follow the **last opened, first closed** rule.

A more worry-free approach is to simply omit writing `stop()` and rely on member destruction order: member variables are destroyed in reverse order of declaration. As long as you arrange the member declaration order in the header file so that "components opened first are listed later," the destruction process naturally handles the reverse shutdown. However, this requires you to constantly remember that "declaration order = shutdown order." If someone ever reshuffles the member order, this implicit convention silently breaks—so I still prefer explicitly writing a `stop()` to make the intent crystal clear.

## In Practice: A Working Home Theater Facade

Discussing a media player is too abstract, so let's build a minimal example that actually compiles and runs. The following `HomeTheater` is a typical facade: it manages four subsystems—`LightController`, `DVDPlayer`, `SoundSystem`, and `Projector`. When the client wants to watch a movie, they simply call `watch_movie()`, and the facade handles powering on these four subsystems **in the correct sequence**. When finished, a call to `close_movie()` ensures the facade powers them off in the correct order. The client remains completely unaware of the existence of these four subsystems.

First, let's define the subsystems. Here, we make a crucial design decision: although the four subsystems perform different tasks, they **all share a common "on/off" interface**. This is a classic scenario involving "a group of similar objects," so we use polymorphism to unify them—extracting a `HomeTheaterBaseComponents` base class that provides pure virtual `on()` and `off()` methods:

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

::: warning Don't slip up here
The base class destructor **must** be `virtual` (even if written as `= default`, as long as it includes `virtual`). If you forget to add `virtual`, deleting a derived object via a base class pointer is undefined behavior—the derived part's destructor won't be called, and resources will leak silently. This rule is particularly easy to trip over in facade scenarios, because facades often use a "container full of base class pointers" to manage a group of derived objects, making this a high-risk area for UB.
:::

Then, the four subsystems implement this interface respectively. Each subsystem simply prints a line for `on` / `off`, plus logging for construction and destruction (we will rely on these logs later to verify the facade's orchestration sequence):

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

Next is the facade itself. The facade holds a container filled with smart pointers to base classes—here we use `std::vector<std::shared_ptr<HomeTheaterBaseComponents>>`. Why do we use `shared_ptr` instead of `unique_ptr`? We will discuss this specifically in a moment, but first, let's look at what the facade looks like:

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

Look at what the facade does: it **assembles** the entire subsystem in its constructor, placing components into the container in the desired order. `watch_movie` is just a loop that calls `on()` for each item, completely unconcerned whether `each` is a light or a projector—polymorphism dispatches the call. `close_movie` works similarly. The client code is incredibly clean:

```cpp
#include "HomeTheater.h"

int main() {
    HomeTheater theater;
    theater.watch_movie();   // 一行,四个子系统全亮
    theater.close_movie();   // 一行,四个子系统全灭
}
```

### First, let's verify: is the order correct?

Before we take this for granted, let's compile and run this code to see if `on()` and `off()` are really called in the order we defined, and what order the destructors follow.

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

The construction order is Light → DVD → Sound → Projector. The power-on sequence of `watch_movie` matches this order, and the power-off sequence of `close_movie` is also consistent (from the first to the last). The destruction order appears to be "from the first to the last" as well—here is a detail worth exploring: when `std::vector` is destroyed, it destroys elements **from front to back** one by one. Let's write a minimal example to verify this order:

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

Confirmed: `vector` destroys elements from front to back (FIFO, in insertion order). Therefore, in the home theater setup, `Light` is destroyed first, and `Projector` is destroyed last. If you need "last on, first off" (reverse shutdown), you must manually iterate in reverse order within `close_movie`, or simply avoid relying on `vector`'s implicit destruction order—make your intent explicit, so readers don't have to guess.

## Why we use shared_ptr, and a slightly counter-intuitive point

Returning to our earlier question: why does the facade use `std::vector<std::shared_ptr<...>>` instead of `std::vector<std::unique_ptr<...>>`? In this home theater example, `unique_ptr` is actually sufficient from an ownership perspective—the facade has exclusive ownership of the subsystems, there is no sharing requirement, and `unique_ptr` is lighter and more appropriate.

However, `shared_ptr` has an advantage when "putting things into containers" that many people are unaware of, so we need to discuss it separately. When you write `std::shared_ptr<Base> sp = std::make_shared<Derived>();`, the `shared_ptr` internally **records the destruction method of the derived type at the moment of construction** (it stores a type-erased deleter). This means that **even if `Base`'s destructor is not `virtual`, the `shared_ptr` will correctly call `~Derived()` upon destruction**. Let's write a minimal example to compare the behavior of `shared_ptr` and `unique_ptr` when the "base class destructor is non-virtual":

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

The difference is clear at a glance: the `~Derived()` for the `shared_ptr` version is called correctly, whereas the `unique_ptr<Base>` version **only calls `~Base()`, completely skipping `~Derived()`** (technically this is undefined behavior, usually manifesting as a resource leak in the derived part). The reason is that the `shared_ptr`'s deleter performs type erasure at construction; it remembers at the very moment of `make_shared<Derived>()` that "it needs to call `~Derived()`". In contrast, the `unique_ptr<Base>`'s deleter is statically bound; it only knows it holds a `Base*`, so upon destruction it executes `delete (Base*)`. If the base class destructor is non-virtual, it can only reach `~Base()`.

::: tip Which one to choose
This rule doesn't mean you should blindly use `shared_ptr`. For the vast majority of facade scenarios, **just give the base class a `virtual ~Base() = default;` and use `unique_ptr`**—it is lighter, has clearer ownership semantics (exclusive), and avoids the overhead of atomic reference counting. You only need to be aware of this specific `shared_ptr` behavior in one situation: when you cannot modify the base class (e.g., it comes from a third-party library and the destructor isn't virtual) but insist on storing derived objects in a `unique_ptr<Base>` container. In that case, `shared_ptr` serves as a lifesaving workaround. However, the "proper" solution remains to "make the base class destructor virtual" or avoid polymorphism altogether and use `std::variant`.
:::

## Don't go to extremes: a facade is not for rewriting subsystems

At this point, we have a working facade that correctly powers on and off, requiring only two lines of code from the client. But we aren't done yet—I must be honest with you: **the Facade pattern is the easiest to abuse among all GoF patterns**. The reason is simple: it's too convenient. Whenever you find that "the client needs to call several classes," you casually wrap a facade around them. If you keep wrapping like this, you won't end up with a facade, but a **God Object**.

What does this mean? The primary job of a facade is **"orchestration" and "abstracting workflows"**, not "reimplementing business logic." A good facade looks like this: it knows who to start first and who to start last, and how to roll back on failure, but it **does not make decisions for the subsystem**. For example, the home theater's `watch_movie` simply turns things on one by one; it doesn't decide "whether the projector needs to preheat for 30 seconds"—preheating is the `Projector`'s own responsibility; the facade just calls it in order.

However, if you abuse the facade and stuff every detail into one big interface, it will slowly swallow the subsystem's responsibilities, eventually turning into a monster like this:

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

This facade exhibits several obvious code smells. First, it **encroaches on the subsystem's responsibilities**—warming up the projector for 30 seconds is clearly the job of `Projector::warm_up()`, yet the facade `sleep`s for 30 seconds on its behalf. This effectively moves business knowledge from the subsystem into the facade, reducing the subsystem to a dumb box that simply obeys orders. Second, it **becomes difficult to unit test**—if we want to test the "projector warm-up" logic, we find it welded inside the facade's `watch_movie`. We are forced to construct the entire home cinema setup just to test a tiny warm-up feature. Third, the facade **becomes hard to maintain**—any change to subsystem details requires modifying the facade. The facade is no longer an "orchestrator of workflows," but rather a "dumping ground for all business logic." This is exactly the problem the Facade pattern aims to solve, only now it has reappeared in a different location.

A simple criterion to determine if a facade has gone bad is this: **The facade should not contain any code for "specific business decisions," only code for "in what order to call whom, and how to roll back."** Once you catch yourself writing parameters like "how long the projector should warm up" or "how much to scale the subtitles" in the facade, you should be alert—these are the responsibilities of the subsystem. The facade is merely the messenger calling them.

## Advanced: Using `std::variant` to Replace Polymorphic Containers

In the home cinema example, the types of the four subsystems are actually **fully determined at compile time** (there are exactly four, and no dynamic additions will occur). In this "closed set" scenario, we don't necessarily need the polymorphism + heap allocation approach. We can use C++17's `std::variant` to bundle them into a container enumerable at compile time, **completely avoiding virtual function calls and heap allocation**:

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

The benefits of this approach are tangible: there is no heap allocation (`variant` is a value type, stored inline within the `vector`'s contiguous memory), and no virtual table lookups (`std::visit` generates all branches at compile time). Cache friendliness is significantly higher than a scattered collection of heap objects. The cost is that the four subsystems must each define an `on()` / `off()` member (no need to inherit from a common base class, no `virtual` keyword), and **the set of types in this container is nailed down at compile time**—if you want to insert a fifth `Amplifier` type at runtime, you must modify the `variant` type list in `Component` and recompile.

So the trade-off is clear: **if subsystem types are fixed at compile time and few in number, prefer `std::variant` (value semantics, zero virtual function overhead); if subsystem types need to be extended dynamically at runtime or come from plugins, you must use polymorphism + base class pointers**. Home theaters, fixed-state state machines, and the fixed stages of a compilation pipeline all belong to the former category, making `std::variant` often the more modern and better-fitting tool.

## When to Use a Facade, and When Not To

Let's walk through this trade-off again. The Facade is best suited for scenarios where **"you need to encapsulate the collaboration logic of a complex set of subsystems into a stable, simple entry point"**—such as a home theater (a single power on/off flow), a media player (a stream/decode/render flow), a database connection pool (a connect/return/health check flow), or a compiler frontend (a preprocess/lexical/syntax flow). In these scenarios, the facade reduces direct coupling between the client and the subsystem, centralizes error handling and lifecycle management, and allows you to modify underlying implementations without disturbing users. The value is very real.

However, there are several signs that you shouldn't apply a facade, or that your existing facade should be split up. First, **if the subsystems are already very thin** (only one or two, with interfaces that are already clean), wrapping a facade around them is purely superfluous and only adds an indirection layer with no actual benefit. Second, **if the facade starts swallowing business logic** (like that `MegaHomeTheaterFacade` earlier), what you need is not more facade, but to return responsibilities to the subsystems. Third, **if different clients have vastly different orchestration requirements for the same set of subsystems** (Client A needs the full set, Client B needs only two, Client C needs a completely different order), a single large facade cannot accommodate all usage patterns. You should consider **writing a dedicated small facade for each client type**, or simply letting clients call subsystems directly—forcing all clients through the same large facade will only pile the facade full of "if path A do this, if path B do that" branches, sliding back towards a God Object.

## Summary

Let's walk through the entire train of thought:

| Stage | Approach | Why it's still not enough |
|---|---|---|
| Client orchestrates subsystems directly | Writing calls to 5 subsystems directly in the client | Timing knowledge is hardcoded in the caller; changing the caller requires copying the code, and failure rollback is scattered everywhere. |
| Thin Facade | A class holds all subsystems, exposes `play()`/`stop()` | Assumes no failure, lacks error handling and cleanup. |
| Facade with Cleanup | try/catch in `play`, calls `stop()` to shut down on failure | Sufficient for use; clients now only care about "success / failure". |
| Polymorphic Container Facade | `vector<shared_ptr<Base>>` + virtual `on/off` | Heap allocation and virtual functions are redundant overhead in closed-set scenarios. |
| `std::variant` Facade | Value-type container + `std::visit` compile-time dispatch | Not applicable when subsystem types need dynamic extension. |

Keep these key conclusions in mind:

- **The essence of the Facade pattern is not to invent new capabilities, but to purposefully encapsulate existing subsystems by responsibility into a unified entry point**; it solves the problem that **"clients shouldn't need to know the orchestration details of the subsystems."**
- **The primary job of a facade is "orchestration flow + error handling + lifecycle management,"** not rewriting business logic; once specific business decisions (warm-up seconds, subtitle stretch factor) appear in the facade, it starts sliding towards a God Object.
- **Polymorphic base class destructors must be `virtual`**, otherwise deleting a derived object via a base class pointer is UB; when a `vector<shared_ptr<Base>>` destructs, elements are destroyed from front to back (in insertion order).
- **`shared_ptr`'s deleter performs type erasure upon construction, so it can correctly destroy derived objects even if the base class destructor is non-virtual**; however, the more proper approach is still to give the base class a `virtual` destructor and use the lighter `unique_ptr`.
- **When subsystem types are fixed at compile time, prefer `std::variant` + `std::visit` to avoid virtual functions and heap allocation**; use polymorphism only when runtime dynamic extension is needed.

## References

- cppreference: [`std::shared_ptr` destructor semantics](https://en.cppreference.com/w/cpp/memory/shared_ptr/~shared_ptr) (since C++11, type-erased deleter at construction)
- cppreference: [`std::unique_ptr` with incomplete types / virtual destructors](https://en.cppreference.com/w/cpp/memory/unique_ptr) (since C++11)
- cppreference: [`std::variant` and `std::visit`](https://en.cppreference.com/w/cpp/utility/variant) (since C++17)
- GoF, *Design Patterns: Elements of Reusable Object-Oriented Software* — Chapter on Facade
- Companion compilable project: [Facade / HomeTheater](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/tree/main/code/volumn_codes/vol4/design-patterns/Facade/HomeTheater)
