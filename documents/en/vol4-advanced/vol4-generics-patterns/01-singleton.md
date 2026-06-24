---
title: 'Singleton Pattern: From Comment Constraints to Meyer''s Singleton'
description: Starting from the most primitive "reminder comments," we will progressively
  derive a thread-safe Meyer's Singleton, debunk the obsolete Double-Checked Locking
  Pattern (DCLP), and wrap up with dependency injection.
chapter: 11
order: 1
tags:
- host
- cpp-modern
- intermediate
- 单例模式
difficulty: intermediate
platform: host
cpp_standard:
- 11
- 17
- 20
reading_time_minutes: 18
related:
- 工厂方法与抽象工厂
prerequisites:
- 'Chapter 6: 类与对象'
translation:
  source: documents/vol4-advanced/vol4-generics-patterns/01-singleton.md
  source_hash: 138f2f4f5f33eaa47850b552e5dd2ee87dbf4fb3bbf3b41ab9bbc1b85df46459
  translated_at: '2026-06-24T00:52:47.485102+00:00'
  engine: anthropic
  token_count: 2777
---
# Singleton Pattern: From Comment Constraints to Meyer's Singleton

## What problem are we actually solving?

Let's hold off on the definitions for a moment. Consider a common scenario: a program needs to read a global configuration (`host`, `port`, `username`, etc.). This configuration remains unchanged from startup to shutdown, and any part of the program might need to access it. You could, of course, wrap the configuration in an object and pass it around everywhere—but you will quickly get annoyed: every function signature gains an extra `Config&` parameter, threaded through layers of calls, just so some low-level utility function can read a `port`.

The Singleton pattern addresses exactly this kind of requirement: **ensuring that an object has only one instance during the program's lifetime and providing a global access point**. Loggers, configuration managers, database connection pools, and device driver interfaces all have a natural requirement for "global uniqueness."

However, in C++, "global uniqueness" is **not automatically guaranteed just because you declared it**. C++ is a language that loves implicit operations: unless you explicitly forbid it, copy construction, assignment, move semantics, or even an accidental pass-by-value can silently create a second instance behind your back. So, the real question we need to answer is—**how to use language mechanisms, rather than human conventions, to strictly enforce "only one"**.

In the following sections, we will proceed step-by-step, starting with the crudest approach, examining why each step falls short, and finally deriving the standard answer in modern C++.

## Step 1: The most primitive approach—writing comments (incorrect example)

Many developers, when encountering a "create only once" requirement for the first time, instinctively react like this:

```cpp
struct GlobalOled {
    // You should only invoke the creation for once!!!
    GlobalOled();
};
```

Throwing in a bunch of exclamation marks and writing constraints into comments. Honestly, this isn't an exaggeration; I have genuinely seen this style in production code. The problem is that these constraints are written for humans, while the **compiler doesn't check them at all**.

Let's assume that everyone reads comments carefully during development—but C++ can pull tricks behind your back. For instance, someone might write `GlobalOled another = oled;` for convenience in some function. This is a perfectly normal copy construction, yet in that single line, your "globally unique" guarantee is broken. Or perhaps they stuff it into a container by value or capture it in a `std::function`. During RAII initialization, a copy or move can be triggered at any moment. Comments won't stop any of these.

So, this path doesn't work. We need to make the compiler enforce the rules for us.

## Step Two: Block All Copying Paths — `= delete`

Since the pitfall lies in "being secretly copied," the most direct solution is to **disable all copying and moving paths** that break uniqueness:

```cpp
struct GlobalOled {
public:
    // ...

private:
    GlobalOled();
    GlobalOled(const GlobalOled&) = delete;
    GlobalOled& operator=(const GlobalOled&) = delete;
    GlobalOled(GlobalOled&&) = delete;
    GlobalOled& operator=(GlobalOled&&) = delete;
};
```

`= delete` is a powerful tool introduced in C++11: a deleted function still participates in overload resolution, and if anyone attempts to call it, the compiler issues an error directly. This is far superior to comments—now "non-copyable" is a compile-time hard constraint.

However, a new conflict arises here: we also placed the constructor in `private` to prevent arbitrary external construction. But now, **no one can create it**, not even that "single instance" itself. We are missing a controlled entry point: we must prevent external arbitrary `new` calls, while still providing a way for the outside world to get the instance.

## Step 3: Private Constructor + Static Access Point — Meyer's Singleton

Let's consolidate the problem: we entrust the uniqueness of construction to an entry function we control. If external code wants to use the instance, it must go through this entry. As for "how to guarantee it is constructed only once inside the entry," C++11 provides a solution so clean it's practically free—**`static` local variables inside a function**:

```cpp
class GlobalOled {
public:
    static GlobalOled& get_instance() {
        static GlobalOled oled;  // 只在首次经过时初始化一次
        return oled;
    }

private:
    GlobalOled();
    GlobalOled(const GlobalOled&) = delete;
    GlobalOled& operator=(const GlobalOled&) = delete;
    GlobalOled(GlobalOled&&) = delete;
    GlobalOled& operator=(GlobalOled&&) = delete;
};
```

This code pattern has a name: **Meyer's Singleton** (named after Scott Meyers). The core of it is just one line: `static GlobalOled oled;`. However, the guarantee behind this line is robust: since C++11, the standard explicitly states that **if multiple threads enter this declaration for the first time concurrently, only one thread will execute the initialization, while the remaining threads will block waiting until initialization completes** ([stmt.dcl], commonly known as *magic statics*).

What does this mean? **The language guarantees thread-safe initialization of the singleton for us, so we don't have to write a single lock.** Before taking this for granted, let's verify this behavior.

## Verification: Are magic statics really thread-safe?

Talk is cheap. Let's write a small program where 500 threads race to call `get_instance()`. We will place an atomic counter in the constructor to see exactly how many times it is constructed:

```cpp
#include <atomic>
#include <thread>
#include <vector>
#include <iostream>

class MeyersSingleton {
public:
    static MeyersSingleton& instance() {
        static MeyersSingleton s;  // C++11 [stmt.dcl]: 线程安全地初始化一次
        return s;
    }
    static inline std::atomic<int> construct_count{0};

private:
    MeyersSingleton() { ++construct_count; }
    MeyersSingleton(const MeyersSingleton&) = delete;
    MeyersSingleton& operator=(const MeyersSingleton&) = delete;
};

int main() {
    constexpr int kThreadCount = 500;
    std::vector<std::thread> ts;
    ts.reserve(kThreadCount);
    for (int i = 0; i < kThreadCount; ++i) {
        ts.emplace_back([] { auto& s = MeyersSingleton::instance(); (void)s; });
    }
    for (auto& t : ts) t.join();
    std::cout << "construct_count = " << MeyersSingleton::construct_count
              << " (expect 1)\n";
}
```

Let's compile and run this (with `-O2` optimization enabled to intentionally intensify the race condition):

```sh
$ g++ -std=c++23 -O2 -pthread singleton_verify.cpp -o singleton_verify
$ for i in 1 2 3 4 5; do ./singleton_verify; done
construct_count = 1 (expect 1)
construct_count = 1 (expect 1)
construct_count = 1 (expect 1)
construct_count = 1 (expect 1)
construct_count = 1 (expect 1)
```

Running five consecutive times with 500 threads racing concurrently, `construct_count` stays rock-solid at 1. This is the promise of magic statics—you don't need to write locks, use `call_once`, or worry about race conditions. The language guarantees "initialize once" for you. **In modern C++, Meyer's Singleton is the definitive choice for singletons; there is absolutely no reason to hand-roll anything more complex.**

## Pitfall Warning: The Old Days of DCLP

::: warning Pitfall Warning
If you are looking at pre-C++11 resources, you will likely encounter something called **DCLP (Double-Checked Locking Pattern)**. Many online blogs still circulate it, sometimes even like the example below—**this implementation is flawed, do not copy it**:

```cpp
// ⚠️ 反面教材:memory_order_consume 在这里不靠谱
static GlobalOled& get_instance() {
    GlobalOled* p = oled.load(std::memory_order_consume);
    if (p) return *p;
    std::lock_guard<std::mutex> _(instance_lock);
    p = oled.load(std::memory_order_consume);
    if (!p) {
        p = new GlobalOled;
        oled.store(p, std::memory_order_release);
    }
    return *p;
}
```

The issue lies with `memory_order_consume`. The original intent of *consume* is to "only protect accesses with dependencies," which sounds sufficient for DCLP. However, the standard significantly weakened its semantics after C++17. In practice, almost all mainstream compilers **directly downgrade it to acquire**—meaning, while you think you are writing the weak guarantee of *consume*, you get the strong guarantee of *acquire* at runtime. The semantics do not match what you wrote, and portability is abysmal. Manually writing *consume* remains a minefield to this day.
:::

If you absolutely must write DCLP manually (and I will emphasize this again: **in modern C++, `magic statics` are sufficient, so manual writing is unnecessary**), the correct memory order is **acquire / release**:

```cpp
class DclpSingleton {
public:
    static DclpSingleton* instance() {
        auto* p = ptr_.load(std::memory_order_acquire);  // 第一次检查(无锁)
        if (!p) {
            std::lock_guard<std::mutex> lk(mtx_);
            p = ptr_.load(std::memory_order_relaxed);    // 第二次检查(持锁)
            if (!p) {
                p = new DclpSingleton();
                ptr_.store(p, std::memory_order_release);  // 发布
            }
        }
        return p;
    }

private:
    DclpSingleton() = default;
    DclpSingleton(const DclpSingleton&) = delete;
    DclpSingleton& operator=(const DclpSingleton&) = delete;
    static inline std::mutex mtx_;
    static inline std::atomic<DclpSingleton*> ptr_{nullptr};
};
```

`acquire` guarantees that when a non-null pointer is read, the object it points to is fully constructed. `release` guarantees that when the pointer is written back, the object's construction is visible to other threads. Let's verify this again: when two threads race to acquire the lock, do they get the same instance?

```sh
$ ./singleton_verify
Meyers:  construct_count = 1 (expect 1)
DCLP:    same instance = true (expect true)
```

The conclusion is sound. However, I must reiterate one thing: **this DCLP code is just here to show you what the "old way" looked like when done correctly; it is not meant for you to use.** Meyer's Singleton replaces that entire mess of DCLP with a single `static`, and it involves no heap allocation (`new`), no raw pointers, and no destruction order issues. DCLP is a legacy from pre-C++11 times; it is only useful now for recognizing it when reading old code.

## Real-World Example: A Working Global Configuration Reader

Discussing `GlobalOled` is too abstract, so let's build something practical. The following `ConfigManager` is a typical singleton configuration reader: it reads a configuration file in `key=value` format, provides an interface to query by key, and returns `std::optional` to indicate that "the key might not exist":

```cpp
#pragma once
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>

class ConfigManager {
public:
    static ConfigManager& instance() {
        static ConfigManager config;  // Meyer's Singleton
        return config;
    }

    void read_from_file(const std::filesystem::path& path);
    std::optional<std::string> get_value(const std::string& key);

private:
    ConfigManager() = default;
    ~ConfigManager() = default;
    ConfigManager(const ConfigManager&) = delete;
    ConfigManager& operator=(const ConfigManager&) = delete;
    ConfigManager(ConfigManager&&) = delete;
    ConfigManager& operator=(ConfigManager&&) = delete;

    void parse_line(const std::string& line);
    std::unordered_map<std::string, std::string> maps_;
};
```

You see, the pattern is exactly the same as before: a `static` local variable inside `instance()`, all four special member functions deleted, and a private constructor. The `std::optional` return value forces the caller to handle the "key not found" scenario, which is more elegant than returning an empty string or throwing an exception. `std::filesystem::path` naturally provides cross-platform path support.

```cpp
void ConfigManager::parse_line(const std::string& line) {
    if (line.empty() || line[0] == '#') return;  // 空行 / 注释跳过

    const auto eq = line.find_first_of('=');
    if (eq == std::string::npos) return;          // 不是合法 kv,跳过

    std::string key = line.substr(0, eq);
    std::string val = line.substr(eq + 1);
    key.erase(key.find_last_not_of(" \t") + 1);   // rtrim key
    val.erase(0, val.find_first_not_of(" \t"));   // ltrim val
    maps_.insert({key, val});
}
```

Here is how we use it; we can access that unique instance from anywhere:

```cpp
auto& config = ConfigManager::instance();
config.read_from_file("app.conf");
if (auto host = config.get_value("host")) {
    connect(*host);  // 只有真的拿到值才进入
}
```

::: tip Compilable Companion Project
The complete code for this section (including handling of `#` comments, blank lines, a 500-thread concurrent read test, and a minimal `Logger` singleton) is available in this repository. Just clone it and run CMake: [Singleton](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/tree/main/code/volumn_codes/vol4/design-patterns/Singleton). A quick note: the `GlobalConfig` reads `test_config.txt` from the same directory (CMake automatically copies it to `build/`), so run it via `cd build && ./ConfigManager`; for `Logger`, simply run `./build/Logger`.
:::

## Why Singletons Are Unpopular

At this point, we have a correct, thread-safe, and very simple-to-write singleton. But the story doesn't end here—I must be honest with you: **the singleton pattern actually has a rather poor reputation in software engineering**. Many engineers consider it a pattern to be used with caution, or even an anti-pattern. Why?

