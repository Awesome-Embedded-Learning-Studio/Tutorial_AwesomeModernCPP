---
title: 'Flyweight Pattern: Stop Copying a Glyph for Every Character'
description: 'Starting from a text editor stuffed with tens of millions of strings, we split the "mutable" from the "immutable" step by step until the Flyweight pattern falls out, then prove that the shared pool really holds only one copy of each object, that the original factory constructs duplicates under concurrency, and why shared_ptr is the right ownership model for modern C++'
chapter: 11
order: 10
tags:
  - host
  - cpp-modern
  - intermediate
  - 对象池
  - 享元模式
difficulty: intermediate
platform: host
cpp_standard: [11, 17, 20]
reading_time_minutes: 20
related:
  - 'Singleton Pattern: From Comment-Only Constraints to Meyer''s Singleton'
prerequisites:
  - Classes and Object-Oriented Programming
  - 'Singleton Pattern: From Comment-Only Constraints to Meyer''s Singleton'
translation:
  source: documents/vol4-advanced/vol4-generics-patterns/10-flyweight.md
  source_hash: b17a11d7d21a448635544e38cb63387b7c8e9e1c92569730efb90ea2822013ae
  translated_at: '2026-09-26T05:33:20+00:00'
  engine: anthropic
  token_count: 5000
---

# Flyweight Pattern: Stop Copying a Glyph for Every Character

## What problem are we actually solving

Let's not talk about the pattern yet — let's talk about a concrete scenario. Imagine you're writing a text editor that has to open a book with tens of millions of characters. The instinctive way to write it goes like this: every character in the document is an object, and that object carries the character's glyph data (strokes, bitmaps, font metrics):

```cpp
struct Glyph {
    std::string content;          // "你"
    std::vector<Stroke> strokes;  // glyph stroke data, dozens of bytes
    FontMetrics   metrics;
    // ... what really eats memory is this whole tail, not content
};
std::vector<Glyph> document;      // tens of millions of complete Glyph copies
```

Intuitive to write, intuitive to read — but do a little arithmetic and the problem shows up: there are fewer than 4,000 commonly used Chinese characters, yet this book holds tens of millions of `Glyph` objects — **the same character "我" gets fully duplicated hundreds of thousands of times in memory**, and the dozens of bytes of stroke data inside every copy are identical. That duplicated data is the real memory killer; the little overhead of `content` itself isn't even worth mentioning by comparison.

The Flyweight Pattern exists to solve exactly this class of performance problems: **huge numbers of objects, where most of the state is duplicated and shareable**. Its idea is blunt enough to fit in one sentence: stop copying a glyph for every character — pull the glyph out and put it into a shared pool, and store nothing in the document but a reference to that shared glyph.

Before we start, though, one thing deserves a moment of thought: if the idea is this good, why do we never build any shared pool when handling ASCII text with `std::string`? The answer is **cost**. An ASCII character is a single byte, while a pointer or a reference usually takes 8 bytes (on 64-bit); to save that 1 byte you introduce an 8-byte pointer and actually make the object bigger — and on top of that you pay for maintaining the pool, a losing trade. The Flyweight pattern has its sweet spot: **the deal only pays off when the shared portion of the state is "heavy enough" and the number of objects is "large enough"**. Glyphs, textures, chess piece configurations, database connection settings — these are all classic sweet spots. A 1-byte `char` is not.

So let's proceed step by step: start from the most intuitive version, see exactly why each step still falls short, and squeeze out a modern C++ Flyweight that actually runs, is thread-safe, and has unambiguous ownership.

## Step 1: The most primitive approach — every object carries its full state

Let's lay the problem out with the plainest possible code first. For pieces on a chessboard, every piece stores its own copy of the color and type:

```cpp
#include <iostream>
#include <string>
#include <vector>

class ChessPiece {
public:
    ChessPiece(std::string color, std::string type, int x, int y)
        : color_(std::move(color))
        , type_(std::move(type))
        , x_(x)
        , y_(y) {}

    void draw() const {
        std::cout << color_ << type_ << " 放在 (" << x_ << "," << y_ << ")\n";
    }

private:
    std::string color_;  // black / red
    std::string type_;   // black pawn / red pawn / chariot / horse ...
    int         x_;      // board coordinates
    int         y_;
};

int main() {
    std::vector<ChessPiece> board;
    board.emplace_back("黑", "卒", 2, 3);
    board.emplace_back("黑", "卒", 2, 4);
    board.emplace_back("黑", "卒", 2, 5);
    // ... 16 black pawns on the board, each carrying a full copy of "黑"+"卒"
    for (const auto& p : board) p.draw();
}
```

