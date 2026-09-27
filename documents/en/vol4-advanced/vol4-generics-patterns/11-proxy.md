---
title: 'Proxy Pattern: You''re Already Using It Without Realizing It'
description: 'Starting from smart pointers—the proxies you use every day without realizing it—we derive six kinds of proxies step by step (virtual, protection, remote, caching, synchronization, COW), and along the way debunk the dated `shared_ptr::unique()` practice in COW code'
chapter: 11
order: 11
tags:
  - host
  - cpp-modern
  - intermediate
  - 代理模式
difficulty: intermediate
platform: host
cpp_standard: [11, 17, 20]
reading_time_minutes: 22
related:
  - 'Singleton Pattern: From Comment-Only Constraints to Meyer''s Singleton'
prerequisites:
  - Classes and Object-Oriented Programming
translation:
  source: documents/vol4-advanced/vol4-generics-patterns/11-proxy.md
  source_hash: c442a2df8ae0ca2b6d2bc0121f2c8e46a14cb8d564d386847f416fc290fcfdcc
  translated_at: '2026-09-26T05:29:21+00:00'
  engine: anthropic
  token_count: 14000
---

# Proxy Pattern: You're Already Using It Without Realizing It

## What Problem Are We Actually Solving

Let's set the name "Proxy Pattern" aside for a moment and look at a scenario you write almost every single day. Suppose we have a working object `Source` that does some real work:

```cpp
class Source {
public:
    void do_work() { /* the real work */ }
};

int main() {
    Source* src = new Source;
    src->do_work();      // works just fine
    delete src;          // but you have to remember to release it manually
}
```

This code runs, of course, but you already know where it's fragile—you're holding a bare pointer, and who calls `delete`, whether `delete` gets called twice, whether the pointer gets used again after `delete`, all of that rides on your brain keeping watch. The standard answer C++ has prepared for us is to wrap this layer of manual management up:

```cpp
int main() {
    auto src = std::make_unique<Source>();
    src->do_work();      // use it exactly as before—the syntax hasn't changed at all
    // released automatically on scope exit, no delete needed
}
```

You'll notice something interesting: once `std::unique_ptr<Source>` is wrapped around it, the call `src->do_work()` looks **exactly** like it did before. The proxied object keeps being used the way it always was, the outward interface is fully transparent, yet you picked up RAII for free—destruction and release of the heap memory are no longer your problem.

Honestly, that is the Proxy pattern. **The core of the Proxy pattern: a proxy object stands in where the real object would be, exposes the same (or a compatible) interface to the outside, the caller uses it just as it would use the real object, and the proxy quietly does some other work in between.** In the `unique_ptr` example, that secret work is "managing the lifetime."

Why do we need such a layer? Because in the real world, handing an object directly to callers usually comes with a pile of chores that are **unrelated to the business but must be done**: loading an image takes two seconds, calling a remote service means crossing the network, modifying a field needs to be logged, reading a sensitive resource requires a permission check, computing a result should avoid redundant computation... If all these chores get stuffed into the business object, the business class turns dirty and brittle; if they scatter across every call site, you get duplicate code all over the floor. The Proxy pattern pulls this layer of "access control + cross-cutting behavior" out and hands it to a middleman—the business class only does business, and callers only use the interface.

Next we'll look, one by one, at just how many kinds of work this "middleman" can do for us, and where the pitfall hides behind each kind.

## Step 1: Property Proxy—Turning Field Reads and Writes into Interceptable Operations

Let's start with the example closest to everyday code. You have a class with a field in it, say a port configuration. The normal way to write it is just `int port;`—whoever wants to read it reads, whoever wants to write it writes, the field itself has no opinions. But one day product says: when `port` is assigned, the range must be validated; when it's read, a notification must go out. You could of course wrap `port` in a pair of `get_port()` / `set_port()`—but that breaks the natural "read and write the field directly" style, and every user has to switch to function calls.

The Proxy pattern's answer: write a `property<T>` template that outwardly looks like a value (supports implicit conversion to `T`, supports `operator=`), but secretly hangs a custom getter/setter onto those two actions, reading and writing:

