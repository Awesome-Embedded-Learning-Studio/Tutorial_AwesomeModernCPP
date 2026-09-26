---
title: 'Adapter Pattern: Getting Two Sides Talking Without Touching the Old Code'
description: 'Start from the awkward scene where your shapes are made of lines but the driver only plots points, work step by step toward the object adapter, and along the way settle why the class adapter is not recommended, when a bidirectional adapter earns its keep, and how to think about cache optimization.'
chapter: 11
order: 5
tags:
  - host
  - cpp-modern
  - intermediate
  - 适配器模式
difficulty: intermediate
platform: host
cpp_standard: [11, 17, 20]
reading_time_minutes: 20
related:
  - 'Singleton Pattern: From Comment-Only Constraints to Meyer''s Singleton'
prerequisites:
  - Classes and Object-Oriented Programming
translation:
  source: documents/vol4-advanced/vol4-generics-patterns/05-adapter.md
  source_hash: 9266219c2c5bb01f30410e81cd5d8a90068d5b4ea59c8d583987da832ae5fe36
  translated_at: '2026-09-26T05:12:18+00:00'
  engine: anthropic
  token_count: 5800
---

# Adapter Pattern: Getting Two Sides Talking Without Touching the Old Code

## What Problem Are We Actually Solving

Let's skip the definition for now and look at a painfully real scenario. You are building a drawing module. You have painstakingly abstracted a set of geometric shapes — `Rectangle`, `Triangle`, `Circle` — each of which stores itself internally as a set of `Line` segments, and each `Line` consists of two `Point2D`s. You are fluent with this abstraction; all your business logic is built on top of it.

Then the colleague who owns the OLED driver walks over, hands you a header file, and says: "Our driver only knows how to plot points; here is what the interface looks like — just feed in whatever you want drawn." What it wants is a set of `Point`s, not a set of `Line`s.

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

Now the problem is on the table: **your side is all `Line`, the driver side only understands `Point`, and the two interfaces simply do not match.** What now?

The first instinct is to "change one side": either modify the driver so it supports drawing `Line`s, or modify your own geometric abstraction so shapes store `Point`s internally. In a real project, though, both roads are usually blocked — the driver may well be a vendor-supplied binary library plus a header file, so you cannot touch the source at all; meanwhile your geometric abstraction has a whole suite of functionality hanging off it (area computation, collision detection, serialization), and tearing up the foundation to rewrite it just to accommodate one driver is an unacceptable cost. **A classic "neither side can move, yet they must cooperate" situation.**

The Adapter pattern was born for exactly this situation. Its goal is not to "modify" either side, but to **slip a layer of translation into the middle so that both sides' interfaces stay untouched while still working together**. You can think of it as one of those two-prong-to-three-prong power adapters: you would not pry the socket off your dorm wall, nor would you snip a pin off your three-prong appliance — you just buy an adapter, both sides stay as they are, and the electricity flows.

In the rest of this article we will corner ourselves into that translation layer step by step, see why each step looks the way it does, and where the real traps are hiding.

## Step One: Pin Down the Trio — Target / Adaptee / Adapter

Before writing any code, let's nail down the terminology — otherwise "who adapts to whom" will get confusing fast. The original GoF Adapter pattern has three fixed roles:

**Target** is **the interface the business side wishes for** — "what the thing I want to call looks like". In our story, the business code (the geometry module) wishes the driver exposed a "draw a set of Lines" interface — that wish is the Target.

**Adaptee** is **the existing class whose interface does not match but which you cannot change** — "what I actually have in hand". The OLED driver only plots points, so it is the Adaptee.

**Adapter** is **the middle layer we are going to write in this chapter**. Outwardly it implements the Target's interface; inwardly it holds an Adaptee and translates each Target call into a call the Adaptee can understand.

The relationship among the three boils down to this: the business code talks only to the Target; the Adapter pretends to be the Target while secretly forwarding every call to the Adaptee. From beginning to end the business code has no idea the Adaptee exists — and that is the Adapter pattern's greatest value: **encapsulating "interface incompatibility" inside a single class, polluting neither side**.

## Step Two: Lay Out the Adaptee — the OLED Driver Only Plots Points