This code is completely correct functionally; the problem hides in memory: the board is covered with 16 black pawns, and across those 16 objects `color_` is always `"黑"` and `type_` is always `"卒"` — this data has been copied verbatim 16 times. The 16 copies differ in no way whatsoever, yet every one of them dutifully occupies its memory.

Where's the problem? In the fact that **we kneaded "the state that changes" and "the state that never changes" into the same object**. For a concrete "black pawn", the color and type never change from the moment it is created until the game ends; the only thing that changes is its current position `(x_, y_)` on the board. We stored these two kinds of state — utterly different in nature — in the same way, so the unchanging state is forced to bloat in lockstep with the number of objects.

## Step 2: Splitting the state — intrinsic vs. extrinsic state

The core move of the Flyweight pattern is to cut this pile of state into two categories with one stroke, along the line of "does it change or not":

**Intrinsic state** is the part of the object that is **immutable, reusable, and shareable by multiple users**. For a chess piece, that's the identity "black pawn" — color plus type. For a glyph, it's the glyph data itself. We extract this part, put it into a shared pool, and store exactly one copy globally.

**Extrinsic state** is the part that **varies with context and is only pinned down at the moment of use**. For a chess piece, that's its current coordinates on the board; for a glyph, it's the line and column where it appears in the document. This part cannot be shared, because it inherently differs from moment to moment and place to place, so it **never enters the Flyweight object** — the caller passes it in on the fly at use time.

This split is the soul of the Flyweight pattern. Once you've figured out which state is intrinsic and which is extrinsic, all the remaining code is just the engineering implementation of the split. Let's write this mental model down in its plainest form: carve the immutable `(color, type)` pair out into its own small shareable object, and leave extrinsic state like the coordinates with the caller, to be passed in at use time.

```cpp
#include <iostream>
#include <string>

// Flyweight object: holds only intrinsic state (color + type)
class ChessPiece {
public:
    ChessPiece(std::string color, std::string type)
        : color_(std::move(color))
        , type_(std::move(type)) {}

    // Extrinsic state (x, y) arrives as a parameter, never stored in the object
    void draw(int x, int y) const {
        std::cout << color_ << type_ << " 放在 (" << x << "," << y << ")\n";
    }

private:
    std::string color_;
    std::string type_;
};
```

See how `ChessPiece` slimmed down — it only knows who it is (color + type) and has no idea where on the board it stands. The position, an extrinsic state, is passed in as a parameter the instant `draw` is called, used and immediately discarded, costing the object not a single byte of memory.

But one link is still missing: we now have an object that *can* be shared, yet nothing guarantees it actually *is* shared. If a caller wanting a "black pawn" casually writes `ChessPiece p("黑", "卒")` to build a new one, how are we any better off than before the split? We need an **entry point** whose sole job is: "identical intrinsic state gets constructed once; anyone asking later just gets that one handed back". This entry point has a proper name: the Flyweight Factory.

## Step 3: The Flyweight factory — a find-or-insert shared pool

The factory's job is simple: you ask for a "black pawn", I first check whether the pool already has one; if it does, I hand you the existing copy, and only if not do I build a new one, stuff it into the pool, and hand it over. This routine has a name — **find-or-insert** — and at heart it's just a cache:

```cpp
#include <memory>
#include <string>
#include <unordered_map>

class ChessFactory {
public:
    std::shared_ptr<ChessPiece> get_chess(const std::string& color,
                                          const std::string& type) {
        std::string key = color + type;
        auto it = pool_.find(key);
        if (it != pool_.end()) {
            return it->second;            // already there, reuse directly
        }
        auto piece = std::make_shared<ChessPiece>(color, type);
        pool_[key] = piece;
        return piece;
    }

private:
    std::unordered_map<std::string, std::shared_ptr<ChessPiece>> pool_;
};
```

