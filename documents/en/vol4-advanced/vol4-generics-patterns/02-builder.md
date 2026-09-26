---
title: 'Builder Pattern: From a Mess of Constructor Arguments to a Fluent Builder'
description: 'Starting from the most primitive "mess of constructor arguments", we push step by step toward a fluent builder, use std::optional along the way to kill off the isValid flag, and finally press "forgot a required field" from a run-time error into a compile-time one with a staged builder'
chapter: 11
order: 2
tags:
  - host
  - cpp-modern
  - intermediate
  - 构建器模式
difficulty: intermediate
platform: host
cpp_standard: [11, 17, 20]
reading_time_minutes: 22
related:
  - 'Singleton Pattern: From Comment-Only Constraints to Meyer''s Singleton'
prerequisites:
  - Classes and Object-Oriented Programming
translation:
  source: documents/vol4-advanced/vol4-generics-patterns/02-builder.md
  source_hash: 71bd26712cc72b900d6681eaf4699d1a87dd71e0ab8609508ae2456b48c7dded
  translated_at: '2026-09-26T05:04:51+00:00'
  engine: anthropic
  token_count: 13000
---
# Builder Pattern: From a Mess of Constructor Arguments to a Fluent Builder

## What Problem Are We Actually Solving

Picture a really plain scenario. You write a to-do item `Task`: it has required fields — priority, deadline, task description — and optional fields — title and notes. In version one you take the lazy route and give `Task` a single constructor that lists every field, and the call site looks like this:

```cpp
Task* a_task = new Task(
    Task::Priority::High,
    CTime{2025, 9, 24, 20, 38, 11},
    "This is a Demo Task",
    "Demo Tasks are placed for a detailed test",
    "A Task");
```

You finish writing it and stare at these lines for three seconds. The problems are already bubbling up: the caller has to memorize the argument order — whether the title goes in slot three or slot five depends entirely on counting commas; the day you want to add a new field, say external `links`, this constructor's signature changes and thousands of call sites across the whole repository have to change with it; worse, once a constructor grows complicated, it can throw in flows you do not control — construction fails, the object never exists at all, yet you are already clutching a half-initialized state you can neither catch nor patch. Your colleagues have already put you on trial with a merciless git blame, and you are spinning in circles in a panic.

The real problem is that in one line of code we are **doing three things at once**: first, submitting the construction materials (that pile of arguments); second, executing the construction procedure itself (validation, assignment, maybe even connecting to a database); third, making `a_task` actually point to a legally existing `Task` object. Submit the materials, execute the construction, deliver the object — these three steps are welded dead into a single constructor, and we have no point where we can step in.

> A note: I have touched a bit of Java before, and what I saw there was builders being abused. So my position is — bring in a builder only when you find your scenario has genuinely grown as complicated as the one above; otherwise, just construct the object the plain way.

What the Builder pattern solves is exactly this: **split the three steps — "collect the materials", "validate", "actually construct" — apart and hand them to a dedicated middleman, the Builder, so that client programmers get the chance to assemble an object step by step, elegantly and pluggably.** From here we go step by step, starting from the dumbest version, and see why each step is still not enough.

## Step 1: The Most Primitive Approach — One Giant Constructor (a Cautionary Example)

Many people's first instinct is exactly that pile above. Honestly, writing small objects this way is completely fine — `Point(int x, int y)` feels comfortable to everyone. But once the fields go past four or five, mixed required and optional, this constructor starts being hostile to humans.

There are two layers of trouble here. The first is **readability**: five `std::string`s squeezed together, and you cannot tell which is the title, which the description, which the notes; the IDE's parameter hints can save you at the dev machine, but they cannot save the naked eye during code review. The second layer is sneakier — **coupling**: the constructor shoulders the full set of duties of "receive arguments + validate legality + maybe perform some side effects (write logs, connect to a database)", and once any of those fails, you cannot even get a "half-constructed" object — a constructor either succeeds or throws its way out; there is no buffer zone in between.