```cpp
template <typename T>
class Property {
public:
    using Getter = std::function<T()>;
    using Setter = std::function<void(const T&)>;

    // default: the "store the value directly" implementation
    Property()
        : getter_([this] { return value_; }),
          setter_([this](const T& v) { value_ = v; }) {}

    explicit Property(const T& v) : Property() { value_ = v; }

    // inject custom getter/setter — validation, notifications, logging all hook in here
    Property(Getter g, Setter s) : getter_(std::move(g)), setter_(std::move(s)) {}

    operator T() const { return getter_(); }  // read: goes through the getter
    Property& operator=(const T& v) {         // write: goes through the setter
        setter_(v);
        return *this;
    }

private:
    mutable T value_{};   // mutable: lets the getter modify it even inside const methods
    Getter getter_;
    Setter setter_;
};
```

The key to this code is that it turns "read" and "write"—two actions that normally cannot be intercepted—into two `std::function`s. The two lambdas in the default constructor simply route reads and writes straight onto the internal `value_`, behaving exactly like an ordinary field; but the moment you pass in custom `Getter`/`Setter`s, you can hook up whatever you want: validation, notifications, logging, rate limiting.

Note that `mutable`—the getter is a `const` method, which by the rules cannot modify members, but once `value_` is declared `mutable` it can. That's because in the "store the value directly" mode the getter genuinely needs to read and write `value_`, while semantically reading a property should be `const`; `mutable` reconciles that contradiction for us here.

What does it look like in use? You'll find that a field wrapped in a `Property` behaves outwardly almost no differently from a real field—reading it implicitly converts to `T` (through the getter), writing it triggers `operator=` (through the setter):

```cpp
Property<int> port(8080);
int v = port;     // implicit conversion to T, triggers the getter
port = 9090;      // operator=, triggers the setter
```

Let's verify that this chain of implicit conversion and assignment really behaves the way we described:

```cpp
#include <iostream>
#include <functional>

template <typename T>
class Property {
public:
    Property() : getter_([this] { return value_; }),
                 setter_([this](const T& v) { value_ = v; }) {}
    explicit Property(const T& v) : Property() { value_ = v; }
    operator T() const { return getter_(); }
    Property& operator=(const T& v) { setter_(v); return *this; }
    const T& raw() const { return value_; }
private:
    mutable T value_{};
    std::function<T()> getter_;
    std::function<void(const T&)> setter_;
};

int main() {
    Property<int> port(8080);
    int v = port;          // implicit conversion to T -> getter
    std::cout << "read = " << v << "\n";
    port = 9090;           // operator= -> setter
    std::cout << "after assign = " << port.raw() << "\n";
}
```

Compile and run:

```sh
$ g++ -std=c++20 -O2 -Wall property_verify.cpp -o property_verify
$ ./property_verify
read = 8080
after assign = 9090
```

Both reading and writing dutifully went through the lambdas we hung on them. That is the essence of the property proxy—**without changing the syntax of field usage, turning the two actions of read and write into interceptable hooks**.

But there's a pitfall to name in advance: `operator T()` is an implicit conversion, meaning whenever the context needs a `T`, the compiler will silently call the getter. Most of the time that's the behavior you want, but in certain overload-resolution scenarios it can easily fire conversions you didn't anticipate. So in practice, the property proxy is better reserved for fields where you genuinely need to globally intercept reads and writes—don't go replacing every `int` in sight with `Property<int>`, or implicit conversions will dig you a pile of baffling pits.

## Step 2: Virtual Proxy—Deferring Expensive Construction to the Last Possible Moment

This next one is the most classic use in the whole Proxy family. Some objects are expensive to create: loading a high-resolution image, establishing a database connection, parsing a large configuration... These objects share a common trait—**you won't necessarily use every one of them**. If the program eagerly constructs all such objects the moment it starts, startup is slow and memory fills up, yet some of those objects may never be touched for the entire run of the program.

The virtual proxy (Virtual Proxy / Lazy Proxy) approach: first build a **cheap proxy** that merely remembers "what the real object looks like" (a filename, a connection string) without actually loading anything; only when it's truly needed for the first time (say `display()` gets called) does the proxy go ahead and construct the real object, and every subsequent access is forwarded to it. That is lazy loading.

We'll reuse a classic example—displaying images. Real images load slowly from disk, and we don't want to load at proxy-construction time:

```cpp
#include <iostream>
#include <memory>
#include <string>

struct Image {
    virtual void display() = 0;
    virtual ~Image() = default;
};

// the real image: loads from disk right in the constructor, slow
class RealImage : public Image {
public:
    explicit RealImage(const std::string& f) : filename_(f) { load_from_disk(); }
    void display() override { std::cout << "Displaying " << filename_ << "\n"; }

private:
    void load_from_disk() {
        std::cout << "Loading " << filename_ << " from disk (expensive)\n";
    }
    std::string filename_;
};
```

`RealImage`'s constructor already calls `load_from_disk()`—that's the root of its "expensiveness". The proxy's job is to defer this expensive construction to the first `display()`:

```cpp
class ImageProxy : public Image {
public:
    explicit ImageProxy(const std::string& f) : filename_(f) {}
    void display() override {
        ensure_real();     // construct RealImage on the first call only
        real_->display();  // forward to the real object
    }

private:
    void ensure_real() {
        if (!real_) real_ = std::make_unique<RealImage>(filename_);
    }
    std::string filename_;
    std::unique_ptr<RealImage> real_;
};
```

Look: the proxy's interface is **identical** to the real object's (both inherit `Image`, both have `display()`), and the caller can't tell at all that it's holding a proxy. But the real object isn't constructed until `display()` is called for the first time; before that, all you hold is a featherweight `ImageProxy` that hasn't spent a cent of loading cost.

That `if (!real_)` above is a formulation that is perfectly correct **in a single thread**. But the story doesn't end here—the real pitfall is still ahead.

## Let's Verify First: Lazy Loading Holds Up in a Single Thread

First let's confirm, in a single thread, that the proxy really achieves "load on first access, and don't load again on subsequent accesses". Hang an atomic counter on `RealImage` and see how many times it gets constructed across multiple `display()` calls:

```cpp
#include <atomic>
#include <iostream>
#include <memory>
#include <string>

struct Image {
    virtual void display() = 0;
    virtual ~Image() = default;
};

class RealImage : public Image {
public:
    explicit RealImage(const std::string& f) : filename_(f) {
        load_from_disk();
        ++load_count;
    }
    void display() override { std::cout << "Displaying " << filename_ << "\n"; }
    static std::atomic<int> load_count;

private:
    void load_from_disk() {
        std::cout << "Loading " << filename_ << " from disk (expensive)\n";
    }
    std::string filename_;
};
std::atomic<int> RealImage::load_count{0};

class ImageProxy : public Image {
public:
    explicit ImageProxy(const std::string& f) : filename_(f) {}
    void display() override {
        if (!real_) real_ = std::make_unique<RealImage>(filename_);
        real_->display();
    }
private:
    std::string filename_;
    std::unique_ptr<RealImage> real_;
};

int main() {
    ImageProxy proxy("cat.png");
    std::cout << "before display, load_count = " << RealImage::load_count << "\n";
    proxy.display();
    proxy.display();
    std::cout << "after 2 displays, load_count = " << RealImage::load_count
              << " (expect 1)\n";
}
```

Compile and run:

```sh
$ g++ -std=c++23 -O2 -Wall proxy_lazy_verify.cpp -o proxy_lazy_verify
$ ./proxy_lazy_verify
before display, load_count = 0
Loading cat.png from disk (expensive)   # note: loading happens on the first display only
Displaying cat.png
Displaying cat.png
after 2 displays, load_count = 1 (expect 1)
```

`load_count` sits firmly at 1: the proxy deferred the expensive loading to the first access, and subsequent accesses reused the same real object. That is the whole value of the virtual proxy.

## Pitfall Warning: Concurrent Lazy Loading with `if (!real_)` Is a Data Race

::: warning Pitfall ahead
That `if (!real_) real_ = std::make_unique<...>` pattern **is for single-threaded use only**. The moment multiple threads first access this proxy at the same time, the code becomes a bare-faced **data race**—several threads may simultaneously read `real_` as empty, simultaneously construct the real object, and whoever writes back later overwrites the others; the real object constructed multiple times, the pointer overwritten, the previously constructed objects leaking—all of it can happen.

Let's not just assert that with words—run it under ThreadSanitizer and let the tool speak for us:

```cpp
#include <memory>
#include <thread>
#include <vector>

struct Image { virtual void display() = 0; virtual ~Image() = default; };
class RealImage : public Image {
public:
    explicit RealImage(int) {}
    void display() override {}
};

class NaiveProxy : public Image {
public:
    explicit NaiveProxy(int id) : id_(id) {}
    void display() override {
        if (!real_) real_ = std::make_unique<RealImage>(id_);  // data race!
        real_->display();
    }
private:
    int id_;
    std::unique_ptr<RealImage> real_;
};

int main() {
    NaiveProxy p(7);
    std::vector<std::thread> ts;
    for (int i = 0; i < 20; ++i) ts.emplace_back([&p] { p.display(); });
    for (auto& t : ts) t.join();
}
```

```sh
$ g++ -std=c++23 -O1 -pthread -fsanitize=thread proxy_lazy_tsan.cpp -o proxy_lazy_tsan
$ ./proxy_lazy_tsan 2>&1 | head -5
==================
WARNING: ThreadSanitizer: data race (pid 69560)
    #0 NaiveProxy::display() ...   # reads real_
    ...
    Previous write of size 8 by thread T1:
    #0 NaiveProxy::display() ...   # writes real_
```

TSan caught it at a glance: one thread is writing `real_` while another reads it, the two have no synchronization relationship whatsoever, and per the standard that is undefined behavior. Never carry the single-threaded `if (!real_)` straight into a multithreaded environment—skip this fix and it will blow up, guaranteed.
:::

There are two correct approaches. If you want "construct exactly once" semantics, C++ hands us a clean answer—`std::call_once`, the same family of machinery as the magic statics behind Meyer's Singleton: the language guarantees on your behalf that only one thread performs the initialization:

```cpp
#include <mutex>

class ImageProxy : public Image {
public:
    explicit ImageProxy(const std::string& f) : filename_(f) {}
    void display() override {
        std::call_once(flag_, [this] {
            real_ = std::make_unique<RealImage>(filename_);
        });
        real_->display();
    }

private:
    std::string filename_;
    std::unique_ptr<RealImage> real_;
    std::once_flag flag_;
};
```

Internally, `call_once` uses atomic synchronization lighter than a simple lock, and its semantics are precisely "exactly one thread performs the initialization, the remaining threads block and wait". The other approach is hand-writing double-checked locking (DCLP) plus acquire/release on `std::atomic`, which we dismantled in detail in the singleton chapter, so we won't repeat it here. The conclusion is the same: **for lazy loading under concurrency you must bring in a real synchronization mechanism—a naked `if` won't do.**

## Step 3: Protection Proxy—Peeling "Who Can Do What" Out of the Business Logic

This next one hands the cross-cutting concern of permission checking to a proxy. Imagine a sensitive object with a `secret()` method that only specific identities may call. Your first instinct might be to write `if (!has_permission) throw ...` inside `secret()`, but that welds permission logic to business logic—touch one spot and the whole thing ripples.

The protection proxy (Protection Proxy) works like this: the proxy and the real object implement the same interface, and the proxy performs a permission check before forwarding—forward on pass, refuse on failure. That way the real object `Sensitive` contains nothing but pure business, and permissions are entirely the proxy's business:

```cpp
#include <stdexcept>
#include <iostream>

class Sensitive {
public:
    void secret() { std::cout << "secret data\n"; }
};

class ProtectionProxy {
public:
    ProtectionProxy(Sensitive* s, bool allowed) : s_(s), allowed_(allowed) {}
    void secret() {
        if (!allowed_) throw std::runtime_error("access denied");
        s_->secret();
    }

private:
    Sensitive* s_;
    bool allowed_;
};
```

Look at what the proxy does—two steps: first check `allowed_` and throw on failure; on pass, forward the request untouched to the real object. This check used to be part of the business; now it has been extracted to the access entry point, and the real object knows nothing about permissions at all.

The benefit isn't just "cleanliness". A permission check is itself an important audit event—who tried to access what, when, whether it succeeded or was refused, and why: these are the core contents of an audit log. A protection proxy always fires at the access entry point; it can record successes as well as refusals with their reasons (missing permission, expired credential, untrusted origin), making it a natural audit point. Put authorization and auditing into the proxy, and the business class sticks to business; when the permission policy changes, you edit the proxy in one place and you're done.

