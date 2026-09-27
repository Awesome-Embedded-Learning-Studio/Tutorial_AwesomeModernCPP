---
chapter: 1
cpp_standard:
- 11
- 14
- 17
description: 'Make "who releases" explicit: how the ownership model drives type choices
  and function signatures'
difficulty: intermediate
order: 2
platform: host
prerequisites:
- 'Deep Dive into RAII: The Cornerstone of Resource Management'
- Move Construction and Move Assignment
reading_time_minutes: 16
related:
- 'Deep Dive into unique_ptr: The Zero-Overhead Smart Pointer with Exclusive Ownership'
- 'Deep Dive into shared_ptr: Shared Ownership and Reference Counting'
tags:
- host
- cpp-modern
- intermediate
- 内存管理
- 智能指针
title: 'Resource Ownership: Exclusive, Shared, and Borrowed'
translation:
  source: documents/vol2-modern-features/ch01-smart-pointers/02-ownership-model.md
  source_hash: 4e5270c76513e0f1c497e44bddf1b5897e3f4e4e3a813c15de8611388b048efe
  translated_at: '2026-09-27T04:46:06+00:00'
  engine: anthropic
  token_count: 5500
---
# Resource Ownership: Exclusive, Shared, and Borrowed

Let's look at a perfectly ordinary piece of code: one object gets passed around between functions, every one of them receives its pointer, and every one of them uses it:

```cpp
Session* load_session(const char* user);   // returns a raw pointer

void handle_request(Session* s) {
    audit(s);
    cache_touch(s);
    // Done using it. Delete it here? Does audit delete it? cache_touch? Or this spot?
}

void audit(Session* s)       { /* read-only, no delete */ }
void cache_touch(Session* s) { /* read-only, no delete */ }
```

You can't write the `delete` anywhere with confidence: put it inside `handle_request`, and what if some other caller still wants to use the object? Leave it out, and `Session` objects pile up on the heap. Comments can't rescue the scene either. You write "the caller is responsible for releasing", but there is more than one caller; you write "the callee is responsible for releasing", but the callee only glanced at the object in passing. When this code finishes running, who owns the release? You can't answer—and that's not your fault, because the answer appears nowhere in the code.

Ownership is the patch for exactly this hole. The language has no reserved word for it, and you won't find a class named after it in the standard library—it is a design convention: for every step of a resource's life, from birth to death, we have to state plainly who is in charge. The C++ compiler does not enforce this convention, but the type system can encode the answer into types. For an interface that "returns an object", writing `Session*` is one blunt answer and writing `std::unique_ptr<Session>` is another; the information a reader walks away with is completely different.

When we took RAII apart in the previous article, what we solved was "how to release": the destructor binds the release action, and it fires the moment scope is left. What the code above lacks isn't that—it lacks "who releases". There is also a sneakier failure mode to guard against: several modules all keep the pointer, each believing it has a stake; when everybody holds it, nobody is responsible. We'll meet that sentence again in the shared_ptr article, where an even sharper way of putting it is waiting.

## What Ownership Really Is: A Convention Never Written into the Language

Let's pin the word "ownership" down so it stops floating: **when a piece of code holds a resource, it is responsible for releasing it. We can let it take over that responsibility or hand it off, but at any moment, the responsibility must land on one party's head**.

The C++ language itself doesn't know about this convention. `int* p = new int(42);` carries no ambiguity in the compiler's eyes and holds no information about "who deletes"; the constraint lives only in your head, in comments, and in team conventions. The C++ Core Guidelines (the coding-rules collection stewarded by Bjarne Stroustrup and Herb Sutter—the resource-management entries all carry an R in their numbers, R for Resource) state the recommended practice as R.1:

```text
R.1: Manage resources automatically using resource handles and RAII
```

In plain words: manage resources automatically using resource handles and RAII.

The previous article already took RAII's mechanism apart: the destructor does the releasing, and "who is responsible" gets bound to the object's lifetime. What R.1 says is: don't let resources run around naked—wrap them in a handle that releases automatically. The ownership model sits one level above that sentence: handles come in more than one shape. Some are exclusive, some are shared, and some own nothing at all and merely borrow, and the division of labor among them has to be sorted out.

Rust takes this to the extreme: ownership is literally the language's foundation. Flip to chapter 4 of The Rust Book (Rust's official tutorial) and it gives three rules:

```text
Each value in Rust has an owner.
There can only be one owner at a time.
When the owner goes out of scope, the value will be dropped.
```

