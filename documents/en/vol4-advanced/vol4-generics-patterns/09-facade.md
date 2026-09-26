---
title: 'Facade Pattern: Stuffing a Mess of Subsystem Collaboration into One Entry Point'
description: 'Start from the rawest "the client orchestrates a pile of subsystems itself" version, squeeze the Facade pattern out of it step by step, draw the line between facade and encapsulation, get a home-theater facade running with polymorphism + shared_ptr, and finish by exposing the God Object as the most common abuse'
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
  - 'Singleton Pattern: From Comment-Only Constraints to Meyer''s Singleton'
prerequisites:
  - Classes and Object-Oriented Programming
  - 'Chapter 9: Smart Pointers and Ownership'
translation:
  source: documents/vol4-advanced/vol4-generics-patterns/09-facade.md
  source_hash: 2107ed9e6fff8b41bc6e5c042b9043655fba2b37cc8d37259a6758ed303f483f
  translated_at: '2026-09-26T05:15:42+00:00'
  engine: anthropic
  token_count: 7500
---

# Facade Pattern: Stuffing a Mess of Subsystem Collaboration into One Entry Point

## What Problem Are We Actually Solving

Hold off on the definition for a moment. Picture a scenario you will find very familiar: you are writing a media player for a dev board, and underneath it sits five subsystems — `NetworkStream` pulls the stream, `VideoDecoder` and `AudioDecoder` each handle one decoding path, `Renderer` pushes the frames onto the screen, and `SubtitleEngine` loads and syncs subtitles. The subsystems themselves are all written cleanly, each doing exactly one thing. What actually hurts is none of them individually — it is **getting them to cooperate in the right order**.

With no encapsulation at all, your client code (say, some `MainWindow::on_play_clicked` slot) ends up looking like this:

```cpp
// The client orchestrates the subsystems directly, with no intermediate layer
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

It looks like it would run, but you know in your gut that a pile of problems is buried here. First, the client must **remember** the sequence — open the stream first, then initialize the decoders, then `setup` the renderer, and sync the subtitles last; get the order wrong once (say, calling `render_frame` before `setup`) and you get either a crash or garbled frames. Second, this sequencing knowledge is **hard-coded inside the caller's head**: bring in another caller (a test case that wants to exercise playback failure, a command-line tool that wants to play files in bulk) and the whole long chain has to be copied over verbatim, again. Worse still is error handling: if `vdec.init` fails, the client has to remember to go back and close the already-opened `stream` — and this "halfway down the success path something fails and we must clean up in reverse" logic doubles with every subsystem you add; miss one and you have a resource leak.

The Facade pattern exists precisely for this. GoF defines it as: **providing a unified interface to a set of interfaces in a subsystem**. Honestly, that definition reads a bit convoluted, so let's put it in plain words: **a facade does not invent new capability — it purposefully wraps a bunch of subsystems you already have, each doing its own thing, into a single unified entry point, organized by responsibility**. The client degenerates from "I must remember how to orchestrate five subsystems" to "I just call `player.play(url)`" — as for opening the stream, decoding, rendering, subtitles, rolling back on error, all of that is now the facade's own business.

So let's go step by step and watch this "entry point" grow from thin to thick.

## Step One: The Thinnest Facade — an Entry Point That Orchestrates

Let's not try to reach the finished article in one jump; start with the thinnest possible layer: write a class that moves that whole long sequence inside verbatim and exposes only two actions to the outside — "start playing" and "stop playing". At the very least, the client no longer has to memorize the order.

```cpp
// MediaPlayerFacade.h
#pragma once
#include <memory>
#include <string>

class MediaPlayerFacade {
public:
    MediaPlayerFacade();
    ~MediaPlayerFacade();

