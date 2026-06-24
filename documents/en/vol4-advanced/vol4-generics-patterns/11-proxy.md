---
title: 'Proxy Pattern: You''ve Been Using It All Along Without Realizing It'
description: 'Starting with smart pointers, the "proxies you use every day without
  realizing it," we progressively derive six proxy patterns: virtual, protection,
  remote, cache, synchronization, and copy-on-write (COW). Along the way, we debunk
  the old practice of using `shared_ptr::unique()` in COW code.'
chapter: 11
order: 11
tags:
- host
- cpp-modern
- intermediate
- 代理模式
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
translation:
  source: documents/vol4-advanced/vol4-generics-patterns/11-proxy.md
  source_hash: c442a2df8ae0ca2b6d2bc0121f2c8e46a14cb8d564d386847f416fc290fcfdcc
  translated_at: '2026-06-24T00:58:38.997567+00:00'
  engine: anthropic
  token_count: 5158
---
# Proxy Pattern: You're Already Using It Without Realizing It

## What Problem Are We Actually Solving?

Let's set the name "Proxy Pattern" aside for a moment and look at a scenario you likely write every day. Suppose we have a working object `Source` that performs some actual work:

```cpp
class Source {
public:
    void do_work() { /* 真正的活儿 */ }
};

int main() {
    Source* src = new Source;
    src->do_work();      // 用得好好的
    delete src;          // 但你得记得手动释放
}
```

Sure, this code runs, but we both know where it's fragile—you're holding a raw pointer. When to `delete`, whether you might `delete` twice, or if you'll use the memory after `delete`—it all relies on your brain keeping track. The standard solution C++ provides is to wrap this manual management:

```cpp
int main() {
    auto src = std::make_unique<Source>();
    src->do_work();      // 该咋用咋用,语法完全没变
    // 离开作用域自动释放,不用 delete
}
```

You will notice something interesting: once we wrap it with `std::unique_ptr<Source>`, the call `src->do_work()` looks **exactly the same** as before. The proxied object is used exactly as it was before; the external interface is completely transparent, yet we gain RAII for free—we no longer need to worry about heap memory destruction and release.

To be honest, this is the Proxy Pattern. **The core of the Proxy Pattern is: using a proxy object to stand in for the real object, providing the same (or compatible) interface to the outside. The caller uses it just like the real object, while the proxy secretly does other work in the background.** In the case of `unique_ptr`, the work the proxy is doing secretly is "managing the lifecycle".

Why do we need this layer? Because in the real world, handing an object directly to the caller often comes with a bunch of **chores that are unrelated to the business logic but are mandatory**: loading an image takes two seconds, calling a remote service goes over the network, modifying a field requires logging, reading a sensitive resource requires checking permissions, calculating a result requires avoiding duplicate computation... If we stuff all these chores into the business object, the business class becomes bloated and brittle; if we scatter them across every call site, we end up with code duplication everywhere. The Proxy Pattern extracts this layer of "access control + cross-cutting behavior" and hands it to an intermediary. The business class focuses solely on business logic, and the caller focuses solely on using the interface.

Next, we will look one by one at how many different types of work this "intermediary" can actually do for us, and the pitfalls behind each one.

## Step one: Property Proxy—Turning field read/write into interceptable operations

Let's start with an example close to our daily work. You have a class with a field, say, a port configuration. The normal way to write this is just `int port;`, where anyone can read or write it, and the field itself has no "attitude." But one day, Product says: when assigning to this `port`, we must validate the range; when reading it, we must send a notification. You could, of course, wrap `port` with `get_port()` / `set_port()`—but that breaks the natural syntax of "direct field access," forcing every user to change to function calls.

The answer given by the Proxy Pattern is: write a `property<T>` template. It looks like a value on the outside (supports implicit conversion to `T`, supports `operator=`), but secretly hooks custom getters/setters onto the read and write actions:

```cpp
template <typename T>
class Property {
public:
    using Getter = std::function<T()>;
    using Setter = std::function<void(const T&)>;

    // 默认走「直接存值」的实现
    Property()
        : getter_([this] { return value_; }),
          setter_([this](const T& v) { value_ = v; }) {}

    explicit Property(const T& v) : Property() { value_ = v; }

    // 注入自定义的 getter/setter —— 校验、通知、日志都挂在这里
    Property(Getter g, Setter s) : getter_(std::move(g)), setter_(std::move(s)) {}

    operator T() const { return getter_(); }  // 读:走 getter
    Property& operator=(const T& v) {         // 写:走 setter
        setter_(v);
        return *this;
    }

private:
    mutable T value_{};   // mutable:getter_ 在 const 方法里也能改它
    Getter getter_;
    Setter setter_;
};
```

The key to this code is that it transforms the normally uninterceptable actions of "reading" and "writing" into two `std::function` objects. The two lambdas in the default constructor simply pass read and write operations through to the internal `value_`, behaving exactly like a normal field. However, as long as you provide custom `Getter`/`Setter` functions, you can attach whatever logic you need—validation, notifications, logging, rate limiting, you name it.

Note the `mutable` keyword—the getter is a `const` method, so it shouldn't modify members by default. However, since `value_` is declared `mutable`, it allows for modification. This is because the getter needs to read from and write to `value_` in "direct storage" mode, while semantically, reading a property should be a `const` operation. `mutable` helps us reconcile this contradiction.

What does it look like in practice? You will find that fields wrapped in `Property` behave almost indistinguishably from real fields—reading them implicitly converts to `T` (invoking the getter), and writing to them triggers `operator=` (invoking the setter):

```cpp
Property<int> port(8080);
int v = port;     // 隐式转 T,触发 getter
port = 9090;      // operator=,触发 setter
```

Let's first verify that the chain of implicit conversions and assignments works exactly as we described.

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
    int v = port;          // 隐式转 T -> getter
    std::cout << "read = " << v << "\n";
    port = 9090;           // operator= -> setter
    std::cout << "after assign = " << port.raw() << "\n";
}
```

Let's compile and run it:

```sh
$ g++ -std=c++20 -O2 -Wall property_verify.cpp -o property_verify
$ ./property_verify
read = 8080
after assign = 9090
```

Read and write operations both obediently followed the lambda we hung up. This is the essence of a property proxy—**intercepting read and write actions as hooks without changing the syntax used to access the field**.

However, there is a pitfall we need to point out immediately: `operator T()` performs an implicit conversion. This means that whenever the context requires a `T`, the compiler will secretly call the getter. While this is usually the desired behavior, it can easily trigger unexpected conversions in certain overload resolution scenarios. Therefore, in practice, property proxies are better suited for fields where "global interception of reads and writes" is truly necessary. Don't go replacing every `int` with `Property<int>`, or implicit conversions will dig you a bunch of inexplicable holes.

## Step 2: Virtual Proxy—Deferring expensive creation to the last moment

The next type is the most classic use case for the proxy pattern. Some objects are expensive to create: loading a high-resolution image, establishing a database connection, parsing a large configuration file... The common denominator for these objects is—**you don't necessarily need them every time**. If the program eagerly constructs all such objects upon startup, it will start slowly and consume memory, yet some objects might never be accessed during the entire program run.

The Virtual Proxy (or Lazy Proxy) offers a solution: first create a **cheap proxy** that only remembers "what the real object looks like" (such as a filename or connection string) without actually loading it. When the first real access is needed (e.g., when `display()` is called), the proxy swings into action to construct the real object, and all subsequent accesses are forwarded to it. This is lazy loading.

Let's use a classic example—displaying an image. Loading a real image from disk is slow, and we don't want to load it when constructing the proxy:

```cpp
#include <iostream>
#include <memory>
#include <string>

struct Image {
    virtual void display() = 0;
    virtual ~Image() = default;
};

// 真实图片:构造时就要从磁盘加载,很慢
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

The `RealImage` constructor calls `load_from_disk()`—this is the root of its "expense". The proxy's task is to defer this expensive construction until the first `display()` call:

```cpp
class ImageProxy : public Image {
public:
    explicit ImageProxy(const std::string& f) : filename_(f) {}
    void display() override {
        ensure_real();     // 第一次调用时才构造 RealImage
        real_->display();  // 转发给真实对象
    }

private:
    void ensure_real() {
        if (!real_) real_ = std::make_unique<RealImage>(filename_);
    }
    std::string filename_;
    std::unique_ptr<RealImage> real_;
};
```

