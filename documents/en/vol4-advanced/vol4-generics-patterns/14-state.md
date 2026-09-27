---
title: 'State Machine Pattern: From a Mess of if/else to Type-Safe State Objects'
description: 'Starting from the most intuitive "keep the state in an enum and switch on it in every method" approach, we work our way step by step to the object-oriented State pattern, to a type-safe state machine built on variant + visit, and on to table-driven transitions, spelling out what each of the three styles costs, and closing with when a state machine earns its keep and when it doesn''t'
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
  - 'Singleton Pattern: From Comment-Only Constraints to Meyer''s Singleton'
  - 'Strategy Pattern: From a Heap of if/else to Compile-Time Swappable Policies'
translation:
  source: documents/vol4-advanced/vol4-generics-patterns/14-state.md
  source_hash: e54e87b48e485cdb3b74fcca7018afddf105bed9edabccc91f856e2fa30aa2cc
  translated_at: '2026-09-26T05:46:30+00:00'
  engine: anthropic
  token_count: 12000
---

# State Machine Pattern: From a Mess of if/else to Type-Safe State Objects

## What problem are we actually solving

Let's not start with a definition. Picture a thoroughly common scenario: you are writing a media player that can be `play()`-ed, `pause()`-ed, and `stop()`-ed. Sounds like nothing — but sit down to write it and the problem shows up: the very same `play()` should start playback in the "stopped" state, be a no-op in the "playing" state, and resume playback in the "paused" state. One event, landing in different states, produces entirely different behavior.

Most people's first instinct is to store the state in an `enum` and then `switch` on it inside every method:

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
                std::cout << "already playing\n";  // no-op
                break;
            case PlayerState::Paused:
                std::cout << "resume\n";
                state_ = PlayerState::Playing;
                break;
        }
    }
    void pause() { /* another switch, copying the above all over again */ }
    void stop()  { /* and copy it yet again */ }

private:
    PlayerState state_ = PlayerState::Stopped;
};
```

It runs. But once the number of states grows, this style shows its true face. Suppose you later add three more states — "fast-forwarding", "buffering", "error" — on top of the original three events: each of `play()`, `pause()`, `stop()` now needs several new `case`s grafted onto its existing `switch`, and **every new state forces you to touch every event handler at the same time** — miss one `case` and you have a bug, and the compiler won't warn you. Even worse, the transition rule "stop received while in Playing" physically lives inside `stop()`, a hundred thousand miles away from "pause received while in Playing". When reading the code, if you want to know how a state can unfold, you have to flip through all the methods; when you change one transition, you live in fear of breaking the others.

The state machine pattern exists to untangle exactly this mess. Its core idea fits in one sentence: **pull "state" out of the scattered pile of `case`s and turn it into independent objects/types, where each state cares only about how it responds to events, and the transition rules are localized in the code that departs from that state**. Now "how does the Paused state respond to events" lives entirely inside the single class `PausedState` — you can modify it without touching a hair on `PlayingState`; adding a new state means adding a new class, and not one line of the other states has to change.

But "turning states into objects" has several implementation routes in C++, with completely different costs. The object-oriented State pattern relies on virtual-function dispatch: flexible at runtime, but every transition may cost a heap allocation. `std::variant` + `std::visit` relies on type-safe closed-set dispatch: no virtual calls, no heap allocation, but the set of states must be nailed down at compile time. The most primitive `enum` + `switch` simply crams all states into one integer: fastest, but least resilient to change. So let's proceed step by step: start from that pile of `switch`es, see why it isn't enough, and let the later approaches force their way out of it.

## Step 1: The most primitive version — enum + switch (a cautionary example)

Let's write that `MediaPlayer` out in full, with all three events, and see where exactly it feels awkward:

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

First let's run this code and confirm the behavior is right. The driver below pushes the state machine through the sequence `play → pause → play → stop → pause`, which happens to cover all three valid transitions and two calls that "ought to be ignored":

```sh
$ g++ -std=c++23 -O2 -pthread state_switch_player.cpp -o state_switch_player
$ ./state_switch_player
[Stopped] start playing
[Playing] pausing
[Paused] resume
[Playing] stopping
[Stopped] pause() ignored
```

The behavior is fine. But the code's own flaws are hiding behind it.

First, **"the behavior of a single state gets chopped into N pieces"**. To answer "what does the Paused state actually do", you have to scan all three functions `play()`, `pause()`, `stop()`, pick out the `case PlayerState::Paused` lines in each, and stitch them back together. The more states and the more events, the worse the fragmentation. Second, **adding a new state means N edits**. Suppose you add a `Buffering` state: each of the three `switch`es in `play()`, `pause()`, `stop()` needs a new `case`; miss any one, and that state receiving that event will "silently do nothing" — in scenarios like protocol parsing or device control that is a bug, and the compiler says not a word, because it has no idea you "were supposed" to handle it. Third, the most fatal: **there is no centralized, readable transition-rule table behind the `switch`**. What the whole state machine looks like, you can only imagine.

The essence of the problem: the state is not encapsulated as an independent, self-responsible thing — it is just an integer being queried over and over by a `switch`. We first need to "extract" the states, letting each state take responsibility for its own event responses.

## Step 2: Pulling states out into objects — the State pattern

Let's flip the approach: since each state responds to events differently, make "the response" a virtual-function interface, and let each concrete state implement its own version. The "context" that holds the state (`MediaPlayer`) does nothing but forward events to the current state object, without caring which state it actually is.

There is a chicken-and-egg problem to settle first: `State`'s method signatures must accept a `MediaPlayer&` so they can switch states inside their responses, while `MediaPlayer` in turn must hold a `State`. Mutually dependent. The standard C++ solution is a **forward declaration** — declare the name `MediaPlayer` up front, and that's all `State` needs to refer to it by reference/pointer (references and pointers both need only a forward declaration, not a complete type):

```cpp
class MediaPlayer;  // forward declaration so State can use MediaPlayer&

