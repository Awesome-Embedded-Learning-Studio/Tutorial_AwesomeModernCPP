---
title: 'Adapter Pattern: Making incompatible interfaces work together without modifying
  existing code'
description: We begin with an awkward scenario where the drawing interface expects
  lines but the driver only handles points. We then systematically derive the object
  adapter, explaining why the class adapter is discouraged, when to use a bidirectional
  adapter, and how to approach caching optimizations.
chapter: 11
order: 5
tags:
- host
- cpp-modern
- intermediate
- 适配器模式
difficulty: intermediate
platform: host
cpp_standard:
- 11
- 17
- 20
reading_time_minutes: 20
related:
- 单例模式:从注释约束到 Meyer's Singleton
prerequisites:
- 'Chapter 6: 类与对象'
translation:
  source: documents/vol4-advanced/vol4-generics-patterns/05-adapter.md
  source_hash: 9266219c2c5bb01f30410e81cd5d8a90068d5b4ea59c8d583987da832ae5fe36
  translated_at: '2026-06-24T00:55:17.834797+00:00'
  engine: anthropic
  token_count: 3498
---
# Adapter Pattern: Making Incompatible Interfaces Work Together Without Changing Old Code

## What problem are we actually solving?

Let's skip the formal definition and look at a very realistic scenario. You are working on a drawing module and have painstakingly abstracted a set of geometric shapes—`Rectangle`, `Triangle`, and `Circle`. Internally, they are all stored as a collection of `Line` segments, where a `Line` consists of two `Point2D` objects. You are comfortable with this abstraction, and all your business logic is built on top of it.

Then, a colleague working on the OLED driver walks over, hands you a header file, and says: "Our driver only knows how to draw points. The interface looks like this; just feed the data you want to draw into it." The problem is, it expects a collection of `Point` objects, not a collection of `Line` objects.

```cpp
struct Point2D {
    int x;
    int y;
};

struct Line {
    Point2D start;
    Point2D end;
};
```

Now, here is the problem: **You are working entirely with `Line` objects, but the driver only recognizes `Point`. The interfaces simply do not match.** What do we do?

The first instinct is to "change one side": either modify the driver to support drawing `Line` objects, or change your geometric abstraction to store `Point` data internally. However, in real-world engineering, both paths are usually blocked. The driver code might be a vendor-provided binary library with a header file, meaning you cannot touch the source code. Meanwhile, your geometric abstraction has a whole ecosystem built on top of it—area calculation, collision detection, serialization—so rewriting the foundation just to adapt to a driver is an unacceptable cost. **This is a classic scenario where "neither side can move, but they must work together."**

The Adapter pattern exists precisely for this situation. Its goal is not to "modify" either side, but to **insert a translation layer in the middle so that both sides retain their original interfaces while still working together**. You can think of it like a travel power adapter: you don't rip the socket out of the hotel wall, and you don't cut the plug off your appliance; you just plug in an adapter. Both sides stay unchanged, and the power flows.

Next, we will derive this translation layer step-by-step to see why it looks the way it does and where the real pitfalls lie.

## Step 1: Identify the Trio — Target / Adaptee / Adapter

Before we start, let's nail down the terminology; otherwise, "adapting whom" vs. "being adapted by whom" gets confusing. The classic GoF Adapter pattern has three fixed roles:

**Target** is the **interface expected by the business side**. This is "what I want the thing I'm calling to look like." In our story, the business code (the geometry module) expects the driver to expose a "draw a set of Lines" interface—so that expectation is the Target.

**Adaptee** is the **existing class with a mismatched interface that cannot be changed**. This is "what I actually have." The OLED driver can only draw points, so it is the Adaptee.

**Adapter** is the **intermediate layer we are writing in this section**. It implements the Target interface externally while holding an instance of the Adaptee internally, translating Target calls into calls the Adaptee understands.

The relationship between the three is essentially: the business code talks only to the Target. The Adapter pretends to be the Target, but behind the scenes, it delegates every call to the Adaptee. The business code never knows the Adaptee exists, which is the greatest value of the Adapter—**encapsulating the "interface incompatibility" within a single class without polluting either side.**

## Step 2: Reveal the Adaptee — The OLED Driver Only Draws Points

Let's make the Adaptee concrete so we have a basis for adaptation later. The driver exposes only one interface, `draw_points`, which accepts a pair of iterators `[begin, end)` and draws each `Point` to the screen using `set_pixel`:

```cpp
class OledDriver {
public:
    // Adaptee 侧的接口:只认 Point,不认 Line
    using ConstIter = std::vector<Point2D>::const_iterator;

    void draw_points(ConstIter begin, ConstIter end) {
        for (auto it = begin; it != end; ++it) {
            set_pixel(it->x, it->y);
        }
    }

private:
    void set_pixel(int x, int y) {
        ++pixels_drawn_;  // 这里用计数模拟"画了一个点"
    }

public:
    int pixels_drawn_ = 0;
};
```

Next, let's define the geometry for the business logic side — the `Rectangle` class uses four `Line` objects to describe its edges:

```cpp
class Rectangle {
public:
    Rectangle(Point2D left_top, int width, int height) {
        Point2D rt{left_top.x + width, left_top.y};
        Point2D lb{left_top.x, left_top.y + height};
        Point2D rb{left_top.x + width, left_top.y + height};
        lines_ = { {left_top, rt}, {rt, rb}, {rb, lb}, {lb, left_top} };
    }
    const std::vector<Line>& lines() const { return lines_; }

private:
    std::vector<Line> lines_;
};
```

At this point, the conflict is laid out on the table: `Rectangle` gives us `lines()`, which returns a `vector<Line>`, while the driver expects an iterator range of `vector<Point2D>`. A `Line` has two endpoints, so four lines mean eight points, separated by a "line-to-point" translation gap. This is a situation we often encounter.

## Step 3: Object Adapter — Hold an Adaptee, Implement a Target

What we need to do now is write an adapter that accepts the `Line` collection from the business side, flattens it internally into a bunch of `Point2D`s, and exposes an iterator range that "looks like what the driver wants." GoF calls this pattern an **Object Adapter**, because it holds the data being adapted via **composition**:

```cpp
class LineToPointsAdapter {
public:
    using ConstIter = std::vector<Point2D>::const_iterator;

    explicit LineToPointsAdapter(const std::vector<Line>& lines) {
        points_.reserve(lines.size() * 2);
        for (const auto& l : lines) {
            points_.push_back(l.start);
            points_.push_back(l.end);
        }
    }

    // 对外暴露的"Target 接口":一对迭代器,正好喂给 OledDriver
    std::pair<ConstIter, ConstIter> points() const {
        return {points_.begin(), points_.end()};
    }

private:
    std::vector<Point2D> points_;
};
```

You see, this adapter does something quite simple: upon construction, it splits each incoming `Line` into two endpoints and stuffs them into its own `points_` member; externally, it provides a `points()` method that returns an iterator range for these points. It acts as a "translator," converting the semantics of "lines" into the semantics of "points."

Using it is almost transparent—the application code doesn't need to know the driver exists; we just need to pass the adapter to the driver:

```cpp
int main() {
    Rectangle rect({0, 0}, 10, 5);
    LineToPointsAdapter adapter(rect.lines());

    OledDriver driver;
    auto [begin, end] = adapter.points();
    driver.draw_points(begin, end);

    std::cout << "pixels_drawn = " << driver.pixels_drawn_ << " (expect 8)\n";
}
```

Let's verify this and compile a build:

```sh
$ g++ -std=c++23 -O2 -Wall -Wextra adapter_verify.cpp -o adapter_verify
$ ./adapter_verify
pixels_drawn = 8 (expect 8)
```

Four sides, two endpoints per side, exactly eight points. The adapter has done its job—**neither `Rectangle` nor `OledDriver` was modified, yet they successfully collaborated to draw a figure.**

At this point, you might ask a very reasonable question: `LineToPointsAdapter` expands all the lines into points and stores them upon construction. Doesn't that seem a bit "eager"? Yes, this is the most straightforward implementation. **It performs the full translation at object construction.** The benefit is that subsequent access is just a normal memory traversal with no additional overhead; the downside is that if the `lines` in the business logic change later, the `points_` inside the adapter become stale. We will discuss this pitfall later.

## Step 4: Verifying Transparency — The Target/Adaptee/Adapter Trio

Talk is cheap. Let's lock down the claim that "the business code is completely unaware of the Adaptee's existence" with code. Here is a more classic example: our business logic has a `Printer` abstraction (the Target), which expects a `print(string)` interface. However, we only have an old `LegacyLogger` (the Adaptee) on hand, whose signature is `write_line(const char*)`—neither the parameter types nor the function names match.

