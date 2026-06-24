---
title: 'State Machine Pattern: From a Mess of if/else to Type-Safe State Objects'
description: Starting from the most intuitive approach—storing states in an `enum`
  and switching behaviors with a `switch`—we progressively derive the object-oriented
  State Pattern, the type-safe `variant` + `visit` state machine, and finally table-driven
  transitions. We clarify the trade-offs of these three styles, and ultimately determine
  when to use a state machine and when to avoid it.
chapter: 11
order: 14
tags:
- host
- cpp-modern
- intermediate
- 状态机
difficulty: intermediate
platform: host
cpp_standard:
- 11
- 17
- 20
reading_time_minutes: 22
related:
- 单例模式:从注释约束到 Meyer's Singleton
- 策略模式:从一堆 if/else 到编译期可替换的 Policy
translation:
  source: documents/vol4-advanced/vol4-generics-patterns/14-state.md
  source_hash: e54e87b48e485cdb3b74fcca7018afddf105bed9edabccc91f856e2fa30aa2cc
  translated_at: '2026-06-24T00:59:57.005869+00:00'
  engine: anthropic
  token_count: 4701
---
# State Machine Pattern: From a Mess of if/else to Type-Safe State Objects

## What problem are we actually solving?

Let's skip the formal definitions for a moment. Consider a very common scenario: you are writing a media player that can be controlled via `play()`, `pause()`, and `stop()`. This sounds simple enough, but once you start coding, you'll run into a problem—specifically, the behavior of `play()` depends entirely on the current state. When "stopped", it should start playback; when "playing", it should be a no-op; and when "paused", it should resume playback. The same event triggers completely different behaviors depending on the state.

Many developers' first instinct is to store the state using an `enum` and then use a `switch` statement inside every method:

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

It works. However, as the number of states grows, the flaws of this approach become glaringly apparent. Suppose you later add three new states—"Fast Forward", "Buffering", and "Error"—on top of the existing three events. Now, your `play()`, `pause()`, and `stop()` methods each need several new `case` branches added to their `switch` statements. Furthermore, **every time you add a state, you must modify every single event handler simultaneously**. If you miss just one `case`, you introduce a bug, and the compiler won't warn you. Even worse, the transition rule for "stop received while in Playing state" is physically scattered inside the `stop()` method, miles away from the code for "pause received while in Playing state". When reading the code, to understand how a state behaves, you have to flip through all the methods; when modifying a transition, you risk breaking other methods.

The State Pattern exists to resolve this entanglement. Its core idea can be summarized in a single sentence: **extract "states" from a scattered pile of `case` statements into independent objects or types, where each state only cares about how it responds to events, and transition rules are localized within the code that "originates from this state"**. This way, "how the Paused state responds to events" resides entirely within the `PausedState` class; modifying it won't affect `PlayingState` in the slightest. Adding a new state simply means adding a new class; the existing states remain untouched.

However, "extracting states into objects" can be implemented in C++ via several paths, each with vastly different trade-offs. The object-oriented State Pattern relies on virtual function dispatch, offering runtime flexibility but often requiring heap allocation for transitions. `std::variant` + `std::visit` relies on type-safe closed-set dispatch, avoiding virtual calls and heap allocation, but the set of states must be fixed at compile time. The most primitive `enum` + `switch` approach simply crams states into an integer; it is the fastest but the least resilient to change. Let's walk through this step-by-step, starting with that pile of `switch` statements to see why it falls short, and gradually forcing our way towards better implementations.

## Step 1: The Most Primitive Approach — enum + switch (The Anti-Pattern)

Let's flesh out the `MediaPlayer` example above with three events to see exactly where the awkwardness lies:

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

Let's run this code first to verify that its behavior is correct. If we step through the state machine in the driver below using the sequence `play → pause → play → stop → pause`, we execute exactly three valid transitions and two calls that should be ignored:

```sh
$ g++ -std=c++23 -O2 -pthread state_switch_player.cpp -o state_switch_player
$ ./state_switch_player
[Stopped] start playing
[Playing] pausing
[Paused] resume
[Playing] stopping
[Stopped] pause() ignored
```