struct State {
    virtual ~State() = default;
    virtual void play(MediaPlayer& ctx) = 0;
    virtual void pause(MediaPlayer& ctx) = 0;
    virtual void stop(MediaPlayer& ctx) = 0;
    virtual std::string name() const = 0;  // just for logging
};
```

Then the context. `MediaPlayer` forwards events to the current state object as-is, and additionally provides a `set_state()` for the state objects to switch with:

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

Look at that: `MediaPlayer` is now so clean it's practically a bare shell — it doesn't know which states exist, doesn't know the transition rules; it does exactly one thing: **forward the event to the current state, and allow the current state to swap itself for another**. All state-related logic has moved into the concrete state classes.

Next, the three concrete states. We first write out the declarations and the "no-op" branches (calls that are inherently meaningless, like `StoppedState::pause()`), and for the branches that switch state we declare them only, leaving the implementations for later. The reasoning: `PlayingState::pause()` needs to construct a `PausedState`, and `PausedState::play()` needs to construct a `PlayingState`; if the two class definitions nest into each other, the compiler gets stuck on "not completely defined yet". Move the implementations of the state-switching member functions out of the class, place them after all concrete state classes have been declared, and the deadlock breaks:

```cpp
struct StoppedState : State {
    void play(MediaPlayer& ctx) override;   // switch to Playing, implemented below
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
    void pause(MediaPlayer& ctx) override;  // switch to Paused, implemented below
    void stop(MediaPlayer& ctx) override;   // switch to Stopped, implemented below
    std::string name() const override { return "Playing"; }
};