```cpp
// Target:业务期望的接口
class Printer {
public:
    virtual ~Printer() = default;
    virtual void print(const std::string& msg) = 0;
};

// Adaptee:旧类,签名不兼容,而且改不了
class LegacyLogger {
public:
    void write_line(const char* content) {
        std::cout << "[legacy] " << content << "\n";
    }
};

// 对象适配器:实现 Target,内部持有 Adaptee
class LoggerAdapter : public Printer {
public:
    explicit LoggerAdapter(std::unique_ptr<LegacyLogger> adaptee)
        : adaptee_(std::move(adaptee)) {}

    void print(const std::string& msg) override {
        adaptee_->write_line(msg.c_str());  // 翻译:std::string -> const char*
    }

private:
    std::unique_ptr<LegacyLogger> adaptee_;
};

// 业务代码:只依赖 Target 抽象,完全不知道 LegacyLogger 存在
void greet(Printer& p) {
    p.print("hello from adapter");
}
```

The `greet` function only knows about `Printer&`; it is completely unaware that a `LegacyLogger` is hidden behind that `Printer`. This is the direct benefit of the adapter pattern encapsulating "interface incompatibility"—**the business logic depends on a clean abstraction, while the adaptation details are locked inside the `LoggerAdapter` class**. Let's compile and verify this:

```sh
$ g++ -std=c++23 -O2 -Wall -Wextra adapter_verify.cpp -o adapter_verify
$ ./adapter_verify
[legacy] hello from adapter
```

The business function says "hello from adapter", which the adapter translates, and the old logger outputs verbatim. What the adapter does is translate "say something" into "write a line".

## Class Adapter: Why the private inheritance version is not recommended

In the original GoF book, the adapter actually has two faces. The "compose an Adaptee" approach shown above is called an **object adapter**, while the other is called a **class adapter**, implemented via **private inheritance of Adaptee + public inheritance of Target**:

```cpp
// 类适配器:私有继承拿实现,公有继承满足接口
class ClassAdapter : private LegacyLogger, public Printer {
public:
    void print(const std::string& msg) override {
        write_line(msg.c_str());  // 直接复用 Adaptee 的成员
    }
};
```

The semantics of private inheritance here are "implemented in terms of"—the Adapter wants to borrow the implementation from `LegacyLogger`, but it does not want to expose an "is-a" relationship. Therefore, it uses `private` inheritance to hide the inheritance relationship from the outside world, keeping `write_line` for its own internal use. It compiles and runs, and this was indeed a common pattern in the C++ examples from the Gang of Four (GoF) back in the day.

To be honest, in modern C++, I would hardly ever write code like this, for several reasons. **First, the class adapter hard-codes the Adaptee into the inheritance chain**—you can only decide who to adapt to at compile time; swapping the Adaptee at runtime is impossible, whereas an object adapter holds a pointer/reference, making runtime implementation swaps trivial. **Second, the class adapter requires that you inherit from the Adaptee**. If the Adaptee is `final`, or if its interface consists of non-virtual free functions (common in C-style third-party libraries), the path of private inheritance is a dead end. An object adapter only needs to "hold an object or a reference" to work, making it applicable in a much wider range of scenarios. **Third, once you start using multiple inheritance heavily, code coupling and readability suffer**—composition is almost always more flexible and yields fewer surprises than inheritance in C++.

So, take note: **In modern C++, the object adapter (composition) is the default choice. The class adapter (private inheritance) should only be considered in narrow scenarios where the Adaptee must be used as a base class and the implementation does not change at runtime.** The principle of "favor composition over inheritance" holds true for the adapter pattern as well.

## Bidirectional Adapters: Both Sides Need to Use Each Other's Interfaces

We aren't done yet. All previous examples demonstrated "unidirectional adaptation"—the business side produces `Line`, the driver side consumes `Point`, and data flows in one direction. However, in real-world systems, you will encounter a trickier situation: **both subsystems cannot be modified, and they both need to consume each other's data structures.**

Let's look at a concrete scenario. Besides "drawing," our geometry module has now integrated a **geometry calculation engine**. The interface for this engine requires `Line` objects to calculate lengths, intersections, and areas:

```cpp
class GeometryEngine {
public:
    // 这个引擎要的是 Line
    double total_length(std::vector<Line>::const_iterator begin,
                        std::vector<Line>::const_iterator end);
};
```

