---
title: 'Builder Pattern: From a Mess of Constructor Arguments to Fluent Builders'
description: Starting from the most primitive "all-in-one constructor," we progressively
  derive a fluent builder, conveniently use `std::optional` to eliminate the `isValid`
  flag, and finally use a phased builder to turn "missing required fields" from a
  runtime error into a compile-time error.
chapter: 11
order: 2
tags:
- host
- cpp-modern
- intermediate
- 构建器模式
difficulty: intermediate
platform: host
cpp_standard:
- 11
- 17
- 20
reading_time_minutes: 22
related:
- 单例模式:从注释约束到 Meyer's Singleton
prerequisites:
- 'Chapter 6: 类与对象'
translation:
  source: documents/vol4-advanced/vol4-generics-patterns/02-builder.md
  source_hash: 71bd26712cc72b900d6681eaf4699d1a87dd71e0ab8609508ae2456b48c7dded
  translated_at: '2026-06-24T00:53:38.818434+00:00'
  engine: anthropic
  token_count: 4940
---
# Builder Pattern: From a Mess of Constructor Arguments to Fluent Builders

## What problem are we actually solving?

Let's consider a very simple scenario. We write a `Task` class for a to-do item. It has required fields—priority, deadline, and description—and optional fields—title and notes. In the first version, to keep things simple, we just give `Task` a constructor that lists all fields. The call site looks like this:

```cpp
Task* a_task = new Task(
    Task::Priority::High,
    CTime{2025, 9, 24, 20, 38, 11},
    "This is a Demo Task",
    "Demo Tasks are placed for a detailed test",
    "A Task");
```

You stare at that line of code for three seconds. The problem is already bubbling up: the caller has to memorize the parameter order, relying on counting commas to know if the title goes in the third or fifth slot; if you want to add a new field later, like `links`, changing the constructor signature forces you to update thousands of calls across the entire repository; even worse, once the constructor gets complex, it can throw exceptions in a flow you can't control—if construction fails, the object doesn't exist, yet you're left holding a half-initialized state that you can't catch or fix. Your colleagues are already subjecting you to a harsh `git blame` trial, leaving you spinning in panic.

The real problem is that we are **doing three things at once in one line of code**: first, submitting the raw materials (that string of parameters); second, executing the construction process itself (validation, assignment, maybe even connecting to a database); and third, making `a_task` actually point to a legally existing `Task` object. Submitting materials, executing construction, and delivering the object—these three steps are welded tightly into one constructor, leaving us no room to intervene at any stage.

> Note: I have some experience with Java, where I noticed that the Builder pattern is often abused. Therefore, I believe—only when the scenario becomes as complex as described above should you reach for a Builder. Otherwise, just construct objects however you normally would.

The Builder pattern exists to solve exactly this: **separating "collecting materials", "validation", and "actual construction" into three distinct steps, moving them to a dedicated intermediary—the Builder—so that client programmers can assemble objects step-by-step in an elegant, pluggable way.** Let's walk through it step-by-step, starting with the dumbest approach, to see why each step still falls short.

## Step 1: The Most Primitive Approach—A Giant Constructor (Anti-Pattern)

Many people's first reaction is the blob above. To be honest, this is perfectly fine for small objects; no one feels uncomfortable with `Point(int x, int y)`. But once the fields exceed four or five, especially with a mix of required and optional ones, this constructor starts to become hostile.

There are two layers of issues here. The first is **readability**: five `std::string`s crammed together make it impossible to distinguish which is the title, the description, or the remark; IDE parameter hints might save you on your dev machine, but they won't save your eyes during code review. The second layer is more insidious: **coupling**. The constructor bears the full burden of "receiving parameters + validating legality + potentially performing side effects (logging, database connection)". If these things fail, you can't even get a "half-constructed" object—the constructor either succeeds or throws an exception and leaves; there is no buffer zone.

You might think: "I won't throw exceptions; I'll just add a `bool is_valid{false}` member and check it manually after construction." This path is walkable, but the cost is that every `Task` object must now carry an `is_valid` flag forever. Business code will be littered with `if (task.is_valid)` checks, and the class's state is polluted by this "validity" flag. **We use objects specifically to encapsulate state, and now we've encapsulated a flag that says "I might be a broken object".**

So, this path is a dead end too. We need to make construction a process that can be done in steps, checked midway, and have the validation logic moved out of the `Task` body itself.

## Step 2: Simplification via Getters/Setters—Moving Optional Items Out of the Constructor

