---
chapter: 8
cpp_standard:
- 11
- 14
- 17
- 20
description: Master single inheritance syntax, construction and destruction order,
  and understand object slicing and its solutions.
difficulty: intermediate
order: 1
platform: host
prerequisites:
- 函数调用与类型转换
reading_time_minutes: 11
tags:
- cpp-modern
- host
- intermediate
- 进阶
title: Single Inheritance
translation:
  source: documents/vol1-fundamentals/ch08/01-single-inheritance.md
  source_hash: 376ea1b98d2b58cd5d6b521c744807707f050f68e0eb11eaf1c8891ae4dce688
  translated_at: '2026-06-16T03:45:21.597953+00:00'
  engine: anthropic
  token_count: 2304
---
# Single Inheritance

All the classes we have written so far are "standalone"—each class encapsulates its own data and provides its own interface, with no familial relationship between them. However, real-world entities do not exist in isolation: a Student is a Person, a Car is a Vehicle. This "is-a" relationship is the core semantic that inheritance expresses.

Inheritance allows us to derive a new class from an existing one. The new class automatically acquires the members and capabilities of the base class, and then adds its own specific features on top of that. To put it plainly, inheritance is not about "writing fewer lines of code"—though it certainly achieves that—but rather **how to establish clear hierarchical relationships between types**. Once the hierarchy is established, the subsequent implementation of polymorphism and interface abstractions has a solid foundation.

## Basic Syntax of Inheritance

Let's look at the simplest form of inheritance first:

```cpp
class Person {
public:
    Person(std::string name) : name_(std::move(name)) {}
    void introduce() const { std::cout << "I am " << name_ << "\n"; }
private:
    std::string name_;
};

// Student inherits from Person
class Student : public Person {
public:
    Student(std::string name, std::string school)
        : Person(std::move(name)), school_(std::move(school)) {}

    void study() const { std::cout << "I study at " << school_ << "\n"; }
private:
    std::string school_;
};
```

`class Student : public Person` This line does three things: it declares `Student` as a class derived from `Person`; it uses `public` inheritance, meaning the `public` members of the base class remain `public` in the derived class; and the memory layout of a `Student` object contains a complete `Person` subobject.

To put it simply, "inheritance" means that inside a `Student` object, there is a `Person` hidden away. `Student` possesses all member variables of `Person`, and also has access to all `public` member functions of `Person`—you can call `introduce` on a `Student` object just as if it were defined within `Student` itself.

However, there is one detail to pay special attention to: `name_` is a `private` member of `Person`. Although it exists within the `Student` object, the member functions of `Student` **cannot access it directly**. Private means private; inheritance does not change this. What a derived class can directly use are the `public` and `protected` members of the base class; `private` members can only be manipulated indirectly through the `public` interface provided by the base class. This is also why the `Student` constructor writes `Person(std::move(name))`—the derived class's constructor must pass parameters to the base class's constructor via the initialization list, allowing the base class to complete the initialization of the base class part.

> **Warning**: If you forget to call the base class constructor in the derived class, the compiler will attempt to call the base class's default constructor (the one with no arguments). If the base class lacks a default constructor—for example, if `Person` only has a `Person(std::string)` constructor and no `Person()`—compilation will fail directly. The error message can sometimes be quite convoluted, causing beginners to get stuck here. So remember this rule: **When a base class lacks a default constructor, the derived class must explicitly call one of the base class's constructors in the initialization list.**

## Order of Construction and Destruction

Understanding the execution order of construction and destruction is a prerequisite for mastering the inheritance mechanism. Let's use an example with print statements to observe this in practice:

```cpp
class Base {
public:
    Base() { std::cout << "Base constructed\n"; }
    ~Base() { std::cout << "Base destroyed\n"; }
};

class Derived : public Base {
public:
    Derived() { std::cout << "Derived constructed\n"; }
    ~Derived() { std::cout << "Derived destroyed\n"; }
};

int main() {
    Derived d;
    // ...
}
```

Creating and then destroying a `Derived` object produces the following output:

```text
Base constructed
Derived constructed
Derived destroyed
Base destroyed
```

