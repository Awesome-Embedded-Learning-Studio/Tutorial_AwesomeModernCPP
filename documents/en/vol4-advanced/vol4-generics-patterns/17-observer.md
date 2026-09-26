---
title: 'Observer Pattern: From Dangling Pointers to Dangle-Proofing with `weak_ptr`'
description: 'Starting from the most intuitive "observable holds a bunch of raw pointers" design, we run headlong into the dangling crash when an observer dies first, then clean up lifetime management with `weak_ptr` — and along the way build an event source with RAII subscription, snapshot notification, and reentrant thread safety.'
chapter: 11
order: 17
tags:
  - host
  - cpp-modern
  - intermediate
  - 观察者模式
  - weak_ptr
  - 回调机制
difficulty: intermediate
platform: host
cpp_standard: [11, 17, 20]
reading_time_minutes: 22
related:
  - 'Singleton Pattern: From Comment-Only Constraints to Meyer''s Singleton'
prerequisites:
  - 'Chapter 6: Classes and Object-Oriented Programming'
  - 'Smart Pointers and Ownership'
translation:
  source: documents/vol4-advanced/vol4-generics-patterns/17-observer.md
  source_hash: 314206f7881fdc071a9103fc20074a09b893fdb876b4d2bc942e5fc837c45301
  translated_at: '2026-09-26T05:43:40+00:00'
  engine: anthropic
  token_count: 12000
---

# Observer Pattern: From Dangling Pointers to Dangle-Proofing with `weak_ptr`

## What Problem Are We Actually Solving

Let's skip the formal definition for now. Think of the most common scenario: you're building a weather station. A background `WeatherForecast` periodically pulls new temperature and humidity readings from sensors, then needs to distribute the data to several different display endpoints — a default console display, a mobile app push endpoint, and a big TV-wall screen. These three endpoints look nothing alike and iterate on completely different schedules. Sure, you could hardcode all of them inside `WeatherForecast` and have the station call `phone.show(...)`, `tv.show(...)`, and so on one by one — but you'd fall apart fast: every new endpoint (say, a web dashboard) means going back to edit the weather station's source; every removed endpoint means another round of deleting calls. The weather station should care about exactly one thing — "new data arrived" — yet it ends up being forced to know every display device in the world.

This is exactly the class of requirement the Observer pattern addresses: **fully decouple the "data source" from "a crowd of components that care about data changes" — the data source only fires one broadcast when its state changes; whoever wants to listen subscribes, stops listening whenever they like, and the data source knows none of them**. Weather stations, UI event buses, stock quote pushes, keypress state-change notifications — they all share the same natural demand: "one change must be announced to a crowd of listeners whose identities we don't know."

But "fire one broadcast" is **absolutely not as simple as writing a loop that calls functions one by one** in C++. There's a pitfall unique to C++, nastier than in any other language: **a listener may be destroyed while the weather station is still mid-broadcast**. Java has GC, Python has reference counting as a safety net, but object lifetimes in C++ are managed by hand — once a listener dies first, what the weather station holds is a pointer to a ghost; the next broadcast accesses freed memory, and the program either crashes, spews garbage, or flips over on the spot under ASan. So the question this article really answers is: **how do we let the observable broadcast changes such that no matter when an observer is destroyed, no dangling pointer ever appears**.

So let's proceed step by step: start from the most intuitive approach, see why each step falls short, and squeeze out the modern C++ standard answer at the end.

## Step 1: The Most Intuitive Approach — The Observable Holds a Set of Raw Pointers

The structure most people sketch subconsciously the first time they write the Observer pattern looks like this: an abstract observer interface, an observable that internally maintains a list of observers, and on state change, a loop over the list invoking each callback. Let's take the weather station from the playground as our blueprint and extract its skeleton first:

```cpp
struct MessagePackage {
    double temperature;
    double humidity;
};

// Abstract observer interface: every "endpoint that wants notifications" implements it
struct Sender {
    virtual ~Sender() = default;
    virtual void receiving_message(const MessagePackage& message) = 0;
};

// A few concrete endpoints
struct DefaultSender : Sender {
    void receiving_message(const MessagePackage& message) override {
        std::println("Receiving Message: Temperature: {}, Humidity: {}",
                     message.temperature, message.humidity);
    }
};

struct PhoneSender : Sender {
    void receiving_message(const MessagePackage& message) override {
        std::println("Hello from Message! Temperature: {}, Humidity: {}",
                     message.temperature, message.humidity);
    }
};

struct TVSender : Sender {
    void receiving_message(const MessagePackage& message) override {
        std::println("Hello from TV! Temperature: {}, Humidity: {}",
                     message.temperature, message.humidity);
    }
};
```

