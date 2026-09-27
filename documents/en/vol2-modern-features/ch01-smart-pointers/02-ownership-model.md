---
chapter: 1
cpp_standard:
- 11
- 14
- 17
description: 'Getting "who is responsible for releasing" straight—how the ownership model drives type choices and function signatures'
difficulty: intermediate
order: 2
platform: host
prerequisites:
- 'Chapter 1: Deep Dive into RAII'
- 'Chapter 0: Move Construction and Move Assignment'
reading_time_minutes: 16
related:
- 'Understanding unique_ptr: Zero-Overhead Smart Pointer with Exclusive Ownership'
- 'Understanding shared_ptr: Shared Ownership and Reference Counting'
tags:
- host
- cpp-modern
- intermediate
- 内存管理
- 智能指针
title: 'Resource Ownership: Exclusive, Shared, and Non-Owning'
translation:
  source: documents/vol2-modern-features/ch01-smart-pointers/02-ownership-model.md
  source_hash: 750f01c059abc0179c71af4bb253cabe7222f2f7ce09c88f1f54f0c677f8607f
  translated_at: '2026-09-27T02:03:54.833482+00:00'
  engine: anthropic
  token_count: 4630
---
# Resource Ownership: Exclusive, Shared, and Non-Owning

Let's look at a stretch of perfectly ordinary code: an object gets passed around between functions, everyone ends up with its pointer, everyone uses it:

```cpp
Session* load_session(const char* user);   // 返回一个裸指针

void handle_request(Session* s) {
    audit(s);
    cache_touch(s);
    // 用完了。删吗？audit 删？cache_touch 删？还是这里删？
}

void audit(Session* s)       { /* 只读，不删 */ }
void cache_touch(Session* s) { /* 只读，不删 */ }
```

Whichever line you write the `delete` on, it doesn't sit right. Put it in `handle_request`, and what if some other caller still wants the object? Leave it out, and `Session`s keep piling up on the heap. Comments can't save the scene either. You write "the caller releases it," and it turns out there is more than one caller; you write "the callee releases it," and it turns out the callee only stopped by for a glance. So when this code finishes running, whose job is the release? You can't answer—and honestly, that's not on you: nowhere in this entire stretch of code can the answer be found.

Ownership is the patch for exactly this hole. The language has no reserved word for it, and the standard library won't turn up a class named after it—it is a design-level agreement: for every step of a resource's life, birth to death, who is in charge must be spelled out. The C++ compiler doesn't enforce the agreement, but the type system can encode the answer into types. For the same "return an object" interface, `Session*` is one blunt answer, `std::unique_ptr<Session>` is another, and the information the reader walks away with is completely different.

When we took RAII apart in the previous post, what got settled was how to release: the destructor binds the release action, and it runs once control leaves the scope. That is not what the code above is missing—it is missing the who. There is also a sneakier ailment to keep an eye on: several modules all keep the pointer, each believing it holds a stake; when everybody holds it, that amounts to nobody being responsible. We will meet that sentence once more in the shared_ptr post, where a harsher version of it is waiting.

## What Ownership Really Is: A Convention Not Written into the Language

Let's pin the word "ownership" down so it stops hanging in the air: **a piece of code that holds a resource answers for releasing that resource. The responsibility can be taken over or handed off, but at any given moment it must sit with exactly one party.**

The C++ language itself doesn't recognize this agreement. `int* p = new int(42);` is free of ambiguity in the compiler's eyes and carries no information about who deletes; the constraint can only live in your head, in comments, in team rules. The C++ Core Guidelines (a coding-rules collection maintained under the lead of Bjarne Stroustrup and Herb Sutter; the resource-management entries all carry an R in their numbers, R for Resource) write the recommended practice down as R.1:

```text
R.1: Manage resources automatically using resource handles and RAII
```

In other words: manage resources automatically, using resource handles and RAII.

We already took RAII's mechanism apart in the previous post: the destructor handles the release, and "who is responsible" gets bound to the object's lifetime. What R.1 says is: don't let resources run around naked—wrap them in a handle that releases on its own. The ownership model sits one level above that sentence: the handle does not come in a single shape. There are exclusive ones, shared ones, and ones that own nothing at all and merely borrow for a while—the division of labor among them has to be sorted out.

