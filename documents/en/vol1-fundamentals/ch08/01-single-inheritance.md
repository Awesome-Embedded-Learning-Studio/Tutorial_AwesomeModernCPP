---
title: Single inheritance
description: Master the syntax of single inheritance, the order of construction and
  destruction, and understand the object slicing problem and its solutions.
chapter: 8
order: 1
difficulty: intermediate
reading_time_minutes: 15
platform: host
prerequisites:
- 函数调用与类型转换
tags:
- cpp-modern
- host
- intermediate
- 进阶
cpp_standard:
- 11
- 14
- 17
- 20
---
# Single Inheritance

So far, all the classes we have written are "standalone" — a class encapsulates its own data, provides its own interface, and has no familial relationship with any other class. But real-world entities do not exist in isolation: a student (Student) is a person (Person), a car (Car) is a type of vehicle (Vehicle). This "is-a" relationship is the core semantic that inheritance aims to express.

Inheritance allows us to derive a new class from an existing one. The new class automatically acquires the members and capabilities of the base class, and then adds its own unique features on top of that. To put it plainly, inheritance does not merely solve the problem of "writing fewer lines of code" — although it certainly achieves that — but rather **how to establish clear hierarchical relationships between types**. Once the hierarchy is established, subsequent polymorphism and interface abstraction have a solid foundation to build upon.

## Basic Syntax of Inheritance

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

`class Student : public Person` does three things: it declares that `Student` is a class derived from `Person`; it uses the `public` inheritance mode, meaning the `public` members of the base class remain `public` in the derived class; and it ensures that the memory layout of a `Student` object contains a complete `Person` subobject.

To put it bluntly, "inheritance" means that a `Student` object hides a `Person` inside it. A `Student` has all the member variables of a `Person`, and also has all the public member functions of a `Person` — you can call `.name()` and `.age()` on a `Student` object just as if they were originally defined in `Student`.

But there is one detail to pay special attention to: `name_` and `age_` are private members of `Person`. Although they exist within the `Student` object, the member functions of `Student` **cannot access them directly**. Private is private, and inheritance does not change this. What a derived class can directly use are the public and protected members of the base class; private members can only be manipulated indirectly through the public interface provided by the base class. This is also why the `Student` constructor writes `: Person(name, age)` — the derived class's constructor must pass parameters to the base class's constructor via the initializer list, letting the base class complete the initialization of the base class portion.

> **Pitfall Warning**: If you forget to call the base class constructor in the derived class's constructor, the compiler will try to call the base class's default constructor (the one with no parameters). If the base class does not have a default constructor — for example, if `Person` only has a `Person(const std::string&, int)` but no `Person()` — compilation will fail directly. The error messages can sometimes be quite convoluted, and beginners easily get stuck here. So remember this rule: **when a base class lacks a default constructor, the derived class must explicitly call one of the base class's constructors in the initializer list**.

## Order of Construction and Destruction

Understanding the execution order of construction and destruction is a required course for grasping the inheritance mechanism. We use an example with print statements to observe this in practice:

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

Creating and then destroying a `Derived` object produces the following output:

```text
Base::Base()
Derived::Derived()
Derived::~Derived()
Base::~Base()
```

During construction, we go from the base class to the derived class — lay the foundation before building the floors, because the derived class's construction might depend on the base class members already being in a valid state. During destruction, it goes in reverse — tear down the upper floors before the foundation, because the derived class's destructor might need to access base class members to complete resource cleanup. If the base class were destroyed first, the derived class's destructor would be accessing an already-invalid object. Remember this rule in one sentence: **construction goes from inside out, destruction goes from outside in**. No matter how deep the inheritance hierarchy is, this rule remains the same.

## Using Base Class Members

A derived class can use the public and protected members of its base class just like its own members. Let's look at a more complete example:

```cpp
class Student : public Person {
private:
    std::string school_;

public:
    Student(const std::string& name, int age, const std::string& school)
        : Person(name, age), school_(school) {}

    void introduce() const
    {
        Person::introduce();  // 复用基类的 introduce()
        std::cout << "I study at " << school_ << ".\n";
    }
};
```

What is noteworthy here is the `Person::introduce()` call. The derived class defines a function with the same name as one in the base class; this is called **hiding** — it is not overriding, but rather the derived class's `introduce()` obscures the base class's `introduce()`. Calling `introduce()` directly on a `Student` object executes `Student`'s own version. To reuse the base class's implementation, we must use `Person::introduce()` to explicitly specify the scope.

> **Pitfall Warning**: Name hiding is a rather subtle trap in C++ inheritance. If you define a function called `foo` in the derived class, then all functions named `foo` in the base class (regardless of whether the parameter lists match) will be hidden. This is not overloading — overloading occurs within the same scope, whereas inheritance spans two scopes. If you want to preserve the base class's overload set, you can write `using Person::introduce;` in the derived class to pull all overloaded versions from the base class into the derived class's scope.

## Object Slicing — The Easiest Pitfall in Inheritance

Having covered the basic usage, let's face a problem that truly gives beginners a headache: **Object Slicing**.

```cpp
void print_person(Person p)   // 按值传递！
{
    p.introduce();
}

Student s("Alice", 20, "MIT");
print_person(s);  // 看起来没问题，实际上已经切片了
```