Translated one by one: every value has its owner; only one owner is on duty at any moment; and the moment the owner leaves its scope, the value is dropped. Rust's compiler enforces these—violate one and it doesn't compile. C++ gets no such treatment; we approximate the same effect with conventions plus type-level encoding. We're not starting a Rust tutorial here—we borrow the rules for a moment to show you that "ownership can be a language's starting point". C++ hands the same question back to the engineer, and the tool in hand is the type system.

The way to sort this out is to assign roles to the code that "holds a resource". In day-to-day C++, that means three holding relationships—exclusive, shared, and borrowed—plus one action that hands ownership to a new holder: transfer. Let's walk through them one by one.

## Exclusive: At Any Given Moment, Exactly One Owner

Exclusive is the simplest of the three roles: the resource has exactly one owner, and when the owner is gone, the resource is released along with it. Its landing spot in the standard library is `std::unique_ptr`:

```cpp
auto a = std::make_unique<Widget>(7);
// auto b = a;          // won't compile: unique_ptr forbids copying
auto b = std::move(a);  // ownership changes hands: b takes over, a is left empty-handed
```

`std::make_unique<Widget>(7)` may be your first encounter with it; what it does is not complicated: it builds a `Widget(7)` on the heap for you and hands the result to the returned `unique_ptr` to manage. It takes over the `new` so completely that you never touch a raw pointer — for this article we'll simply use it as the "created already managed" spelling, and leave the details for the next article.

Put the two properties together—non-copyable, movable—and that is exclusive ownership's entire manifesto. Why ban copying? Copy an owning handle and you have two "masters", each believing the release is its own business; the ending is the same memory deleted twice. When we hand-wrote `FileHandle` in article 01, we `= delete`d the copy constructor to ward off exactly this accident, and we'll give it a dedicated look later.

As for the movable half, reread it with the move constructor from the last chapter in mind. You remember what a move constructor does: it carries off the source object's pointer and leaves the source empty. Translated into the language of ownership, it is one sentence: ownership moved from source to target. After `std::move(a)` executes, `a` no longer owns anything; it is a valid empty shell—you can safely destruct it, and you can assign it a new value too. Last chapter we practiced how to write a move constructor; this article upgrades that into a question: **once this line has executed, who is responsible for releasing?** When you can answer without pausing to think, move semantics is truly in your hands.

## Shared: The Last One Out Handles the Release

Some resources genuinely have more than one owner: you have surely seen a config read by three subsystems, or a connection used by two tasks. We can't let any single party unilaterally decide its death, so we set a common rule instead: everyone holds together, and the last one to let go handles the release. This role is called shared, its landing spot is `std::shared_ptr`, and it uses reference counting to tally how many holders remain—when the count hits zero, the object is released.

Shared is not free. Every `shared_ptr` you copy and every one you destroy pays one atomic-counting operation, and behind it all a control block is being fed on the heap. The full anatomy of those two costs is left for the shared_ptr article; here we only need to carry one impression forward: sharing has a price, and it is worth paying only under genuine need. What counts as genuine need? Let's flip the question: is your reason for reaching for `shared_ptr` merely "finally, I don't have to think about ownership"? If that's really the thought, don't write it. Genuine need runs the other way: it is what you commit to only after thinking ownership through and confirming there is no way around it—multiple modules must decide **independently** "I'm still using it", and none of us can say for certain whether the others still are. Borrowing can't serve that scenario, because borrowing presupposes that the object is deterministically alive in someone else's hands.

## Borrowed: Use It, but Don't Own the Release

The third role gets the most screen time: we merely use the resource for a moment, and whose business the release is stops being our concern. The role's name is borrowed (a term the Rust community popularized; C++ folks use it too). You write its carriers every day:

```cpp
void print(const Book& b);          // reference: never null, caller guarantees the object exists
Book* find(const std::string& k);   // pointer: nullable, returns nullptr when not found
```

Let's put the Core Guidelines' own words for the two carriers side by side:

```text
R.3: A raw pointer (a T*) is non-owning
R.4: A raw reference (a T&) is non-owning
```

Translated: a raw pointer and a raw reference both own nothing.

Note the wording of both rules: they don't say raw pointers are bad; they say a raw pointer **expresses no ownership**. When you write `T*` in a signature, the reader should understand "I'm merely borrowing this". Writing `T&` carries the same story, plus one extra clause: "guaranteed non-null". How to choose between them? There is really a single criterion: nullability. When we want to express "might not exist" (a failed lookup, an optional config), use `T*` and remember to null-check it inside the function. For every other occasion, use `T&` and write the non-null guarantee into the type.

