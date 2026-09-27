---
chapter: 1
conference: cppcon
conference_year: 2025
cpp_standard:
- 20
- 23
description: CppCon 2025 talk notes — syntax uniformity, SmartPtr constraints, multi-parameter concepts, generic vs OOP, iterative refinement, and a first look at C++26 static reflection
difficulty: intermediate
order: 3
platform: host
reading_time_minutes: 44
speaker: Bjarne Stroustrup
tags:
- cpp-modern
- host
- intermediate
talk_title: Concept-based Generic Programming
title: Syntax Consistency, Advanced Concepts, and Generic Philosophy
video_bilibili: https://www.bilibili.com/video/BV1ptCCBKEwW
video_youtube: https://www.youtube.com/watch?v=VMGB75hsDQo
translation:
  source: documents/vol10-open-lecture-notes/cppcon/2025/01-concept-based-generic-programming/03-syntax-advanced-concepts-and-generic-philosophy.md
  source_hash: a5e8794c36e62e51a93b0c85de18727690686fa3a62b7f61d4e772240495c0d4
  translated_at: '2026-09-26T15:17:51+00:00'
  engine: anthropic
  token_count: 11000
---
# Syntax Uniformity Might Matter Far More Than We Think

I used to think syntax uniformity was just cosmetic polish — "it makes the code look a little nicer". But if you look back at Simula or Java, you'll find an awkward design: user-defined types must be created with `new`, while built-in types cannot be. In Simula you can't even apply `new` to an `int`. That leads to a fatal consequence — you can never write a truly general container or algorithm, because at the syntactic level the world is split in two. One half handles built-in types, the other half handles user-defined types: two sets of code, two sets of rules.

C++ avoided this problem from day one. `int x = 0;` and `MyString x;` are syntactically indistinguishable, which means that when you write `template<typename T>`, the way you create the `T` is exactly the same whether it's an `int` or a `MyString`. This decision looks inconsequential, but it is the precondition that makes all of C++ generic programming possible.

The same logic applies to resource management. If resource management were not part of type design, and you had to manually call `malloc`/`free` or `new`/`delete`, your generic code could never be truly general — you would always have to special-case "this type needs its resources released by hand" somewhere. RAII embeds resource management into the lifetime of the type itself, and that is what lets generic code treat all types alike. Reading this far, it really hit me: the significance of RAII isn't just "preventing forgotten releases" — it is a cornerstone of the type system on which generic programming stands in the first place.

## Putting a Lock on a Smart Pointer's Arrow Operator with Concepts

With that precondition sorted out, let's look at a very concrete example. When I was writing a simple smart pointer, I ran into a question: `operator->` is not something every type should have.

Think about it: the semantics of `operator->` is "access a member through a pointer". If my smart pointer wraps an `int`, what members does an `int` have to access? So `operator->` only makes sense when `T` is a class type. Before concepts existed, you either provided it unconditionally (and users calling it on an `int` got an avalanche of unreadable template errors), or you piled `std::enable_if` on top of SFINAE until the code read like arcane scripture. With concepts, things become remarkably clean.

```cpp
#include <iostream>
#include <concepts>
#include <string>

// Define a concept: T must be a class type (struct counts too)
template<typename T>
concept HasMembers = std::is_class_v<T>;

template<typename T>
class SmartPtr {
    T* ptr_;
public:
    explicit SmartPtr(T* p = nullptr) : ptr_(p) {}
    ~SmartPtr() { delete ptr_; }

    // Copying disabled, to keep the example simple
    SmartPtr(const SmartPtr&) = delete;
    SmartPtr& operator=(const SmartPtr&) = delete;

    // operator* is available for all types
    T& operator*() const {
        return *ptr_;
    }

    // operator-> exists only when T is a class type
    // If you try to call -> on a SmartPtr<int>, the compiler flat-out tells you the
    // member function doesn't exist, instead of dumping template instantiation errors
    T* operator->() const requires HasMembers<T> {
        return ptr_;
    }
};

// Test: for class types, both operators are available
void test_with_class() {
    SmartPtr<std::string> sp(new std::string("hello"));
    std::cout << *sp << std::endl;       // OK, operator*
    std::cout << sp->size() << std::endl; // OK, operator->, because string is a class
}

// Test: for int, only operator* is available
void test_with_int() {
    SmartPtr<int> sp(new int(42));
    std::cout << *sp << std::endl;  // OK, operator*
    // std::cout << sp->  // compile error! SmartPtr<int> has no operator->
    // The error message is crystal clear: no member named 'operator->'
}
```

I ran it: `test_with_class()` works fine, and in `test_with_int()`, if you uncomment the `sp->` line, GCC's error is "no member named 'operator->' in 'SmartPtr<int>'" — clean and crisp. Back in the `enable_if` days the error could scroll off a whole screen; now it's a single line. That's the experience improvement concepts deliver — not "doing something that was impossible before", but "doing the same thing with a ten-times-better experience".

You might ask, why not just stick with `operator*` and call it a day? True, if you only use `operator*`, the smart pointer behaves uniformly for all types. But `operator->` is just too convenient when working with object types — it would be a real shame to give it up entirely. So the right move is not "remove it across the board" but "precisely control when it exists". That's exactly what concepts are for.

## pair's Copy Constructor: A Narrowing Trap in the Standard

With the smart pointer done, I followed the thread and started looking at `std::pair`'s implementation. `std::pair` has a templated copy constructor that looks roughly like this: you can copy-construct a `pair<C, D>` from a `pair<A, B>`, as long as `A` converts to `C` and `B` converts to `D`. That is indeed what the standard specifies. Sounds reasonable, right?

But look closer and there's a problem: this conversion is an ordinary implicit conversion, which means it allows narrowing conversions. For example, you can copy a `pair<double, double>` into a `pair<int, int>` — the fractional parts are simply truncated, and the compiler doesn't even give you a warning. That is not the behavior I want.