Experienced developers might already be muttering: optional fields shouldn't be shoved into the constructor in the first place—just give them a getter/setter pair, right? Absolutely correct. Let's split the fields into two categories: required items that must be valid for the `Task` to exist, and optional items that can be configured later. We keep the required items in the constructor and configure the optional ones via setters afterwards:

```cpp
class Task {
public:
    enum class Priority { Immediate, High, Medium, Low };
    struct CTime { int year, month, day, hour, minute, second; };

    // 必填:优先级、截止时间、描述
    Task(Priority p, CTime ddl, const std::string& desc)
        : priority_(p), ddl_(ddl), description_(desc) {
        if (desc.empty()) {
            throw std::invalid_argument("Invalid Task Description");
        }
        // 可能还要写日志、连数据库……
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

This step is already a huge improvement over that mess of constructors—the constructors have slimmed down, and optional fields can be filled as needed. But if you look at the `Task` class now, you'll notice it's shouldering two responsibilities: **it is both "a business object representing a to-do item" and "a tool for constructing itself."** Validation logic, setters, and the side effect of logging are all crammed into `Task`. Construction logic and business logic are tangled together, making the class increasingly dirty.

What's worse is that validation failures can still only throw exceptions. Once `Task` becomes complex, the validation, assignment, and side effects in the constructor will pile up. If you want to change the failure handling strategy (for example, switching from throwing exceptions to returning error codes), you have to modify the `Task` class itself. But `Task` is a business object referenced throughout the entire repository; changing a single line requires pulling in a whole team for review (and enduring a barrage of criticism).

And we're not done yet. The real question is: **can we extract the "how to construct" aspect entirely from `Task` and delegate it to a dedicated utility class?** This way, `Task` focuses solely on its business semantics, while the utility handles construction details, validation strategies, and failure fallbacks, keeping them separate.

## Step 3: Delegate Construction — The Simple Builder

This "utility class dedicated to construction" is the **Builder**. We make `Task` befriend `Builder`, keeping only a private "slot for stuffing fields in." All the work of gathering materials, validating, and assembling is handed over to `TaskBuilder`.

Here is a particularly handy design: instead of using `bool` flags to track whether a field "has been filled," we use `std::optional` directly. `std::optional<Task::Priority>` serves as both "a container for a `Priority` value" and "a switch indicating whether the value was filled." You can check if it was filled just like checking a pointer with `if (priority_)`, and get the value with `*priority_`. This saves a pile of `is_xxx_set` flags, keeping the class state clean and tidy.

```cpp
class TaskBuilder {
public:
    void set_priority(Task::Priority p) { priority_ = p; }
    void set_ddl(Task::CTime d)          { ddl_ = d; }
    void set_description(std::string d)  { description_ = std::move(d); }
    void set_title(std::string t)        { title_ = std::move(t); }
    void set_details(std::string d)      { details_ = std::move(d); }

