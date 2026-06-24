---
title: 'Observer Pattern: From Dangling Pointers to weak_ptr for Dangling Prevention'
description: We start with the most intuitive approach where the observable holds
  a set of raw pointers, encounter the dangling crash when the observer dies first,
  and then use `weak_ptr` to cleanly manage lifetimes. Along the way, we implement
  RAII subscription, snapshot notification, and a reentrant, thread-safe event source.
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
cpp_standard:
- 11
- 17
- 20
reading_time_minutes: 22
related:
- 单例模式:从注释约束到 Meyer's Singleton
prerequisites:
- 'Chapter 6: 类与对象'
- 智能指针与所有权
translation:
  source: documents/vol4-advanced/vol4-generics-patterns/17-observer.md
  source_hash: 314206f7881fdc071a9103fc20074a09b893fdb876b4d2bc942e5fc837c45301
  translated_at: '2026-06-24T01:02:25.158190+00:00'
  engine: anthropic
  token_count: 5605
---
# Observer Pattern: From Dangling Pointers to `weak_ptr` Safety

## What problem are we actually solving?

Let's skip the formal definition for a moment. Consider a common scenario: you are building a weather station. A background `WeatherForecast` component periodically obtains new temperature and humidity readings from sensors and needs to distribute this data to several different display endpoints—a console monitor, a mobile app push notification service, and a large TV wall display. These three interfaces are completely different and evolve on different schedules. You could hardcode them inside `WeatherForecast`, having the station directly call `phone.show(...)`, `tv.show(...)`, and so on, but this quickly becomes unmanageable: every time you add a new endpoint (like a web dashboard), you have to modify the weather station's source code; every time you remove one, you have to remove the corresponding calls. The weather station should only care about "new data arrived," but instead, it is forced to know about every display device in existence.

The Observer pattern solves exactly this requirement: **it completely decouples the "data source" from the "set of components interested in data changes." The source is only responsible for broadcasting a signal when the state changes. Anyone who wants to listen can subscribe, and they can leave whenever they want. The source doesn't know any of them personally.** Weather stations, UI event buses, stock tickers, button state change notifications—they all share the natural requirement of "one change needs to notify a bunch of unknown listeners."

However, "broadcasting a signal" is **absolutely not as simple as writing a loop and calling functions one by one** in C++. There is a pitfall unique to C++ that is far more dangerous than in other languages: **a listener might be destroyed while the weather station is still broadcasting.** Java has the Garbage Collector (GC); Python has reference counting as a safety net; but in C++, object lifetimes are managed manually. Once a listener dies first, the weather station holds a pointer to a ghost. The next broadcast becomes a use-after-free, causing the program to crash, output garbage, or crash immediately under AddressSanitizer (ASan). Therefore, the real question we need to answer in this post is—**how can an observable broadcast changes while ensuring that no dangling pointers appear, regardless of when an observer is destroyed?**

Next, we will proceed step-by-step, starting with the most intuitive approach. We will examine why each step falls short, eventually forcing us to arrive at a standard modern C++ solution.

## Step 1: The most intuitive approach—the observable holds a list of raw pointers

Many people's first attempt at the Observer pattern results in a structure like this: an abstract observer interface, and an observable that maintains a list of observers internally, traversing the list to trigger callbacks when the state changes. Let's use the weather station from the playground as a blueprint and extract its skeleton:

```cpp
struct MessagePackage {
    double temperature;
    double humidity;
};

// 观察者抽象接口:所有「想被通知的端」实现它
struct Sender {
    virtual ~Sender() = default;
    virtual void receiving_message(const MessagePackage& message) = 0;
};

// 几个具体的端
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

There are no pitfalls here. Polymorphic interfaces are standard for the Observer pattern. `virtual ~Sender() = default` ensures that the derived class destructor is called correctly when destructing via a base class pointer. Do not omit this line—as long as your observer is destroyed via a `Sender*`, missing a virtual destructor results in undefined behavior (UB). Next is the observed subject. Let's look at the most intuitive approach using raw pointers first:

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

You see, this is the minimal skeleton of the Observer pattern: `register_observer` is responsible for adding an observer's address to the list, and `notify_once` traverses the list to call each one when the state changes. Logically, this is completely correct. As long as the observers stay alive, this code runs beautifully.

But the problem lies precisely with the premise "as long as the observers stay alive." Let's write a minimal usage scenario where we register the address of a stack object to the weather station, and then let that object go out of scope before the weather station broadcasts:

```cpp
int main() {
    WeatherForecast forecast;
    {
        DefaultSender obs;          // 栈上对象
        forecast.register_observer(&obs);
        forecast.notify_once();     // 此时 obs 还活着,正常
    }                              // obs 离开作用域,栈帧回收
    forecast.notify_once();        // obs 里的指针悬空了 -> use-after-free
}
```

When the second `notify_once()` is entered, `&obs` is still sitting safely in `observers_`, but `obs` has already been reclaimed. This line accesses stack memory that has already been freed. Talk is cheap, so let's compile and run this with AddressSanitizer to see what actually happens.

## First, let's verify: will a dangling pointer really crash?

We write a minimal reproduction case, intentionally allowing the observer to go out of scope before the notification:

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
        s.notify(10);              // 此时 obs 活着,正常
    }                              // obs 离开作用域 -> 指针悬空
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

ASan immediately caught a `stack-use-after-scope`—`notify` dereferences `observers_[i]` at line 16, which has already left scope, accessing reclaimed stack memory. This is the real cost of dangling pointers: in a production environment without ASan, it might manifest as "reading garbage values," "occasional segmentation faults," or "works on my machine but breaks on CI"—a classic, notoriously difficult-to-reproduce bug. **Using raw pointers for observers is a trap you will eventually fall into if object lifetimes are not perfectly aligned.**

The problem is clear: what we lack is not the ability to broadcast, but the governance of "what the observable should do with its pointers after the observers die." Let's fix this step by step.

## Step 2: Make the observable hold a `shared_ptr`—plugging the dangling pointer leak, but creating zombies

Since the pitfall lies in "the observer dies first, leaving a dangling pointer," the most intuitive solution is to transfer ownership as well—have the observable hold a `shared_ptr<Observer>`. This way, as long as the observable is alive, the observer stays alive, and the pointer never dangles. This is exactly the approach used in the `WeatherForecast` example in the playground:

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

With this change, the dangling pointer is indeed gone—the reference counting in `shared_ptr` guarantees that the object will not be destructed as long as a copy remains in the vector. However, a new conflict has emerged, and it is more insidious than a dangling pointer: **the observed object has quietly taken ownership of the observer**, resulting in "zombie observers." Let's see how `main` is used in the playground:

```cpp
int main() {
    WeatherForecast forecast;
    forecast.register_observer(std::make_shared<DefaultSender>());
    forecast.register_observer(std::make_shared<PhoneSender>());
    forecast.register_observer(std::make_shared<TVSender>());
    forecast.notify_once();
}
```

Note that `std::make_shared<DefaultSender>()` creates a **temporary object**. After it is passed into `register_observer`, the reference count is taken over by the copy stored in the vector. At this point, the reference count is one—held only by the observer. Sounds fine? Let's verify what actually happens:

```cpp
// 假设我们在某个函数里这样用
void run(WeatherForecast& forecast) {
    auto phone = std::make_shared<PhoneSender>();
    forecast.register_observer(phone);
    std::cout << "phone 还在,引用计数 = " << phone.use_count() << "\n";
}   // phone 离开作用域,外部引用没了
// 但 forecast 里的 shared_ptr 还在 -> PhoneSender 没死,成了"僵尸"
// 之后 forecast.notify_once() 仍会调用它
```

Let's test this zombie behavior in practice:

```sh
$ g++ -std=c++23 -O2 -pthread observer_verify.cpp -o observer_verify && ./observer_verify
=== verify_zombie (strong ref keeps dead observer alive) ===
outside ref dropped next, but subject holds one
notify after outside dropped its ref:
  observer 3 got 999