    bool play(const std::string& url);  // Orchestrates the whole set of subsystems; returns true on success
    void stop();

private:
    std::unique_ptr<NetworkStream> stream_;
    std::unique_ptr<VideoDecoder> vdec_;
    std::unique_ptr<AudioDecoder> adec_;
    std::unique_ptr<Renderer> renderer_;
    std::unique_ptr<SubtitleEngine> subtitle_;
};
```

Several details here deserve a pause. First, the facade **owns all the subsystems itself** (the `unique_ptr` members) — that is the biggest difference from "stuffing the five subsystems straight into the client": the client no longer holds any subsystem, lifetimes are managed centrally by the facade, and when the facade is destroyed, the five `unique_ptr`s automatically release their subsystems in the reverse order of declaration. The client does not write a single line of cleanup code. Second, `play` returns `bool` rather than `void`, which means we explicitly surface the fact that "playing may fail" to the caller, while **not exposing the specific reason for the failure** (the facade swallows the details and writes them into its log). That is a significant trade-off, and we will come back later to whether it is a reasonable one.

In the implementation, `play` simply moves that sequence inside as-is:

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

    // Main loop (simplified): keep reading packets, decoding, rendering...
    return true;
}
```

Now the client has it easy — all it writes is:

```cpp
MediaPlayerFacade player;
if (!player.play("https://example.com/stream.m3u8")) {
    std::cerr << "播放失败\n";
}
```

Going from "the client must remember an 8-step order" to "a single line of `play`" is already worth the price of admission on its own. But if you stop right here, this facade is still missing one extremely important piece — **error handling and cleanup**.

## Step Two: Pulling Error Handling and Resource Cleanup Inside Too

We are not done yet. The previous `play` assumed that no subsystem ever fails — fine for a demo, guaranteed to blow up in production. The reality: `stream_->open` can fail from network jitter, `vdec_->init` can fail because the codec parameters are invalid, `renderer_->setup` can fail because the window could not be acquired. Once some step in the middle fails, **the subsystems already initialized before it must be shut down in reverse** — otherwise it is a leak.

This "halfway through the success path something fails and we need to roll back in reverse" logic is precisely the responsibility a facade should carry — because only the facade knows the complete sequence, and only it knows "which ones to close on failure, and in what order". We put the cleanup logic into a `stop()`, then trigger it from `play` with an exception or an early return. Here I will use an exception + `catch` to demonstrate this "unified cleanup" style:

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
        stop();   // Unified cleanup: whatever was opened first gets closed last
        return false;
    }
}
```

You will notice that in this style the `catch` block does not care which step threw — it just calls `stop()` for one thorough cleanup and returns `false`. **The facade flattens away all the details of "where the error came from" and reports only "worked / did not work" to the outside** — that is exactly the facade's value in error handling: the client does not need a different cleanup branch for every kind of failure; the facade paves that road flat for it.

`stop()` itself simply releases the subsystems it already holds under the principle "whatever was opened first gets closed last" — in practice, that is calling `reset()` on every `unique_ptr` member:

```cpp
void MediaPlayerFacade::stop() {
    subtitle_.reset();   // Subtitles closed first (they were opened last)
    renderer_.reset();   // Renderer off
    adec_.reset();       // Audio decoder off
    vdec_.reset();       // Video decoder off
    stream_.reset();     // Stream closed last (it was opened first)
}
```

This "reverse shutdown" order is deliberate: the renderer still points at the frames coming out of the decoders — if you close the decoders first and the renderer second, then when the renderer's destructor goes to touch that frame memory, it is a dangling access. So always **close what was opened last, first**. Actually, the more carefree approach is to not write `stop()` at all and rely on member destruction order instead: member variables are destroyed in the reverse order of declaration, so as long as you arrange the member declarations in the header so that "what opens first is declared last", destruction naturally shuts things down in reverse. But that requires everyone to forever keep "declaration order = shutdown order" in mind — the day somebody reorders the members, this implicit convention quietly breaks. That is why I still lean toward writing an explicit `stop()`, whose intent is obvious at a glance.

## Hands-On: A Home-Theater Facade That Actually Runs

Talking only about a media player is too abstract, so let's do a minimal example that really compiles and really runs. This `HomeTheater` is a typical facade: it manages four subsystems — `LightController` (the lights), `DVDPlayer` (the disc player), `SoundSystem` (the audio), and `Projector` (the projector). When the client wants to watch a movie, it just calls `watch_movie()` and the facade powers the four subsystems on **in the correct order**; when the movie is over, it calls `close_movie()` and the facade shuts them down in the correct order. The client is completely unaware that these four subsystems exist.

First, we define the subsystems. There is one important design decision here: although the four subsystems do different jobs, they **all share the "can turn on / can turn off" interface**. This is a classic "a group of same-kind objects" scenario, so we unify it with polymorphism — pull out a `HomeTheaterBaseComponents` base class that offers pure virtual `on()` / `off()`:

```cpp
#pragma once
#include <memory>
#include <print>
#include <vector>