You can see that the proxy's interface is identical to the real object (both inherit from `Image` and have `display()`), so the caller cannot even tell they are using a proxy. However, the real object is not constructed until `display()` is called for the first time. Before that, you only hold a lightweight `ImageProxy`, paying zero cost for loading.

Here is a completely correct implementation for **single-threaded** environments, which is the `if (!real_)` logic shown above. But we are not done yet—the real pitfall lies ahead.

## Let's verify: Lazy loading works in single-threaded contexts

First, let's confirm in a single-threaded environment that the proxy indeed achieves "load on first access, do not reload on subsequent accesses." We attach an atomic counter to `RealImage` to see how many times it is constructed during multiple `display()` calls:

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

Let's compile and run it:

```sh
$ g++ -std=c++23 -O2 -Wall proxy_lazy_verify.cpp -o proxy_lazy_verify
$ ./proxy_lazy_verify
before display, load_count = 0
Loading cat.png from disk (expensive)   # 注意:第一次 display 才加载
Displaying cat.png
Displaying cat.png
after 2 displays, load_count = 1 (expect 1)
```

`load_count` is solidly 1. The proxy defers the expensive load until the first access, and subsequent accesses reuse the same real object. This is the entire value of a virtual proxy.

## Warning: Lazy Loading Under Concurrency, `if (!real_)` is a Data Race

::: warning Warning
The `if (!real_) real_ = std::make_unique<...>` pattern shown above **should only be used in single-threaded contexts**. Once multiple threads access this proxy for the first time simultaneously, this code becomes a blatant **data race**—multiple threads might read `real_` as null at the same time and attempt to construct the real object concurrently. The last write wins, overwriting previous ones. The real object might be constructed multiple times, the pointer might be overwritten, and previously constructed objects might leak. All of this is possible.

Let's not just talk about it; let's run ThreadSanitizer to verify and let the tools do the talking:

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
        if (!real_) real_ = std::make_unique<RealImage>(id_);  // 数据竞争!
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
    #0 NaiveProxy::display() ...   # 读 real_
    ...
    Previous write of size 8 by thread T1:
    #0 NaiveProxy::display() ...   # 写 real_
```

TSan spotted it immediately: one thread is writing to `real_`, while another is reading from `real_`. There is no synchronization between them, so according to the standard, this is undefined behavior. Never simply port a single-threaded `if (!real_)` check directly to a multi-threaded environment; if you don't fix this, it will crash.
:::

There are two correct approaches. If you want "construct-once" semantics, C++ provides a clean answer—`std::call_once`. This belongs to the same class of mechanisms as the magic statics behind Meyer's Singleton; the language guarantees that only one thread will execute the initialization:

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

`call_once` uses lightweight atomic synchronization internally rather than a simple lock. Its semantics are precisely: "exactly one thread executes the initialization, while the remaining threads block and wait." Another approach is to manually implement Double-Checked Locking Pattern (DCLP) with `std::atomic` acquire/release semantics, which we deconstructed in detail in the singleton chapter, so we won't repeat it here. The conclusion is the same: **for lazy initialization under concurrency, a synchronization mechanism is mandatory; a raw `if` is insufficient.**

## Step 3: Protection Proxy — Extracting "Who Can Do What" from Business Logic

The next approach delegates this cross-cutting concern of permission checking to a proxy. Imagine a sensitive object with a `secret()` method that should only be accessible to specific identities. You might instinctively want to write `if (!has_permission) throw ...` inside `secret()`, but this welds the permission logic directly to the business logic, making changes ripple through the entire codebase.

The Protection Proxy approach works like this: both the proxy and the real object implement the same interface. The proxy performs permission checks before forwarding the request; it only forwards if the check passes, otherwise it denies access. This way, the real object `Sensitive` contains only pure business logic, while the proxy handles all permissions:

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

You can see that the proxy does just two things: first, it checks `allowed_`, throwing an exception if it fails; second, if it passes, it forwards the request verbatim to the real object. This check was originally part of the business logic, but now it's extracted to the access entry point. The real object remains completely unaware of permissions.

The benefit of this approach isn't just "cleanliness." Permission checks themselves are significant audit events—who accessed what, when, the result (success or denial), and the reason—are all core content of an audit log. A protection proxy always triggers at the access entry point, allowing it to record both successes and denials with attached reasons (missing permissions, expired credentials, untrusted source). It acts as a natural audit point. By putting authentication and auditing into the proxy, the business class focuses solely on business logic. If the permission policy changes, we only need to modify the proxy in one place.

There is a design trade-off to note here: `ProtectionProxy` in this example does not inherit from `Sensitive`'s abstract base class, but rather replicates the signature of `secret()`. This is because `Sensitive` itself has no abstract interface to inherit (it is a concrete class). In real-world engineering, we would typically first extract a pure virtual interface like `ISensitive`, and have both `Sensitive` and `ProtectionProxy` implement it. This way, the caller receives an `ISensitive&`, allowing for seamless replacement—this is the benefit of "interface equivalence" and the prerequisite for the transparent substitution capability of the Proxy pattern.

## Step 4: Remote Proxy — Hiding Network Details Locally

The Remote Proxy (also known as Communication Proxy) addresses a different scenario: the real object resides on another machine (or in another process), so calling it requires network communication. However, we don't want the caller code to be cluttered with details like "serialization, send request, wait for response, parse, retry, timeout"—these communication details are completely irrelevant to the business logic, yet extremely verbose.

The proxy solution is to create a local object that implements the same interface as the remote service, but internally translates every method call **into a network request**. To the caller, it looks just like a local object call; behind the scenes, the proxy handles parameter packing, sending, receiving, and unpacking:

```cpp
#include <chrono>
#include <stdexcept>
#include <string>
#include <thread>

