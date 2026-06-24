---
title: 'Command Pattern: Turning Actions into Reversible Objects'
description: Starting from the most intuitive "direct function calls," we will gradually
  derive the Command interface, build a text editor with undo/redo functionality along
  the way, and finally wrap things up with `std::move_only_function`.
chapter: 11
order: 13
tags:
- host
- cpp-modern
- intermediate
- 命令模式
difficulty: intermediate
platform: host
cpp_standard:
- 11
- 17
- 20
- 23
reading_time_minutes: 20
related:
- 单例模式:从注释约束到 Meyer's Singleton
prerequisites:
- 'Chapter 6: 类与对象'
translation:
  source: documents/vol4-advanced/vol4-generics-patterns/13-command.md
  source_hash: a188fc4959e344434c1f602a173343a066b544f1bc6428793b8a526867cb9536
  translated_at: '2026-06-24T00:59:38.597937+00:00'
  engine: anthropic
  token_count: 3155
---
# Command Pattern: Turning Actions into Undoable Objects

## What problem are we actually solving?

Let's skip the formal definition for a moment. Consider a common scenario: you are writing a text editor. The user clicks "Insert Line," so you write `editor.append("hello")` and call it a day—the action happens instantly and then vanishes. This sounds fine until the product manager comes back the next day asking for Ctrl+Z undo support, an operation history, and the ability to batch operations into "macros" for one-click replay. You look at your code and see it's full of naked function calls with no trace of "what happened"—undo? Undo what? The functions have returned, and you have nothing to roll back.

The Command pattern solves exactly this class of requirements: **transforming a "to-do" from a fleeting function call into an object with identity, state, and the ability to be stored and moved**. Once an action becomes an object, you can push it into a queue for delayed execution, save it to a log for later replay, combine it into a macro, or—most commonly—remember it so you can reverse it when the user presses Ctrl+Z.

However, "encapsulating an action in an object" isn't as simple as "writing a wrapper class" in C++. There is a very common pitfall here: many people implementing the Command pattern for the first time instinctively declare `execute()` as `const`, thinking "executing a command doesn't change the command object itself." Then, when they get to the undo part, they are stuck—you have nowhere to store the pre-execution state because you promised not to modify members inside a `const` function. So, the real question we need to answer in this post is—**how do we cleanly encapsulate an action into an object so it can execute, be reversed, without polluting the receiver or relying on fragile runtime type identification**?

Let's walk through this step-by-step, starting with the most intuitive approach, seeing why it falls short, and finally deriving a modern C++ solution.

## Step 1: The Most Intuitive Approach—Direct Function Calls (The Anti-Pattern)

Many people, when first encountering "an editor needs to support insert and delete," subconsciously write code like this:

```cpp
class TextEditor {
public:
    void append_text(const std::string& line) { lines_.push_back(line); }
    void pop_text_once() {
        if (!lines_.empty()) lines_.pop_back();
    }
    void dump() const;
private:
    std::vector<std::string> lines_;
};
```

It is also very convenient to use; we can call it wherever needed:

```cpp
TextEditor editor;
editor.append_text("Hello, World");
editor.pop_text_once();
```

Honestly, this approach works perfectly fine in scenarios where "the action happens immediately and we never need to look back." Don't reflexively slap on a design pattern just because you see one. The problem arises the moment **the requirements require us to "look back"**—when the product manager asks for "undo," you realize that `append_text` finishes execution and forgets everything. It doesn't remember what was inserted or how long the text was. Undo requires "memory," but raw function calls lack memory by nature.

So, why not just add "memory"? We can package the "action to be done" together with the "action required to undo it" into a single object—this is the prototype of the Command pattern.

## Step 2: Encapsulate Actions into Objects — Abstract Command

First, we define a unified interface that all "actions to be done" must satisfy. The most critical step is to declare both `execute()` and `undo()` simultaneously. This makes "undoability" a built-in capability of the command from the start, rather than a patch applied later.

```cpp
struct Command {
    virtual ~Command() = default;
    virtual void execute() = 0;
    virtual void undo() = 0;   // 从第一天起就要想好怎么撤销
};
```

Here is a detail worth pausing to consider: why isn't `execute()` `const`? Because most reversible commands store the "information needed for undo" (such as the length inserted or the old value being replaced) in their own members at the moment of execution, to be used later by `undo()`. If you declare it `const`, you are essentially blocking your own path to storing state—when you actually need to write `undo()`, you will be tied down by `const` and forced to resort to hacks like `mutable`. **So the rule here is: do not make `execute()` `const`. The command object itself carries state; it is not a pure function.**

