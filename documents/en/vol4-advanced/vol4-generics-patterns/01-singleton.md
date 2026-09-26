---
title: 'Singleton Pattern: From Comment-Only Constraints to Meyer''s Singleton'
description: 'Starting from the most primitive "just write a comment" constraint, we work our way step by step toward a thread-safe Meyer''s Singleton, call out DCLP for the obsolete folklore it is, and close with dependency injection'
chapter: 11
order: 1
tags:
  - host
  - cpp-modern
  - intermediate
  - 单例模式
difficulty: intermediate
platform: host
cpp_standard: [11, 17, 20]
reading_time_minutes: 18
related:
  - 'Factory Method and Abstract Factory: From a Single Switch to Creating a Family of Products'
prerequisites:
  - 'Chapter 6: Classes and Object-Oriented Programming'
translation:
  source: documents/vol4-advanced/vol4-generics-patterns/01-singleton.md
  source_hash: 138f2f4f5f33eaa47850b552e5dd2ee87dbf4fb3bbf3b41ab9bbc1b85df46459
  translated_at: '2026-09-26T05:02:57+00:00'
  engine: anthropic
  token_count: 7900
---

# Singleton Pattern: From Comment-Only Constraints to Meyer's Singleton

## What problem are we actually solving

Let's not rush to a definition. Think of the most common scenario: the program needs to read a global configuration (`host`, `port`, `username`...), that config never changes from startup to shutdown, and any corner of the program might want to read it. Sure, you can stuff the config into an object and pass that object all over the place — but you'll tire of it fast: every function signature grows one more `Config&` parameter, threaded down layer after layer, just so some low-level utility function can read a single `port`.

This is exactly the kind of need the Singleton pattern addresses: **guarantee that an object has only one instance for the entire run of the program, and provide a global access point to it**. Loggers, configuration managers, database connection pools, device driver interfaces — all of them carry a natural "globally unique" requirement.

But "globally unique" is **not something that holds just because you declared it so** in C++. C++ is a language that loves implicit operations: unless you explicitly seal them off, copy construction, assignment, moves — even an accidental pass-by-value — can quietly manufacture a second instance behind your back. So the question we really need to answer is: **how do we nail down "there can be only one" with language mechanics, rather than with human discipline**.

So let's take it step by step, starting from the silliest version, seeing exactly why each step falls short, until we corner the standard modern C++ answer.

## Step 1: The most primitive approach — a comment (an anti-example)

The first time many people meet a "create this only once" requirement, the knee-jerk reaction looks like this:

```cpp
struct GlobalOled {
    // You should only invoke the creation for once!!!
    GlobalOled();
};
```

A pile of exclamation marks, with the constraint written into a comment. Honestly, this is not an exaggeration — I have genuinely seen this style in production code. The problem is that this kind of constraint is written for humans, and **the compiler takes no part in enforcing it**.

Let's assume everyone reads comments carefully during development — but C++ does things behind your back where you can't see. Say somebody, for convenience inside some function, casually writes `GlobalOled another = oled;`. That is a perfectly ordinary copy construction, yet in that one line your "globally unique" guarantee is gone. Or someone tucks it by value into a container, or into a `std::function` capture — copy or move operations can fire at any point along an RAII initialization path. A comment blocks none of these.

So this road is a dead end. We need the compiler to stand guard for us.

## Step 2: Sealing off every copy path — `= delete`

Since the pitfall is "it can get copied behind your back", the most direct fix is to **disable every copy and move path that could break uniqueness**:

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

`= delete` is a weapon C++11 handed us: deleted functions still participate in overload resolution, so the moment anyone tries to call one, the compiler rejects it outright. This is a huge step up from comments — "no copying" is now a hard compile-time constraint.

But a new contradiction appears here: we moved the constructor into `private` so that outsiders can't just construct one — except now **nobody can construct it at all**, not even that "single instance" itself. What we're missing is a controlled entry point: one that doesn't let the outside world `new` freely, yet still gives it a way to obtain the instance.

## Step 3: Private constructor + static access point — Meyer's Singleton

Let's narrow the problem down: hand the uniqueness of construction to one entry function we control, and anyone who wants the instance must go through that entry. As for "how the entry guarantees construction happens only once", C++11 gives an answer so clean it is practically free — **a `static` local variable inside a function**:

```cpp
class GlobalOled {
public:
    static GlobalOled& get_instance() {
        static GlobalOled oled;  // initialized once, on first pass through
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

This shape of code has a name: **Meyer's Singleton** (named after Scott Meyers). Its core is the single line `static GlobalOled oled;`, but the guarantee behind that line is rock solid: since C++11, the standard explicitly states that **if multiple threads first enter this declaration at the same time, exactly one of them performs the initialization, and all the others block and wait until it completes** ([stmt.dcl], colloquially *magic statics*).

What does that mean? **Thread-safe initialization of the singleton is already guaranteed by the language — we don't have to write a single line of locking.** Don't take my word for it yet; let's verify first.

## Let's verify first: are magic statics really thread-safe

Claims need proof. Let's write a small program where 500 threads race for `get_instance()` at the same time, hang an atomic counter in the constructor, and see how many times it actually gets constructed:

```cpp
#include <atomic>
#include <thread>
#include <vector>
#include <iostream>

