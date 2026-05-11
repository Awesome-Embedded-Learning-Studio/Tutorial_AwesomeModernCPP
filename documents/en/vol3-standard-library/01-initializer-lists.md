---
chapter: 3
cpp_standard:
- 11
- 14
- 17
- 20
description: Detailed explanation of member initializer lists
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
title: Initialization list
---
# Constructor Optimization: Initialization Lists vs. Member Assignment

In embedded C++ projects, we easily focus on the "visible" areas: interrupts, DMA (Direct Memory Access), timing, cache hit rates, Flash/RAM usage... But for code that "seems to only run once" like constructors, we tend to let our guard down.

However, in systems with **frequent object creation, tight memory, and complex construction paths**, how we write constructors directly impacts:

- Whether redundant construction/destruction occurs
- Whether hidden default initialization costs are introduced
- Whether object invariants are broken
- Whether we "lose optimization space" at compile time

And these issues **almost all come down to one thing: whether you use an initialization list.**

------

## 1. A Common, But Not "Harmless", Pattern

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

In other words, **each member is "processed" at least twice**.

On desktop platforms, this overhead is usually negligible. But in embedded systems, especially when:

- Constructing a large number of objects
- Members are structs, arrays, or STL containers
- Construction happens during the startup phase (Boot / Driver Init)

This "invisible default initialization" starts to become a real cost.

------

## 2. Initialization Lists Are Not "Syntactic Sugar"

Compare this with the initialization list approach:

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

The key change here isn't "writing fewer lines of code." Instead, **the object lifecycle has changed**. Our member initialization becomes more direct—**initialization happens directly during the construction phase**. In other words, **an initialization list is not assignment; it is part of construction**.

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

Once a reference is bound, it cannot be made to refer to another object. Therefore, **the initialization list is the only correct approach**.

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

If we don't use an initialization list here, the code won't even compile.

------

## 4. "Semantic Integrity" Brought by Initialization Lists

In embedded engineering, we often emphasize that **"an object must be in a usable state once construction is complete"**. Initialization lists naturally align with this principle.

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

> **Once construction is complete, the internal state is complete and self-consistent.**

Conversely, scattering initialization throughout the constructor body essentially allows "half-initialized states" to exist, which is a very dangerous design signal in low-level systems.

------

## 5. Compiler Optimization Perspective: Initialization Lists = More Optimization Space

From the compiler's perspective:

- Initialization lists provide **deterministic construction semantics**
- Members' initial values are known during the construction phase
- This makes it easier to perform:
  - Constant propagation
  - Construction elimination
  - Stack object merging
  - In some scenarios, complete object elimination

Especially when we make heavy use of `constexpr`, `inline`, and templates, **initialization lists are a prerequisite for compile-time optimization**.

------

## Final Thoughts

Initialization lists aren't some "advanced technique." They really aren't complicated. In embedded systems, **every redundant initialization translates into real instructions, real Flash usage, and real time**. Initialization lists are exactly that kind of modern C++ fundamental where **you lose if you don't write them, and gain if you do**.