Next, let's implement a concrete command for "inserting a block of text". It needs to hold a reference to the receiver (`TextEditor`) and its own parameters (the text to insert), while `undo()` simply removes the inserted content by length:

```cpp
class AppendCommand : public Command {
public:
    AppendCommand(TextEditor& editor, std::string text)
        : editor_(editor), text_(std::move(text)) {}

    void execute() override { editor_.append_text(text_); }
    void undo() override    { editor_.erase_tail(text_.size()); }

private:
    TextEditor& editor_;   // 接收者:真正干活的家伙
    std::string text_;     // 参数:这个命令要插的文本
};
```

You will notice that a command object is simply a package of three things: a reference to a **receiver**, the **parameters** required for execution, and a pair of `execute()`/`undo()` methods. The interface exposed by the receiver (`TextEditor`) (e.g., `append_text` / `erase_tail`) remains stable. We can wrap a layer of "undo" capability around it without touching a single line of `TextEditor` code. This is the core benefit of the Command pattern: **the "undoability" of an action no longer pollutes the receiver; the receiver only cares about "what can be done," while "whether it can be undone" is the responsibility of the command layer**.

## Let's verify this: Can the undo queue really retrace its steps?

Talk is cheap. Let's write a minimal undo stack, execute a few commands, and then `undo` them one by one to see if the buffer can truly return to its initial state. First, let's equip `TextEditor` with an interface that can trim the tail by length (to support undo):

```cpp
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

class TextBuffer {
public:
    void append(const std::string& s) { buf_ += s; }
    void erase_tail(std::size_t n) {
        if (n > buf_.size()) throw std::out_of_range("erase_tail");
        buf_.erase(buf_.size() - n, n);
    }
    const std::string& str() const { return buf_; }
private:
    std::string buf_;
};
```

The logic for the undo stack is deceptively simple: during `execute`, we perform the action first and then push it onto the stack; during `undo`, we pop the top of the stack and call its `undo()`. The LIFO nature fits perfectly with "undoing the most recent step":

```cpp
class UndoStack {
public:
    void execute(std::unique_ptr<Command> c) {
        c->execute();
        history_.push_back(std::move(c));   // 执行成功才入栈
    }
    void undo() {
        if (history_.empty()) return;
        history_.back()->undo();
        history_.pop_back();
    }
private:
    std::vector<std::unique_ptr<Command>> history_;
};
```

Let's run it. First, insert two segments, then undo them one by one, and finally test the macro commands while we're at it:

```sh
$ g++ -std=c++23 -O2 command_verify.cpp -o command_verify
$ ./command_verify
after 2 appends : 'Hello, World'
after 1 undo    : 'Hello, '
after 2 undo    : ''
after macro ABC : 'ABC'
after macro undo: ''
```

After two `append` operations, the buffer is `Hello, World`. Undoing once reverts it to the first half, `Hello,` (the trailing comma and space remain), and undoing again brings us back to an empty string—the state has completely retraced its path. This is the undoability promised by the Command pattern, delivered in full.

## Step 3: Packaging multiple actions — Macro commands

How did we get that `macro ABC` earlier? The answer is to combine commands. The Command pattern is naturally suited for the Composite pattern: since "a single action" is a command, why can't "a sequence of actions" be a command as well? We write a `MacroCommand` that holds a group of sub-commands internally, while itself being a `Command`:

```cpp
class MacroCommand : public Command {
public:
    void add(std::unique_ptr<Command> c) { subs_.push_back(std::move(c)); }

    void execute() override {
        for (auto& c : subs_) c->execute();        // 正序执行
    }
    void undo() override {
        for (auto it = subs_.rbegin(); it != subs_.rend(); ++it)
            (*it)->undo();                          // 逆序撤销
    }
private:
    std::vector<std::unique_ptr<Command>> subs_;
};
```

Here lies a real pitfall that trips up many newcomers. `execute()` iterates in forward order, so why on earth must `undo()` iterate in **reverse**? Just think about the stack's Last-In-First-Out (LIFO) nature: the last sub-command executed modified the newest state. To revert to the state before execution, we must undo it first before we can get to the previous sub-command. Taking `ABC` as an example, the execution order is `A→B→C`, so the undo order must be `C→B→A`. If you mistakenly write a forward `undo`, the buffer won't have enough content to trim, `erase_tail` will go out of bounds, or the state will be mismatched. You'll end up with a program that seems to run but is actually in a corrupted state.

