---
title: 'Command Pattern: Turning Actions into Undoable Objects'
description: 'Starting from the most intuitive "just call the function directly" approach, we derive the Command interface step by step, build a text editor with undo/redo along the way, and wrap up with `std::move_only_function`'
chapter: 11
order: 13
tags:
  - host
  - cpp-modern
  - intermediate
  - 命令模式
difficulty: intermediate
platform: host
cpp_standard: [11, 17, 20, 23]
reading_time_minutes: 20
related:
  - 'Singleton Pattern: From Comment-Only Constraints to Meyer''s Singleton'
prerequisites:
  - Classes and Object-Oriented Programming
translation:
  source: documents/vol4-advanced/vol4-generics-patterns/13-command.md
  source_hash: a188fc4959e344434c1f602a173343a066b544f1bc6428793b8a526867cb9536
  translated_at: '2026-09-26T05:32:48+00:00'
  engine: anthropic
  token_count: 4900
---

# Command Pattern: Turning Actions into Undoable Objects

## What problem are we actually solving

Let's skip the definition for now. Think of the most common scenario: you're writing a text editor, the user clicks "Insert Line", and you toss in an `editor.append("hello")` and call it done — the action happens on the spot and vanishes on the spot. Sounds fine, until the product folks come find you the next day saying we also need Ctrl+Z undo, an operation history, and the ability to bundle a group of operations into a "macro" for one-click replay. You go back and look at the code: the screen is full of bare function calls, with no trace of "what happened" left anywhere — undo? Fat chance. The functions have already returned; what exactly are you going to roll back with?

The Command pattern exists for exactly this kind of requirement: **turning a "thing to be done" from a fleeting function call into an object with identity, with state, one that can be stored and moved around**. Once an action becomes an object, you can push it into a queue for deferred execution, stuff it into a log for later replay, compose it into macros, or — most common of all — remember it, so that when the user presses Ctrl+Z you run its inverse.

But "wrap the action in an object" is not, in C++, as simple as "write a class around it". There's a trap here that is remarkably easy to step on: the first time many people write the Command pattern, they casually declare `execute()` as `const`, reasoning that "executing a command doesn't change the command object itself" — and then they reach undo and freeze: you have nowhere to record the pre-execution state, because inside a `const` function you promised not to touch members. So the question this article really sets out to answer is — **how do we encapsulate an action into an object cleanly, so that it can both execute and undo, without polluting the receiver and without leaning on fragile runtime type identification**.

From here we'll go step by step, starting from the most intuitive way of writing it, seeing why each step falls short, and finally squeezing out a modern-C++ canonical answer.

## Step 1: The most intuitive approach — calling the function directly (an anti-example)

When most people first face "the editor needs to support insert and delete", the code they write by reflex looks like this:

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

It's pleasant to use too — call it wherever you need it:

```cpp
TextEditor editor;
editor.append_text("Hello, World");
editor.pop_text_once();
```

Honestly, in scenarios where "the action happens on the spot and nothing ever looks back", there is nothing wrong with this style — don't reflexively slap a pattern onto everything you see. The trouble arrives **the moment the requirements start to "look back"** — when the product folks ask for undo, you suddenly realize that once `append_text` returns, it's over: nobody remembers what was inserted, or how long it was. Undo needs "memory", and a bare function call is born without one.

So why not just add the "memory"? We can bundle "the action to perform" together with "what to do when undoing" into a single object — and that is the embryo of the Command pattern.

## Step 2: Encapsulating an action in an object — the abstract Command

We start by defining a uniform interface that every "thing to be done" satisfies. The most crucial step is declaring `execute()` and `undo()` together, making "undoability" a built-in capability of commands from day one rather than a patch bolted on afterwards:

```cpp
struct Command {
    virtual ~Command() = default;
    virtual void execute() = 0;
    virtual void undo() = 0;   // Think through how to undo, from day one
};
```

