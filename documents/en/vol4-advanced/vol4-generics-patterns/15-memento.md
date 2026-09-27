---
title: 'Memento Pattern: Encapsulating State in an Opaque Black Box'
description: Starting from the most intuitive "full copy", we derive snapshots/undo/redo step by step, tighten the memento into a black box with `friend`, and then expose the pitfall of `make_shared` colliding with a private constructor
chapter: 11
order: 15
tags:
  - host
  - cpp-modern
  - intermediate
  - 备忘录模式
difficulty: intermediate
platform: host
cpp_standard: [11, 17, 20]
reading_time_minutes: 20
related:
  - 'Command Pattern: Turning Actions into Undoable Objects'
prerequisites:
  - Classes and Object-Oriented Programming
translation:
  source: documents/vol4-advanced/vol4-generics-patterns/15-memento.md
  source_hash: d2a5b8ee57ceca1899acaa006a5762b6401a33a6d09c3697654ddd449205a66b
  translated_at: '2026-09-26T05:45:30+00:00'
  engine: anthropic
  token_count: 3500
---

# Memento Pattern: Encapsulating State in an Opaque Black Box

## What problem are we actually solving

Let's skip the definition for now. Think of a feature you use every single day but have almost never noticed: Ctrl+Z in a text editor. You type a line, type another, move the cursor around a few times, then hit undo — and the editor travels back in time to how things looked a few steps ago. Have you ever wondered **how it knows what "a few steps ago" looked like, without scattering your document's internal details (buffer pointers, cursor offsets, selection endpoints) all over the program?**

The most brute-force approach: before every keystroke, `deep copy` the entire document and stash it away. Undo means hopping back to the previous copy. Nothing wrong with the logic — it runs — but the cost is glaring: once the document gets big and the operations pile up, the history stack devours memory in no time, and the `deep copy` itself can be expensive (nested structures, handles, subcomponents — not a single one may be missed). The sneakier trouble is this: **once that copy lands in someone else's hands, who guarantees it won't be quietly tampered with?** The undo system is holding a copy of "the past you"; if it casually writes `m->content = "hacked"`, your "immutable history" exists in name only.

This is exactly the class of requirements the Memento pattern addresses: **capture an object's internal state — without exposing the object's internal implementation — and keep it for restoring verbatim later**. It's a natural pair with the Command pattern ([the article we wrote earlier](./13-command.md)): the Command pattern objectifies "actions", the Memento pattern objectifies "state". The Command pattern does undo by "storing the inverse operation" — lightweight, but easy to miscompute on complex operations; the Memento pattern does undo by "storing a full snapshot" — robust, but memory-hungry. Choosing between them is fundamentally a trade-off between **the computational cost of inverse operations** and **the storage cost of a full snapshot**.

From here we'll go step by step, starting from the most intuitive full copy, seeing why each step falls short, and finally squeezing out a modern formulation that is both properly encapsulated and elegantly supports undo and redo.

## Step 1: The most intuitive approach — public fields + a full copy

Many people's first attempt at a snapshot looks something like this: define a struct mirroring the editor's state exactly, make every field public, and `make_shared` one to stash away when saving:

```cpp
struct EditorMemento {
    std::string content;
    std::size_t cursor_pos;
    EditorMemento(std::string c, std::size_t p)
        : content(std::move(c)), cursor_pos(p) {}
};

class TextEditor {
public:
    void insert(const std::string& s) {
        content_.insert(cursor_pos_, s);
        cursor_pos_ += s.size();
    }

    std::shared_ptr<EditorMemento> create_memento() const {
        return std::make_shared<EditorMemento>(content_, cursor_pos_);
    }

    void restore(std::shared_ptr<const EditorMemento> m) {
        if (!m) return;
        content_ = m->content;
        cursor_pos_ = m->cursor_pos;
    }

private:
    std::string content_;
    std::size_t cursor_pos_ = 0;
};
```

It does work — `create_memento()` snaps the photo, `restore()` pastes it back. But look closely at `EditorMemento` and an unsettling fact appears: **its `content` and `cursor_pos` are entirely public**. That means anyone holding this `shared_ptr` — the undo stack, the serialization module, even some intermediate layer passed across modules — can read and write those fields directly.

