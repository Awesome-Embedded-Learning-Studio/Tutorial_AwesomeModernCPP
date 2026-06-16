---
chapter: 4
cpp_standard:
- 17
- 23
description: Use `optional` to replace special values and raw pointers, safely expressing
  optional semantics.
difficulty: intermediate
order: 4
platform: host
prerequisites:
- 'Chapter 4: std::variant'
reading_time_minutes: 13
related:
- 错误处理的现代方式
tags:
- host
- cpp-modern
- intermediate
- optional
- 类型安全
title: 'std::optional: Elegantly Expressing ''Possibly No Value'''
translation:
  source: documents/vol2-modern-features/ch04-type-safety/04-optional.md
  source_hash: 023bedb493fc224460a40b28ab6a7c83a8beeb85a1497fe7d4e791d11f9aaf5d
  translated_at: '2026-06-16T03:57:42.500130+00:00'
  engine: anthropic
  token_count: 2834
---
# std::optional: Elegantly Expressing "A Value May Be Absent"

## Introduction

I have written too much code like this: returning `-1` to mean "not found", returning `nullptr` to mean "error", or returning an empty string to mean "configuration item does not exist". These conventions seem reasonable when writing them, but looking back three months later, I start to break into a cold sweat—does `-1` mean "not found" or did it actually return `-1`? Is `""` an "optional empty value" or an "error"? Every function returning a special value is laying a trap for my future self.

`std::optional` (introduced in C++17) exists to solve the problem of "how to safely express the potential absence of a value." It encodes the information of "has value or has no value" directly into the type system—the compiler and the caller can see immediately from the function signature that "this return value may be empty," without relying on comments or documentation.

## Step 1 — Traditional Approaches to "Possibly No Value"

Before `std::optional` appeared, C++ programmers mainly used the following methods to express "possibly no value":

**Special Values (Sentinel Values)**: Use a specific value to represent "invalid." `-1` indicates a failed search, `npos` indicates an invalid index, and an empty string indicates unconfigured. The problem is that the "special value" differs for every function, and the caller must remember these conventions. Furthermore, some types simply don't have a suitable special value—for example, `double`'s `NaN` could be a perfectly valid return value.

**Raw Pointers**: Return `nullptr` to mean "no value." This is common in lookup functions. The problem is that pointer semantics are too broad. `T*` can mean "nullable optional value," "non-owning observer pointer," or "pointing to a dynamically allocated object." The caller cannot distinguish these semantics from the type alone. Even more dangerous, dereferencing a null pointer is UB (undefined behavior), which doesn't give you any friendly error messages.

**std::pair<T, bool>**: The second element indicates "whether the value is valid." This is slightly better than the previous two approaches, but it is verbose to use—you have to check `.second` every time, and the value of `.first` is undefined when `.second` is `false` (default construction might not be valid).

```cpp
// Sentinel value approach
int find_index(const std::vector<int>& vec, int target);

// Raw pointer approach
const User* find_user(const std::string& name);

// std::pair approach
std::pair<User*, bool> try_get_user(const std::string& name);
```

These three approaches share a common flaw: **the type signature does not express the semantic "possibly no value."** The return type of `int` won't tell you that `-1` is a special value, `T*` won't tell you if `nullptr` represents "not found" or "error," and `std::pair` is just clumsy. `std::optional` solves this problem directly at the type level.

## Step 2 — Core Semantics and API of optional

`std::optional<T>` represents "either holding a value of type `T`, or holding nothing." It is a value type (not a pointer), and the held object is directly embedded within `std::optional`'s internal storage—no dynamic memory allocation is involved.

### Construction

```cpp
std::optional<int> empty_opt;              // No value
std::optional<int> opt = 42;               // Holds 42
std::optional<int> opt2 = std::nullopt;    // Explicitly no value
```

### Checking and Accessing

```cpp
if (opt) {
    // Has value
    std::cout << *opt << std::endl;        // Dereference operator
    std::cout << opt.value() << std::endl; // Member function
} else {
    // No value
}
```

⚠️ Regarding the choice between `operator*` and `.value()`, my advice is: use `operator*` in code paths where you have **already checked** `has_value()`. It offers better performance and clear semantics. In situations where you have **not checked**, `.value()` is safer—it throws an exception rather than resulting in UB. However, neither approach is as elegant as `value_or`, as the latter directly handles the "what to do if empty" problem.

### The Magic of value_or

`value_or` is one of `std::optional`'s most practical APIs. It accepts a default value argument; if the `optional` has a value, it returns the held value; otherwise, it returns the default value:

