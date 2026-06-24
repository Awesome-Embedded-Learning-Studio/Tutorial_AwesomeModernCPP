---
title: 'Mediator Pattern: Untangling the Web into a Star'
description: Starting with the most intuitive approach of "controls holding references
  to each other," we progressively derive the Mediator interface. We clarify star-shaped
  coupling using chat rooms and dialog boxes, and wrap up with a `std::any` event
  bus.
chapter: 11
order: 21
tags:
- host
- cpp-modern
- intermediate
- 中介者模式
difficulty: intermediate
platform: host
cpp_standard:
- 11
- 17
- 20
- 23
reading_time_minutes: 22
related:
- 单例模式:从注释约束到 Meyer's Singleton
prerequisites:
- 'Chapter 6: 类与对象'
translation:
  source: documents/vol4-advanced/vol4-generics-patterns/21-mediator.md
  source_hash: 6a8520fa246562d58f1f345f4ce615e08ada0fd2760d2a0177c7b51c44dfba15
  translated_at: '2026-06-24T01:04:52.643241+00:00'
  engine: anthropic
  token_count: 4111
---
# Mediator Pattern: Untangling the Web into a Star

## What Problem Are We Actually Solving?

Let's skip the formal definition for a moment. Consider a common scenario: you are building a book search dialog with four controls—a title input box, an author input box, a candidate list, and a confirmation button. The product requirements specify three rules: the list must filter in real-time as the user types in the title or author fields; the confirmation button is only enabled when both title and author are filled; and clicking confirm submits the currently selected item from the list.

This sounds simple enough. Instinctively, you might make these four controls aware of each other: the `Textbox` holds a `Listbox*` and a `Button*`, the `Listbox` holds two `Textbox*` pointers, and the `Button` holds a `Listbox*` and two `Textbox*` pointers. Inside each control's callback, you directly reach out and modify the others. On the first day, you feel quite productive. On the second day, product adds a fifth control, "Advanced Filter," and you realize the constructor signatures of all four existing controls need to change—because the new control must also be visible to them. A week later, the controls grow to eight, and the dependency graph has become a tangled spider web that no one can unravel.

The Mediator pattern addresses exactly this type of requirement: **it transforms a "mesh coupling" where objects directly call and hold references to each other into a "star coupling"—where all objects talk only to one mediator, who handles message routing and rule coordination**. A chat room is the classic example: users don't pass notes directly to other users; instead, they send messages to the chat room, which decides who receives them. A GUI dialog is the canonical example from the Gang of Four (GoF) book: each control simply notifies the dialog, "I have changed," and the dialog decides if other controls need to react.

However, "adding an intermediate layer" is not as simple as "just creating a new class" in C++. There are several easy traps to fall into. Many people writing a mediator for the first time will have the mediator `#include` all colleague classes, turning the mediator into a "God object" that knows everything—the mesh coupling hasn't disappeared, it's just moved inside the mediator. Others, wanting to support arbitrary payloads on an event bus, casually use `void*` or strings to erase types, completely sacrificing compile-time type safety and blowing up at runtime. Therefore, the real question we need to answer in this article is—**how do we make colleague objects depend only on an abstract mediator interface without knowing each other, while preventing the mediator from devolving into a God object and avoiding type safety loss through type erasure**?

Let's walk through this step-by-step, starting with the most intuitive approach, seeing why each step falls short, and finally deriving a standard answer using modern C++.

## Step 1: The Most Intuitive Approach—Controls Holding References to Each Other (The Anti-Pattern)

When many people first encounter "four controls need to interact," the code they instinctively write looks like this: each control stuffs pointers to the other controls inside itself, reaching out directly to modify them in callbacks.

```cpp
class Button;   // 互相前向声明,真到 .cpp 里就要互相 #include
class Textbox;
class Listbox;

class Listbox {
public:
    void bind(Button* b, Textbox* t) { button_ = b; textbox_ = t; }
    void on_selection_changed();
private:
    Button*  button_ = nullptr;
    Textbox* textbox_ = nullptr;
};

class Textbox {
public:
    void bind(Button* b, Listbox* l) { button_ = b; listbox_ = l; }
    void on_text_changed();
private:
    Button*  button_ = nullptr;
    Listbox* listbox_ = nullptr;
};

class Button {
public:
    void bind(Textbox* t, Listbox* l) { textbox_ = t; listbox_ = l; }
    void on_click();
private:
    Textbox* textbox_ = nullptr;
    Listbox* listbox_ = nullptr;
};
```