The behavior is fine. However, the inherent flaws in the code are lurking beneath the surface.

First, **"the behavior of a single state is split across N places."** If you want to know what the `Paused` state actually does, you have to scan through the `play()`, `pause()`, and `stop()` functions, picking out the `case PlayerState::Paused` lines from each to piece together the logic. As the number of states and events grows, this fragmentation becomes severe. Second, **adding a new state requires N modifications**. Suppose you want to add a `Buffering` state. You would need to add a `case` to each of the three `switch` statements in `play()`, `pause()`, and `stop()`. If you miss even one, that state will "silently do nothing" when that event occurs—which is a bug in scenarios like protocol parsing or device control—and the compiler won't say a word, because it doesn't know what you "intended" to handle. Third, and most fatal—**there is no centralized, readable transition rule table on the `switch`**. You have to mentally visualize the entire structure of the state machine.

The root of the problem is that the state is not encapsulated as an independent, self-responsible entity; it is merely an integer queried repeatedly by `switch` statements. We need to "extract" the state first, allowing each state to handle its own event responses.

## Step 2: Extracting States into Objects — The State Pattern

Let's switch approaches: since the response to events differs for every state, we can make "response" a virtual function interface, where each concrete state implements its own version. The "context" holding the state (`MediaPlayer`) is only responsible for forwarding events to the current state object, without caring which specific state it is.

We need to solve a chicken-and-egg problem first: the method signature of `State` needs to accept `MediaPlayer&` to switch states within the response, but `MediaPlayer` also needs to hold `State`. This is a circular dependency. The standard C++ solution is **forward declaration**—we declare the name `MediaPlayer` first, and `State` can reference it via a pointer or reference (pointers and references only need a forward declaration, not a full definition):

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

Next is the context. The `MediaPlayer` forwards events directly to the current state object, while providing a `set_state()` method for the state object to switch states:

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

You see, `MediaPlayer` is now stripped down to almost nothing but a shell. It knows nothing about the specific states or the transition rules; it does only one thing: **pass the event to the current state and allow that state to replace itself with another**. All state-related logic has been moved into the concrete state classes.

Next, we have three concrete states. Let's first write out the declarations and the "no-op" branches (calls like `StoppedState::pause()` that are meaningless by nature), and leave the branches requiring state transitions declared but implemented later. The reason for this is that `PlayingState::pause()` needs to construct a `PausedState`, while `PausedState::play()` needs to construct a `PlayingState`. If the definitions of these two classes are nested within each other, the compiler will get stuck because the definitions aren't complete yet. By moving the implementations of member functions that require state transitions outside the class definition, and placing them after all concrete state classes have been declared, we can break this deadlock:

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

Finally, we fill in the switching logic into the implementation we left empty earlier. At this point, all three concrete state classes are complete types, so writing `make_shared<PlayingState>()` and similar code presents no obstacles:

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

Here is how we use it. We only interact with the `MediaPlayer` shell, and we don't need to worry about how the internal state switches at all:

```cpp
MediaPlayer player(std::make_shared<StoppedState>());
player.play();   // Stopped -> Playing
player.pause();  // Playing  -> Paused
player.play();   // Paused   -> Playing
player.stop();   // Playing  -> Stopped
player.pause();  // Stopped: pause() ignored
```

Let's first verify that this code actually runs and that the conversion order is correct:

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

The transition chain `Stopped → Playing → Paused → Playing → Stopped` works as expected, and pressing `pause()` while already `Stopped` is silently ignored.