// 模拟一个传输层(真实场景是 socket / HTTP / gRPC)
class Transport {
public:
    std::string send_request(const std::string& req) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));  // 模拟网络延迟
        if (req == "get_time") return "2025-09-29T12:00:00Z";
        if (req.rfind("compute:", 0) == 0) return "result:" + req.substr(8);
        throw std::runtime_error("unknown request");
    }
};

// 远程服务的本地抽象
struct RemoteService {
    virtual std::string get_time() = 0;
    virtual int remote_compute(int x, int y) = 0;
    virtual ~RemoteService() = default;
};

// 远程代理:把方法调用翻译成 transport 请求
class RemoteServiceProxy : public RemoteService {
public:
    explicit RemoteServiceProxy(Transport* t) : transport_(t) {}

    std::string get_time() override {
        return transport_->send_request("get_time");
    }

    int remote_compute(int x, int y) override {
        std::string req = "compute:" + std::to_string(x) + "," + std::to_string(y);
        transport_->send_request(req);              // 真实场景会解析 "result:..."
        return x + y;
    }

private:
    Transport* transport_;
};
```

You see, what the proxy does is translate `get_time()` into the string `"get_time"` to send out, and translate `remote_compute(x, y)` into `"compute:x,y"` to send out. The caller still writes `service.remote_compute(3, 4)`, completely unaware that this call traversed a network.

The expression `req.rfind("compute:", 0) == 0` deserves a quick mention—this is the idiomatic way in C++ to check if a string starts with a specific prefix (`rfind` searches starting at position 0; if found, it returns 0; otherwise, it returns `npos`). In C++20, we can directly use `req.starts_with("compute:")`, which is more straightforward. Here, we use `rfind` so the code compiles under C++17 as well.

The real difficulty with a remote proxy isn't this translation, but **fault handling**: network calls can time out, partially fail, retry halfway through, or return with incorrect content. These failure modes are much more complex than local calls. Therefore, remote proxies in production engineering often incorporate retry strategies, timeout controls, circuit breaking, and fallback mechanisms. This is also why RPC frameworks like gRPC and Thrift help you automatically generate proxy classes—they stuff all that complex fault handling into the generated proxy, so you just focus on calling the interface.

## Step 5: Caching Proxy — Storing Results of Repeated Computations

A caching proxy (also known as a memoization proxy) addresses the issue where "repeating the same calculation is wasteful." Some computations are inherently expensive (parsing a large expression, querying a database, running a complex model), yet the same inputs often appear repeatedly. If we cache the results at the proxy layer, the second identical request hits the cache directly, saving us the trouble of bothering the real object:

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
            return it->second;          // 命中缓存,直接返回
        }
        int result = real_->compute(x); // 没命中,才算
        cache_[x] = result;
        return result;
    }

private:
    Expensive* real_;
    std::unordered_map<int, int> cache_;
};
```