One design trade-off to flag here: `ProtectionProxy` in this example does not inherit an abstract base class of `Sensitive`; it copies the `secret()` signature instead. That's because `Sensitive` itself has no abstract interface to inherit (it's a concrete class). In real projects, we'd usually first extract a pure-virtual interface like `ISensitive` and have both `Sensitive` and `ProtectionProxy` implement it, so callers hold an `ISensitive&` and the swap is seamless—that is the benefit of "interface conformability", and it is the precondition for the Proxy pattern's transparent substitution.

## Step 4: Remote Proxy—Hiding the Network Details Behind a Local Object

The remote proxy (Remote Proxy / Communication Proxy) targets another scenario: the real object lives on another machine (or in another process), and calling it means crossing the network. But you don't want caller code littered with "serialize, send request, wait for response, parse, retry, timeout"—those communication details have nothing to do with the business, yet they are extremely verbose.

The proxy's solution: build a local object that implements the same interface as the remote service, but internally **translates every method call into a network request**. To the caller, it looks like calling a local object; behind the scenes, the proxy packs the parameters for it, sends them out, receives the reply, and unpacks it:

```cpp
#include <chrono>
#include <stdexcept>
#include <string>
#include <thread>

// a simulated transport layer (in real life: socket / HTTP / gRPC)
class Transport {
public:
    std::string send_request(const std::string& req) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));  // simulate network latency
        if (req == "get_time") return "2025-09-29T12:00:00Z";
        if (req.rfind("compute:", 0) == 0) return "result:" + req.substr(8);
        throw std::runtime_error("unknown request");
    }
};

// local abstraction of the remote service
struct RemoteService {
    virtual std::string get_time() = 0;
    virtual int remote_compute(int x, int y) = 0;
    virtual ~RemoteService() = default;
};

// the remote proxy: translates method calls into transport requests
class RemoteServiceProxy : public RemoteService {
public:
    explicit RemoteServiceProxy(Transport* t) : transport_(t) {}

    std::string get_time() override {
        return transport_->send_request("get_time");
    }

    int remote_compute(int x, int y) override {
        std::string req = "compute:" + std::to_string(x) + "," + std::to_string(y);
        transport_->send_request(req);              // a real implementation would parse "result:..."
        return x + y;
    }

private:
    Transport* transport_;
};
```

All the proxy does is translate `get_time()` into the string `"get_time"` and send it, and `remote_compute(x, y)` into `"compute:x,y"` and send that. The caller still writes `service.remote_compute(3, 4)`, with no idea that this call crossed a network.

The `req.rfind("compute:", 0) == 0` idiom deserves a word—it's the classic C++ way to test "does this string start with this prefix" (`rfind` searches from position 0, returns 0 on a hit, `npos` otherwise). Since C++20 you can write `req.starts_with("compute:")` directly, which is more straightforward; `rfind` is used here so the code also compiles under C++17.

The genuinely hard part of a remote proxy isn't this translation, it's **failure handling**: a network call can time out, can partially fail, can die halfway through a retry, can return successfully but with wrong content. These failure modes are far more complicated than a local call, so remote proxies in real projects usually also build in retry strategies, timeout control, circuit breaking, and degradation. That's also why RPC frameworks like gRPC and Thrift generate proxy classes for you—they stuff all that complicated failure handling into the generated proxy, and you just call the interface.

## Step 5: Caching Proxy—Storing the Results of Repeated Computations

The caching proxy (Caching / Memoization Proxy) solves the problem of "computing the same thing over and over is wasteful". Some computations are expensive by nature (parsing a big expression, querying a database, running a complex model), yet the same inputs tend to recur. If the proxy layer caches results, the second identical request hits the cache directly and never bothers the real object again:

```cpp
#include <optional>
#include <unordered_map>

class Expensive {
public:
    virtual int compute(int x) = 0;
    virtual ~Expensive() = default;
};

class CachingProxy : public Expensive {
public:
    explicit CachingProxy(Expensive* r) : real_(r) {}

    int compute(int x) override {
        if (auto it = cache_.find(x); it != cache_.end()) {
            return it->second;          // cache hit, return directly
        }
        int result = real_->compute(x); // miss: only now do we compute
        cache_[x] = result;
        return result;
    }

private:
    Expensive* real_;
    std::unordered_map<int, int> cache_;
};
```