Here comes the tricky part: sometimes this geometry engine receives a set of `Point`s from elsewhere (for example, a batch of points read from a sensor), and it needs to reconstruct them into `Line`s to perform calculations. Conversely, the OLED driver sometimes receives a set of `Line`s and needs to expand them into `Point`s to render them. In other words, **translation is required in both directions (`Line -> Point` and `Point -> Line`)**.

In scenarios with this "bidirectional dependency," a unidirectional adapter is insufficient. We need a **bidirectional adapter**: it holds two sets of data internally (`points` and `lines`) and exposes interfaces for accessing both directions simultaneously. If we construct it from `Line`s, it automatically expands the `Point`s for us. Conversely, if we construct it from `Point`s, it automatically pairs the `Line`s for us:

```cpp
class BidirectionalAdapter {
public:
    // 方向一:从 Line 进来,顺带展开成 Point
    explicit BidirectionalAdapter(std::vector<Line> lines)
        : lines_(std::move(lines)) {
        points_.reserve(lines_.size() * 2);
        for (const auto& l : lines_) {
            points_.push_back(l.start);
            points_.push_back(l.end);
        }
    }

    // 对外两个方向都能取:要 Line 给 Line,要 Point 给 Point
    const std::vector<Line>& lines() const { return lines_; }
    const std::vector<Point2D>& points() const { return points_; }

private:
    std::vector<Line> lines_;
    std::vector<Point2D> points_;
};
```

Here, I have intentionally implemented only the direction "construct from `Line`". This is because the semantics of `Line -> Point` are deterministic—a line has two endpoints, so we just need to unwrap it. However, the reverse semantics of `Point -> Line` are actually **not unique**: four points can form two lines, connect end-to-end to form four lines, or even represent two independent line segments. The exact matching logic depends entirely on business conventions, so the conversion logic for `Point -> Line` in a bidirectional adapter must be hardcoded by you based on the specific scenario; there is no "universal answer."

Let's verify this bidirectional adapter using a rectangle (four lines, eight points):

```sh
$ g++ -std=c++23 -O2 -Wall -Wextra bidi_cache_verify.cpp -o bidi_cache_verify
$ ./bidi_cache_verify
bidi points = 8 (expect 8)
bidi lines  = 4 (expect 4)
```

Construct once, and we have data ready for both directions: give the OLED driver `points()` if it asks for points, and give the geometry engine `lines()` if it asks for lines. One adapter serving two subsystems simultaneously is the core value of a bidirectional adapter. Of course, the cost is obvious: **it must maintain two copies of data internally, doubling memory usage**. Furthermore, if the data changes, we must synchronize updates to both copies, or simply mark one copy as "lazily expanded." So, don't jump straight to a bidirectional adapter; **only when both directions are actually consumed is it worth the extra complexity.**

## Cache Optimization: What to do when redrawing the same set of lines

Next, we face a new problem. Imagine the screen refreshes dozens of times per second, and your `Rectangle` is drawn every frame. If we use the previous version of `LineToPointsAdapter`, we construct a new adapter every frame and re-expand the same four lines into eight points—**this expansion action will be repeated hundreds or thousands of times, even though the input hasn't changed.**

This is a typical "cacheable transformation." In an era where memory is becoming increasingly cheap, **trading space for time** is a cost-effective strategy: we maintain a cache of "source data -> expanded result." When expanding for the first time, we record the result. Subsequently, if we detect that the source data hasn't changed, we return the cached result directly, skipping the expansion. This shares the same logic as HTTP request caching or CPU instruction caching—**as long as the transformation has a cost and the input repeats, it is worth caching.**

The key to implementation is determining whether the "input is the same." The most straightforward method is to use the source data's address (or identity) as the key:

```cpp
class CachedLineToPointsAdapter {
public:
    explicit CachedLineToPointsAdapter(std::vector<Line>* key) : key_(key) {}

    const std::vector<Point2D>& get_points() {
        auto found = cache_.find(key_);
        if (found != cache_.end()) {
            return found->second;  // 命中缓存,跳过展开
        }
        // 未命中:第一次展开,并存入缓存
        ++expand_calls_;
        std::vector<Point2D> pts;
        pts.reserve(key_->size() * 2);
        for (const auto& l : *key_) {
            pts.push_back(l.start);
            pts.push_back(l.end);
        }
        return cache_[key_] = std::move(pts);
    }

    static std::size_t expand_calls_;  // 统计真实展开次数(演示用)

private:
    std::vector<Line>* key_;
    static inline std::unordered_map<std::vector<Line>*, std::vector<Point2D>>
        cache_;
};
std::size_t CachedLineToPointsAdapter::expand_calls_ = 0;
```