You might think: fine, no exceptions then — I add a `bool is_valid{false}` member and manually check after construction. That road is walkable too, but the price is that every `Task` object from now on carries an `is_valid` flag, business code ends up checking `if (task.is_valid)` everywhere, and the class's state is polluted into a complete mess by one "am I valid" flag. **The whole point of using objects is to encapsulate state — and now we have encapsulated our way into a flag that announces "I might be a garbage object".**

So this road dead-ends too. We need construction to become something that can proceed in steps, can be inspected midway, and can have its validation logic moved out of `Task` itself.

## Step 2: Simplifying with getters/setters — Moving Optional Fields Out of the Constructor

Experienced readers are already muttering: optional fields should never have gone into the constructor in the first place — just give them a getter/setter pair and be done. Absolutely right. We first split the fields into two groups — required fields that "must be valid before the `Task` exists", and optional fields that "can be configured later at leisure". Required fields stay in the constructor; optional fields get set afterward via setters:

```cpp
class Task {
public:
    enum class Priority { Immediate, High, Medium, Low };
    struct CTime { int year, month, day, hour, minute, second; };

    // Required: priority, deadline, description
    Task(Priority p, CTime ddl, const std::string& desc)
        : priority_(p), ddl_(ddl), description_(desc) {
        if (desc.empty()) {
            throw std::invalid_argument("Invalid Task Description");
        }
        // Maybe write logs, connect to a database...
    }

    void set_title(std::string t)   { title_ = std::move(t); }
    void set_details(std::string d) { details_ = std::move(d); }

private:
    Priority                       priority_;
    CTime                          ddl_;
    std::string                    description_;
    std::optional<std::string>     title_;
    std::optional<std::string>     details_;
};
```

This step is already much better than the giant constructor — the constructor slimmed down, and optional fields can be filled in as needed. But look at the `Task` class now and you will find it carrying two responsibilities: **it is both "a business object representing a to-do item" and "the tool that constructs itself"**. The validation logic, the setters, that log-writing side effect — all crammed into `Task`. Construction logic and business logic are stirred together, and the class gets dirtier and dirtier.

What grates even more is that a failed validation can still only throw. Once `Task` grows complicated, the constructor piles up ever more validation, assignment, and side effects; if you want to switch the failure-handling strategy (say, from throwing to returning an error code), you have to modify `Task` itself — but `Task` is a business object referenced all over the repository, and touching a single hair of it drags a whole crowd into review. (And, as a bonus, you get flamed for it.)

And it still does not end here. What we really want to ask is: **can we pull "how to construct" out of `Task` as a whole and hand it to a dedicated tool class?** That way `Task` minds only its own business semantics, while construction details, validation strategy, and failure fallback all belong to that tool — neither bothering the other.

## Step 3: Delegating the Construction Job — A Simple Builder

This "tool class dedicated to construction" is the **Builder**. We have `Task` recognize the `Builder` as a friend and keep only a private "stuff the fields in" opening for itself, while all the collecting, validating, and assembling work goes to `TaskBuilder`.

There is a particularly convenient design decision here: "has this field been filled" is no longer tracked with `bool` flags but directly with `std::optional`. A `std::optional<Task::Priority>` is at once "a container for a `Priority` value" and the switch for "has this value been filled" — you test `if (priority_)` exactly like checking a pointer, and `*priority_` fetches the value. A pile of `is_xxx_set` flags disappears, and the class's state stays squeaky clean.