Inside `compute`, the proxy checks the cache first—return on hit, compute on miss. The real object `Expensive` knows nothing about any cache; it just computes.

The caching proxy looks simple, but the real difficulty is all in the **cache policy**. First, thread safety: the `cache_` above is a plain `unordered_map`, concurrent access from multiple threads is a data race, and real projects either add a lock or switch to a concurrent hash map. Second, the consistency model: can your cache tolerate brief divergence from the backend? If yes, cache-aside plus a TTL is enough; if not (account balances, say), caching isn't the right first choice at all. Then eviction: a cache can't grow forever, it needs LRU or a capacity cap. Finally, you must guard against cache breakdown (a stampede of concurrent misses on the same key, all hammering the backend) and avalanche (masses of keys expiring at once). Each of these unfolds into a topic of its own, but they share one trait—**they can all be encapsulated inside the proxy, without touching a single line of the business class**. That is the value of the Proxy pattern's "centralize cross-cutting concerns".

## Step 6: Synchronization Proxy—Wrapping a Lock Around a Non-Thread-Safe Object

The synchronization proxy (Synchronization Proxy) targets this situation: you're handed an object that isn't thread-safe itself (it may come from a third-party library, may be legacy code, or may deliberately skip locking for performance), but now you need to use it from multiple threads—and you don't want to (or can't) modify its source to add locks.

The proxy's approach: wrap it in a layer that automatically acquires and releases a lock around every method call:

```cpp
#include <mutex>

class SomeInterface {
public:
    virtual void op() = 0;
    virtual ~SomeInterface() = default;
};

class SyncProxy : public SomeInterface {
public:
    explicit SyncProxy(SomeInterface* r) : real_(r) {}
    void op() override {
        std::lock_guard<std::mutex> lk(mtx_);   // lock on entering the method
        real_->op();                            // the real work
    }                                           // unlock automatically on exit

private:
    SomeInterface* real_;
    std::mutex mtx_;
};
```

The appeal of this style: you don't touch the real object's code—you just wrap a layer around it, and it's instantly thread-safe. For third-party libraries and untouchable legacy code, it's a lifeline.

But here I have to pour cold water on it. **The synchronization proxy is not a thread-safety silver bullet—a coarse-grained lock will drag you back down to a single core.** Look at that `mtx_` above: it's shared by the entire proxy, meaning only one thread can call `op()` at any moment—if `op()` takes long, every other thread has to queue up and the multi-core advantage instantly drops to zero. Worse is deadlock: if a call chain can enter proxy A, hold A's lock, then call into proxy B and grab B's lock, while another thread does the reverse—holding B first and then coming for A—you get the classic circular wait.

So the practical points for a synchronization proxy: make the lock granularity as fine as possible (shard by resource; use the reader-writer lock `std::shared_mutex` so read-heavy, write-light workloads can read concurrently); hold the lock for as short a time as possible (finish everything you can before entering the critical section); and agree on a consistent locking order across multiple proxies. Where the scenario allows, replacing pessimistic locking with optimistic concurrency (version numbers + CAS) often scales better. The synchronization proxy gives you a starting point, not a destination.

## Step 7: Copy-On-Write (COW)—The `shared_ptr::unique()` Relic

This last one is the most technically dense and the easiest to get wrong in the whole Proxy family. The COW (Copy-On-Write) proxy addresses this: an object is shared in many places, reads vastly outnumber writes, and you want everyone sharing the same underlying data to save memory; but the moment one of them wants to modify, it must first copy the data and change its own copy—not pollute the other sharers.