You might think: we're all colleagues here, who would go and modify a snapshot? But the Memento pattern's demand on encapsulation is precisely the opposite: **it must guarantee "cannot be modified", not "please don't modify"**. A public memento lays the editor's internal representation bare to the entire program. One day somebody, just for debugging, writes `m->content.clear()` on a snapshot in the undo stack — and while you're tracking the crash down, "a historical snapshot got modified" is the last possibility you'd think of.

::: warning A commonly overlooked encapsulation trap
The all-public-fields `EditorMemento` is all over the place in Memento examples online, but it actually **violates the core promise of the Memento pattern**: a memento should be a black box to the outside — unreadable and unwritable — with only the Originator allowed to read and write its own state. The original GoF description of this pattern deliberately distinguishes a "wide interface" (full access, available only to the Originator) from a "narrow interface" (the opaque handle the Caretaker gets to hold). Our step-one version has only the wide interface and no narrow one — the encapsulation is a no-op. Leave this unfixed, and once the undo stack grows complicated, sooner or later someone will get bright ideas about those snapshots.
:::

So this version works, but the encapsulation isn't standing yet. We need a way to make the memento "transparent to the originator, a black box to the outside world".

## Step 2: Turning the memento into a black box — nested class + friend

C++ hands us a mechanism practically tailor-made for the Memento pattern: **declare the memento as a nested class of the originator, make its constructor and fields all private, and then turn around and make the originator its friend**. This way, across the entire program **only the originator itself** can construct the memento and read or write its fields; everyone else (the undo stack included) gets an opaque object in which even `content` is invisible.

```cpp
class TextEditor {
public:
    // The memento is a nested type of the Originator — a black box to the outside
    class Memento {
        friend class TextEditor;  // Only TextEditor can access what follows
        std::string content;
        std::size_t cursor_pos = 0;

        Memento(std::string c, std::size_t p)
            : content(std::move(c)), cursor_pos(p) {}

    public:
        // The outside (Caretaker included) can only copy/move this opaque handle; the contents are unreadable
        Memento() = default;
        Memento(const Memento&) = default;
        Memento(Memento&&) = default;
        Memento& operator=(const Memento&) = default;
        Memento& operator=(Memento&&) = default;
    };

    std::shared_ptr<Memento> create_memento() const {
        return std::shared_ptr<Memento>(new Memento(content_, cursor_pos_));
    }

    void restore(const std::shared_ptr<Memento>& m) {
        if (!m) return;
        content_ = m->content;       // friend grant: private fields are accessible here
        cursor_pos_ = m->cursor_pos;
    }

    void insert(const std::string& s) {
        content_.insert(cursor_pos_, s);
        cursor_pos_ += s.size();
    }

private:
    std::string content_;
    std::size_t cursor_pos_ = 0;
};
```

Note the key design moves in this version. The memento has moved inside `TextEditor`, becoming `TextEditor::Memento`; its constructor and two fields are all `private`, and its only friend is `TextEditor` itself. That's why `new Memento(...)` compiles inside `create_memento` (the originator is a friend) and `m->content` is readable inside `restore` (again because of the friend declaration); while outside code, even clutching a `shared_ptr<Memento>`, cannot touch a single character of `content`.

The public part of `Memento` keeps only the default constructor and the set of copy/move special member functions — enough for the undo stack (a `std::vector<shared_ptr<Memento>>`) and `shared_ptr` to move it around normally — but it never exposes the real internal state to the outside world. This is what GoF calls the "narrow interface": what the Caretaker holds is an **opaque handle** — it can store it, pass it, drop it, but it cannot see what's inside.

Let's first verify that this encapsulation actually holds.

## Let's verify first: can the outside world really not read the memento's contents

Talk is cheap, so let's deliberately write a line in `main` that "tries to read the snapshot's contents from the outside" and see whether the compiler goes along:

```cpp
int main() {
    TextEditor editor;
    editor.insert("Hello");
    auto snap = editor.create_memento();

    // Outside code tries to read m->content — this line should fail to compile
    std::cout << snap->content << "\n";   // ERROR
    return 0;
}
```

Compile, and here's the real output (`g++ 16.1.1`, `-std=c++23 -O2`):

```sh
$ g++ -std=c++23 -O2 memento_encap_break.cpp -o memento_encap_break
memento_encap_break.cpp:53:24: error: 'std::string TextEditor::Memento::content'
      is private within this context
memento_encap_break.cpp:10:21: note: declared private here
```

