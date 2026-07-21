---
chapter: 12
cpp_standard:
- 11
- 14
- 17
description: 'Class templates are the foundation of the STL. This piece covers the
  three differences from function templates that trip people up: members defined inside
  or outside the class, the lazy-instantiation temper, and why dependent names need
  typename and this-> for disambiguation.'
difficulty: intermediate
order: 3
platform: host
prerequisites:
- 'Templates, From Scratch: A Code Recipe with Placeholders'
- 'Function Templates, In Depth: Compilation Model and the No-Partial-Specialization
  Trap'
reading_time_minutes: 10
related:
- 'Template Specialization and Partial Specialization: The Art of Pattern Matching'
- 'Name Lookup and ADL: How Two-Phase Lookup Works'
tags:
- host
- cpp-modern
- intermediate
- 模板
- 泛型
title: 'Class Templates: Members, Dependent Names, and Lazy Instantiation'
---
# Class Templates: Members, Dependent Names, and Lazy Instantiation

Class templates are the foundation of the STL. `std::vector`, `std::map`, and `std::string` (really `std::basic_string<char>`) are all class templates. Knowing function templates does not mean knowing class templates. There are a few key differences, and they happen to be exactly where newcomers stumble. This piece focuses on three: whether member functions go inside or outside the class, the error-hiding temper that lazy instantiation brings, and why dependent names need `typename` and `this->` for disambiguation. Get these three straight, and writing your own containers or policy classes, or reading STL source, stops snagging on these details.

## What a Class Template Looks Like

Start from a minimal stack and look at things as we go.

```cpp
template <typename T>
class Stack {
public:
    void push(const T& value) { data_.push_back(value); }
    void pop() { data_.pop_back(); }
    const T& top() const { return data_.back(); }
    bool empty() const { return data_.empty(); }
private:
    std::vector<T> data_;   // use vector as the backing store
};
```

`template <typename T>` tells the compiler this is a class template and `T` is a type parameter. Everywhere `T` appears, it gets substituted with the concrete type at instantiation. `Stack<int>` replaces every `T` with `int`, `Stack<std::string>` with `std::string`, and each produces a separate class.

Writing all member functions inside the class, as above, is the most common style and the cleanest. Member functions can also be written outside the class, but the syntax has a trap covered in the next section.

Member functions of a class template are themselves templates (more precisely, "templated functions"). Only when a member function is actually called does the compiler instantiate it for the current type. This leads into the most important temper of class templates.

## Lazy Instantiation: No Use, No Pay

Member functions of a class template are **instantiated only when actually used**. Writing `Stack<Heavy>` does not generate every member function of `Stack`, only the ones you call. The previous piece mentioned this; here is a blunter example.

```cpp
#include <iostream>

template <typename T>
struct Box {
    T value;

    void used_show() const {
        std::cout << "value = " << value << "\n";
    }

    // The body refers to nonexistent_field, which does not exist on T.
    // As long as nobody calls it, the compiler never instantiates it, so it never errors.
    void unused_broken() const {
        std::cout << value.nonexistent_field << "\n";
    }
};

int main() {
    Box<int> b{42};
    b.used_show();   // only this one is used
    return 0;
}
```

`unused_broken` writes `value.nonexistent_field`, which for `T=int` is `int::nonexistent_field`, pure nonsense. Yet this compiles and runs fine.

```bash
$ g++ -Wall -Wextra -std=c++20 lazy_inst.cpp -o lazy_inst && ./lazy_inst
value = 42
```

The reason is lazy instantiation. `main` only calls `used_show`, so the compiler instantiates only `Box<int>::used_show`. `unused_broken` is never used, the compiler never looks at it, and whatever nonsense is inside never errors.

This is the STL's backbone. `std::vector` has dozens of member functions. If you only use `push_back` and `size`, the compiler instantiates only those, and `insert`, `erase`, and `emplace` are never generated. Otherwise every `vector<X>` would have to instantiate all its members, and compile time and binary size would balloon out of control.