Historically, `std::string` itself used COW (it was removed later in C++11, because under multithreading the synchronization cost of COW's reference counting actually dragged performance down). Let's demonstrate with a `CowString`:

```cpp
#include <memory>
#include <string>

class CowString {
public:
    CowString() : data_(std::make_shared<std::string>()) {}
    CowString(const std::string& s) : data_(std::make_shared<std::string>(s)) {}

    // read: return a const reference directly; many CowStrings share the same copy
    const std::string& str() const { return *data_; }

    // write: first ensure exclusive ownership (copying if necessary), then modify
    void append(const std::string& s) {
        ensure_unique();
        data_->append(s);
    }

private:
    void ensure_unique() {
        if (data_.use_count() > 1) {                         // not exclusive
            data_ = std::make_shared<std::string>(*data_);   // make a copy
        }
    }
    std::shared_ptr<std::string> data_;
};
```

The heart of COW is the `ensure_unique()` move—before writing, ask "am I the sole owner?", and if not, copy first. Multiple `CowString`s copy-constructed from one another share the same `shared_ptr`; when reading, everyone reads the same memory, and only when modifying does each go off and edit its own copy. In read-mostly scenarios, this saves a great deal of copying overhead.

But—this code hides a **relic of the past**, and it's precisely the spot where that formulation from the original notes would teach people the wrong thing.

::: warning Pitfall ahead
You may have seen `ensure_unique()` written like this in some older material:

```cpp
void ensure_unique() {
    if (!data_.unique()) {        // ⚠️ shared_ptr::unique() — removed from the standard as of C++20
        data_ = std::make_shared<std::string>(*data_);
    }
}
```

The member function `std::shared_ptr::unique()` was marked **deprecated** starting in C++17 and **formally removed from the standard** as of C++20. In other words, per the ISO standard, from C++20 onward `shared_ptr` no longer has a `unique()` member. The reason it still compiles in libstdc++ / libc++ today is purely that the mainstream standard library implementations kept it as an extension for compatibility—but that doesn't make it right; switch to a stricter implementation or some future version, and your code will fail to compile. **Modern C++ should not write `shared_ptr::unique()` anymore.**
:::

The more crucial pitfall isn't actually "the function was removed", it's that **the criterion itself—`unique()` (and likewise `use_count() == 1`)—is simply unreliable under concurrency**. The COW `ensure_unique` logic is "glance at the reference count; if it's 1, modify in place, otherwise copy". But the reference count is a quantity that changes—you just read a 1, you're about to modify in place, and right at that moment another thread happens to copy the `shared_ptr`, the count jumps to 2, and your side is still naively modifying the shared data in place, polluting the other thread's copy right along with it.

This is a classic TOCTOU (time-of-check-to-time-of-use) race, and `shared_ptr`'s own atomic reference count cannot plug it, because the synchronization of the reference count only guarantees the count is correct—it does not guarantee "nobody will come copy right away while the count is 1". Let's verify—one thread repeatedly copies and releases a `shared_ptr` (manufacturing reference-count jitter), while another thread repeatedly observes `use_count()`:

```cpp
#include <atomic>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

int main() {
    auto shared = std::make_shared<std::string>("base");
    std::thread t1([&] {
        for (int i = 0; i < 100000; ++i) {
            auto copy = shared;   // use_count: 1 -> 2
            (void)copy;           // destruction: 2 -> 1
        }
    });
    std::atomic<long> saw_two{0};
    std::thread t2([&] {
        for (int i = 0; i < 100000; ++i) {
            if (shared.use_count() > 1) ++saw_two;   // the reference count will be observed jumping
        }
    });
    t1.join();
    t2.join();
    std::cout << "saw use_count > 1 about " << saw_two << " times\n";
}
```

Compile and run:

```sh
$ g++ -std=c++23 -O2 -pthread proxy_cow_race.cpp -o proxy_cow_race
$ ./proxy_cow_race
saw use_count > 1 about 31234 times
```

The same `shared_ptr` had `use_count()` bouncing madly between 1 and 2 more than thirty thousand times under concurrency. What does that mean? It means that between your `use_count() == 1` check inside `ensure_unique` and the moment you start modifying, in those few clock cycles, another thread can perfectly well push the reference count from 1 up to 2—and you, oblivious to the change, keep modifying the shared data in place. That is COW's fatal wound under multithreading.

So correct multithreaded COW must add an extra lock, putting "check the reference count" and "modify the data" into the same critical section (the way `ensure_unique` pairs with an external `std::mutex` in that Step 7 code earlier in this article), or simply admit: in modern C++, **move semantics are already cheap enough that COW's payoff often can't cover the concurrency complexity it introduces**. The standard library's decision back then to cut COW from `std::string` in favor of move semantics + SSO was exactly for this reason. COW isn't wrong—it's a technique that "looks ingenious but is riddled with pits", and if you must use it, think the concurrency part through first.

## Proxy vs. Decorator: Where Do They Really Differ

At this point you might ask: the Proxy pattern sounds an awful lot like the Decorator pattern—both are "wrap a layer, keep the interface unchanged, do a little extra work in the middle". Structurally, the two really are nearly identical (both are composition + interface conformability); what separates them is **intent**, not what the code looks like:

The Decorator's intent is "**adding** new behavior to an object", and it usually **can stack multiple layers** (wrap `Milk` around a `Coffee`, then `Sugar`, then `Whip`, each layer adding something); decorator and decoratee are typically peers, and the caller knows full well it is composing functionality. The Proxy's intent, by contrast, is "**controlling** access to an object": what it does is lazy loading, authorization, caching, remote forwarding, synchronization control—none of which is "adding business features", it's "exercising control along the access path". Proxies usually don't stack (you rarely see an authorization proxy wrapped in a caching proxy wrapped in a synchronization proxy), and the caller usually **has no idea at all** that it's using a proxy—which is precisely the proxy's goal of "transparent substitution".

Similar-looking code is fine; just keep one question straight in your head: "am I adding functionality to the object, or controlling access to it?" The former goes to Decorator, the latter to Proxy.

## Summary

Let's walk back through the whole Proxy journey:

| Proxy type | What it handles for the caller | Key pitfall |
|---|---|---|
| Property proxy | Intercepts field reads/writes, hangs getter/setter on them | `operator T()` implicit conversion can fire unexpectedly |
| Virtual proxy | Defers construction of expensive objects | A naked `if (!real_)` under concurrency is a data race—use `call_once` |
| Protection proxy | Authorization + auditing | The real object needs an abstract interface for transparent substitution |
| Remote proxy | Network communication, retries, timeouts | Complex failure modes (timeouts/partial failures); don't let callers stay blind to latency |
| Caching proxy | Memoization, avoiding repeated computation | Thread safety, consistency, eviction, breakdown/avalanche—all pits |
| Synchronization proxy | Adds an external lock to a non-thread-safe object | Coarse-grained lock = single core; multi-proxy compositions deadlock easily |
| COW proxy | Shared reads, copy on write | `shared_ptr::unique()` removed in C++20; `use_count()` unreliable under concurrency |

Note down these key conclusions:

- **The essence of a proxy** is "interface conformability + access control": the business class does only business, and cross-cutting concerns (lifetime, lazy loading, authorization, caching, network, locks) all belong to the proxy.
- **Concurrent lazy loading in a virtual proxy must be synchronized**: the single-threaded `if (!real_)` is a data race under multithreading, and `std::call_once` is modern C++'s clean answer.
- **`shared_ptr::unique()` was deprecated starting in C++17 and removed from the standard in C++20**—stop writing it in modern code; and `use_count() == 1` as a COW criterion was always an unreliable TOCTOU race under concurrency.
- **Proxy and Decorator share the same structure**; the difference is intent: Decorator "adds business behavior", stacks, and the caller is aware; Proxy "controls access", usually doesn't stack, and the caller is unaware.

::: tip Companion compilable project
The examples in this section ship as a complete compilable project under `code/volumn_codes/vol4/design-patterns/Proxy/` in the repository (`.h` + main + `CMakeLists.txt`); `cmake -S . -B build && cmake --build build` reproduces the outputs above.
:::

## References

- [cppreference: `std::shared_ptr<T>::unique`](https://en.cppreference.com/w/cpp/memory/shared_ptr/unique) (deprecated in C++17, removed in C++20)
- [cppreference: `std::call_once`](https://en.cppreference.com/w/cpp/thread/call_once) (the standard tool for "construct exactly once" under concurrency, since C++11)
- [cppreference: `std::memory_order`](https://en.cppreference.com/w/cpp/atomic/memory_order) (acquire/release needed if you hand-write DCLP)
- GoF, *Design Patterns: Elements of Reusable Object-Oriented Software* — the Proxy intent taxonomy (Virtual / Remote / Protection)
- The companion piece in this series: [Singleton Pattern: From Comment-Only Constraints to Meyer's Singleton](./01-singleton.md) (a full teardown of magic statics / DCLP)