This code compiles and runs without crashing, but the information unique to `Student` ("I study at MIT") completely disappears. The reason is that the parameter `p` of `print_person` is passed by value as a `Person` type. When passing the argument, the compiler needs to copy the `Student` object into a `Person` variable, but the memory space of `Person` is only large enough to hold `name_` and `age_`. `school_` and anything else unique to `Student` are — literally — "sliced off."

Folks, this is not some compiler bug; it is a direct consequence of C++'s value semantics. The solution is simple: **use references or pointers, not value types**.

```cpp
void print_person(const Person& p)   // 引用，不切片
{
    p.introduce();
}
```

References and pointers are merely aliases or addresses pointing to the original object; they do not involve any copying action, so the object remains intact.

> **Pitfall Warning**: Object slicing doesn't only happen during function parameter passing; it can also sneak up inside containers. If you write `std::vector<Person> vec; vec.push_back(student);`, slicing will occur just the same. The correct approach is to use pointer containers like `std::vector<std::unique_ptr<Person>>` or `std::vector<Person*>`. Additionally, assignment operations like `Person p = student;` will also cause slicing — any value-type conversion from a derived class to a base class cannot escape this fate.

## Protected Members — An Access Level Born for Inheritance

`protected` is an access level between `public` and `private`: code outside the class cannot access `protected` members, but member functions of derived classes can. It is designed specifically for inheritance scenarios — allowing derived classes to "see" these members while maintaining encapsulation against the outside world.

```cpp
class Vehicle {
private:
    double speed_;       // 只有 Vehicle 自己能直接访问

protected:
    std::string brand_;  // Vehicle 和它的派生类能访问

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
        std::cout << brand_ << "\n";    // 合法：protected 成员
        // std::cout << speed_ << "\n"; // 非法：private 成员
        std::cout << speed() << "\n";   // 合法：通过公有接口
    }
};
```

So when should you use `protected`? My advice is: **default to `private`, and only switch to `protected` when you explicitly know that a derived class needs direct access to a certain member**. Overusing `protected` breaks encapsulation — you expose internal implementation details to all derived classes, and once you want to modify these details in the future, the blast radius becomes hard to control. A good practice is to encapsulate the operations that need to be exposed to derived classes into `protected` member functions, rather than directly exposing data members.

## Hands-on: The Vehicle Hierarchy

Now let's tie the concepts we've covered together. This program demonstrates a `Vehicle` base class and two derived classes, `Car` and `Truck`, covering construction/destruction order, member access, and a comparison of object slicing.

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

void show_vehicle(const Vehicle& v)   // 引用，不切片
{
    std::cout << "[ref] ";
    v.describe();
}

void show_vehicle_sliced(Vehicle v)   // 值传递，切片！
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

Let's break it down section by section: when constructing `Car`, `[Vehicle]` happens before `[Car]` — the base class is constructed first. You might notice that even when passing by reference, the output only shows "Toyota at 120 km/h" without "5 seats" appearing — this is because `describe()` is not a virtual function, so the compiler binds to `Vehicle::describe()` based on the static type of the reference, `Vehicle&`, even though the actual object is a `Car`. However, there is a key difference between pass-by-reference and pass-by-value: with pass-by-value, there is an extra construction and destruction of a temporary `Vehicle` copy (concrete evidence of slicing), whereas pass-by-reference has no such process — the object is intact, it's just that the function call isn't "polymorphic" yet. To achieve "pass by reference and invoke the derived class version," we need virtual functions, which is the topic of the next chapter. As for destruction, when `Truck` leaves the block scope, `[Truck]` is destructed before `[Vehicle]`, and `Car` is destructed when `main` ends — the destruction order is always the reverse of the construction order.

## Exercises

### Exercise 1: Design an Animal Hierarchy

Create an `Animal` base class containing two members: `name_` (private) and `sound_` (protected). Provide a `name()` public interface and a `speak()` method. Then derive `Dog` and `Cat`, setting their respective sounds in their constructors. Require `Dog` to additionally include a `breed_` breed field and provide a `describe()` method, and verify the construction and destruction order.

### Exercise 2: Fix the Object Slicing Bug

The following code has an object slicing problem; find it and fix it:

```cpp
void process(Student s)   // 有 bug
{
    std::cout << s.school() << "\n";
}

Student stu("Bob", 21, "Stanford");
process(stu);
```

Hint: Change the parameter to pass-by-reference. Think about this: if the function needs to store the object internally (for example, putting it into a container), are references still sufficient?

## Summary

In this chapter, we dove deep into the core mechanism of single inheritance. Inheritance uses `class Derived : public Base` to express "is-a" relationships, and derived classes automatically acquire all members of the base class. Construction goes from the base class to the derived class, and destruction goes in reverse — this holds true in inheritance chains of any depth. Derived classes can directly use the public and protected members of the base class, while private members can only be accessed indirectly through interfaces. Protected members (`protected`) are designed for inheritance scenarios, but they should be used sparingly; default to `private` to maintain encapsulation.

Object slicing is the easiest pitfall in inheritance: any value-type conversion from a derived class to a base class will lose the parts unique to the derived class. There is only one solution — use references or pointers.

The inheritance we have discussed so far is still static: which version of a function to call is determined at compile time. In the next chapter, we will introduce virtual functions, allowing the target of a function call to be determined at runtime — that is the domain of polymorphism.