The borrow family has two more specialized members: `string_view` and `span`, which turn "borrowing" into proper view types. What `string_view` looks like on the inside waits until the views chapter; the dedicated `span` article comes even later, in the containers part of the standard-library volume. For now, getting the names on the peg is enough.

> We should also meet `owner<T>` from the GSL (Guidelines Support Library, the support library that goes with the Core Guidelines, maintained by Microsoft on GitHub). Its definition is a single line:
>
> ```cpp
> template <typename T> using owner = T;
> ```
>
> What we're looking at is a pure type alias: zero extra runtime overhead, not one byte of burden. It is written for tools to read: tag a line with `owner<int*> p = new int(42);` and the `cppcoreguidelines-owning-memory` check in clang-tidy (a static-analysis tool) has something to work with. What it catches are type-level violations: a `new` result landing in a non-owner raw pointer, a `delete` of a non-owner, the result of an owner-returning function handed to a non-owning variable—whichever we commit, it is willing to call us out by name. The boundary must also be stated clearly: it only trusts declared types; it does not track data flow. Leaks, use-after-release, and other ills that only show up along the execution path are beyond its reach. In the world of raw pointers, this is the minimal clue we can leave for static analysis.

## Transfer: A Move Is Ownership Changing Hands

With all three holding relationships covered, the remaining action is called transfer. You already know it—it is the move we're all familiar with. Last chapter we wrote a move constructor line by line; the two steps—carry the pointer off, empty the source—should still be fresh. Now we give that maneuver its formal name, and the name is **ownership changing hands**: the source object flips from "owning" to "not owning", the target flips the other way, and at no instant does a second owner squeeze in.

The moved-from object's state is "valid but unspecified"—the exact phrasing the last chapter gave. Restated in ownership terms: it no longer owns any resource, but you can safely destruct it, and you can assign it a new value, at which point ownership is back in its hands. The one thing not to do is read its value.

Where transfers lurk in code—you can hunt for them from now on—all three spots sit on owning handles: an explicit `std::move` is one; passing an owning handle by value is another, where a `unique_ptr` handed to a by-value parameter moves ownership into the function's hands; and a `unique_ptr` returned from a function is the third (RVO, the return value optimization, got a dedicated article last chapter). Whenever your eyes scan across such a line, let "ownership just changed hands" pass through your mind, and the new owner of the release duty reads itself out.

## Signatures Are Ownership Documentation

Write the convention in a comment and the comment rots. Write the convention into a function signature and the signature can't lie—change it, and every call site has to move with it; the compiler will name them one by one for you. So whatever ownership information a signature can carry deserves our serious attention. Among the Core Guidelines' R series there is a batch of entries devoted to how parameters should be passed; let's walk through the most used ones.

Let's start with the takes-over category. The Core Guidelines call it a **sink**—literally "the receiving end": stuff pours in, and it stays on the function's side. A whole class of functions are born for this: pushing a task into a queue, handing a connection to a manager—that is this work. Here is R.32's original text:

```text
R.32: Take a `unique_ptr<widget>` parameter to express that a function assumes ownership of a `widget`
```

Translated: taking a `unique_ptr<widget>` parameter by value says "this function assumes ownership of the widget". When it comes to actual code, we take the parameter by value:

```cpp
void enqueue(std::unique_ptr<Task> t) {   // sink: the queue takes over; the task's fate is the queue's from here on
    queue_.push_back(std::move(t));       // then handed on to the container
}

enqueue(std::make_unique<Task>(42));      // built and handed straight over, never touching the ground
```

As the caller, when you see a by-value `unique_ptr` parameter, your duty is crystal clear: move it in, then forget about it.

Another class of functions has a trickier job: it plays neither sink nor borrow—it wants to **change what the caller's smart pointer points to**. Reloading a configuration, re-establishing a connection: that's this class. It's called a **reseat**—literally, "seat it again"—and the work done is swapping out the object the pointer aims at. On to R.33's original text:

```text
R.33: Take a `unique_ptr<widget>&` parameter to express that a function reseats the widget
```

Translated: taking a `unique_ptr<widget>&` parameter declares "this function will swap out the widget the pointer aims at".

```cpp
void reload(std::unique_ptr<Config>& cfg);   // reseat: cfg still belongs to the caller, it just gets a different object
// The function body is roughly: cfg = std::make_unique<Config>(...);
```