Let's draw five frames in a row to see exactly how many times the expansion is triggered:

```sh
$ ./bidi_cache_verify
expand_calls = 1 (expect 1)
```

Five requests were made. Expansion occurred only once, while the remaining four hits were served from the cache. This is the direct benefit of cache optimization. **Of course, using a raw pointer as a key has a prerequisite—the lifetime of the source data object must exceed that of the cache**. Otherwise, if the memory address is reused, the cache will become corrupted. In real-world engineering, a more robust approach is to use a hash of the object's content (for example, feeding the coordinates of each `Line` into a hash function) as the key. The trade-off is that the hash must be recalculated for every cache lookup. The specific balance depends on your data scale and mutation frequency; this falls under engineering trade-offs rather than the pattern itself, so we will stop here.

::: warning Pitfall Warning
In a caching adapter, the easiest way to fail isn't "cache hit rates," but **cache invalidation**. Once you cache the expansion result, if the source data is subsequently modified, your cache will return stale points. The implementation above, which uses a pointer as a key, is completely oblivious to changes in the content of `*key_`—if someone modifies the coordinates inside the `Rectangle`, the eight points in the cache remain old, and the screen will draw misaligned shapes. **For any adapter with a cache, you must clearly determine "when the source data changes and how to invalidate the cache afterward." If you don't figure this out, it will eventually blow up in production.**
:::

## The Object Adapter Lifecycle Trap: Copy-on-Construction vs. Holding a Reference

Let's shift our focus from the cache back to the adapter itself to discuss another very common pitfall. The constructor for the `LineToPointsAdapter` we saw earlier looks like this:

```cpp
explicit LineToPointsAdapter(const std::vector<Line>& lines) {
    points_.reserve(lines.size() * 2);
    for (const auto& l : lines) {
        points_.push_back(l.start);
        points_.push_back(l.end);
    }
}
```

Note that it receives the data **by `const&`**, and then **copies the content** into `points_` inside the constructor. This choice is safe—the adapter holds its own copy, decoupling the adapter's lifetime from the source data. Even if the source data is destroyed, the adapter remains unaffected. The trade-off is a full copy during construction.

However, sometimes you might want to cut corners and think, "I'm only using this temporarily, so copying is wasteful. Why not just hold a reference instead?":

```cpp
// ⚠️ 危险:持有引用,源数据失效后引用悬垂
class RefAdapter {
public:
    explicit RefAdapter(const std::vector<Line>& lines) : lines_(lines) {}
    // ...
private:
    const std::vector<Line>& lines_;  // 悬垂引用高发区
};
```

This code compiles and runs fine most of the time—until one day, someone feeds a temporary object (like a function-returned `vector` or a source moved by `std::move`) into `RefAdapter`. The reference instantly dangles, and you are left with a wild pointer. **An adapter holding a reference imposes the implicit constraint of "source data lifetime" on every caller, and the C++ compiler cannot check this.** My advice is: **default to the safe path of "copy on construction"; only consider holding a reference when you can explicitly guarantee via documentation or the type system that the source data outlives the adapter (for example, if the source data is a long-lived singleton).** This is the same class of issue discussed in the Singleton chapter regarding "global state penetrating interfaces"—hiding lifetime constraints in comments guarantees that someone will eventually step on them.

## Boundaries Between Adapters and Their Look-alikes

By now, you have likely realized that the Adapter pattern is quite "primitive"—it does not invent new mechanisms but simply connects two incompatible interfaces. However, precisely because it is primitive, it is easily confused with other structural patterns. Let's clarify a few boundaries:

**Adapter vs. Bridge.** An Adapter is a post-hoc patch—you have two existing classes with incompatible interfaces that you cannot change, so you must insert a translation layer between them. A Bridge is a proactive design—from the start, you separate "abstraction" and "implementation" into two independent inheritance hierarchies, allowing them to evolve and combine freely. In other words, **an Adapter solves "they already don't fit," while a Bridge solves "preventing them from not fitting."**

**Adapter vs. Decorator.** A Decorator **does not change the interface**; it implements the exact same interface as the decorated object, simply adding new behavior before or after the call (logging, caching, permission checks). An Adapter **changes the interface**—its external interface differs from the object it holds internally, and its job is translation. **A Decorator is "same interface, extra features"; an Adapter is "different interface, translation."**