```cpp
class TaskBuilder {
public:
    void set_priority(Task::Priority p) { priority_ = p; }
    void set_ddl(Task::CTime d)          { ddl_ = d; }
    void set_description(std::string d)  { description_ = std::move(d); }
    void set_title(std::string t)        { title_ = std::move(t); }
    void set_details(std::string d)      { details_ = std::move(d); }

    std::optional<Task> build() const {
        // If required fields are not all filled in, return nullopt — failure is internalized into the return type
        if (!priority_ || !ddl_ || !description_) {
            return std::nullopt;
        }
        Task t(*priority_, *ddl_, *description_);
        if (title_)   t.set_title(*title_);
        if (details_) t.set_details(*details_);
        return t;
    }

private:
    std::optional<Task::Priority> priority_;
    std::optional<Task::CTime>    ddl_;
    std::optional<std::string>    description_;
    std::optional<std::string>    title_;
    std::optional<std::string>    details_;
};
```

You see, the validation logic now lives in `TaskBuilder`, fully decoupled from `Task`'s business body. `build()` returns `std::optional<Task>`, which means "construction may fail" is encoded directly into the return type — when the caller receives the result, it is forced to handle the "might be `nullopt`" layer, and you can never again forget the failure branch. Compared with throwing exceptions, this style is steadier: a failed construction is "a normally anticipated outcome", not a control-flow jump that suddenly explodes out of nowhere.

::: tip std::optional is a wonderfully handy utility class
We can check whether a member has been filled exactly the way we check a pointer — `if (priority_)` to test, `*priority_` to take the value. Now we finally do not have to maintain a pile of `is_xxx_valid` bool flags: "is there a value" is internalized straight into `std::optional`'s type semantics.
:::

Usage looks like this — one setter per line, and finally `build()`:

```cpp
TaskBuilder builder;
builder.set_priority(Task::Priority::High);
builder.set_ddl({2025, 9, 25, 10, 0, 0});
builder.set_description("Prepare blog post");
builder.set_title("Simple Builder");
builder.set_details("Non-fluent style");

std::optional<Task> maybe_task = builder.build();
if (maybe_task) {
    maybe_task->do_work();
}
```

Nice — it runs. But as you keep writing, you start to feel the fatigue: for every field you set you have to repeat `builder.` once, and five or ten rounds of that make your eyes blur and your hands ache. Anyone who has used Kotlin's `apply` or written jQuery is more sensitive to this: **this kind of "chained" API could clearly be strung together in a single sentence — why break it into ten lines?**

## Step 4: Getting the Builder to Flow — The Fluent Builder

The trick is almost free: each `with_*` method, after setting its field, **returns a reference to the builder itself**, `return *this;`. That way the return value of the previous call is the builder itself, and you can immediately hang the next call on it — and the call chain starts to flow.

```cpp
class TaskBuilder {
public:
    TaskBuilder& with_priority(Task::Priority p) {
        priority_ = p;
        return *this;
    }
    TaskBuilder& with_ddl(Task::CTime d)         { ddl_ = d;            return *this; }
    TaskBuilder& with_description(std::string s) { description_ = std::move(s); return *this; }
    TaskBuilder& with_title(std::string t)       { title_ = std::move(t);       return *this; }
    TaskBuilder& with_details(std::string d)     { details_ = std::move(d);     return *this; }

    Task build() const {
        if (!priority_ || !ddl_ || !description_) {
            throw std::runtime_error("Cannot build Task: missing required field");
        }
        Task t(*priority_, *ddl_, *description_);
        if (title_)   t.set_title(*title_);
        if (details_) t.set_details(*details_);
        return t;  // RVO
    }

private:
    std::optional<Task::Priority> priority_;
    std::optional<Task::CTime>    ddl_;
    std::optional<std::string>    description_;
    std::optional<std::string>    title_;
    std::optional<std::string>    details_;
};
```

Note that here I casually switched `build()`'s failure strategy from "return `std::optional`" to "throw an exception". Both are legitimate engineering choices; the difference lies in how you view "construction failed": if you see it as an anticipated, low-probability event the caller should handle in passing, `std::optional` fits better — failure is encoded into the type; if you see "calling build without all required fields filled" as the programmer having screwed up — a logic error that should never happen — throwing is more direct and bubbles the error up to a unified top-level fallback. We use exceptions here because they make the later demonstrations clearer.

