---
chapter: 12
cpp_standard:
- 11
- 14
- 17
description: Class templates are the cornerstone of the STL. This piece covers the three key differences from function templates in practice — where member functions are defined, the lazy-instantiation quirk where unused code never errors, and why dependent names need typename and this-> for disambiguation
difficulty: intermediate
order: 3
platform: host
prerequisites:
- 'Templates, From Scratch: A Code Recipe with Placeholders'
- 'Function Templates, In Depth: Compilation Model and the No-Partial-Specialization Trap'
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
translation:
  source: documents/vol4-advanced/vol1-basics-cpp11-14/03-class-templates.md
  source_hash: ec2c5bfc897c70e1f138bd4f9083c207ef59727424cd593c680d13c7c1f1c481
  translated_at: '2026-09-26T04:04:48+00:00'
  engine: anthropic
  token_count: 5800
---
# Class Templates: Members, Dependent Names, and Lazy Instantiation

Class templates are the cornerstone of the STL—`std::vector`, `std::map`, and `std::string` (which is really `std::basic_string<char>`) are all class templates. Knowing how to write function templates does not mean knowing how to write class templates; the two differ in a few key ways, and those differences are exactly where newcomers stumble over and over when writing class templates. This piece focuses on three of them: whether member functions go inside or outside the class, the error-hiding quirk that lazy instantiation brings, and why dependent names need `typename` and `this->` for disambiguation. Once these three are fully digested, writing your own containers, writing policy classes, or reading STL source code will stop snagging on these details.

## What a Class Template Looks Like

We start from a minimal stack, writing and observing as we go.

```cpp
template <typename T>
class Stack {
public:
    void push(const T& value) { data_.push_back(value); }
    void pop() { data_.pop_back(); }
    const T& top() const { return data_.back(); }
    bool empty() const { return data_.empty(); }
private:
    std::vector<T> data_;   // use vector as the underlying storage
};
```

`template <typename T>` tells the compiler this is a class template and `T` is a type parameter. Wherever `T` appears in the class, instantiation replaces it with a concrete type. `Stack<int>` swaps every `T` for `int`, `Stack<std::string>` swaps it for `std::string`, and each generates its own separate class.

In the code above, every member function is defined inside the class—the most common style, and a concise one. Member functions can also be defined outside the class, but that spelling has a trap of its own, which the next section covers in detail.

A class template's member functions are themselves templates (more precisely, "templated functions"). Only when a member function is actually called does the compiler instantiate it for the current type. That fact leads us to the single most important behavioral quirk of class templates.

## Lazy Instantiation: You Don't Pay for What You Don't Use

For a class template's member functions, **only the ones actually used get instantiated**. Writing a `Stack<Heavy>` does not generate every member function of `Stack`; only the ones you actually call get generated. The previous piece mentioned this rule; here a more direct example makes it plain.

```cpp
#include <iostream>

template <typename T>
struct Box {
    T value;

    void used_show() const {
        std::cout << "value = " << value << "\n";
    }

    // This function body uses nonexistent_field, which does not exist on T at all
    // As long as nobody calls it, the compiler never instantiates it, so no error
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

The `value.nonexistent_field` inside `unused_broken` becomes `int::nonexistent_field` when `T=int`—pure nonsense. Yet this code compiles and runs fine:

```bash
$ g++ -Wall -Wextra -std=c++20 lazy_inst.cpp -o lazy_inst && ./lazy_inst
value = 42
```

The reason is lazy instantiation. `main` calls only `used_show`, so the compiler instantiates only `Box<int>::used_show`; `unused_broken` is never used anywhere, and the compiler never so much as glances at it—whatever nonsense is written inside raises no error.

This property is the backbone of the STL. `std::vector` has dozens of member functions; if you use only `push_back` and `size`, the compiler instantiates just those two, and `insert`, `erase`, `emplace` and the rest are not generated at all. Otherwise every `vector<X>` would have its full set of members instantiated, and compile times and binary size would balloon beyond acceptance.

Every advantage has its cost. The price of lazy instantiation is that **errors hide deep**. Write a type error inside some member function, and as long as no test case reaches that function, compilation never exposes it. By the time someone actually calls it, it may be much later, in code far away, and tracking it down becomes painful. So when writing class templates, it is best to explicitly run a "full-type self-check" that instantiates every member at least once, or to write unit tests covering every member function. GCC has a diagnostic along the lines of `-Wtemplate-body` that helps you catch part of this class of problems, and new GCC versions point out some obvious errors inside template bodies by default.

## Defining Member Functions Outside the Class: Never Skip the Template Header

To define a member function outside the class, the syntax is a `template <...>` header plus the `ClassName<T>::` qualifier. Neither can be omitted.

```cpp
template <typename T>
class Stack {
public:
    void push(const T& value);
    const T& top() const;
private:
    std::vector<T> data_;
};

