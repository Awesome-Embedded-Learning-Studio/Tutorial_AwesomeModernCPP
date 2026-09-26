---
chapter: 8
cpp_standard:
- 11
- 14
- 17
- 20
description: Master the syntax of single inheritance and the order of construction and destruction, and understand the object slicing problem and its solutions.
difficulty: intermediate
order: 2
platform: host
prerequisites:
- Function Calls and Type Conversion
reading_time_minutes: 11
tags:
- cpp-modern
- host
- intermediate
- 进阶
title: Single Inheritance
translation:
  source: documents/vol1-fundamentals/ch08/02-single-inheritance.md
  source_hash: d8e4b22bbeec9cdf72d737e2c76a590bcd0c06db569a722c07f5e331a63b5dda
  translated_at: '2026-09-25T11:27:45+00:00'
  engine: anthropic
  token_count: 2200
---
# Single Inheritance: Writing is-a Relationships into the Type System

In the previous article we set up the criteria for judging "should inheritance be used at all": inheritance earns its place only when a genuine is-a relationship exists and the objects really need to be handled uniformly through the base class. This article turns to inheritance itself: how the syntax is written, in what order constructors and destructors run, and what object slicing is all about. A Student is a Person, and a Car is a Vehicle—how these is-a relationships land in the type system is what this article delivers.

Inheritance lets us derive a new class from an existing one. The new class automatically acquires the members and capabilities of the base class, then adds its own specifics on top. To put it plainly, inheritance is not about "writing fewer lines of code" (though it does deliver that too), but about **how to build clear hierarchical relationships between types**. Once the hierarchy is in place, polymorphism and interface abstraction finally have somewhere to live.

## Basic Inheritance Syntax

Let's first look at the simplest form of inheritance:

```cpp
class Person {
private:
    std::string name_;
    int age_;

public:
    Person(const std::string& name, int age)
        : name_(name), age_(age) {}

    const std::string& name() const { return name_; }
    int age() const { return age_; }
};

class Student : public Person {
private:
    std::string school_;

public:
    Student(const std::string& name, int age, const std::string& school)
        : Person(name, age), school_(school) {}

    const std::string& school() const { return school_; }
};
```

Look at the line `class Student : public Person`: it does three things. It declares `Student` as a class derived from `Person`; it uses `public` inheritance, under which the base class's `public` members remain `public` in the derived class; and the memory layout of a `Student` object contains a complete `Person` subobject.

To put "inheritance" bluntly: tucked inside every `Student` object is a `Person`. `Student` carries all of `Person`'s member variables and all of `Person`'s public member functions, and we can call `.name()` and `.age()` on a `Student` object just as if they had been defined in `Student` all along.

One detail deserves special attention: `name_` and `age_` are `Person`'s private members. Although they live inside the `Student` object, `Student`'s member functions **cannot access them directly**. Private is private, and inheritance does not change that. What a derived class can use directly are the base class's public and protected members; the private ones can only be manipulated indirectly through the public interface the base class provides. That is also why the `Student` constructor writes `: Person(name, age)`: a derived-class constructor must forward arguments to a base-class constructor through its initializer list, leaving the base-class part of the initialization to the base class.

If we forget to call a base-class constructor in the derived-class constructor, the compiler falls back to calling the base class's default (parameterless) constructor. If the base class has no default constructor—for instance, `Person` has only `Person(const std::string&, int)` and no `Person()`—compilation fails outright, and the error message can read pretty convoluted; this is a spot where beginners often get stuck. So remember one rule: **when the base class has no default constructor, the derived class must explicitly call one of the base class's constructors in its initializer list**.

## Construction and Destruction Order

Getting the execution order of construction and destruction straight is required coursework for understanding inheritance. Let's observe it for real with an example that prints along the way:

```cpp
#include <iostream>

class Base {
public:
    Base() { std::cout << "Base::Base()\n"; }
    ~Base() { std::cout << "Base::~Base()\n"; }
};

class Derived : public Base {
public:
    Derived() { std::cout << "Derived::Derived()\n"; }
    ~Derived() { std::cout << "Derived::~Derived()\n"; }
};
```

Create and then destroy a `Derived` object, and the output is:

```text
Base::Base()
Derived::Derived()
Derived::~Derived()
Base::~Base()
```

Remember this order: construction runs from base to derived, because the derived class's constructor may depend on the base class's members already being in a valid state. Destruction runs in reverse, because the derived class's destructor may need to access base-class members to finish cleanup—if the base class were destroyed first, the derived destructor would be touching an object that is already dead. One sentence to remember it by: **construction goes from the inside out, destruction from the outside in**. However deep the inheritance hierarchy, the rule never changes.