To be honest, this approach works perfectly fine in a toy scenario where "there are few controls and the rules won't change." Don't reflexively slap on a design pattern the moment you see one. The real problem arises the moment **requirements start to expand**. When you add a sixth control, the `bind()` signatures of the three existing controls all need to change because they must now see the new control. When you want to modify a linkage rule (for example, "only allow submission if the terms are checked"), you will find that this rule is scattered across `Textbox::on_text_changed`, `Listbox::on_selection_changed`, and `Button::on_click`. Changing one spot requires verifying that the other two don't break as a consequence.

Even worse are the compile-time issues. For `Textbox` to hold a `Button*`, its implementation must see the full definition of `Button`. Similarly, `Button` needs to see the full definition of `Textbox`. Forward declarations can only support pointer declarations; they cannot support member function calls to the other class's methods. Consequently, header files end up mutually `#include`-ing each other, easily spiraling into circular dependencies. Let's test in `/tmp` whether this minimal skeleton of "mutual references" can actually compile:

```sh
$ g++ -std=c++23 -O2 -Wall -Wextra -pthread /tmp/mediator_circular.cpp -o mediator_circular
OK: compiles, but bind() signatures are a combinatorial nightmare
```

It compiles, but at a cost: every time we add a widget, we have to update the arguments in `bind()` for N existing widgets. This is a combinatorial explosion. Every time a rule changes, we have to hunt through N classes. Once this spiderweb is woven, every subsequent requirement just adds more silk to the web, rather than adding actual functionality.

So, this path is a dead end. We must ensure that widgets don't know about each other.

## Step 2: Introduce the Mediator Interface — Star Coupling

Let's rein in the problem: since direct references between widgets are the root of all evil, we establish a rule—**widgets are only allowed to know one thing: the "mediator"**. As for how the mediator routes messages or who it forwards them to, the widget doesn't care.

We first define an abstract mediator interface. Here, we use the most straightforward "event name + string" protocol: when a widget changes, it tells the mediator "it's me (id) that changed," and the mediator decides how to coordinate the response:

```cpp
struct IMediator {
    virtual ~IMediator() = default;
    virtual void notify(const std::string& sender, const std::string& event) = 0;
};
```

Then, we define the abstract colleague base class. Note the critical step here—**the control holds only an `IMediator*`, and no other control type names appear**. This single line is the entire secret behind "star coupling":

```cpp
class Widget {
public:
    Widget(std::string id, IMediator* mediator)
        : mediator_(mediator), id_(std::move(id)) {}

    const std::string& id() const { return id_; }

protected:
    void notify(const std::string& event) {
        mediator_->notify(id_, event);   // 只跟中介者说话
    }

    IMediator* mediator_;
    std::string id_;
};
```

Look, `Widget` now depends only on the `IMediator` abstract interface. It doesn't know whether other controls exist, nor what they are named. No matter how many controls we add later, we won't need to change a single line of `Widget`'s signature.

### Build a Chat Room

A chat room is the cleanest demonstration of this structure. A `User` acts as a colleague, and the `ChatRoom` acts as the mediator. When a `User` wants to send a message, they simply call out to the mediator to "forward this to so-and-so" or "broadcast this," without holding a pointer to any other `User`:

```cpp
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

struct IMediator {
    virtual ~IMediator() = default;
    virtual void send_message(const std::string& from,
                              const std::string& to,
                              const std::string& msg) = 0;
    virtual void broadcast(const std::string& from, const std::string& msg) = 0;
};

class User {
public:
    User(std::string name, IMediator* mediator)
        : name_(std::move(name)), mediator_(mediator) {}

    const std::string& name() const { return name_; }

    void send_to(const std::string& to, const std::string& msg) {
        mediator_->send_message(name_, to, msg);   // 转交中介者
    }
    void broadcast(const std::string& msg) {
        mediator_->broadcast(name_, msg);          // 转交中介者
    }
    void receive(const std::string& from, const std::string& msg) {
        std::cout << "[" << name_ << "] recv from " << from
                  << ": " << msg << "\n";
    }

private:
    std::string name_;
    IMediator* mediator_;   // ← 只依赖抽象,不知道其它 User 的存在
};
```

Note that we do not have a signature like `User::send_to(const User&)` here at all—the `User` class is completely unaware that other `User` objects exist. All decisions regarding "who to send to", "what to do if not found", and "whether to log", have been moved to the `ChatRoom` side:

```cpp
class ChatRoom : public IMediator {
public:
    void register_user(std::shared_ptr<User> user) {
        users_[user->name()] = std::move(user);
    }

    void send_message(const std::string& from,
                      const std::string& to,
                      const std::string& msg) override {
        auto it = users_.find(to);
        if (it != users_.end()) {
            it->second->receive(from, msg);
        } else {
            // 「目标不存在」的处理策略,集中放在中介者里
            std::cout << "[room] " << to << " not online (handled by mediator)\n";
        }
    }

    void broadcast(const std::string& from, const std::string& msg) override {
        for (auto& [name, user] : users_) {
            if (name != from) user->receive(from, msg);
        }
    }

private:
    std::unordered_map<std::string, std::shared_ptr<User>> users_;
};
```

`ChatRoom` holds a table of `name -> User`. Private messaging involves looking up the table and forwarding, while broadcasting involves iterating through the table (skipping oneself). If a lookup fails, the mediator decides how to handle the fallback. Later, if product management asks to add sensitive word filtering to private messages, add audit logging for all messages, or support "storing offline messages when the recipient is not online"—we only need to modify the `ChatRoom` class. We don't need to touch a single line of code in `User`. This is the open-closed principle brought by star coupling: rule evolution only modifies the center, without affecting the leaves.

Here, let's verify that this code actually runs and that there are indeed no direct references between `User` instances:

```sh
$ g++ -std=c++23 -O2 -Wall -Wextra -pthread /tmp/mediator_verify.cpp -o mediator_verify
$ ./mediator_verify
[Bob] recv from Alice: hi Bob!
[Carol] recv from Bob: hello everyone, I'm Bob
[Alice] recv from Bob: hello everyone, I'm Bob
[room] Dave not online (handled by mediator)
```

It works. Alice's private message to Bob reaches only Bob; Bob's broadcast goes to both Alice and Carol, but not back to himself; when Carol tries to privately message the non-existent Dave, the mediator uniformly handles it with a single "offline" response. Throughout this entire flow, there is zero coupling between the `User` objects themselves—this is exactly what we expect from the Mediator pattern when it transforms a mesh network into a star topology.

## Step 3: The Classic GoF Scenario—Dialog Widget Interaction

The chat room is a gentle example. The real test is the dialog box from the original GoF book: multiple heterogeneous widgets (`Textbox`, `Listbox`, `Button`) interacting with each other based on non-trivial rules. This is the precise scenario the Mediator pattern was designed to solve, so let's complete it.

The widget types differ, but they all derive from the same abstract colleague base class, `Widget`, unifying them through a single exit point: "notify the mediator." Different widgets wrap the notification within their respective business logic methods, keeping the interface clean for the caller:

```cpp
class Textbox : public Widget {
public:
    using Widget::Widget;
    void set_text(const std::string& s) {
        text_ = s;
        notify("changed");    // 文本一变,告诉中介者
    }
    const std::string& text() const { return text_; }
private:
    std::string text_;
};

class Listbox : public Widget {
public:
    using Widget::Widget;
    void set_items(std::vector<std::string> v) {
        items_ = std::move(v);
        notify("changed");
    }
    const std::vector<std::string>& items() const { return items_; }
private:
    std::vector<std::string> items_;
};

class Button : public Widget {
public:
    using Widget::Widget;
    void enable()  { enabled_ = true; }
    void disable() { enabled_ = false; }
    bool enabled() const { return enabled_; }
    void click() {
        if (!enabled_) {
            std::cout << "[button] '" << id_ << "' disabled, ignored\n";
            return;
        }
        std::cout << "[button] '" << id_ << "' clicked -> submit\n";
    }
private:
    bool enabled_ = false;
};
```

Next is the concrete mediator, which centralizes all three rules from the product team. Note that the mediator holds pointers to all controls and orchestrates their interactions—this is the role of the "center" in a star topology, where the controls remain unaware of each other:

```cpp
class BookSearchDialog : public IMediator {
public:
    BookSearchDialog() {
        // 控件出生时就把自己交给中介者
        title_tb_  = std::make_unique<Textbox>("title",  this);
        author_tb_ = std::make_unique<Textbox>("author", this);
        list_      = std::make_unique<Listbox>("results", this);
        submit_btn_ = std::make_unique<Button>("submit", this);
    }

    void notify(const std::string& sender, const std::string& event) override {
        if (event != "changed") return;

        // 规则 1:输入变化 -> 按当前输入刷新候选列表
        std::cout << "[dialog] refilter by '"
                  << title_tb_->text() << "' / '"
                  << author_tb_->text() << "'\n";

        // 规则 2:标题和作者都非空,才允许提交
        if (!title_tb_->text().empty() && !author_tb_->text().empty()) {
            submit_btn_->enable();
            std::cout << "[dialog] submit enabled\n";
        } else {
            submit_btn_->disable();
        }
    }

    Textbox& title()  { return *title_tb_; }
    Textbox& author() { return *author_tb_; }
    Button&  submit() { return *submit_btn_; }

private:
    std::unique_ptr<Textbox> title_tb_;
    std::unique_ptr<Textbox> author_tb_;
    std::unique_ptr<Listbox> list_;
    std::unique_ptr<Button>  submit_btn_;
};
```

Here is how we use it: anyone who wants to interact with it only needs to deal with the dialog box:

```cpp
int main() {
    BookSearchDialog dlg;
    dlg.submit().click();                    // 空输入 -> 按钮 disabled,点不动
    dlg.title().set_text("C++");             // 只有标题 -> 仍 disabled
    dlg.submit().click();
    dlg.author().set_text("Stroustrup");     // 两个都有 -> enabled
    dlg.submit().click();                    // 这次能点
}
```

Let's run it to see if the rules actually take effect:

```sh
$ g++ -std=c++23 -O2 -Wall -Wextra -pthread /tmp/mediator_dialog.cpp -o mediator_dialog
$ ./mediator_dialog
[button] 'submit' disabled, ignored
[dialog] refilter by 'C++' / ''
[button] 'submit' disabled, ignored
[dialog] refilter by 'C++' / 'Stroustrup'
[dialog] submit enabled
[button] 'submit' clicked -> submit
```

Everything checks out: when both input boxes are empty, the submit is rejected; when only the title is filled, the mediator notifies rule 2 to disable the button again; when both are filled, the mediator lights up the button, and this time the click actually submits. Throughout the entire process, the three concrete classes, `Textbox`, `Listbox`, and `Button`, have no `#include` dependencies on each other—they only include the `IMediator` abstract header. The three rules that were originally scattered across three classes are now neatly organized in the single `BookSearchDialog::notify` function. In the future, if the product team wants to change the rule to "must also agree to terms," you only need to modify this one function.

## Step 4: Event Bus — Type-Erased Mediator

At this stage, we have a clean star topology. However, you will notice a new bottleneck: the chain of `if (sender == "title")` statements inside `BookSearchDialog::notify` routes based on strings. This approach is not type-safe, and every time a new event is added, you must modify the branches in the `notify` method within the mediator. In other words, this version of the mediator is not closed for modification regarding "new event types"—it violates the other side of the open-closed principle.

A more modern approach is to upgrade the mediator into an "event bus": let the "event type" itself serve as the protocol. The mediator is only responsible for "who subscribed to which type of event, I deliver that type of event to them." Publishers and subscribers share only the event type, not any specific interface. To achieve "any type can serve as a payload," we need type erasure. Since C++17, the standard library provides `std::any` as a ready-made tool, which we can pair with `std::type_index` as a hash key to bucket events by their type:

```cpp
#include <any>
#include <functional>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <vector>

class EventBus {
public:
    template <typename Event>
    void subscribe(std::function<void(const Event&)> handler) {
        // 把「具体类型 handler」擦除成「吃 std::any 的统一 handler」
        handlers_[std::type_index(typeid(Event))]
            .push_back([h = std::move(handler)](const std::any& payload) {
                h(std::any_cast<const Event&>(payload));
            });
    }

    template <typename Event>
    void publish(const Event& event) {
        auto it = handlers_.find(std::type_index(typeid(Event)));
        if (it == handlers_.end()) return;   // 无人订阅 -> 安全忽略
        for (auto& h : it->second) h(event);
    }

private:
    using ErasedHandler = std::function<void(const std::any&)>;
    std::unordered_map<std::type_index, std::vector<ErasedHandler>> handlers_;
};
```

The beauty of this code lies in the fact that type erasure occurs inside `subscribe`: the outside registers a strongly typed `std::function<void(const MessageSent&)>`, which is wrapped by a lambda, cast to a uniform signature that accepts `std::any`, and then stored in the table. The restoration via `std::any_cast<const Event&>` happens inside that same lambda's closure, where this `Event` type is fixed at compile time. Therefore, both the publisher and the subscriber work with strong types, while erasure remains an internal detail of the mediator, invisible to the user.