// Out-of-class definition: must carry the template header, and the class name must carry <T>
template <typename T>
void Stack<T>::push(const T& value) {
    data_.push_back(value);
}

template <typename T>
const T& Stack<T>::top() const {
    return data_.back();
}
```

Two common traps. First, forgetting the `template <typename T>` header—the compiler has no idea where the `T` in `Stack<T>` comes from. Second, forgetting the `<T>` on the class name and writing `void Stack::push(...)`; that treats `Stack` as a concrete class, whereas `Stack` is only a template name and must carry `<T>` to denote some instantiation.

The benefit of out-of-class definitions is a cleaner header file; the drawback is that templates' inclusion model requires the definition to remain visible at the point of use, so out-of-class definitions usually still have to live in the header file (or at the end of the same header). This differs from the ordinary-class routine of "declare in .h, implement in .cpp". Most small class templates simply put their member functions inside the class and save the trouble; large template libraries (STL implementations, for instance) split member functions out of the class and place them either in the same header or in a `.tpp` (template implementation) file that the header then includes.

## Dependent Names vs Non-Dependent Names: Disambiguating with `typename`

This is the point where class templates confuse people the most. First, memorize two terms:

- **Non-dependent name**: a name that depends on no template parameter. Examples: `int`, `std::cout`—the compiler already knows what it is at the template definition point.
- **Dependent name**: a name that depends on some template parameter. Example: `T::value_type`—its exact meaning is known only once `T` is pinned down.

The trouble lies with dependent names. While parsing the template definition, the compiler does not yet know what `T` is, so it cannot know whether `T::value_type` is a type, a static member variable, or something else entirely. The C++ rule: **by default, do not assume it is a type**. If you want the compiler to treat it as a type, you must say so explicitly with the `typename` keyword.

```cpp
template <typename Container>
void print_first(const Container& c) {
    typename Container::value_type first = *c.begin();
    std::cout << "first = " << first << "\n";
}
```

Here `Container::value_type` is used as a type (declaring a variable), so it must be preceded by `typename`. With it added, the code compiles and runs without complaint:

```bash
$ g++ -Wall -Wextra -std=c++20 typename_dis.cpp -o typename_dis && ./typename_dis
first = 10
```

Remove the `typename`, and GCC stops you right there; the error message states the rule plainly:

```text
typename_bad.cpp:3:5: error: need 'typename' before 'Container::value_type'
      because 'Container' is a dependent scope
    3 |     Container::value_type first = *c.begin();
      |     ^~~~~~~~~~~
```

"`Container` is a dependent scope"—so the compiler dares not assume `value_type` is a type; you must declare it explicitly.

When do you need to add `typename`? Rule of thumb: whenever a dependent qualifier (a `::` carrying template parameters) appears in a position that expects a type (declaring a variable, performing a cast, giving a return type, supplying a template type argument), add `typename`. C++20 (P0634) relaxed one spot: in a type-only context (a `using` type alias, a function return type, the type in a `new` expression, a data member declaration—positions of these kinds) `typename` may be omitted, but places such as local variable declarations still require it. The most worry-free habit is "add typename whenever a dependent name is used as a type"—it never goes wrong.

A similar disambiguation exists for the `template` keyword. If a dependent name is followed by `<`, the compiler cannot tell whether it names a template or is a less-than comparison; in that case you write `T::template Foo<int>()` to tell it explicitly that `Foo` is a template. This one is rarely used, but it is good to know the tool exists when you run into it.

## Dependent Base Classes and `this->`

When class templates get involved in inheritance, you run into another related trap. Look at this:

```cpp
template <typename T>
struct Base {
    void helper() {}
    int data = 7;
};