The call site suddenly reads like a sentence:

```cpp
Task task = TaskBuilder{}
                .with_priority(Task::Priority::High)
                .with_ddl({2025, 9, 25, 10, 0, 0})
                .with_description("Finish Builder blog")
                .with_title("Blog Writing")
                .with_details("Explain fluent builder")
                .build();
```

Here let us first verify that this chain of calls really runs through, and that a missing required field really throws:

```sh
$ g++ -std=c++23 -O2 -Wall -Wextra builder_verify.cpp -o builder_verify
$ ./builder_verify
Task{desc=Finish Builder blog, prio=1, ddl=2025-9-25, title=Fluent Builder, details=return *this chains the calls}
caught: Cannot build Task: missing required field
```

The fully constructed object has every field in place; the run that omitted `priority` and `ddl` got intercepted straight by `build()` and threw. Chained calls, the `std::optional`-as-flag technique, run-time validation — all three line up.

Then the next question arrives. Chaining has a side effect: it turns the builder itself into **an intermediate state that can be passed around** — which happens to be another of its talents: deferred construction. Look: since every `with_*` returns the builder itself, we can perfectly well "pause" the construction process at some step, toss the builder into another subsystem as an argument, and let that side keep filling once it has queried the database and gotten the real title:

```cpp
auto partial = TaskBuilder{}
                   .with_priority(Task::Priority::High)
                   .with_ddl({2025, 9, 25, 10, 0, 0})
                   .with_description("Complete the final project report.");

// Hand the half-built builder out; finish building once the async query returns the real title
std::string title = data_base.query_title_by_time({2025, 9, 25, 10, 0, 0});
Task task = partial.with_title(title)
                  .with_details("Check all data points.")
                  .build();
```

You will find a remarkably valuable benefit here: **from now on, every `Task` object circulating in the code is a "fully constructed, fields-valid" object** — no more embarrassment of "half-initialized `Task`s sprinting all over the world". The half-finished state is locked inside `TaskBuilder`; only at the moment `build()` walks out the gate does it release one complete `Task`. The type system isolates "finished goods" from "work in progress" for us.

::: warning Do not reuse one builder across threads
The fluent builder carries mutable state. One `TaskBuilder` being `with_*`-ed and then `build()`-ed by two threads at once has no synchronization whatsoever on its field reads and writes — that is a plain data race. Either give each thread its own builder instance, or treat the builder as a "construct-once-then-discard" temporary — the `TaskBuilder{}...build()` form above destroys the builder as soon as it is used, which is the safest usage. To hand a half-built builder across threads, either pass a copy by value or honestly add a lock.
:::

## Let's Verify This First: Whether RVO Really Saves That Copy

`build()` has a local object `t`, and then `return t;`. Intuitively, hauling a big object out of a function should cost at least one move, right? Let's not take that on faith — measure it, by hanging a move/copy counter on the object:

```cpp
class Tracked {
public:
    int v;
    static inline int kMoveCount = 0;
    static inline int kCopyCount = 0;

    Tracked() : v(0) {}
    explicit Tracked(int x) : v(x) {}
    Tracked(Tracked&& o) noexcept : v(o.v) { ++kMoveCount; }
    Tracked(const Tracked& o) : v(o.v)     { ++kCopyCount; }
};

class TrackedBuilder {
public:
    TrackedBuilder& with_value(int x) { value_ = x; return *this; }
    Tracked build() const {
        Tracked t(*value_);   // local object
        return t;             // expected to be elided by NRVO / RVO
    }
private:
    std::optional<int> value_;
};

int main() {
    Tracked t = TrackedBuilder{}.with_value(42).build();
    std::cout << "value=" << t.v
              << " moves=" << Tracked::kMoveCount
              << " copies=" << Tracked::kCopyCount << "\n";
}
```

To rule out the suspicion that "the optimizer is doing magic under `-O2`", we run it once each at `-O2` and `-O0`:

```sh
$ g++ -std=c++23 -O2 rvo_verify.cpp -o rvo_verify && ./rvo_verify
value=42 moves=0 copies=0
$ g++ -std=c++23 -O0 rvo_verify.cpp -o rvo_verify_O0 && ./rvo_verify_O0
value=42 moves=0 copies=0
```

With optimizations off, moves and copies are still 0. This is not a gift from the compiler — it is a **standard guarantee**: since C++17, when `return`-ing a same-named local object, the copy/move **is mandatorily elided** (*mandatory copy elision*); the object is constructed directly in the caller's stack frame, and the "first build a temporary then haul it over" step never happens at all. So we can `return t;` in `build()` with peace of mind: however heavy `Task` is, we never pay the price of a copy.

> Note: this perk only arrived with C++17, so do not rush to rely on it under C++11~14 — there it will very likely only work with optimizations enabled.

## The Real Trap Comes Later: Discovering a Forgotten Required Field at Run Time

The fluent builder is good, but it has one flaw you simply cannot get around — **validation of required fields can only be dragged out to run time**. You write `TaskBuilder{}.with_ddl(...).build()`, leaving out `priority` and `description`; the compiler says not one word, compiles it smoothly, and only when the program runs and `build()` throws do you suddenly see the light.

Where does the problem sit? In the `TaskBuilder` type itself. It expresses "a builder that filled in priority", "a builder that filled in priority and ddl", and "a builder with everything filled in" all as **the same type**, `TaskBuilder`. The type system cannot tell them apart, so naturally it cannot check on your behalf at compile time — all it sees is one `TaskBuilder` "whose fields are not yet filled"; whether you call `with_priority` is your business, outside its jurisdiction.

Is there a way to get the type system involved? Yes. The idea: **every time a required field is filled, the builder "transforms" into a new type; only after walking through all required stages do you obtain the type that "can `build()`".** Miss any single step, and the type in your hand simply has no `build()` method — the compiler stops you on the spot. This is the **staged builder** (also called a typed builder).

## Step 5: Pressing Required-Field Validation into Compile Time — The Staged Builder

We first define an internal draft, `TaskDraft`, that gathers all the fields; it will be moved along between stages. Then we give each "fill a field" step a type of its own — `SetPriority`, `SetDdl`, `SetDescription`, `OptionalStage` — and each type's `with_*` method returns **the next stage's type**:

```cpp
struct TaskDraft {
    std::optional<Task::Priority> priority;
    std::optional<Task::CTime>    ddl;
    std::optional<std::string>    description;
    std::optional<std::string>    title;
    std::optional<std::string>    details;
};

struct SetDdl;
struct SetDescription;
struct OptionalStage;

struct SetPriority {
    TaskDraft d;
    SetDdl with_priority(Task::Priority p);          // returns the next stage
};
struct SetDdl {
    TaskDraft d;
    SetDescription with_ddl(Task::CTime ddl);        // returns the next stage
};
struct SetDescription {
    TaskDraft d;
    OptionalStage with_description(std::string desc);  // enters the optional stage
};
struct OptionalStage {
    TaskDraft d;
    OptionalStage& with_title(std::string t)   { d.title = std::move(t);   return *this; }
    OptionalStage& with_details(std::string det) { d.details = std::move(det); return *this; }
    Task build() {
        // The three required fields are enforced as filled by the type system; no runtime validation needed here
        Task t(*d.priority, *d.ddl, std::move(*d.description));
        if (d.title)   t.set_title(*d.title);
        if (d.details) t.set_details(*d.details);
        return t;
    }
};
```

Note one key difference: inside `OptionalStage::build()` **there is no longer any `if (!priority || ...)` validation**. Why is it unnecessary? Because the type system has already guaranteed it for you: the only path to obtain the `OptionalStage` type is walking, in order, through `with_priority` → `with_ddl` → `with_description` — and every step fills the corresponding `optional` in. By the time `build()` is reached, the three required fields are necessarily non-empty, and dereferences like `*d.priority` are absolutely safe. That is the taste of "pressing a run-time check into a compile-time guarantee".