First let's write the Adaptee down concretely, so the adaptation later has something to stand on. The driver exposes exactly one interface, `draw_points`: it takes a pair of iterators `[begin, end)` and plots each `Point` onto the screen with `set_pixel`:

```cpp
class OledDriver {
public:
    // The Adaptee-side interface: it understands Point, not Line
    using ConstIter = std::vector<Point2D>::const_iterator;

    void draw_points(ConstIter begin, ConstIter end) {
        for (auto it = begin; it != end; ++it) {
            set_pixel(it->x, it->y);
        }
    }

private:
    void set_pixel(int x, int y) {
        ++pixels_drawn_;  // simulate "a point was plotted" with a counter
    }

public:
    int pixels_drawn_ = 0;
};
```

Next let's lay out the business-side geometry too — a `Rectangle` describes its edges with four `Line`s:

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

At this point the conflict is out on the table: `Rectangle` hands you `lines()`, returning a `vector<Line>`; the driver wants an iterator range over `vector<Point2D>`. One `Line` has two endpoints, four lines make eight endpoints, and between them yawns a "line-to-point" translation gap. Your author runs into this kind of situation all the time.

## Step Three: The Object Adapter — Hold an Adaptee, Implement a Target

What we are about to do is write an adapter that catches the business side's collection of `Line`s, expands it internally into a bunch of `Point2D`s, and then exposes an iterator range "the way the driver wants it". GoF calls this style the **object adapter**, because it holds the adapted data by **composition**:

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

    // The exposed "Target interface": a pair of iterators, exactly what OledDriver wants to eat
    std::pair<ConstIter, ConstIter> points() const {
        return {points_.begin(), points_.end()};
    }

private:
    std::vector<Point2D> points_;
};
```

As you can see, what this adapter does is utterly plain: at construction it splits each incoming `Line` into its two endpoints and stuffs them into its own `points_`; outwardly it offers a `points()` returning an iterator range over those points. It is a "translation machine", converting "line" semantics into "point" semantics.

Using it is nearly transparent — the business code never needs to know the driver exists; you just hand the adapter over to the driver:

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

Let's verify: compile and run it.

```sh
$ g++ -std=c++23 -O2 -Wall -Wextra adapter_verify.cpp -o adapter_verify
$ ./adapter_verify
pixels_drawn = 8 (expect 8)
```

Four edges, two endpoints each, exactly eight points. The adapter did its job — **neither `Rectangle` nor `OledDriver` was changed, yet they successfully cooperated to draw the picture.**

Here you might ask a perfectly reasonable question: `LineToPointsAdapter` expands all the lines into points and stores them right at construction — isn't that a bit eager? Yes, and this is the most straightforward implementation: **finish the entire translation at object construction time**. The upside is that every later access is a plain memory walk with no extra overhead; the downside is that if the business side's `lines` changes afterwards, this copy of `points_` inside the adapter goes stale. We will come back to this trap specifically later.

## Step Four: Let's Verify It Here — the Transparency of the Target/Adaptee/Adapter Trio

Talk is cheap, so let's nail down "the business code knows nothing of the Adaptee" in code. Here is a more classic example: on the business side there is a `Printer` abstraction (that is the Target) that expects a `print(string)` interface; but all we have on hand is an old `LegacyLogger` (the Adaptee) whose signature is `write_line(const char*)` — neither the parameter type nor the function name matches.

```cpp
// Target: the interface the business side wishes for
class Printer {
public:
    virtual ~Printer() = default;
    virtual void print(const std::string& msg) = 0;
};

// Adaptee: an old class with an incompatible signature, and one we cannot change
class LegacyLogger {
public:
    void write_line(const char* content) {
        std::cout << "[legacy] " << content << "\n";
    }
};

// Object adapter: implements the Target, holds the Adaptee inside
class LoggerAdapter : public Printer {
public:
    explicit LoggerAdapter(std::unique_ptr<LegacyLogger> adaptee)
        : adaptee_(std::move(adaptee)) {}

    void print(const std::string& msg) override {
        adaptee_->write_line(msg.c_str());  // translation: std::string -> const char*
    }

private:
    std::unique_ptr<LegacyLogger> adaptee_;
};