## Using Base Class Members

A derived class can use the base class's public and protected members just as if they were its own. Here is a more complete example:

```cpp
class Student : public Person {
private:
    std::string school_;

public:
    Student(const std::string& name, int age, const std::string& school)
        : Person(name, age), school_(school) {}

    void introduce() const
    {
        Person::introduce();  // Reuse the base class's introduce()
        std::cout << "I study at " << school_ << ".\n";
    }
};
```

Look at the `Person::introduce()` call. Defining a function in the derived class with the same name as one in the base class is called **hiding**: the derived class's `introduce()` shadows the base class's `introduce()`. Calling `introduce()` directly on a `Student` object runs `Student`'s own version; to reuse the base implementation, you must name the scope explicitly with `Person::introduce()`.

Same-name function hiding is one of the sneakier traps in C++ inheritance. Once we define a function called `foo` in the derived class, every function named `foo` in the base class—whatever its parameter list—gets hidden. This is not overloading: overloading happens within a single scope, while inheritance spans two. To keep the base class's overload set available, we can add a `using Person::introduce;` declaration in the derived class, pulling all of the base class's overloaded versions into the derived class's scope.

## Object Slicing — the Most Common Pitfall in Inheritance

With the basics covered, let's face a problem that genuinely gives beginners headaches: **object slicing**.

```cpp
void print_person(Person p)   // Pass by value!
{
    p.introduce();
}

Student s("Alice", 20, "MIT");
print_person(s);  // Looks fine, but it has already been sliced
```

Look at this code: it compiles, it runs without crashing, and yet everything specific to `Student` (the "I study at MIT" part) has vanished. The cause is that `print_person`'s parameter `p` is a `Person` passed by value. To pass the argument, the compiler must copy the `Student` object into a variable of type `Person`, and `Person`'s storage only has room for `name_` and `age_`: `school_`, along with anything else specific to `Student`, has been "sliced off" (quite literally).

Folks, let's keep our heads clear here. The compiler has no bug; this is the direct consequence of C++'s value semantics. The fix is simple: **use references or pointers, not value types**.

```cpp
void print_person(const Person& p)   // Reference: no slicing
{
    p.introduce();
}
```

A reference or a pointer is merely an alias or an address of the original object—no copying happens—so the object we pass in arrives intact.

Object slicing does not only strike when passing function arguments; it also sneaks into containers. If we write `std::vector<Person> vec; vec.push_back(student);`, slicing happens all the same. The right approach is a pointer container such as `std::vector<std::unique_ptr<Person>>` or `std::vector<Person*>`. On top of that, the assignment `Person p = student;` slices too—no value-type conversion from a derived class to a base class escapes this fate.

## Protected Members — the Access Level Made for Inheritance

Think of `protected` as an access level between `public` and `private`: code outside the class cannot touch `protected` members, but the derived class's member functions can. It exists specifically for inheritance scenarios, letting derived classes "see" these members while encapsulation stays intact toward the outside world.

```cpp
class Vehicle {
private:
    double speed_;       // Only Vehicle itself can access this directly

protected:
    std::string brand_;  // Vehicle and its derived classes can access it

public:
    Vehicle(const std::string& brand, double speed)
        : brand_(brand), speed_(speed) {}

    double speed() const { return speed_; }
};

class Car : public Vehicle {
public:
    Car(const std::string& brand, double speed)
        : Vehicle(brand, speed) {}

    void print_info() const
    {
        std::cout << brand_ << "\n";    // Legal: protected member
        // std::cout << speed_ << "\n"; // Illegal: private member
        std::cout << speed() << "\n";   // Legal: through the public interface
    }
};
```

So when should `protected` be used? Our advice: **default to `private`, and change a member to `protected` only when you know for certain that a derived class needs direct access to it**. Overusing `protected` damages encapsulation: we would be exposing internal implementation details to every derived class, and once those details need to change in the future, the blast radius is hard to control. A good practice is to wrap the operations that derived classes need into `protected` member functions rather than exposing data members directly.

## In Practice: The Vehicle Hierarchy

Now let's string the previous pieces together. This program presents a `Vehicle` base class with two derived classes, `Car` and `Truck`, and covers construction/destruction order, member access, and a contrast that exposes object slicing.