    std::optional<Task> build() const {
        // 必填项没填齐,就返回 nullopt,把失败内化进返回类型
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

Look, the validation logic now lives in `TaskBuilder`, completely decoupled from the core business logic of `Task`. The `build()` method returns a `std::optional<Task>`, which means the possibility of construction failure is encoded directly into the return type. The caller receives the result and is forced to handle the potential `nullopt` case, making it impossible to forget the failure path. Compared to throwing exceptions, this approach is more robust: construction failure is treated as an "expected outcome," rather than a sudden control flow jump.

::: tip std::optional is an extremely useful utility class
We can check if the member has been populated just like checking a pointer—using `if (priority_)` for existence checks and `*priority_` to access the value. We no longer need to maintain a bunch of boolean flags like `is_xxx_valid`; the "has value" state is now internalized directly into the type semantics of `std::optional`.
:::

Usage looks like this: one setter per line, followed by `build()`:

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

Great, it works. But as you write more code, you'll start to feel the fatigue—setting every single field requires repeating `builder.` over and over again. Typing it five or ten times is enough to make your eyes blur and your hands ache. Those who have used Kotlin's `apply` or written jQuery code will feel this even more acutely: **this kind of "chained" API could clearly be written in a single line, so why break it up into ten?**

## Step 4: Making the builder flow — fluent builder

The trick is so simple it's practically free: have each `with_*` method **return a reference to the builder itself** (`return *this;`) after setting the field. This way, the return value of the previous call is the builder itself, allowing you to immediately chain the next call onto it, making the call chain flow.

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

Note that here we have switched the failure strategy of `build()` from "returning `std::optional`" to "throwing an exception". Both are valid engineering choices; the difference lies in how you view "construction failure": if you consider it an expected, low-probability event that the caller should handle, `std::optional` is more appropriate, as the failure is encoded into the type. If you feel that "calling `build` without filling in required fields" is a programmer error—a logical error that shouldn't happen—throwing an exception is more direct, as it bubbles the error up to a top-level handler. We use exceptions here because they make the subsequent demonstration clearer.

The call site now reads just like a single sentence:

```cpp
Task task = TaskBuilder{}
                .with_priority(Task::Priority::High)
                .with_ddl({2025, 9, 25, 10, 0, 0})
                .with_description("Finish Builder blog")
                .with_title("Blog Writing")
                .with_details("Explain fluent builder")
                .build();
```

Let's first verify that this chain of calls actually works, and that it throws an exception if a required field is missing:

```sh
$ g++ -std=c++23 -O2 -Wall -Wextra builder_verify.cpp -o builder_verify
$ ./builder_verify
Task{desc=Finish Builder blog, prio=1, ddl=2025-9-25, title=Fluent Builder, details=return *this chains the calls}
caught: Cannot build Task: missing required field
```

A fully constructed object has all fields present; the attempt missing `priority` and `ddl` was blocked by `build()`, which threw an exception. Chained calls, `std::optional` flags, and runtime validation—all three pieces are working together.

Now, the next question arises. Chained calls have a side effect: they turn the builder itself into an **intermediate state that can be passed around**. This is actually where it shines: deferred construction. Since every `with_*` returns the builder itself, we can "pause" the construction process at any step, pass the builder as an argument to another subsystem, let that subsystem query the database to get the actual title, and then continue filling it in:

```cpp
auto partial = TaskBuilder{}
                   .with_priority(Task::Priority::High)
                   .with_ddl({2025, 9, 25, 10, 0, 0})
                   .with_description("Complete the final project report.");

// 把半成品构建器传出去,等异步查到真正的标题再接着 build
std::string title = data_base.query_title_by_time({2025, 9, 25, 10, 0, 0});
Task task = partial.with_title(title)
                  .with_details("Check all data points.")
                  .build();
```

You will discover a particularly valuable benefit here: **from now on, `Task` objects circulating in the code are always "fully constructed with valid fields".** We no longer have to deal with the awkwardness of "half-initialized `Task` objects running wild". The intermediate state is locked inside `TaskBuilder`; only when `build()` releases it do we get a complete `Task`. The type system separates our "finished products" from our "work-in-progress" items.

::: warning Do not reuse the same builder across multiple threads
The fluent builder has mutable state. If two threads call `with_*` and then `build()` on the same `TaskBuilder` instance simultaneously, the field read/write operations lack any synchronization, resulting in pure data races. Either use a separate builder instance for each thread, or treat the builder as a "use-once-and-throw-away" temporary object—the `TaskBuilder{}...build()` pattern shown above is the safest approach, as the builder is destroyed immediately after use. If you need to pass a work-in-progress object across threads, either pass by value (copying it) or properly synchronize access with a lock.
:::

## Let's verify this first: Did RVO really eliminate that copy?

There is a local object `t` inside `build()`, followed by `return t;`. Intuitively, moving a large object out of a function should require at least one move operation, right? Let's not take anything for granted; let's test this by attaching a move/copy counter to the object:

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
        Tracked t(*value_);   // 局部对象
        return t;             // 预期被 NRVO / RVO 消除
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

To rule out the suspicion that the optimizer is performing magic at `-O2`, we run once each at `-O2` and `-O0`:

```sh
$ g++ -std=c++23 -O2 rvo_verify.cpp -o rvo_verify && ./rvo_verify
value=42 moves=0 copies=0
$ g++ -std=c++23 -O0 rvo_verify.cpp -o rvo_verify_O0 && ./rvo_verify_O0
value=42 moves=0 copies=0
```

With optimizations disabled, the move and copy counts remain at zero. This isn't a compiler optimization; it is a **guarantee of the standard**. Since C++17, when returning a local object with the same name, copy/move operations are **mandatory elided**. The object is constructed directly on the caller's stack frame, completely skipping the step of "creating a temporary object and moving it." Therefore, we can safely write `return t;` inside `build()`. No matter how heavy `Task` is, we never pay the cost of a copy.

> Please note that this feature is available starting from C++17. Don't rush to apply it to C++11/14; it is likely to be effective only when optimizations are enabled.

## The Real Pitfall: Catching Missing Fields at Runtime

The fluent builder is nice, but it has an unavoidable flaw—**validation of required fields is delayed until runtime**. If you write `TaskBuilder{}.with_ddl(...).build()` but forget `priority` and `description`, the compiler won't complain. It compiles cleanly, and you only realize the mistake when the program runs and `build()` throws an exception.

Where is the problem? It lies with the `TaskBuilder` type itself. It treats "a builder with priority set," "a builder with priority and deadline set," and "a fully configured builder" as **the same type**: `TaskBuilder`. The type system cannot distinguish between them, so it cannot enforce checks at compile time. To the compiler, it just sees a `TaskBuilder` with fields potentially unset. Whether you call `with_priority` is your business; it cannot interfere.

Is there a way to let the type system participate? Yes. The idea is: **every time a required field is filled, the builder "transforms" into a new type.** Only after completing all required stages do you get the type that "can `build()`." If you miss a step, the type in hand simply doesn't have a `build()` method, and the compiler stops you immediately. This is the **Staged Builder (or Typed Builder)**.

## Step 5: Push Required Field Validation to Compile Time — Staged Builder

First, we define an internal draft, `TaskDraft`, that holds all fields and is moved between stages. Then, we design a separate type for each "fill field" step—`SetPriority`, `SetDdl`, `SetDescription`, `OptionalStage`. The `with_*` method of each type returns the **type of the next stage**:

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
    SetDdl with_priority(Task::Priority p);          // 返回下一阶段
};
struct SetDdl {
    TaskDraft d;
    SetDescription with_ddl(Task::CTime ddl);        // 返回下一阶段
};
struct SetDescription {
    TaskDraft d;
    OptionalStage with_description(std::string desc);  // 进入可选阶段
};
struct OptionalStage {
    TaskDraft d;
    OptionalStage& with_title(std::string t)   { d.title = std::move(t);   return *this; }
    OptionalStage& with_details(std::string det) { d.details = std::move(det); return *this; }
    Task build() {
        // 三个必填字段已被类型系统强制填过,这里无需运行时校验
        Task t(*d.priority, *d.ddl, std::move(*d.description));
        if (d.title)   t.set_title(*d.title);
        if (d.details) t.set_details(*d.details);
        return t;
    }
};
```

Notice a key difference: the `if (!priority || ...)` validation block is **completely gone** from `OptionalStage::build()`. Why is it no longer needed? Because the type system guarantees it for us: the only way to obtain an `OptionalStage` object is to successfully complete the chain `with_priority` → `with_ddl` → `with_description`—and each step fills in the corresponding `optional` field. By the time we reach `build()`, all three required fields are guaranteed to be non-null, so dereferencing via `*d.priority` is absolutely safe. This is the essence of "compressing runtime checks into compile-time guarantees."

Using it requires a strict chain like this:

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

Let's first verify that the correct usage works:

```sh
$ g++ -std=c++23 -O2 -Wall -Wextra staged_builder_verify.cpp -o staged_builder_verify
$ ./staged_builder_verify
Task{desc=Staged builder, title=Typed}
```

Now, let's witness the power of the compiler. We will intentionally make two common mistakes to see how the compiler stops us.

The first one is **building without filling in required fields**. Suppose we only fill in `priority` and `ddl`, skipping `with_description`, and try to call `.build()` directly:

```cpp
Task t = TaskBuilder::create()
             .with_priority(Task::Priority::High)
             .with_ddl({2025, 9, 25, 10, 0, 0})
             .build();   // ← 试图在 SetDescription 上调 build()
```

The compiler's response:

```sh
$ g++ -std=c++23 staged_missing.cpp -o staged_missing
staged_missing.cpp:7:19: error: 'struct SetDescription' has no member named 'build'
```

The `SetDescription` type simply doesn't have a `build()` method—`build()` only exists on `OptionalStage`. Since you can't get an `OptionalStage` (because `with_description` wasn't called), you naturally can't build. If you miss a required field, the compiler shows you the red card during compilation.

The second case is **incorrect ordering**. Someone might get ahead of themselves and write `with_ddl` before `with_priority`:

```cpp
auto x = TaskBuilder::create()
             .with_ddl({2025, 9, 25, 10, 0, 0});   // ← 在 SetPriority 上调 with_ddl()
```

The compiler's response:

```sh
$ g++ -std=c++23 staged_wrongorder.cpp -o staged_wrongorder
staged_wrongorder.cpp:4:36: error: 'struct SetPriority' has no member named 'with_ddl'
```

`SetPriority` does not have a `with_ddl` method—`with_ddl` belongs to the `SetDdl` stage. You must first call `with_priority` to transform into `SetDdl` before you are eligible to call `with_ddl`. The call sequence is strictly enforced by the type flow.

This is the definitive proof that the phased builder enforces both constraints at compile time: **missing required fields or incorrect call orders will simply fail to compile.** The need for runtime exceptions is completely eliminated.

::: tip The Cost of Phased Builders
There is no such thing as a free lunch. The cost of this mechanism is **increased type complexity**—each required phase requires a separate struct definition, and fields are moved between stages. As the number of fields grows, so does the number of stages. Therefore, this approach is best suited for scenarios where "there are few required fields, but they absolutely cannot be missed" (such as protocol headers or security-related configurations). If your object has a large number of optional fields and only two or three required ones, the standard fluent builder with runtime validation is usually sufficient; there is no need to burden the codebase with type膨胀 for the sake of compile-time checks.
:::

## Step 6: Separation of Concerns — Composite Builder

Looking back, the fluent builder piles all `with_*` methods into a single `TaskBuilder` class. As fields multiply, this class inflates into an all-encompassing "super constructor," mixing required fields, optional fields, and even "business-domain-grouped" fields (e.g., "security-related fields," "logging-related fields") into one big lump. If you later want to add a new group of fields to a specific domain, you have to modify the `TaskBuilder` itself—violating the Open/Closed Principle (OCP) that we worked so hard to achieve.

The Composite Builder approach separates these concerns: **a base Builder holds all fields and handles the final `build()`; around it, we derive several sub-builders, each responsible for only one category of fields.** Sub-builders do not hold copies of the fields; instead, they hold a reference to the base Builder. After setting fields, they call a `done_xxx()` method to switch back to the base Builder, which can then jump to the next sub-builder. Need to add a new group of fields? Just write a new sub-builder and attach it. The base Builder and other sub-builders don't need to change a single line.

```cpp
class TaskBuilder;        // 基础 Builder:持有所有字段 + build()
class BuilderMain;        // 子构造器 A:负责必填字段
class BuilderOptional;    // 子构造器 B:负责可选字段

class TaskBuilder {
public:
    std::optional<Task::Priority> priority;
    std::optional<Task::CTime>    ddl;
    std::optional<std::string>    description;
    std::optional<std::string>    title;
    std::optional<std::string>    details;