The compiler slams the door in our face: `content is private within this context`. That is friend + nested class nailing the encapsulation shut — **not a reminder in a comment, not a convention, but a hard compile-time constraint**. The undo stack, the serialization module, any external code: all any of them can get is an opaque `shared_ptr<Memento>`, completely unaware that its internal fields even exist.

The normal "save a snapshot, then restore it" flow, on the other hand, goes through just fine. Let's run it:

```sh
$ g++ -std=c++23 -O2 memento_verify2.cpp -o memento_verify2
$ ./memento_verify2
Content: "Hello, world" | Cursor@12
Content: "Hello" | Cursor@5
```

The first line is before the restore (`, world` already inserted), the second is after `restore(snap)` — both cursor and content return precisely to the moment of `Hello`. The encapsulation stands, and nothing was lost functionally.

## Pitfall warning: `make_shared` colliding with a private constructor

::: warning Pitfall warning
If you follow the inertia of step one and also create snapshots in step two with `std::make_shared<Memento>(...)`, the code will **fail to compile** — with an error message scary enough that newcomers may give up on the spot. Let's step into the pit first, then explain why.

Change `create_memento` to this line:

```cpp
std::shared_ptr<Memento> create_memento() const {
    return std::make_shared<Memento>(content_, cursor_pos_);  // ⚠️ does not compile
}
```

Compile, real output (key lines excerpted):

```sh
$ g++ -std=c++23 -O2 memento_verify.cpp -o memento_verify
.../stl_construct.h:133:7: error: 'Memento(std::string, std::size_t)'
      is private within this context
.../memento_verify.cpp:15:9: note: declared private here
```

The heart of the error is that the `Memento(...)` constructor `is private within this context` — that is, the construction call happens in **a context with no right to access the private constructor**.

Where does the problem come from? `std::make_shared` doesn't just `new` the object and call it a day: it wants to allocate the object and the control block in the same block of memory, so internally it goes down the `std::allocator_traits<...>::construct` -> `std::construct_at` -> placement `new` path. The code along that path **lives inside the standard library, not inside `TextEditor`**; and your `friend class TextEditor` lets through the single class `TextEditor` only — the standard library's allocation infrastructure is nowhere near the whitelist. So when `construct_at` tries to call the private constructor, access control kicks it right back out.
:::

The fix is dead simple: **bypass `make_shared` and use `std::shared_ptr<Memento>(new Memento(...))` instead**. On this path it is `TextEditor` itself, inside `create_memento`, calling the private constructor **directly** (the originator is the friend, so it has the right to call it), bypassing every bit of standard-library allocator infrastructure — so it compiles.

```cpp
std::shared_ptr<Memento> create_memento() const {
    // TextEditor is a friend of Memento: calling the private constructor directly here is legal.
    // Skip make_shared — otherwise allocator_traits::construct runs into access control.
    return std::shared_ptr<Memento>(new Memento(content_, cursor_pos_));
}
```

The cost is one lost control-block merge and one extra independent heap allocation (`make_shared` packs the object and the control block into a single allocation; `shared_ptr(new ...)` takes two). For an object like a memento — **created infrequently, with a fairly short lifetime** — this overhead is entirely acceptable in exchange for encapsulation that genuinely holds. If you truly care about that one allocation, other routes exist (pairing `Memento` with `std::enable_shared_from_this` plus a static factory, or simply giving `Memento` value semantics instead of using `shared_ptr`), but each complicates the code by a notch; in most scenarios it isn't worth it.

Remember this conclusion: **once you build memento encapsulation on friend + a private constructor, stop creating snapshots with `make_shared`** — `shared_ptr(new ...)` is all you need.

## In practice: a history stack with undo/redo

A single snapshot only gets you "back to one particular moment". Real editors support **continuous undo and continuous redo**: I undo three steps, change my mind and redo two, and somewhere in between I might insert a new edit that invalidates the entire "redo future". That calls for a separate `History` class (the Caretaker in GoF terms), which maintains a linear sequence of snapshots and a "current pointer" — undo moves the pointer back, redo moves it forward.