```cpp
#include <utility>
#include <iostream>

void test_std_pair_narrowing() {
    std::pair<double, double> src{3.14, 2.718};
    // This line compiles! 3.14 becomes 3, 2.718 becomes 2
    // Not a single warning; the data is silently lost
    std::pair<int, int> dst = src;
    std::cout << dst.first << ", " << dst.second << std::endl; // Output: 3, 2
}
```

I ran it, and the output really is `3, 2`; the compiler (GCC 15, with `-Wall -Wextra`) said nothing at all.

## Writing a Safe pair Ourselves: NonNarrowConvertible

We already discussed detection mechanisms for narrowing conversions in depth in [the first article](01-type-safety-and-number-concept.md). Here we'll take a more concise route — leveraging the language rule that brace initialization forbids narrowing — to implement `NonNarrowConvertible`. The idea is simple: when copy-constructing, constrain the conversion with a concept so that narrowing is not allowed.

```cpp
#include <iostream>
#include <concepts>
#include <type_traits>
#include <stdexcept>

// A concept: A can be converted to B without narrowing
// Key idea: detect it with brace initialization, because brace initialization forbids narrowing
template<typename A, typename B>
concept NonNarrowConvertible = requires(A a) {
    // If this line compiles, there is no narrowing from A to B
    // because brace initialization rejects narrowing conversions
    B{static_cast<A>(a)};
};

template<typename T1, typename T2>
class SafePair {
public:
    T1 first;
    T2 second;

    SafePair() : first{}, second{} {}
    SafePair(T1 f, T2 s) : first(f), second(s) {}

    // The core part: copy-constructing from another SafePair
    // requires both members to be NonNarrowConvertible
    template<typename U1, typename U2>
    requires NonNarrowConvertible<U1, T1> && NonNarrowConvertible<U2, T2>
    SafePair(const SafePair<U1, U2>& other)
        : first(static_cast<T1>(other.first))
        , second(static_cast<T2>(other.second))
    {}
};

void test_safe_pair_no_narrowing() {
    SafePair<double, double> src{3.14, 2.718};

    // This line fails to compile! double -> int is narrowing
    // The error message will point at the unsatisfied NonNarrowConvertible concept
    // SafePair<int, int> dst = src;  // uncommenting this errors out

    // This one is fine; int -> double is not narrowing
    SafePair<int, int> src2{3, 2};
    SafePair<double, double> dst2 = src2;  // OK
    std::cout << dst2.first << ", " << dst2.second << std::endl; // 3, 2
}
```

It took me a whole evening to get this `NonNarrowConvertible` trick working. The principle is the language rule that brace initialization forbids narrowing: if narrowing exists from `A` to `B`, then `B{a}` is ill-formed all by itself. The `requires` expression detects that ill-formedness and turns it into "concept not satisfied" rather than a hard compile error. That promotes narrowing detection from "data silently lost at runtime" to "rejected outright at compile time".

One hidden risk is worth noting, though: this implementation of `NonNarrowConvertible` relies on "whether brace initialization compiles", not on precisely determining "whether narrowing exists". For numeric types the two are equivalent, but for more complex types brace initialization can fail for other reasons (say, no matching constructor), and the resulting error message can be confusing. It's good enough for the current scenario; if more complex cases come up later, we can refine the concept further.

Besides, C++'s protection against narrowing is actually incomplete — the brace-initialization rule only applies to initialization; assignment, function argument passing, return values, and other paths all get a free pass. Real safety has to come from constraints at the type-system level, such as using concepts to block unsafe conversion paths at compile time.

## A First Taste of C++26 Static Reflection

Hand-rolling `NonNarrowConvertible` like this is good exercise for understanding how concepts compose, but later in the talk the speaker offered a more concise idea: instead of defining ourselves what "narrowing" means, just ask the compiler directly, "can you initialize a `T` with a value of type `S`?" This shift in perspective looks tiny, yet it solved a problem that had me stuck for a long time — our hand-rolled version isn't accurate for cases like `char*` to `std::string`, whereas if you ask the compiler "can `S` initialize `T`", the compiler knows the answer inside and out.

The speaker was also honest about one caveat, though: don't confuse this special case with the general methodology it is meant to illustrate. The construction technique we spent most of the earlier sections learning — composing small concepts into bigger ones — is the truly reusable weapon. The initialization version works purely because "can it initialize" happens to overlap heavily with "can it narrow" in this particular scenario. In other scenarios like assignment or comparison, you won't be that lucky, and you'll have to do the composition yourself, honestly and patiently. The tools in the toolbox are general; which scenario lets you take a shortcut is a matter of luck.

At the very end, the talk showed off something that would have been "completely impossible five years ago" — static reflection (P2996). Before C++26, if you needed to know which members a struct has, what each member is called, what its type is, and what its offset in memory is, macros were your only option — one typo and it fails silently, and you debug until you question your life choices. C++26 static reflection finally lets us ask the compiler directly, "what does this type look like?"

```cpp
// Written against the C++26 static reflection proposal P2996 R12
// Note: as of early 2026, no mainstream compiler fully implements this proposal; this code is for study reference
#include <meta>
#include <string_view>
#include <print>
#include <cstddef>
#include <array>

// Member descriptor: records the meta-information of one member
struct member_descriptor {
    std::string_view name;   // the member's name
    std::size_t offset;      // offset within the object
    std::size_t size;        // bytes this member occupies
};

// The core magic: generate an array of member descriptors for any type
template<typename T>
consteval auto get_layout() {
    // ^^T is the reflection operator: asks the compiler for T's meta-information
    // nonstatic_data_members_of returns std::vector<std::meta::info>
    auto members = std::meta::nonstatic_data_members_of(^^T);
    constexpr size_t N = members.size();

    std::array<member_descriptor, N> layout{};
    for (size_t i = 0; i < N; ++i) {
        layout[i] = {
            // identifier_of fetches the member name (provided the member has an identifier)
            .name   = std::meta::identifier_of(members[i]),
            // offset_of returns a member_offset struct; .bytes gives the byte offset
            .offset = static_cast<std::size_t>(std::meta::offset_of(members[i]).bytes),
            // size_of returns the number of bytes this member occupies
            .size   = std::meta::size_of(members[i])
        };
    }

    return layout;
}

// A struct for testing
struct Player {
    int id;
    float x;
    float y;
    double health;
    char name[32];
};

int main() {
    constexpr auto xd = get_layout<Player>();

    for (const auto& m : xd) {
        std::println("成员: {:<10} 偏移: {:>3} 字节  大小: {:>3} 字节",
                     m.name, m.offset, m.size);
    }

    return 0;
}
```