Rust takes the matter to the extreme: ownership is outright the foundation of the language. Open The Rust Book (the official Rust tutorial) at chapter 4, and it gives three rules:

```text
Each value in Rust has an owner.
There can only be one owner at a time.
When the owner goes out of scope, the value will be dropped.
```

One by one: every value has an owner; only one owner is on duty at any moment; the moment the owner leaves its scope, the value is dropped. Rust's compiler enforces all three—break one and the code refuses to compile. C++ gets no such treatment; we close in on the same effect with guidelines plus type encoding. This is not a Rust tutorial. We borrow the rules for a moment because we want you to see that ownership can be taken as the starting point of an entire language. C++ hands the same question back to the engineer, and the tool in hand is the type system.

The way to sort it out is to assign roles to the code that "holds a resource." Landed in everyday C++, that means three holding relationships—exclusive, shared, borrow—plus one action that hands ownership over: transfer. We'll take them one by one.

## Exclusive: At Any Given Moment, Exactly One Party Releases

Exclusive is the simplest of the three roles: the resource has exactly one owner, and when the owner goes, the resource is released with it. Its landing spot in the standard library is `std::unique_ptr`:

```cpp
auto a = std::make_unique<Widget>(7);
// auto b = a;          // 编译不过：unique_ptr 禁止拷贝
auto b = std::move(a);  // 所有权换手：b 接管，a 从此两手空空
```

Put the two properties together—copying forbidden, moving allowed—and that is the entire manifesto of exclusive ownership. Why forbid copying? Copy an owning handle, and now there are two "owners," each believing the release is its own business, and the ending is the same memory deleted twice. When we hand-wrote `FileWrapper` in the previous post, we `= delete`d the copy constructor to head off exactly this accident; we will come back for a dedicated look at it later.

As for the movable half, reread it with the move constructor you learned in the previous chapter. What a move constructor does, you remember: carry off the source object's pointer, set the source to null. Translated into the language of ownership, it is one sentence: ownership moved from source to target. Once `std::move(a)` has executed, `a` no longer owns anything. It is left a legal empty shell—you can safely destruct it, and assigning it a new value is fine too. In the previous chapter we practiced how to write a move constructor; this post upgrades that into a question: **when this line has finished executing, who answers for the release?** The day you can answer without stopping to think, move semantics is genuinely in your hands.

## Shared: The Last One Out Handles the Release

Some resources really do have more than one owner: one configuration read by three subsystems, one connection used by two tasks—you have seen these scenes. We can't let any single party decide its death unilaterally, so instead we set a common rule: everyone holds it together, and the last one to let go handles the release. The role's name is shared; its landing spot is `std::shared_ptr`, which counts the remaining holders with a reference count, and when the count reaches zero, the object is released.

Shared doesn't come free. Every `shared_ptr` you copy and every one you destroy pays for one atomic count operation, and somewhere behind it all, a control block is being kept alive on the heap. The full dissection of those two costs is left to the shared_ptr post; here we only need to carry one impression forward: sharing has a price, worth paying only when the need is real. What counts as real need? Ask from the other direction: is the reason you reached for `shared_ptr` merely "finally, no more thinking about ownership"? If that's the thought, don't write it. Real need runs the other way: you have thought ownership through, confirmed there is no way around it, and only then put the pen down—multiple modules must **independently** decide "I'm still using it," and none of us can say for certain whether the others still are. Borrowing can't serve scenes like that, because borrowing presupposes the object is definitively alive in someone else's hands.

## Borrow: You Use It, but the Release Isn't Your Job

The most frequent player is the third role: we only use the resource for a moment, and whose job the management is stops being our concern. The role's name is borrow (a term the Rust community made popular; the C++ side goes by the same name). You write its carriers every single day:

```cpp
void print(const Book& b);          // 引用：不可空，调用方保证对象在
Book* find(const std::string& k);   // 指针：可空，没找到就给 nullptr
```

Put the Core Guidelines' original wording for the two carriers side by side:

```text
R.3: A raw pointer (a T*) is non-owning
R.4: A raw reference (a T&) is non-owning
```

Read together: a raw pointer and a raw reference own nothing.

