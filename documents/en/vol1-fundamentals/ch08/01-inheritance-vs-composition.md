---
chapter: 8
cpp_standard:
- 11
- 14
- 17
- 20
description: Establish the design judgment before entering inheritance syntax—composition
  expresses has-a, inheritance expresses is-a, the trade-off between behavioral substitutability
  and implementation reuse, and a decision order that starts from composition.
difficulty: intermediate
order: 1
platform: host
prerequisites:
- Class Definition
- Constructors
reading_time_minutes: 11
tags:
- cpp-modern
- host
- intermediate
- 进阶
title: Inheritance or Composition
translation:
  source: documents/vol1-fundamentals/ch08/01-inheritance-vs-composition.md
  source_hash: 2bcec7bc0b72d0adbe2d2253c1756ca140e0ce6019fea46c0d7ee13b51ea165a
  translated_at: '2026-09-25T11:28:37+00:00'
  engine: anthropic
  token_count: 2400
---
# Inheritance or Composition: Settle the Relationship Before Writing the Class

C++ is a language you can use to do object-oriented programming. See that? What a plainly honest assertion.

Right, then—starting with this chapter, we are going to look at how C++ gets "inheritance" and "composition" implemented.

Hey, hold on! Put VSCode—or your flashy neovim—down for a moment. When we sit down to write code, what we need even more is to think one important question through, and that question is design. Your author has seen plenty of learners—himself squarely included—who fell hard for OOP in the early days of learning. Any task at all, and before the idea is even thought through, out comes an interface. Then when the project gets audited at the end, the utterly superfluous designs turn out to have caused enormous code bloat. Scarier still: once the design goes wrong, the project faces a rewrite. Inheritance skipped exactly where inheritance belonged, composition skipped exactly where composition belonged, and in the end the interfaces come out painful to write; you get to grumble that programming is truly irritating, and there goes the fun.

> An LLM chimes in: when should inheritance be used? A syntax slip gets stopped by the compiler on the spot; a wrong relationship gets not a peep out of the compiler—the code compiles as usual and runs as usual, until one day the requirements change, the inheritance hierarchy refuses to budge, and only then do you discover the tool was wrong at the root. The compiler cannot help us with this kind of mistake; only design judgment can. I find this one good, so it gets excerpted and parked here as well.

## Composition: The Approach You Have Been Using All Along

Let's take a robot as our example: it has two motors, one for the left wheel and one for the right. The motor is a class; the robot holds motors as members:

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

The relationship between `Robot` and `Motor` is "a robot **has a** motor"—has-a. The entire mechanism of composition is the member object: the outer class holds the member, and calls the member's public interface whenever it needs its capabilities. No new syntax—those two lines of delegation inside `move` are things you already wore smooth back in Chapter 6 and Chapter 7.

Composition's coupling is naturally low. `Robot` uses `Motor` only through its public interface; however `Motor` is implemented internally—even if it gets swapped for another model someday—`Robot`'s code does not have to change. **That looseness is an important bargaining chip for when we pit composition against inheritance later. To stress the point: your author uses composition almost exclusively, reaching for inheritance only in the most necessary scenarios. Why that is, you will come to feel as you keep writing code—that territory belongs to design patterns and software engineering, and we will not wander that far here.**

## Enter Inheritance: A Glimpse of the is-a Relationship

Now let's look at another kind of relationship. A student is a kind of person, a sedan is a kind of vehicle—"is a kind of", that is, is-a. C++ expresses it with inheritance:

```cpp
class Person { /* name, age ... */ };

class Student : public Person { /* student ID, school ... */ };
```

The line `class Student : public Person` means: `Student` is a kind of `Person`. The syntax details (construction order, access control, object slicing) open up in the next article; here we keep our eyes locked on the semantics—what inheritance buys is not "a few lines of code saved", it is a **type hierarchy**: a `Student` object can be used as a `Person`, and anywhere that accepts a `Person` can take in a `Student`. The polymorphism later on is built on exactly this, and it is the most essential difference between inheritance and composition: what composition reuses is an **implementation**; what inheritance declares is **substitutability**.

Precisely because inheritance declares substitutability, its coupling is heavy by birth: the moment the base class's interface or implementation details change, every derived class changes with them. That is why "should this be inheritance" matters far more than "how do I write inheritance"—and it is exactly why we put this discussion in front of the syntax.

## is-a Alone Is Not Enough: Behavior Has to Substitute

You might feel the criterion is already clear: is-a means inheritance, has-a means composition. But there is a classic counterexample, and we will look at the code first:

```cpp
class Ellipse {
public:
    void set_axes(double width, double height);  // the two axes adjust independently
    // ...
};

class Circle : public Ellipse {  // "a circle is a kind of ellipse"?
public:
    // set_axes is inherited as-is
};
```

Mathematically, a circle really is a special case of an ellipse, and this is-a looks self-evident to us. But `Ellipse` has promised the outside world that "the two axes can be set independently", while a circle has exactly one radius: after calling `circle.set_axes(2, 3)`, the object is no longer a circle, and the ellipse's "independent axes" contract is broken as well. **The inherited interface and the constraint the derived class must maintain end up fighting each other; any code written against "this is an ellipse" goes wrong the moment it receives this circle.**

That is what the **Liskov Substitution Principle** is talking about, and I badly want to wrap it up in a quotation:

> **Wherever a base class is accepted, swapping in a derived class object must keep the program's behavior correct. is-a is the necessary condition; behavioral substitutability is the complete criterion. When judging, we must go down every contract of the base class interface and ask once: can the derived class hold it? If it cannot, the inheritance is wrong—no matter how semantically true the "really is a kind of" sounds**.

## Inheriting to Reuse Implementation: The Most Common Misuse

When beginners abuse inheritance, it is mostly not because they cannot tell is-a from has-a—the motive itself is tilted: they have taken a liking to some piece of the base class's implementation and want to "inherit it and spare themselves the rewrite". Let's lay that pattern out:

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
        print("ticket: " + content);  // uses the base class's implementation directly
    }
};
```

Let's ask the question: is a ticket machine a kind of printer? No. The one sentence this inheritance truly wants to express is: use this ready-made `print` code. Once the relationship is established, any function accepting a `Printer&` can take in a ticket machine, and the price it pays for that is hanging itself on `Printer`'s inheritance tree—the base class changes, and it takes the hit along with it; it wants a different way of printing, and it has to wriggle around inside this inheritance relationship to get it.

The same need, written with composition, looks like this:

```cpp
class TicketMachine {
    Printer printer_;  // has a printer, not "is a kind of" printer

public:
    void issue(const std::string& content)
    {
        printer_.print("ticket: " + content);
    }
};
```

Not a shred of reuse is lost, and the relationship is set right while we are at it. That is the concrete meaning of what we say—"**composition over inheritance**": **to reuse an implementation, use composition plus delegation; inheritance is reserved for scenarios that genuinely are "a kind of" and need to be operated uniformly through the base class**.

## A Decision Order

String the preceding conclusions into one actionable decision path. When we want to build a relationship between a new class and an existing one, we ask in this order:

First ask **can composition do it**: is all you want some one capability of the other class? A member plus delegation suffices, and this one step blocks out most cases for us—composition is the default option. Only when we truly need "to be operated uniformly through a base class" (polymorphism; the next article's virtual functions supply that layer of capability) do we ask **does is-a hold**: is the new class genuinely a kind of the existing one? Even if it holds, that is still not enough—last, ask **is the behavior substitutable**: every contract of the base class interface, can the new class honor them all? Only when all three gates pass does public inheritance get its turn.

There is also a stability lens that can help us corroborate: inheritance is left to essential, stable relationships (a circle is a kind of shape—that fact does not change), while composition handles incidental, likely-to-change relationships (a shape carrying a color, carrying a border—those are attributes bolted on afterwards). The more likely a relationship is to change, the more it should be composition—in the hands-on finale at the end of this chapter, `ColoredShape` will play this judgment out exactly as written.

| | Inheritance | Composition |
|---|---|---|
| Semantics | is-a (is a kind of) | has-a (has a) |
| Coupling | High—derived classes depend on the base class's interface and implementation details | Low—only through the member's public interface |
| Relationships it fits | Essential, stable ones | Incidental, likely-to-change ones |
| What gets reused | Substitutability (the type hierarchy) | The implementation (member plus delegation) |

With the decision method standing firm, the next article makes inheritance itself clear: how the syntax is written, in what order construction and destruction execute, and what object slicing is about. The abstract classes, multiple inheritance, and hands-on finale later in this chapter all come back to this article's judgment again and again.

## Try It Yourself

### Exercise 1: is-a or has-a

Please judge the four relationships below: which are is-a (worth considering inheritance for), and which are has-a (should use composition)? Note that they may well have no standard answer at all~

1. Uav and FlyingMachine
2. Uav and GpsModule
3. Rectangle and Polygon
4. Polygon and a vertex list (`std::vector<Point>`)

### Exercise 2: Rewrite the Wrong Inheritance as Composition

What is wrong with the code below? Please rewrite it correctly:

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