Usage is a strictly ordered chain like this:

```cpp
struct TaskBuilder {
    static SetPriority create() { return SetPriority{TaskDraft{}}; }
};

Task t = TaskBuilder::create()
             .with_priority(Task::Priority::High)
             .with_ddl({2025, 9, 25, 10, 0, 0})
             .with_description("Staged builder")
             .with_title("Typed")
             .build();
```

First let us verify the correct usage runs:

```sh
$ g++ -std=c++23 -O2 -Wall -Wextra staged_builder_verify.cpp -o staged_builder_verify
$ ./staged_builder_verify
Task{desc=Staged builder, title=Typed}
```

Now for the moment of witnessing its power. We deliberately commit the two most common mistakes and watch how the compiler blocks them.

The first: **build with a required field missing**. Suppose we filled only `priority` and `ddl`, skipped `with_description`, and went straight for `.build()`:

```cpp
Task t = TaskBuilder::create()
             .with_priority(Task::Priority::High)
             .with_ddl({2025, 9, 25, 10, 0, 0})
             .build();   // ← attempting to call build() on a SetDescription
```

The compiler's reaction:

```sh
$ g++ -std=c++23 staged_missing.cpp -o staged_missing
staged_missing.cpp:7:19: error: 'struct SetDescription' has no member named 'build'
```

The `SetDescription` type has no `build()` method at all — `build()` exists only on `OptionalStage`. You cannot get an `OptionalStage` (because `with_description` was never called), so naturally you cannot build. A missing required field: a straight red card at compile time.

The second: **the order is written backwards**. Someone's fingers are quick, and `with_ddl` gets written before `with_priority`:

```cpp
auto x = TaskBuilder::create()
             .with_ddl({2025, 9, 25, 10, 0, 0});   // ← calling with_ddl() on a SetPriority
```

The compiler's reaction:

```sh
$ g++ -std=c++23 staged_wrongorder.cpp -o staged_wrongorder
staged_wrongorder.cpp:4:36: error: 'struct SetPriority' has no member named 'with_ddl'
```

`SetPriority` has no `with_ddl` method — `with_ddl` is the `SetDdl` stage's business. You must first `with_priority` and transform into a `SetDdl` before you are entitled to call `with_ddl`. The call order is nailed down hard by the type flow.

That is the hard evidence of the staged builder pressing both mistakes into compile-time errors: **a missing required field and an out-of-order call both fail to compile.** The whole run-time-exception business is dispensed with.

::: tip The price of the staged builder
There is no free lunch. This mechanism's price is **more complicated type design** — every required stage needs its own struct, and fields are moved along between stages. Once fields multiply, the number of stages grows with them. So it fits scenarios of "few required fields, but absolutely none may be missed" (protocol headers, security-related configuration, say); if your object has a big pile of optional fields and just two or three required ones, the plain fluent builder plus run-time validation from earlier is usually enough — no need to shoulder the type bloat just for compile-time checking.
:::

## Step 6: Splitting Up Responsibilities — The Composite Builder

Looking back, the fluent builder piles every `with_*` method into a single `TaskBuilder` class. Once fields multiply, this class balloons into an all-encompassing "super constructor" — required ones, optional ones, even ones "grouped by business domain" (say, "security-related fields", "logging-related fields") all squeezed together. The day you want to add a new group of fields for some domain, you have to modify the `TaskBuilder` body — which violates the open-closed principle (OCP) we spent so much effort chasing.

The composite builder's idea is to slice the responsibilities apart: **one base builder holds all the fields and handles the final `build()`; around it, several sub-builders are derived, each responsible for one category of fields.** A sub-builder does not hold a copy of the fields but a reference to the base builder — after setting its fields, it calls a `done_xxx()` to switch back to the base builder, and from there you jump to the next sub-builder. Want to add a new group of fields? Write a new sub-builder and hang it on — the base builder and the other sub-builders do not move a single line.