::: warning Real Pitfall: `undo` Must Be in Reverse
The `undo()` method of a macro command **must** iterate through sub-commands in **reverse order**, strictly corresponding to the forward order of `execute()`. The intuition that "since it's ABC forward, it should be ABC backward" is wrong—forward execution accumulates state sequentially, while the reverse must peel back layer by layer in stack order. Writing a forward `undo` is the most common subtle bug in this type of code. It often doesn't crash immediately but reveals state errors only under specific operation sequences.
:::

A macro command packages a group of actions into a single atomic unit. To an undo stack, a macro is just "one step"—pushed once, undone once, with all three internal sub-commands reverted together. This is the most primitive implementation of "transactional operations."

## Pitfall Alert: Don't Use `dynamic_cast` for Parameters

At this point, I need to mention a common implementation style because it appears in the companion project and has tripped up many people. Some implementations design it the other way around: they let the abstract command carry a "type" tag. Upon execution, the receiver determines "Is this an Append command?" based on the tag, and then uses `dynamic_cast` to down-cast the command pointer to the derived class to extract the parameters:

```cpp
struct TextEditorCommand {
    enum class Type { APPEND, REMOVE };
    // ...
private:
    const Type type;
};

struct TextEditor {
    void process(Invoker* invoker) {
        for (auto& command : invoker->commands) {
            if (command->get_type() == Type::REMOVE) {
                pop_text_once();
            } else {
                // 用 dynamic_cast 取回 AppendCommand 里的 text
                AddCommand* adder = dynamic_cast<AddCommand*>(command.get());
                if (adder) append_text(adder->append);
            }
        }
    }
};
```

This approach works, and the accompanying project uses it too, but it **smells heavily of an anti-pattern**. The problem is that the Command pattern carefully encapsulates the "execution logic" within each command's own `execute()` method, so the receiver doesn't need to know the specific command type. However, by using `dynamic_cast`, you leak the execution logic back to the receiver. The receiver once again bears the burden of "knowing all command types," meaning you must modify this switch statement every time a new command is added—precisely the kind of coupling the Command pattern is meant to avoid.

A more practical concern is that `dynamic_cast` relies on RTTI (Run-Time Type Information). In embedded systems and game engines, RTTI is often disabled with `-fno-rtti` to save `.rodata` space and reduce binary size. Let's verify what happens when we turn off RTTI:

```sh
$ g++ -std=c++23 -O2 -fno-rtti command_cast.cpp
command_cast.cpp:31:20: error: 'dynamic_cast' not permitted with '-fno-rtti'
   31 |         auto* ap = dynamic_cast<AppendCmd*>(c.get());
command_cast.cpp:33:30: error: cannot use 'typeid' with '-fno-rtti'
   33 |                      typeid(*c).name(),
```

Compiling directly fails. This means that once we use `dynamic_cast`, our code can no longer be used in projects with RTTI disabled, effectively cutting portability in half.

::: warning Don't make the receiver recognize commands
The correct approach is to let each command handle the work inside its own `execute()` method. The receiver only sees the abstract interface `Command&` and doesn't need to know if it is specifically an `AppendCommand` or an `EraseCommand`. As long as the receiver's underlying interface (`append_text` / `erase_tail`) remains stable, adding a new command simply means adding a derived class; the receiver doesn't need a single line of changes. The `dynamic_cast` plus type tag approach essentially degrades the Command pattern back into a switch-case. Don't write it this way.
:::

## Step 4: Functional Commands — Closures are Commands

A command object is, at its core, just an "execute" closure plus an "undo" closure. Since C++ has lambdas and `std::function`, can we skip handwriting a bunch of derived classes and directly construct a command using two closures? We sure can, and it's very satisfying to write.

Let's use C++23's `std::move_only_function` here. You might ask: why not use the older `std::function`? Because command objects often need to own resources exclusively (for example, capturing a `unique_ptr` or a file handle), whereas `std::function` requires the wrapped target to be copyable—it needs to copy the closure internally, so it simply cannot hold a move-only closure. `std::move_only_function` (available since C++23 in the `<functional>` header) was made for this: it only requires movability, which perfectly matches the exclusive ownership semantics of "a command object is executed once and undone once."