class MeyersSingleton {
public:
    static MeyersSingleton& instance() {
        static MeyersSingleton s;  // C++11 [stmt.dcl]: thread-safe one-time initialization
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

Compile and run (with `-O2` on, deliberately making the race hotter):

```sh
$ g++ -std=c++23 -O2 -pthread singleton_verify.cpp -o singleton_verify
$ for i in 1 2 3 4 5; do ./singleton_verify; done
construct_count = 1 (expect 1)
construct_count = 1 (expect 1)
construct_count = 1 (expect 1)
construct_count = 1 (expect 1)
construct_count = 1 (expect 1)
```

Five runs in a row, 500 concurrent threads racing each time, and `construct_count` sits firmly at 1. That is the magic statics promise: no locks, no `call_once`, no anxiety — the language has taken "initialize exactly once" off your hands. **In modern C++, the first choice for writing a singleton is Meyer's Singleton; there is no reason whatsoever to hand-write anything more complicated.**

## Pitfall warning: the DCLP relic

::: warning Pitfall ahead
If you're digging through older, pre-C++11 material, odds are you'll run into something called **DCLP (Double-Checked Locking Pattern)**. Plenty of blog posts still circulate it, and some even write it like this — **this version is broken; do not copy it**:

```cpp
// ⚠️ Anti-example: memory_order_consume is unreliable here
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

The problem sits with `memory_order_consume`. The intent of consume is "only order accesses that carry a data dependency", which sounds sufficient for DCLP — but after C++17 the standard weakened it substantially, and in practice **essentially every mainstream compiler just demotes it to acquire**. In other words, you thought you wrote the weak consume guarantee, but what runs is the strong acquire guarantee; the semantics don't match what you wrote, and portability is dreadful. Hand-writing consume is still a minefield today.
:::

If you absolutely must hand-write DCLP (and let me stress once more, **modern C++ needs nothing beyond magic statics — no hand-rolling required**), the correct memory orders are **acquire / release**:

```cpp
class DclpSingleton {
public:
    static DclpSingleton* instance() {
        auto* p = ptr_.load(std::memory_order_acquire);  // first check (lock-free)
        if (!p) {
            std::lock_guard<std::mutex> lk(mtx_);
            p = ptr_.load(std::memory_order_relaxed);    // second check (holding the lock)
            if (!p) {
                p = new DclpSingleton();
                ptr_.store(p, std::memory_order_release);  // publish
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

acquire guarantees that when a non-null pointer is read, the object it points to has been fully constructed; release guarantees that when the pointer is stored back, the object's construction is visible to other threads. Let's verify this one too — two threads racing, and do they end up with the same instance:

```sh
$ ./singleton_verify
Meyers:  construct_count = 1 (expect 1)
DCLP:    same instance = true (expect true)
```

The result looks good. But I'll nag one more time: **this DCLP code exists only so you can see what a correct version of the old approach looks like — it is not for you to use**. One `static` line in Meyer's Singleton replaces that entire DCLP blob, with no heap allocation (`new`), no raw pointers, and no destruction-order headaches. DCLP is a pre-C++11 legacy; the only reason to keep it in your head today is to recognize it when reading old code.

## In practice: a working global config reader

GlobalOled alone is a bit abstract, so let's build something genuinely usable. This `ConfigManager` is a typical singleton config reader: it loads a `key=value` config file and offers a key-based lookup that returns `std::optional` to say "this key might not exist":

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

Same recipe as before: one `static` local inside `instance()`, all four special member functions deleted, constructor private. The `std::optional` return value forces the caller to deal with "the key doesn't exist", which is far more graceful than returning an empty string or throwing. And `std::filesystem::path` gives you cross-platform paths for free.

```cpp
void ConfigManager::parse_line(const std::string& line) {
    if (line.empty() || line[0] == '#') return;  // skip empty lines / comments

    const auto eq = line.find_first_of('=');
    if (eq == std::string::npos) return;          // not a valid kv pair, skip

    std::string key = line.substr(0, eq);
    std::string val = line.substr(eq + 1);
    key.erase(key.find_last_not_of(" \t") + 1);   // rtrim key
    val.erase(0, val.find_first_not_of(" \t"));   // ltrim val
    maps_.insert({key, val});
}
```

Using it looks like this — anywhere in the program can grab that one and only instance:

```cpp
auto& config = ConfigManager::instance();
config.read_from_file("app.conf");
if (auto host = config.get_value("host")) {
    connect(*host);  // only proceed if we actually got a value
}
```

::: tip Companion compilable project
The complete code for this section (including `#` comment handling, blank-line handling, a 500-thread concurrent-read test, and a minimal `Logger` singleton) lives in this repo — clone it, run cmake once, and it just works: [Singleton](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/tree/main/code/volumn_codes/vol4/design-patterns/Singleton). One heads-up: the `GlobalConfig` demo reads `test_config.txt` from its own directory (CMake copies it into `build/` automatically), so run it with `cd build && ./ConfigManager`; for `Logger`, just `./build/Logger`.
:::

## Why singletons are disliked

At this point we have a singleton that is correct, thread-safe, and dead simple to write. But we're not done — I have to be honest with you: **the Singleton pattern actually has a rather poor reputation in software engineering**, and quite a few engineers treat it as a pattern to use sparingly, or even as an anti-pattern. Why?

**First, it tramples single responsibility.** A `ConfigManager` is both "configuration management" and "global access" at once; the moment you write `ConfigManager::instance().get_value(...)` anywhere, that global object punches through your module's interface boundary — your function used to depend on the abstraction "I can obtain a certain config value", and now it is hard-coupled to one concrete global implementation.

**Second, it makes unit testing miserable.**

```cpp
void do_work() {
    ConfigManager::instance().get_value("timeout");  // hard-wired to the global
}

void test_do_work() {
    // Want to swap in a fake ConfigManager to test edge cases? Sorry, no can do.
    do_work();
}
```

Because the instance is globally unique and hard-coded inside `instance()`, you simply cannot replace it with a mock at test time. The singleton forces every module that uses it to be tested together with the real singleton, and the premise of unit testing — "units are independent" — is broken.

**Third, it violates the Open-Closed Principle (OCP).** The day you want to go from "one global config" to "one config per tenant", you'll find that from the moment `ConfigManager` was designed as a singleton, extending it to multiple instances means refactoring a huge swath of code.

**Fourth, static singletons have lifecycle traps of their own.** If the singleton holds heavyweight resources (a large cache, file handles), it stays resident until the program ends even if you only used it briefly. More insidiously, **the destruction order of static objects is uncontrollable** — if singleton A depends on another static object B, and B is destroyed before A, then A touching B during its own destruction is undefined behavior (the famous *static deinitialization order* problem).

## Improvement: locking the singleton inside a subsystem — dependency injection

All of the ailments above share one root: **the singleton "leaks through" interfaces, forcing global state onto every caller**. A healthier approach flips the dependency around — **don't let callers reach out for the global; let the upper layer actively inject the objects they need**:

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

// inject manually at the use site
int main() {
    auto& oled = GlobalOled::get_instance();   // the singleton is confined to the main layer
    OledUpdater updater(oled);                 // updater depends only on a reference; it knows nothing global
    updater.do_work();
}
```

This flip brings three immediate benefits. `OledUpdater` no longer depends on global state — it depends on a plain `GlobalOled&`, so at test time you can casually pass in a fake implementation. The singleton's scope is squeezed down into the `main`-layer subsystem, instead of `::get_instance()` being smeared across the whole program. And when you later want multiple instances, changing the wiring code in `main` is enough — `OledUpdater` doesn't change a single line.

That is the idea of **dependency injection (DI)**. True singletons — the "exactly one in the program, no way around it" kind — are actually very rare. Most of the time, when you think you need a singleton, what you actually need is "unique within a certain subsystem", and that need is solved by injecting a reference.

## Summary

Let's trace the whole evolution path once more:

| Stage | Approach | Why it still isn't enough |
|---|---|---|
| Comment constraint | Write `// only once` | The compiler doesn't check; implicit copies slip through |
| `= delete` | Delete copy/move | Now nothing can construct the instance |
| Meyer's Singleton | Private constructor + `static` local variable | **Good enough** (C++11 magic statics guarantees thread safety) |
| Hand-written DCLP | Double-checked locking + acquire/release | Historical legacy; modern C++ doesn't need it |
| Dependency injection | Confine the singleton to a subsystem, inject references | Solves global-state pollution and testability |

Note down these key conclusions:

- **In modern C++, the first choice for writing a singleton is Meyer's Singleton** (private constructor + `static` local variable + deleted copy/move), and not one line of locking to write.
- **Do not hand-write DCLP**, and in particular do not use `memory_order_consume` — magic statics already takes care of initialization thread safety.
- The real cost of a singleton is not the implementation, but **global-state pollution, test pain, OCP violation, and static destruction order**.
- In most "I need a singleton" scenarios, what's really needed is **dependency injection** — constrain uniqueness inside one subsystem instead of letting it roam the whole program.

## References

- [cppreference: Static local variables](https://en.cppreference.com/w/cpp/language/storage_duration#Static_local_variables) (magic statics, since C++11)
- [cppreference: `std::memory_order`](https://en.cppreference.com/w/cpp/atomic/memory_order) (the semantics of acquire/release/consume)
- Scott Meyers, *Effective C++* Item 4 / Andrei Alexandrescu, *Modern C++ Design* Chapter 6 (singletons and multithreading)
- Companion compilable project: [Singleton](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/tree/main/code/volumn_codes/vol4/design-patterns/Singleton)
