---
title: '**Memento Pattern: Encapsulating ''State'' in an Opaque Black Box**'
description: Starting with the most intuitive "full copy," we progressively derive
  snapshot/undo/redo functionality, conveniently using `friend` to tighten the memento
  into a black box, and then expose the pitfalls of `make_shared` colliding with private
  constructors.
chapter: 11
order: 15
tags:
- host
- cpp-modern
- intermediate
- 备忘录模式
difficulty: intermediate
platform: host
cpp_standard:
- 11
- 17
- 20
reading_time_minutes: 20
related:
- 命令模式:把「动作」变成能撤销的对象
prerequisites:
- 'Chapter 6: 类与对象'
translation:
  source: documents/vol4-advanced/vol4-generics-patterns/15-memento.md
  source_hash: d2a5b8ee57ceca1899acaa006a5762b6401a33a6d09c3697654ddd449205a66b
  translated_at: '2026-06-24T01:00:48.753975+00:00'
  engine: anthropic
  token_count: 3014
---
# Memento Pattern: Encapsulating State in an Opaque Black Box

## What Problem Are We Actually Solving?

Let's skip the formal definition for a moment. Think about a feature you use every day but rarely think about: Ctrl+Z in a text editor. You type a line, type another, move the cursor a few times, and then press undo—the editor seems to travel back in time, reverting to a previous state. Have you ever wondered **how it knows what the past looked like without scattering internal details of your document (buffer pointers, cursor offsets, selection ranges) all over the place?**

The most brute-force approach is to `deep copy` the entire document before every keystroke and store it. Undoing simply means reverting to the previous copy. This logic works, but the cost is obvious: as the document grows and operations accumulate, the history stack quickly devours memory. Furthermore, `deep copy` itself can be expensive (nested structures, handles, and subcomponents must all be handled). A more subtle issue is **once this copy is out in the wild, who guarantees it won't be silently modified?** If the undo system holds a copy of "the past you," and someone casually does `m->content = "hacked"`, your "immutable history" becomes meaningless.

The Memento pattern is designed to solve exactly this class of requirements: **capturing an object's internal state without exposing its implementation details, so that it can be restored later in its original form.** It is a natural counterpart to the Command pattern ([which we covered earlier](./13-command.md))—the Command pattern objectifies "actions," while the Memento pattern objectifies "state." The Command pattern supports undo by "storing reverse operations," which is lightweight but prone to calculation errors in complex scenarios; the Memento pattern supports undo by "storing full snapshots," which is robust but memory-intensive. Choosing between them essentially boils down to a trade-off between **the computational cost of reverse operations** and **the storage cost of full snapshots**.

Next, we will proceed step-by-step, starting with the most intuitive full copy approach. We will see why each step falls short, eventually forcing us to arrive at a modern C++ approach that is both properly encapsulated and elegantly supports undo and redo.

## Step 1: The Most Intuitive Approach — Public Fields + Full Copy

Many people's first attempt at a snapshot looks something like this: define a structure identical to the editor's state, make all fields public, and use `make_shared` to stash a copy when saving:

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

It certainly works—`create_memento()` takes a snapshot, and `restore()` pastes it back. But if you look closely at `EditorMemento`, you will discover an unsettling fact: **its `content` and `cursor_pos` are all public**. This means anyone holding this `shared_ptr`—the undo stack, the serialization module, or even some intermediate layer passed across modules—can directly read and write the fields inside.

You might think, "We are all colleagues here, who would modify a snapshot?" But the encapsulation requirements of the Memento pattern are actually the opposite: **it must guarantee that modification is "impossible," rather than merely hoping "no one changes it."** A public memento effectively exposes the editor's internal representation to the entire program. One day, someone might write `m->content.clear()` in the undo stack just for debugging, and you will never guess that the crash was caused by a corrupted historical snapshot.

::: warning A Commonly Overlooked Encapsulation Pitfall
The approach of making all `EditorMemento` fields public is rampant in online Memento examples, but it actually **violates the core promise of the Memento pattern**: a memento should be a black box that is unreadable and unwritable to the outside world; only the Originator itself should be able to read and write its own state. The original GoF (Gang of Four) description specifically distinguishes between a "wide interface" (full access available only to the Originator) and a "narrow interface" (an opaque handle that the Caretaker can only hold). Our current implementation has only a wide interface and lacks a narrow interface, so encapsulation is non-existent. If we don't fix this, once the undo stack becomes complex, someone will inevitably try to mess with the snapshots.
:::

So, this version works, but the encapsulation is not established. We need to find a way to make the memento "transparent to the Originator, a black box to the outside world."

## Step 2: Making the Memento a Black Box — Nested Class + friend

