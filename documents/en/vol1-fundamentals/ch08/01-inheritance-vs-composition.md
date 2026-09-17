---
chapter: 8
cpp_standard:
- 11
- 14
- 17
- 20
description: Establish the design judgment before the syntax—composition expresses has-a,
  inheritance expresses is-a, the behavioral-substitutability requirement, the misuse of
  inheriting for implementation reuse, and a decision order that starts from composition.
difficulty: intermediate
order: 1
platform: host
prerequisites:
- 类的定义
- 构造函数
reading_time_minutes: 11
tags:
- cpp-modern
- host
- intermediate
- 进阶
title: Inheritance or Composition
translation:
  source: documents/vol1-fundamentals/ch08/01-inheritance-vs-composition.md
  source_hash: f2d09dc9eaa826d78e21bbd6b135ef648a6a7d0a8a9e673fe6e957bcf103339e
  translated_at: '2026-09-17T00:00:00+00:00'
  engine: manual
---
# Inheritance or Composition: Settle the Relationship Before Writing the Class

C++ is a language you can use to do object-oriented programming. See! What a plainly honest assertion.

Right—so from this chapter on, we are going to look at how C++ implements "inheritance" and "composition".

This chapter is about inheritance. But before touching the syntax, we owe ourselves an answer to a more valuable question: when should inheritance be used at all? A syntax mistake gets stopped by the compiler on the spot; a wrong relationship sails through silently—everything compiles, everything runs, until one day the requirements change and the inheritance hierarchy turns out to be immovable, and only then do you discover the tool was wrong at the root. The compiler cannot help us with this kind of mistake; only design judgment can.

The good news: of the two relationships this judgment needs, you already own one. Ever since Chapter 6 introduced class definitions, we have been placing one class's objects inside another class as members—that is composition. This chapter adds inheritance to the toolbox, so this article first sets out what each relationship expresses and when to use which; the four articles that follow cover syntax and mechanics, and we can come back here to check our decisions against this table at any time.

## Composition: The Way You've Been Using All Along

Take a robot as our example: it has two motors, one per wheel. The motor is a class; the robot holds motors as members:

```cpp
class Motor {
public:
    void set_speed(int rpm);
    int speed() const;
};

class Robot {
    Motor left_;
    Motor right_;

public:
    void move(int rpm)
    {
        left_.set_speed(rpm);
        right_.set_speed(rpm);
    }
};
```

The relationship between `Robot` and `Motor` is "a robot **has a** motor"—has-a. The entire mechanism of composition is the member object: the outer class holds the member and calls its public interface whenever it needs the member's capabilities. No new syntax; those two lines of delegation in `move` you have written since Chapter 6 and Chapter 7.

Composition's coupling is naturally low. `Robot` uses `Motor` only through its public interface; however `Motor` is implemented internally—even replaced by another model entirely—`Robot`'s code does not change. That looseness is an important chip in the comparison we make with inheritance later.

## Enter Inheritance: A Glimpse of is-a

Let's look at another kind of relationship. A student is a person; a sedan is a vehicle—"is a", that is, is-a. C++ expresses it with inheritance:

```cpp
class Person { /* name, age ... */ };

class Student : public Person { /* student ID, school ... */ };
```

The line `class Student : public Person` means: `Student` is a kind of `Person`. The syntax details (construction order, access control, object slicing) come in the next article; here we keep our eyes on the semantics—what inheritance buys is not "fewer lines of code" but a **type hierarchy**: a `Student` object can be used as a `Person`, and anything that accepts a `Person` can take a `Student`. The polymorphism later in this chapter is built on exactly this, and it is the essential difference from composition: composition reuses an **implementation**; inheritance declares **substitutability**.

Precisely because inheritance declares substitutability, its coupling is inherently heavy: change the base class's interface or implementation details, and every derived class changes with it. That is why "should we inherit" matters far more than "how to write inheritance"—and why this article comes before the syntax.

## is-a Alone Is Not Enough: Behavior Must Substitute

You might think the criterion is settled: is-a means inheritance, has-a means composition. But here is a classic counterexample; code first:

```cpp
class Ellipse {
public:
    void set_axes(double width, double height);  // the two axes adjust independently
    // ...
};

class Circle : public Ellipse {  // "a circle is a kind of ellipse"?
public:
    // set_axes is inherited, unchanged
};
```

Mathematically, a circle really is a special case of an ellipse; the is-a looks perfectly natural to us. But `Ellipse` promises the outside world that "the two axes can be set independently", while a circle has exactly one radius: after `circle.set_axes(2, 3)`, the object is no longer a circle, and the ellipse's "independent axes" contract is broken too. The inherited interface and the constraint the derived class must maintain end up fighting each other; any code written against "this is an ellipse" breaks the moment it receives this circle.

This is what the **Liskov Substitution Principle** is about: wherever a base class is accepted, substituting a derived class object must keep the program correct. is-a is a necessary condition; behavioral substitutability is the complete criterion. When judging, we go through every contract of the base class interface and ask: can the derived class honor it? If not, the inheritance is wrong—no matter how true the "is a kind of" sounds semantically.

