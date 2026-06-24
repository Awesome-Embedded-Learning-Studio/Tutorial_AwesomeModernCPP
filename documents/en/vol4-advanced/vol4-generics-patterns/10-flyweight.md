---
title: 'Flyweight Pattern: Stop Copying a Glyph for Every Character'
description: Starting from a text editor stuffed with tens of millions of strings,
  we will progressively separate the "mutable" from the "immutable" to derive the
  Flyweight pattern. Along the way, we will verify that the shared pool truly contains
  only a single instance of each object, demonstrate how a naive factory triggers
  duplicate construction under concurrency, and explain why `shared_ptr` represents
  the correct ownership model in modern C++.
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
cpp_standard:
- 11
- 17
- 20
reading_time_minutes: 20
related:
- 单例模式:从注释约束到 Meyer's Singleton
prerequisites:
- 'Chapter 6: 类与对象'
- 单例模式:从注释约束到 Meyer's Singleton
translation:
  source: documents/vol4-advanced/vol4-generics-patterns/10-flyweight.md
  source_hash: b17a11d7d21a448635544e38cb63387b7c8e9e1c92569730efb90ea2822013ae
  translated_at: '2026-06-24T00:57:31.042652+00:00'
  engine: anthropic
  token_count: 4373
---
# Flyweight Pattern: Stop Copying Glyph Data for Every Character

## What Problem Are We Actually Solving?

Let's set aside the pattern definition for a moment and look at a concrete scenario. Imagine you are writing a text editor that needs to open a massive book containing tens of millions of characters. Our intuitive approach might look like this: every character in the document is an object, and that object carries the character's glyph data (strokes, bitmaps, font metrics):

```cpp
struct Glyph {
    std::string content;          // "你"
    std::vector<Stroke> strokes;  // 几十字节的字形笔画数据
    FontMetrics   metrics;
    // ... 真正占内存的是后面这一坨,不是 content
};
std::vector<Glyph> document;      // 几千万份完整的 Glyph
```

It feels intuitive to write and intuitive to read, but if you do the math, you will spot the problem immediately: there are fewer than 4,000 commonly used Chinese characters, yet this book contains tens of millions of `Glyph` objects—**the same character "我" (I) is fully duplicated hundreds of thousands of times in memory**, with the exact same dozens of bytes of stroke data in every copy. This repetitive data is the real memory hog; the overhead of `content` itself is negligible by comparison.

The **Flyweight Pattern** is designed to solve performance problems exactly like this: **"a large number of objects where most of the state is repetitive and can be shared"**. The concept is straightforward enough to sum up in one sentence: stop copying the font metrics for every character—extract the font data, put it in a shared pool, and store only a reference to that shared data in the document.

However, before we start coding, we need to clarify one thing: if this idea is so good, why don't we usually use a shared pool when writing `std::string` for ASCII text? The answer lies in the **cost**. An ASCII character is only one byte, while a pointer or reference typically takes 8 bytes (on a 64-bit system). To save 1 byte, you introduce an 8-byte pointer, which actually makes the object larger. When you add the overhead of maintaining the pool, the loss outweighs the gain. The Flyweight Pattern has a "sweet spot": **this trade-off is only profitable when the shared state is "heavy enough" and the number of objects is "large enough"**. Font metrics, textures, chess piece configurations, and database connection settings are typical sweet spots. A 1-byte `char` is not.

Next, we will proceed step-by-step, starting with the most intuitive approach. We will look at why each step falls short, eventually forcing out a modern C++ Flyweight that works, is thread-safe, and has clear ownership semantics.

## Step 1: The Naive Approach — Each Object Carries Its Full State

Let's start by exposing the problem using the most straightforward approach. For chess pieces on a board, each piece stores its own color and type:

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
    std::string color_;  // 黑 / 红
    std::string type_;   // 卒 / 兵 / 车 / 马 ...
    int         x_;      // 棋盘坐标
    int         y_;
};