C++ provides a mechanism almost tailor-made for the Memento pattern: **declare the Memento as a nested class of the Originator, make its constructor and fields all private, and then make the Originator its friend**. This way, **only the Originator itself** throughout the entire program can construct the memento and read/write its fields; everyone else (including the undo stack) receives an opaque object that cannot even see `content`.

```cpp
class TextEditor {
public:
    // 备忘录是 Originator 的嵌套类型,对外是个黑盒
    class Memento {
        friend class TextEditor;  // 只有 TextEditor 能访问下面这些
        std::string content;
        std::size_t cursor_pos = 0;

        Memento(std::string c, std::size_t p)
            : content(std::move(c)), cursor_pos(p) {}

    public:
        // 外部(含 Caretaker)只能拷贝/移动这个不透明句柄,读不到内容
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
        content_ = m->content;       // friend 授权:这里能访问私有字段
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

Let's examine the key design decisions in this version. The `Memento` has been moved inside `TextEditor`, becoming `TextEditor::Memento`. Its constructor and two fields are now `private`, with `TextEditor` itself being the only friend. This means that `new Memento(...)` works inside `create_memento` (because the caller is a friend), and `m->content` is readable inside `restore` (also because of the friend relationship). However, external code—even if holding a `shared_ptr<Memento>`—cannot touch a single character of `content`.

The public section of `Memento` only exposes the default constructor and a set of copy/move special member functions. This allows the undo stack (a `std::vector<shared_ptr<Memento>>`) and `shared_ptr` to handle it correctly, but it never exposes the actual internal state to the outside world. This is the "narrow interface" described by GoF—the Caretaker holds an **opaque handle**: it can be stored, passed, or discarded, but its contents remain invisible.

Let's first verify that this encapsulation actually holds.

## Verification: Can the outside world really not read the Memento content?

Talk is cheap. Let's intentionally write a line of code in `main` that "attempts to read snapshot content from the outside" to see if the compiler allows it:

```cpp
int main() {
    TextEditor editor;
    editor.insert("Hello");
    auto snap = editor.create_memento();

    // 外部代码试图读取 m->content —— 这一行应当编译失败
    std::cout << snap->content << "\n";   // ERROR
    return 0;
}
```

Compile, and here is the actual output (`g++ 16.1.1`, `-std=c++23 -O2`):

```sh
$ g++ -std=c++23 -O2 memento_encap_break.cpp -o memento_encap_break
memento_encap_break.cpp:53:24: error: 'std::string TextEditor::Memento::content'
      is private within this context
memento_encap_break.cpp:10:21: note: declared private here
```

The compiler blocks us outright with: `content is private within this context`. This demonstrates how `friend` combined with nested classes locks encapsulation down tight—**enforced by compile-time hard constraints, not by comments or conventions**. The undo stack, serialization module, or any external code can only obtain an opaque `shared_ptr<Memento>`, completely unaware of the internal fields.

The standard "save snapshot, then restore" flow works correctly, so let's run through it:

```sh
$ g++ -std=c++23 -O2 memento_verify2.cpp -o memento_verify2
$ ./memento_verify2
Content: "Hello, world" | Cursor@12
Content: "Hello" | Cursor@5
```

The first line shows the state before restoration (where `, world` has already been inserted), and the second line shows the result after `restore(snap)`—both the cursor position and the content are precisely restored to the exact moment of `Hello`. The encapsulation holds, and the functionality is intact.

## Pitfall Warning: `make_shared` and Private Constructors

::: warning Pitfall Warning
If you follow the inertia from the first step and use `std::make_shared<Memento>(...)` to create a snapshot in the second step, the code will **fail to compile**. The error message is terrifyingly long, and newcomers might easily give up on the spot. We will show you the pitfall first, and then explain why.

Change `create_memento` to this line:

```cpp
std::shared_ptr<Memento> create_memento() const {
    return std::make_shared<Memento>(content_, cursor_pos_);  // ⚠️ 编不过
}
```

```text
Compiling, actual output (key lines excerpted):
```

```sh
$ g++ -std=c++23 -O2 memento_verify.cpp -o memento_verify
.../stl_construct.h:133:7: error: 'Memento(std::string, std::size_t)'
      is private within this context