There is a flip side. Lazy instantiation means **errors hide deep**. Write a type error in some member function, and as long as no test case touches it, it never surfaces at compile time. When someone finally calls it, possibly much later and far away, tracking it down takes real effort. So when writing class templates, it is best to either run a "full self-check" that instantiates every member at least once, or write unit tests that cover every member. Recent GCC versions flag some of these by default with diagnostics in the template body.

## Defining Members Outside the Class: Do Not Skip the Template Head

When a member function is written outside the class, the syntax is a `template <...>` head plus a `ClassName<T>::` qualifier. Neither can be omitted.

```cpp
template <typename T>
class Stack {
public:
    void push(const T& value);
    const T& top() const;
private:
    std::vector<T> data_;
};

// out-of-class definition: needs the template head, and the class name needs <T>
template <typename T>
void Stack<T>::push(const T& value) {
    data_.push_back(value);
}

template <typename T>
const T& Stack<T>::top() const {
    return data_.back();
}
```

Two common traps. One is forgetting the `template <typename T>` head, so the compiler does not know where `T` in `Stack<T>` comes from. The other is forgetting `<T>` on the class name, writing `void Stack::push(...)`, which treats `Stack` as a concrete class when it is only a template name and needs `<T>` to refer to an instantiation.

The upside of out-of-class definitions is a clean header. The downside is that the inclusion model still requires the definition to be visible at the use site, so out-of-class definitions usually still live in the header (or at the bottom of the same header file). This is still not the "declaration in .h, implementation in .cpp" of ordinary classes. Most small class templates put member functions inline in the class for simplicity. Large template libraries (STL implementations, for instance) split member functions out and place them in the same header or in a `.tpp` (template implementation) file that the header then includes.

## Dependent vs Non-dependent Names: `typename` Disambiguation

This is where class templates get confusing. Memorize two terms.

A **non-dependent name** does not depend on any template parameter. Things like `int` and `std::cout`. The compiler knows what they are at the template definition point.

A **dependent name** depends on some template parameter. Something like `T::value_type`, whose meaning is unknown until `T` is fixed.

The trouble is with dependent names. When the compiler parses the template definition, it does not yet know what `T` is, so it cannot know whether `T::value_type` is a type, a static member variable, or something else. The C++ rule is: **do not assume it is a type by default.** If you want it used as a type, you must say so with the `typename` keyword.

```cpp
template <typename Container>
void print_first(const Container& c) {
    typename Container::value_type first = *c.begin();
    std::cout << "first = " << first << "\n";
}
```

`Container::value_type` here is used as a type (to declare a variable), so it needs `typename` in front. With it, the code compiles and runs.

```bash
$ g++ -Wall -Wextra -std=c++20 typename_dis.cpp -o typename_dis && ./typename_dis
first = 10
```

Drop the `typename`, and GCC stops it, with an error message that states the rule clearly.

```text
typename_bad.cpp:3:5: error: need 'typename' before 'Container::value_type'
      because 'Container' is a dependent scope
    3 |     Container::value_type first = *c.begin();
      |     ^~~~~~~~~~~
```

"`Container` is a dependent scope," so the compiler will not assume `value_type` is a type unless you say so.

When do you need `typename`? Rule of thumb: when a dependent qualifier (a `::` involving a template parameter) appears in a position that needs a type (declaring a variable, a cast, a return type, a template type argument), add `typename`. C++20 relaxed this in a few spots: P0634 made `typename` optional in type-only contexts — type aliases (`using`), function return types, the type in a `new` expression, and data member declarations — but local variable declarations still need it. Building the habit of "dependent name used as a type, add typename" is the safest path and never wrong.

There is a parallel `template` keyword for disambiguation. If a dependent name is followed by `<`, the compiler cannot tell whether this is a template or a less-than comparison, and you write `T::template Foo<int>()` to say that `Foo` is a template. This is rare, but you will know the tool exists when you hit it.