// Business code: depends only on the Target abstraction, with no idea LegacyLogger exists
void greet(Printer& p) {
    p.print("hello from adapter");
}
```

The function `greet` knows only `Printer&`; it does not even know that a `LegacyLogger` is hiding behind that `Printer`. This is the direct payoff of the Adapter pattern encapsulating "interface incompatibility": **the business side depends on a clean abstraction, and the adaptation details are locked inside the single class `LoggerAdapter`**. Let's verify by compiling:

```sh
$ g++ -std=c++23 -O2 -Wall -Wextra adapter_verify.cpp -o adapter_verify
$ ./adapter_verify
[legacy] hello from adapter
```

The business function's single "hello from adapter" comes right back out of the old logger, translated on the way by the adapter. What the adapter does here is translate "say something" into "write a line".

## The Class Adapter: Why the Private-Inheritance Version Is Not Recommended

In the original GoF text the adapter actually has two faces. The "compose an Adaptee" style above is the **object adapter**; the other face is the **class adapter**, written as **privately inherit the Adaptee + publicly inherit the Target**:

```cpp
// Class adapter: private inheritance to grab the implementation, public inheritance to satisfy the interface
class ClassAdapter : private LegacyLogger, public Printer {
public:
    void print(const std::string& msg) override {
        write_line(msg.c_str());  // reuse the Adaptee's member directly
    }
};
```

The semantics of private inheritance here are "implemented in terms of" — the Adapter wants to borrow `LegacyLogger`'s implementation without exposing an is-a relationship, so it uses `private` inheritance to switch the inheritance relationship off to the outside world, keeping `write_line` for its own internal use. It does compile and run, and GoF's C++ examples back in the day often used this style.

Honestly, in modern C++ I would almost never write it this way, for several reasons. **First, the class adapter hard-wires the Adaptee into the inheritance chain** — you can only decide whom to adapt at compile time; swapping in a different Adaptee at runtime is impossible, whereas the object adapter holds a pointer/reference in hand, and swapping the implementation at runtime is trivial. **Second, the class adapter requires that you be able to inherit from the Adaptee** — if the Adaptee is `final`, or its interface is non-virtual free functions to begin with (common in C-style third-party libraries), private inheritance is a dead end; the object adapter works as long as it can "hold an object or a reference", a far wider applicability. **Third, once multiple inheritance piles up, both coupling and readability suffer** — in C++, composition is almost always more flexible and full of fewer surprises than inheritance.

So remember one thing: **in modern C++, the object adapter (composition) is the default choice; the class adapter (private inheritance) deserves consideration only in the narrow scenario where the Adaptee genuinely must be used as a base class and the implementation will not be swapped at runtime.** "Prefer composition over inheritance wherever you can" holds for adapters just as much as anywhere else.

## The Bidirectional Adapter: When Both Sides Need to Use Each Other's Interface

The story does not end there. All the examples so far were "one-way adaptation" — the business side produces `Line`, the driver side consumes `Point`, and the data flows in a single direction. But in real systems you will meet a more painful situation: **two subsystems, neither of which you can change, and each of which needs to work with the other's data structures.**

Here is a concrete scenario. Besides "drawing", our geometry module has also been hooked up to a **geometry computation engine**, whose interface eats `Line`s to compute lengths, intersections, and areas:

```cpp
class GeometryEngine {
public:
    // This engine wants Lines
    double total_length(std::vector<Line>::const_iterator begin,
                        std::vector<Line>::const_iterator end);
};
```

Now the awkward part arrives: this geometry engine sometimes receives a set of `Point`s from elsewhere (say, a bunch of points read back from some sensor), and it has to turn those points back into `Line`s before it can compute; meanwhile the OLED driver side sometimes ends up holding a set of `Line`s that need expanding into `Point`s before it can draw. In other words, **both directions (`Line -> Point` and `Point -> Line`) need translation**.

In this "mutual dependency" scenario a one-way adapter no longer suffices; we need a **bidirectional adapter**: internally it holds both copies of the data (`points` and `lines`), and outwardly it offers access interfaces in both directions at once. Construct it from `Line`s and it expands the `Point`s for you as a bonus; construct it from `Point`s the other way round, and it pairs up the `Line`s for you:

```cpp
class BidirectionalAdapter {
public:
    // Direction one: Lines come in, Points get expanded on the side
    explicit BidirectionalAdapter(std::vector<Line> lines)
        : lines_(std::move(lines)) {
        points_.reserve(lines_.size() * 2);
        for (const auto& l : lines_) {
            points_.push_back(l.start);
            points_.push_back(l.end);
        }
    }

