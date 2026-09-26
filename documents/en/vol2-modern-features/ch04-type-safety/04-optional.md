---
chapter: 4
cpp_standard:
- 17
- 23
description: Use `optional` to replace sentinel values and raw pointers, safely expressing
  optional semantics
difficulty: intermediate
order: 4
platform: host
prerequisites:
- 'Chapter 4: std::variant: A Type-Safe Union'
reading_time_minutes: 13
related:
- Modern Approaches to Error Handling
tags:
- host
- cpp-modern
- intermediate
- optional
- 类型安全
title: 'std::optional: Elegantly Expressing ''A Value May Be Absent'''
translation:
  source: documents/vol2-modern-features/ch04-type-safety/04-optional.md
  source_hash: cca7dab1aeeb453ad244119796bece77e99f2d5d6feee48f6072d4e858119d94
  translated_at: '2026-09-25T15:28:59+00:00'
  engine: anthropic
  token_count: 2400
---
# std::optional: Elegantly Expressing 'A Value May Be Absent'

We've written way too much code like this: a function returns `-1` to mean "not found", returns `nullptr` to mean "something went wrong", returns an empty string to mean "config entry doesn't exist". These conventions feel perfectly natural while you're writing them; three months later, looking back, you break out in a cold sweat — is that `-1` "not found", or an honest-to-goodness -1? Is that `nullptr` "an optional empty value" or "an error"? Every function that returns a magic value is planting a landmine for your future self.

`std::optional` (introduced in C++17) exists to solve exactly this problem: how do you safely express "there may be no value"? It encodes "has a value or doesn't" into the type system — both the compiler and the caller can see straight from the function signature that "this return value may be empty", with no need for comments or documentation to convey it.

## Step 1 — The Traditional Approaches to "A Value May Be Absent"

Before `optional` came along, C++ programmers mainly had these ways to express "there may be no value":

**Special values (sentinel values)**: Use some specific value to mean "invalid". `-1` means the lookup failed, `UINT_MAX` means an invalid index, an empty string means not configured. The problem: every function's "special value" is different, and callers have to memorize all these conventions. Worse, some types simply have no suitable special value — for a `double`, `-1.0` is a perfectly legitimate return value.

**Raw pointers**: Return `nullptr` to mean "no value". Very common in lookup functions. The problem: a pointer's semantics are far too broad. A `T*` can mean "an optional value that may be null", or "a non-owning observing pointer", or "a pointer to a dynamically allocated object". Callers can't tell these semantics apart from the type alone. Even more dangerous: dereferencing a null pointer is UB, and it won't give you any friendly error message.

**std::pair<T, bool>**: The second element says whether the value is valid. A bit better than the previous two, but tedious to use — you have to check `.second` every single time, and when `second == false`, the value of `first` is undefined (default construction may not even be legal).

```cpp
// Comparing the three traditional approaches
int find_index_old(const std::vector<int>& v, int target)
{
    for (int i = 0; i < static_cast<int>(v.size()); ++i) {
        if (v[i] == target) return i;
    }
    return -1;  // Sentinel-value convention: callers must remember that -1 means not found
}

int* find_ptr_old(std::vector<int>& v, int target)
{
    for (auto& x : v) {
        if (x == target) return &x;
    }
    return nullptr;  // Raw pointer: ambiguous semantics
}

std::pair<int, bool> find_pair_old(const std::vector<int>& v, int target)
{
    for (int i = 0; i < static_cast<int>(v.size()); ++i) {
        if (v[i] == target) return {i, true};
    }
    return {0, false};  // first's value is meaningless here
}
```

All three approaches share one common flaw: **the type signature does not express the "may have no value" semantics**. A return type of `int` doesn't tell you that `-1` is a sentinel; an `int*` doesn't tell you that `nullptr` means "not found" rather than "error". `std::optional` fixes this at the type level, directly.

## Step 2 — Core Semantics and API of optional

`std::optional<T>` means "either holds a value of type `T`, or holds nothing at all". It is a value type (not a pointer); the contained object is embedded directly in the `optional`'s internal storage — no dynamic memory allocation.