Nothing treacherous in this part. A polymorphic interface is standard equipment for the Observer pattern, and `virtual ~Sender() = default` guarantees that destruction through a base-class pointer correctly reaches the derived destructor — never omit this line: as long as your observers can be destroyed through a `Sender*`, a missing virtual destructor is undefined behavior. Next comes the observable; here is the most intuitive, raw-pointer version first:

```cpp
class WeatherForecast {
public:
    void register_observer(Sender* o) { observers_.push_back(o); }
    void detach_observer(Sender* o) {
        observers_.erase(std::remove(observers_.begin(), observers_.end(), o));
    }
    void notify_once() {
        for (auto* o : observers_) {
            o->receiving_message(sensor_.get_message_pack());
        }
    }
private:
    struct WeatherSensor {
        static MessagePackage get_message_pack();
    };
    std::vector<Sender*> observers_;
};
```

See, this is the minimal skeleton of the Observer pattern: `register_observer` stuffs an observer's address into the list, and `notify_once` walks the list and calls each one on state change. Logically it's completely correct; as long as the observers stay alive, this code runs beautifully.

But the problem lies precisely in that precondition — "as long as the observers stay alive." Let's write a minimal usage scenario: register the address of a stack object with the weather station, then let that object leave scope before the station broadcasts:

```cpp
int main() {
    WeatherForecast forecast;
    {
        DefaultSender obs;          // stack object
        forecast.register_observer(&obs);
        forecast.notify_once();     // obs is still alive here; fine
    }                              // obs leaves scope, stack frame reclaimed
    forecast.notify_once();        // the pointer to obs now dangles -> use-after-free
}
```

When the second `notify_once()` runs, `observers_` still confidently holds that `&obs` — but `obs` was reclaimed long ago. That line is an access to already-freed stack memory. Claims need proof, so let's compile it with AddressSanitizer and see what actually happens.

## Let's Verify First: Do Dangling Pointers Really Blow Up

Let's write a minimal reproducer that deliberately lets the observer leave scope before the notification:

```cpp
#include <iostream>

struct Observer {
    virtual ~Observer() = default;
    virtual void on_event(int v) = 0;
};

class Subject {
public:
    void subscribe(Observer* o) { observers_[count_++] = o; }
    void notify(int v) {
        for (int i = 0; i < count_; ++i) observers_[i]->on_event(v);
    }
private:
    static constexpr int kMax = 4;
    Observer* observers_[kMax];
    int count_ = 0;
};

struct Loud : Observer {
    int id;
    explicit Loud(int i) : id(i) {}
    void on_event(int v) override { std::cout << "Loud " << id << " got " << v << "\n"; }
};

int main() {
    Subject s;
    {
        Loud obs(1);
        s.subscribe(&obs);
        s.notify(10);              // obs is alive here; fine
    }                              // obs leaves scope -> pointer dangles
    s.notify(20);                  // use-after-free
}
```

Compile with ASan, then run:

```sh
$ g++ -std=c++23 -O0 -fsanitize=address -g observer_dangle.cpp -o observer_dangle
$ ./observer_dangle
=================================================================
==89061==ERROR: AddressSanitizer: stack-use-after-scope on address 0x...030
READ of size 8 at 0x...030 thread T0
    #0 Subject::notify(int) observer_dangle.cpp:16
    #1 main observer_dangle.cpp:40
SUMMARY: AddressSanitizer: stack-use-after-scope observer_dangle.cpp:16
```

ASan catches a `stack-use-after-scope` on the spot — `notify` at line 16 dereferences an `observers_[i]` that has already left scope, accessing reclaimed stack memory. That's the real price of a dangling pointer: in production builds without ASan, it might show up as "reading a pile of garbage values", as "an occasional segfault", or as "works fine on my machine, crashes on CI" — the classic, hardest-to-reproduce kind of bug. **With raw pointers as observers, whenever lifetimes don't line up, you will step into this pit sooner or later.**