The proxy checks the cache in `compute` first; if it hits, it returns immediately. If it misses, it calculates. The real object, `Expensive`, is completely unaware of the caching layer—it just focuses on the calculation.

While the caching proxy looks simple, the real difficulty lies in the **caching strategy**. First is thread safety: the `cache_` above is a standard `unordered_map`, so concurrent access from multiple threads causes data races. In production code, we would either add a lock or switch to a concurrent hash table. Next is the consistency model: can your application tolerate temporary inconsistency with the backend? If yes, a cache-aside pattern with a TTL (Time-To-Live) is sufficient. If not (for example, with account balances), caching might not be the right choice. Then there is the eviction policy: a cache cannot grow indefinitely; it needs an LRU (Least Recently Used) mechanism or a capacity limit. Finally, we must prevent cache stampedes (where massive concurrent requests miss the same key and hammer the backend) and cache avalanches (where large numbers of keys expire simultaneously). Each of these topics could easily be its own discussion, but they share one common trait—**they can all be encapsulated within the proxy without modifying a single line of the business logic class**. This is the value of the Proxy Pattern: "centralizing cross-cutting concerns."

## Step 6: Synchronization Proxy—Wrapping a Non-Thread-Safe Object with a Lock

The Synchronization Proxy addresses this scenario: you have an object that is not thread-safe (it might be from a third-party library, legacy code, or designed without locks for performance), but you now need to use it in a multithreaded context. You do not want to (or cannot) modify the source code of that object to add locking.

The proxy approach is to wrap it, automatically acquiring and releasing a lock around every method call:

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
        std::lock_guard<std::mutex> lk(mtx_);   // 进方法先锁
        real_->op();                            // 真正干活
    }                                           // 离开自动解锁

private:
    SomeInterface* real_;
    std::mutex mtx_;
};
```

The appeal of this approach is that we don't need to touch the actual object's code. We simply wrap a layer around it, and it becomes thread-safe immediately. For third-party libraries or legacy code that cannot be modified, this is a lifesaver.

However, I must offer a reality check. **A synchronization proxy is not a silver bullet for thread safety; coarse-grained locks will effectively revert you to single-core performance.** Look at the `mtx_` above: it is shared by the entire proxy. This means that at any given moment, only one thread can call `op()`. If `op()` takes a long time, all other threads must queue up, instantly negating the advantages of multi-core processing. Even more troublesome is deadlocking: if a call chain enters proxy A holding A's lock, then calls proxy B to acquire B's lock, while another thread does the reverse (holding B then acquiring A), a classic circular wait condition forms.

Therefore, the practical points for synchronization proxies are: keep lock granularity as fine as possible (shard by resource, or use a read-write lock like `std::shared_mutex` to allow concurrent reads in read-heavy scenarios); keep lock holding times as short as possible (do everything possible before entering the critical section); and establish a consistent locking order across multiple proxies. If the scenario allows, using optimistic concurrency (versioning + CAS) to replace pessimistic locking often yields better scalability. A synchronization proxy is just a starting point, not the finish line.

## Step 7: Copy-On-Write (COW) — The Old Days of `shared_ptr::unique()`

The final variant is the most technically dense and error-prone within the proxy pattern. A Copy-On-Write (COW) proxy addresses a specific scenario: an object is shared across multiple locations where reads far outnumber writes. We want them to share the same underlying data to save memory; however, when one location needs to modify the data, it must first copy the data and modify its own private copy, ensuring it does not affect other sharers.

Historically, `std::string` utilized COW (later removed in C++11 because the synchronization cost of reference counting in multi-threaded environments actually hindered performance). Let's demonstrate this with a `CowString`:

```cpp
#include <memory>
#include <string>

class CowString {
public:
    CowString() : data_(std::make_shared<std::string>()) {}
    CowString(const std::string& s) : data_(std::make_shared<std::string>(s)) {}

    // 读:直接返回 const 引用,多个 CowString 共享同一份
    const std::string& str() const { return *data_; }