We modify the undo stack to accept two parameters: an "execute closure" and an "undo closure," storing this pair as an entry:

```cpp
#include <functional>
#include <utility>
#include <vector>

class FunctionalUndoStack {
public:
    void execute(std::move_only_function<void()> do_it,
                 std::move_only_function<void()> undo_it) {
        do_it();                                   // 先执行
        history_.push_back({std::move(do_it), std::move(undo_it)});
    }
    void undo() {
        if (history_.empty()) return;
        history_.back().undo_();
        history_.pop_back();
    }
private:
    struct Entry {
        std::move_only_function<void()> do_;
        std::move_only_function<void()> undo_;
    };
    std::vector<Entry> history_;
};
```

Here is how we use it: we simply pass the action and its inverse together. The lambda expression handles all the capturing and execution, so we don't need to write any derived classes:

```cpp
TextBuffer buf;
FunctionalUndoStack stack;
std::string chunk = "World";

stack.execute(
    [&buf, chunk] { buf.append(chunk); },          // 执行:插入
    [&buf, chunk] { buf.erase_tail(chunk.size()); } // 撤销:砍掉等长尾部
);
```

Let's also verify this to ensure that the closure approach works completely:

```sh
$ g++ -std=c++23 -O2 command_lambda.cpp -o command_lambda
$ ./command_lambda
after execute: 'World'
after undo   : ''
```

Clean and concise. But what is the cost of this functional approach? The trade-off is that **type boundaries are not visible at compile time**. In the OOP approach, `AppendCommand` is a named type; a simple grep search reveals which commands exist in the project and what their respective `undo()` methods look like. In the functional approach, commands are scattered across various lambdas, and the type system treats them uniformly, which reduces readability and discoverability. Therefore, the choice between the two paths is clear: if the command types are limited, type clarity is desired, or you want to uniformly add macros and logging, go with OOP derived classes; if the actions are one-off, closures can be written inline, and you don't want to define a class just for a single action, go with functional closures. The two styles do not conflict, and they can easily coexist within the same project.

## Practice: An "Optimizing" Command Queue

The undo queue discussed earlier is the most classic application of the Command pattern, but its capabilities go beyond that. The accompanying project features an interesting approach: instead of executing commands one by one as they arrive, it batches a group of commands within the `Invoker` and performs a **simplification** step before actual execution. If an "insert" is immediately followed by a "delete", the two cancel each other out and don't need to be executed at all. This is analogous to dead code elimination in a compiler, but happening at the command layer.

Let's first look at the simplification logic itself. It involves a single traversal that maintains a result stack: when an "insert" is encountered, it is pushed onto the stack; when a "delete" is encountered, the top "insert" is popped off the stack (cancellation); otherwise, it is copied as-is:

```cpp
void simplify() {
    std::vector<std::shared_ptr<TextEditorCommand>> result;
    for (const auto& cmd : commands) {
        if (!result.empty()
            && result.back()->get_type() == Type::APPEND
            && cmd->get_type() == Type::REMOVE) {
            result.pop_back();        // 插入 + 删除 = 啥也没干,抵消
        } else {
            result.push_back(cmd);
        }
    }
    commands = std::move(result);
}
```

Let's run the input from the companion project directly through the four commands: `ADD("Hello, World")`, `ADD("Hello, World")`, `ERASE`, `ADD("Hello, World")`. The simplifier watches the queue closely: the first two `ADD` commands each push their results onto the stack. When the third `ERASE` arrives, it sees that the top of the stack is exactly an `ADD`, so it cancels out the top entry (the second `ADD` is gone). The fourth `ADD` then pushes onto the stack. Ultimately, two `ADD` commands remain in the queue, and after execution, the buffer contains two lines of `Hello, World`:

```sh
$ g++ -std=c++23 -O2 TextEditorMain.cpp -o TextEditor
$ ./TextEditor
Hello, World
Hello, World
```

Two lines, which is exactly the simplified result. This is where the Command pattern outperforms raw function calls—because actions are objects, we can statically analyze, optimize, batch, or replay them **before** execution, without touching a single line of the receiver's code. The accompanying project also demonstrates another capability of the `Invoker`: `append_command` adds commands, while `remove_command` removes a specific command from the queue. Since commands are objects, they can be added, deleted, referenced, or cancelled—something that is impossible in the world of raw function calls.