The problem is now clear: what we lack isn't the ability to broadcast, but governance of "**what should happen to the pointer the observable holds once the observer dies**". Let's tame it step by step.

## Step 2: Let the Observable Hold a `shared_ptr` — Plugs the Dangle, but Breeds Zombies

Since the pit is "observer dies first, pointer dangles", the most intuitive fix is to hand over ownership too — let the observable hold a `shared_ptr<Observer>`, so that as long as the observable lives, the observer lives with it and the pointer never dangles. This is exactly how the playground blueprint's `WeatherForecast` does it:

```cpp
class WeatherForecast {
public:
    void register_observer(std::shared_ptr<Sender> sender) {
        observers_.push_back(std::move(sender));
    }
    void detach_observer(std::shared_ptr<Sender> sender) {
        observers_.erase(
            std::remove(observers_.begin(), observers_.end(), sender),
            observers_.end());
    }
    void notify_once() {
        for (auto& each : observers_) {
            each->receiving_message(sensor_.get_message_pack());
        }
    }
private:
    struct WeatherSensor {
        static MessagePackage get_message_pack();
    };
    std::vector<std::shared_ptr<Sender>> observers_;
};
```

After this change, the dangling pointer is indeed gone — `shared_ptr`'s reference count guarantees that as long as the vector still stores a copy, the object won't be destructed. But a new contradiction appears here, and it's sneakier than the dangle: **the observable quietly takes over ownership of the observer**, and "zombie observers" are born. Let's look at how the playground's `main` uses it:

```cpp
int main() {
    WeatherForecast forecast;
    forecast.register_observer(std::make_shared<DefaultSender>());
    forecast.register_observer(std::make_shared<PhoneSender>());
    forecast.register_observer(std::make_shared<TVSender>());
    forecast.notify_once();
}
```

Note that `std::make_shared<DefaultSender>()` creates a **temporary object**; after it's passed into `register_observer`, the reference count is taken over by the copy in the vector, and the count is 1 — held by the observable alone. Sounds fine? Let's verify what it leads to:

```cpp
// Suppose we use it like this inside some function
void run(WeatherForecast& forecast) {
    auto phone = std::make_shared<PhoneSender>();
    forecast.register_observer(phone);
    std::cout << "phone 还在,引用计数 = " << phone.use_count() << "\n";
}   // phone leaves scope, the outside reference is gone
// But forecast's shared_ptr remains -> PhoneSender isn't dead; it's a "zombie" now
// Later forecast.notify_once() will still call it
```

Let's actually run this zombie behavior:

```sh
$ g++ -std=c++23 -O2 -pthread observer_verify.cpp -o observer_verify && ./observer_verify
=== verify_zombie (strong ref keeps dead observer alive) ===
outside ref dropped next, but subject holds one
notify after outside dropped its ref:
  observer 3 got 999
(observer 3 was meant to die but subject kept it alive)
```

Look: the outside world dropped its reference to observer 3 long ago, so it should be dead — but because the observable holds a `shared_ptr`, it lingers on and keeps getting notified. There are two serious consequences. **First, the lifetime has been hijacked** — when the observer dies is no longer decided by its creator but held hostage by the observable. This is especially deadly in UI scenarios (a view that should be destroyed when its window closes gets pinned alive by the event source: memory leak plus logic corruption). **Second, the ownership semantics are polluted** — `shared_ptr` means "shared ownership, destruct when the last owner lets go", but in the Observer pattern the observable doesn't want to own the observer at all; it merely wants to "be able to notify the observer while it's alive". Those are two completely different demands.

Worse, this line of thinking pushes the lifetime problem from one extreme to the other: with raw pointers, "the observable manages nothing at all — observers may die whenever they like, and it never knows"; with `shared_ptr`, "the observable forcibly takes over — observers can't die even if they want to". What we want is neither extreme but the delicate balance point in between — **the observable knows whether an observer is still around, but doesn't prevent it from dying**.

## Step 3: Dangle-Proofing with `weak_ptr` — Observe Without Owning, and Know When It Died

`weak_ptr` is tailor-made for exactly this balance point. Its semantics are precisely what we want: **it doesn't bump the reference count or extend the object's lifetime, yet at any moment it can check "is the object still there" — if yes, borrow a temporary `shared_ptr` to work with; if no, honestly report that it's gone**. Swap the observable's stored reference from `shared_ptr` to `weak_ptr`, and the whole lifetime governance suddenly becomes tractable:

```cpp
#include <memory>
#include <vector>

class WeatherForecast {
public:
    // Accept a shared_ptr, but store only a weak_ptr — doesn't extend the observer's lifetime
    void register_observer(const std::shared_ptr<Sender>& sender) {
        observers_.push_back(sender);   // shared_ptr -> weak_ptr implicit conversion
    }
    void notify_once() {
        MessagePackage pack = WeatherSensor::get_message_pack();
        for (auto it = observers_.begin(); it != observers_.end(); ) {
            if (auto live = it->lock()) {        // The key: try upgrading the weak ref back to shared
                live->receiving_message(pack);   // Upgrade succeeded -> object alive, notify it
                ++it;
            } else {
                it = observers_.erase(it);       // Upgrade failed -> object dead, clean up while we're at it
            }
        }
    }
private:
    struct WeatherSensor {
        static MessagePackage get_message_pack();
    };
    std::vector<std::weak_ptr<Sender>> observers_;
};
```

The core is the single line `it->lock()`. `weak_ptr::lock()` is an atomic operation ([util.smartptr.weak.obs]) that returns a new `shared_ptr`: if the managed object is still alive, the new `shared_ptr` points to it and the reference count goes up by one; if the object has already been destructed, it returns an empty `shared_ptr`. Before going further we should verify that `lock` behaves the way we expect, because the safety of this entire pattern rests on that one semantic.

## Let's Verify First: How `weak_ptr::lock()` Actually Behaves

Let's write a minimal example: bind a `weak_ptr` to a `shared_ptr` and call `lock()` in both situations — object alive and object destroyed:

```cpp
#include <iostream>
#include <memory>

static void verify_weak_lock() {
    std::weak_ptr<int> w;
    {
        auto sp = std::make_shared<int>(42);
        w = sp;
        auto locked = w.lock();                 // object alive
        std::cout << "alive: use_count=" << locked.use_count()
                  << " value=" << (locked ? *locked : 0) << "\n";
        std::cout << "expired()=" << std::boolalpha << w.expired() << "\n";
    }                                           // sp leaves scope, object destructed
    auto locked = w.lock();                     // object already destroyed
    std::cout << "after destroy: locked.empty=" << (locked == nullptr)
              << " expired=" << w.expired() << "\n";
    if (auto p = w.lock()) {
        std::cout << "UNEXPECTED: got value " << *p << "\n";
    } else {
        std::cout << "lock failed -> skip callback (no crash)\n";
    }
}

int main() { verify_weak_lock(); }
```

Compile and run:

```sh
$ g++ -std=c++23 -O2 -pthread observer_verify.cpp -o observer_verify
$ ./observer_verify
=== verify_weak_lock ===
alive: use_count=2 value=42
expired()=false
after destroy: locked.empty=true expired=true
lock failed -> skip callback (no crash)
```

Look: while the object is alive, the `shared_ptr` returned by `lock()` pushes the reference count to 2 (the original sp plus the one from lock), and `expired()` is false; after the object is destructed, `lock()` returns a null pointer and `expired()` is true, so we skip the callback accordingly — nothing crashes. This is the cornerstone of the entire dangle-proof pattern — **at the moment of notification, temporarily upgrade the `weak_ptr` into a `shared_ptr`; as long as that temporary reference exists, the object absolutely cannot be destructed mid-callback**. That's a hard guarantee `weak_ptr` gives us.

Now let's run the complete "observer dies first, notification stays safe" scenario once more to confirm dangle-proofing really holds:

```sh
$ ./observer_verify
=== verify_weak_observer (weak_ptr prevents dangle) ===
notify with observer 2 alive:
  observer 2 got 100
notify after observer 2 destroyed:
(no crash: dead observers were skipped by lock)
```

observer 1 is a temporary destructed right after registration, and observer 2 is destructed at the end of the inner scope — every later notification fails `lock()` on them, skips them automatically, and the program marches on undisturbed. Compare that with the earlier `stack-use-after-scope` crash under ASan: same "observer dies first" situation, and the weak_ptr version doesn't so much as sneeze. **This is the modern C++ standard answer for governing observer lifetimes: don't own, but know.**

## Step 4: RAII Subscription — Destruction Is Unsubscription