Look closely at the difference here: `unique_ptr<Widget>&` is a reference to the smart pointer itself. Ownership stays clutched in the caller's hand, the object still sits safely with the caller, and the function merely swaps out what it points to. sink and reseat look like near-twins as names, yet a whole ownership model lies between their semantics—and all the signature differs by is a single `&`.

The just-letting-us-use-it class is called **borrow**, and the overwhelming majority of functions belong to it. Parameters are `const T&` or `T*`, with the same nullability criterion we laid down in the borrowing section. The duty read out of the signature is equally clear: the function touches no ownership and carries nothing away when it leaves.

When a function hands something back, it is likewise taking a stance, and return values come in two stories. A factory returning `unique_ptr<T>` is **handing the object over with ownership attached**—returning a newly created object defaults to this route. Returning `T*` or `T&` is a **loan**: the caller receives only a license to use, and you must never delete it. Think back to `find`'s return value and it's obvious: the object remains under the container's jurisdiction; if the caller's hand slips and deletes it, a dangling pointer is left lying in the container.

Smart pointers also carry a pair of operations that are easy to misread, so let's learn them side by side: `p.get()` is a loan—it passes the internal raw pointer around for others to look at, and ownership doesn't budge. `p.release()` is abdication: the smart pointer lets go, hands you back the raw pointer, and the whole weight of releasing lands on you:

```cpp
auto p = std::make_unique<Widget>(7);

Widget* peek = p.get();      // loan: p still owns; nobody should delete peek
Widget* raw  = p.release();  // abdication: p goes empty; releasing raw is the caller's job
delete raw;                  // after abdicating, the release is on you
```

Two names sitting that close together, doing completely different work—give them an extra look before you use them, and don't cross-wire them. The full boundaries of `reset()` are likewise left for the unique_ptr article.

Reading signatures alone keeps us at arm's length from the truth. So next we print every construction and destruction and actually run it, to see in whose hands the object finally breathes its last. One snippet per signature: a read-only borrow, a by-value sink, and a factory's move-out with the return value:

```cpp
// GCC 16.2.1, -O2 -std=c++17
#include <iostream>
#include <memory>
#include <utility>

struct Task {
    explicit Task(int id) : id_(id) {
        std::cout << "Task(" << id_ << ") 构造\n";
    }
    ~Task() {
        std::cout << "~Task(" << id_ << ") 析构\n";
    }
    int id() const { return id_; }
private:
    int id_;
};

// borrow: read-only, touches no ownership
void report(const Task& t) {
    std::cout << "report 看到了 Task " << t.id() << "\n";
}

// sink: takes by value, the function takes over
void finish(std::unique_ptr<Task> t) {
    std::cout << "finish 接手了 Task " << t->id() << "\n";
}   // t destructs here -- the object dies inside the function

// factory: once built, handed out with ownership attached (move-out)
std::unique_ptr<Task> make_task(int id) {
    return std::make_unique<Task>(id);
}

int main() {
    std::cout << "--- 场景一：borrow ---\n";
    {
        auto t = make_task(1);
        report(*t);              // borrow: return it once done
    }                            // t leaves scope, unique_ptr destructs the Task

    std::cout << "--- 场景二：sink ---\n";
    {
        auto t = make_task(2);
        finish(std::move(t));    // passed by value, ownership changes hands into the function
    }                            // t is already empty-handed; nothing happens here

    std::cout << "--- main 收尾 ---\n";
    return 0;
}
```

The demo program is right below—click "Try It Out" and it runs on the spot:

<OnlineCompilerDemo
  title="Hands-On: Tracing Ownership Handoffs End to End"
  source-path="code/examples/vol2/52_ownership_trace.cpp"
  description="Every construction and destruction is printed. Watch two spots: the borrow's destruction lands at the end of main's scope; the sink's destruction lands inside finish's body — whose hands the object dies in was written into the signature long ago."
  run-options="-O2 -std=c++17"
  allow-run
/>

With the output in hand, let's count through it: in scenario one, Task 1's destruction follows `report` and only shows up when the scope wraps up—borrowing never touched ownership, and the release stayed right where it was. In scenario two, Task 2's destruction appears inside `finish`; it breathes its last right after "finish 接手了" prints, and by the time control returns to main, nothing is left. `make_task` plays the third signature: the object went out with the return value and landed in the caller's hands. Same class, same factory—yet match the three signatures back, line by line, against where the object's death lands, and not a single line is out of place.

## Three Accidents: The Price of a Broken Model