struct HomeTheaterBaseComponents {
    virtual ~HomeTheaterBaseComponents() = default;   // Polymorphic base; do not omit the virtual destructor
    virtual void on() noexcept = 0;
    virtual void off() noexcept = 0;
};
```

::: warning Do not slip up here
The base class destructor **must** be `virtual` (writing `= default` is fine, as long as it carries `virtual`). Forget the `virtual`, and deleting a derived object through a base-class pointer later is undefined behavior — the derived part's destructor never runs, and resources leak silently. This rule is especially easy to trip over in facade scenarios, because a facade typically manages a pile of derived objects through a "container full of base-class pointers" — prime UB territory.
:::

Then each of the four subsystems implements the interface. Each subsystem just prints one line in `on` / `off`, plus construction and destruction logs (later we will rely on those logs to verify the facade's orchestration order):

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

Next comes the facade itself. The facade holds a container full of base-class smart pointers — here we use `std::vector<std::shared_ptr<HomeTheaterBaseComponents>>`. Why `shared_ptr` rather than `unique_ptr`? We will get to that specifically in a moment; first, here is what the facade looks like:

```cpp
class HomeTheater {
public:
    HomeTheater() {
        // Assemble the subsystems; the order is "the order in which we want them lit up at power-on"
        components_.push_back(std::make_shared<LightController>());
        components_.push_back(std::make_shared<DVDPlayer>());
        components_.push_back(std::make_shared<SoundSystem>());
        components_.push_back(std::make_shared<Projector>());
    }