At this point the dangling pointer is solved, but one tail remains: after an observer is destructed, its `weak_ptr` stays in `observers_`. Each notification can still recognize and clean these up, but the list gradually accumulates a pile of dead weak references — they waste memory and force every notification through a batch of doomed `lock()` calls. A more elegant approach is to have the observer unsubscribe proactively **at the very moment of its own destruction** — but here's a chicken-and-egg problem: when the observer is being destructed, its own `this` is about to become invalid, so how does it unsubscribe?

The answer is an **RAII subscription token**. Subscribing doesn't return void; it returns an object that holds "the information needed to unsubscribe" (a pointer to the observable plus a subscription id), and its destructor performs the unsubscription. The observer stores this token as a member, so when the observer is destructed, its members get destructed first — the token destructs — the unsubscribe completes — and only then does the observer's own body destruct. The ordering is naturally correct:

```cpp
#include <cstddef>
#include <functional>
#include <unordered_map>
#include <memory>
#include <mutex>

class WeatherForecast {
public:
    using Callback = std::function<void(const MessagePackage&)>;

    // RAII subscription token: unsubscribes automatically on destruction
    class Subscription {
    public:
        Subscription() = default;
        Subscription(std::size_t id, WeatherForecast* owner)
            : id_(id), owner_(owner) {}
        Subscription(const Subscription&) = delete;
        Subscription& operator=(const Subscription&) = delete;
        Subscription(Subscription&& o) noexcept
            : id_(o.id_), owner_(o.owner_) { o.owner_ = nullptr; o.id_ = 0; }
        Subscription& operator=(Subscription&& o) noexcept {
            if (this != &o) { unsubscribe(); id_ = o.id_; owner_ = o.owner_;
                               o.owner_ = nullptr; o.id_ = 0; }
            return *this;
        }
        ~Subscription() { unsubscribe(); }
        void unsubscribe() {
            if (owner_) { owner_->detach_by_id(id_); owner_ = nullptr; id_ = 0; }
        }
    private:
        std::size_t id_ = 0;
        WeatherForecast* owner_ = nullptr;
    };

    // Subscribe: register the callback together with the weak_ptr, return the RAII token
    Subscription subscribe(Callback cb) {
        std::lock_guard<std::mutex> lk(mtx_);
        std::size_t id = next_id_++;
        callbacks_.emplace(id, std::move(cb));
        return Subscription{id, this};
    }

    void detach_by_id(std::size_t id) {
        std::lock_guard<std::mutex> lk(mtx_);
        callbacks_.erase(id);
    }

private:
    std::mutex mtx_;
    std::unordered_map<std::size_t, Callback> callbacks_;
    std::size_t next_id_ = 1;
};
```

This design turns "unsubscribing" from something the programmer must remember to do into something a destructor guarantees automatically — as long as the token destructs, the unsubscribe will happen. It's the same spirit of RAII we discussed in the Singleton chapter: **hand the constraint to a language mechanism instead of a human-memory convention**. The observer holds a `Subscription` member; when it dies, the token dies with it, the unsubscribe completes with it, and the observable's list is never littered with the corpses of dead weak references.

But let me honestly point out a trade-off here: the RAII subscription token uses an "id + callback" model rather than an "id + weak_ptr to the observer object" model. The reason is that a `std::function` callback can capture arbitrary state (including a `weak_ptr<Observer>`), which is more flexible than hard-wiring an observer interface — you can write object-oriented code like `subscribe([this](auto& p){ view_.on_change(p); })`, or a fully object-free purely functional callback. The cost is that `std::function` performs a heap allocation (plus the potential missed inlining from type erasure); in extremely performance-sensitive, extremely high-frequency-notification scenarios, going back to the bare `Observer*` interface + `weak_ptr` set is cheaper. For most business scenarios this overhead is entirely negligible.

## Pitfall Warning: Mutating the Subscription List Inside notify

::: warning Pitfall warning
Sooner or later you will hit this scenario: inside its own callback, an observer decides, based on what it received, "I don't need to listen anymore, I'm unsubscribing", or "I want to register another new observer". Sounds perfectly reasonable — but if you call `erase` or `push_back` while `notify` is mid-iteration over `observers_`, you trigger iterator invalidation: a crash, a few observers getting skipped, or some getting called twice.