With the roles sorted out, let's look from the other direction: each role misused has its own characteristic way of crashing. At scenes like these the compiler mostly can't help—it was never asked to enforce this convention, and whatever it can't block falls through to runtime. Let's walk through three accidents together.

### Accident One: The Borrow Outlives the Object and Dangles

Borrowing presupposes a live object—we all accept that. But once that premise breaks, the lent-out pointer becomes a dangling pointer, and a lent-out reference dangles just the same: the name is still there, but what it pointed at is gone. The classic way to write this bug **is returning the address of a local variable**:

```cpp
// GCC 16.2.1, -O2 -std=c++17
#include <iostream>

const int* find_answer() {
    int local = 42;    // a local, living in this call's stack frame (the chunk of stack memory the call occupies)
    return &local;     // once the function returns, the frame -- and local with it -- is invalidated
}

int main() {
    const int* p = find_answer();
    std::cout << *p << "\n";   // undefined behavior: reading invalidated stack memory
    return 0;
}
```

GCC, at least, doesn't stay silent about this pattern: at compile time it warns you that the address of a local variable is returned. As for what the run prints—42, garbage, or a straight crash—nothing was promised. We don't guess; the actual run will give us the answer. Save the code above as `dangling.cpp`, and let's compile it and run it once:

```text
$ g++ -O2 -std=c++17 dangling.cpp
dangling.cpp: In function ‘const int* find_answer()’:
dangling.cpp:5:12: warning: address of local variable ‘local’ returned [-Wreturn-local-addr]
$ ./a.out
Segmentation fault
```

Before you lend out a `T&` or `T*`, it's worth stopping to ask one question: for as long as the other side uses it, is the original object **guaranteed** alive? "Probably alive" doesn't count; what we want is "guaranteed alive". An object held by the caller, or managed by an RAII handle further up—those count. If you can't answer, don't lend it; find another way to hand it over.

### Accident Two: Copying an Owning Handle, a Double Free

The protagonist of our second accident is the one among us most eager to save trouble. He creates one raw pointer, then turns around and feeds it to two `unique_ptr`s:

```cpp
Widget* raw = new Widget(7);

std::unique_ptr<Widget> a(raw);   // a believes it is responsible for releasing
std::unique_ptr<Widget> b(raw);   // b believes it too
// Leaving scope: a's destructor deletes once, b's destructor deletes again -- the same memory, deleted twice
```

Look at the scene clearly: the two `unique_ptr`s don't know of each other's existence, each faithfully carries out its release duty, and together they add up to a double free—that is undefined behavior in person. On mainstream allocators like glibc, the common ending is the allocator detecting the duplicate reclaim and terminating the program outright. On a worse day, the second `delete` corrupts memory someone else is actively using, and the accident scene can drift very far away.

We didn't put this example in the online demo: a double free is undefined behavior to begin with, and however it crashes on the demo platform tells you nothing worth referencing. You only need to walk away with one sentence—`unique_ptr`'s copy ban exists to prevent exactly this, but the approach above bypasses the ban, because when the constructor receives a raw pointer it has no way of knowing whether some other handle already manages that pointer. Article 01's `FileHandle` `= delete`d copying outright, hand-writing the same line of defense. Ownership handovers should go through the front door: move, pass-by-value, return values. Don't take the side door of "constructing pairs from a raw pointer".

### Accident Three: An Ownership Vacuum Where Nobody Deletes

The third accident is the quietest; one look at the scene explains everything: no crash, no warning—the object just lies silently on the heap until the moment the process exits. The two parties handed things over via raw pointer, each assuming deletion was the other's business:

```cpp
// GCC 16.2.1, -O2 -std=c++17
#include <iostream>
#include <string>

struct Session {
    explicit Session(std::string tag) : tag_(std::move(tag)) {
        std::cout << "Session(" << tag_ << ") 构造\n";
    }
    ~Session() {
        std::cout << "~Session(" << tag_ << ") 析构\n";
    }
    const std::string& tag() const { return tag_; }
private:
    std::string tag_;
};

// Party A: creates the object, hands it out as a raw pointer, believing it's merely relaying it
Session* build_session() {
    return new Session("无主");
}

// Party B: takes the pointer, uses it briefly, figures "I didn't new it, it's not mine to delete"
void use_session(Session* s) {
    std::cout << "用了一下 Session " << s->tag() << "\n";
}

int main() {
    Session* s = build_session();   // the handover happens, but neither side declares responsibility
    use_session(s);
    std::cout << "main 结束\n";
    return 0;                       // still no delete by this point
}
```