Events are just ordinary value types and do not inherit from any interface:

```cpp
struct MessageSent { std::string from; std::string body; };
struct UserLogin   { std::string who; };

int main() {
    EventBus bus;
    int analytics_count = 0;

    bus.subscribe<MessageSent>([&](const MessageSent& e) {
        std::cout << "[logger] " << e.from << " -> " << e.body << "\n";
    });
    bus.subscribe<MessageSent>([&](const MessageSent&) { ++analytics_count; });
    bus.subscribe<UserLogin>([&](const UserLogin& e) {
        std::cout << "[presence] " << e.who << " online\n";
    });

    bus.publish(MessageSent{"Alice", "hello world"});
    bus.publish(UserLogin{"Bob"});
    bus.publish(MessageSent{"Alice", "again"});

    std::cout << "analytics_count = " << analytics_count << " (expect 2)\n";
}
```

Run it:

```sh
$ g++ -std=c++23 -O2 -Wall -Wextra -pthread /tmp/mediator_eventbus.cpp -o mediator_eventbus
$ ./mediator_eventbus
[logger] Alice -> hello world
[presence] Bob online
[logger] Alice -> again
analytics_count = 2 (expect 2)
```

`MessageSent` is received once by each of the two subscribers (the logger and the analytics counter), while `UserLogin` is only received by the presence subscriber. Note a detail here: publishing an event that has no subscribers (like `UserLogin` before anyone subscribed to it, or vice versa) does not raise an error; it is simply and safely ignored by the bus. This is the flexibility of the event bus compared to direct calls: the publisher doesn't need to know if anyone is listening; it just broadcasts.

The event bus represents a fundamental improvement over the previous `BookSearchDialog`: **to add a new event type, we don't need to change a single line of code in the mediator**. You simply define a new event `struct`, and `subscribe` / `publish` work immediately. This is because it downgrades the "protocol" from "mediator member function signatures" to "event value types," making it truly open for extension.

## Let's Verify Here: The Cost of Type Erasure

::: warning Type Erasure Is Not Free
The event bus erases the payload using `std::any`. The cost is that **if you write the wrong event type when subscribing, the compiler won't catch it; you'll only get a `std::bad_any_cast` exception when `std::any_cast` is called**. We'll write a specific snippet to verify this failure mode, so you don't get caught out by it in production:

```cpp
struct Ping { int x; };
struct Pong { double y; };

int main() {
    std::any a = Ping{42};
    try {
        // 订阅者误以为 payload 是 Pong —— 编译器一声不吭
        const Pong& bad = std::any_cast<const Pong&>(a);
        std::cout << "no throw? y=" << bad.y << "\n";
    } catch (const std::bad_any_cast& e) {
        std::cout << "caught bad_any_cast: " << e.what() << "\n";
    }
    const Ping& ok = std::any_cast<const Ping&>(a);
    std::cout << "ok.x = " << ok.x << "\n";
}
```

```sh
$ g++ -std=c++23 -O2 -Wall -Wextra -pthread /tmp/mediator_any_badcast.cpp -o any_badcast
$ ./any_badcast
caught bad_any_cast: bad any_cast
ok.x = 42
```

In practice, this is exactly what happens: you store a `Ping`, but try to retrieve a `Pong`, resulting in a `bad_any_cast` at runtime with not a single compiler warning. So, the event bus trades "openness to event type extension" for "pushing type matching checks from compile time to runtime." For low-frequency, observable business events (login, send message, place order), this overhead is completely acceptable. However, for hot paths or latency-sensitive paths, you need to weigh whether the cost of `std::any` copying and `type_index` lookup is worth it.
:::

Additionally, the event bus has a pitfall more subtle than type erasure—**lifetimes**. We used `[&]` to capture the local variable `analytics_count` above, which requires that `analytics_count` is still alive when the event is published. In real systems, subscribers are often long-lived objects, while events might be published after the subscriber has been destructed. At that point, `this` or references captured by the lambda become dangling references. The event bus doesn't manage this for you. You must either explicitly unsubscribe in the subscriber's destructor, or use a weak pointer mechanism to filter out invalid subscribers—this is structurally identical to the lifetime problem in the Observer pattern, so we'll flag it here but not expand further.

## When the Mediator Bites Back