The root cause: **the notification path and the add/remove path operate on the same container**. One side iterates while the other mutates the structure — STL containers make no promise of working correctly under such concurrent modification. The correct approach is **snapshot notification**: on entering `notify`, first copy all current callbacks into a local vector under the lock, then **release the lock** and iterate over that copy outside the lock, calling each one. Now a callback may mutate `observers_` however it likes — it modifies the original table while the notification walks the copy, and the two never interfere. After the notification finishes, apply the adds and removes accumulated during the round all at once (typically stashed in two buffers, `pending_add_` / `pending_remove_`, processed together when the outermost `notify` exits).
:::

Let's verify the snapshot path actually works:

```cpp
#include <iostream>
#include <functional>
#include <vector>

class Subject {
public:
    using Cb = std::function<void(int)>;
    void subscribe(Cb cb) { observers_.push_back(std::move(cb)); }

    // snapshot version: copy first, then invoke; callbacks may mutate the original table freely
    void notify_good(int v) {
        std::vector<Cb> snap(observers_);     // take a copy
        for (auto& cb : snap) cb(v);          // iterate the copy
    }
private:
    std::vector<Cb> observers_;
};

int main() {
    Subject s;
    int hits = 0;
    s.subscribe([&](int v){ ++hits; std::cout << "A got " << v << "\n"; });
    s.subscribe([&](int v){ ++hits; std::cout << "B got " << v << "\n"; });
    s.notify_good(1);
    std::cout << "total hits = " << hits << " (expect 2)\n";
}
```

```sh
$ g++ -std=c++23 -O2 -pthread observer_reentry.cpp -o observer_reentry && ./observer_reentry
=== notify_good (snapshot) ===
A got 1
B got 1
total hits = 2 (expect 2)
```

Both observers were called exactly once. The price of snapshot is copying the callback list on every notification (copying a `std::function` is a heap allocation), so this scheme suits scenarios with "moderate notification frequency and a manageable observer count"; if notification frequency is extremely high, you can switch to copy-on-write with an immutable `shared_ptr<vector<Cb>>`, or go all the way down the lock-free RCU route — but those already step outside the Observer pattern itself.

## Pitfall Warning: Circular Dependencies and Infinite Notification Recursion

::: warning Pitfall warning
There's an even sneakier pit: **A observes B, and B observes A**. A changes and notifies B; B's callback modifies A; A notifies B again; B modifies A again... That's an infinite loop, which manifests as a stack overflow (`StackOverflow`) or a CPU permanently hogged by a single event chain.

The first-choice remedy isn't some elaborate detection mechanism but blocking the problem at the source — **compare the old and new values before `setX()`, and notify only when something actually changed**. This is the plainest and most effective move, because it strangles "meaningless self-triggering" at the root:

```cpp
class Person {
public:
    void set_age(int new_age) {
        if (age_ == new_age) return;   // value unchanged: don't notify; breaks the loop outright
        age_ = new_age;
        forecast_.notify_once();       // broadcast only when it really changed
    }
private:
    int age_ = 0;
    // ...
};
```

Beyond that there are a few auxiliary measures: merge several consecutive changes into one notification (the `begin_update()` / `end_update()` transaction pattern, broadcasting only when the transaction ends); hang a "notification suppression switch" on the callback path (an RAII guard like `ScopedNotificationDisable`) to temporarily silence notifications inside update sections known to trigger the loop; and avoid bidirectional observation at the design level in the first place — if it must be bidirectional, make it explicit who is the master data source and who is the passive end, and the passive end must never modify the master inside a callback. **When you really do hit a loop, do change detection first — that's the highest-value move.**
:::

## In Practice: A Working Weather Station Event Source

Let's knead all the governance techniques from before — `weak_ptr` dangle-proofing, RAII subscription, snapshot notification, change detection — into one genuinely usable `WeatherForecast`. It periodically fetches data from the sensor and notifies all subscribers whenever temperature or humidity changes; subscribers may be destroyed at any time without crashing the station, and subscribers may add or remove subscriptions from inside callbacks without crashing the notification:

```cpp
#pragma once
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

struct MessagePackage {
    double temperature;
    double humidity;
};

class WeatherForecast {
public:
    using Callback = std::function<void(const MessagePackage&)>;

    class Subscription {
    public:
        Subscription() = default;
        Subscription(std::size_t id, WeatherForecast* owner)
            : id_(id), owner_(owner) {}
        Subscription(const Subscription&) = delete;
        Subscription& operator=(const Subscription&) = delete;
        Subscription(Subscription&& o) noexcept;
        Subscription& operator=(Subscription&& o) noexcept;
        ~Subscription() { unsubscribe(); }
        void unsubscribe();
    private:
        std::size_t id_ = 0;
        WeatherForecast* owner_ = nullptr;
    };

    WeatherForecast() = default;

    Subscription subscribe(Callback cb);

    // Fetch once and broadcast; notify only if temperature or humidity changed (change detection)
    void poll_once();

private:
    void detach_by_id(std::size_t id);

    // Simulated sensor: in a real project this is a hardware read or a network fetch
    struct WeatherSensor {
        static MessagePackage get_message_pack();
    };

    std::mutex mtx_;
    std::unordered_map<std::size_t, Callback> callbacks_;
    std::size_t next_id_ = 1;
    MessagePackage last_{};          // previous snapshot, used for change detection
};
```

```cpp
WeatherForecast::Subscription WeatherForecast::subscribe(Callback cb) {
    std::lock_guard<std::mutex> lk(mtx_);
    std::size_t id = next_id_++;
    callbacks_.emplace(id, std::move(cb));
    return Subscription{id, this};
}

void WeatherForecast::detach_by_id(std::size_t id) {
    std::lock_guard<std::mutex> lk(mtx_);
    callbacks_.erase(id);
}

void WeatherForecast::Subscription::unsubscribe() {
    if (owner_) { owner_->detach_by_id(id_); owner_ = nullptr; id_ = 0; }
}

WeatherForecast::Subscription::Subscription(Subscription&& o) noexcept
    : id_(o.id_), owner_(o.owner_) { o.owner_ = nullptr; o.id_ = 0; }

WeatherForecast::Subscription& WeatherForecast::Subscription::operator=(Subscription&& o) noexcept {
    if (this != &o) {
        unsubscribe();
        id_ = o.id_; owner_ = o.owner_;
        o.owner_ = nullptr; o.id_ = 0;
    }
    return *this;
}

void WeatherForecast::poll_once() {
    MessagePackage pack = WeatherSensor::get_message_pack();

    // Change detection: neither temperature nor humidity changed, don't notify (breaks potential event loops)
    bool changed = pack.temperature != last_.temperature
                || pack.humidity    != last_.humidity;
    last_ = pack;
    if (!changed) return;

    // snapshot: copy the callbacks under the lock, invoke outside it
    std::vector<Callback> snapshot;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        snapshot.reserve(callbacks_.size());
        for (auto& kv : callbacks_) snapshot.push_back(kv.second);
    }
    for (auto& cb : snapshot) {
        try { cb(pack); } catch (...) { /* one observer's failure doesn't take down the rest */ }
    }
}
```

Usage looks like this — endpoints come to listen whenever they want and leave whenever they want, and the weather station doesn't care in the slightest who they are or when they die:

```cpp
int main() {
    WeatherForecast forecast;

    auto phone = std::make_shared<PhoneSender>();
    WeatherForecast::Subscription sub1 = forecast.subscribe(
        [wphone = std::weak_ptr(phone)](const MessagePackage& m) {
            if (auto p = wphone.lock()) p->receiving_message(m);
        });

    {
        auto tv = std::make_shared<TVSender>();
        WeatherForecast::Subscription sub2 = forecast.subscribe(
            [wtv = std::weak_ptr(tv)](const MessagePackage& m) {
                if (auto p = wtv.lock()) p->receiving_message(m);
            });
        forecast.poll_once();   // both phone and tv receive it
    }                           // sub2 destructs -> automatic unsubscribe

    forecast.poll_once();       // only phone receives it — no dangle, no zombies
}
```

Notice we use `weak_ptr` again inside the callback — that's a "double insurance": even if someone forgets to store the `Subscription` as a member, and even if the unsubscribe hasn't taken effect in time because of some race, a failed `lock()` inside the callback makes it skip silently instead of touching an already-destructed object. **Here `weak_ptr` plays the dual roles of "dangle-proofing" and "fault tolerance".**

::: tip Companion compilable project
The complete project for this section lives in this repository: three kinds of `Sender` (default/phone/TV), sensor fetching, registration and notification in one pipeline — clone it, run cmake once, and it works: [Observer / WeatherForecast](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/tree/main/code/volumn_codes/vol4/design-patterns/Observer/WeatherForecast).
:::