The pool is an `unordered_map`: the key is the string concatenated from "color + type", and the value is a `shared_ptr<ChessPiece>`. Using `shared_ptr` here is deliberate — we'll devote a whole section to it later. For now remember one thing: **the pool and the caller can hold the same piece at the same time; the pool is responsible for "guaranteeing uniqueness" and the caller for "actually using it" — each does its own job**.

Now let's run this version end to end. Three positions get placed on the board, but the black pawn exists only once in memory:

```cpp
int main() {
    ChessFactory factory;
    auto black_pawn = factory.get_chess("黑", "卒");
    auto red_pawn   = factory.get_chess("红", "兵");

    // the same black_pawn, drawn at three different coordinates
    black_pawn->draw(2, 3);
    red_pawn->draw(5, 6);
    black_pawn->draw(2, 4);
}
```

```sh
$ g++ -std=c++23 -O2 -pthread flyweight_chess.cpp -o flyweight_chess
$ ./flyweight_chess
黑卒 放在 (2,3)
红兵 放在 (5,6)
黑卒 放在 (2,4)
```

The output is indistinguishable from the "one copy per piece" version — and that's correct: the Flyweight pattern is completely transparent to functionality; it touches memory only, never behavior. The real difference is in memory: even with 16 black pawns on the board, the pool forever holds exactly one "black pawn" object, while the 16 positions each store their own current extrinsic-state coordinates and pass them in together at draw time.

## Let's verify first: is the sharing real

Talk is cheap, so let's write a small program proving that "fetching the same key twice really yields the same object, not two copies with identical content". The criterion is simple — `shared_ptr::get()` returns the underlying raw pointer; if the pointers from the two fetches are equal, it's the same object:

```cpp
#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>

class Glyph {
public:
    explicit Glyph(std::string content) : content_(std::move(content)) {}
    const std::string& content() const { return content_; }
private:
    std::string content_;
};

class GlyphFactory {
public:
    std::shared_ptr<Glyph> get(const std::string& content) {
        auto it = pool_.find(content);
        if (it != pool_.end()) return it->second;
        auto g = std::make_shared<Glyph>(content);
        pool_[content] = g;
        return g;
    }
    std::size_t size() const { return pool_.size(); }
private:
    std::unordered_map<std::string, std::shared_ptr<Glyph>> pool_;
};

int main() {
    GlyphFactory factory;
    auto a1 = factory.get("你");
    auto a2 = factory.get("你");   // fetch the same character twice
    auto b1 = factory.get("好");

    std::cout << "a1.get() == a2.get() : " << std::boolalpha
              << (a1.get() == a2.get()) << "\n";   // expect true: the same object
    std::cout << "a1.get() == b1.get() : " << std::boolalpha
              << (a1.get() == b1.get()) << "\n";   // expect false: different characters
    std::cout << "pool size : " << factory.size() << "\n";
}
```

```sh
$ g++ -std=c++23 -O2 -pthread flyweight_verify.cpp -o flyweight_verify
$ ./flyweight_verify
a1.get() == a2.get() : true
a1.get() == b1.get() : false
pool size : 2
```

The two `get("你")` calls yield the same pointer, and the pool size is 2 — one entry each for "你" and "好". The sharing is real, not an illusion.

While we're at it, let's do the memory math. Store a one-million-character document in both the "Flyweight" and the "brute force" way, and see how much the pointer array saves over the string array (here the "glyph" is simulated by a single `char`; real glyph data would be much heavier, which would only make the Flyweight advantage more pronounced):

```sh
$ ./flyweight_mem
sizeof(std::string)            = 32 bytes
sizeof(std::shared_ptr<Glyph>) = 16 bytes
doc_fly  pointer array 概算    = 15625 KB
doc_naive string  array 概算   = 31250 KB
pool 去重后对象数              = 15
```

A `shared_ptr` is even smaller than a `std::string` (16 vs. 32 bytes), but the more crucial point: across one million positions, the actual glyph data (simulated with `char`) is stored only 15 times in the pool. Swap `Glyph` for a real glyph-style heavy object of dozens or hundreds of bytes, and what Flyweight saves is no longer a factor of two but on the order of millions of times. That's the payoff of "replacing values with references".

## A more intuitive example: characters and words in text

The chess example splits intrinsic/extrinsic state very cleanly. Let's switch to an example closer to the original book's setting, and along the way demonstrate an advanced Flyweight usage — **what gets shared doesn't have to be a single object; it can also be common combinations**.