**First, it overrides single responsibility.** A `ConfigManager` acts as both "configuration manager" and "global access". As soon as you write `ConfigManager::instance().get_value(...)` anywhere, this global object pierces your module's interface boundary. Originally, your function should only depend on the abstraction of "being able to get a certain configuration value," but now it is directly coupled to a specific global implementation.

**Second, it makes unit testing extremely painful.**

```cpp
void do_work() {
    ConfigManager::instance().get_value("timeout");  // 写死成全局
}

void test_do_work() {
    // 想换个假的 ConfigManager 来测边界条件?抱歉,改不了。
    do_work();
}
```

Since the instance is globally unique and hardcoded within `instance()`, you cannot swap it out for a mock during testing. The Singleton forces every module that uses it to be tested alongside the real singleton, thereby violating the premise of "unit independence" in unit testing.

**Third, it violates the Open-Closed Principle (OCP).** If you eventually need to switch from "one global configuration" to "one configuration per tenant," you will find that the moment `ConfigManager` was designed as a Singleton, extending it to support multiple instances requires a massive refactor.

**Fourth, static Singletons have their own lifecycle pitfalls.** If a Singleton holds heavyweight resources (large buffers, file handles), it will persist until the program ends, even if you only used it briefly. Even more insidious is that **the destruction order of static objects is uncontrollable**—if Singleton A depends on another static object B, and B is destroyed before A, then A accessing B during its own destruction results in undefined behavior (the notorious *static deinitialization order* problem).

## Improvement: Contain the Singleton within a Subsystem — Dependency Injection

The root cause of all these flaws is the same: **the Singleton "pierces" through the interface, forcing global state onto every caller.** A healthier approach is to invert the dependency relationship—**instead of letting the caller reach for the global, the upper layer should actively inject the required objects:**

```cpp
class OledUpdater {
public:
    explicit OledUpdater(GlobalOled& oled) : oled_(oled) {}

    void do_work() {
        oled_.process_buffer_update();
    }

private:
    GlobalOled& oled_;
};

// 使用时手动注入
int main() {
    auto& oled = GlobalOled::get_instance();   // 单例被关在 main 这一层
    OledUpdater updater(oled);                 // updater 只依赖引用,不知道全局
    updater.do_work();
}
```

This inversion brings three immediate benefits: `OledUpdater` no longer relies on global state; it depends only on a `GlobalOled&`, so during testing we can easily pass in a fake implementation. The scope of the singleton is compressed to the `main` subsystem layer, rather than having `::get_instance()` scattered throughout the program. If we need to extend this to multiple instances later, we only need to modify the assembly code in `main`; `OledUpdater` doesn't need to change a single line.

This is the mindset of **Dependency Injection (DI)**. True singletons—the kind where "there is only one in the program and you can't avoid it"—are actually very rare. Most of the time, when we think we need a singleton, what we actually need is "uniqueness within a specific subsystem," and that requirement is easily solved by injecting a reference.

## Summary

Let's review the entire evolution path:

| Stage | Approach | Why it falls short |
|---|---|---|
| Comment constraints | Write `// only once` | The compiler doesn't check it, can't stop implicit copies |
| `= delete` | Disable copy/move | Can't construct instances anymore |
| Meyer's Singleton | Private ctor + `static` local variable | **Sufficient** (C++11 magic statics guarantee thread safety) |
| Hand-written DCLP | Double-checked lock + acquire/release | Historical legacy, not needed in modern C++ |
| Dependency Injection | Confine singleton to subsystem, inject reference | Solves global state pollution and testability |

Keep these key conclusions in mind:

- **For singletons in modern C++, the first choice is Meyer's Singleton** (private constructor + `static` local variable + deleted copy/move). We don't need to write a single line of locking code.
- **Don't hand-write DCLP**, especially avoid using `memory_order_consume`—magic statics have already taken care of thread-safe initialization.
- The real cost of a singleton isn't in its implementation, but in **global state pollution, difficulty in testing, violating OCP, and static destruction order**.
- In most "I need a singleton" scenarios, what is actually needed is **Dependency Injection**—constraining uniqueness to a subsystem rather than letting it run wild globally.

## References

- [cppreference: Static local variables](https://en.cppreference.com/w/cpp/language/storage_duration#Static_local_variables) (magic statics, since C++11)
- [cppreference: `std::memory_order`](https://en.cppreference.com/w/cpp/atomic/memory_order) (semantics of acquire/release/consume)
- Scott Meyers, *Effective C++* Item 4 / Andrei Alexandrescu, *Modern C++ Design* Chapter 6 (Singletons and Multithreading)
- Companion compilable project: [Singleton](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/tree/main/code/volumn_codes/vol4/design-patterns/Singleton)