## Why the Observer Pattern Gets a Bad Rap

At this point we have a correct, lifetime-clean, and thread-safe observer implementation. But the story doesn't end here — I have to be honest with you: the Observer pattern carries its own engineering costs, and you'd best know them before using it.

**First, it's implicit invocation.** Who the observable calls, and when, is invisible in the source — you only see `notify()`, not who will respond. That means when debugging, if some observer's callback fires inexplicably, you have to dig through the subscription list at runtime to find out who registered it. This kind of "implicit jump in control flow" is the Observer pattern's biggest readability cost; at scale it makes code hard to trace.

**Second, notification order is unpredictable.** If your logic depends on "who gets notified first" (say observer A must update before B, because B depends on A's intermediate result), an observer implementation that stores callbacks in an `unordered_map` will bite you hard — hash table iteration order is unspecified. Even if you switch to a `vector`, you must document explicitly that "the order is registration order", and pray nobody quietly changes that assumption later.

**Third, exception swallowing.** You saw the `try { cb(pack); } catch (...) {}` above — to keep one observer's exception from blowing up the entire notification flow, we swallowed it. But that means a bug inside an observer may be silently eaten, leaving no trace during troubleshooting. A more responsible approach is logging, or providing a configurable exception-policy hook — but either way, **"swallowing exceptions" is itself a compromise we can't avoid making**.

**Fourth, however clean the lifetime governance, it cannot stop "the observer is alive but in a bad state".** A `weak_ptr` can only tell you whether the object exists, not what state it's in. An object may still be alive while its internal resources have gone invalid (the network dropped, the file closed) — the observer still gets notified and still uses that stale state. `weak_ptr` cures dangling, not logical correctness.

## Summary

Let's walk the whole evolution path once:

| Stage | Approach | Why it's still not enough |
|---|---|---|
| Raw pointers | Observable holds a `vector<Observer*>` | Observer dies first -> dangling pointer -> use-after-free |
| `shared_ptr` | Observable holds a `vector<shared_ptr<Observer>>` | Takes over ownership -> zombie observers that can't die when they should |
| `weak_ptr` dangle-proofing | Holds a `vector<weak_ptr<Observer>>`, `lock()` inside `notify` | **Good enough** (dead ones are skipped — no dangle, no zombies) |
| RAII subscription token | Destruction is unsubscription | Observer's destruction auto-removes its own weak reference |
| Snapshot notification | `notify` copies first, then invokes | Tames iterator invalidation caused by adds/removes inside callbacks |

Note down these key conclusions:

- **For observers in modern C++, `weak_ptr` is the first choice for lifetime governance** — the observable holds weak references and upgrades them to a temporary `shared_ptr` via `lock()` at `notify` time; dead objects are skipped, with neither dangling nor ownership hijacking.
- **Never let the observable hold a `shared_ptr<Observer>`** — that forcibly takes over the observer's ownership, breeds "zombie observers that can't die when they should", and pollutes ownership semantics.
- **Always pair subscriptions with an RAII token**, so unsubscription happens automatically when the observer destructs, rather than relying on human memory to call `unsubscribe()`.
- **`notify` must go through a snapshot**, otherwise adds/removes inside callbacks trigger iterator invalidation; on circular dependencies, do change detection at the source first.
- `weak_ptr` cures dangling pointers, not logical correctness — whether an observer's state is valid is beyond its jurisdiction.

## References

- [cppreference: `std::weak_ptr`](https://en.cppreference.com/w/cpp/memory/weak_ptr) (C++11; semantics of `lock()` / `expired()` / `use_count`)
- [cppreference: `std::shared_ptr`](https://en.cppreference.com/w/cpp/memory/shared_ptr) (reference counting and the control block)
- [cppreference: `std::enable_shared_from_this`](https://en.cppreference.com/w/cpp/memory/enable_shared_from_this) (safely obtaining a `shared_ptr` to itself from inside the object)
- *Design Patterns* (GoF), the Observer chapter; Andrei Alexandrescu, *Modern C++ Design*, Chapter 5 (generalized observers)
- Companion compilable project: [Observer / WeatherForecast](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/tree/main/code/volumn_codes/vol4/design-patterns/Observer/WeatherForecast)