Suppose we're rendering a large document where high-frequency characters like "你", "好", "吧" appear over and over, and common phrases like "你好" and "谢谢" appear over and over too. We can toss both the characters and the phrases into the same shared pool, store only a sequence of references in the document, and fetch them in order at render time:

```cpp
#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class Glyph {
public:
    explicit Glyph(std::string content) : content_(std::move(content)) {}
    void draw() const { std::cout << content_; }
private:
    std::string content_;
};

class GlyphFactory {
public:
    std::shared_ptr<Glyph> get(const std::string& content) {
        auto it = pool_.find(content);
        if (it != pool_.end()) return it->second;
        auto g = std::make_shared<Glyph>(content);
        pool_[content] = g;
        return g;
    }
private:
    std::unordered_map<std::string, std::shared_ptr<Glyph>> pool_;
};

// Document: holds only references to the shared glyphs, not the glyph data itself
class Document {
public:
    void add_word(const std::shared_ptr<Glyph>& glyph) {
        text_.push_back(glyph);
    }
    void render() const {
        for (const auto& g : text_) g->draw();
        std::cout << "\n";
    }
private:
    std::vector<std::shared_ptr<Glyph>> text_;
};

int main() {
    GlyphFactory factory;
    Document doc;
    auto ni = factory.get("你");
    auto hao = factory.get("好");
    auto ba = factory.get("吧");
    doc.add_word(ni); doc.add_word(hao); doc.add_word(ba);
    doc.add_word(ni); doc.add_word(hao);   // the second "你好", fully reused
    doc.render();
}
```

```sh
$ ./flyweight_source_example
你好吧你好
```

Five characters appear in the document, yet the pool holds only three glyphs. If you like, you can stuff "你好" as a whole into the same pool as a single Flyweight key — the next time "你好" shows up, it's an instant hit, saving even the two lookups. Flyweight granularity is tunable per scenario: the smaller the objects and the more frequently they appear, the more visible the sharing payoff, and the more worthwhile tossing them into the pool becomes.

## Pitfall warning: this factory is not thread-safe

At this point we have a functionally correct, memory-saving Flyweight. Don't rush to use it — **the original factory hides a deep pitfall: under concurrency it constructs duplicate objects**.

Look at `get`: it does `find` first, and only on a miss does it `make_shared` and then `insert`. There is zero synchronization among these three steps. Single-threaded, that's perfectly fine — but the moment multiple threads ask for the same key simultaneously, you crash into a classic **TOCTOU (Time-of-Check-to-Time-of-Use) race**: thread A checks, finds "你" missing, and is about to build it; thread B checks, also finds it missing, and also goes to build it; both finish, both `insert` into the pool, and the object for "你" ends up constructed twice. The Flyweight promise of "globally unique" quietly falls apart under concurrency like this.

Let's deliberately slow the constructor down a little to widen the race window, and run it for you:

```cpp
class Glyph {
public:
    explicit Glyph(const std::string& content) : content_(content) {
        ++kConstruct;
        std::this_thread::sleep_for(std::chrono::microseconds(100));  // widen the race window
    }
    static inline std::atomic<int> kConstruct{0};
    std::string content_;
};

class NaiveFactory {                          // the factory from the original notes, no locking
public:
    std::shared_ptr<Glyph> get(const std::string& content) {
        auto it = pool_.find(content);
        if (it != pool_.end()) return it->second;
        auto g = std::make_shared<Glyph>(content);
        pool_[content] = g;
        return g;
    }
    std::size_t size() const { return pool_.size(); }
private:
    std::unordered_map<std::string, std::shared_ptr<Glyph>> pool_;
};
```

Have 64 threads simultaneously ask for the same character "你", and count how many times it actually gets constructed:

```sh
$ g++ -std=c++23 -O2 -pthread flyweight_race2.cpp -o flyweight_race2
$ for i in 1 2 3; do echo "--- run $i ---"; ./flyweight_race2; done
--- run 1 ---
构造次数 = 4 (理想 1)
pool 终态大小 = 1 (理想 1)
--- run 2 ---
构造次数 = 4 (理想 1)
pool 终态大小 = 1 (理想 1)
--- run 3 ---
构造次数 = 5 (理想 1)
pool 终态大小 = 1 (理想 1)
```