### Construction

```cpp
#include <optional>
#include <string>
#include <iostream>

std::optional<int> a;                      // Empty (holds no value)
std::optional<int> b = 42;                 // Holds 42
std::optional<int> c = std::nullopt;       // Explicitly empty
std::optional<std::string> d = "hello";    // Holds "hello"

// Construct in place (avoids a temporary)
std::optional<std::string> e(std::in_place, 10, 'x');  // "xxxxxxxxxx"
```

### Checking and Accessing

```cpp
std::optional<int> opt = 42;

// Check whether it holds a value
if (opt.has_value()) { /* ... */ }
if (opt) { /* ... */ }             // Equivalent implicit bool conversion

// Access the value
int x = *opt;                       // Dereference (unchecked — UB if empty)
int y = opt.value();                // Throws std::bad_optional_access if empty
int z = opt.value_or(0);            // Returns the default 0 if empty

// Access members (for class types)
std::optional<std::string> name = "Alice";
if (name) {
    std::cout << "length: " << name->size() << "\n";  // operator->
}
```

On choosing between `operator*` and `value()`, our advice: on code paths where you have **already checked** `has_value()`, `*opt` is all you need — better performance and clearer semantics. When you **haven't checked**, `value()` is safer — it throws an exception instead of UB. But neither is as elegant as `value_or()`, because the latter handles the "what if it's empty" question head-on.

### The Beauty of value_or

`value_or()` is one of `optional`'s most practical APIs. It takes a default value as its argument: if the `optional` holds a value, it returns the held value; otherwise it returns the default:

```cpp
std::optional<std::string> get_config(const std::string& key);

// Read the config; use the default when not configured
std::string host = get_config("server_host").value_or("localhost");
int port = get_config("server_port")
    .transform([](const std::string& s) { return std::stoi(s); })
    .value_or(8080);
```

That `transform` above is a C++23 feature; we'll cover it in detail shortly.

## Step 3 — Memory Layout of optional

An `optional<T>`'s internal storage typically consists of two parts: an aligned buffer to hold the `T`, plus a `bool` flag indicating whether a value is present. This means `sizeof(std::optional<T>)` is usually larger than `sizeof(T)`.

```cpp
#include <optional>

std::cout << "sizeof(int):              " << sizeof(int) << "\n";            // 4
std::cout << "sizeof(optional<int>):    " << sizeof(std::optional<int>) << "\n";    // Typical: 8
std::cout << "sizeof(double):           " << sizeof(double) << "\n";         // 8
std::cout << "sizeof(optional<double>): " << sizeof(std::optional<double>) << "\n"; // Typical: 16
std::cout << "sizeof(string):           " << sizeof(std::string) << "\n";    // Typical: 32
std::cout << "sizeof(optional<string>): " << sizeof(std::optional<std::string>) << "\n"; // Typical: 40
```

Let's draw those two parts (taking `optional<int>` as an example, one cell for the has-a-value case and one for the empty case):

![Memory layout of std::optional: an aligned buffer plus a bool flag](./04-optional-layout.drawio)

The actual `sizeof` results depend on the standard library implementation and the platform's alignment requirements. But the core fact holds: `optional<T>` is roughly one aligned `bool` bigger than `T`. Due to alignment requirements, it sometimes grows a bit more than you'd expect. This is not a design flaw of `optional` — it stores the `T` value directly on the stack with no heap allocation involved, so the extra overhead is justified.

The object an `optional` holds and the "has a value" flag live inside the same object, with no dynamic memory allocation anywhere. On destruction, if the `optional` holds a value, `T`'s destructor is called automatically. All of this is automatic — no manual management required.

## Step 4 — optional vs Pointers

Both `optional<T>` and `T*` can express "there may be no value", but their semantics are completely different.

`optional<T>` has value semantics — it holds (or intends to hold) a complete `T` object. Copying the `optional` copies the `T` value (if one is held); destroying the `optional` destroys the `T`. What it expresses is "here is a `T`, or for the moment there isn't one".