At this point, you can see where the State pattern outperforms that tangled `switch` statement. **First, "how the Paused state responds to all events" is entirely contained within the `PausedState` class**—modifying it doesn't affect other states. **Second, adding a new state just means adding a new class**: if `Buffering` comes along, you write a `BufferingState`, define how it reacts to `play`/`pause`/`stop`, and you don't have to touch a single line of the other classes. This is exactly what the Open/Closed Principle is all about—open for extension (adding states doesn't require changing old ones), closed for modification (existing state classes remain untouched). **Third, unit testing becomes straightforward**: you instantiate a `PausedState` in isolation, feed it events, and verify that it transitions correctly, without needing to spin up a full `MediaPlayer`.

## Pitfall Warning: `shared_ptr` Allocates on Every Transition

::: warning Heap allocation on every transition
The State pattern implementation above has a cost that is easily overlooked: **the line `set_state(std::make_shared<XxxState>())` performs a heap allocation on every single state transition**. `make_shared` constructs both the object and the control block (reference count). If your state machine sits on a hot path—for example, parsing a protocol where every incoming byte triggers a state change—this allocation overhead will quickly amplify into a performance bottleneck.

A more subtle issue is that the "identity" of the state object changes. Every time you enter the `Playing` state, you get a brand new `PlayingState` instance. If the state itself needs to store data (e.g., "seconds played since entering Playing"), storing it in the state object won't work because the data from the previous `Playing` instance is destroyed when you switch away. This state-dependent data must be attached to the context, the `MediaPlayer`.

There are two solutions. First, if your state objects **have no members and are pure behavior dispatchers** (like the example above), make them shared instances—stateless state classes. A single global instance of `StoppedState` is sufficient for the whole program. During transitions, reuse the same `shared_ptr` via `set_state(StoppedState::instance())` to avoid allocation. Second, if the set of states is fixed at compile time and performance is critical, abandon the `shared_ptr` approach entirely and use `std::variant` from the next section to eliminate heap allocation fundamentally.
:::

Essentially, we are using `shared_ptr` here for its **convenient interface**—state objects are polymorphic, can be passed across translation units, and have their lifecycles managed automatically by reference counting. The cost is that single allocation. For a low-frequency state machine like a media player UI, this overhead is negligible; but if you are writing a network protocol parser processing hundreds of thousands of packets per second, the `shared_ptr` approach needs to be replaced.

## Step 3: Fixed State Set at Compile Time — `variant` + `visit`

C++17 gives us `std::variant`, a **type-safe, closed union**—"closed" being the keyword: the types a `variant` can hold are frozen the moment you define it; they cannot be added at runtime. This aligns perfectly with the fact that "a state machine's set of states is finite and enumerable." We represent the states using a `variant`:

```cpp
struct Stopped {};
struct Playing {};
struct Paused {};

using PlayerState = std::variant<Stopped, Playing, Paused>;
```

Here, those three `struct`s are empty because our player state itself doesn't carry data. However, you should understand that the true power of `variant` lies in this: **each state can carry its own data, and the compiler enforces that you handle it correctly**. For example, if we add an `int resume_position;` to `Paused`, you must handle this field within your dispatch logic when you `visit` the `variant`, otherwise the code won't compile. This is a strong guarantee that `enum + switch` cannot provide—an `enum` state is no different from a plain integer, and any "associated data" must be stuffed into the surrounding context; the compiler cannot verify completeness for you.

In the context of `MediaPlayer`, we store the state directly by value. `visit` accepts a visitor that has "an overload for each state" and returns the new state:

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

There are two points worth clarifying. First, **the "closed set" of `std::visit` becomes a compile-time contract here**: your visitor (the `struct`s like `Play`, `Pause`, and `Stop`) must provide a callable overload for every type in the `variant`. If you miss any state, the code will not compile. This completely saves you from the pit of "silently doing nothing because a `switch` missed a `case`"—in the State pattern, "forgetting to handle a state" changes from a runtime bug to a compile-time error. Second, **`variant` stores by value, and transitions involve "calculating the next `variant` and assigning it," with absolutely no heap allocation**. This eliminates the allocation-per-transition overhead of the `shared_ptr` approach from the previous section. The trade-off is that the set of states must be hardcoded at compile time; adding states at runtime requires modifying the source code.

Let's verify that the transition chain in the `variant` version is consistent with the previous two versions:

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

The same conversion chain, the same semantics. However, please note that **`variant + visit` is not a zero-overhead abstraction**. Let's compare the performance by timing the `variant` version against a naive `switch` version, running fifty million iterations:

```sh
$ g++ -std=c++23 -O2 -pthread state_bench.cpp -o state_bench
$ ./state_bench
variant: 240 ms  (sink=0)
switch:  8 ms  (sink=2)
```

The performance gap here is significant, so we need to be honest about where it comes from. The compiler can see right through the `switch` version (the `enum` is just an integer, leading to a single `jmp` via a jump table), so it is fast enough that the overhead is essentially just the loop itself. The `variant` version, however, constructs a new `variant` and traverses the `visit` dispatch table every time. Additionally, my visitor chains the three branches together using `if constexpr`, which hinders compiler optimization/folding, resulting in a roughly thirty-fold slowdown. **The real conclusion is not that "variant is always slow"**, but rather that "variant + visit trades the cost of 'virtual functions + heap allocation' for 'closed-set dispatch tables + construction by value'. For the frequency of most business logic state machines, this overhead is negligible, but it is still non-zero compared to the most primitive switch." If your state machine truly resides on a hot path where every nanosecond counts, the answer is to go back to `enum + switch`.

## Step 4: Write transitions as data — Table-driven

At this point, we have explored two types of "behavior-driven state machines" (State pattern, variant). Their shared characteristic is: **transition rules and action code are intermixed within the member functions of the state classes**. This is appropriate when state behavior is complex. However, sometimes your state machine is essentially a table of "from where, received what, go where, and optionally do something." When the rules are very regular and the actions are very simple, writing the transition rules as **data** is much clearer than writing them as code — you can scan all transitions at a glance, or even serialize them into a configuration file for hot-reloading.

First, let's define a data structure for a transition. It includes `from` (current state), `on` (event), an optional `guard` (guard condition), an optional `action` (side effect), and `to` (target state):

```cpp
struct Transition {
    State from;
    Event on;
    std::function<bool()> guard;   // 可选,返回 false 则这条规则不生效
    std::function<void()> action;  // 可选,转换时执行
    State to;
};
```

The state machine itself holds such a table and a current state. Upon receiving an event, it scans the table for the first rule where `from == current && on == event && guard()` holds true, executes its `action`, and updates the current state to `to`:

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

Let's run it and check if every rule in the table matches as expected:

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

Pay attention to the third event: `Green` receives `Emergency`, which matches the fourth rule in the table, triggering the `action` to print `force red`, and the state becomes `Red`. Immediately following, the fourth event is `Red` receiving `Emergency` again—there is no rule in the table matching `from==Red && on==Emergency`, so it is "ignored" (`on_event` returns `false`). This demonstrates the benefit of being table-driven: **for all rules of the entire state machine, you just scan `table_` once to see everything**. It is crystal clear which state ignores which event, and adding a rule is just adding a row to the table; the core dispatch logic (the loop in `on_event`) doesn't need a single line changed.

The table-driven approach is particularly suitable for scenarios with "many rules, simple actions, and a need for visualization or configuration," such as workflow engines, regex engines, or communication protocol state transitions. We must be honest about its cost: `std::function` itself stores a callable object and usually involves a heap allocation (especially when capturing large objects in a lambda), and the `for` loop performs a linear scan of the entire table. As the number of states grows and events become dense, the matching cost increases. If you need more speed, you can convert the table to `std::unordered_map<std::pair<State, Event>, Transition>` or similar, to perform a direct hash lookup on `(from, on)`, replacing the linear scan with an O(1) lookup.

## How to Choose Between the Three Approaches

Let's walk through this evolutionary path to see exactly what trade-offs we are making at each step:

| Approach | Cost | Strength | Best For |
|---|---|---|---|
| `enum` + `switch` | Bloated with many states; adding a state requires N changes; missed `case` is unchecked | Fastest, zero allocation, clear at a glance | Few states, performance-sensitive, simple transitions (embedded interrupt handling, protocol header parsing) |
| State Pattern (`shared_ptr`) | Heap allocation on every transition, virtual dispatch, unstable state identity | Highly localized state behavior, best Open/Closed Principle, easy to test | Complex state behavior, low-frequency transitions, runtime state addition (GUIs, media players) |
| `variant` + `visit` | State set fixed at compile time, closed-set dispatch has non-zero overhead | Type safety (missed state = compile error), no heap allocation, can carry state-specific data | Fixed state set, state-specific data, performance requirements (compiler frontends, parsers) |
| Table-Driven | `std::function` may allocate, linear scan | Centralized readable rules, serializable, hot-swappable | Many rules with simple actions, need visualization/configuration (workflows, protocol state machines) |

How to choose? First, ask "will the set of states change at runtime?" If yes, or if state behavior is very complex and needs to be organized in an OOP style, choose the State Pattern and accept the cost. If no, then the type safety and zero allocation of `variant` are almost always better than a raw `enum`—unless you are truly running a hot path where "every nanosecond counts," in which case stick to `switch`. If the rules are as numerous as a spreadsheet and the actions are simple, go straight to table-driven, and serialize the table into a configuration.

## When NOT to Use a State Machine

Honestly, not all "stateful" code should use a state machine. The abstraction of a state machine has inherent costs—you have to define state classes/variants/tables, write dispatch logic, and write tests for every transition. This cost is only worth it when there are enough states and the transitions are complex enough.

If you only have two or three states and three or five transitions, an `enum` and a `switch` will do. Don't force the State Pattern just to "use a pattern"; it will only make the code take a detour to get back to something you could understand at a glance. If you find clear parent-child relationships between states (e.g., under "Running" there is "Manual" and "Automatic", and under "Automatic" there is "Accelerating" and "Cruising"), a flat state machine starts to struggle—exploding all parent-child combinations into flat states causes exponential growth in the number of states. What you need then is not the flat state machine discussed here, but a **Hierarchical State Machine (Statechart)**: events unhandled by a substate automatically bubble up to the parent state, and the parent can uniformly handle "things that should happen regardless of the substate." In C++, such requirements usually call for existing libraries (Boost.SML, Boost.Statechart), as the ROI for hand-rolling a hierarchical state machine is generally poor.

One final engineering intuition: **start with the simplest `switch` to get the state machine working. Wait until it actually starts to balloon and become painful, then consider switching to variant or the State Pattern.** Patterns are for solving real complexity, not for showing off from the start.

## Summary

Take note of these key conclusions:

- **The core problem a state machine solves** is "the same event behaves differently in different states," extracting this "state-dependent behavior" from scattered `switch` statements and localizing it so that "each state is responsible for itself."
- **`enum` + `switch` is the fastest but least resilient to change**: adding a state requires changes in N places, and the compiler doesn't care if you miss a `case`.
- **The State Pattern** encapsulates each state's behavior as a class using virtual functions. It has the best Open/Closed Principle and is the easiest to test, but `shared_ptr` means a heap allocation on every transition. Making states shared instances when they have no members can eliminate the allocation cost.
- **`variant` + `visit`** fixes the state set at compile time using a closed-set type. Missing a state results in a direct compilation error, and there is no heap allocation; the cost is that states cannot be added at runtime and `visit` dispatch is not zero-cost.
- **Table-Driven** approaches treat transitions as data. Rules are centralized, readable, serializable, and hot-swappable, suitable for scenarios with many rules and simple actions. The cost is the potential allocation of `std::function` and matching overhead.
- Don't use a state machine just to "use a pattern." If there are few states, use `switch`. If parent-child states are obvious, consider hierarchical state machines or even existing libraries (Boost.SML).

::: tip Companion Compilable Project
The examples for this section are in the repository at `code/volumn_codes/vol4/design-patterns/State/` as a complete, compilable project (`.h` + main + `CMakeLists.txt`). Run `cmake -S . -B build && cmake --build build` to reproduce the outputs shown above.
:::

## References

- [cppreference:`std::variant`](https://en.cppreference.com/w/cpp/utility/variant) (Since C++17, type-safe closed-set union)
- [cppreference:`std::visit`](https://en.cppreference.com/w/cpp/utility/variant/visit) (Closed-set dispatch on variant, since C++17)
- [cppreference:`enum class`](https://en.cppreference.com/w/cpp/language/enum) (Strongly-typed enumeration, since C++11)
- Gamma, Helm, Johnson, Vissides, *Design Patterns* - State Pattern (GoF original, OOP state machine)
- Robert C. Martin, *Clean Architecture*, Chapter 22 "Shaping Architecture" (Application of state machines and finite automata in business modeling)