## Dependent Base and `this->`

Inheritance with class templates runs into a related trap. Look at this.

```cpp
template <typename T>
struct Base {
    void helper() {}
    int data = 7;
};

template <typename T>
struct Derived : Base<T> {
    void call() {
        helper();   // error!
    }
};
```

`Derived` inherits from `Base<T>`, and inside `Derived::call` it calls the base class `helper()`. Intuitively the base clearly has `helper`, so this should work. But the compiler errors.

```text
dep_base.cpp:10:9: error: there are no arguments to 'helper' that depend on
      a template parameter, so a declaration of 'helper' must be available
   10 |         helper();
      |         ^~~~~~
```

The reason is the same logic as the `typename` case. `Base<T>` is a dependent base; its exact shape is unknown until `T` is fixed. When the compiler parses the definition of `Derived`, it does not look up non-dependent names (names without `T`, like `helper`) inside a dependent base, because for some specialization of `T`, `helper` might be a variable, or might not exist, and looking it up could produce a wrong answer.

The fix is to make the name "carry dependence," most commonly with `this->`.

```cpp
template <typename T>
struct Derived : Base<T> {
    int fetch() { return this->data; }   // this-> makes the lookup enter the dependent base
    void call() { this->helper(); }
};

int main() {
    Derived<int> d;
    d.call();
    std::cout << "fetch() = " << d.fetch() << "\n";   // reaches Base<T>::data = 7
}
```

`this->` tells the compiler "this is a member of the current object, look it up at instantiation time," and the problem goes away. It compiles and runs.

```bash
$ g++ -Wall -Wextra -std=c++20 dep_base_ok.cpp -o dep_base_ok && ./dep_base_ok
fetch() = 7
```

An equivalent spelling is explicit qualification `Base<T>::helper()`, but that turns off virtual dispatch, so if `helper` is virtual, do not use `Base<T>::`, use `this->`. For ordinary cases both work, and `this->` is more general.

This rule is two sides of the same coin as "two-phase lookup," covered in the next piece. Two-phase lookup requires the compiler to resolve non-dependent names at the template definition stage, while names inside a dependent base are not visible at that stage, so the compiler simply does not look there. Once you understand the motivation, `this->` stops feeling like "pointless syntactic noise." It exists to keep templates predictable under two-phase lookup.

## Static Members: One per Type

Class templates can have static data members. Unlike static members of ordinary classes, **each instantiation of a class template gets its own independent copy of a static member**.

```cpp
template <typename T>
struct Counter {
    static int instances;   // declaration
};

// definition (out of class): needs the template head
template <typename T>
int Counter<T>::instances = 0;

// Counter<int>::instances and Counter<double>::instances are two different variables
```

`Counter<int>::instances` and `Counter<double>::instances` are two completely independent static variables that do not affect each other. This is often used to count instances per type or hold a per-type cache.

Here is a trap to untangle. The out-of-class definition of a class template static member (`template <typename T> int Counter<T>::instances = 0;`) carries its own `template` head, which makes it a weak symbol. It **can** live in a header included by multiple translation units — the linker merges the copies automatically, with no ODR violation. It is only **non-template** static members (static members of an ordinary class) that are restricted to a single TU; putting those in a header included by multiple TUs does cause duplicate-definition errors. If the out-of-class definition feels like hassle, C++17 `inline` static members let you initialize in-class directly.

```cpp
template <typename T>
struct Counter {
    static inline int instances = 0;   // C++17: inline static member, in-class init, no out-of-class definition
};
```

This is the modern form, which removes the hassle of out-of-class definitions.

The next piece moves into specialization and partial specialization. Class templates can be partially specialized (unlike function templates), and the "pattern matching" semantics of partial specialization is the most expressive part of templates. The special implementation of `std::vector<bool>` and the entire machinery of type traits are all built on it.