Notice the wording of the two rules: they don't call the raw pointer bad; they say it **does not express ownership**. When you write `T*` in a signature, the reader should take it as "just borrowing this for a while." `T&` works the same way, with one extra clause attached: "guaranteed not null." How to choose between the two? The criterion comes down to a single one: nullability. When we want to say "might not be there" (a failed lookup, an optional configuration), you use `T*`, and remember to null-check inside the function. Every other occasion takes `T&`, writing the non-null guarantee into the type.

The borrow family has two more specialized members: `string_view` and `span`, which turn "borrowing" into proper view types. What `string_view` looks like inside waits for the views chapter to take it apart. The dedicated `span` article comes even later, in the containers part of the standard-library volume. Here, hanging the names on the wall is enough.

> We should also meet `owner<T>` from the GSL (Guidelines Support Library—the support library that goes with the Core Guidelines, maintained by Microsoft on GitHub). Its definition is a single line:
>
> ```cpp
> template <typename T> using owner = T;
> ```
>
> What we're looking at is a pure type alias: zero extra cost at runtime, not so much as one extra byte of burden. It is written for tools to read: mark `owner<int*> p = new int(42);`, and the `cppcoreguidelines-owning-memory` check in clang-tidy (a static-analysis tool) has something to stand on. What it catches are type-level violations: a `new` result landing in a non-owner raw pointer, a `delete` on a non-owner, the result of an owner-returning function handed to a non-owning variable—whichever one of these we commit, it is willing to call us out by name. The boundary has to be stated clearly too: it only recognizes declared types; it does not chase data flow. Leaks, use-after-free—faults you can only see by following the execution path—are beyond its reach. In the world of raw pointers, this is the minimal clue we can leave for static analysis.

## Transfer: A Move Is Ownership Changing Hands

All three holding relationships are covered; the remaining action is called transfer. You already know it—it is the move we are all familiar with. In the previous chapter we wrote the move constructor line by line; the two steps—carry the pointer off, null out the source—you remember. Now that set of moves gets an official name, and the name is **ownership changing hands**: the source object switches from "owning" to "not owning," the target switches the other way, and in between there is not an instant where a second owner squeezes in.

The moved-from object's state is "valid but unspecified"—the exact wording from the previous chapter. Said again in ownership terms: it no longer owns any resource, but you can safely destruct it, and you can assign it a new value, at which point ownership is back in its hands. The one thing not to do is read its value.

Where transfers lurk in code—you can check against this list from now on—all three spots sit on handles that own: an explicit `std::move` is one; passing an owning handle by value is one—a `unique_ptr` handed to a by-value parameter switches ownership into the function's hands; a `unique_ptr` returned from a function is one more (RVO is return-value optimization, and we gave its details a dedicated article in the previous chapter). When your eyes sweep across these lines, run one sentence through your head—"ownership just changed hands"—and the new owner of the release duty reads itself out.

## Signatures Are Ownership Documentation

Write the agreement into a comment, and the comment goes stale. Write the agreement into a function signature, and the signature can't lie—you change the signature, every call site has to move with it, and the compiler calls them out for you, one spot at a time. So the ownership information a signature can carry deserves to be taken seriously. Among the Core Guidelines' R series, a batch of entries is devoted to how parameters should be passed; let's pick the most commonly used and walk through them.

Start with the taking-over kind. The Core Guidelines' name for it is **sink**, and the word paints its own picture: things get poured in, and they stay on the function's side. One class of functions is born doing this work: you stuff a task into a queue, you hand a connection to a manager—that is the job. Here is R.32's original text:

```text
R.32: Take a `unique_ptr<widget>` parameter to express that a function assumes ownership of a `widget`
```

Read plainly: taking a `unique_ptr<widget>` parameter by value says "this function assumes ownership of the widget." When it comes down to actual code, we let the parameter take by value:

```cpp
void enqueue(std::unique_ptr<Task> t) {   // sink：队列接手，任务的死活从此归队列
    queue_.push_back(std::move(t));       // 再转交给容器
}

enqueue(std::make_unique<Task>(42));      // 造出来直接交出去，中间不落地
```

As the caller, seeing a by-value `unique_ptr` parameter, your obligations are fully clear: move it in, then forget about it.

Another class of functions has a trickier job: it serves as neither sink nor borrow—it wants to **change what the caller's smart pointer points at**. Reloading a configuration, re-establishing a connection: that is the class of work being described. The name is **reseat**, literally "sit it back down," and the work is swapping out the object the pointer points at. Next, R.33's original text:

```text
R.33: Take a `unique_ptr<widget>&` parameter to express that a function reseats the widget
```

In plain words: taking a `unique_ptr<widget>&` parameter declares "this function reseats the widget."

```cpp
void reload(std::unique_ptr<Config>& cfg);   // reseat：cfg 还归调用方，只是会被换个对象
// 函数体内大致是：cfg = std::make_unique<Config>(...);
```

Look carefully at the difference here: `unique_ptr<Widget>&` is a reference to the smart pointer itself. Ownership stays clutched in the caller's hands, the object stays firmly in the caller's keeping, and the function only swaps out the thing it points to. sink and reseat look like neighboring names, but what lies between their semantics is an entire ownership model—and the difference on the signature is one `&`.

The just-using-it kind is called **borrow**, and the overwhelming majority of functions belong to it. The parameters are `const T&` or `T*`, and the criterion is still the nullability given in the borrow section. The obligation read out of the signature is equally clear: the function doesn't touch ownership, and it walks away with nothing.

When a function hands something back, it is taking a stance too, and return values come in two versions. A factory returning `unique_ptr<T>` hands the made object over **ownership included**—returning a new object defaults to that route. Returning `T*` or `T&` is a **loan**: what the caller receives is a license to use, and you must never, ever `delete` it. Think back to `find`'s return value and it clicks: the object is still under the container's jurisdiction; if the caller's hand slips and deletes it, a dangling pointer is left lying in the container.

There is also a pair of operations on smart pointers that are easy to misread—let's meet them together. `p.get()` is a loan: it passes the internal raw pointer out for others to look at, and ownership doesn't move an inch. `p.release()` is abandonment: the smart pointer lets go, the raw pointer comes back to you, and the entire duty of releasing is pressed onto your shoulders:

```cpp
auto p = std::make_unique<Widget>(7);

Widget* peek = p.get();      // 借出：p 仍然拥有，没人该删 peek
Widget* raw  = p.release();  // 弃管：p 变空，raw 的释放归调用方
delete raw;                  // 弃管之后，释放只能自己来
```

Two names sitting that close, doing completely different work—one extra glance before using them, so you don't mix them up. Together with the full boundaries of `reset()`, that is all left for the unique_ptr post.

Staring at signatures on paper still keeps a page between us and the machine. So next we print every construction and every destruction, actually run the thing, and watch in whose hands the object finally draws its last breath. One snippet per signature: a read-only borrow, a sink that takes over by value, and a factory handing things over with the return value—a move-out:

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

// borrow：只读一下，不碰所有权
void report(const Task& t) {
    std::cout << "report 看到了 Task " << t.id() << "\n";
}

// sink：按值收，函数接手
void finish(std::unique_ptr<Task> t) {
    std::cout << "finish 接手了 Task " << t->id() << "\n";
}   // t 在这里析构——对象死在函数里

// 工厂：造好之后，连所有权一起交出（move-out）
std::unique_ptr<Task> make_task(int id) {
    return std::make_unique<Task>(id);
}

int main() {
    std::cout << "--- 场景一：borrow ---\n";
    {
        auto t = make_task(1);
        report(*t);              // 借用：用完就还
    }                            // t 离开作用域，unique_ptr 析构 Task

    std::cout << "--- 场景二：sink ---\n";
    {
        auto t = make_task(2);
        finish(std::move(t));    // 按值传参，所有权换手进函数
    }                            // t 已经两手空空，这里无事发生

    std::cout << "--- main 收尾 ---\n";
    return 0;
}
```

The program is small enough to build and run yourself—compile it, run it, then check it line by line against the output we got:

```text
--- 场景一：borrow ---
Task(1) 构造
report 看到了 Task 1
~Task(1) 析构
--- 场景二：sink ---
Task(2) 构造
finish 接手了 Task 2
~Task(2) 析构
--- main 收尾 ---
```

Output in hand, let's count through it. In scenario one, Task 1's destructor trails behind `report` and only shows up when the scope wraps up: borrowing never laid a finger on ownership, and the release stayed where it was. Come scenario two, Task 2's destructor appears inside `finish`—once the takeover line prints, the object breathes its last, and when control returns to `main`, nothing is left. `make_task` plays the third signature: the object went out with the return value and landed in the caller's hands. The class is the same class, the factory the same factory—match the three snippets back one by one against where each object's death lands, and doesn't every single line sit exactly where it should?

## Three Accidents: The Price of a Broken Model

With the roles sorted out, let's flip around and look from the other side: each role, used wrongly, has its own characteristic way of flipping over. At scenes like these the compiler mostly can't help us. Nobody ever asked it to enforce this agreement, and what it can't block falls to runtime. Let's sit through three accidents together.

### Accident One: The Borrow Outlives the Object, and Dangles

Borrowing presupposes a living object—on that we all agree. But once the presupposition breaks, the lent pointer becomes a dangling pointer, and a lent reference dangles just the same: the name is still there; what it points at is gone. The most classic way to write it is returning a local variable's address:

```cpp
// GCC 16.2.1, -O2 -std=c++17
#include <iostream>