    // Both directions are served: Lines for those who want Lines, Points for those who want Points
    const std::vector<Line>& lines() const { return lines_; }
    const std::vector<Point2D>& points() const { return points_; }

private:
    std::vector<Line> lines_;
    std::vector<Point2D> points_;
};
```

I deliberately wrote only the "construct from `Line`" direction here, because the semantics of `Line -> Point` are unambiguous — one line, two endpoints, expand and done. But the reverse, `Point -> Line`, is genuinely **not unique**: four points can be paired into two lines, or chained head-to-tail into four lines, or even treated as two independent segments. Which pairing is right depends entirely on the business contract, so in a bidirectional adapter the `Point -> Line` conversion logic must be pinned down by you for the concrete scenario — there is no "universal answer".

Let's verify the bidirectional adapter with a rectangle (four lines, eight points):

```sh
$ g++ -std=c++23 -O2 -Wall -Wextra bidi_cache_verify.cpp -o bidi_cache_verify
$ ./bidi_cache_verify
bidi points = 8 (expect 8)
bidi lines  = 4 (expect 4)
```

Construct it once, and the data for both directions is ready — hand out points when the OLED driver asks for `points()`, hand out lines when the geometry engine asks for `lines()`. One adapter serving two subsystems at once: that is the core value of the bidirectional adapter. The cost is equally obvious: **it maintains two copies of the data internally, doubling memory**, and if the data can change you must keep both copies in sync, or simply mark one of them as "lazily expanded". So do not reach for a bidirectional adapter right off the bat — **only when both directions are genuinely consumed is it worth this extra complexity**.

## Cache Optimization: What to Do When the Same Lines Are Drawn Over and Over

Now the next problem arrives. Imagine the screen refreshes dozens of times per second, and your `Rectangle` gets drawn every frame. With our earlier `LineToPointsAdapter`, every frame constructs a new adapter and re-expands the same four lines into eight points — **this expansion runs hundreds or thousands of times over, while the input has not changed at all**.

This is a textbook "cacheable conversion". With memory getting cheaper by the year, **trading space for time** is a good deal: we maintain a "source data -> expanded result" cache, record the result on the first expansion, and on later calls, seeing that the source data has not changed, return the cached result directly and skip the expansion. It is the same idea as HTTP request caching or the CPU's instruction cache — **whenever a conversion has a cost and the input repeats, caching pays off**.

The crux of the implementation is deciding "is this the same input as before". The most straightforward approach is to use the source data's address (its identity) as the key:

```cpp
class CachedLineToPointsAdapter {
public:
    explicit CachedLineToPointsAdapter(std::vector<Line>* key) : key_(key) {}

    const std::vector<Point2D>& get_points() {
        auto found = cache_.find(key_);
        if (found != cache_.end()) {
            return found->second;  // cache hit, skip the expansion
        }
        // Miss: first expansion, store into the cache
        ++expand_calls_;
        std::vector<Point2D> pts;
        pts.reserve(key_->size() * 2);
        for (const auto& l : *key_) {
            pts.push_back(l.start);
            pts.push_back(l.end);
        }
        return cache_[key_] = std::move(pts);
    }