`T*` has reference semantics — it points at some external `T` object (or is null). Copying the pointer copies only the address, not the object itself. What it expresses is "somewhere out there is a `T`, and I may point at it".

```cpp
std::optional<int> opt = 42;
int* ptr = &opt.value();  // Points at the int inside the optional

opt = 123;                // The optional is reassigned; the old 42 is destroyed
// ptr may now point at 123 (implementation-dependent), or may dangle — don't do this

std::optional<int> opt2 = opt;  // Copy: opt2 is an independent copy, holding 123
int* ptr2 = &raw;               // Assume raw is some int variable
std::optional<int> opt3 = *ptr2;  // Copies the value ptr2 points to — unrelated to ptr2
```

Our rule of thumb: **if you need to express "a value that may or may not exist", use `optional`; if you need to express "a nullable reference to some external object", use a pointer**. Don't use `optional` to imitate a pointer, and don't use a pointer to imitate `optional` — they have different jobs.

## Step 5 — optional as a Return Value

`optional`'s most common use is as a function return type. Its semantics are completely explicit: the function may return a valid value, or it may return "no value". The caller must handle the "no value" case at the type-system level.

### Lookup Operations

```cpp
#include <optional>
#include <vector>
#include <string>

std::optional<std::size_t> find_index(
    const std::vector<int>& v, int target)
{
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (v[i] == target) return i;
    }
    return std::nullopt;
}

// Caller side
auto idx = find_index(data, 42);
if (idx) {
    std::cout << "found at index " << *idx << "\n";
} else {
    std::cout << "not found\n";
}
```

Compared with the earlier version that used `-1` as a sentinel, `optional`'s advantage is that the caller **cannot possibly forget** to check the return value. If you write `data[*find_index(data, 42)]` directly without checking `has_value()`, dereferencing in the empty case is UB — but at least the API's design intent is unambiguous: the type signature has already told you "this value may be empty".

### Factory Functions

```cpp
class Connection {
public:
    static std::optional<Connection> create(const std::string& addr)
    {
        // Try to establish a connection
        if (addr.empty()) return std::nullopt;  // Invalid parameter
        // ... actual connection logic
        return Connection(addr);
    }

private:
    explicit Connection(std::string addr) : addr_(std::move(addr)) {}
    std::string addr_;
};

// Usage
auto conn = Connection::create("192.168.1.1");
if (conn) {
    // Connection succeeded
} else {
    // Connection failed
}
```

## Step 6 — optional as a Parameter

`optional` also works as a function parameter, meaning "this parameter is optional". This is more flexible than function overloading or default arguments, because the caller can decide at runtime whether to supply a value:

```cpp
void print_greeting(const std::string& name,
                    std::optional<std::string> title = std::nullopt)
{
    if (title) {
        std::cout << "Hello, " << *title << " " << name << "!\n";
    } else {
        std::cout << "Hello, " << name << "!\n";
    }
}

print_greeting("Alice");                    // Hello, Alice!
print_greeting("Bob", std::string("Dr."));  // Hello, Dr. Bob!
```

One word of caution, though: don't overuse `optional` parameters. If a parameter is needed in most cases, a default value may be a better fit than `optional`. `optional` parameters suit scenarios where "sometimes it's there, sometimes it isn't, and the two cases mean completely different things".

## Step 7 — A Preview of the C++23 Monadic Operations

C++23 introduced three monadic operations for `std::optional`: `and_then`, `transform`, and `or_else`. Borrowed from functional programming, these operations make chained processing of `optional` far more elegant.

### transform: Transforming the Value

`transform` takes a function: if the `optional` holds a value, it applies that function to the value and returns an `optional` containing the transformed result; if the `optional` is empty, it returns an empty `optional`.

```cpp
std::optional<int> parse_int(const std::string& s)
{
    try {
        return std::stoi(s);
    } catch (...) {
        return std::nullopt;
    }
}

// C++20 style: check manually
std::optional<std::string> input = get_input();
std::optional<int> result;
if (input) {
    result = parse_int(*input);
}

// C++23 style: chained transform
auto result2 = get_input().transform([](const std::string& s) -> int {
    return std::stoi(s);  // Simplified example; real code should handle exceptions
});
```