Ideally "你" should be constructed exactly once; in reality it was constructed 4 to 5 times. And here's the especially sneaky part — `pool_.size()` still reads 1 after everything finishes, so it *looks* like "nothing happened". That's because `operator[]` ends up overwriting the multiple construction results with one another, and the pool's final state converges to a single entry. **So judging by pool size alone, you'd never spot the problem**: the object's final state is shared, but the side effects of construction (loading resources, allocating memory, initializing state) have genuinely happened several times over. In real scenarios, constructing the flyweight object is often precisely the "heavy" operation — loading a texture, parsing a configuration — and a few duplicate constructions can waste more than all the memory you painstakingly saved.

::: warning The Flyweight factory concurrency trap
The original find-or-insert factory **holds only in single-threaded code**. Once flyweight objects may be fetched concurrently by multiple threads, you must lock explicitly: `unordered_map` is not a thread-safe container, and find-or-insert itself carries a TOCTOU race. Don't be fooled by "the final pool size looks normal" — that's just an illusion created by `operator[]` overwriting entries; the construction side effects still got duplicated.
:::

## Fixing it right: a thread-safe factory with one lock

The fix is actually direct — wrap the whole find-or-insert in a `std::mutex`. Only one thread can be inside the critical section at a time, so `find` and `insert` become one atomic whole, and the race naturally disappears:

```cpp
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

class ThreadSafeGlyphFactory {
public:
    std::shared_ptr<Glyph> get(const std::string& content) {
        std::lock_guard<std::mutex> lk(mtx_);
        auto it = pool_.find(content);
        if (it != pool_.end()) return it->second;
        auto g = std::make_shared<Glyph>(content);
        pool_[content] = g;
        return g;
    }

private:
    std::mutex mtx_;
    std::unordered_map<std::string, std::shared_ptr<Glyph>> pool_;
};
```

The same 64 concurrent threads — this time the construction count is a steady 1:

```sh
$ g++ -std=c++23 -O2 -pthread flyweight_threadsafe.cpp -o flyweight_threadsafe
$ for i in 1 2 3; do echo "--- run $i ---"; ./flyweight_threadsafe; done
--- run 1 ---
构造次数 = 1 (理想 1)
--- run 2 ---
构造次数 = 1 (理想 1)
--- run 3 ---
构造次数 = 1 (理想 1)
```

You may have heard of the "Double-Checked Locking Pattern (DCLP)" route — do a lock-free `find` outside the lock first, return immediately on a hit, and only take the lock on a miss. I have to warn you: this route is extremely hard to get right in C++. Reads and writes of an `unordered_map` itself come with no data-race guarantees under multithreading; reading a map outside the lock while another thread may be writing it is already undefined behavior. To "read outside the lock" safely, you'd have to switch to a concurrency-safe hash table, or use atomic load/store on `std::atomic<std::shared_ptr>` (since C++20) — and the complexity jumps immediately. For the overwhelming majority of scenarios, **one mutex wrapping the entire find-or-insert is the cheapest and least error-prone choice** — contention on a flyweight factory is usually low (after a hot key's first construction, nearly everything is a `find` hit), so the cost of the lock is far smaller than the bugs your blind optimization introduces.

## Why shared_ptr, and not raw pointers or weak_ptr

We've been using `shared_ptr` all along; it's time to make clear why, because the GoF book's original Flyweight used raw pointers — and that style is a pitfall in modern C++.

The Flyweight ownership model is a bit special: **the factory "manages" the flyweight objects, callers "use" them, both sides need to hold them, but neither should own them exclusively**. That's precisely `shared_ptr` semantics — shared ownership, with the object reclaimed only when the last holder destructs.

Let's first verify a key property: **a `shared_ptr` handed to a caller keeps the object alive even after the factory's pool is cleared**. This is the bedrock on which shared ownership stands; let's run it to confirm:

```cpp
int main() {
    std::shared_ptr<Glyph> outer;
    {
        std::unordered_map<std::string, std::shared_ptr<Glyph>> pool;
        auto g = std::make_shared<Glyph>("你");
        pool["你"] = g;
        outer = g;                                  // the caller holds a copy too
        std::cout << "池子活着时 use_count = " << g.use_count() << "\n";
    }                                               // pool destructed, but outer is still alive
    std::cout << "池子死后   use_count = " << outer.use_count() << "\n";
    std::cout << "outer 还能用吗? content = " << outer->content() << "\n";
}
```