There's a detail here worth pausing on: why isn't `execute()` `const`? Because the vast majority of undoable commands, at the moment they execute, tuck the "information needed to undo" (say, the length that was inserted, or the old value that got replaced) into their own members, saving it for the later `undo()`. Declare it `const` and you've sealed off your own path to recording state — when the time comes to actually write `undo()`, `const` will have you tied up, and the only way out is hacks like `mutable`. **So the rule here is: `execute()` should not be `const`. The command object carries state; it is not a pure function.**

Next, let's turn "append a piece of text" into a concrete command. It needs to hold a reference to the receiver (`TextEditor`) plus its own parameter (the text to insert), and its `undo()` chops off exactly as much as was inserted:

```cpp
class AppendCommand : public Command {
public:
    AppendCommand(TextEditor& editor, std::string text)
        : editor_(editor), text_(std::move(text)) {}

    void execute() override { editor_.append_text(text_); }
    void undo() override    { editor_.erase_tail(text_.size()); }

private:
    TextEditor& editor_;   // The receiver: the one doing the real work
    std::string text_;     // The parameter: the text this command inserts
};
```

You'll notice a command object is just a package of three things — a reference to the **receiver**, the **parameters** needed to execute, and a pair of `execute()`/`undo()` methods. The interface the receiver (`TextEditor`) exposes (`append_text` / `erase_tail`) is stable; without touching a single line of `TextEditor`'s code, we can wrap an "undoable" capability around it. That is the core dividend of the Command pattern: **the "undoability" of an action no longer pollutes the receiver — the receiver only cares about "what it can do", while "whether it can be undone" is the command layer's business**.

## Let's verify first: can the undo queue really retrace its steps

Talk is cheap, so let's write a minimal undo stack, have it execute a few commands and then undo them one by one, and see whether the buffer really returns to its starting point. First, give `TextEditor` an interface that can chop the tail by length (to support undo):

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

The undo stack's logic is insultingly simple: on `execute`, execute first and then push onto the stack; on `undo`, pop the top and call its `undo()`. LIFO is a natural fit for "undo the most recent step":

```cpp
class UndoStack {
public:
    void execute(std::unique_ptr<Command> c) {
        c->execute();
        history_.push_back(std::move(c));   // Onto the stack only after successful execution
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

Run it: append two chunks, undo them one after another, and finally give the macro command a quick test:

```sh
$ g++ -std=c++23 -O2 command_verify.cpp -o command_verify
$ ./command_verify
after 2 appends : 'Hello, World'
after 1 undo    : 'Hello, '
after 2 undo    : ''
after macro ABC : 'ABC'
after macro undo: ''
```

After two appends the buffer is `Hello, World`; one undo steps it back to just the first half, `Hello,` (that trailing comma and space are still there), and one more undo takes it back to the empty string — the state retraced its path in full. That is the undoability the Command pattern promises, delivered in the flesh.

## Step 3: Bundling several actions together — the macro command

Where did that earlier `macro ABC` come from? From composing commands. The Command pattern is a natural fit for the composite pattern: if "one action" is a command, why can't "a whole sequence of actions" be a command too? Let's write a `MacroCommand` that internally holds a group of sub-commands and is itself a `Command`:

```cpp
class MacroCommand : public Command {
public:
    void add(std::unique_ptr<Command> c) { subs_.push_back(std::move(c)); }