```cpp
class History {
public:
    void push(std::shared_ptr<TextEditor::Memento> m) {
        // Inserting a new snapshot away from the tail: discard the redo branch after it
        if (cursor_ + 1 < static_cast<int>(stack_.size())) {
            stack_.erase(stack_.begin() + cursor_ + 1, stack_.end());
        }
        stack_.push_back(std::move(m));
        cursor_ = static_cast<int>(stack_.size()) - 1;
    }

    bool can_undo() const { return cursor_ > 0; }
    bool can_redo() const {
        return cursor_ + 1 < static_cast<int>(stack_.size());
    }

    std::shared_ptr<TextEditor::Memento> undo() {
        if (!can_undo()) return nullptr;
        --cursor_;
        return stack_[static_cast<std::size_t>(cursor_)];
    }

    std::shared_ptr<TextEditor::Memento> redo() {
        if (!can_redo()) return nullptr;
        ++cursor_;
        return stack_[static_cast<std::size_t>(cursor_)];
    }

private:
    std::vector<std::shared_ptr<TextEditor::Memento>> stack_;
    int cursor_ = -1;   // -1 means empty history
};
```

A few design points in this version deserve comment. The first `if` in `push` is the "discard the redo branch" logic — it means: **once you insert a new snapshot from a position in the middle of the history (after having undone a few steps), you have effectively opened a new fork in the timeline, and the old "redo future" should no longer exist**. This is exactly the editor behavior you know: undo twice, type one new character, and the old redo chain is gone. Without this step, the redo stack would drift out of sync with the actual state, and the undo system would restore you to a "past that never existed".

`cursor_` is an `int` rather than a `size_t` so that `-1`, the "empty history" state, has a natural representation; whenever it is compared against `stack_.size()` (unsigned), the code consistently goes through `static_cast<int>` for an explicit conversion, dodging signed/unsigned comparison warnings. `undo` / `redo` both follow the route of checking `can_undo` / `can_redo` first, then moving the pointer; on an empty history or out of bounds they return `nullptr`, and the caller, on receiving `nullptr`, naturally skips the restore — the semantics hang together.

You'll also notice that `History` holds `std::shared_ptr<TextEditor::Memento>` — a type whose **fully qualified name you must spell out**, because `Memento` is a nested class of `TextEditor`. This actually exposes a design trade-off: making the memento a nested type buys good encapsulation, but it forces the manager (Caretaker) to **depend on the Originator** at the type level. In our simple scenario that doesn't matter; but if you want `History` to become a generic undo framework serving any number of originators, you'd have to abstract the "opaque handle" further — into a type-erased `std::any`, or an interface exposing only `apply()`. That's a topic for another article, so we won't unfold it here.

Let's run it once and see whether undo/redo and branch discarding actually behave:

```sh
$ g++ -std=c++23 -O2 memento_history.cpp -o memento_history
$ ./memento_history
Content: "Hello, world" | Cursor@12
[undo] Content: "Hello" | Cursor@5
[undo] Content: "" | Cursor@0
[redo] Content: "Hello" | Cursor@5
[edit] Content: "Hello!!!" | Cursor@8
can_redo = 0 (expect 0)
can_undo = 1 (expect 1)
```

Let's untangle this trajectory. After inserting two pieces of text the state is `Hello, world`; undoing twice takes us back to `Hello` and then the empty string; redoing once advances us to `Hello` again. Now, mid-redo, we insert `!!!` — and `can_redo` immediately drops back to `0`: the old "redo future" has been cleanly discarded, while `can_undo` remains `1`, because the new `Hello!!!` state is itself undoable. Behavior matches expectations exactly.

## When to use the Memento pattern, and when not to

By now we have a solidly encapsulated implementation that can undo and redo. But we're not done — I have to be honest with you: the Memento pattern is no cure-all; it has bills of its own to settle.

**First, it eats memory — linearly.** Every snapshot stored is a full copy of the state. For a text document of a few hundred KB, a few dozen undos are fine; but if the object you are snapshotting is a 3D scene with millions of vertices, or a large business object stuffed with caches, the history stack will exhaust memory in short order. Mitigations come in a few flavors: cap the history depth (keep only the last N steps), make delta snapshots (store only what changed relative to the previous step), or abandon full snapshots altogether and switch to the [Command pattern](./13-command.md) to store only inverse operations. Delta snapshots sound lovely but are error-prone to implement — you must guarantee that "any delta applied to its baseline restores the state exactly", and the boundary cases are numerous enough to bald you; so most implementations honestly keep storing full snapshots and lean on a depth cap as the safety net.