struct PausedState : State {
    void play(MediaPlayer& ctx) override;   // switch to Playing, implemented below
    void pause(MediaPlayer& /*ctx*/) override {
        std::cout << "[Paused] pause() already paused\n";
    }
    void stop(MediaPlayer& ctx) override;   // switch to Stopped, implemented below
    std::string name() const override { return "Paused"; }
};
```

Finally, fill the switching logic into the implementations we left empty. At this point all three concrete state classes are complete types, so writing things like `make_shared<PlayingState>()` meets no obstacles:

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

Usage looks like this — you deal only with the `MediaPlayer` shell, and how the internal state switches is entirely not your concern:

```cpp
MediaPlayer player(std::make_shared<StoppedState>());
player.play();   // Stopped -> Playing
player.pause();  // Playing  -> Paused
player.play();   // Paused   -> Playing
player.stop();   // Playing  -> Stopped
player.pause();  // Stopped: pause() ignored
```

Let's verify here that this code really runs and the transition order is really right:

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

The transition chain `Stopped → Playing → Paused → Playing → Stopped`, and at the end, while stopped, pressing `pause()` is quietly ignored. Exactly as expected.

At this point you can already see where the State pattern beats that pile of `switch`es. **First, "how the Paused state responds to every event" all lives in the single class `PausedState`** — modifying it can't reach any other state. **Second, adding a new state is just adding a new class**: `Buffering` arrives, you write a `BufferingState`, decide inside it how it responds to `play`/`pause`/`stop`, and not one line of the other classes changes. This is precisely what the open-closed principle wants — open for extension (adding states without modifying the old ones), closed for modification (the old state classes' code never needs to be touched). **And on top of that, unit testing becomes genuinely pleasant**: construct a `PausedState` on its own, feed it events, and check whether it switches correctly — no need to spin up a whole `MediaPlayer`.

## Pitfall warning: shared_ptr allocates on every transition

::: warning A heap allocation on every transition
The State pattern above carries a cost that is easy to miss: **the line `set_state(std::make_shared<XxxState>())` performs a heap allocation on every state transition**. `make_shared` has to construct the object together with its control block (the reference count), and if the state machine sits on some high-frequency path — say a protocol parser that pushes the state machine once per byte received — that little allocation cost rapidly amplifies into a performance bottleneck.

A sneakier problem is that the state object's "identity" keeps changing. Every time you enter the Playing state you get a brand-new `PlayingState` instance. If the state itself needs to record data (say, "how many seconds have elapsed since entering Playing"), storing the data in the state object won't work — the data the previous `Playing` recorded is destroyed the moment you switch away — so this kind of state-dependent data can only hang off the context `MediaPlayer`.

There are two ways out. One: if your state objects **have no members and are pure behavior dispatch** (like the example above), make them shared instances — the state class is stateless, one `StoppedState` instance for the whole program is enough, and transitions do `set_state(StoppedState::instance())`, reusing the same `shared_ptr` and never allocating again. Two: when the set of states can be nailed down at compile time and performance is sensitive, abandon the `shared_ptr` route outright and use the next section's `std::variant`, eliminating heap allocation at the root.
:::

To put it bluntly, `shared_ptr` is here because **its interface is convenient** — state objects are polymorphic, passable across translation units, and lifetime-managed automatically by the reference count. The price is that one allocation. For a low-frequency state machine like a player UI, you couldn't even measure this overhead; but if you're writing a network protocol parser handling hundreds of thousands of packets per second, the `shared_ptr` route has to go.

## Step 3: Nailing the state set down at compile time — variant + visit

C++17 gave us `std::variant`, a **type-safe, closed-set union** — "closed set" is the key phrase: which types a `variant` can hold is fixed the moment you define it; nothing can be added at runtime. That dovetails exactly with the fact that "a state machine's set of states is finite and enumerable". We represent the state with a `variant`:

```cpp
struct Stopped {};
struct Playing {};
struct Paused {};