    static std::size_t expand_calls_;  // counts the real expansions (for the demo)

private:
    std::vector<Line>* key_;
    static inline std::unordered_map<std::vector<Line>*, std::vector<Point2D>>
        cache_;
};
std::size_t CachedLineToPointsAdapter::expand_calls_ = 0;
```

Let's draw five frames in a row and see how many times the expansion actually fires:

```sh
$ ./bidi_cache_verify
expand_calls = 1 (expect 1)
```

Five requests, one expansion — the remaining four all hit the cache. That is the immediate payoff of cache optimization. **Of course, using a raw pointer as the key carries a precondition — the source data object's own lifetime must outlive the cache's** — otherwise the address gets reused and the cache starts mixing things up. In real projects, the sturdier approach is to key on a content hash of the object (say, feeding every `Line`'s coordinates into a hash function), at the cost of computing a hash on every cache lookup. How to weigh that trade-off depends on your data size and how often it changes; that layer is engineering judgement rather than part of the pattern itself, so we will leave it there.

::: warning Pitfall Ahead
What wrecks cached adapters most often is not the cache hit rate, but **cache invalidation**. Once you have cached an expansion result and the source data is later modified, your cache will keep serving stale points. The pointer-keyed implementation above is completely blind to changes in the contents of `*key_` — if someone edits the coordinates inside a `Rectangle`, the eight points in the cache are still the old ones, and the screen will draw a misaligned shape. **Any adapter with a cache must think through "when does the source data change, and how does the cache invalidate once it does" — skip that thinking, and sooner or later it blows up in production.**
:::

## The Object Adapter's Lifetime Trap: Copy at Construction vs Holding a Reference

Let's move our gaze from the cache back to the adapter itself and talk about one more trap that is remarkably easy to step on. The constructor of that earlier `LineToPointsAdapter` looked like this:

```cpp
explicit LineToPointsAdapter(const std::vector<Line>& lines) {
    points_.reserve(lines.size() * 2);
    for (const auto& l : lines) {
        points_.push_back(l.start);
        points_.push_back(l.end);
    }
}
```

Note that it **receives by `const&` and then copies the contents inside the constructor** into `points_`. That choice is safe — the adapter holds a copy of its own, the source data's lifetime is decoupled from the adapter's, and the source data being destroyed does not affect the adapter. The cost is one full copy at construction.

But sometimes you will want to cut a corner: "I'm only using it briefly anyway — a full copy is a loss; just hold a reference and be done with it, right?"

```cpp
// ⚠️ Dangerous: holds a reference; the reference dangles once the source data dies
class RefAdapter {
public:
    explicit RefAdapter(const std::vector<Line>& lines) : lines_(lines) {}
    // ...
private:
    const std::vector<Line>& lines_;  // prime dangling-reference territory
};
```

This version compiles, and most of the time it even runs fine — until one day somebody feeds `RefAdapter` a temporary (a `vector` returned from a function, or a source that has been `std::move`d away), the reference dangles on the spot, and what you are holding is a handful of wild pointers. **An adapter holding a reference imposes the implicit constraint "the source data's lifetime" on every caller, and the C++ compiler performs zero checking on this.** My advice: **default to the safe road of "copy at construction"; consider holding a reference only when you can guarantee — via documentation or the type system — that the source data's lifetime outlives the adapter's** (for instance, when the source data is itself a long-lived singleton). This is the same disease as the "global state leaking through interfaces" we discussed in the Singleton chapter — hide a lifetime constraint in a comment, and sooner or later somebody steps on it.

## The Boundaries Between the Adapter and Its Cousins

By now you have probably noticed that the Adapter pattern is fairly "plain" — it invents no new mechanism, it just connects two incompatible interfaces. Precisely because it is plain, it is especially easy to confuse with the other structural patterns. Let's draw a few boundaries:

**Adapter vs Bridge.** The adapter is an after-the-fact remedy — two already-existing classes with incompatible interfaces that you cannot change, so all you can do is slip a layer of translation in between. The bridge is designed in advance — from day one you split "abstraction" and "implementation" into two independent inheritance axes that can evolve separately and combine freely. Put differently, **the adapter solves "they already do not match"; the bridge prevents the mismatch up front**.

**Adapter vs Decorator.** The decorator **does not change the interface** — it implements the same interface as the decorated object and merely adds new behavior around the calls (logging, caching, permission checks). The adapter **changes the interface** — its outward interface differs from the interface of the object it holds inside; translation is its job. **The decorator is "same interface, extra seasoning"; the adapter is "different interface, translation"**.

**Adapter vs Facade.** The facade **simplifies** a complex subsystem into a new, easier-to-use entry point — it is usually one-to-many, gathering a dozen-plus subsystem classes behind one tidy interface. The adapter is one-to-one, **converting** one existing interface into a different shape. **The facade is "subtraction"; the adapter is "conversion"**.

Keep these three boundaries in mind, and when a requirement lands on your desk you can quickly tell which one you need: translation (Adapter), seasoning (Decorator), simplification (Facade), or splitting dimensions (Bridge)? These things look alike; their intents are entirely different.

## The Cost of the Adapter Pattern

Finally, let's talk honestly about the costs. The Adapter pattern's biggest virtue is **conformity to the Open-Closed Principle** — you touch no old code, add one new class, and two incompatible systems cooperate; for those "untouchable" legacy systems (vendor drivers, third-party libraries, cross-team interfaces) this is a lifeline. It also makes otherwise-unreusable code reusable again, with the cost locked inside one class, not polluting the business side.

But the costs are real. **First, it adds a layer of indirection** — every call is relayed through the adapter, theoretically paying one extra function call, and although in practice that cost is usually negligible, it is worth watching on hot paths (say, the per-frame drawing loop). **Second, it can mask the real complexity** — especially with bidirectional and cached adapters, once internal state piles up, debugging actually gets harder, because you have to look through the adapter's layer of "translation" to understand what really happened. **Third, adapters proliferate** — if you write an adapter for every pair of incompatible interfaces, sooner or later the system will be "raining adapters everywhere", and at that point the real signal is "your abstraction design itself is flawed", not "write a few more adapters".

So use adapters in moderation: **they are the right medicine for "the interfaces do not match and I cannot change either side", not a fig leaf for "the interfaces were designed as a mess".** The healthy move is to design consistent interfaces at the source, and let the adapter step in only when you genuinely cannot control one of the sides — scenarios like integrating third-party libraries, legacy code, or cross-language bindings.

## Summary

Let's trace the whole arc of the Adapter pattern once more:

| Stage | Approach | Why it is needed / why it is still not enough |
|---|---|---|
| Object adapter | Compose an Adaptee, implement the Target interface | The default choice; the implementation can be swapped at runtime, no inheritance required |
| Class adapter | Privately inherit the Adaptee + publicly inherit the Target | Hard-wired at compile time, requires inheritability; not recommended in modern C++ |
| Bidirectional adapter | Maintain both copies of the data internally, exportable in both directions | Only when both sides need each other's interface; doubles memory |
| Cache optimization | Memoize conversion results by key, skip repeated conversions | Saves time for high-frequency repeated conversions, but you must solve cache invalidation |

Note down these key conclusions:

- **The Adapter pattern is the after-the-fact remedy for "both interfaces mismatch and neither can be changed"**, not up-front interface design; that latter job belongs to the Bridge.
- **In modern C++, default to the object adapter (composition)**: it can swap implementations at runtime, does not require inheriting the Adaptee, and has the widest applicability; the class adapter (private inheritance) can almost always be replaced by composition.
- **A bidirectional adapter is worth it only when both directions are genuinely consumed** — it doubles memory and carries a lot of internal state; do not reach for it right away.
- **Cache optimization is a double-edged sword**: the time saved presupposes that you have thought through "when does the source data change, and how does the cache invalidate" — otherwise it is a time bomb.
- **The adapter is the right medicine for "untouchable legacy code"**, not a fig leaf for "chaotic interface design"; the root cause of mismatched interfaces still needs treating.

::: tip Companion Compilable Project
The examples in this section ship as a complete compilable project under `code/volumn_codes/vol4/design-patterns/Adapter/` in the repository (`.h` files + a `main` + `CMakeLists.txt`); `cmake -S . -B build && cmake --build build` reproduces the outputs above.
:::

## References

- [cppreference: `std::unique_ptr`](https://en.cppreference.com/w/cpp/memory/unique_ptr) (the preferred way for an object adapter to hold its Adaptee, since C++11)
- [cppreference: `std::unordered_map`](https://en.cppreference.com/w/cpp/container/unordered_map) (the key->result mapping for a cached adapter)
- Erich Gamma et al., *Design Patterns: Elements of Reusable Object-Oriented Software*, Chapter 4 (the original GoF Adapter pattern, object adapter vs class adapter)
- Fedor G. Pikus, *C++20 Design Patterns* (the original inspiration for the geometric `Line`/`Point` adaptation scenario)