.../memento_verify.cpp:15:9: note: declared private here
```

The core of the error is that the constructor `Memento(...)` `is private within this context`—meaning the construction call is happening in **a context that is not authorized to access the private constructor**.

Where is the problem? `std::make_shared` doesn't just directly `new` the object and call it a day; it needs to allocate the object and the control block within the same contiguous memory block. Internally, it follows the path of `std::allocator_traits<...>::construct` -> `std::construct_at` -> placement `new`. The code along this path **belongs to the standard library internals, not `TextEditor`**. Since you declared `friend class TextEditor`, you only granted access to the `TextEditor` class specifically. The standard library's allocation infrastructure is not on the whitelist. Consequently, when `construct_at` attempts to call the private constructor, access control kicks it out.

:::

The solution is very straightforward: **bypass `make_shared` and use `std::shared_ptr<Memento>(new Memento(...))` instead**. This path allows `TextEditor` itself to call the private constructor **directly** within `create_memento` (the initiator is the friend, so it has the right to call it), without going through any standard library allocator infrastructure, so it compiles successfully.

```cpp
std::shared_ptr<Memento> create_memento() const {
    // TextEditor 是 Memento 的 friend,这里直接调私有构造,合法。
    // 不走 make_shared,否则 allocator_traits::construct 会撞访问控制。
    return std::shared_ptr<Memento>(new Memento(content_, cursor_pos_));
}
```

The trade-off is one less control block merge and one more independent heap allocation (`make_shared` packs the object and the control block into a single allocation, whereas `shared_ptr(new ...)` performs two). For objects like **mementos**, which are created infrequently and have relatively short lifespans, this overhead is completely acceptable in exchange for truly robust encapsulation. If you are particularly concerned about this extra allocation, there are other approaches (such as equipping `Memento` with `std::enable_shared_from_this` plus a static factory, or simply using value semantics for `Memento` instead of `shared_ptr`), but they all complicate the code significantly and usually aren't worth it in most scenarios.

Remember this conclusion: **once you use `friend` + a private constructor to implement memento encapsulation, do not use `make_shared` to create snapshots**; just use `shared_ptr(new ...)`.

## Practice: History Stack with Undo/Redo

A single snapshot only allows us to "return to a specific moment." Real-world editors typically support **continuous undo and redo**: we undo three steps, change our minds and redo two steps, and might even insert a new edit in between that invalidates the entire "redo future." This requires a separate `History` class (the Caretaker in GoF terms) that maintains a linear sequence of snapshots and a "current pointer." Undoing moves the pointer back, and redoing moves it forward.

```cpp
class History {
public:
    void push(std::shared_ptr<TextEditor::Memento> m) {
        // 在非末尾处插入新快照时,丢弃之后的 redo 分支
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
    int cursor_ = -1;   // -1 表示空历史
};
```

There are several notable design points in this version. The first `if` block in `push` implements the logic for "discarding the redo branch." The idea is this: **once you insert a new snapshot from a position in the middle of history (after having undone a few steps), you effectively create a new branch in the timeline, and the original "redo future" should no longer exist.** This matches the behavior you are familiar with in text editors: undo two steps, type a new character, and the original redo chain disappears. Without this step, the redo stack would become inconsistent with the actual state, and the undo system would restore a "past that never existed."

We use `int` for `cursor_` instead of `size_t` to allow `-1` to serve as a natural representation for the "empty history" state. When comparing with `stack_.size()` (which is unsigned), we consistently use `static_cast<int>` for explicit conversion to avoid signed/unsigned comparison warnings. Both `undo` and `redo` follow the pattern of checking `can_undo` or `can_redo` before moving the pointer. They return `nullptr` when the history is empty or out of bounds, so the caller naturally skips the restore operation upon receiving `nullptr`, keeping the semantics consistent.

You will also notice that `History` holds `std::shared_ptr<TextEditor::Memento>`—a type that **requires the fully qualified name** because `Memento` is a nested class of `TextEditor`. This actually reveals a design trade-off: while defining the Memento as a nested type provides good encapsulation, it forces the manager (Caretaker) to **depend on the Originator** at the type level. In our simple scenario, this is fine. However, if you wanted `History` to become a generic undo framework serving various types of originators, you would need to further abstract the "opaque handle" into a type-erased `std::any` or an interface that only exposes `apply()`. That is a topic for another article, so we won't expand on it here.

Let's run through it to verify that the undo/redo logic and branch discarding work correctly:

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

Let's trace this execution path. After inserting two pieces of text, the state is `Hello, world`. Undoing twice takes us back to `Hello` and then an empty string. Redoing once advances us to `Hello`. At this point, if we insert `!!!` during a redo, `can_redo` immediately reverts to `0`—the original "redo future" is cleanly discarded. `can_undo` remains `1` because the new `Hello!!!` state is itself undoable. The behavior is exactly as expected.

## When to use the Memento pattern, and when not to

At this point, we have a solidly encapsulated implementation that supports undo and redo. But we aren't done yet—I must be honest with you: the Memento pattern is not a panacea; it has a price to pay.

**First, it consumes memory, and it does so linearly.** Every snapshot saved is a full copy of the state. For a text document of a few hundred kilobytes, undoing a few dozen times is fine. But if the object you are snapshotting is a 3D scene with millions of vertices, or a massive business object packed with cache, the history stack will exhaust memory in no time. There are a few ways to mitigate this: limit history depth (keep only the last N steps), use delta snapshots (store only the changes relative to the previous step), or abandon full snapshots entirely and use the [Command Pattern](./13-command.md) to store only reverse operations. Delta snapshots sound nice, but are error-prone to implement—you have to ensure that "any delta plus the baseline accurately restores the state," and the edge cases are enough to tear your hair out. Therefore, most implementations honestly store full snapshots and rely on depth limits as a safety net.

**Second, the cost of encapsulation isn't in writing, but in maintenance.** Whenever the originator adds an internal state field, you must remember to handle it synchronously in `create_memento` and `restore`. Miss one field, and undoing results in a "why didn't this setting revert" metaphysical bug—and these bugs only expose themselves when a user actually undoes that specific field. If test coverage is slightly lacking, it slips right through. A practical discipline is: **the set of fields in the Memento should correspond one-to-one with the set of state fields in the Originator that participate in snapshots**. When adding new fields, treat the Memento as a "mirror" of the Originator and modify them together.

**Third, it's not a choice between the Command pattern and Memento pattern; they are often used together.** The Command pattern excels at "objectifying actions, making them replayable, and packaging them into macros," but its undo relies on calculating reverse operations, which is hard to get right for complex tasks. The Memento pattern excels at "reliably returning to a specific state," but eats memory and has coarse granularity. A common combination in real-world engineering is: **use the Command pattern to organize the operation flow, and use the Memento pattern as a safety net for those complex commands where "reverse operations are hard to calculate."** Take a snapshot before the command executes, and restore on undo. This retains the replay and macro capabilities of the Command pattern while swapping absolute correctness for snapshot storage. As mentioned in our previous article on the Command pattern, once operations involve replacement, cursor movement, or multi-buffer linkage, the "pre-execution state" that `undo()` needs to save balloons rapidly. That is often when you need to leverage the Memento pattern—these two articles connect right here.

::: tip Command Pattern vs. Memento Pattern, how to choose
In a nutshell: **Simple operation, large state: use Command pattern** (store reverse operations, save memory); **Complex operation, small state: use Memento pattern** (store full snapshots, safe). When both are complex, use the Command pattern for the skeleton and the Memento pattern to backstop complex commands.
:::

## Summary

Let's review the entire evolution path:

| Stage | Approach | Why it wasn't enough |
|---|---|---|
| Public Field Memento | `struct EditorMemento { public: ... }` | Fields are all public, encapsulation is non-existent, snapshots can be tampered with arbitrarily |
| Nested Class + friend | Private constructor, private fields, `friend class Originator` | Encapsulation holds, but creation requires `shared_ptr(new ...)`, cannot use `make_shared` |
| History Stack Caretaker | `vector<shared_ptr<Memento>>` + current pointer | A single memento only allows "return to a specific moment"; continuous undo/redo requires a manager |
| Command + Memento Hybrid | Commands organize flow, complex commands take snapshots before execution | Pure snapshots eat memory; pure command reverse operations are inaccurate; they complement each other |

Keep these key conclusions in mind:

- **The core of the Memento is encapsulation, not the snapshot itself**—anyone can take a snapshot, but only a snapshot that is a "black box to the outside and transparent to the originator" deserves to be called the Memento pattern. In C++, the standard way to implement this is a nested class + private constructor + `friend class Originator`.
- **Create snapshots using `std::shared_ptr<Memento>(new ...)`, not `std::make_shared`**—private constructors trigger access control errors under the `allocator_traits::construct` path used by `make_shared`. This is the pitfall that trips people up the most in this chapter.
- **The essence of undo/redo is a linear history with a pointer**—`undo` moves the pointer back, `redo` moves it forward. Inserting a new snapshot in the middle discards the subsequent redo branch. This is the behavior model you know from all editors.
- **Mementos eat memory, and fields must be maintained as a mirror of the originator**—When the state is large and operations are simple, prioritize the Command pattern's reverse operations. When both are complex, use Commands for the skeleton and Mementos to backstop complex commands.

::: tip Complete Compilable Project
The example for this section has a complete compilable project in the repository at `code/volumn_codes/vol4/design-patterns/Memento/` (`.h` + main + `CMakeLists.txt`). Run `cmake -S . -B build && cmake --build build` to see the output shown above.
:::

## References

- [cppreference: `std::shared_ptr` and `std::make_shared`](https://en.cppreference.com/w/cpp/memory/shared_ptr/make_shared) (Allocation differences between `make_shared` and `shared_ptr(new ...)`, since C++11)
- [cppreference: Nested classes and friends](https://en.cppreference.com/w/cpp/language/nested_type) (Access control semantics for C++ nested classes and `friend`)
- GoF, *Design Patterns: Elements of Reusable Object-Oriented Software* — Original definition of the Memento pattern, proposing the "wide interface / narrow interface" dichotomy