template <typename T>
struct Derived : Base<T> {
    void call() {
        helper();   // Error!
    }
};
```

`Derived` inherits from `Base<T>`, and `Derived::call` invokes the base class's `helper()`. Intuitively, the base class clearly has `helper`, so calling it directly should work. Yet the compiler errors out:

```text
dep_base.cpp:10:9: error: there are no arguments to 'helper' that depend on
      a template parameter, so a declaration of 'helper' must be available
   10 |         helper();
      |         ^~~~~~
```

The reason follows the same logic as the `typename` rule. `Base<T>` is a dependent base, and what it actually looks like is known only once `T` is pinned down. While parsing `Derived`'s definition, the compiler does not look up non-dependent names (names carrying no `T`, like `helper`) inside the dependent base, because it worries that in some specialization of `T`, `helper` might be a variable—or might not exist at all—and looking it up could well produce the wrong answer.

The fix is to make the name "carry a dependency", and the most common way is `this->`:

```cpp
template <typename T>
struct Derived : Base<T> {
    int fetch() { return this->data; }   // this-> lets lookup enter the dependent base
    void call() { this->helper(); }
};

int main() {
    Derived<int> d;
    d.call();
    std::cout << "fetch() = " << d.fetch() << "\n";   // reaches Base<T>::data = 7
}
```

`this->` tells the compiler "this is a member of the current object; go look for it at instantiation time"—problem solved. It compiles and runs:

```bash
$ g++ -Wall -Wextra -std=c++20 dep_base_ok.cpp -o dep_base_ok && ./dep_base_ok
fetch() = 7
```

An equivalent alternative is explicit qualification, `Base<T>::helper()`, but that spelling disables dynamic dispatch of virtual functions—so if `helper` is virtual, do not use `Base<T>::`; use `this->`. In ordinary scenarios both work, and `this->` is the more general one.

This rule and the "two-phase lookup" covered in the next piece are two faces of the same coin. Two-phase lookup requires the compiler to look up non-dependent names already at the template definition stage, while names inside a dependent base are not yet visible at that stage—so the compiler simply declines to look. Once you understand the motive, `this->` stops feeling like "redundant syntactic noise": it exists to make templates behave predictably within the framework of two-phase lookup.

## Static Members: One Copy Per Type

Class templates can have static data members. Unlike an ordinary class's static members, a class template's static members get **an independent instance per instantiated type**.

```cpp
template <typename T>
struct Counter {
    static int instances;   // declaration
};

// Definition (outside the class): must carry the template header
template <typename T>
int Counter<T>::instances = 0;

// Counter<int>::instances and Counter<double>::instances are two different variables
```

`Counter<int>::instances` and `Counter<double>::instances` are two completely independent static variables that do not affect each other. This is often used to keep per-type counters or caches dedicated to each type.

One trap to sort out here: an out-of-class definition of a class template's static member (the `template <typename T> int Counter<T>::instances = 0;` form) carries a `template` header of its own and is a weak symbol, so it **may** sit in a header included by multiple translation units—the linker merges the copies automatically, no conflict. What is truly "restricted to one translation unit" is a **non-template** static member (an ordinary class's static member); that is what causes duplicate definitions when a header containing it is pulled into multiple TUs. If the out-of-class definition feels like a chore, C++17's `inline` static members can be initialized right inside the class:

```cpp
template <typename T>
struct Counter {
    static inline int instances = 0;   // C++17: inline static member, initialized in class, no out-of-class definition needed
};
```

This is the modern style, sparing you the out-of-class definition entirely.

In the next piece we move on to specialization and partial specialization. Class templates can be partially specialized (unlike function templates), and the "pattern matching" semantics of partial specialization is the most expressive part of templates—both the special implementation of `std::vector<bool>` and the entire bag of tricks behind type traits are built on it.