**Second, the cost of the encapsulation isn't in writing it — it's in maintaining it.** Every time the Originator gains an internal state field, you must remember to handle it in `create_memento` and `restore` in sync. Miss one field and undo starts producing "why didn't this setting revert after my undo" voodoo bugs — bugs that surface only when a user actually undoes that specific field, so slightly thin test coverage lets them slip right through. A practical discipline: **the memento's field set should correspond one-to-one with the set of the Originator's state fields that participate in snapshots**; when adding a field, treat the memento as the Originator's "mirror" and change them together.

**Third, it's not an either-or against the Command pattern — they are frequent collaborators.** The Command pattern excels at "objectifying actions, replaying them, packaging them into macros", but its undo relies on computing inverse operations, which is easy to get wrong on complex ones; the Memento pattern excels at "reliably returning to a definite state", but eats memory and is coarse-grained. The common combination in real engineering: **use the Command pattern to organize the operation flow, and use mementos to backstop those complex commands whose "inverse operations are hard to compute"** — snap a photo before the command executes, and restore on undo; you keep the Command pattern's replay and macro capabilities, and buy absolute undo correctness with a snapshot. As our earlier Command pattern article mentioned, once operations involve replacement, cursor movement, or multi-buffer linkage, the "pre-execution state" that `undo()` has to save balloons quickly — that's when you reach for the memento. The two articles plug into each other right here.

::: tip Command vs. Memento: how to choose
In one sentence: **simple operations, large state — use the Command pattern** (store inverse operations, save memory); **complex operations, small state — use the Memento pattern** (store full snapshots, robust). When both are complex, let the Command pattern build the skeleton and let mementos backstop the complex commands.
:::

## Summary

Let's walk the whole evolution path once:

| Stage | Approach | Why it wasn't enough |
|---|---|---|
| Public-field memento | `struct EditorMemento { public: ... }` | All fields public, encapsulation in name only, snapshots tamperable at will |
| Nested class + friend | Private constructor, private fields, `friend class Originator` | Encapsulation holds, but creation must use `shared_ptr(new ...)`; `make_shared` is off the table |
| History-stack Caretaker | `vector<shared_ptr<Memento>>` + current pointer | A single memento only goes "back to one moment"; continuous undo/redo needs the manager |
| Command + Memento combined | Commands organize the flow; complex commands snapshot before executing | Pure snapshots eat memory, pure commands miscompute inverses — the two complement each other |

Note down these key conclusions:

- **The core of the memento is encapsulation, not the snapshot itself** — anyone can take a snapshot; only a snapshot that is "a black box to the outside, transparent to the originator" deserves the name Memento pattern. The standard C++ way to achieve that is a nested class + private constructor + `friend class Originator`.
- **Create snapshots with `std::shared_ptr<Memento>(new ...)`, not `std::make_shared`** — a private constructor triggers an access-control error on the `allocator_traits::construct` path that `make_shared` takes. This is the chapter's most trip-prone pitfall.
- **The essence of undo/redo is a linear history with a pointer** — `undo` moves the pointer back, `redo` moves it forward, and inserting a new snapshot away from the tail discards the redo branch after it. That is the behavior model of every editor you know.
- **Mementos eat memory, and their fields must be maintained as the Originator's mirror** — with large state and simple operations, prefer the Command pattern's inverse operations; when both are complex, commands build the skeleton and mementos backstop the complex ones.

::: tip Companion compilable project
The examples in this section have a complete compilable project in the repository under `code/volumn_codes/vol4/design-patterns/Memento/` (`.h` + main + `CMakeLists.txt`); `cmake -S . -B build && cmake --build build` reproduces the outputs above.
:::

## References

- [cppreference: `std::shared_ptr` and `std::make_shared`](https://en.cppreference.com/w/cpp/memory/shared_ptr/make_shared) (the allocation difference between `make_shared` and `shared_ptr(new ...)`, since C++11)
- [cppreference: nested classes and friends](https://en.cppreference.com/w/cpp/language/nested_type) (access-control semantics of C++ nested classes and `friend`)
- GoF, *Design Patterns: Elements of Reusable Object-Oriented Software* — the original definition of the Memento pattern, which proposed the "wide interface / narrow interface" dichotomy