(observer 3 was meant to die but subject kept it alive)
```

Look, Observer 3 has already released its reference externally. It should have died, but because the Subject holds a `shared_ptr` to it, it clings to life and continues to receive notifications. This leads to two serious consequences:

**First, the lifetime is hijacked.** When the observer dies is no longer decided by its creator, but is held hostage by the Subject. This is particularly fatal in UI scenarios (a view that should be destroyed when a window closes is kept alive by the event source, leading to memory leaks and logic errors).

**Second, ownership semantics are polluted.** The intent of `shared_ptr` is "shared ownership; destroy when the last owner lets go," but in the Observer pattern, the Subject has no desire to own the Observer. It only wants to "notify it if it is still alive." These are two completely different requirements.

Worse still, this approach pushes the lifetime management problem from one extreme to the other. Raw pointers mean "the Subject doesn't care at all; the Observer can die whenever, and I won't know." `shared_ptr` means "the Subject forcibly takes over; the Observer can't die even if it wants to." What we need is not these two extremes, but the delicate balance in between: **the Subject knows if the Observer is present, but does not prevent it from dying.**

## Step 3: `weak_ptr` Prevents Dangling References — Observing Without Ownership, Knowing When It's Gone

`weak_ptr` is tailor-made for this balance point. Its semantics are exactly what we want: **it does not increase the reference count or extend the object's lifetime, but it allows us to check "is the object still there?" at any time. If it is, we borrow a temporary `shared_ptr` to use; if not, we gracefully report that it's gone.** By switching the reference held by the Subject from `shared_ptr` to `weak_ptr`, the entire lifetime management becomes clear:

```cpp
#include <memory>
#include <vector>