int main() {
    std::vector<ChessPiece> board;
    board.emplace_back("黑", "卒", 2, 3);
    board.emplace_back("黑", "卒", 2, 4);
    board.emplace_back("黑", "卒", 2, 5);
    // ... 棋盘上 16 个黑卒,每个都完整拷了一份 "黑"+"卒"
    for (const auto& p : board) p.draw();
}
```

This code is functionally correct, but the problem lies in memory usage: the board is filled with 16 black pawns. Within these 16 objects, `color_` is always `"Black"` and `type_` is always `"Pawn"`. This data is duplicated verbatim 16 times. These 16 copies are identical, yet each one occupies memory dutifully.

Where is the issue? It stems from the fact that **we have mixed "mutable state" with "immutable state" in the same object**. The color and type, for a specific "Black Pawn", will not change from the moment it is created until the game ends; the only thing that changes is its position `(x_, y_)` on the board. We stored these two fundamentally different types of state in the same way, forcing the immutable state to bloat alongside the number of objects.

## Step 2: Splitting the State — Intrinsic vs. Extrinsic State

The core action of the Flyweight pattern is to slice this pile of state into two categories based on whether it changes or not:

**Intrinsic state** is the part of the object that is **immutable, reusable, and can be shared by multiple users**. For a chess piece, this is its identity as a "Black Pawn" — color plus type. For a glyph, it is the glyph data itself. We extract this part, place it in a shared pool, and store only one copy globally.

**Extrinsic state** is the part that **varies with context and is determined only at the time of use**. For a chess piece, this is its current coordinates on the board; for a glyph, it is the line and column where it appears in a document. This part cannot be shared because it varies by time and place, so it **does not enter the Flyweight object**. Instead, the caller passes it in temporarily when using the object.

This split is the soul of the Flyweight pattern. Once you clarify which states are intrinsic and which are extrinsic, the rest of the code is simply the engineering implementation of this split. Let's first write this mental model into the most straightforward version: extract the pair `(color, type)` as immutable intrinsic state to create a separate, small, shareable object, and leave the extrinsic state like coordinates with the caller to be passed in during use.

```cpp
#include <iostream>
#include <string>

// 享元对象:只装内部状态(颜色 + 类型)
class ChessPiece {
public:
    ChessPiece(std::string color, std::string type)
        : color_(std::move(color))
        , type_(std::move(type)) {}

    // 外部状态(x, y)作为参数传进来,不存进对象
    void draw(int x, int y) const {
        std::cout << color_ << type_ << " 放在 (" << x << "," << y << ")\n";
    }

private:
    std::string color_;
    std::string type_;
};
```

You see, `ChessPiece` has slimmed down—it only knows its own identity (color + type), and is completely unaware of its position on the board. This external state, the position, is passed in as a parameter only at the exact moment `draw` is called. It is used immediately and then discarded, without occupying a single byte of the object's memory.

However, we are missing one piece of the puzzle: we now have a "sharable" object, but nothing guarantees it is actually shared. If the caller wants a "Black Pawn" and carelessly creates a new one with `ChessPiece p("Black", "Pawn")`, how is this any better than what we had before? We need an **entry point** specifically responsible for ensuring that "identical internal state is instantiated only once; subsequent requests simply return that existing instance." This entry point has a specific name: the Flyweight Factory.

## Step 3: Flyweight Factory — The find-or-insert Shared Pool

The factory's job is simple: you ask for a "Black Pawn", and I first check if it's already in the pool. If it is, I give you the existing one; if not, I create a new one, put it in the pool, and then give it to you. This pattern is known as **find-or-insert**, and its essence is simply caching:

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
            return it->second;            // 已有,直接复用
        }
        auto piece = std::make_shared<ChessPiece>(color, type);
        pool_[key] = piece;
        return piece;
    }

private:
    std::unordered_map<std::string, std::shared_ptr<ChessPiece>> pool_;
};
```

The pool is an `unordered_map`, where the key is a string composed of "color + type", and the value is a `shared_ptr<ChessPiece>`. We use `shared_ptr` for a specific reason, which we will cover in detail later. For now, just remember this one thing: **the pool and the caller can hold references to the same piece simultaneously. The pool is responsible for "ensuring uniqueness," while the caller is responsible for "using it." Each has its own job.**