During construction, we go from the base class to the derived class—lay the foundation before building the house—because the derived class's construction might depend on the base class members being in a valid state. During destruction, the reverse happens—tear down the upper floors before dismantling the foundation—because the derived class's destructor might need to access base class members to clean up resources. If the base class were destroyed first, the derived class destructor would be accessing an already invalidated object. Remember this rule with one phrase: **Construction goes from the inside out; destruction goes from the outside in**. No matter how deep the inheritance hierarchy is, this rule holds true.

## Using Base Class Members

A derived class can use the `public` and `protected` members of the base class just like its own members. Let's look at a more complete example:

```cpp
class Base {
public:
    void doWork() { std::cout << "Base working\n"; }
    void doWork(int x) { std::cout << "Base working with " << x << "\n"; }
};

class Derived : public Base {
public:
    void doWork() { std::cout << "Derived working\n"; } // Hides Base::doWork
    void callBaseWork() {
        doWork();       // Calls Derived::doWork
        Base::doWork(); // Explicitly calls Base::doWork
        Base::doWork(42); // Explicitly calls Base::doWork(int)
    }
};
```

What is noteworthy here is the call to `doWork()`. The derived class defines a function with the same name as one in the base class; this is called **hiding**—it is not overriding, but rather the derived class's `doWork` obscures the base class's `doWork`. Calling `doWork` directly on a `Derived` object executes the `Derived` version. To reuse the base class implementation, we must use `Base::` to explicitly specify the scope.

> **Warning**: Name hiding is a subtle pitfall in C++ inheritance. If you define a function named `doWork` in the derived class, all functions in the base class named `doWork` (regardless of the parameter list) will be hidden. This is not overloading—overloading occurs within the same scope, whereas inheritance crosses two scopes. If you wish to retain the base class's overload set, you can write `using Base::doWork;` in the derived class to pull all overloaded versions from the base class into the derived class's scope.

## Object Slicing—The Easiest Pitfall in Inheritance

Having covered the basic usage, we now face a problem that truly gives beginners a headache: **Object Slicing**.

```cpp
void printInfo(Person p) { // Problem: passed by value
    p.introduce();
}

int main() {
    Student s("Alice", "MIT");
    printInfo(s); // Slicing occurs here
}
```

This code compiles and runs without crashing, but the specific information of `Student` ("I study at MIT") completely disappears. The reason lies in the parameter `p` of the `printInfo` function: it is of type `Person` passed by value. When passing arguments, the compiler needs to copy the `Student` object into a variable of type `Person`. The memory space of `Person` is only large enough to hold `Person`'s members; `school_` and anything specific to `Student` are—literally—"sliced off".

Folks. This is not a compiler bug; it is a direct consequence of C++ value semantics. The solution is simple: **Use references or pointers, not value types**.

```cpp
void printInfo(const Person& p) { // Use reference
    p.introduce();
}
```

References and pointers are merely aliases or addresses pointing to the original object; they involve no copying action, so the object remains intact.

> **Warning**: Object slicing doesn't just happen during function parameter passing; it can also sneak up in containers. If you write `std::vector<Person>`, slicing will occur as well. The correct approach is to use pointer containers like `std::vector<std::unique_ptr<Person>>` or `std::vector<Person*>`. Additionally, assignment operations like `Person p = s;` will also cause slicing—any value type conversion from a derived class to a base class cannot escape this fate.

## Protected Members—Access Level Born for Inheritance

`protected` is an access level between `private` and `public`: code outside the class cannot access `protected` members, but member functions of derived classes can. It is designed specifically for inheritance scenarios—allowing derived classes to "see" these members while maintaining encapsulation from the outside.

```cpp
class Base {
protected:
    int data_; // Derived classes can access this directly
};
```

So when should you use `protected`? My advice is: **Default to `private`, and only change to `protected` when you explicitly know that a derived class needs direct access to a specific member**. Overusing `protected` breaks encapsulation—you expose internal implementation details to all derived classes, making it hard to control the impact if you want to modify these details later. A good practice is to encapsulate operations that need to be exposed to derived classes into `protected` member functions, rather than directly exposing data members.

## Practice: Vehicle Hierarchy

Now let's connect the previous points. This program demonstrates a `Vehicle` base class and two derived classes, `Car` and `Motorcycle`, covering construction/destruction order, member access, and a comparison of object slicing.