```cpp
class TaskBuilder;        // Base builder: holds all fields + build()
class BuilderMain;        // Sub-builder A: owns the required fields
class BuilderOptional;    // Sub-builder B: owns the optional fields

class TaskBuilder {
public:
    std::optional<Task::Priority> priority;
    std::optional<Task::CTime>    ddl;
    std::optional<std::string>    description;
    std::optional<std::string>    title;
    std::optional<std::string>    details;

    BuilderMain     main();       // enter the "required fields" sub-builder
    BuilderOptional optional();   // enter the "optional fields" sub-builder

    Task build() const {
        if (!priority || !ddl || !description) {
            throw std::runtime_error("Task build error: missing required field");
        }
        Task t(*priority, *ddl, *description);
        if (title)   t.set_title(*title);
        if (details) t.set_details(*details);
        return t;
    }
};

class BuilderMain {
public:
    explicit BuilderMain(TaskBuilder& b) : b_(b) {}
    BuilderMain& with_priority(Task::Priority p) { b_.priority = p;            return *this; }
    BuilderMain& with_ddl(Task::CTime d)         { b_.ddl = d;                 return *this; }
    BuilderMain& with_description(std::string s) { b_.description = std::move(s); return *this; }
    TaskBuilder& done_main() { return b_; }       // required fields set; switch back to the base builder
private:
    TaskBuilder& b_;
};

class BuilderOptional {
public:
    explicit BuilderOptional(TaskBuilder& b) : b_(b) {}
    BuilderOptional& with_title(std::string t)   { b_.title = std::move(t);   return *this; }
    BuilderOptional& with_details(std::string d) { b_.details = std::move(d); return *this; }
    TaskBuilder& done_optional() { return b_; }   // optional fields set; switch back to the base builder
private:
    TaskBuilder& b_;
};

BuilderMain     TaskBuilder::main()     { return BuilderMain(*this); }
BuilderOptional TaskBuilder::optional() { return BuilderOptional(*this); }
```

This code has a few details worth taking apart. The base builder's fields are all `public` — not to save effort, but so that the sub-builders can read and write them directly, skipping layer upon layer of getters/setters. The sub-builders hold a `TaskBuilder&` reference rather than a copy, so "setting a field in `BuilderMain`" and "setting a field in `BuilderOptional`" are really mutating the same base builder, and the final `build()` reads that same shared state. `done_main()` / `done_optional()` return a reference to the base builder, which lets the switching "sub-builder → base builder → another sub-builder" string into one chain.

The call site therefore looks like a sentence broken into paragraphs — first enter `main()` to set the required fields, `done_main()` back to the base builder, then into `optional()` to set the optional ones, `done_optional()` back, and finally `build()`:

```cpp
TaskBuilder base;

Task t = base.main()
             .with_priority(Task::Priority::High)
             .with_ddl({2025, 9, 25, 10, 0, 0})
             .with_description("Composite builder")
             .done_main()
             .optional()
             .with_title("Project Report")
             .with_details("Check all data points")
             .done_optional()
             .build();
```

Let's verify this one too, both the fully constructed case and the missing-required case:

```sh
$ g++ -std=c++23 -O2 -Wall -Wextra composite_builder_verify.cpp -o composite_builder_verify
$ ./composite_builder_verify
Task{desc=Composite builder, title=Project Report, details=Check all data points}
caught: Task build error: missing required field
```

At this point we have a builder with clean responsibilities, extensible, and satisfying the open-closed principle: the base builder manages final assembly, the sub-builders manage their own field groups, and adding a new field group only requires hanging on a new sub-builder — not one line of old code has to move.

## So How Do You Choose Among These Builders

Let's lay the forms we walked through side by side, and you can see which one fits your scenario best:

| Style | Call shape | Where it shines | Where it hurts |
|---|---|---|---|
| Giant constructor | `Task(p, ddl, desc, t, d)` | Fastest to write; fine for small objects | Readability collapses once fields multiply; construction logic couples into the business class |
| Simple builder (non-fluent) | `b.set_xxx(...)` called line by line | The most straightforward implementation | Verbose calls; chaining is unavailable |
| Fluent builder | `b.with_x().with_y().build()` | Reads like a sentence; can pause midway and hand around a half-built builder | Required-field validation drags to run time; mutable state, so beware across threads |
| Staged builder | Each step returns a different type | Missing required fields and wrong order are both caught at compile time | Complicated type design; stages explode once required fields multiply |
| Composite builder | Base builder + multiple sub-builders | Clean responsibilities; new field groups require no changes to old code | Higher design cost; a slightly steeper API learning curve |

Just remember these conclusions: **many optional fields and lenient required-field validation — go fluent; required fields absolutely must not be missed and order matters — go staged; fields group by business domain and the team keeps adding new ones — go composite.** In most projects the fluent builder is the best value-for-effort default, with staged and composite as the upgrade paths reserved for harsher constraints.

## Summary

Let's straighten out the whole evolutionary path:

| Stage | Approach | Why it still was not enough |
|---|---|---|
| Giant constructor | One constructor stuffs in every field | Unreadable once fields multiply; construction logic couples into the business class; failure can only throw or carry an `is_valid` flag |
| getter/setter simplification | Required fields stay in the constructor, optional ones go through setters | `Task` carries both business duties and construction duties; the class gets dirtier and dirtier |
| Simple builder | Delegates to `TaskBuilder`, with `std::optional` acting as the flags | Line-by-line `b.set_xxx()` is too verbose, broken into ten lines |
| Fluent builder | `with_*` returns `*this`; chained calls | Required-field validation can only drag to run time; the builder has mutable state |
| Staged builder | Each step returns a different type; the type flow nails the order down | Type design grows complicated; stages explode once required fields multiply |
| Composite builder | Base builder + sub-builders sharing state by reference | Higher design cost, but satisfies the open-closed principle with the best extensibility |

Note down these key conclusions:

- **The essence of the Builder pattern is splitting "collect materials / validate / construct" out of a welded-shut constructor** and handing them to a dedicated middleman class, so that `Task` minds only its own business semantics.
- **`std::optional` is a sharp weapon for replacing the `is_valid` flag** — "has the field been filled" is internalized straight into type semantics, keeping the class's state squeaky clean.
- **`build()`'s `return t;` is zero-copy** — since C++17, *mandatory copy elision* guarantees that a same-named local object is constructed directly in the caller's stack frame; return big objects with confidence.
- **The fluent builder's required-field validation is a run-time affair** — the type system cannot distinguish builders by "how many fields have been filled". To press it into compile time, bring in the staged builder and let every required step return a different type.
- **A builder is a stateful intermediate object** — reusing one across threads is a data race. Either discard it after use, pass a copy by value, or add a lock.

::: tip Companion compilable project
This section's examples ship as a complete compilable project under `code/volumn_codes/vol4/design-patterns/Builder/` in the repository (`.h` + main + `CMakeLists.txt`); `cmake -S . -B build && cmake --build build` reproduces the outputs above.
:::

## References

- [cppreference:`std::optional`](https://en.cppreference.com/w/cpp/utility/optional) (since C++17, the semantic type for "may not have a value")
- [cppreference:Return value optimization / Copy elision](https://en.cppreference.com/w/cpp/language/copy_elision) (*mandatory copy elision* since C++17)
- Fedor G. Pikus, *Hands-On Design Patterns with C++*, Chapter 5 (builders and fluent interfaces)
- Companion piece in this volume: [Singleton Pattern: From Comment-Only Constraints to Meyer's Singleton](./01-singleton.md)