```cpp
// C++17/20 style
int timeout = config.get_timeout().value_or(1000);

// C++23 style (allows lazy evaluation of the default)
int timeout = config.get_timeout().value_or_else([]{
    return calculate_default_timeout();
});
```

The `value_or_else` above is a new feature in C++23, which we will detail later.

## Step 3 — Memory Layout of optional

The internal storage of `std::optional` usually consists of two parts: an aligned buffer for storing the `T` value, plus a `bool` flag indicating whether a value is present. This means `sizeof(std::optional<T>)` is usually larger than `sizeof(T)`.

```cpp
struct A { int x; };
struct B { int x; int y; };
struct C { std::array<int, 100> data; };

std::cout << sizeof(A) << "\n";                     // 4
std::cout << sizeof(std::optional<A>) << "\n";      // 8 (4 + alignment padding)

std::cout << sizeof(B) << "\n";                     // 8
std::cout << sizeof(std::optional<B>) << "\n";      // 16 (8 + alignment padding)

std::cout << sizeof(C) << "\n";                     // 400
std::cout << sizeof(std::optional<C>) << "\n";      // 408 (400 + alignment padding)
```

The actual `sizeof` result depends on the standard library implementation and the platform's alignment requirements. But the core fact is: `std::optional<T>` is approximately the size of `T` plus one aligned `bool`. Due to alignment requirements, sometimes the overhead is higher than expected. This is not a design flaw of `std::optional`—it stores the `T` value directly on the stack without involving heap allocation, so this extra overhead is reasonable.

The object held by `std::optional` and the "has value" flag are inside the same object, involving no dynamic memory allocation. Upon destruction, if the `optional` holds a value, `T`'s destructor is called automatically. All of this is automatic, requiring no manual management.

## Step 4 — Differences Between optional and Pointers

`std::optional<T>` and `T*` can both express "possibly no value," but their semantics are drastically different.

`std::optional<T>` is value semantics—it holds (or intends to hold) a complete `T` object. Copying an `optional` copies the `T` value (if present), and destroying an `optional` destroys the `T`. It expresses "there is a `T` here, or temporarily there isn't."

`T*` is reference semantics—it points to some external `T` object (or is null). Copying the pointer just copies the address; it does not copy the object itself. It expresses "there is a `T` somewhere, and I may point to it."

```cpp
// Value semantics: The optional owns the data
std::optional<std::string> opt_name = get_name();
// Copies the string data

// Reference semantics: The pointer observes external data
const std::string* ptr_name = get_name_ptr();
// Only copies the address
```

My general principle is: **if you need to express "a value may or may not exist," use `std::optional`; if you need to express "a nullable reference to an external object," use a pointer.** Don't use `std::optional` to simulate pointers, and don't use pointers to simulate `std::optional`—they have different responsibilities.

## Step 5 — optional as a Return Value

The most common use for `std::optional` is as a function return value. Its semantics are very clear: the function may return a valid value, or it may return "no value." The caller must handle the "no value" case at the type system level.

### Lookup Operations

```cpp
std::optional<User> find_user(std::string_view name) {
    auto it = std::find_if(users.begin(), users.end(), [&](const User& u) {
        return u.name == name;
    });

    if (it != users.end()) {
        return *it;  // Implicit conversion to std::optional<User>
    }
    return std::nullopt;  // Explicitly empty
}
```

Compared to the previous version using `-1` as a sentinel value, the advantage of `std::optional` is that the caller **cannot possibly forget** to check the return value. If you write `*opt` directly without checking `has_value()`, dereferencing on an empty value is UB, but at least the API design intent is clear—the type signature has already told you "this value may be empty."

### Factory Functions

```cpp
std::optional<Device> create_device(const std::string& id) {
    if (!is_id_valid(id)) {
        return std::nullopt;
    }
    return Device(id);  // Move construction
}
```

## Step 6 — optional as an Argument

`std::optional` can also be used as a function parameter to indicate "this parameter is optional." This is more flexible than function overloading or default parameters, as the caller can decide at runtime whether to provide a value:

```cpp
void set_timeout(std::optional<int> ms) {
    if (ms) {
        configure_timeout(*ms);
    } else {
        use_default_timeout();
    }
}

// Usage
set_timeout(100);   // Set specific timeout
set_timeout(std::nullopt);  // Use default
```

However, I must offer a warning: don't overuse `std::optional` parameters. If a parameter is required in most cases, using a default value might be more appropriate than `std::optional`. `std::optional` parameters are best suited for scenarios where "sometimes it's there, sometimes it isn't, and the two cases mean completely different things."