```cpp
#include <iostream>
#include <string>

class Vehicle {
public:
    Vehicle(std::string brand, int speed)
        : brand_(std::move(brand)), speed_(speed) {
        std::cout << "Vehicle constructed\n";
    }
    virtual ~Vehicle() { std::cout << "Vehicle destroyed\n"; } // Virtual destructor (explained later)

    void describe() const {
        std::cout << brand_ << " at " << speed_ << " km/h\n";
    }

protected:
    std::string brand_;
    int speed_;
};

class Car : public Vehicle {
public:
    Car(std::string brand, int speed, int seats)
        : Vehicle(std::move(brand), speed), seats_(seats) {
        std::cout << "Car constructed\n";
    }
    ~Car() { std::cout << "Car destroyed\n"; }

    void describe() const {
        Vehicle::describe();
        std::cout << "  " << seats_ << " seats\n";
    }

private:
    int seats_;
};

void printVehicleInfo(const Vehicle& v) {
    v.describe();
}

int main() {
    Car toyota("Toyota", 120, 5);

    std::cout << "\n--- By Reference ---\n";
    printVehicleInfo(toyota);

    std::cout << "\n--- By Value (Slicing) ---\n";
    printVehicleInfo(toyota); // If parameter were Vehicle v, slicing happens

    std::cout << "\n--- Cleanup ---\n";
}
```

Compile and run:

```bash
g++ -std=c++20 main.cpp -o main && ./main
```

Verify the output:

```text
Vehicle constructed
Car constructed

--- By Reference ---
Toyota at 120 km/h
  5 seats

--- By Value (Slicing) ---
Toyota at 120 km/h

--- Cleanup ---
Car destroyed
Vehicle destroyed
```

Looking at this step-by-step: when constructing `Car` (Toyota), `Vehicle` is constructed first, then `Car`—the base class is constructed first. You might notice that when passing by reference, the output is only "Toyota at 120 km/h", and "5 seats" does not appear—this is because `describe` is not a `virtual` function; the compiler binds `Vehicle::describe` based on the static type of the reference `Vehicle&`, even though the actual object is a `Car`. However, there is a key difference between passing by reference and passing by value: passing by value involves the construction and destruction of a temporary `Vehicle` copy (conclusive evidence of slicing), whereas passing by reference does not involve this process—the object is intact, it's just that the function call isn't "polymorphic" yet. To achieve "pass by reference and call the derived class version," we need virtual functions, which is the topic of the next chapter. Regarding destruction, when `toyota` leaves the block scope, `Car` is destructed first, then `Vehicle`—the destruction order is always the reverse of the construction order.

## Exercises

### Exercise 1: Design an Animal Hierarchy

Create an `Animal` base class containing `age_` (private) and `sound_` (protected) members, providing a `makeSound` public interface and a `getAge` method. Then derive `Dog` and `Cat`, setting their respective sounds in their constructors. Require `Dog` to additionally include a `breed_` field and provide a `bark` method, and verify the order of construction and destruction.

### Exercise 2: Fix the Object Slicing Bug

The following code has an object slicing problem. Find it and fix it:

```cpp
void process(Person p) { /* ... */ }
// ...
process(studentObj);
```

Hint: Change the parameter to pass by reference. Think about this: if the function needs to store the object (for example, putting it into a container), is a reference still sufficient?

## Summary

In this chapter, we delved into the core mechanisms of single inheritance. Inheritance uses the `:` syntax to express "is-a" relationships, where derived classes automatically acquire all members of the base class. Construction goes from base to derived, and destruction is the reverse—this holds true for inheritance chains of any depth. Derived classes can directly use the `public` and `protected` members of the base class, while `private` members can only be accessed indirectly via interfaces. Protected members (`protected`) are designed for inheritance scenarios but should be used cautiously; default to `private` to maintain encapsulation.

Object slicing is the easiest pitfall in inheritance: any value type conversion from a derived class to a base class will lose the parts specific to the derived class. There is only one solution—use references or pointers.

So far, the inheritance we have discussed is static: which version of a function to call is determined at compile time. In the next chapter, we introduce virtual functions, allowing the target of a function call to be determined at runtime—that is the realm of polymorphism.