    BuilderMain     main();       // 进入「必填字段」子构造器
    BuilderOptional optional();   // 进入「可选字段」子构造器

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
    TaskBuilder& done_main() { return b_; }       // 设完必填,切回基础 Builder
private:
    TaskBuilder& b_;
};

class BuilderOptional {
public:
    explicit BuilderOptional(TaskBuilder& b) : b_(b) {}
    BuilderOptional& with_title(std::string t)   { b_.title = std::move(t);   return *this; }
    BuilderOptional& with_details(std::string d) { b_.details = std::move(d); return *this; }
    TaskBuilder& done_optional() { return b_; }   // 设完可选,切回基础 Builder
private:
    TaskBuilder& b_;
};

BuilderMain     TaskBuilder::main()     { return BuilderMain(*this); }
BuilderOptional TaskBuilder::optional() { return BuilderOptional(*this); }
```

There are several details in this code worth examining. The fields in the base `Builder` are all `public`, not to cut corners, but to allow sub-constructors to read and write them directly, avoiding layers of getters and setters. The sub-constructors hold a `TaskBuilder&` reference rather than a copy, so setting fields in `BuilderMain` or `BuilderOptional` actually modifies the same base builder. Finally, `build()` reads this single source of truth. `done_main()` and `done_optional()` return references to the base builder, which allows chaining the transition from "sub-constructor → base builder → another sub-constructor" into a single fluent chain.

The call site therefore looks like a sentence broken into clauses—enter `main()` to set required fields, `done_main()` returns to the base builder, enter `optional()` to set optional fields, `done_optional()` returns again, and finally `build()`:

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

Let's also verify the two scenarios: complete construction and missing required fields.

```sh
$ g++ -std=c++23 -O2 -Wall -Wextra composite_builder_verify.cpp -o composite_builder_verify
$ ./composite_builder_verify
Task{desc=Composite builder, title=Project Report, details=Check all data points}
caught: Task build error: missing required field
```

At this point, we have a builder with clear responsibilities, extensibility, and adherence to the Open/Closed Principle: the base `Builder` handles the final assembly, while sub-builders manage their respective field groups. To add a new field group, we simply attach a new sub-builder without touching a single line of existing code.

## How to Choose Between These Builders

Let's compare the different forms we've explored side-by-side to see which one best fits your scenario:

| Style | Invocation Pattern | Strengths | Weaknesses |
|---|---|---|---|
| Giant Constructor | `Task(p, ddl, desc, t, d)` | Fastest to write; sufficient for small objects | Readability collapses as fields grow; construction logic couples into the business class |
| Simple Builder (Non-fluent) | `b.set_xxx(...)` line by line | Most straightforward implementation | Verbose to call; cannot use chaining |
| Fluent Builder | `b.with_x().with_y().build()` | Reads like a sentence; can pause and pass half-built objects | Mandatory validation pushed to runtime; has mutable state, so be careful with threads |
| Staged Builder | Each step returns a different type | Catches missing mandatory fields and wrong ordering at compile time | Complex type design; stages explode with many mandatory fields |
| Composite Builder | Base Builder + multiple sub-builders | Clear separation of concerns; adding new field groups doesn't change old code | High design cost; slightly steeper API learning curve |

Just keep these conclusions in mind: **If you have many optional fields and loose validation, choose Fluent. If mandatory fields absolutely cannot be skipped and order matters, choose Staged. If fields can be grouped by business domain and the team will keep adding new fields, choose Composite.** For most projects, the Fluent Builder offers the best cost-performance ratio as the default choice, while Staged and Composite Builders serve as upgrade paths for stricter constraints.

## Summary

Let's walk through the entire evolutionary path:

| Stage | Approach | Why it wasn't enough |
|---|---|---|
| Giant Constructor | Stuff all fields into one constructor | Unreadable with many fields; construction logic couples into the business class; failure can only be signaled via exceptions or an `is_valid` flag |
| Getter/Setter Simplification | Keep mandatory fields in constructor, use setters for optional ones | `Task` bears both business and construction responsibilities, making the class increasingly messy |
| Simple Builder | Delegate to `TaskBuilder`, use `std::optional` as flags | Calling `b.set_xxx()` line by line is too verbose, breaking into ten lines |
| Fluent Builder | `with_*` returns `*this` for chaining | Mandatory validation is pushed to runtime; builder has mutable state |
| Staged Builder | Each step returns a different type, pinning down order via type flow | Type design becomes complex; stages explode with many mandatory fields |
| Composite Builder | Base Builder + sub-builders sharing state via references | High design cost, but satisfies the Open/Closed Principle with the best extensibility |

Keep these key takeaways in mind:

- **The essence of the Builder pattern is extracting the three steps—collecting materials, validation, and construction—from a rigid constructor**, delegating them to a dedicated intermediate class so that `Task` only handles its business semantics.
- **`std::optional` is a powerful tool for replacing `is_valid` flags**—"Is the field filled?" is internalized directly into the type semantics, keeping the class state clean.
- **`build()`'s `return t;` is zero-copy**. Since C++17, *mandatory copy elision* guarantees that a named local object is constructed directly on the caller's stack frame, so you can safely return large objects.
- **Fluent Builder validation is runtime**—the type system cannot distinguish between builders based on "how many fields are filled." To push this to compile time, use a Staged Builder where each mandatory step returns a different type.
- **A builder is a stateful intermediate object**; reusing it across threads leads to data races. Either use it once and discard it, pass by value (copy), or add a lock.

::: tip Companion Compilable Project
The examples for this section are available as a complete compilable project in the repository at `code/volumn_codes/vol4/design-patterns/Builder/` (`.h` + main + `CMakeLists.txt`). Run `cmake -S . -B build && cmake --build build` to reproduce the outputs shown above.
:::

## References

- [cppreference: `std::optional`](https://en.cppreference.com/w/cpp/utility/optional) (Since C++17, a semantic type for "possibly having no value")
- [cppreference: Return value optimization / Copy elision](https://en.cppreference.com/w/cpp/language/copy_elision) (Since C++17, *mandatory copy elision*)
- Fedor G. Pikus, *Hands-On Design Patterns with C++*, Chapter 5 (Builders and Fluent Interfaces)
- Sister article in this volume: [Singleton Pattern: From Comment Constraints to Meyer's Singleton](./01-singleton.md)