## Step 7 — Preview of C++23 Monadic Operations

C++23 introduces three monadic operations for `std::optional`: `transform`, `and_then`, and `or_else`. Borrowing concepts from functional programming, these operations make chaining `optional` processing more elegant.

### transform: Transforming the Value

`transform` accepts a function. If the `optional` has a value, it uses this function to transform the value and returns an `optional` containing the result; if the `optional` is empty, it returns an empty `optional`.

```cpp
std::optional<int> parse_id(std::string_view str);

std::optional<User> get_user(std::string_view id_str) {
    return parse_id(id_str).transform([](int id) {
        return database.find_user(id);
    });
}
```

### and_then: Chaining Operations That May Fail

`and_then` accepts a function that returns an `std::optional`. If the current `optional` has a value, it calls this function and returns its result; otherwise, it directly returns an empty `optional`. This is more suitable than `transform` for scenarios where "the result of the previous step is the input for the next, and each step might fail."

```cpp
std::optional<Config> load_config(std::string_view path) {
    return read_file(path)          // Returns std::optional<std::string>
        .and_then(parse_json);      // Returns std::optional<json>
        .and_then(validate_config); // Returns std::optional<Config>
}
```

### or_else: Handling the Empty Case

`or_else` accepts a function that is called when the `optional` is empty. It is typically used for logging or providing an alternative:

```cpp
opt_value.or_else([]{
    std::cerr << "Warning: Value not available, using fallback.\n";
    return std::optional<Value>{fallback_value};
});
```

Combining these three operations allows you to write very fluent chain code, avoiding deeply nested `if` statements. If your compiler doesn't support C++23 yet, you can refer to the previous helper function `map_optional` to achieve similar effects.

## Practical Application — Lazy Initialization

`std::optional` can also be used to implement lazy initialization: deferring the construction of an object until it is actually needed. This is very useful when object construction is expensive, but "whether it is needed" cannot be determined at compile time:

```cpp
class ExpensiveObject {
    // ...
};

class Manager {
public:
    void do_work() {
        // Initialize only on first use
        if (!worker) {
            worker.emplace();  // In-place construction
        }
        worker->process();
    }

private:
    std::optional<ExpensiveObject> worker;
};
```

This is superior to using `std::unique_ptr` for lazy initialization, because `std::optional` involves no heap allocation—the object is stored directly in the buffer inside the `optional`.

## Embedded in Practice — Configuration Items and Sensor Reading

In embedded systems, sensor data cannot always be read successfully (sensors may not be ready, the bus may time out), and configuration items may not always exist. `std::optional` can elegantly express these "operations that may fail":

```cpp
std::optional<float> read_temperature() {
    if (sensor_ready()) {
        return adc_read_temperature();  // Returns float
    }
    return std::nullopt;  // Sensor not ready
}

// Usage
auto temp = read_temperature();
if (temp) {
    update_display(*temp);
} else {
    show_error("Sensor offline");
}
```

The value of `std::optional` in this scenario is that it encodes "read failure" into the return type. The caller cannot possibly forget to handle the "read failure" case—because you must check `has_value()` before accessing the temperature value. This is much safer than returning a `float` and relying on the caller to "remember that 0.0 might indicate failure."

## Summary

`std::optional` is the standard way in C++17 to express "possibly no value." It is safer than sentinel values (won't be confused with legal values), has clearer semantics than raw pointers (value semantics vs reference semantics), and is more elegant than `std::pair` (API designed specifically for this).

The core API of `std::optional` is very concise: `has_value()` to check, `operator*` to dereference, and `value_or` to provide a default value. It involves no dynamic memory allocation; objects are stored directly inside the `optional`. C++23's `transform`, `and_then`, and `or_else` provide more elegant syntax for chaining.

The key principle for using `std::optional` is: use it to express the semantic of "missing value," not "error." If you need to pass error information (error codes, error descriptions), please use `std::expected` (C++23) or a custom `Result` type. `std::optional` is only responsible for "has or has not," not "why not."

The next topic we will discuss, `std::variant`, belongs to the same family as `std::optional`—"can hold a certain value or hold nothing"—but `std::variant` is more powerful and comes at a higher cost.

## Reference Resources

- [cppreference: std::optional](https://en.cppreference.com/w/cpp/utility/optional)
- [cppreference: std::bad_optional_access](https://en.cppreference.com/w/cpp/utility/optional/bad_optional_access)
- [C++23 Monadic operations for std::optional](https://en.cppreference.com/w/cpp/utility/optional)
- [C++ Core Guidelines: Optional](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines)