class WeatherForecast {
public:
    // 接受 shared_ptr,但只存 weak_ptr —— 不延长观察者寿命
    void register_observer(const std::shared_ptr<Sender>& sender) {
        observers_.push_back(sender);   // shared_ptr -> weak_ptr 隐式构造
    }
    void notify_once() {
        MessagePackage pack = WeatherSensor::get_message_pack();
        for (auto it = observers_.begin(); it != observers_.end(); ) {
            if (auto live = it->lock()) {        // 关键:试着把 weak 升级回 shared
                live->receiving_message(pack);   // 升级成功 -> 对象活着,通知它
                ++it;
            } else {
                it = observers_.erase(it);       // 升级失败 -> 对象已死,顺手清理
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

The core logic boils down to a single line: `it->lock()`. `weak_ptr::lock()` is an atomic operation ([util.smartptr.weak.obs]) that returns a new `shared_ptr`: if the managed object is still alive, this new `shared_ptr` points to it and increments the reference count; if the object has already been destroyed, it returns an empty `shared_ptr`. We need to verify that the behavior of `lock` meets our expectations, as the safety of the entire pattern hinges on this specific semantic.

## Let's Verify: How Does `weak_ptr::lock()` Actually Behave?

We will write a minimal example that binds a `weak_ptr` to a `shared_ptr`, and then calls `lock()` in two scenarios: when the object is alive, and when it has been destroyed.

```cpp
#include <iostream>
#include <memory>

static void verify_weak_lock() {
    std::weak_ptr<int> w;
    {
        auto sp = std::make_shared<int>(42);
        w = sp;
        auto locked = w.lock();                 // 对象存活
        std::cout << "alive: use_count=" << locked.use_count()
                  << " value=" << (locked ? *locked : 0) << "\n";
        std::cout << "expired()=" << std::boolalpha << w.expired() << "\n";
    }                                           // sp 离开作用域,对象析构
    auto locked = w.lock();                     // 对象已销毁
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

Let's compile and run it:

```sh
$ g++ -std=c++23 -O2 -pthread observer_verify.cpp -o observer_verify
$ ./observer_verify
=== verify_weak_lock ===
alive: use_count=2 value=42
expired()=false
after destroy: locked.empty=true expired=true
lock failed -> skip callback (no crash)
```

Look, when the object is alive, the `shared_ptr` returned by `lock()` increments the reference count to two (the original `sp` plus this new one), and `expired()` returns false. After the object is destructed, `lock()` returns a null pointer, `expired()` returns true, and we skip the callback based on this, so nothing crashes. This is the cornerstone of the entire anti-dangling pattern—**temporarily upgrading the `weak_ptr` to a `shared_ptr` at the moment of notification ensures that as long as this temporary reference exists, the object will absolutely not be destructed during the callback execution**. This is the hard guarantee provided by `weak_ptr`.

Let's run through the complete scenario where the "observer dies first, but notification remains safe" one more time to confirm that the anti-dangling mechanism truly holds:

```sh
$ ./observer_verify
=== verify_weak_observer (weak_ptr prevents dangle) ===
notify with observer 2 alive:
  observer 2 got 100
notify after observer 2 destroyed:
(no crash: dead observers were skipped by lock)
```

`observer 1` is a temporary object and is destroyed immediately after registration; `observer 2` is destroyed at the end of the inner scope. Subsequent notifications fail all their `lock()` attempts on them and automatically skip over them, so the program continues safely. Compare this to the earlier `stack-use-after-scope` crash with ASan; similarly, "the observer dies first," yet the `weak_ptr` version doesn't even hiccup. **This is the standard answer in modern C++ for managing observer lifecycles: own nothing, but know everything.**

## Step 4: RAII Subscription — Destruction is Unsubscription

At this stage, we have solved the dangling pointer problem, but there is a lingering issue: after an observer is destroyed, its `weak_ptr` remains in `observers_`. Although every notification identifies and cleans these up, the list gradually accumulates a pile of dead weak references, wasting memory and causing every notification to perform a batch of failed `lock()` calls in vain. A more elegant approach is to have the observer actively unsubscribe **at the exact moment of its destruction**. However, this presents a chicken-and-egg problem: when the observer is being destroyed, its own `this` pointer is about to become invalid, so how can it unsubscribe?

The answer is to use an **RAII subscription token**. Instead of returning void, the subscription returns an object. This object holds the "information required to unsubscribe" (a pointer to the observable plus a subscription ID), and its destructor performs the unsubscription. The observer stores this token as a member variable. Consequently, when the observer is destroyed, its members are destroyed first—the token is destroyed—unsubscription completes—and then finally the observer itself is destroyed. The order is naturally correct:

```cpp
#include <cstddef>
#include <functional>
#include <unordered_map>
#include <memory>
#include <mutex>

class WeatherForecast {
public:
    using Callback = std::function<void(const MessagePackage&)>;

    // RAII 订阅令牌:析构时自动退订
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

    // 订阅:把回调和 weak_ptr 一起登记,返回 RAII 令牌
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

This design transforms "unsubscribing" from a task programmers must remember to perform into something the destructor guarantees automatically: as long as the token is destroyed, unsubscription is certain to occur. This aligns with the spirit of RAII discussed in the Singleton chapter: **delegate constraints to language mechanisms, not human conventions**. The observer holds a `Subscription` member; when it dies, the token dies with it, the unsubscription completes, and no corpses of weak references are left behind in the observable's list.

However, I must honestly share a trade-off here: the RAII subscription token uses an "id + callback" model rather than an "id + `weak_ptr` to the observer object" model. This is because a `std::function` callback can capture arbitrary state (including `weak_ptr<Observer>`), making it more flexible than hard-binding a specific observer interface. You can write object-oriented code like `subscribe([this](auto& p){ view_.on_change(p); })`, or completely object-less pure functional callbacks. The cost is that `std::function` incurs a heap allocation (and potential missed inlining due to type erasure). In performance-critical scenarios with extremely high notification frequencies, reverting to the raw `Observer*` interface + `weak_ptr` approach is more efficient. For most business logic, this overhead is negligible.

## Pitfall Warning: Modifying the Subscription List During Notification

::: warning Pitfall Warning
You will eventually encounter this scenario: an observer, inside its own callback, decides "I don't need to listen anymore, I want to unsubscribe," or "I want to register a new observer." This sounds reasonable, but if you directly `erase` or `push_back` to `observers_` while iterating through it in `notify`, you will trigger iterator invalidation—resulting in crashes, missed observers, or duplicate calls.

The root of the problem is: **the notification path and the modification path operate on the same container**. Iterating on one side while modifying the structure on the other; STL containers do not guarantee correct behavior under such concurrent modification. The correct approach is **snapshot notification**: upon entering `notify`, first copy all current callbacks to a local vector inside a lock, then **release the lock**, and iterate over this copy outside the lock to call them one by one. This way, callbacks are free to modify `observers_` however they like—they modify the original list, while the notification iterates over the copy, so the two do not interfere. After the notification finishes, apply any accumulated additions or removals from this round (usually placed in `pending_add_` / `pending_remove_` buffers and processed in bulk when the outermost `notify` exits).
:::

Let's verify that this snapshot approach works:

```cpp
#include <iostream>
#include <functional>
#include <vector>

class Subject {
public:
    using Cb = std::function<void(int)>;
    void subscribe(Cb cb) { observers_.push_back(std::move(cb)); }

    // snapshot 版:先拷一份再调用,回调里随便改原表
    void notify_good(int v) {
        std::vector<Cb> snap(observers_);     // 拷一份
        for (auto& cb : snap) cb(v);          // 遍历拷贝
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

Both observers are invoked exactly once. The cost of `snapshot` is copying the callback list on every notification (copying a `std::function` involves a heap allocation), so this approach is suitable for scenarios with "moderate notification frequency and a manageable number of observers." If the notification frequency is extremely high, we can switch to an immutable `shared_ptr<vector<Cb>>` for copy-on-write, or simply go for a lock-free RCU approach—but those go beyond the scope of the Observer pattern itself.

## Pitfall Warning: Circular Dependencies and Infinite Recursion

::: warning Pitfall Warning
There is an even more insidious trap: **A observes B, and B observes A**. When A changes, it notifies B; B's callback modifies A, which notifies B again; B modifies A again... This creates a dead loop. In the program, this manifests as a stack overflow (`StackOverflow`) or the CPU being permanently occupied by an infinite event chain.

The preferred way to handle this is not to implement complex detection mechanisms, but to block it at the source—**compare the old and new values before `setX()`, and only notify if the value has actually changed**. This is the simplest yet most effective trick, because it cuts off "meaningless self-triggering" at the root:

```cpp
class Person {
public:
    void set_age(int new_age) {
        if (age_ == new_age) return;   // 值没变,不通知,直接打断环路
        age_ = new_age;
        forecast_.notify_once();       // 真变了才广播
    }
private:
    int age_ = 0;
    // ...
};
```

In addition, there are several auxiliary strategies: batching multiple consecutive changes into a single notification (a `begin_update()` / `end_update()` transaction model that broadcasts only upon completion); placing a "notification suppression switch" on the callback path (using an RAII guard like `ScopedNotificationDisable`) to temporarily disable notifications during update segments known to trigger loops; and avoiding bidirectional observation at the design level. If bidirectional observation is absolutely necessary, clearly define which side is the primary data source and which is the passive side—the passive side must never modify the primary side within a callback. **If a loop does occur, implement change detection first; this is the most cost-effective step.**
:::

## Practice: A Working Weather Station Event Source

Let's combine all the governance techniques we discussed—`weak_ptr` for dangling prevention, RAII subscriptions, snapshot notifications, and change detection—to build a fully functional `WeatherForecast`. It periodically fetches data from sensors and notifies all subscribers whenever the temperature or humidity changes. Subscribers can be destroyed at any time without crashing the weather station, and subscribers can safely add or remove subscriptions within callbacks without causing notification crashes:

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

    // 取一次数据并广播;温度或湿度之一变化才通知(变更检测)
    void poll_once();

private:
    void detach_by_id(std::size_t id);

    // 模拟传感器:实际项目里这里是硬件读取或网络拉取
    struct WeatherSensor {
        static MessagePackage get_message_pack();
    };

    std::mutex mtx_;
    std::unordered_map<std::size_t, Callback> callbacks_;
    std::size_t next_id_ = 1;
    MessagePackage last_{};          // 上一次的快照,用于变更检测
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

    // 变更检测:温度湿度都没变,不通知(打断潜在的事件环路)
    bool changed = pack.temperature != last_.temperature
                || pack.humidity    != last_.humidity;
    last_ = pack;
    if (!changed) return;

    // snapshot:锁内拷一份回调,锁外调用
    std::vector<Callback> snapshot;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        snapshot.reserve(callbacks_.size());
        for (auto& kv : callbacks_) snapshot.push_back(kv.second);
    }
    for (auto& cb : snapshot) {
        try { cb(pack); } catch (...) { /* 单个观察者崩不波及整体 */ }
    }
}
```

This is how it works: endpoints can join or leave whenever they want, and the weather station doesn't care who they are or when they die:

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
        forecast.poll_once();   // phone 和 tv 都收到
    }                           // sub2 析构 -> 自动退订

    forecast.poll_once();       // 只有 phone 收到,没有悬空、没有僵尸
}
```

Note that we use `weak_ptr` in the callback again—this acts as a "double insurance": even if someone forgets to store the `Subscription` as a member, or if the unsubscribe fails to take effect due to some race condition, a failed `lock()` in the callback will cause it to be silently skipped rather than accessing a destructed object. **Here, `weak_ptr` assumes the dual role of "preventing dangling pointers" and "fault tolerance."**

::: tip Companion Compilable Project
The complete project for this section is available in this repository. It includes three types of `Sender` (default, mobile, TV), sensor data retrieval, and a full pipeline of registration and notification. Just clone it and run CMake: [Observer / WeatherForecast](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/tree/main/code/volumn_codes/vol4/design-patterns/Observer/WeatherForecast).
:::

## Why the Observer Pattern is Unpopular

At this point, we have a correct Observer implementation with clean lifetime management and thread safety. But the story doesn't end here—I must honestly tell you that the Observer pattern comes with its own engineering costs, so you should be aware of them before using it.

**First, it is "implicit invocation."** Who the subject calls and when it calls them is not visible in the source code—you only see `notify()`, but you don't see who will respond. This means that during debugging, if an observer's callback is triggered inexplicably, you have to inspect the subscription list at runtime to know who registered it. This "implicit jump of control flow" is the biggest readability cost of the Observer pattern; large-scale use can make the code difficult to trace.

**Second, notification order is unpredictable.** If your logic depends on "who receives the notification first" (for example, Observer A must update before B because B depends on A's intermediate result), an Observer implementation using `unordered_map` to store callbacks will bite you hard—the traversal order of a hash table is non-deterministic. Even if you switch to `vector`, you must explicitly document in your docs that "order is registration order," and pray that no one changes this assumption later.

**Third, exception swallowing.** You saw the `try { cb(pack); } catch (...) {}` above—to prevent one observer's exception from crashing the entire notification process, we swallowed it. However, this means that bugs in observers might be silently swallowed, and you won't even know an error occurred when troubleshooting. A more responsible approach is to log the error, or provide a configurable exception strategy hook, but in any case, **"swallowing exceptions" is a compromise we have to make**.

**Fourth, no matter how clean the lifetime management is, it can't stop "the observer is alive but its state is invalid."** `weak_ptr` only tells you if the object exists, not what state it is in. An object might still be alive, but its internal resources are invalid (e.g., network disconnected, file closed), yet the observer will still be notified and will still attempt to use that invalid state. `weak_ptr` fixes dangling pointers, not logical correctness.

## Summary

Let's review the entire evolution path:

| Stage | Approach | Why it's still not enough |
|---|---|---|
| Raw Pointer | Subject holds `vector<Observer*>` | Observer dies first -> dangling pointer -> use-after-free |
| `shared_ptr` | Subject holds `vector<shared_ptr<Observer>>` | Takes ownership -> zombie observers, things that should die won't |
| `weak_ptr` Dangling Prevention | Holds `vector<weak_ptr<Observer>>`, `lock()` in `notify` | **Sufficient** (skips if dead, no dangling, no ownership hijacking) |
| RAII Subscription Token | Destructor unsubscribes | Observer destructor automatically clears its own weak reference |
| Snapshot Notification | `notify` copies first, then calls | Fixes iterator invalidation caused by add/remove inside callbacks |

Keep these key conclusions in mind:

- **For modern C++ Observers, the preferred choice for lifetime management is `weak_ptr`**—the subject holds a weak reference, `lock()` upgrades it to a temporary `shared_ptr` during `notify`, and if the object is dead, it skips. This avoids dangling pointers without hijacking ownership.
- **Never let the subject hold a `shared_ptr<Observer>`**, as that forcibly takes ownership of the observer, creating "zombie observers" that refuse to die and polluting ownership semantics.
- **Subscriptions must be paired with an RAII token**, allowing unsubscribe to happen automatically when the observer destructs, rather than relying on a human remembering to call `unsubscribe()`.
- **`notify` must use a snapshot**, otherwise add/remove operations in callbacks will trigger iterator invalidation; if you encounter circular dependencies, perform change detection at the source first.
- `weak_ptr` fixes dangling pointers, not logical correctness—it doesn't care if the observer's state is correct.

## References

- [cppreference: `std::weak_ptr`](https://en.cppreference.com/w/cpp/memory/weak_ptr) (C++11, `lock()` / `expired()` / `use_count` semantics)
- [cppreference: `std::shared_ptr`](https://en.cppreference.com/w/cpp/memory/shared_ptr) (Reference counting and control blocks)
- [cppreference: `std::enable_shared_from_this`](https://en.cppreference.com/w/cpp/memory/enable_shared_from_this) (Safely obtaining an object's own `shared_ptr`)
- *Design Patterns* (GoF), Observer section; Andrei Alexandrescu, *Modern C++ Design*, Chapter 5 (Generic Observer)
- Companion compilable project: [Observer / WeatherForecast](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/tree/main/code/volumn_codes/vol4/design-patterns/Observer/WeatherForecast)