    // 写:先确保独占(必要时复制),再改
    void append(const std::string& s) {
        ensure_unique();
        data_->append(s);
    }

private:
    void ensure_unique() {
        if (data_.use_count() > 1) {                         // 不独占
            data_ = std::make_shared<std::string>(*data_);   // 复制一份
        }
    }
    std::shared_ptr<std::string> data_;
};
```

The core of COW is the `ensure_unique()` trick—check "Am I the sole owner?" before writing, and copy if not. When multiple `CowString` objects are copy-constructed, they share the same `shared_ptr`. They read from the same memory, and only create separate copies when modifying. In scenarios where reads far outnumber writes, this saves significant copying overhead.

However—this code hides a **legacy pitfall**, which is also the bad practice found in the source material.

::: warning Pitfall Ahead
You might see `ensure_unique()` written like this in older resources:

```cpp
void ensure_unique() {
    if (!data_.unique()) {        // ⚠️ shared_ptr::unique() —— C++20 起已从标准移除
        data_ = std::make_shared<std::string>(*data_);
    }
}
```

The member function `std::shared_ptr::unique()` was marked as **deprecated** in C++17 and was **officially removed from the standard in C++20**. This means that, according to the ISO standard, `shared_ptr` no longer has the `unique()` member starting from C++20. The fact that it still compiles in libstdc++ or libc++ today is purely because mainstream standard library implementations have retained it as an extension for compatibility reasons—but this doesn't make it correct. If you switch to a stricter implementation or a future version, your code will fail to compile. **Do not write `shared_ptr::unique()` in modern C++.**
:::

The more critical pitfall isn't actually the "function removal," but rather that **the criterion of `unique()` (and `use_count() == 1`) is inherently unreliable under concurrency**. The `ensure_unique` logic in COW (Copy-On-Write) is: "I glance at the reference count; if it's one, I modify in-place; otherwise, I copy." However, the reference count is a changing value—you might just read it as 1 and are about to modify in-place, but another thread恰好 happens to copy a `shared_ptr` at that exact moment, causing the reference count to jump to 2. Meanwhile, you are still foolishly modifying the shared data in-place, directly corrupting the other thread's copy.

This is a classic TOCTOU (Time-Of-Check-To-Time-Of-Use) race condition. The atomic reference counting of `shared_ptr` alone cannot prevent this, because the synchronization of the reference count itself only guarantees the count is correct; it does not guarantee that "no one else will immediately copy when the count is 1." Let's verify this—we will have one thread repeatedly copy and release a `shared_ptr` (creating reference count jitter), while another thread repeatedly observes `use_count()`:

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
            (void)copy;           // 析构: 2 -> 1
        }
    });
    std::atomic<long> saw_two{0};
    std::thread t2([&] {
        for (int i = 0; i < 100000; ++i) {
            if (shared.use_count() > 1) ++saw_two;   // 会观察到引用计数跳变
        }
    });
    t1.join();
    t2.join();
    std::cout << "saw use_count > 1 about " << saw_two << " times\n";
}
```

Let's compile and run it:

```sh
$ g++ -std=c++23 -O2 -pthread proxy_cow_race.cpp -o proxy_cow_race
$ ./proxy_cow_race
saw use_count > 1 about 31234 times
```

With the same `shared_ptr`, under concurrency, `use_count()` jumped frantically between one and two over thirty thousand times. What does this mean? It means that inside your `ensure_unique`, the check for `use_count() == 1` is completely vulnerable. In the few clock cycles between when you check and when you actually modify the data, another thread could easily bump the reference count from one to two. You would remain unaware of this change and proceed to modify shared data in place. This is the fatal flaw of COW in multithreaded environments.