### and_then: Chaining Operations That May Fail

`and_then` takes a function that returns an `optional`. If the current `optional` holds a value, it calls that function and returns its result; otherwise it returns an empty `optional` directly. This is a better fit than `transform` for scenarios where "the previous step's result is the next step's input, and every step may fail".

```cpp
std::optional<User> find_user(int id);
std::optional<std::string> get_email(const User& u);

// C++20 style: nested ifs
auto user = find_user(42);
if (user) {
    auto email = get_email(*user);
    if (email) {
        std::cout << "Email: " << *email << "\n";
    }
}

// C++23 style: chained and_then
find_user(42)
    .and_then(get_email)
    .transform([](const std::string& email) {
        std::cout << "Email: " << email << "\n";
        return email;
    });
```

### or_else: Handling the Empty Case

`or_else` takes a function that is invoked when the `optional` is empty. It is typically used for logging or providing a fallback:

```cpp
auto email = find_user(42)
    .and_then(get_email)
    .or_else([] {
        std::cerr << "Failed to get email\n";
        return std::optional<std::string>("fallback@example.com");
    });
```

Combine these three operations and you can write very fluent chained code, avoiding multiple levels of nested `if` statements. If your compiler doesn't support C++23 yet, you can refer back to the earlier `optional_map` helper function to achieve a similar effect.

## Practical Application — Lazy Initialization

`optional` can also be used to implement lazy initialization: construction of the object is deferred until it is actually needed. This is extremely useful when constructing the object is expensive, but whether it is needed can't be determined at compile time:

```cpp
class ExpensiveResource {
public:
    ExpensiveResource() { /* Time-consuming initialization */ }
    void do_work() { /* ... */ }
};

class Service {
public:
    void process()
    {
        if (!resource_) {
            resource_.emplace();  // Construct on first use
        }
        resource_->do_work();
    }

private:
    std::optional<ExpensiveResource> resource_;  // Initially empty
};
```

This beats lazy initialization via `std::unique_ptr`, because `optional` involves no heap allocation — the object is stored directly in the `optional`'s internal buffer.

## Embedded in Practice — Config Entries and Sensor Reads

In embedded systems, sensor data can't be read successfully every time (the sensor may not be ready, the bus may time out), and config entries don't always exist. `optional` expresses these "may fail" operations elegantly:

```cpp
#include <optional>
#include <cstdint>

struct SensorReading {
    float temperature;
    uint32_t timestamp;
};

class TemperatureSensor {
public:
    std::optional<SensorReading> read()
    {
        if (!is_ready()) return std::nullopt;

        SensorReading r;
        r.temperature = read_raw_value() * kScale;
        r.timestamp = get_tick();
        return r;
    }

private:
    bool is_ready();
    float read_raw_value();
    uint32_t get_tick();

    static constexpr float kScale = 0.0625f;
};

// Usage
void print_temperature(TemperatureSensor& sensor)
{
    auto reading = sensor.read();
    if (reading) {
        std::printf("Temp: %.1f C (at %u)\n",
                    reading->temperature,
                    static_cast<unsigned>(reading->timestamp));
    } else {
        std::printf("Sensor not ready\n");
    }
}
```

The value of `optional` in this scenario is that it encodes "read failed" as part of the return type. The caller cannot possibly forget to handle the read-failure case — because you must check `has_value()` before you can access the temperature value. That is far safer than returning `0.0f` and relying on the caller to "remember that 0.0 might mean failure".

## References

- [cppreference: std::optional](https://en.cppreference.com/w/cpp/utility/optional)
- [cppreference: std::bad_optional_access](https://en.cppreference.com/w/cpp/utility/optional/bad_optional_access)
- [C++23 Monadic operations for std::optional](https://en.cppreference.com/w/cpp/utility/optional)
- [C++ Core Guidelines: Optional](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines)