    void execute() override {
        for (auto& c : subs_) c->execute();        // Execute in order
    }
    void undo() override {
        for (auto it = subs_.rbegin(); it != subs_.rend(); ++it)
            (*it)->undo();                          // Undo in reverse order
    }
private:
    std::vector<std::unique_ptr<Command>> subs_;
};
```

Now here's a genuine trap, one newcomers fall into with particular ease. `execute()` iterates forward — so on what grounds does `undo()` go in **reverse**? Think about the stack's last-in-first-out nature and it clicks: the last sub-command to execute touched the freshest state, so to get back to the pre-execution picture you must undo it first, and only then can the previous sub-command have its turn. Take `ABC`: the execution order is `A→B→C`, so the undo order must be `C→B→A`; if your hand slips and you write a forward undo, there simply isn't that much left in the buffer to chop — `erase_tail` goes out of range, or the state stops matching, and you end up with a program that still looks like it runs but whose state has quietly gone haywire.

::: warning The real trap: undo must go in reverse
A macro command's `undo()` must iterate its sub-commands in **reverse**, in strict correspondence with `execute()`'s forward order. The intuition that "forward is ABC, so backward is ABC too" is wrong — going forward piles up state layer by layer, so going backward must peel it back off layer by layer in the order it was stacked. A forward-written undo is the most common hidden bug in this kind of code, and it usually doesn't crash right away: the state errors only surface under particular sequences of operations.
:::

A macro command bundles a group of actions into one atomic unit; to an undo stack, the macro is "one step" — one push, one undo, and the three sub-commands inside roll back together. This is precisely the most bare-bones way to implement "transactional operations".

## Pitfall warning: don't use dynamic_cast to extract parameters

Having gotten this far, there's a common implementation style I have to call out specially, because it shows up in the companion project and has tripped plenty of people. Some implementations flip the design around: the abstract command carries a "type" tag, and at execution time the receiver judges from the tag "is this an Append command", then uses `dynamic_cast` to down-cast the command pointer to the derived class so it can dig out the parameters:

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
                // Use dynamic_cast to get back the text inside AppendCommand
                AddCommand* adder = dynamic_cast<AddCommand*>(command.get());
                if (adder) append_text(adder->append);
            }
        }
    }
};
```

This style runs, and the companion project does write it this way, but it **reeks of anti-pattern**. The problem: the Command pattern worked hard to encapsulate the "execution logic" inside each command's own `execute()`, so the receiver never needs to know which kind of command it is holding; the moment you reach for `dynamic_cast`, the execution logic leaks right back into the receiver, which once again shoulders the burden of "knowing every command type" — every new command means coming back to modify this switch. That is exactly the coupling the Command pattern exists to avoid.

The more practical problem: `dynamic_cast` depends on RTTI (runtime type information), and in embedded work and game engines RTTI is often switched off with `-fno-rtti`, to save whatever `.rodata` and binary size it can. Let's verify what happens when RTTI is turned off:

```sh
$ g++ -std=c++23 -O2 -fno-rtti command_cast.cpp
command_cast.cpp:31:20: error: 'dynamic_cast' not permitted with '-fno-rtti'
   31 |         auto* ap = dynamic_cast<AppendCmd*>(c.get());
command_cast.cpp:33:30: error: cannot use 'typeid' with '-fno-rtti'
   33 |                      typeid(*c).name(),
```

A straight compile failure. In other words, once you've used `dynamic_cast`, your code can never enter those RTTI-disabled projects — portability cut in half on the spot.

::: warning Don't make the receiver recognize commands
The correct approach is to let each command do its own work in its `execute()`; the receiver sees only the abstract interface `Command&` and never needs to know whether it's an `AppendCommand` or an `EraseCommand`. As long as the receiver's underlying interface (`append_text` / `erase_tail`) is stable, adding a new command means adding one derived class — not a single line of the receiver changes. The `dynamic_cast` + type-tag style is, in essence, the Command pattern degenerating back into switch-case. Don't write it that way.
:::

## Step 4: Functional commands — a closure is a command

When you get down to it, a command object is just an "execute" closure plus an "undo" closure. C++ has lambdas and `std::function`, so can we skip hand-writing a pile of derived classes and just slap a command together out of two closures? Yes — and it feels great to write.

Here we go straight to C++23's `std::move_only_function`. Why not the old `std::function`, you ask? Because command objects often need to own resources exclusively (capturing a `unique_ptr`, say, or a file handle), and `std::function` requires its wrapped target to be copyable — it copies the closure internally, so a move-only closure simply doesn't fit. `std::move_only_function` (C++23 onward, header `<functional>`) was born for exactly this: it requires only movability, a perfect match for the exclusive semantics of "one command object gets executed once and undone once".

We rework the undo stack to take two arguments — an "execute closure" and an "undo closure" — and store the pair as one entry:

```cpp
#include <functional>
#include <utility>
#include <vector>

class FunctionalUndoStack {
public:
    void execute(std::move_only_function<void()> do_it,
                 std::move_only_function<void()> undo_it) {
        do_it();                                   // Execute first
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

Usage looks like this: the action and its inverse are packed up and passed in together, the lambdas take care of capturing everything and executing everything, and you never write a single derived class:

```cpp
TextBuffer buf;
FunctionalUndoStack stack;
std::string chunk = "World";

stack.execute(
    [&buf, chunk] { buf.append(chunk); },          // Execute: insert
    [&buf, chunk] { buf.erase_tail(chunk.size()); } // Undo: chop off the equal-length tail
);
```

Let's verify this path as well, to confirm the closure route runs perfectly well:

```sh
$ g++ -std=c++23 -O2 command_lambda.cpp -o command_lambda
$ ./command_lambda
after execute: 'World'
after undo   : ''
```

Clean and crisp. So what's the price of this functional style? The price is that **the type boundary is invisible at compile time**: in the OOP style, `AppendCommand` is a named type — one grep and you know which commands exist in the project and what each one's `undo()` looks like; in the functional style, commands are scattered across lambdas everywhere, the type system treats them all alike, and readability and discoverability drop a notch. So the trade-off between the two roads is clear — if the set of commands is small, you want clear types, and you plan to add macros and logging uniformly, go with OOP derived classes; if actions are one-shot, the closure can be written in place, and you don't want to define a class for a single action, go with functional closures. The two styles don't conflict; they coexist happily within the same project.

## In practice: a command queue that can "optimize execution"

The undo queue above is the Command pattern's most classic application, but the pattern's powers don't stop there. The companion project has a rather clever trick: instead of "execute each command as it arrives", it first accumulates a batch of commands in an `Invoker` and runs a **simplification** pass before actually executing — if an "insert" is immediately followed by a "delete", the two cancel out and never need to run at all. It's the command-layer cousin of dead-code elimination in a compiler.

First look at the simplification logic itself: it's a single pass maintaining a result stack — an "insert" gets pushed; a "delete" pops the "insert" off the top (cancellation); everything else is copied over as-is:

```cpp
void simplify() {
    std::vector<std::shared_ptr<TextEditorCommand>> result;
    for (const auto& cmd : commands) {
        if (!result.empty()
            && result.back()->get_type() == Type::APPEND
            && cmd->get_type() == Type::REMOVE) {
            result.pop_back();        // insert + delete = did nothing; they cancel
        } else {
            result.push_back(cmd);
        }
    }
    commands = std::move(result);
}
```

Let's run the companion project's input straight through: four commands, `ADD("Hello, World")`, `ADD("Hello, World")`, `ERASE`, `ADD("Hello, World")`. The simplifier's eye stays on the queue: the first two ADDs each get pushed onto the result; when the third command, ERASE, arrives, the top of the stack is exactly an ADD, so the top cancels (the second ADD is gone); the fourth ADD gets pushed again. The final queue holds two ADDs, and after execution the buffer contains two lines of `Hello, World`:

```sh
$ g++ -std=c++23 -O2 TextEditorMain.cpp -o TextEditor
$ ./TextEditor
Hello, World
Hello, World
```

Two lines — exactly the simplified result. This is where the Command pattern beats bare function calls: because actions are objects, you can statically analyze them, optimize them, batch them, and replay them **before** execution, without touching a line of the receiver. The companion project also demonstrates another capability of the `Invoker` in passing: `append_command` adds commands to it, and `remove_command` can pull a specified command out of the queue — commands are objects, so they can be added, removed, referenced, canceled; none of that exists in a world of bare function calls.

::: tip The companion compilable project
The complete code for this section (the command queue, simplification-by-cancellation, and the `dynamic_cast`-parameter-extraction anti-example) lives in this repository — clone it, run cmake once, and it just works: [Command/TextEditor](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/tree/main/code/volumn_codes/vol4/design-patterns/Command/TextEditor).
:::

## When you shouldn't use the Command pattern

At this point we have a command system that can undo, compose, and optimize. But we're not done — I have to be honest with you: **the Command pattern is not a panacea. In many scenarios you simply don't need it, and forcing it on makes the code more convoluted, not less**.

First, if your actions will **never be undone, never be delayed, never be queued**, don't apply the Command pattern. For something a single `editor.append_text("x")` solves, insisting on wrapping an `AppendCommand`, stuffing it into a queue, and then finding an Invoker to trigger it earns you nothing beyond one extra hop of indirection and one extra heap allocation. Patterns exist for changing requirements; when the requirements aren't changing, calling the function directly is always the optimal answer.

Second, the cost of implementing undo is easily underestimated. Our earlier "chop off as much as was inserted" undo holds only for the simplest append operations; the moment your operations involve replacement, cursor movement, or several buffers moving in concert, the "pre-execution state" that `undo()` must save balloons quickly, and at that point you usually need the Memento pattern to snapshot the receiver's entire state — memory cost and complexity both step up a level. The "undoability" the Command pattern promises is not free; the cost of saving state is its most tangible price.

Third, the **lifetime management** of command objects is an invisible pit. Commands hold references to the receiver, so the receiver must outlive them; the instant the receiver is destroyed before the commands, the references inside `execute()` / `undo()` become dangling references — the classic use-after-free. In the companion project you can see that it manages the commands themselves with `std::shared_ptr<TextEditorCommand>`, but the lifetime of the receiver `TextEditor` is guaranteed by hand — once the queue has accumulated a pile of commands while the receiver dies early, the whole queue is wrecked. This one is especially lethal in asynchronous, cross-thread scenarios.

## Summary

Let's trace the whole evolutionary path once through:

| Stage | Approach | Why it's still not enough |
|---|---|---|
| Direct function calls | `editor.append_text(...)` | No memory: no undo, queuing, or replay |
| Abstract Command | `execute()` + `undo()` derived classes | Receiver stays clean, but every new action needs a new class |
| Macro command | Compose several commands, undo in reverse | Solved — but watch the reverse-order undo trap |
| `dynamic_cast` for parameters | Receiver down-casts commands by tag | **Anti-pattern**: leaks coupling, depends on RTTI, fails to compile once RTTI is off |
| Functional closures | Two closures packed into `std::move_only_function` | Solved — but the type boundary weakens and discoverability drops |

Note down these key takeaways:

- The essence of the Command pattern is lifting an "action" from a fleeting function call to an **object with identity, with state, one that can be stored** — which is what makes undo, queuing, replay, and composition possible.
- Don't mark `execute()` `const` — the command object must record the state undo will need; it is not a pure function.
- A macro command's `undo()` must iterate its sub-commands in reverse; that is the most common hidden bug in this kind of code.
- Don't use `dynamic_cast` + type tags to "recognize" command types — it degenerates the Command pattern back into switch-case, and it won't even compile with RTTI disabled. The right way is to let each command do its own work in `execute()`.
- C++23's `std::move_only_function` makes "packing two closures into one command" natural, a good fit for one-shot, move-only actions; the price is a weaker type boundary, so whether to use it depends on how much you value discoverability.
- Commands hold references to the receiver — make sure the receiver outlives the commands, or undo time becomes use-after-free time.

## References

- [cppreference: `std::move_only_function`](https://en.cppreference.com/w/cpp/utility/functional/move_only_function) (C++23, a move-only callable wrapper)
- [cppreference: `std::function`](https://en.cppreference.com/w/cpp/utility/functional/function) (for comparison; requires copyability)
- [cppreference: `dynamic_cast`](https://en.cppreference.com/w/cpp/language/dynamic_cast) (runtime type identification; depends on RTTI)
- Gamma, Helm, Johnson, and Vlissides, *Design Patterns*, the Command chapter; Klaus Iglberger, *C++ Software Design*, the discussion of Command and type erasure
- The companion compilable project: [Command/TextEditor](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/tree/main/code/volumn_codes/vol4/design-patterns/Command/TextEditor)