## Inheriting to Reuse Implementation: The Most Common Misuse

Beginners rarely abuse inheritance because they confuse is-a with has-a; more often the motive itself is off: they covet some piece of the base class's implementation and want to "inherit it instead of rewriting it". Let's lay that pattern out:

```cpp
class Printer {
public:
    void print(const std::string& text);
};

// inheriting just to "reuse" print
class TicketMachine : public Printer {
public:
    void issue(const std::string& content)
    {
        print("ticket: " + content);  // uses the base's implementation directly
    }
};
```

Is a ticket machine a kind of printer? No. The only thing this inheritance truly wants is to use this ready-made `print` code. Once the relationship exists, any function accepting `Printer&` can take a ticket machine, and the price paid is being hung on `Printer`'s inheritance tree: the base class changes, this class suffers; it wants a different way of printing, it has to wriggle inside the inheritance relationship.

The same need, written with composition:

```cpp
class TicketMachine {
    Printer printer_;  // has a printer, rather than "is a kind of" printer

public:
    void issue(const std::string& content)
    {
        printer_.print("ticket: " + content);
    }
};
```

Not a bit of reuse is lost, and the relationship is set straight. That is what we mean, concretely, by "composition over inheritance": **to reuse an implementation, use composition plus delegation; inheritance is reserved for cases that truly are "a kind of" and need to be operated uniformly through the base class**.

## A Decision Order

String the conclusions into one actionable path. When a new class needs a relationship with an existing one, ask in this order:

First, **can composition work**: is it only some capability of the other class you want? A member plus delegation suffices, and this step blocks most cases—composition is the default. If you truly need "to be operated through the base class uniformly" (polymorphism, delivered by virtual functions in the next article), then ask **does is-a hold**: is the new class genuinely a kind of the existing one? Even that is not enough; finally ask **is behavior substitutable**: can the new class honor every contract of the base class interface? Only when all three gates pass does public inheritance get its turn.

There is also a stability lens to cross-check with: inheritance belongs to essential, stable relationships (a circle is a shape—that fact does not change), while composition handles accidental, changeable relationships (a shape having a color or a border is an attached attribute). The more likely a relationship is to change, the more it should be composition—`ColoredShape` in this chapter's hands-on finale will act this judgment out exactly as written.

| | Inheritance | Composition |
|---|---|---|
| Semantics | is-a (is a kind of) | has-a (has a) |
| Coupling | High—derived classes depend on the base's interface and implementation details | Low—only through the member's public interface |
| Fits | Essential, stable relationships | Accidental, changeable relationships |
| What gets reused | Substitutability (type hierarchy) | Implementation (member plus delegation) |

With the judgment in place, the next article covers inheritance itself: how the syntax is written, in what order construction and destruction execute, and what object slicing is about. Abstract classes, multiple inheritance, and the practice finale later in this chapter all return to this article's judgment again and again.

## Try It Yourself

### Exercise 1: is-a or has-a

Judge the four relationships below—which are is-a (inheritance candidates) and which are has-a (composition candidates):

1. Uav and FlyingMachine
2. Uav and GpsModule
3. Rectangle and Polygon
4. Polygon and a vertex list (`std::vector<Point>`)

::: details Reference answer

Let's judge them one by one. 1 is is-a: a UAV is a kind of flying machine.
2 is has-a: a UAV has a GPS module.
3 is is-a: a rectangle is a kind of polygon.
4 is has-a: a polygon has a vertex list.

Note the contrast between 1 and 2: the same class can be on the inheritance side toward one class and the composition side toward another; the two do not interfere. And 3 still has to pass the "behavior substitutable" gate before real inheritance—following this article's circle-and-ellipse reasoning, think about which of a rectangle's contracts other polygons might not honor. Container-as-member relationships like 4 almost always choose composition.

:::

### Exercise 2: Fix the Wrong Inheritance

What is wrong with the code below? Rewrite it correctly:

```cpp
class UartDriver {
public:
    void send(const std::uint8_t* data, std::size_t n);
};

class TemperatureSensor : public UartDriver {
public:
    double read();
    // read's implementation calls send directly to fire off a request frame
};
```

::: details Reference answer

The mistake is the motive, and let's name it plainly: a temperature sensor is not a kind of UART driver; this inheritance exists purely to reuse `send`'s implementation. The consequence is that any function accepting `UartDriver&` can take a temperature sensor, and the sensor is chained to `UartDriver`'s inheritance tree. Rewritten with composition:

```cpp
#include <cstddef>
#include <cstdint>

class UartDriver {
public:
    void send(const std::uint8_t* data, std::size_t n);
};

class TemperatureSensor {
    UartDriver uart_;  // has a UART driver

public:
    double read();
    // read's implementation sends the request frame via uart_.send(...)
};
```

The reuse still arrives through the delegation `uart_.send(...)`, and the type relationship is set straight: the sensor **has a** UART driver. When the transport changes later (to SPI, say), only how the member is swapped and used is affected; `TemperatureSensor`'s outward identity never moves.

:::