    void watch_movie() {
        for (auto& each : components_) {
            each->on();   // Polymorphic call: whichever gets touched lights up
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

Look at what the facade did: it **assembled** the whole set of subsystems in its constructor, pushing them into the container in the desired order; `watch_movie` is a single loop calling `on()` on each in turn, never caring whether `each` is a light or a projector — polymorphism dispatches on its behalf; `close_movie` likewise. The client code is obscenely clean:

```cpp
#include "HomeTheater.h"

int main() {
    HomeTheater theater;
    theater.watch_movie();   // One line, all four subsystems on
    theater.close_movie();   // One line, all four subsystems off
}
```

### Verify It First: Is the Order Right

Let's not take it on faith — compile and run this, and check whether `on()` and `off()` really happen in the order we pushed things in, and in what order destruction happens:

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

The construction order at power-on is Light→DVD→Sound→Projector; `watch_movie` powers on in exactly that order, and `close_movie` powers off in the same order (first to last). The destruction order also looks like "first to last" — and here is a detail worth expanding on: when a `std::vector` is destroyed, it destroys its elements **front to back**, one by one. Let's write a separate minimal example to verify that order:

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

Confirmed: a `vector` destroys its elements front to back (FIFO, insertion order), so in the home theater Light is destroyed first and Projector last. If what you need is "last opened closes first" (reverse shutdown), then you must iterate in reverse manually inside `close_movie`, or simply stop leaning on the vector's implicit destruction order — spell the intent out; do not make readers guess.

## Why shared_ptr, and One Slightly Counter-Intuitive Point

Back to that question from a moment ago: why does the facade use `std::vector<std::shared_ptr<...>>` rather than `std::vector<std::unique_ptr<...>>`? In this home-theater example, purely from an ownership standpoint, `unique_ptr` would actually suffice — the facade exclusively owns the subsystems, there is no sharing to be had, and `unique_ptr` is lighter and more fitting.

But when it comes to "stuffing into a container", `shared_ptr` has one advantage many people do not know about, and it deserves its own discussion. When you write `std::shared_ptr<Base> sp = std::make_shared<Derived>();`, the `shared_ptr` internally **records how to destroy the derived type at the very moment of construction** (it stores a type-erased deleter). This means that **even if `Base`'s destructor is not `virtual`, destroying the `shared_ptr` still correctly invokes `~Derived()`**. Let's write a minimal example comparing how `shared_ptr` and `unique_ptr` behave when "the base destructor is non-virtual":

```cpp
#include <iostream>
#include <memory>

struct Base {
    Base() { std::cout << "  Base()\n"; }
    ~Base() { std::cout << "  ~Base()\n"; }   // Note: not virtual
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

The difference is plain as day: in the `shared_ptr` version, `~Derived()` is correctly invoked, while the `unique_ptr<Base>` version **invokes only `~Base()` — `~Derived()` is simply skipped** (technically this is undefined behavior, usually showing up as a leak of the derived part's resources). The reason is that the `shared_ptr`'s deleter is type-erased at construction: at the very moment of `make_shared<Derived>()`, it already remembered "call `~Derived()`"; whereas the `unique_ptr<Base>`'s deleter is statically bound — all it knows is that it holds a `Base*`, so destruction is `delete (Base*)`, and with a non-virtual base destructor that can only reach `~Base()`.

::: tip Which one to choose
This rule is not a license to reach for `shared_ptr` mindlessly. For the vast majority of facade scenarios, **give the base class a `virtual ~Base() = default;`, then use `unique_ptr` — done**: it is lighter, its ownership semantics are clearer (exclusive), and there is no atomic reference-counting overhead. There is exactly one situation where you need to be aware of this `shared_ptr` difference: when you cannot modify the base class (say, it comes from a third-party library and its destructor is not virtual) yet you absolutely must stuff derived objects into a `unique_ptr<Base>` container — there, `shared_ptr` is a lifesaving workaround. But the more proper fix remains "find a way to make the base destructor virtual", or drop polymorphism altogether and switch to `std::variant`.
:::

## Do Not Go Too Far: a Facade Is Not for Rewriting the Subsystems

At this point we have a facade that runs, powers on and off correctly, and lets the client get away with two lines. But we are still not done — I have to be honest with you: **of all the GoF patterns, Facade is the easiest one to abuse**. The reason is simple: it is far too handy. The moment you notice "the client has to call several classes", you wrap a facade around them, and wrap, and wrap — and what you harvest is not a facade but a **God Object**.

What do I mean? A facade's proper job is **orchestration and abstracting the flow**, not "reimplementing the business logic". A good facade looks like this: it knows who to turn on first and who last, and how to roll back on failure, but it **does not make decisions on the subsystems' behalf**. In the home theater, `watch_movie` only does "on, one by one" — it does not judge "should the projector warm up for 30 seconds first"; warm-up is `Projector`'s own responsibility, and the facade merely calls it in order.

But if you abuse the facade and stuff every last detail into one big facade, it will slowly swallow the subsystems' responsibilities until it turns into a monster like this:

```cpp
// Counter-example: the facade is "rewriting" the subsystems' business logic
class MegaHomeTheaterFacade {
public:
    void watch_movie() {
        // Dim the lights, preheat the projector 30 s, switch the sound system to cinema mode, jump the DVD track, stretch the subtitles...
        // All crammed into the facade; every subsystem degenerates into a "dumb box that only knows on/off"
        lights_.set_brightness(0);
        std::this_thread::sleep_for(std::chrono::seconds(30));  // Preheating? That is the projector's job
        sound_.set_mode(kCinema);
        dvd_.seek_chapter(1);
        // ...
    }
};
```

This facade has several obvious code smells. First, it **steals the subsystems' responsibilities** — preheating the projector for 30 seconds is clearly `Projector::warm_up()`'s job, yet now the facade sleeps 30 seconds on its behalf, which amounts to moving business knowledge out of the subsystem and into the facade; the subsystem degenerates into a dumb box that only does as it is told. Second, it **becomes impossible to unit-test** — you want to test the "projector warm-up" logic, only to find it welded inside the facade's `watch_movie`, forcing you to construct an entire home theater just to test one little warm-up. Third, the facade **becomes hard to maintain** — any change in any subsystem's details requires touching the facade; the facade is no longer "the orchestrator of the flow" but "the gathering place of all business logic" — which is exactly the problem facade set out to eliminate, resurrected in a different spot.

The simple test for whether a facade has gone bad is this one line: **no code for "concrete business decisions" should appear in a facade — only code for "in what order, call whom, how to roll back"**. The moment you catch yourself writing parameters like "how long the projector should preheat" or "by what factor the subtitles should stretch" inside the facade, be on guard — those are the subsystems' responsibilities; the facade merely calls them.

## Going Further: Replacing the Polymorphic Container with std::variant

In the home-theater example, the four subsystems' **types are in fact fully determined at compile time** (exactly these four, never extended dynamically). For this "closed set" scenario, we do not necessarily have to bring out the whole polymorphism + heap-allocation apparatus; we can use C++17's `std::variant` to pack them into a container enumerable at compile time, **completely avoiding virtual function calls and heap allocation**:

```cpp
#include <variant>
#include <vector>
#include <print>

using Component = std::variant<
    LightController, DVDPlayer, SoundSystem, Projector>;

class HomeTheaterVariant {
public:
    HomeTheaterVariant() {
        components_.reserve(4);  // reserve first, to avoid triggering a variant copy/move during reallocation
        components_.emplace_back(std::in_place_type<LightController>);
        components_.emplace_back(std::in_place_type<DVDPlayer>);
        components_.emplace_back(std::in_place_type<SoundSystem>);
        components_.emplace_back(std::in_place_type<Projector>);
    }

    void watch_movie() {
        for (auto& c : components_) {
            std::visit([](auto& comp) { comp.on(); }, c);  // Compile-time dispatch, no virtual functions
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

The benefits of this style are tangible: no heap allocation (a `variant` is a value type, inlined directly in the `vector`'s contiguous memory), no virtual table lookup (`std::visit` generates all the branches at compile time), and cache friendliness far above a scatter of heap objects. The cost is that the four subsystems must each define their own `on()` / `off()` members (no common base class to inherit, no `virtual` needed), and **this container's set of types is nailed down at compile time** — to insert a fifth kind, an `Amplifier`, at runtime, you would have to edit the `Component` variant's type list and recompile.

So the trade-off is crisp: **if the kinds of subsystems are fixed at compile time and few in number, prefer `std::variant` (value semantics, zero virtual-function overhead); if the kinds must extend dynamically at runtime, or come from plugins, polymorphism + base-class pointers is the only way**. The home theater, the fixed handful of states inside a state machine, the fixed stages of a compile pipeline — all belong to the former camp, and `std::variant` is often the more modern, better-fitting tool.

## When to Use a Facade, and When Not To

Let's walk through the trade-offs once. Facade shines in the scenario "you need to wrap the collaboration logic of a group of complex subsystems into one stable, simple entry point" — the home theater here (one power-on/off flow), a media player (one pull-stream / decode / render flow), a database connection pool (one acquire / return / health-check flow), a compiler front end (one preprocess / lex / parse flow). In these scenarios the facade's value is very real: it reduces direct coupling between the client and the subsystems, centralizes error handling and lifetime management, and lets you modify the underlying implementation without disturbing its users.

But several signals say you should not add a facade, or that the facade you already have deserves to be split up. First, **the subsystems are thin to begin with** (only one or two, and their interfaces are already clean) — wrapping a facade around them is pure redundancy, adding an indirection layer with no real payoff. Second, **the facade starts swallowing business logic** (like that `MegaHomeTheaterFacade` earlier) — what you need then is not more facade, but handing the responsibilities back to the subsystems. Third, **different clients have wildly divergent orchestration needs for the same set of subsystems** (client A wants the full set, client B only two of them, client C a completely different order) — one big facade cannot hold every usage; you should consider **writing a dedicated small facade for each class of client**, or simply letting clients pick and call the subsystems directly. Forcing every client through the same big facade only fills it with "if path A do this, if path B do that" branches — and there you go, sliding toward a God Object again.

## Summary

Let's trace the whole thread once more:

| Stage | Approach | Why it is still not enough |
|---|---|---|
| Client orchestrates subsystems directly | The 5 subsystem calls written verbatim in the client | Sequencing knowledge hard-coded in the caller; every new caller copies it all again; failure rollback scattered everywhere |
| Thin facade | One class owns all the subsystems, exposes `play()`/`stop()` | Assumes nothing fails; no error handling or cleanup |
| Facade with cleanup | try/catch in `play`, failure calls `stop()` to close in reverse | Good enough — the client now only cares about "worked / did not" |
| Polymorphic-container facade | `vector<shared_ptr<Base>>` + virtual `on/off` | For a closed set, heap allocation and virtual functions are wasted overhead |
| `std::variant` facade | Value-type container + `std::visit` compile-time dispatch | Not applicable when the kinds of subsystems must extend dynamically |

Note down these key conclusions:

- **The essence of the Facade pattern is not inventing new capability, but purposefully wrapping existing subsystems into one unified entry point by responsibility** — what it solves is the fact that "clients should not have to know the subsystems' orchestration details".
- **A facade's proper job is "orchestrate the flow + handle errors + manage lifetimes"**, not rewriting business logic; the moment concrete business decisions appear inside the facade (warm up for how many seconds, stretch subtitles by what factor), it starts sliding toward a God Object.
- **A polymorphic base class's destructor must be virtual**, otherwise deleting a derived object through a base-class pointer is UB; a `vector<shared_ptr<Base>>` destroys its elements front to back (insertion order).
- **`shared_ptr`'s deleter is type-erased at construction, so it destroys the derived object correctly even when the base destructor is non-virtual**; but the more proper approach remains giving the base a `virtual` destructor and then using the lighter `unique_ptr`.
- **When the kinds of subsystems are fixed at compile time, prefer `std::variant` + `std::visit` to avoid virtual functions and heap allocation**; only reach for polymorphism when runtime extension is genuinely needed.

## References

- cppreference: [`std::shared_ptr` destructor semantics](https://en.cppreference.com/w/cpp/memory/shared_ptr/~shared_ptr) (since C++11; the deleter is type-erased at construction)
- cppreference: [`std::unique_ptr` with incomplete types / virtual destructors](https://en.cppreference.com/w/cpp/memory/unique_ptr) (since C++11)
- cppreference: [`std::variant` and `std::visit`](https://en.cppreference.com/w/cpp/utility/variant) (since C++17)
- GoF, *Design Patterns: Elements of Reusable Object-Oriented Software* — the Facade chapter
- Companion compilable project: [Facade / HomeTheater](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/tree/main/code/volumn_codes/vol4/design-patterns/Facade/HomeTheater)