using PlayerState = std::variant<Stopped, Playing, Paused>;
```

Those three `struct`s are empty because our player states carry no data of their own. But understand where `variant`'s real power lies: **each state can carry its own data, and the compiler forces you to handle it correctly**. Add an `int resume_position;` to `Paused`, for instance, and to `visit` the `variant` you must handle that field in your dispatch logic, or it won't compile. That is a strong guarantee `enum + switch` cannot give — an `enum` state is no different from an ordinary integer, the "data it should carry" can only be stuffed into the context as an extra, and the compiler cannot check completeness for you.

This time the context `MediaPlayer` stores the state by value directly, and `visit` accepts a visitor "overloaded for every state", returning the new state:

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

Two things deserve a pause to get straight. First, **`std::visit`'s "closed set" here becomes a compile-time contract**: your visitor (those `Play`, `Pause`, `Stop` `struct`s) must provide a callable overload for every type in the `variant`; miss any single state and the code does not compile. This rescues you completely from the "a `switch` missed a `case` and silently does nothing" pit — with the State pattern, "forgetting to handle some state" goes from a runtime bug to a compile error. Second, **the `variant` is stored by value, and a transition is "compute the next `variant`, then assign it" — no heap allocation whatsoever**. This wipes out the allocate-on-every-transition cost of the previous section's `shared_ptr` route; the price is that the set of states must be fixed in place at compile time — adding a state at runtime means editing source code.

Let's verify here that the variant version's transition chain matches the previous two versions:

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

Same transition chain, same semantics. But note carefully: **`variant` + `visit` is not a zero-cost abstraction**. Let's compare — run the `variant` version and a naive `switch` version through fifty million transitions each and time them:

```sh
$ g++ -std=c++23 -O2 -pthread state_bench.cpp -o state_bench
$ ./state_bench
variant: 240 ms  (sink=0)
switch:  8 ms  (sink=2)
```

The gap is large, and we need to be honest about where it comes from. The `switch` version is transparent to the compiler at a glance (an `enum` is just an integer, and the jump table is one `jmp`), so it's fast enough that almost nothing is left but the loop overhead; the `variant` version constructs a new `variant` every iteration and goes through the `visit` dispatch table — plus my visitor chains the three branches together with `if constexpr`, which doesn't help the compiler fold them — so it comes out roughly thirty times slower. **The honest conclusion is not "variant is always slow"**, but rather "variant + visit trades the cost of 'virtual functions + heap allocation' for 'closed-set dispatch table + by-value construction'; at the frequency of most business state machines that cost is negligible, but compared with the plainest switch it is still not zero". If your state machine truly sits on a hot path where every nanosecond is haggled over, the answer is to go back to `enum + switch`.

## Step 4: Writing transitions as data — table-driven

By now we have two flavors of "behavior-driven state machine" (the State pattern and the variant), and they share a trait: **the transition rules and the action code are mixed together inside the state classes' member functions**. That fits well when state behavior is complex. But sometimes your state machine is, at heart, a table of "from where, received what, go where, and do a little something along the way" — extremely regular rules, extremely simple actions. Then writing the transition rules as **data** instead of code is far clearer: you can sweep all the transitions in one glance, even serialize them into a config file for hot loading.

First we define the data structure of one transition: it has `from` (current state), `on` (event), an optional `guard` (guard condition), an optional `action` (side effect), and `to` (target state):

```cpp
struct Transition {
    State from;
    Event on;
    std::function<bool()> guard;   // optional; if it returns false, this rule doesn't apply
    std::function<void()> action;  // optional; executed on transition
    State to;
};
```

The state machine itself holds a table of these and a current state; on receiving an event it walks down the table to find the first rule where `from == current && on == event && guard()` holds, executes its `action`, and changes the current state to `to`:

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

Run it and see whether each rule in the table matches as expected:

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

Note the third event: `Green` receives `Emergency`, matches the fourth rule in the table, the `action` fires and prints `force red`, and the state becomes `Red`. Immediately after, the fourth event is `Red` receiving `Emergency` again — no rule with `from==Red && on==Emergency` exists in the table, so it gets "ignored" (`on_event` returns `false`). That's the benefit of table-driven: **all the rules of the entire state machine are visible in one sweep of `table_`**, which state ignores which event is obvious at a glance, and adding a rule means adding one row to the table — the core dispatch logic (that `on_event` loop) doesn't change a single line.

Table-driven fits especially well scenarios with "many rules, simple actions, and a need for visualization or configurability" — workflow engines, regex engines, communication-protocol state transitions. Its costs deserve equally honest treatment: `std::function` itself has to store a callable object and usually involves one heap allocation (especially for lambdas capturing large objects), and the `for` loop linearly scans the whole table, so as states multiply and events thicken, the matching cost climbs. If you need faster, build the table as something like `std::unordered_map<std::pair<State, Event>, Transition>` that hashes directly on `(from, on)`, swapping the linear scan for an O(1) lookup.

## How to choose among the three styles

Let's comb through this evolution path once and see clearly what each step was trading off:

| Style | Cost | Strengths | Who it suits |
|---|---|---|---|
| `enum` + `switch` | Bloated once states multiply; adding a state means N edits; a missed `case` goes unnoticed | Fastest, zero allocation, obvious at a glance | Few states, performance-sensitive, simple transitions (embedded interrupt handling, protocol header parsing) |
| State pattern (`shared_ptr`) | Heap allocation per transition, virtual dispatch, unstable state identity | State behavior highly localized, best open-closed compliance, easy to test | Complex state behavior, low-frequency transitions, states added at runtime (GUIs, players) |
| `variant` + `visit` | State set fixed at compile time; closed-set dispatch has non-zero overhead | Type-safe (missed state = compile error), no heap allocation, can carry state-dependent data | Fixed state set, state-dependent data, performance requirements (compiler front ends, parsers) |
| Table-driven | `std::function` may allocate; linear scan | Rules centralized and readable, serializable, hot-swappable | Many rules with simple actions, visualization or configurability needed (workflows, protocol state machines) |

How to pick? First ask "will the set of states change at runtime?" If yes — or if state behavior is especially complex and wants OOP-style organization — choose the State pattern and accept the cost. If no, then the type safety and zero allocation `variant` gives you are almost always better than a bare `enum` — unless you really are running one of those "every nanosecond counts" hot paths, in which case honestly stick with `switch`. If the rules are table-shaped in their multitude and the actions are simple, go straight to table-driven, and while you're at it serialize the table into a config.

## When you shouldn't use a state machine

Honestly, not all code that "has state" deserves a state machine. The abstraction itself has a cost — you must define state classes/variants/tables, write dispatch logic, and write tests for every transition. That cost only pays off when there are enough states and the transitions are twisty enough.

If you have only two or three states and a handful of transitions, an `enum` plus a `switch` finishes the job; don't force the State pattern just to "use a pattern" — that only sends the code on a detour before it returns to something you can understand at a glance. If you find clear parent-child relationships among your states (say, "running" splitting into "manual" and "auto", and "auto" splitting further into "accelerating" and "cruising"), a flat state machine starts to choke — expand every parent-child combination into flat states and the state count explodes exponentially. What you want then is not the flat state machine of this article but a **hierarchical state machine (Hierarchical State Machine / Statechart)**: events a substate doesn't handle bubble up automatically to the parent state, which can uniformly handle "what must be done regardless of which substate we're in". In C++, such needs usually go straight to existing libraries (Boost.SML, Boost.Statechart); hand-rolling a hierarchical state machine generally has a poor effort-to-payoff ratio.

One last piece of engineering intuition: **write the state machine first with the simplest `switch`, get it running, and only when it has truly started to bloat and truly started to hurt, consider whether to switch to variant or the State pattern**. Patterns exist to solve real complexity, not to strike a pose on day one.

## Summary

Note down these key takeaways:

- **The core problem a state machine solves** is "the same event behaves differently in different states": pull that state-dependent behavior out of the scattered `switch`es and localize it into "each state responsible for itself".
- **`enum` + `switch` is fastest but least resilient to change**: adding a state means N edits, and the compiler ignores a missed `case`.
- The **State pattern** encapsulates each state's behavior into a class via virtual functions — best open-closed compliance, easiest to test — but `shared_ptr` heap-allocates on every transition. When states have no members, making them shared instances eliminates the allocation.
- **`variant` + `visit`** nails the state set down at compile time with closed-set types: missing one state fails the compile outright, with no heap allocation; the cost is that states can't be added at runtime and `visit` dispatch isn't zero-cost.
- **Table-driven** writes transitions as data — rules centralized, readable, serializable, hot-swappable — fitting scenarios with many rules and simple actions, at the cost of `std::function`'s potential allocations and matching overhead.
- Don't adopt a state machine just to "use a pattern". Few states? Use `switch`. Clear parent-child states? Consider a hierarchical state machine or even an existing library (Boost.SML).

::: tip The companion compilable project
This section's examples have a complete compilable project under `code/volumn_codes/vol4/design-patterns/State/` in the repo (`.h` + main + `CMakeLists.txt`); `cmake -S . -B build && cmake --build build` reproduces the outputs above.
:::

## References

- [cppreference: `std::variant`](https://en.cppreference.com/w/cpp/utility/variant) (since C++17, a type-safe closed-set union)
- [cppreference: `std::visit`](https://en.cppreference.com/w/cpp/utility/variant/visit) (closed-set dispatch over a variant, since C++17)
- [cppreference: `enum class`](https://en.cppreference.com/w/cpp/language/enum) (strongly typed enumerations, since C++11)
- Gamma, Helm, Johnson, and Vlissides, *Design Patterns*, the State chapter (the original GoF, object-oriented state machines)
- Robert C. Martin, *Clean Architecture*, Chapter 22, "Shaping Architecture" (state machines and finite automata in business modeling)