Now, let's run through this complete version. We place three positions on the board, but there is only one black pawn in memory:

```cpp
int main() {
    ChessFactory factory;
    auto black_pawn = factory.get_chess("黑", "卒");
    auto red_pawn   = factory.get_chess("红", "兵");

    // 同一份 black_pawn,被画在三个不同的坐标上
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

The output is indistinguishable from the "one copy per piece" version—and that is correct. The Flyweight pattern is completely transparent to functionality; it only affects memory, not behavior. The real difference lies in memory: even if there are 16 black pawns on the board, the pool holds only one "Black Pawn" object. The 16 positions store their current extrinsic state (coordinates) and pass them in together when drawing.

## Let's verify this: Is it really shared?

Talk is cheap. Let's write a small program to prove that "fetching the same key twice yields the exact same object, not two copies with identical content." The criterion is simple—`shared_ptr::get()` returns the underlying raw pointer. If the pointers obtained from two fetches are equal, it is the same object:

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
    auto a2 = factory.get("你");   // 同一个字取两次
    auto b1 = factory.get("好");

    std::cout << "a1.get() == a2.get() : " << std::boolalpha
              << (a1.get() == a2.get()) << "\n";   // 期待 true:同一份对象
    std::cout << "a1.get() == b1.get() : " << std::boolalpha
              << (a1.get() == b1.get()) << "\n";   // 期待 false:不同字
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

Two calls to `get("你")` return the same pointer, and the pool size is two—one instance each for "你" and "好". The sharing is real, not an illusion.

Let's also do a quick memory calculation. We will store a document containing one million characters using both the "Flyweight" and the "Brute Force" approaches, and see how much memory the pointer array saves compared to the string array (here, the "glyph" is simulated with a single `char`; real glyph data would be much heavier, making the Flyweight advantage even more pronounced):

```sh
$ ./flyweight_mem
sizeof(std::string)            = 32 bytes
sizeof(std::shared_ptr<Glyph>) = 16 bytes
doc_fly  pointer array 概算    = 15625 KB
doc_naive string  array 概算   = 31250 KB
pool 去重后对象数              = 15
```

`shared_ptr` is smaller than a `std::string` (16 vs. 32 bytes), but more importantly, across one million positions, the actual glyph data (simulated with `char`) is stored only 15 times in the pool. If we replace `Glyph` with a real heavy object weighing dozens or hundreds of bytes, the savings from the Flyweight pattern aren't just double, but millions of times over. This is the payoff of "replacing values with references."

## A More Intuitive Example: Characters and Words in Text

The chess piece example cleanly separates internal and external state. Let's switch to an example closer to the original book's scenario, and incidentally demonstrate an advanced Flyweight technique—**what is shared doesn't have to be a single object; it can also be common combinations**.

Assume we are rendering a large document where high-frequency characters like "you", "hello", and "right" appear repeatedly, and common phrases like "hello" and "thanks" also appear repeatedly. We can throw both characters and words into the same shared pool, storing only a sequence of references in the document, and retrieve them in order during rendering:

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

// 文档:只持有指向共享字形的引用,不持有字形数据本身
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
    doc.add_word(ni); doc.add_word(hao);   // 第二次「你好」,完全复用
    doc.render();
}
```

```sh
$ ./flyweight_source_example
你好吧你好
```

The document contains five characters, but the pool only holds three glyph instances. If you want, you can even use the entire string "你好" (Hello) as a Flyweight key and put it into the same pool. The next time you encounter "你好", it's a direct hit, saving you even the two lookups. You can adjust the granularity of the Flyweight based on the scenario: the smaller the object and the more frequent its occurrence, the more obvious the benefits of sharing, and the more worth it is to put it in the pool.

## Warning: This Factory is Not Thread-Safe