**Adapter vs. Facade.** A Facade **simplifies** a complex subsystem by providing a new, easier-to-use entry point—it is typically one-to-many, consolidating a dozen subsystem classes into a clean interface. An Adapter is one-to-one, **converting** an existing interface into another shape. **A Facade is "subtraction"; an Adapter is "conversion."**

Remember these three boundaries. When you receive a requirement, you can quickly judge: do I need translation (Adapter), extra features (Decorator), simplification (Facade), or decoupling dimensions (Bridge)? They look similar but serve entirely different intents.

## The Cost of the Adapter Pattern

Finally, let's honestly discuss the costs. The biggest advantage of the Adapter pattern is that **it adheres to the Open-Closed Principle**—you don't touch old code, just add a new class to make two incompatible systems work together. This is a lifesaver for "untouchable" legacy systems (vendor drivers, third-party libraries, cross-team interfaces). It also makes previously unreusable code usable again, confining the cost within a single class without polluting the business side.

But the costs are real. **First, it adds a layer of indirection**—every call must pass through the adapter, theoretically adding the overhead of an extra function call. While negligible in practice, it is worth noting in high-frequency paths (like a per-frame rendering loop). **Second, it can mask real complexity**—especially with bidirectional adapters or cached adapters, internal state accumulates, making debugging harder because you have to look through the adapter's "translation" layer to understand what actually happened. **Third, adapters proliferate**—if you write an adapter for every pair of incompatible interfaces, you will eventually have "adapters everywhere." At that point, the real signal is "your abstraction design is flawed," not "write more adapters."

So, use the Adapter pattern in moderation: **it is a specific remedy for "incompatible interfaces that cannot be changed," not a cover-up for "poorly designed interfaces."** The truly healthy approach is to design interfaces consistently from the source. Adapters should only be deployed when you genuinely cannot control one side—such as integrating third-party libraries, legacy code, or cross-language bindings.

## Summary

Let's review the full trajectory of the Adapter pattern:

| Phase | Approach | Why it's needed / Why it falls short |
|---|---|---|
| Object Adapter | Compose an Adaptee, implement Target interface | Default choice, runtime swappable, no inheritance required |
| Class Adapter | Private inherit Adaptee + Public inherit Target | Hardwired at compile time, requires inheritance, not recommended in modern C++ |
| Bidirectional Adapter | Maintain two datasets internally, export both ways | Only use when both sides consume each other's interface, doubles memory |
| Caching Optimization | Memoize conversion results by key, skip duplicate conversion | Saves time on high-frequency repeated conversions, but must handle cache invalidation |

Keep these key conclusions in mind:

- **The Adapter pattern solves the post-hoc problem of "incompatible interfaces that cannot be changed,"** not proactive interface design; the latter belongs to the Bridge pattern.
- **In modern C++, default to the Object Adapter (composition)**. It allows runtime swapping of implementations, doesn't require inheriting from Adaptee, and has the widest applicability; the Class Adapter (private inheritance) can almost always be replaced by composition.
- **Bidirectional Adapters are only worth it when both directions are actually consumed.** They double memory usage and add internal state; don't jump to use them immediately.
- **Caching optimization is a double-edged sword**: saving time的前提 is you have figured out "when source data changes and how to invalidate the cache," otherwise it is a ticking time bomb.
- **The Adapter pattern is a remedy for "untouchable legacy code,"** not a cover for "messy interface design"; if interfaces don't match, the root cause should be fixed.

::: tip Compilable Companion Project
The examples for this section are in the repository `code/volumn_codes/vol4/design-patterns/Adapter/` as a complete compilable project (`.h` + main + `CMakeLists.txt`). Run `cmake -S . -B build && cmake --build build` to see the outputs shown above.
:::

## References

- [cppreference:`std::unique_ptr`](https://en.cppreference.com/w/cpp/memory/unique_ptr) (The preferred way for an Object Adapter to hold an Adaptee, since C++11)
- [cppreference:`std::unordered_map`](https://en.cppreference.com/w/cpp/container/unordered_map) (Key->result mapping for caching adapters)
- Erich Gamma, et al., *Design Patterns: Elements of Reusable Object-Oriented Software*, Chapter 4 (The original GoF source for the Adapter pattern, Object Adapter vs. Class Adapter)
- Fedor G. Pikus, *C++20 Design Patterns* (Original inspiration for the geometric shape `Line`/`Point` adaptation scenario)