By now, we have a clean star topology and have upgraded it into an event bus that is open for extension. But we're not done yet—I must be honest with you, **the Mediator pattern has a notorious side effect: the mediator itself bloats into a God object.**

The reason is easy to understand: we moved all "routing, rules, coordination" logic into the mediator with good intentions, but rules tend to grow. Today, `BookSearchDialog` manages only three linkage rules. Next month, product management adds "Advanced Filter Panel," "Recent Searches," "Result Pagination," and "Permission Checks," and you naturally shove them all into `notify()`. Six months later, this mediator holds over a dozen widget pointers, and `notify()` is riddled with dozens of `if` branches. It becomes the center of that spiderweb, with all coupling concentrated inside it. We worked hard to untangle the mesh into a star, only for the center of the star to swell into a new mesh.

There are a few ways to mitigate this backlash. First, **split mediators by domain**—don't let one mediator manage everything. Use one mediator for search panel linkage, another for permission checks, and use the event bus as a backbone to connect them. Second, **extract strategies from within the mediator**—the rule "only allow submission if both inputs are non-empty" in `BookSearchDialog` can be extracted into a `SubmitPolicy` strategy object. The mediator then only needs to pass widget events to the strategy and apply the strategy's conclusions back to the widgets. This is effectively a combination of the Mediator and Strategy patterns: the mediator manages topology, while the strategy manages rules. Third, **use the event bus instead of a giant `notify`**—the event bus exposes "new event" as an extension point, which itself suppresses mediator bloat. New rules are added as "subscribing to a new event," rather than stuffing new branches into `notify()`.

One more thing to keep in mind: **the mediator adds a layer of forwarding, which has a cost in extreme low-latency scenarios**. For normal business events, the overhead of `std::function` + `std::any` is negligible. However, if you slap a mediator onto a hot path like per-frame rendering or per-packet network I/O, the cost of type erasure and indirection becomes visible. In such scenarios, either use direct function calls or make the mediator degenerate into compile-time routing (e.g., templates + `if constexpr` dispatching to eliminate erasure).

## Summary

Let's review the entire evolution path:

| Stage | Approach | Why it falls short |
|---|---|---|
| Widgets hold references | Each `Widget` holds pointers to other widgets | Mesh coupling; adding a widget requires modifying N `bind`s; prone to circular `#include`. |
| Abstract Mediator Interface | Colleagues only hold `IMediator*`; star coupling | Rules are centralized in `notify`, but routed by strings and not type-safe. |
| GoF Dialog | Concrete mediator orchestrates heterogeneous widget linkage | `notify` is closed to "new events"; the mediator easily bloats into a God object. |
| Event Bus | `std::any` + `std::type_index` type erasure | Open for event extension, but type matching checks are deferred to runtime (`std::bad_any_cast`). |

Key takeaways:

- **The core benefit of the Mediator is consolidating mesh coupling into a star topology**: Colleague objects depend only on an abstract mediator interface. They don't know each other, and adding new colleagues doesn't affect old ones.
- **The abstract mediator interface must only expose protocols understandable to abstract colleagues** (event names, message structures). Never let the mediator interface `#include` all concrete colleague classes, or the mesh coupling just moves locations.
- **The event bus uses `std::any` + `std::type_index` for type erasure**, making "new events" open for extension, but defers type matching checks to runtime (`std::bad_any_cast`).
- **The biggest backlash of the Mediator is its bloating into a God object**. Mitigations include splitting mediators by domain, extracting rules into strategy objects, and preferring the event bus over a giant `notify`.
- Be cautious using runtime-erased mediators on hot paths; the indirection cost of `std::function` + `std::any` becomes visible at per-frame or per-packet granularity.

::: tip Companion Compilable Project
The examples for this section are available as a complete compilable project in the repository at `code/volumn_codes/vol4/design-patterns/Mediator/` (`.h` + main + `CMakeLists.txt`). Run `cmake -S . -B build && cmake --build build` to see the output described above.
:::

## References

- [cppreference: `std::any`](https://en.cppreference.com/w/cpp/utility/any) (Type erasure container, since C++17)
- [cppreference: `std::type_index`](https://en.cppreference.com/w/cpp/utility/type_index) (Used as a key in `unordered_map` for bucketing by type)
- [cppreference: `std::bad_any_cast`](https://en.cppreference.com/w/cpp/utility/any/bad_any_cast) (Thrown on type mismatch)
- GoF, *Design Patterns: Elements of Reusable Object-Oriented Software* — Chapter on Mediator, the prototype for the dialog case study