```sh
$ ./flyweight_refcount
construct 你 use_count=0
池子活着时 use_count = 3
池子死后   use_count = 1
outer 还能用吗? content = 你
destruct  你
```

While the pool is alive, `use_count` is 3 (one for the `map`, one for the local `g`, one for `outer`); after the pool destructs it drops to 1, yet `outer` can still access the object safely, and the object is destroyed only when `outer` itself destructs. **This is the most crucial correctness guarantee `shared_ptr` brings to Flyweight: the flyweight object's lifetime follows its references, not the factory.**

Compare with the GoF original's raw-pointer scheme: the factory does `pool_[key] = new Glyph(...)` and returns a `Glyph*`. All the caller gets is a raw pointer: it neither knows who owns that pointer nor can it stop the factory from `delete`-ing the object one day. The sample code in the original book doesn't even write the `delete` — run it and you have a genuine memory leak. Write Flyweight in modern C++ and **you put `shared_ptr` in the pool and return `shared_ptr`; ownership coordinates itself** — no leaks, no dangling.

Then why not `weak_ptr`? One argument goes: "the factory holds only `weak_ptr`, so once nobody uses the object it's reclaimed automatically and the pool costs no memory." Sounds lovely, but `weak_ptr` means every `get` has to `lock()` first, and a failed `lock` (the object really was reclaimed) means reconstructing it — yet Flyweight's core payoff is precisely "construct once, reuse over and over". If you let flyweight objects get reclaimed and rebuilt all the time, the shared pool degrades into an ordinary cache and loses the point of "saving construction". **Once a flyweight object is constructed, it should live for the factory's whole lifetime** — exactly the semantics `shared_ptr`'s strong reference expresses. `weak_ptr` suits "used occasionally, forgotten afterward" caches, not flyweights.

## A closer-to-production example: configuration sharing

Let's fold the preceding conclusions into an example that looks more like production code. Suppose a bunch of places in the program connect to databases; the connection configuration is determined by `(host, port)`, identical configurations can perfectly well share one and the same object, and each site keeps its own extrinsic state such as connection counts and timeouts:

```cpp
#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>

class DbConfig {
public:
    DbConfig(std::string host, int port)
        : host_(std::move(host)), port_(port) {}
    void show() const {
        std::cout << "DbConfig: " << host_ << ":" << port_ << "\n";
    }
private:
    std::string host_;
    int         port_;
};

class DbConfigFactory {
public:
    std::shared_ptr<DbConfig> get(const std::string& host, int port) {
        std::lock_guard<std::mutex> lk(mtx_);        // concurrency-safe
        std::string key = host + ":" + std::to_string(port);
        auto it = pool_.find(key);
        if (it != pool_.end()) return it->second;
        auto cfg = std::make_shared<DbConfig>(host, port);
        pool_[key] = cfg;
        return cfg;
    }
private:
    std::mutex    mtx_;
    std::unordered_map<std::string, std::shared_ptr<DbConfig>> pool_;
};

int main() {
    DbConfigFactory factory;
    auto c1 = factory.get("127.0.0.1", 3306);
    auto c2 = factory.get("127.0.0.1", 3306);        // the same object as c1
    auto c3 = factory.get("192.168.1.10", 5432);     // a different one
    c1->show(); c2->show(); c3->show();
    std::cout << "c1 和 c2 是同一份? " << std::boolalpha
              << (c1.get() == c2.get()) << "\n";
}
```

```sh
$ ./flyweight_dbconfig
DbConfig: 127.0.0.1:3306
DbConfig: 127.0.0.1:3306
DbConfig: 192.168.1.10:5432
c1 和 c2 是同一份? true
```

This is a complete Flyweight you can drop straight into production: intrinsic state `(host, port)` is extracted and shared; extrinsic state (connection count, timeout, transaction state) stays over on the connection objects; the factory carries a lock for concurrency safety; `shared_ptr` keeps ownership clear. The "different every time" things you want — connection counts, timeouts — never enter the flyweight; just pass them in at use time. Same routine as the chess coordinates.

## Why the Flyweight pattern is disliked

At this point we have a correct, thread-safe Flyweight with clear ownership. As with the Singleton, we owe the Flyweight an honest account of its costs — no telling only the nice parts.