At this point, we have a functionally correct, memory-saving Flyweight. Don't rush to use it yet—the **original factory has a deeply hidden pitfall: it constructs duplicate objects under concurrency**.

Look at the `get` function: it first `find`s, and only if it fails, does it `make_shared` and then `insert`. There is no synchronization between these three steps. In a single-threaded environment, this is perfectly fine, but once multiple threads request the same key simultaneously, you will hit a classic **TOCTOU (Time-of-Check-to-Time-of-Use) race condition**: Thread A checks and finds "你" (You) is missing, and is about to create it; Thread B also checks and finds it missing, and goes to create it too. Both finish creating and `insert` into the pool, resulting in the object for "你" being created twice. The Flyweight's promise of "global uniqueness" is quietly broken under concurrency.

We intentionally slow down the constructor to widen this race window and show it to you in action:

```cpp
class Glyph {
public:
    explicit Glyph(const std::string& content) : content_(content) {
        ++kConstruct;
        std::this_thread::sleep_for(std::chrono::microseconds(100));  // 放大竞态窗口
    }
    static inline std::atomic<int> kConstruct{0};
    std::string content_;
};

class NaiveFactory {                          // 笔记原版的工厂,无锁
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

Let's have 64 threads request the same character "you" simultaneously, and count exactly how many times it is constructed:

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

Ideally, "you" should be constructed only once, but in reality, four or five constructions occur. There is a particularly tricky pitfall here—`pool_.size()` still reads as 1 after execution, making it look like "nothing happened." This is because `operator[]` eventually overwrites the results of the multiple constructions, so the pool's final state converges to a single entry. **Therefore, simply looking at the pool size reveals nothing**: the final state of the object is shared, but the side effects of construction (loading resources, allocating memory, initializing state) have genuinely occurred several times. In real-world scenarios, constructing a flyweight object is often that "heavy" operation—loading a texture, parsing a configuration. If you repeat the construction a few times, the memory you painstakingly saved might not even compensate for the waste caused by this redundant loading.

::: warning The Flyweight Factory Concurrency Trap
The original find-or-insert factory **is only valid in single-threaded environments**. Once flyweight objects might be accessed concurrently by multiple threads, `unordered_map` is not a thread-safe container, and find-or-insert itself has a TOCTOU race condition, so explicit locking is required. Don't be fooled by "the final pool size looks normal"—that's just an illusion created by `operator[]` overwriting entries; the side effects of construction are still duplicated.
:::

## Fixed: A Thread-Safe Factory with a Lock

Fixing this is actually straightforward—wrap the entire find-or-insert logic with `std::mutex`. Only one thread can enter the critical section at a time, making `find` and `insert` an atomic unit, so the race condition naturally disappears:

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

With 64 concurrent threads, the constructor count is now firmly one:

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

You might have heard of the "Double-Checked Locking Pattern (DCLP)" approach—performing a lock-free `find` outside the lock first, returning directly on a hit, and only acquiring the lock on a miss. I must warn you that this is extremely difficult to get right in C++. The `unordered_map` itself offers no data race guarantees for concurrent reads and writes. Reading a map outside a lock while another thread might be writing to it is already undefined behavior. To achieve safe "lock-free reading," you would need to switch to a concurrent hash table or use atomic load/store with `std::atomic<std::shared_ptr>` (available since C++20), which immediately increases complexity. For the vast majority of scenarios, **wrapping the entire find-or-insert operation in a single mutex is the most cost-effective and least error-prone choice**. Contention in a flyweight factory is typically low (after a hot key is constructed for the first time, subsequent operations are almost always `find` hits), so the cost of the lock is far less than the bugs introduced by premature optimization.

## Why use shared_ptr instead of raw pointers or weak_ptr?

We have been using `shared_ptr` consistently so far, so we need to clarify why. The original Gang of Four (GoF) book on design patterns used raw pointers for the flyweight pattern—a practice that is a trap in modern C++.

The ownership model of the flyweight pattern is somewhat unique: **the factory "manages" the flyweight objects, while the caller "uses" them. Both sides need to hold a reference, but neither should exclusively own the object.** This aligns perfectly with the semantics of `shared_ptr`—shared ownership, where the object is only reclaimed when the last holder destroys it.

Let's verify a critical property: **the `shared_ptr` obtained by the caller ensures the object remains alive even if the factory's pool is cleared.** This is the foundation upon which shared ownership is built. Let's run a quick test to confirm:

```cpp
int main() {
    std::shared_ptr<Glyph> outer;
    {
        std::unordered_map<std::string, std::shared_ptr<Glyph>> pool;
        auto g = std::make_shared<Glyph>("你");
        pool["你"] = g;
        outer = g;                                  // 调用方也持有一份
        std::cout << "池子活着时 use_count = " << g.use_count() << "\n";
    }                                               // pool 析构,但 outer 还在
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

The `use_count` is three while the pool is alive (one held by the `map`, one by the local variable `g`, and one by `outer`). After the pool is destructed, the count drops to one, but `outer` can still safely access the object. The object is only destroyed when `outer` itself is destructed. **This is the most critical correctness guarantee that `shared_ptr` brings to the Flyweight pattern: the lifetime of the Flyweight object follows the references, not the factory.**

Let's compare this with the original GoF (Gang of Four) raw pointer approach: `pool_[key] = new Glyph(...)` in the factory, returning a `Glyph*`. The caller receives a bare pointer; it neither knows who owns this pointer nor can it prevent the factory from `delete`-ing the object one day. The example code in the original book doesn't even include `delete`, resulting in a genuine memory leak when run. In modern C++, when implementing the Flyweight pattern, **we store `shared_ptr` in the pool and return `shared_ptr`; ownership is coordinated automatically**, resulting in neither leaks nor dangling pointers.

Why not use `weak_ptr`? There is a common argument: "The factory only holds `weak_ptr`, so the object is automatically reclaimed when no one uses it, saving memory in the pool." This sounds appealing, but using `weak_ptr` means we must call `lock()` every time we `get`. If `lock` fails (because the object was actually reclaimed), we have to reconstruct it—yet the core benefit of the Flyweight pattern is "construct once, reuse repeatedly." If you allow Flyweight objects to be reclaimed and reconstructed frequently, the shared pool degrades into a generic cache, losing the significance of "saving construction." **Once constructed, a Flyweight object should persist for the lifetime of the factory**, which is exactly the semantics expressed by the strong reference of `shared_ptr`. `weak_ptr` is suitable for "occasional use, forget after use" caches, not for Flyweights.

## A More Practical Example: Configuration Sharing

Let's consolidate the conclusions we've reached into an example that more closely resembles production code. Suppose there are many places in the program that need to connect to a database. The connection configuration is determined by `(host, port)`. Identical configurations can certainly share the same object, while different locations maintain their own extrinsic states, such as connection count and timeout:

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
        std::lock_guard<std::mutex> lk(mtx_);        // 并发安全
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
    auto c2 = factory.get("127.0.0.1", 3306);        // 和 c1 同一份
    auto c3 = factory.get("192.168.1.10", 5432);     // 另一份
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

This is a complete Flyweight implementation ready for production. We extract the intrinsic state `(host, port)` for sharing, leave the extrinsic state (connection count, timeout, transaction status) in the connection object, add a mutex to the factory for thread safety, and use `shared_ptr` for clear ownership. For things like connection counts and timeouts that change every time, we don't put them in the Flyweight; we just pass them in when needed. It's the same logic as chess piece coordinates.

## The Downsides of the Flyweight Pattern

At this point, we have a correct, thread-safe Flyweight with clear ownership. Just like with the Singleton, we need to be honest about the costs of the Flyweight pattern and not just sing its praises.

**First, you have to figure out how to split the state.** This is the biggest hurdle to using Flyweight—dividing an object's fields into "intrinsic state" and "extrinsic state" isn't always obvious. If you split it wrong, you either pass around things that should be shared as extrinsic state (wasting the benefits of sharing), or you shove things that should change into the Flyweight (causing shared objects to interfere with each other, which is an even worse bug). Whether the Flyweight pattern works correctly depends 90% on making the right cut here.

**Second, passing extrinsic state burdens the caller.** The Flyweight removes extrinsic state from the object, but the cost is that you have to pass it back in every time you use it. A `draw(int x, int y)` is fine, but if there is a lot of extrinsic state (position, scale, rotation, color tint), the call site signature becomes bloated. Plus, this state must be stored in the caller's own data structures—you save space inside the Flyweight object, but you end up storing another table of extrinsic state elsewhere. You need to do the math to see if the net benefit is positive.

**Third, it introduces a globally visible factory.** Just like the Singleton problem, a Flyweight factory is essentially a stateful shared facility that anyone can insert into or retrieve from. If the factory's key is designed poorly (for example, if mutable state is baked into the key), the behavior of the whole system becomes hard to track. It's also hard to swap out for testing—you can't easily inject a fake Flyweight pool into a module that depends on a global factory.

**Fourth, not all "similar objects" are worth the Flyweight treatment.** We said this at the start: Flyweight has a sweet spot. The shared state must be "heavy enough," and the object count must be "high enough." If you have a bunch of objects with different fields and almost no repetition, the sharing value is low. Or if the object itself is already very light (like a `char`), forcing a Flyweight on it only makes the code more complex and the memory footprint larger. Before applying Flyweight, ask yourself: Is this state worth maintaining a pool for?

## Summary

Let's review the entire evolution path:

| Stage | Approach | Why it wasn't enough |
|---|---|---|
| Full state per object | All fields mixed in one class | Unchanging intrinsic state is duplicated along with the object count |
| Split intrinsic/extrinsic state | Extract intrinsic state, pass extrinsic state as args | No one guarantees "same state is created only once" |
| Flyweight Factory | find-or-insert shared pool | Functionally correct, but **has TOCTOU race under concurrency** |
| Thread-safe Factory | mutex wrapping find-or-insert | Usable, and `shared_ptr` makes ownership clear |

Keep these key conclusions in mind:

- **The core of Flyweight is "intrinsic state sharing + extrinsic state passing."** To judge if an object fits the Flyweight pattern, the first step is always to ask yourself: Which fields are immutable and shareable? Which change every time?
- **The Flyweight factory is only valid in single-threaded contexts.** The original find-or-insert has a TOCTOU race; under concurrency, you must wrap it with a mutex. Don't be fooled by "the pool's final size looks correct"—the side effects of construction will repeat.
- **Use `shared_ptr`, not raw pointers.** Flyweight implies shared ownership. `shared_ptr` allows the factory and the caller to each hold a reference, automatically coordinating lifetimes, preventing both leaks and dangling pointers. `weak_ptr` would cause frequent recycling and reconstruction, which actually wipes out the construction-savings benefits of Flyweight.
- **Flyweight has a sweet spot:** The shared state must be heavy enough, and the object count high enough, for the trade-off to be worth it. For something as light as an ASCII char, using Flyweight costs more than it saves.

::: tip Companion Compilable Project
The examples for this section are in the repository at `code/volumn_codes/vol4/design-patterns/Flyweight/` as a complete compilable project (`.h` + main + `CMakeLists.txt`). Run `cmake -S . -B build && cmake --build build` to see the output shown above.
:::

## References

- [cppreference: `std::shared_ptr`](https://en.cppreference.com/w/cpp/memory/shared_ptr) (Shared ownership and reference counting, since C++11)
- [cppreference: `std::unordered_map`](https://en.cppreference.com/w/cpp/container/unordered_map) (Common underlying pool for Flyweight factories)
- [cppreference: `std::mutex` / `std::lock_guard`](https://en.cppreference.com/w/cpp/thread/mutex) (Synchronization primitives for thread-safe factories)
- Gamma, Helm, Johnson, Vlissides, *Design Patterns* (GoF), Structural Patterns · Flyweight (The classic text; note that its example code uses raw pointers, modern C++ should use `shared_ptr`)