::: tip Accompanying Compilable Project
The complete code for this section (including command queues, simplified cancellation, and a counter-example of using `dynamic_cast` to retrieve parameters) is in this repository. Clone it and run CMake to build it: [Command/TextEditor](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/tree/main/code/volumn_codes/vol4/design-patterns/Command/TextEditor).
:::

## When NOT to use the Command pattern

At this point, we have a command system that can undo, compose, and optimize. But we aren't done yet—I must be honest with you: **the Command pattern is not a silver bullet. In many scenarios, you simply don't need it, and forcing it will only make your code more convoluted.**

First, if your action **will never be undone, delayed, or queued**, don't use the Command pattern. If a simple `editor.append_text("x")` solves the problem, but you insist on wrapping it in an `AppendCommand`, pushing it into a queue, and finding an `Invoker` to trigger it, you gain nothing but an extra level of indirection and an extra heap allocation. Patterns exist to accommodate changing requirements; when requirements are stable, a direct function call is always the optimal solution.

Second, the implementation cost of "undo" is easily underestimated. The "undo by truncating length" approach we used earlier only works for the simplest append operations. Once your operations involve replacement, cursor movement, or interaction across multiple buffers, the "pre-execution state" that `undo()` needs to save will balloon rapidly. At that point, you often need to rely on the Memento pattern to snapshot the entire receiver state, causing memory overhead and complexity to skyrocket. The "undoability" promised by the Command pattern is not free; the cost of state preservation is its most tangible expense.

Third, **lifetime management** of command objects is a hidden pitfall. Commands hold references to the receiver, so the receiver must outlive the command. If the receiver is destroyed before the command, the reference inside `execute()` or `undo()` becomes a dangling reference—a classic use-after-free. In the accompanying project, you will see that it uses `std::shared_ptr<TextEditorCommand>` to manage the command itself, but the lifetime of the receiver `TextEditor` is manually guaranteed. If a queue accumulates a pile of commands but the receiver is destroyed prematurely, the entire queue becomes useless. This is particularly fatal in asynchronous or multi-threaded scenarios.

## Summary

Let's review the entire evolution path:

| Stage | Approach | Why it falls short |
|---|---|---|
| Direct function call | `editor.append_text(...)` | No memory, cannot undo/queue/replay |
| Abstract Command | `execute()` + `undo()` derived classes | Receiver is clean, but adding an action requires writing a new class |
| Macro Command | Combine multiple commands, undo in reverse | Solved, but beware of the reverse-order undo pitfall |
| `dynamic_cast` for parameters | Receiver down-casts commands by type tag | **Anti-pattern**, leaks coupling, relies on RTTI, won't compile if RTTI is disabled |
| Functional closure | `std::move_only_function` wrapping two closures | Solved, but type boundaries weaken and discoverability decreases |

Keep these key conclusions in mind:

- The essence of the Command pattern is elevating an "action" from a fleeting function call to an **object with identity, state, and storability**, making undo, queuing, replay, and composition possible.
- Do not mark `execute()` as `const`—command objects need to store state for undoing, so they are not pure functions.
- A Macro Command's `undo()` must traverse child commands in reverse order; this is the most common subtle bug in this type of code.
- Don't use `dynamic_cast` + type tags to "identify" command types. This degrades the Command pattern back to a switch-case, and it won't compile without RTTI. The correct approach is to let the command do the work inside its own `execute()`.
- C++23's `std::move_only_function` makes "packing two closures into one command" natural, suitable for one-off, move-only actions. However, the cost is weaker type boundaries; whether to use it depends on your requirements for discoverability.
- Commands hold references to the receiver. Ensure the receiver outlives the command, otherwise undoing results in use-after-free.

## References

- [cppreference: `std::move_only_function`](https://en.cppreference.com/w/cpp/utility/functional/move_only_function) (C++23, move-only callable wrapper)
- [cppreference: `std::function`](https://en.cppreference.com/w/cpp/utility/functional/function) (For comparison, requires copyability)
- [cppreference: `dynamic_cast`](https://en.cppreference.com/w/cpp/language/dynamic_cast) (Run-time type identification, relies on RTTI)
- Gamma, Helm, Johnson, Vlissides, *Design Patterns*: Elements of Reusable Object-Oriented Software (Chapter on Command); Klaus Iglberger, *C++ Software Design* (Discussion of Command and type erasure)
- Accompanying compilable project: [Command/TextEditor](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/tree/main/code/volumn_codes/vol4/design-patterns/Command/TextEditor)
