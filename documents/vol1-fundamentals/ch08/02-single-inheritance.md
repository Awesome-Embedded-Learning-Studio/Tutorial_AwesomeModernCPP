---
chapter: 8
cpp_standard:
- 11
- 14
- 17
- 20
description: 掌握单继承的语法、构造与析构顺序，理解对象切片问题及其解决方案
difficulty: intermediate
order: 2
platform: host
prerequisites:
- 函数调用与类型转换
reading_time_minutes: 11
tags:
- cpp-modern
- host
- intermediate
- 进阶
title: 单继承
---
# 单继承：把 is-a 关系写进类型系统

Over，在上一篇我们就好好的讨论了继承和组合最基本的讨论。大家学习C++是会用，所以的话，**我们对继承和组合点到为止，主要会用~**。学生（Student）是一个人（Person），轿车（Car）是一种交通工具（Vehicle），这些 is-a 关系怎么落进类型系统，就看这一篇。

复习一下，继承是表达 `is-a` 论断的重要抽象，它允许我们能够从一个已有的类出发，派生出一个新类。新类自动获得基类的成员和能力，然后在此基础上添加自己特有的东西。说白了，继承解决的不是"少写几行代码"的问题（虽然它确实能做到这一点），而是**如何在类型之间建立清晰的层次关系**。层次建好了，后面的多态、接口抽象才有地方放。

## 继承的基本语法

咱们先看最简单的继承形态：

```cpp
// 我们相信，一个人起码有名字，更有年龄
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

// 学生一定是一个人
class Student : public Person {
private:
    std::string school_;

public:
    Student(const std::string& name, int age, const std::string& school)
        : Person(name, age), school_(school) {}

    const std::string& school() const { return school_; }
};
```

咱们看 `class Student : public Person` 这一行，它做了三件事：声明 `Student` 是一个从 `Person` 派生出来的类；使用 `public` 继承方式，基类的 `public` 成员在派生类中仍然是 `public` 的；`Student` 对象的内存布局中包含了一个完整的 `Person` 子对象。

所谓"继承"，说得直白一点就是：`Student` 对象内部藏着一个 `Person`。`Student` 拥有 `Person` 的全部成员变量，也拥有 `Person` 的全部公有成员函数，咱们可以对 `Student` 对象调用 `.name()` 和 `.age()`，就像它本来就在 `Student` 里定义了一样。

咱们要特别留意一个细节：`name_` 和 `age_` 是 `Person` 的私有成员，虽然它们存在于 `Student` 对象中，`Student` 的成员函数却**不能直接访问它们**。**私有就是私有，继承也改变不了这一点。**

派生类能直接使用的是基类的公有成员（换而言之，在`public`作用域下的）和受保护成员（），私有成员只能通过基类提供的公有接口来间接操作。这也是 `Student` 构造函数里写 `: Person(name, age)` 的原因：派生类的构造函数必须通过初始化列表把参数传递给基类的构造函数，由基类来完成基类部分的初始化。

> 有 bro 可能会有疑惑：“好复杂哦，为什么会这样呢”。最好的答案依旧是从语义上出发。在经历了几十年软件工程的发展，我们注意到在OOP中，我们的成员和方法一定会分为这几个cases的讨论权限
> 一些成员和方法，你希望**任何人**随意访问。这就是**公开的`public`的**成员
> 一些成员和方法，只希望自己和自己的派生类访问，比如说我不希望Person让别人随意的知晓名字和年龄，但是Student自己是Person，肯定自己知道自己的名字和年龄，这就是**保护的`protected`的**成员
> 最后，就是只希望自己知道自己，甚至自己的派生类都不应该随意越举的，我们叫他**私有的`private`**的成员。
> 这一些我们马上就会介绍的~，这里先给大伙看看。
> 你当然可以随意的设计权限访问，C++没有任何意见，也不会做出约束。但是代价就是你跟设计C的OOP没有半毛钱区别，任何人都可以随意的改动你的成员和方法，直到某一天半夜程序崩溃了，你在找是哪个人在什么时候修改了你的成员。

咱们在派生类的构造函数里要是忘了调用基类的构造函数，编译器会尝试调用基类的默认构造函数（无参构造）。**基类要是没有默认构造函数（比如 `Person` 只有一个 `Person(const std::string&, int)` 而没有 `Person()`），编译直接报错**，而且报错信息有时候看着挺绕的，新手容易在这里卡住。所以记住一条：**基类没有默认构造函数时，派生类必须在初始化列表里显式调用基类的某个构造函数**。

## 构造与析构的顺序

需要知道这个的原因其实很简单，因为我们需要借助这个来理解面对对象的一些基础内容在C++中是如何落地的。

Student 首先是一个Person，其次是一个Student。从这个机理上讲，Student 这个对象我们在锻造的时候，的确应该优先Person的构造。Person有了再加Student独有的内容。这就是一个继承链的构造顺序。即：**构造的时候基类先构造，派生类在其基础上进一步构造**。析构也就更加自然了想到是：派生类作为关系链的最末端先撤离，逐步的拔掉这个对象。

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

创建再销毁一个 `Derived` 对象，输出如下：

```text
Base::Base()
Derived::Derived()
Derived::~Derived()
Base::~Base()
```

咱们记住这个顺序：构造的时候从基类到派生类，因为派生类的构造可能依赖基类成员已经处于合法状态。析构的时候反过来，因为派生类的析构函数可能需要访问基类成员来完成资源清理，如果基类先被析构了，派生类析构函数里访问的就是已经失效的对象。这条规则用一句话记住：**构造从内到外，析构从外到内**。无论继承层次多深，规则都一样。