Therefore, correct multithreaded COW requires an additional lock. You must place the "check reference count" and "modify data" steps within the same critical section (just like the `ensure_unique`配合 external `std::mutex` in step seven of this article's code). Alternatively, simply admit this: in modern C++, **move semantics are cheap enough that the benefits of COW often fail to outweigh the concurrency complexity it introduces**. This is exactly why the standard library removed COW from `std::string`, replacing it with move semantics + SSO. COW isn't wrong, but it is a technique that "looks clever but is full of pitfalls in practice." If you use it, make sure you have a solid plan for concurrency first.

## Proxy vs. Decorator: What is the Real Difference?

At this point, you might ask: the Proxy pattern sounds very similar to the Decorator pattern—both "wrap a layer, keep the interface the same, and do some extra work in the middle." Indeed, their structures are almost identical (both use composition + conforming interfaces). The difference lies in **intent**, not code structure:

The intent of a **Decorator** is to "**add** new behavior to an object," and it usually **can be stacked in multiple layers** (e.g., a `Coffee` wrapped with `Milk`, wrapped with `Sugar`, wrapped with `Whip`—each layer adds something). Decorators and the objects they decorate are often peers, and the caller explicitly knows they are composing features. The intent of a **Proxy**, however, is to "**control** access to an object." It handles lazy loading, authentication, caching, remote forwarding, or synchronization control—none of which are "adding business functionality," but rather "managing the access path." Proxies are usually not stacked (you rarely see an authentication proxy wrapped by a caching proxy, which is then wrapped by a synchronization proxy), and the caller often **doesn't even know** they are using a proxy. This "transparent replacement" is precisely the goal of a proxy.

It doesn't matter if the code looks similar; as long as you clearly distinguish in your mind whether "I am adding functionality to an object" or "I am controlling access to an object," you are set. Use the Decorator for the former, and the Proxy for the latter.

## Summary

Let's review the path of the Proxy pattern:

| Proxy Type | Responsibilities Managed on Behalf of the Caller | Key Pitfalls |
|---|---|---|
| Property Proxy | Intercept field reads/writes, hook up getter/setter | `operator T()` implicit conversion may trigger unintentionally |
| Virtual Proxy | Lazy construction of expensive objects | Bare `if (!real_)` under concurrency is a data race; use `call_once` |
| Protection Proxy | Authentication + Auditing | Real objects need an abstract interface for transparent replacement |
| Remote Proxy | Network communication, retry, timeout | Failure modes are complex (timeout/partial failure); don't let the caller be oblivious to latency |
| Caching Proxy | Memoization, avoid repeated calculation | Thread safety, consistency, eviction, and breakdown/avalanche are all pitfalls |
| Synchronization Proxy | Add external locks to non-thread-safe objects | Coarse-grained locks = single core; combining multiple proxies easily leads to deadlocks |
| COW Proxy | Shared read, copy-on-write | `shared_ptr::unique()` removed in C++20; `use_count()` is unreliable under concurrency |

Keep these key conclusions in mind:

- The **essence of a proxy** is "interface conformance + access control." Business classes handle business logic, while cross-cutting concerns (lifecycle, lazy loading, authentication, caching, network, locking) belong to the proxy.
- **Concurrent lazy loading in a Virtual Proxy must use synchronization**. Single-threaded `if (!real_)` is a data race in multithreaded contexts; `std::call_once` is the clean answer in modern C++.
- **`shared_ptr::unique()` was deprecated in C++17 and removed in C++20**; don't use it in modern code. Furthermore, using `use_count() == 1` as a criterion for COW is inherently unreliable under concurrency due to TOCTOU races.
- **Proxies and Decorators share the same structure**, differing only in intent: Decorators "add business behavior," are stackable, and the caller is aware; Proxies "control access," are usually not stacked, and the caller is unaware.

::: tip Accompanying Compilable Project
The examples for this section are available as a complete, compilable project in the repository at `code/volumn_codes/vol4/design-patterns/Proxy/` (`.h` + main + `CMakeLists.txt`). Run `cmake -S . -B build && cmake --build build` to reproduce the outputs shown above.
:::

## References

- [cppreference: `std::shared_ptr<T>::unique`](https://en.cppreference.com/w/cpp/memory/shared_ptr/unique) (Deprecated in C++17, removed in C++20)
- [cppreference: `std::call_once`](https://en.cppreference.com/w/cpp/thread/call_once) (Standard tool for "construct once" under concurrency, since C++11)
- [cppreference: `std::memory_order`](https://en.cppreference.com/w/cpp/atomic/memory_order) (If writing DCLP manually, acquire/release is needed)
- GoF, *Design Patterns: Elements of Reusable Object-Oriented Software* — Proxy intent classification (Virtual / Remote / Protection)
- Sister article in this series: [Singleton Pattern: From Comment Constraints to Meyer's Singleton](./01-singleton.md) (Full breakdown of magic statics / DCLP)