const int* find_answer() {
    int local = 42;    // 局部变量，住在这次调用的栈帧（函数调用占用的那块栈内存）里
    return &local;     // 函数一返回，栈帧连同 local 一起作废
}

int main() {
    const int* p = find_answer();
    std::cout << *p << "\n";   // 未定义行为：读一块作废的栈内存
    return 0;
}
```

GCC doesn't stay silent about this one: at compile time it warns you that the address of a local variable was returned. As for what the run prints—42, garbage, or a flat-out crash—it promises nothing. We don't guess; the experiment will hand us the answer. Save the code above as `dangling.cpp`, and let's compile once and run once:

```text
$ g++ -O2 -std=c++17 dangling.cpp
dangling.cpp: In function ‘const int* find_answer()’:
dangling.cpp:5:12: warning: address of local variable ‘local’ returned [-Wreturn-local-addr]
$ ./a.out
Segmentation fault
```

Before you lend out a `T&` or a `T*`, it is worth stopping to ask one question: for the entire time the other side is using it, is the original object **guaranteed** to be alive? "Should be alive" doesn't count; what we want is "guaranteed alive." An object held by the caller, kept by an RAII handle further out—that sort counts. If you can't answer, don't lend; hand it over some other way.

### Accident Two: An Owning Handle Gets Copied—Double Free

The protagonist of the second accident is the one among us most eager to save effort. He creates a raw pointer, turns around, and feeds it to two `unique_ptr`s:

```cpp
Widget* raw = new Widget(7);

std::unique_ptr<Widget> a(raw);   // a 认为自己负责释放
std::unique_ptr<Widget> b(raw);   // b 也认为自己负责释放
// 离开作用域：a 析构删一遍，b 析构再删一遍——同一块内存 delete 两次
```

Get this scene into focus: the two `unique_ptr`s don't know of each other's existence, each faithfully carries out its release duty, and together they amount to a double free—precisely the kind of thing undefined behavior refers to. On mainstream allocators such as glibc, the common ending is the allocator detecting the duplicate reclaim and terminating the program outright. On a worse day, the second `delete` corrupts memory someone else is actively using, and the accident scene can drift a very long way from where it started.

This is the one example we left out of the runnable set: a double free is undefined behavior to begin with, and the particular posture it crashes into on any given platform holds no reference value. You only need to take one sentence away—`unique_ptr`'s ban on copying exists to prevent exactly this, and the approach above slips around the ban, because when the constructor takes a raw pointer, it has no way of knowing whether some other handle already manages that pointer. The `FileWrapper` from the previous post `= delete`d copying outright—hand-writing the same line of defense. Ownership changes hands through the front door: move, pass-by-value, return value. Stay off the side door of pairing a raw pointer into a constructor.

### Accident Three: An Ownership Vacuum—Nobody Deletes

The third accident is the quietest—the scene tells the whole story once you've read it: no crash and no warning; the object simply lies on the heap in silence, all the way to the moment the process exits. The two parties made their handover with a raw pointer, each believing the delete was the other's business:

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

// 甲方：造出对象，裸指针交出去，觉得自己只是「转交」
Session* build_session() {
    return new Session("无主");
}

// 乙方：拿到指针用一下，觉得「不是我 new 的，不归我删」
void use_session(Session* s) {
    std::cout << "用了一下 Session " << s->tag() << "\n";
}

int main() {
    Session* s = build_session();   // 交接发生，但没有任何一方声明负责
    use_session(s);
    std::cout << "main 结束\n";
    return 0;                       // 到这里也没有 delete
}
```