```cpp
// inheritance.cpp
#include <iostream>
#include <string>

class Vehicle {
private:
    double speed_;

protected:
    std::string brand_;

public:
    Vehicle(const std::string& brand, double speed)
        : brand_(brand), speed_(speed)
    {
        std::cout << "  [Vehicle] constructed: " << brand_ << "\n";
    }

    ~Vehicle()
    {
        std::cout << "  [Vehicle] destroyed: " << brand_ << "\n";
    }

    double speed() const { return speed_; }
    const std::string& brand() const { return brand_; }

    void describe() const
    {
        std::cout << "  " << brand_ << " at " << speed_ << " km/h";
    }
};

class Car : public Vehicle {
private:
    int seats_;

public:
    Car(const std::string& brand, double speed, int seats)
        : Vehicle(brand, speed), seats_(seats)
    {
        std::cout << "  [Car] constructed: " << seats_ << " seats\n";
    }

    ~Car() { std::cout << "  [Car] destroyed\n"; }

    void describe() const
    {
        Vehicle::describe();
        std::cout << ", " << seats_ << " seats\n";
    }
};

class Truck : public Vehicle {
private:
    double payload_;

public:
    Truck(const std::string& brand, double speed, double payload)
        : Vehicle(brand, speed), payload_(payload)
    {
        std::cout << "  [Truck] constructed: " << payload_ << " tons\n";
    }

    ~Truck() { std::cout << "  [Truck] destroyed\n"; }

    void describe() const
    {
        Vehicle::describe();
        std::cout << ", " << payload_ << " tons\n";
    }
};

void show_vehicle(const Vehicle& v)   // Reference: no slicing
{
    std::cout << "[ref] ";
    v.describe();
}

void show_vehicle_sliced(Vehicle v)   // Pass by value: slicing!
{
    std::cout << "[val] ";
    v.describe();
    std::cout << "\n";
}

int main()
{
    std::cout << "=== 构造顺序 ===\n";
    Car car("Toyota", 120.0, 5);

    std::cout << "\n=== 按引用传递 ===\n";
    show_vehicle(car);

    std::cout << "\n=== 按值传递（切片）===\n";
    show_vehicle_sliced(car);

    std::cout << "\n=== 另一个派生类 ===\n";
    {
        Truck truck("Volvo", 90.0, 15.5);
        show_vehicle(truck);
    }

    std::cout << "\n=== 析构顺序 ===\n";
    return 0;
}
```

Compile and run:

```bash
g++ -Wall -Wextra -std=c++17 inheritance.cpp -o inheritance && ./inheritance
```

Verify the output:

```text
=== 构造顺序 ===
  [Vehicle] constructed: Toyota
  [Car] constructed: 5 seats

=== 按引用传递 ===
[ref]   Toyota at 120 km/h

=== 按值传递（切片）===
  [Vehicle] constructed: Toyota
[val]   Toyota at 120 km/h
  [Vehicle] destroyed: Toyota

=== 另一个派生类 ===
  [Vehicle] constructed: Volvo
  [Truck] constructed: 15.5 tons
[ref]   Volvo at 90 km/h
  [Truck] destroyed
  [Vehicle] destroyed: Volvo

=== 析构顺序 ===
  [Car] destroyed
  [Vehicle] destroyed: Toyota
```

Let's go through it section by section. Constructing the `Car` prints `[Vehicle]` first, then `[Car]`—the base class constructs first. You may notice that even in the pass-by-reference case the output only says "Toyota at 120 km/h" and never "5 seats"—that is because `describe()` is not a virtual function; the compiler binds `Vehicle::describe()` based on the reference's static type `Vehicle&`, even though the actual object is a `Car`. But there is one key difference between the two passing styles: the by-value call produces an extra temporary `Vehicle` copy being constructed and destroyed (direct evidence of slicing), while the by-reference call has no such step—the object stays whole; the call simply is not "polymorphic" yet. Making "pass a reference and reach the derived version" work takes virtual functions, which is the next article's topic. On the destruction side, when the `Truck` leaves its block scope `[Truck]` is destroyed first and `[Vehicle]` after; the `Car` is destroyed when `main` ends. Destruction order is always the reverse of construction order.

## Exercises

### Exercise 1: Design an Animal Hierarchy

Create an `Animal` base class with two members, `name_` (private) and `sound_` (protected), exposing a public `name()` interface and a `speak()` method. Then derive `Dog` and `Cat`, setting each one's own sound in the constructor. `Dog` should additionally carry a `breed_` field and provide a `describe()` method. Verify the construction and destruction order.

### Exercise 2: Fix the Object Slicing Bug

The code below has an object slicing problem. Find it and fix it:

```cpp
void process(Student s)   // Buggy
{
    std::cout << s.school() << "\n";
}

Student stu("Bob", 21, "Stanford");
process(stu);
```

Hint: change the parameter to pass by reference. Then think it over: if the function needs to store the object internally (in a container, say), is a reference still enough?
