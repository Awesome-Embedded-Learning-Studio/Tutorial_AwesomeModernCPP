---
chapter: 3
cpp_standard:
- 11
- 14
- 17
- 20
description: A detailed explanation of member initializer lists
difficulty: intermediate
order: 1
platform: host
prerequisites:
- 'Chapter 2: 零开支抽象'
reading_time_minutes: 5
tags:
- cpp-modern
- host
- intermediate
title: Initializer list
translation:
  source: documents/vol3-standard-library/01-initializer-lists.md
  source_hash: c5f32b513967607a52fde6a6dfa9cc779161a1848aa4ee39dcdc8073b866fe66
  translated_at: '2026-05-26T11:37:01.435396+00:00'
  engine: anthropic
  token_count: 775
---
# Constructor Optimization: Initializer Lists vs. Member Assignment

In embedded C++ projects, we easily focus our attention on the "visible" areas: interrupts, DMA, timing, cache hit rates, Flash/RAM usage... But for code like constructors that "seem to run only once," we tend to let our guard down subconsciously.

However, in systems where **objects are created frequently, memory is tight, and construction paths are complex**, how we write constructors directly impacts:

- Whether redundant construction/destruction occurs
- Whether hidden default initialization costs are introduced
- Whether object invariants are broken
- Whether we "lose optimization space" at compile time

And these issues **almost all come down to one thing: whether you use initializer lists.**

------

## 1. A Common, But Not "Harmless," Pattern

When many developers first learn C++, they often write constructors like this:

```cpp
class Timer
{
public:
    Timer(uint32_t period)
    {
        period_ = period;
        enabled_ = false;
    }

private:
    uint32_t period_;
    bool     enabled_;
};

```

At first glance, there's nothing wrong with this. The logic is clear, and the readability is fine.

But in the compiler's eyes, the true meaning of this code is:

1. `period_` is **default-initialized**
2. `enabled_` is **default-initialized**
3. Enter the constructor body
4. Perform **assignment operations** on both members

In other words, **members are "processed" at least twice**.

On desktop platforms, this overhead is usually negligible. But in embedded systems, especially when:

- A large number of objects are constructed
- Members are structs, arrays, or STL containers
- Construction occurs during the startup phase (Boot / Driver Init)

This "invisible default initialization" starts to become a very real cost.

------

## 2. Initializer Lists Are Not "Syntactic Sugar"

Compare this with the initializer list approach:

```cpp
class Timer
{
public:
    Timer(uint32_t period)
        : period_(period)
        , enabled_(false)
    {}

private:
    uint32_t period_;
    bool     enabled_;
};

```

The key change here isn't "writing fewer lines of code," but rather that **the object lifecycle has changed**. Here, our member initialization becomes more direct—**initialization is completed directly during the construction phase**. In other words, **an initializer list is not assignment; it is part of construction**.

## 3. Some Members Simply "Cannot Be Assigned"

In embedded systems, this situation is not uncommon.

#### 1. `const` Members

```cpp
class Device
{
public:
    Device(uint32_t id)
        : id_(id)
    {}

private:
    const uint32_t id_;
};

```

`const` members **can only be assigned once during the initialization phase**. Assignment inside the constructor body is semantically illegal. This isn't a syntax constraint, but rather the language's protection of "object invariants."

------

#### 2. Reference Members

```cpp
class Driver
{
public:
    Driver(GPIO& gpio)
        : gpio_(gpio)
    {}

private:
    GPIO& gpio_;
};

```

Once a reference is bound, it cannot be made to refer to another object. Therefore, **the initializer list is the only correct approach**.

------

#### 3. Members Without Default Constructors

In your own framework code, this type is actually very common:

```cpp
class SpiBus
{
public:
    explicit SpiBus(uint32_t base_addr);
};

```

If such a class exists as a member:

```cpp
class Sensor
{
public:
    Sensor()
        : spi_(SPI1_BASE)
    {}

private:
    SpiBus spi_;
};

```

If we don't use an initializer list here, the code won't even compile.

------

## 4. "Semantic Completeness" Brought by Initializer Lists

In embedded engineering, we often emphasize that **"an object must be in a usable state once construction is complete."** Initializer lists naturally align with this principle.

```cpp
class RingBuffer
{
public:
    RingBuffer(uint8_t* buf, size_t size)
        : buffer_(buf)
        , size_(size)
        , head_(0)
        , tail_(0)
    {}

private:
    uint8_t* buffer_;
    size_t   size_;
    size_t   head_;
    size_t   tail_;
};

```

This pattern conveys a very clear message:

> **Once an object is constructed, its internal state is complete and self-consistent.**

Conversely, splitting initialization across the constructor body actually allows for the existence of a "half-initialized state," which is a very dangerous design signal in low-level systems.

------

## 5. Compiler Optimization Perspective: Initializer Lists = Greater Optimization Space

From the compiler's perspective:

- Initializer lists provide **deterministic construction semantics**
- The initial values of members are known during the construction phase
- This makes it easier to perform:
  - Constant propagation
  - Construction elimination
  - Stack object merging
  - In some scenarios, even complete object elimination

Especially when we make heavy use of `constexpr`, `inline`, and templates, **initializer lists are a prerequisite for compile-time optimization**.

------

## Run Online

Compare the differences between in-body assignment and initializer lists online, and observe how const and reference members are initialized:

<OnlineCompilerDemo
  title="Initializer Lists vs. Member Assignment"
  source-path="code/examples/vol34567/03_initializer_lists.cpp"
  description="Compare in-body assignment with initializer lists, and experience the initialization of const and reference members"
  allow-run
/>

## Final Thoughts

Initializer lists aren't some "advanced technique." They really aren't complicated. In embedded systems, **every redundant initialization translates into real instructions, real Flash, and real time**. Initializer lists are exactly that kind of modern C++ fundamental where **you lose out if you don't write them, and gain steadily if you do**.