The output I got looks roughly like this (exact offsets may differ across platforms and compile options due to alignment):

```text
成员: id         偏移:   0 字节  大小:   4 字节
成员: x          偏移:   4 字节  大小:   4 字节
成员: y          偏移:   8 字节  大小:   4 字节
成员: health     偏移:  16 字节  大小:   8 字节
成员: name       偏移:  24 字节  大小:  32 字节
```

Note that `health` sits at offset 16, not 12 — that's memory alignment at work: `double` requires 8-byte alignment, so the compiler slipped 4 bytes of padding after `y`. Verifying something like this used to mean hand calculation or writing out `offsetof` macros one by one; now a single line of code spills it all out.

Thinking back to when we learned concepts: a concept is also, at heart, asking the compiler "what conditions does this type satisfy". But the questions concepts can ask are quite limited — "can it do addition", "can it be iterated", "can it be converted". Static reflection flings open the compiler's entire internal knowledge about the type: names, members, base classes, function signatures, template parameters... whatever you want, you take. I used to think templates were black magic; concepts made the black magic readable; static reflection makes the black magic composable. Down the road, reflection plus concepts — iterate over members at compile time, check each one against a specific constraint, then generate code accordingly — clean and crisp.

That said, although C++26 static reflection was voted into the C++26 working draft in mid-2025 (P2996), as of early 2026 no mainstream compiler fully implements it — GCC and Clang (Bloomberg's experimental [clang-p2996](https://github.com/bloomberg/clang-p2996) branch) are both under active development, but neither is complete. The code above is written against the P2996 R12 proposal specification and is for study reference — do not count on using it in production.

---

# A Concept Is Not Just a Label for Template Parameters — It Is Far More Flexible Than You Think

Honestly, for my first two years of learning concepts I treated them as syntactic sugar for "sticking labels on template parameters". Writing `template<std::integral T>` felt about the same as writing an SFINAE if-else, just a bit prettier. Only when I recently chewed through this material again did I realize how shallow my understanding was — a concept is essentially a compile-time function, and since it is a function, it can take multiple parameters, and even value parameters. That shift in understanding made me slap my thigh on the spot, because many constraints I used to think "can't be expressed with concepts" were never language limitations at all; I simply hadn't thought them through.

## Clearing Up a Misconception First: A Concept Is Not Limited to One Type Parameter

When I wrote concepts, almost all of them looked like this:

```cpp
template<typename T>
concept Addable = requires(T a, T b) {
    { a + b } -> std::convertible_to<T>;
};
```

One concept constrains one type — tidy and proper. But think about it: if a generic function takes two parameters of different types, is constraining each type separately enough? Say the signature is `template<typename T, typename U> void foo(T, U)`, and you constrain with `std::integral<T>` and `std::integral<U>` respectively. All that says is "T is an integer and U is an integer"; it says nothing about the relationship between T and U. But if they show up in the same function, they are probably related somehow — otherwise why put the two of them together?

The talk cited a figure: more than half of all concepts take more than one parameter. My first reaction was that the proportion sounded exaggerated, but when I went back and paged through my own project code, it turned out to be true — as soon as your generic code gets even slightly complex, cross-type constraint requirements are everywhere.

Here's a concrete example. Suppose I'm writing a serialization library and I need a concept expressing "a value of type T can be serialized into a buffer of type U":

```cpp
template<typename T, typename Buffer>
concept SerializableTo = requires(T value, Buffer& buf) {
    // Buffer must have a write method that can accept T's serialized result
    { buf.write(std::declval<const char*>(), std::declval<std::size_t>()) }
        -> std::same_as<std::size_t>;
    // It must be possible to compute the byte size of T once serialized
    { serialized_size(value) } -> std::convertible_to<std::size_t>;
};

// In use, the two types are constrained together
template<typename T, typename Buffer>
    requires SerializableTo<T, Buffer>
void serialize(const T& value, Buffer& buf) {
    auto size = serialized_size(value);
    // ... actual serialization logic
}
```

You see, if this concept could only take one parameter, you'd either have to split the constraints across two places (losing the information about how the types relate), or resort to some deeply awkward nested formulation. Multi-parameter concepts let you state outright "what relationship T and U must satisfy", and anyone reading the code can see at a glance that the two types aren't off doing their own thing.

## What Excites Me Even More: Concepts Can Take Value Parameters

This one I simply did not know about. I always assumed a concept's parameter list could only contain types (`typename T`) or things like template template parameters — turns out it can also take plain values. That means at compile time you can mix "type constraints" and "value constraints" in one expression, and what you write looks almost identical to ordinary code.

Suppose I'm writing network-related code and need a buffer with two hard requirements: first, it must hold at least k elements; second, its size must be a power of two (very common with memory pools and ring buffers, because modulo can then be replaced with a bitwise AND).

```cpp
#include <concepts>
#include <cstdint>
#include <type_traits>

// An ordinary compile-time function that checks for a power of two
// Key point: consteval means it can only run at compile time
consteval bool is_power_of_two(std::size_t n) {
    return n > 0 && (n & (n - 1)) == 0;
}

// The concept takes a type parameter S and a value parameter k
template<typename S, std::size_t k>
concept BufferSpace = requires(S buf) {
    // S must have a size() method returning something convertible to size_t
    { buf.size() } -> std::convertible_to<std::size_t>;
    // Value constraint 1: the size is at least k
    requires (S::size_value >= k);
    // Value constraint 2: the size must be a power of two
    requires is_power_of_two(S::size_value);
};
```

Then I define a few buffer types to test with:

```cpp
// A buffer of size 64 (64 is a power of two)
struct SmallBuffer {
    static constexpr std::size_t size_value = 64;
    constexpr std::size_t size() const { return size_value; }
};

// A buffer of size 100 (100 is not a power of two)
struct WeirdBuffer {
    static constexpr std::size_t size_value = 100;
    constexpr std::size_t size() const { return size_value; }
};

// A buffer of size 1024 (1024 is a power of two)
struct NetworkBuffer {
    static constexpr std::size_t size_value = 1024;
    constexpr std::size_t size() const { return size_value; }
};
```

Now let's constrain a template function with this concept:

```cpp
template<typename S>
    requires BufferSpace<S, 128>
void process_buffer(S& buf) {
    // At this point the compiler has already guaranteed:
    // 1. S has a size() method
    // 2. The size is >= 128
    // 3. The size is a power of two
    // So we can safely use bitwise AND as modulo here
    constexpr std::size_t mask = S::size_value - 1;
    // ... actual processing logic
}
```

Come on, let's run it and see how clear the error messages are:

```cpp
int main() {
    SmallBuffer small;
    // process_buffer(small);  // compile error: size_value(64) < 128

    WeirdBuffer weird;
    // process_buffer(weird);  // compile error: 100 is not a power of two

    NetworkBuffer net;
    process_buffer(net);       // compiles: 1024 >= 128 and 1024 is a power of two
}
```

I tried it under GCC 15: after uncommenting the `process_buffer(small)` line, the compiler's error message tells you directly that a constraint is not satisfied, and it points at the `requires (S::size_value >= k)` clause. With `static_assert` instead of a concept, you'd have to put it inside the function body, the failure would be reported from within the function, and once the call stack gets deep it becomes unreadable. Concepts hoist the constraint up to the signature, so the error points straight at the call site — the experience gap is very real.

Looking back at why this works — a concept declaration is essentially `template<...parameters...> concept Name = boolean-expression;`. That boolean expression is evaluated at compile time, and since it is a template parameter list anyway, `typename`, `int`, `std::size_t` and friends can all appear as parameter types. C++20 template parameters already supported non-type parameters; concepts simply inherit that machinery. So there is no special "concept value-parameter syntax" — it's just an ordinary template non-type parameter.

As for `is_power_of_two` being usable inside a concept's `requires` expression — that works because I declared it `consteval`. `consteval`, introduced in C++20, means "this function must execute at compile time; it cannot possibly be called at runtime". Inside a concept's constraint expression, that is exactly the kind of "guaranteed to finish at compile time" function you want, because concepts themselves are compile-time creatures.

Do concepts with value parameters actually get used in real development? My own experience: once you're writing library code or frameworks, you run into them constantly. Thread pools require the task queue size to be a power of two (bitwise-AND modulo optimization); memory allocators require block sizes aligned to some value; SIMD operations require vector lengths to be multiples of 4/8/16; protocol parsers require the buffer to hold at least one complete frame — in all these scenarios, "is the type right" and "is the value compliant" are interwoven. Before, I would either `assert` at runtime or scatter a pile of `static_assert`s across function bodies inside the template. Now, with value-parameter concepts, you can concentrate all the constraints in one place, expressed right at the interface signature.

At this point I finally understand why the perspective "a concept is a compile-time function" matters so much. If you treat it as "a label on a template parameter", your thinking gets boxed into "one concept constrains one type". But treat it as a function — one that can take multiple parameters, take value parameters, call other compile-time functions, and compose — and its expressive power turns out to be nearly as strong as ordinary code; the only difference is that the whole execution happens at compile time.

---

# Testing for Powers of Two: From a Tiny Algorithm to the Whole Saga of Generic Versus Object-Oriented Programming

## A Little Algorithm That Made Me Slap My Thigh

A couple of days ago I was grinding through a very basic problem: determine whether an integer is a power of two. What I'd always used was the clumsiest approach — keep dividing by 2 and check remainders, or, to be "fancier", compute a logarithm. But this time I saw a bit-manipulation version, and honestly, when I saw it I found it really clever, because the logic is just so clean.

The idea goes like this: if a number is a power of two, its binary representation contains exactly one 1, with all the rest 0s. For instance, 8 is `1000` and 32 is `100000`. So you just keep right-shifting, throwing away the last bit, while checking whether the bit you threw away was a 1. If you're left with exactly one 1 at the end, it's a power of two; if you hit any nonzero bit along the way, return false immediately; and if everything shifts out to 0, then 0 itself isn't a power of two either — also false.

I had always thought the bit-trick way to test for a power of two was the classic one-liner `n & (n - 1) == 0`, but that version has a pitfall — it reports 0 as true, so you need an extra `n != 0` check. The shifting version takes a few more lines, but its logic is fully self-consistent and needs no special-casing at all. I wrote a quick verification:

```cpp
#include <iostream>
#include <bitset>

// Test for a power of two by right-shifting
// Idea: a power of two has exactly one 1 in its binary representation
bool is_power_of_two_shift(unsigned int n) {
    if (n == 0) return false;  // 0 is not a power of two

    int count = 0;
    while (n > 0) {
        // Check whether the last bit is 1
        if (n & 1u) {
            count++;
            if (count > 1) return false;  // more than one 1, not a power of two
        }
        n >>= 1;  // shift right, discard the last bit
    }
    return count == 1;
}

// The classic n & (n-1) version; note 0 must be excluded
bool is_power_of_two_classic(unsigned int n) {
    return n != 0 && (n & (n - 1)) == 0;
}

int main() {
    unsigned int test_values[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 16, 31, 32, 63, 64, 127, 128, 255, 256};

    std::cout << "数值     二进制表示              移位法  经典法\n";
    std::cout << "----     ----------              ------  ------\n";
    for (unsigned int n : test_values) {
        std::cout << n << "\t" << std::bitset<8>(n) << "\t"
                  << (is_power_of_two_shift(n) ? "true " : "false")
                  << "  "
                  << (is_power_of_two_classic(n) ? "true " : "false")
                  << "\n";
    }
    return 0;
}
```

The results from the two methods match exactly, but the shifting logic reads more smoothly to me, because its "intent" and its "implementation" are perfectly aligned — it literally counts the 1s. `n & (n - 1)` is clever, but the first time you see it you genuinely need a moment to work out why it clears the lowest set bit. That said, the classic version really is faster, since it needs only one AND and one comparison, while the shifting version loops. So in real engineering I'd still use the classic one, but understanding the shifting approach genuinely helps build intuition for bit manipulation.

## Generic Programming vs Object-Oriented Programming: A Question I Wrestled With for a Long Time

With that little algorithm out of the way, I want to talk about something bigger, because this part finally cleared up a notion that had been blurry in my head for a long time — what exactly is the essential difference between generic programming and object-oriented programming?

When I started learning C++ in 2022, I learned classes and inheritance first and thought object orientation was all there was to C++. Later I bumped into templates, and the piles of angle brackets and compile errors gave me headaches — it felt like "black magic", to be avoided wherever possible. Still later I started on concepts and gradually discovered that generic programming could do far more than I had imagined, but one question never left me: when should I use which of these two things?

Now I've finally got it. The core difference fits in one sentence: **generic programming is more flexible, and it doesn't rely on indirect function calls**.

That "no indirect function calls" part is crucial. OOP polymorphism is implemented through the virtual function table (vtable): when you call a virtual function, the runtime first looks up the table and then jumps — an indirect call. Generic programming pins down types at compile time: what should be inlined gets inlined, what should be specialized gets specialized, and the generated code is as direct as hand-written code. So generic programming usually runs faster — that's not mysticism, it's decided by the underlying mechanism.

## My Painful Saga of Trying to Design a Container Base Class

Speaking of OOP's limits, I have to vent about a pit I dug myself. On a small project I once wanted to manage different container types uniformly, so quite naturally I thought: let me define a `Container` base class, and have `MyList` and `MyVector` inherit from it.

```cpp
// The "ideal" code I wrote back then — it never actually ran
class Container {
public:
    virtual int size() const = 0;
    virtual void push_back(int value) = 0;
    virtual int& operator[](int index) = 0;
    virtual void insert(int pos, int value) = 0;
    virtual void erase(int pos) = 0;
    // ...
};
```

Looks lovely, right? But it blew up the moment I actually wrote it. `std::list`'s `insert` and `std::vector`'s `insert` differ in behavioral details, `std::list` has `splice` while `std::vector` doesn't have it at all, and `std::vector` has `reserve` while `std::list` has no use for it. I tried to find a "common interface" in the base class covering every container's operation set, but their operation sets simply aren't the same, and neither are the constraints on them.

After days of wrestling, the endgame was either a big-everything interface (a pile of methods that just throw "not supported" in some subclasses) or a tiny-fragment one (nothing left but `size()`, at which point why have a base class at all). Eventually I gave up and switched to template functions for handling containers, and things became absurdly simple:

```cpp
#include <iostream>
#include <vector>
#include <list>
#include <concepts>
#include <print>

// Constraints for vector: needs random access, and can reserve
template <typename T>
concept RandomAccessContainer = requires(T t, typename T::value_type v, size_t n) {
    { t.size() } -> std::convertible_to<size_t>;
    { t[n] } -> std::same_as<typename T::reference>;
    { t.reserve(n) };
};

// Constraints for list: no random access needed, but push_back and splice capabilities required
// Note the !RandomAccessContainer<T> here — since std::vector satisfies both concepts,
// without the mutual exclusion, calling process_container(vec) would be an ambiguous overload
template <typename T>
concept SequenceContainer = !RandomAccessContainer<T> && requires(T t, typename T::value_type v) {
    { t.size() } -> std::convertible_to<size_t>;
    { t.push_back(v) };
    { t.front() } -> std::same_as<typename T::reference>;
};

// Handling for random access containers
void process_container(const RandomAccessContainer auto& c) {
    std::println("处理随机访问容器，大小: {}", c.size());
    // Subscript access works
    if (!c.empty()) {
        std::println("  第一个元素: {}", c[0]);
    }
}

// Handling for sequence containers
void process_container(const SequenceContainer auto& c) {
    std::println("处理序列容器，大小: {}", c.size());
    // Access via front()
    if (!c.empty()) {
        std::println("  第一个元素: {}", c.front());
    }
}

int main() {
    std::vector<int> vec{1, 2, 3, 4, 5};
    std::list<int> lst{10, 20, 30};

    process_container(vec);  // matches RandomAccessContainer
    process_container(lst);  // matches SequenceContainer

    return 0;
}
```

You see, with concepts I can impose different requirements on different container types instead of cramming them into one uniform base class. vector needs to support `operator[]` and `reserve`? Write a concept requiring exactly that. list doesn't need random access? Write another concept. Each type satisfies its own constraints and lands in its own function overload. Looking back, the principle is dead simple — **don't try to box everything into one fixed interface; match each type to the most suitable handling based on its own capabilities**.

## They Aren't Enemies; They're Partners

But I must stress one thing: don't take my praise of generic programming as a reason to write off object orientation wholesale. OOP has one scenario that generic programming can hardly replace: **open sets of types**. What's an open set of types? It's when you're writing code and have no idea which types will be added in the future. Take the drawing system of a GUI framework: you define a `Shape` base class with a `draw()` virtual function. Users can then write a `MyCustomShape` inheriting from `Shape` in their own code, and your framework handles the new type without recompilation. That "extend at runtime" capability is beyond generic programming, because templates must know all types at compile time.

So my takeaway is: **if you can enumerate all the types (or at least know them at compile time), use generic programming — better performance, more precise expression; if you need to extend the set of types dynamically at runtime, use OOP polymorphism**. They are complementary, not mutually exclusive.

## draw-all: One Problem, Two Solutions, Both Work

To verify this understanding, I wrote the classic "draw all the shapes" example in both styles, object-oriented and generic:

```cpp
#include <iostream>
#include <vector>
#include <memory>
#include <concepts>
#include <print>

// ============ The object-oriented way ============
class Shape {
public:
    virtual ~Shape() = default;
    virtual void draw() const = 0;
};

class Circle : public Shape {
public:
    void draw() const override {
        std::println("OOP: 绘制圆形");
    }
};

class Rectangle : public Shape {
public:
    void draw() const override {
        std::println("OOP: 绘制矩形");
    }
};

// The OOP draw_all: takes a range of Shape pointers
void draw_all_oop(const std::vector<std::unique_ptr<Shape>>& shapes) {
    for (const auto& s : shapes) {
        s->draw();  // virtual call — an indirect call
    }
}

// ============ The generic programming way ============

// Shape types that inherit from no base class at all
struct Triangle {
    void draw() const {
        std::println("Generic: 绘制三角形");
    }
};

struct Star {
    void draw() const {
        std::println("Generic: 绘制五角星");
    }
};

// Define a concept: all you need is a draw member function
template <typename T>
concept Drawable = requires(const T& t) {
    { t.draw() };
};

// The generic draw_all: takes a range of any type that has draw()
template <std::ranges::range R>
    requires Drawable<std::ranges::range_value_t<R>>
void draw_all_generic(const R& items) {
    for (const auto& item : items) {
        item.draw();  // direct call, resolved at compile time, inlinable
    }
}

int main() {
    std::println("=== 面向对象方式 ===");
    std::vector<std::unique_ptr<Shape>> oop_shapes;
    oop_shapes.push_back(std::make_unique<Circle>());
    oop_shapes.push_back(std::make_unique<Rectangle>());
    draw_all_oop(oop_shapes);

    std::println("\n=== 泛型编程方式 ===");
    std::vector<Triangle> triangles{Triangle{}, Triangle{}};
    std::vector<Star> stars{Star{}};
    draw_all_generic(triangles);
    draw_all_generic(stars);

    // Key point: the generic way handles types with virtual functions too!
    std::println("\n=== 泛型方式处理 OOP 类型 ===");
    std::vector<Circle> circles{Circle{}, Circle{}};
    draw_all_generic(circles);  // totally fine; Circle has draw()

    return 0;
}
```

Run it, and here's the output:

```text
=== 面向对象方式 ===
OOP: 绘制圆形
OOP: 绘制矩形

=== 泛型编程方式 ===
Generic: 绘制三角形
Generic: 绘制三角形
Generic: 绘制五角星

=== 泛型方式处理 OOP 类型 ===
OOP: 绘制圆形
OOP: 绘制圆形
```

Note that last case — `draw_all_generic` is a generic function, yet it handles object-oriented types with virtual functions like `Circle` perfectly well, because `Circle` really does have a `draw()` method and satisfies the `Drawable` concept. In other words, **generic programming plus concepts can cover everything a classic OOP class hierarchy can do**, while also handling types that live outside any class hierarchy at all (like `Triangle` and `Star`, which inherit from no base class whatsoever).

At this point I've finally untangled the whole thing. I used to think templates and concepts were "advanced play" while virtual functions and polymorphism were the "orthodox way"; looking back now, generic programming is actually the more expressive one, and because it needs no indirection, it also performs better. But OOP truly is irreplaceable when handling open sets of types. The two complement each other; picking by scenario is the right way to open this toolbox.

---

# Concepts Don't Need to Be Perfect on the First Try — Iteration in Practice Is What Sharpens Them

That statement makes for a great analogy — it's your LLM overthinking. Before you lift a finger, you spin up endless hypotheticals, trying to use computation to pin down a world that is inherently uncertain. The result: every time you set out to write a concept, you stare at the screen forever wondering "did I miss some constraint?", and in the end not even the first line of code gets written.

A concept is like a type-system "contract": once signed it can't be changed, so you must enumerate every constraint at the moment of writing — that's what a lot of people assume at first. For instance, when constraining a "numeric type", I'd start agonizing: should I add `std::is_copy_constructible`? `std::is_default_constructible`? `std::is_trivially_destructible`? The list grows and grows, until I scare myself off entirely.

In reality, a concept is just like the ordinary code we write: the first version exists to "get it working first". You don't need to account for every edge case on day one. Write down the constraints you genuinely need right now, and add more later when they turn out to be insufficient — that's completely fine.

## Writing a Number Concept from Scratch

For the full implementation of `Number<T>` and an in-depth discussion, see [the first article](01-type-safety-and-number-concept.md). Here I only want to show the concept's core skeleton, to illustrate the philosophy of "iterative evolution":

```cpp
#include <iostream>
#include <concepts>
#include <type_traits>

// Version one: only constrain the operations I actually use right now
// No copy, no move, no default construction — I don't need those yet
template<typename T>
concept Number = requires(T a, T b) {
    { a + b } -> std::convertible_to<T>;
    { a - b } -> std::convertible_to<T>;
    { a * b } -> std::convertible_to<T>;
    { a / b } -> std::convertible_to<T>;
    { -a }    -> std::convertible_to<T>;
};

// A function that only uses addition and subtraction — it needs only part of Number's capabilities
template<Number T>
T compute(T x, T y) {
    return (x + y) * 2 - y;
}
```

Look at it: this `Number` concept leaves out a pile of things — no constraints on `==` and `!=`, none on compound assignments like `+=`, none on `<<` for output, nothing at all. Yet for the `compute` function it is already completely sufficient. If tomorrow I write a new function that needs to compare two numbers for equality, I can write an `EqualityComparable` concept to constrain that function's parameters, rather than going back and bloating `Number` ever further.

Suppose that later I genuinely need a more complete notion of a number: I can extend the existing `Number` rather than tearing it down and starting over:

```cpp
// Version two: extend on top of Number; add comparison when it's needed
template<typename T>
concept ComparableNumber = Number<T> && requires(T a, T b) {
    { a == b } -> std::convertible_to<bool>;
    { a != b } -> std::convertible_to<bool>;
    { a < b }  -> std::convertible_to<bool>;
    { a <= b } -> std::convertible_to<bool>;
    { a > b }  -> std::convertible_to<bool>;
    { a >= b } -> std::convertible_to<bool>;
};

template<ComparableNumber T>
T clamp(T val, T lo, T hi) {
    if (val < lo) return lo;
    if (val > hi) return hi;
    return val;
}
```

This "constrain what you use" approach closely mirrors the typeclass idea from functional programming — you define a minimal set of orthogonal capability primitives and compose them where needed, instead of starting off with a "god concept" that stuffs everything inside.

## The Worry About Matching the Wrong Things

I worried about this too at first: if my `Number` concept only checks for the `+ - * /` operators, might some type that happens to have those operators — yet isn't a number at all — get matched by mistake?

The talk mentioned a classic example: `std::forward_iterator` and `std::input_iterator` are nearly identical in their syntactic constraints; their difference lives mainly at the semantic level — a forward iterator requires that traversing multiple times through the same iterator yields the same results, while an input iterator doesn't guarantee that. You cannot express this difference with purely syntactic constraints.

Then again, let's be realistic. A type that happens to implement `+ - * /` with return values that still convert back to its own type, yet "isn't a number" — the probability of that happening is extremely low. If a type really provides all five operators with exactly matching signatures, it already behaves like a number at the syntactic level; even if its semantics are "matrix" or "polynomial", it's fine to use in scenarios where you only need arithmetic.

What's more, concept-constrained name lookup is far safer than unconstrained name lookup. Once you constrain a function template's parameters with a concept, the compiler's overload resolution only considers candidates that satisfy the concept. That is far more reliable than the traditional SFINAE approach of hiding conditions in the return type via `std::enable_if`, because a concept is an explicit, named constraint: when the compiler reports an error it tells you outright "this type doesn't satisfy Number", instead of dumping fifty lines of template instantiation errors on you.

## The Complementary Relationship with OOP's Hierarchical Constraints

One more point clicked for me: concepts provide "flat" capability constraints, while OOP class hierarchies provide "structured" hierarchical constraints. The two aren't mutually exclusive — they complement each other.

Say you have a class hierarchy `Shape -> Circle / Rectangle` — structured, with inheritance. But you can also write `concept Drawable = requires(T t, std::ostream& os) { { os << t } -> std::same_as<std::ostream&>; };` — this concept doesn't care whether your type inherits from `Shape`; it only cares whether you can be streamed out. A `Circle` can simultaneously satisfy "is a Shape subclass" and "is Drawable"; the two constraints each do their own job in their own scenarios.

I always used to feel you had to pick a side — "either OOP or template generics". Looking back, that view was far too narrow. The tools in a toolbox aren't meant for picking exactly one.

---

# Concepts Aren't the Exclusive Property of Template Parameters — Something I Completely Overlooked

Honestly, this part of the material moved me. Ever since I started learning C++ in 2022, one impression has been deeply rooted in my head: concepts exist to constrain template parameters, written inside `template <concept_name T>`, end of story. Now it turns out concepts can be used entirely independently of template parameters — directly on the parameters of ordinary functions. That flung open a door I had never even seen before.

## First, a Word on That "Tail Wagging the Dog" Problem

Before diving in, I want to mention a viewpoint that resonated deeply with me. When discussing questions like "how do we distinguish forward iterators from input iterators", we often fall into a cart-before-horse mindset — to tell the two apart, we rack our brains inventing syntactic differences: add a tag to one of them, or a special member function, then write a concept to detect whether that tag exists. The whole design exists to solve that one particular problem, and it grows ever more complicated.

The right approach should really be: first produce the most elegant design for the general problem, and then, if a special case genuinely needs distinguishing, patch it with a small trick. The priorities must not be inverted.

## Start with the Simplest Example: Constraining Plain Function Parameters with a Concept

Let's start with a really basic example. Suppose I have a function that processes integers, and I want it to accept the standard integer types `short`, `int`, and `long`, but not the floating-point types `float` and `double`.

Following the traditional template line of thinking, you'd probably write:

```cpp
#include <type_traits>
#include <iostream>

// The traditional way: std::enable_if or static_assert
template <typename T>
void process_old(T val) {
    static_assert(std::is_integral_v<T>, "T must be an integral type");
    std::cout << "processing: " << val << "\n";
}

int main() {
    process_old(42);       // OK
    process_old(3.14);     // fails to compile, with a long, ugly error message
}
```

I've written this style countless times. The problem is the error message — what you see is a blob of template instantiation stack around a failed `static_assert`, which to a newcomer reads like scripture in an alien tongue.

Now switch to the concept way of thinking — and here's the key — **I don't have to write it as a template**:

```cpp
#include <concepts>
#include <iostream>

// Constrain a plain function's parameter with a concept, directly!
void process(std::integral auto val) {
    std::cout << "processing: " << val << "\n";
}

int main() {
    process(42);       // OK, int satisfies std::integral
    process(42L);      // OK, long satisfies std::integral
    // process(3.14);  // compile error, double doesn't satisfy std::integral
}
```

Did you notice? There's no `template` keyword here, no `typename T` — just a perfectly ordinary function, except the parameter type is written as `std::integral` instead of `int`. When the compiler sees the `std::integral` concept, it automatically treats it as a constraint and checks, during overload resolution, whether the passed-in type satisfies it.

The first time I saw this style, everything fell into place at once — so concepts can be used like this! This is essentially generic programming's syntax converging toward ordinary programming. When you write a function, the mental model shifts from "I'm writing a template" to "I'm writing a function whose parameter type is a concept" — a psychological shift that mattered enormously to me.

Of course you can also write it in template form; the effect is equivalent:

```cpp
#include <concepts>
#include <iostream>

// The template form, same effect
template <std::integral T>
void process_template(T val) {
    std::cout << "processing: " << val << "\n";
}

// The non-template form
void process_plain(std::integral auto val) {
    std::cout << "processing: " << val << "\n";
}

int main() {
    process_template(42);
    process_plain(42);
    // The compiler handles both calls in almost exactly the same way
}
```

In most scenarios there's no essential difference — the compiler performs the same overload resolution underneath. But the non-template form has a psychological advantage: when reading the code, what you see at first glance is an ordinary function, with no need to first mentally run through "this is a template, what will T be deduced as". The code's intent is more direct.

One little pitfall to warn you about, though: with the non-template form, you can't use the name `T` inside the function body, because you never declared a `T`. You'll need `decltype` or `auto`:

```cpp
#include <concepts>
#include <iostream>
#include <typeinfo>

void process(std::integral auto val) {
    // There's no T here, so we need auto or decltype
    auto doubled = val * 2;
    std::cout << "type: " << typeid(doubled).name()
              << ", value: " << doubled << "\n";
}

int main() {
    process(42);   // pass an int; doubled is an int too
    process(42L);  // pass a long; doubled is a long too
}
```

## The Scenario That Really Made It Click: Infrastructure Needs in Industrial Code

The integer example above is too simple — you might think "so what". What really made me understand the value of this feature was the industrial-software scenario raised in the talk.

During an internship I took part in a fairly large C++ project, and one impression cut deep: **production code and teaching code are two entirely different things**. The textbook `advance` function is three or four lines — nudge the iterator forward n steps, clean and crisp. But the `advance` in a real project, or core functions like it, gets stuffed with piles of things unrelated to the core logic — logging, debug assertions, correctness checks, telemetry collection, call-chain tracing... every new infrastructure requirement balloons the function by another circle.

Let's look at an example that simulates this scenario. Suppose I have a simplified `advance` that moves an iterator forward 2 steps:

```cpp
#include <iostream>
#include <vector>
#include <list>

// The textbook version: clean, but not enough
template <typename Iter>
void advance_by_2(Iter& it) {
    ++it;
    ++it;
}
```

Now back to the feature that concepts aren't limited to template parameters. If we constrain `advance_by_2`'s parameter with a concept, written in non-template form, we actually gain an important capability: **the function's "identity" in the type system becomes much clearer**. It's no longer a template open to all types, but a function with an explicit interface contract. That lays the groundwork for finer-grained dispatching and composition with concepts later on.

```cpp
#include <iostream>
#include <vector>
#include <list>
#include <concepts>

// Constrain the parameter with a concept, stating plainly "this function takes random access iterators"
void advance_by_2(std::random_access_iterator auto& it) {
    it += 2;  // random access iterators support += directly
}

// Same-named function taking input iterators (which can only step one at a time)
// Note: random access iterators must be excluded; otherwise, when std::random_access_iterator<T> holds,
// both overloads match and we get ambiguity
template <typename T>
    requires std::input_iterator<T> && (!std::random_access_iterator<T>)
void advance_by_2(T& it) {
    ++it;
    ++it;
}

int main() {
    std::vector<int> vec = {1, 2, 3, 4, 5};
    std::list<int> lst = {1, 2, 3, 4, 5};

    auto vit = vec.begin();
    auto lit = lst.begin();

    advance_by_2(vit);  // calls the random access version
    advance_by_2(lit);  // calls the input iterator version

    std::cout << *vit << "\n";  // prints 3
    std::cout << *lit << "\n";  // prints 3
}
```

The first function here uses the abbreviated syntax `std::random_access_iterator auto&` (the concept shorthand C++20 allows). The second one, because it must exclude random access iterators (to avoid both overloads matching and creating ambiguity), switches to the full template + `requires` form, adding `!std::random_access_iterator<T>` to the constraints to guarantee mutual exclusion. Two same-named functions achieve overloading through different concept constraints — random access iterators take the `+= 2` fast path, ordinary input iterators take the two-`++` slow path. This is the more elegant overloading mechanism concepts buy us.

## A Misunderstanding I Used to Hold

At this point I have to confess a misunderstanding I used to hold. When I first learned concepts, I thought their greatest value was "making template error messages prettier". True, concept error messages are a hundred times nicer than `enable_if` ones — but if that's all you see, you're badly underselling concepts.

The real value of concepts is that **they changed how generic programming is thought about**. Writing templates before, my thinking was "here's a type parameter; let me add a constraint"; with concepts, my thinking becomes "here I need something that satisfies certain semantics". The shift from "type parameter" to "semantic requirement" looks subtle, but it shapes your entire design.

Take the `advance_by_2` example above: what I wrote isn't "a template function taking a `T`", but "a function taking random access iterators" and "a function taking input iterators". The code's intent is lifted from the implementation-detail level to the semantic level.

## The Misconception About "Compilation in Isolation"

Many people (the speaker originally included) believed generic functions must be compilable in isolation — that is, type checking should be completable by looking at the function definition alone, without the calling context. Later it was recognized that this is neither what we really need nor what concepts provide.

I had this misconception too. I felt a good generic function should be "self-contained", able to justify its type requirements on its own. But think it through and it's over-demanding. A generic function's constraints should describe "what I need", not "I can handle everything". Whether the type passed at some specific call site satisfies them is a contract check between the call site and the function's constraints — not the function's own business. We'll go deeper into the template compilation model and type checking in [the fourth article](04-template-compilation-and-future.md).

The example of constraining a parameter with `std::integral` is the best illustration: the function only declares "I need an integer"; whether you pass an `int` or a `long` is your business. The function doesn't need to know, in isolation, every possible integer type.

At this point this use of concepts has fully clicked for me. It isn't just "a better `enable_if`"; it's a tool that lets you think about interfaces semantically. And it isn't confined to template parameters — you can use it directly on ordinary function parameters, which brings generic programming's syntax one big step closer to ordinary programming. Looking back, it really isn't that hard; I had simply been viewing it all along through the tinted glasses of "concept = syntactic sugar for template constraints".