Accident three's scene makes no fuss, and we really did run it—click "Try It Out" to see for yourself:

<OnlineCompilerDemo
  title="Hands-On: The Leak Scene of an Ownership Vacuum"
  source-path="code/examples/vol2/53_ownership_leak.cpp"
  description="Count the printed lines: construction, use, and the wrap-up are all there — only the destruction is absent. Nobody deletes, the object is never released, and the program quietly runs to completion anyway."
  run-options="-O2 -std=c++17"
  allow-run
/>

```text
Session(无主) 构造
用了一下 Session 无主
main 结束
```

Lay the output out before your eyes and count: one line for construction, one for use, one for the wrap-up—only the destruction line is missing. Where is the destruction line? It's absent. The `Session` still lies on the same heap, yet the log has already closed. That is the leak's entire crime scene, quiet to the point of being creepy.

Put the vacuum and the double free side by side: they're a genuine pair of antonyms, yet the root cause is identical—"owning or not" was never written into the type. Spell out exclusivity, and the compiler both blocks your copies and releases for you. Write the handover with raw pointers, and neither end has anything to fall back on; everything rides on the two parties' unspoken understanding—and unspoken understanding is not something a brand-new colleague brings through the door.

## Reading Code with One Question: After This Line, Who Releases

All three holding relationships are in place, the transfer action has its slot too, and now we compress them into an executable workflow. Whenever you face a new object or a new handover, run through the following order:

1. Start from exclusive by default: manage it with a `unique_ptr` (or a value type with RAII built in); when it must change hands, use move—that is the transfer action.
2. Only bring in `shared_ptr` when sharing is genuinely needed. What counts as genuine? The criterion from the shared section: multiple modules must decide "I'm still using it" independently.
3. Handle every other access as a borrow: `const T&` or `T*`, picked by nullability.

In my own projects, the feel of it is this: the exclusive-to-shared ratio hovers around nine to one year-round—nine out of ten objects have exactly one owner from cradle to grave. You don't need to memorize the number; the direction is what matters—start from exclusive, push shared back into the exceptional slot, and make it prove it's really necessary. When we get to the shared_ptr article, we'll run this judgment through concrete scenarios once more.

Is this workflow actually any good? We have to practice on real code. With the bookshelf example below, let's interrogate it line by line:

```cpp
class Shelf {
    std::vector<std::unique_ptr<Book>> books_;   // member: the collection; Shelf owns it exclusively
public:
    void add(std::unique_ptr<Book> b) {          // parameter by value: a sink, taking over
        books_.push_back(std::move(b));          // into the collection: ownership enters the container
    }
    const Book* find(const std::string& title) const {
        for (const auto& b : books_) {
            if (b->title() == title) {
                return b.get();                  // loan: the container still owns exclusively
            }
        }
        return nullptr;                          // not found: nullable, hence the pointer return
    }
};
```

Start questioning from the `books_` line: who manages each `Book`'s release? Shelf does; the vehicle is the `unique_ptr` in the container, and when Shelf destructs, they get released one book at a time. What about `add`'s parameter `b`? Ownership changes hands to the function, then `push_back` changes it again into the container; once that line finishes, the caller owes this book nothing further. And `find`'s return value? Its type is `const Book*`, which by R.3's standard plays the non-owning role—you may use it, but you may not delete it; the object belongs to the `unique_ptr` in `books_`. Three questions asked, and every line of this code's release responsibility has a place to land.

From now on, whenever you read someone else's code or review a colleague's changes, this question is the cheapest probe at hand: whichever line you can't answer on is the suspicious scene. Once the unanswerable lines pile up, the codebase is waiting for an ownership convention written into types—comments alone won't bring it back.

At the end of the previous article we left a thread hanging: once the ownership model stands, `unique_ptr` and `shared_ptr` become two landings of one and the same idea, no longer two scattered tools. Now the model stands. The next article takes up the first of them—`unique_ptr`, exclusive ownership's zero-overhead landing: how it builds "non-copyable, movable" into the type, how it meshes with containers and move semantics, and where the boundaries of manual operations like `release` and `reset` lie—all of that is waiting for us on the other side.

## References

- [C++ Core Guidelines: Resource Management (the R series)](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#S-resource)
- [The Rust Book, ch 4.1: What is Ownership?](https://doc.rust-lang.org/book/ch04-01-what-is-ownership.html)
- [GSL: Guidelines Support Library (where the owner alias comes from)](https://github.com/microsoft/GSL)