The scene of accident three makes no fuss at all. We really did run it—count the printed lines:

```text
Session(无主) 构造
用了一下 Session 无主
main 结束
```

Lay the output in front of you and count: one line for construction, one line for use, one line for the wrap-up—only the destructor line is missing. Where did the destructor line go? It's absent. The `Session` is still lying on the same heap, and the log has already wrapped up. That is the entire scene of a leak, quiet to a slightly creepy degree.

Put the vacuum and the double free side by side: they are a genuine pair of antonyms, yet the cause is the same one—whether ownership exists was never written into the type. Spell exclusivity out, and the compiler will both block your copying and release for you. Write the handover with raw pointers instead, and neither end has a footing; everything rides on the two parties' tacit understanding—and tacit understanding is not something the colleague who joined last week brings along.

## What to Ask of Any Code: After This Line, Who Releases?

All three holding relationships are in, and the transfer action has found its seat; now we compress them into a procedure that can be executed. Whenever you face a new object or a new handover, run through the order below:

1. Default to exclusive: bring it under a `unique_ptr` (or a value type with RAII built in); when it must change hands, that is what move is for—the transfer action.
2. Only when sharing is genuinely needed do we bring in `shared_ptr`. What counts as genuine? The shared section gave the criterion: multiple modules must independently decide "I'm still using it."
3. All remaining access we handle as borrowing: `const T&` or `T*`, picked by nullability.

The gut feel from my own projects: the ratio of exclusive to shared hovers around nine to one year-round—ninety percent of objects have exactly one owner from beginning to end. Don't memorize the number; the direction is what matters—set out from exclusive, push shared back into the exception's seat, and make it prove itself genuinely necessary. When we reach the shared_ptr post, this judgment will go through concrete scenes once more.

Does the procedure hold up? It needs practice on real code. Take the bookshelf below, and let's ask our way through it line by line:

```cpp
class Shelf {
    std::vector<std::unique_ptr<Book>> books_;   // 成员：馆藏，Shelf 独占
public:
    void add(std::unique_ptr<Book> b) {          // 参数按值：sink，接手
        books_.push_back(std::move(b));          // 入库：所有权进容器
    }
    const Book* find(const std::string& title) const {
        for (const auto& b : books_) {
            if (b->title() == title) {
                return b.get();                  // 借出：容器仍然独占
            }
        }
        return nullptr;                          // 找不到：可空，所以返回指针
    }
};
```

Start asking from the `books_` line: who manages each `Book`'s release? Shelf does, and the carrier is the `unique_ptr` inside the container—when Shelf destructs, they get released one book at a time. What about `add`'s parameter `b`? Ownership switched into the function's hands, and `push_back` switched it into the container; once this line finishes, the caller owes nothing further on this book. And `find`'s return value? Its type is `const Book*`, which by R.3's standard is a non-owning role—you may use it, but don't delete it; the object belongs to the `unique_ptr` inside `books_`. Three questions asked, and every line of this code's release responsibility has found its footing.

From here on, when you read other people's code or review a colleague's changes, this question is the cheapest probe at hand: whichever line you can't answer for, that line is the suspicious scene. Once the unanswerable lines pile up, the codebase is waiting for a set of ownership conventions written into types—comments alone won't rescue it.

At the end of the previous post we left a thread hanging: once the ownership model stands up, `unique_ptr` and `shared_ptr` become two implementations of the same idea, no longer two scattered tools. The model now stands. In the next post we look at the first of them—`unique_ptr`, the zero-overhead implementation of exclusive ownership: how it builds "no copying, movable" into the type, how it meshes with containers and move semantics, and where manual operations like `release` and `reset` draw their boundaries—all of it waiting on the other side.

## Reference Resources

- [C++ Core Guidelines: Resource Management (the R series)](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#S-resource)
- [The Rust Book, ch 4.1: What is Ownership?](https://doc.rust-lang.org/book/ch04-01-what-is-ownership.html)
- [GSL: Guidelines Support Library (where the owner alias comes from)](https://github.com/microsoft/GSL)