## 使用基类的成员

派生类可以像使用自己成员一样使用基类的公有成员和受保护成员。咱们来看一个更完整的例子：

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

咱们看 `Person::introduce()` 这个调用。派生类中定义了和基类同名的函数，这叫做**隐藏（hide）**：派生类的 `introduce()` 把基类的 `introduce()` 给遮住了。在 `Student` 对象上直接调用 `introduce()`，执行的是 `Student` 自己的版本；想要复用基类实现，就必须用 `Person::introduce()` 显式指定作用域。

> 同名函数隐藏是 C++ 继承中一个比较隐蔽的坑。咱们在派生类里定义了一个叫 `foo` 的函数，那么基类里所有叫 `foo` 的函数（不管参数列表是否相同）都会被隐藏。
>
> ！！！这不是重载哈！！！重载发生在同一个作用域里，继承跨越了两个作用域。咱们要是想保留基类的重载集合，可以在派生类里写一句 `using Person::introduce;`，把基类的所有重载版本都拉到派生类的作用域里来。

## 对象切片——继承中最容易踩的坑

讲完基本用法，咱们来面对一个真正让新手头疼的问题：**对象切片（Object Slicing）**。

```cpp
void print_person(Person p)   // 按值传递！
{
    p.introduce();
}

Student s("Alice", 20, "MIT");
print_person(s);  // 看起来没问题，实际上已经切片了
```

咱们看这段代码：编译能过，运行也不崩溃，但 `Student` 特有的信息（"I study at MIT"）完全消失了。原因在于 `print_person` 的参数 `p` 是按值传递的 `Person` 类型。编译器在传参时需要把 `Student` 对象拷贝到一个 `Person` 类型的变量里，而 `Person` 的内存空间只够放 `name_` 和 `age_`，`school_` 以及任何 `Student` 特有的东西都被（字面意义上的）"切掉"了。

我去！是编译器出bug了？这都不记得？？？不是滴，咱们要清醒一点。编译器没有 bug，这就是 C++ 值语义的直接后果。解决方案很简单：**用引用或指针，不要用值类型**。

```cpp
void print_person(const Person& p)   // 引用，不切片
{
    p.introduce();
}
```

引用和指针只是指向原始对象的别名或地址，不涉及任何拷贝动作，咱们传进去的对象完整无损。

对象切片不仅发生在函数参数传递时，在容器里也会悄悄出现。咱们要是写了 `std::vector<Person> vec; vec.push_back(student);`，同样会发生切片。正确做法是使用 `std::vector<std::unique_ptr<Person>>` 或者 `std::vector<Person*>` 这样的指针容器。另外，赋值操作 `Person p = student;` 也会切片——任何从派生类到基类的值类型转换都逃不掉这个命运。

## 受保护成员——为继承而生的访问级别

咱们把 `protected` 理解成一个介于 `public` 和 `private` 之间的访问级别：类外部的代码不能访问 `protected` 成员，但派生类的成员函数可以。它专门为继承场景设计，允许派生类"看到"这些成员，同时对外部保持封装。

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

那什么时候该用 `protected`？笔者的建议是：**默认用 `private`，只有当您明确知道派生类需要直接访问某个成员时才改成 `protected`**。过度使用 `protected` 会破坏封装：咱们把内部实现细节暴露给了所有派生类，一旦将来想修改这些细节，影响面就很难控制。一个好的做法是：把需要暴露给派生类的操作封装成 `protected` 的成员函数，而不是直接暴露数据成员。

## 实战：Vehicle 层次结构

现在咱们把前面的知识点串起来。这个程序展示了 `Vehicle` 基类和 `Car`、`Truck` 两个派生类，覆盖构造/析构顺序、成员访问、以及对象切片的对比。

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

编译运行：

```bash
g++ -Wall -Wextra -std=c++17 inheritance.cpp -o inheritance && ./inheritance
```

验证输出：

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

咱们逐段来看：构造 `Car` 时先 `[Vehicle]` 再 `[Car]`，基类先构造。您可能注意到，按引用传递时输出也只有 "Toyota at 120 km/h"，并没有出现 "5 seats"——这是因为 `describe()` 不是虚函数，编译器根据引用的静态类型 `Vehicle&` 绑定了 `Vehicle::describe()`，即使实际对象是 `Car`。但引用传递和值传递有一个关键区别：按值传递时多出了临时 `Vehicle` 副本的构造和析构（这就是切片的直接证据），而引用传递没有这个过程，对象完整，只是函数调用还没"多态"起来。要实现"传引用就能调到派生类版本"，需要虚函数，那是下一章的内容。析构方面，`Truck` 离开块作用域时先析构 `[Truck]` 再析构 `[Vehicle]`，`Car` 在 `main` 结束时析构，析构顺序始终与构造顺序相反。

## 练习

### 练习 1：设计 Animal 层次结构

请您创建一个 `Animal` 基类，包含 `name_`（私有）和 `sound_`（受保护）两个成员，提供 `name()` 公有接口和 `speak()` 方法。然后派生出 `Dog` 和 `Cat`，在构造函数中设置各自的叫声。要求 `Dog` 额外包含 `breed_` 品种字段并提供 `describe()` 方法，验证构造和析构顺序。

### 练习 2：修复对象切片 bug

下面这段代码有对象切片问题，请您找出它并修复：

```cpp
void process(Student s)   // 有 bug
{
    std::cout << s.school() << "\n";
}

Student stu("Bob", 21, "Stanford");
process(stu);
```

提示：把参数改成引用传递。您思考一下：如果函数内部需要存储这个对象（比如放进容器），引用还够用吗？