**First, you have to think through how to split the state.** This is Flyweight's biggest hurdle — dividing an object's fields into "intrinsic state" and "extrinsic state" is not always obvious. Cut it wrong, and either you pass what should have been shared back and forth as extrinsic state, throwing away the sharing payoff for nothing, or you stuff what should have been mutable into the flyweight, letting one shared object interfere across users (a far worse bug). Whether your Flyweight is used correctly is 90% decided by whether this cut lands correctly.

**Second, passing extrinsic state is a burden on the caller.** Flyweight carves extrinsic state out of the object, and the price is re-passing it on every use. A single `draw(int x, int y)` is fine; but with a whole pile of extrinsic state (position, scale, rotation, color tint), the call-site signature bloats badly — and that state has to live in the caller's own data structures. You saved space inside the flyweight object, but somewhere else you are now storing a table of extrinsic state. Whether the net gain is positive, you have to tally the whole ledger.

**Third, it introduces a globally visible factory.** Exactly the Singleton's problem: a flyweight factory is at bottom a stateful shared facility that anyone can stuff into and fetch from. Once the factory's keys are badly designed (say, mutable state got concatenated into a key), the whole system's behavior becomes hard to trace. It's also hard to swap out in testing — injecting a fake flyweight pool into a module that depends on the global factory is painful.

**Fourth, not every batch of "similar objects" deserves Flyweight.** We said it up front: Flyweight has a sweet spot — the shared state must be "heavy enough" and the object count "large enough". If what you hold is a pile of objects with disparate fields and almost no repetition, the sharing value is small; or if the objects are already feather-light (a `char`, say), forcing Flyweight on them only makes the code more complex and the memory larger. Before reaching for Flyweight, ask yourself one question: is this slice of state worth maintaining a pool for?

## Summary

Let's walk the whole evolution path once:

| Stage | Approach | Why it still wasn't enough |
|---|---|---|
| Full state per object | All fields kneaded into one class | Immutable intrinsic state gets copied along with the object count |
| Split intrinsic/extrinsic state | Intrinsic state extracted, extrinsic state passed as arguments | Nothing yet guarantees "identical state is constructed only once" |
| Flyweight factory | find-or-insert shared pool | Functionally correct, but **has a TOCTOU race under concurrency** |
| Thread-safe factory | A mutex wrapping find-or-insert | Good enough, and `shared_ptr` makes ownership clear |

Note down these key conclusions:

- **The core of Flyweight is "share intrinsic state + pass extrinsic state as arguments"** — to judge whether an object suits Flyweight, step one is always asking yourself: which fields are immutable and shareable, and which change every time?
- **The flyweight factory holds only in single-threaded code** — the original find-or-insert has a TOCTOU race, and under concurrency it must be wrapped in a mutex. Don't be fooled by "the pool's final size looks normal"; the construction side effects still repeat.
- **Use `shared_ptr`, not raw pointers** — Flyweight is shared ownership; `shared_ptr` lets the factory and the callers each hold a copy and coordinate lifetimes automatically, leaking nothing and dangling never. `weak_ptr` would have objects constantly reclaimed and rebuilt, wiping out the very construction savings Flyweight exists for.
- **Flyweight has a sweet spot**: the shared state must be heavy enough and the object count large enough for the trade to pay. Something already as light as an ASCII `char` loses more than it gains from Flyweight.

::: tip Companion compilable project
This section's examples live in the repository under `code/volumn_codes/vol4/design-patterns/Flyweight/` as a complete compilable project (`.h` + main + `CMakeLists.txt`); `cmake -S . -B build && cmake --build build` reproduces the outputs above.
:::

## References

- [cppreference: `std::shared_ptr`](https://en.cppreference.com/w/cpp/memory/shared_ptr) (shared ownership and reference counting, since C++11)
- [cppreference: `std::unordered_map`](https://en.cppreference.com/w/cpp/container/unordered_map) (the usual underlying pool for flyweight factories)
- [cppreference: `std::mutex` / `std::lock_guard`](https://en.cppreference.com/w/cpp/thread/mutex) (synchronization primitives for the thread-safe factory)
- Gamma, Helm, Johnson, Vlissides, *Design Patterns* (GoF), Structural Patterns · Flyweight (the canonical text; note that its sample code uses raw pointers — modern C++ should use `shared_ptr` instead)
