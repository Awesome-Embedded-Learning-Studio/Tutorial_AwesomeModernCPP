---
title: 'Mediator Pattern: Untangling the Web into a Star'
description: 'Starting from the most intuitive "controls holding references to each other" design, we squeeze out the Mediator interface step by step, use a chat room and a dialog box to make star-shaped coupling clear, and close with a `std::any` event bus'
chapter: 11
order: 21
tags:
  - host
  - cpp-modern
  - intermediate
  - 中介者模式
difficulty: intermediate
platform: host
cpp_standard: [11, 17, 20, 23]
reading_time_minutes: 22
related:
  - 'Singleton Pattern: From Comment-Only Constraints to Meyer''s Singleton'
prerequisites:
  - Classes and Object-Oriented Programming
translation:
  source: documents/vol4-advanced/vol4-generics-patterns/21-mediator.md
  source_hash: 6a8520fa246562d58f1f345f4ce615e08ada0fd2760d2a0177c7b51c44dfba15
  translated_at: '2026-09-26T05:58:45+00:00'
  engine: anthropic
  token_count: 11000
---

# Mediator Pattern: Untangling the Web into a Star

## What Problem Are We Actually Solving

Let's skip the formal definition for now. Picture a thoroughly ordinary scenario: you're writing a book-search dialog with four controls on it — a title input box, an author input box, a candidate list, and a confirm button. The product manager hands you three rules: while the user types in the title or author box, the list must filter in real time against the input; the confirm button may only be clicked once both title and author are filled in; and when confirm is clicked, the currently selected entry in the list gets submitted.

That doesn't sound complicated at all. So on reflex you let these four controls know each other: `Textbox` holds a `Listbox*` and a `Button*`, `Listbox` in turn holds two `Textbox*`s, and `Button` holds a `Listbox*` and two `Textbox*`s. Inside each control's callback you reach straight out and modify the others. On day one you feel quite productive; on day two the product manager adds a fifth control, "Advanced Filter," and you discover that the constructor signatures of all four old controls must change — because the new control has to be visible to them too. A week later the controls number eight, and this dependency graph has become a spider web nobody can cut apart.

The class of requirement the Mediator pattern solves is exactly this: **collapse the "mesh coupling" — a group of objects calling each other directly and holding references to each other — into a "star-shaped coupling," where every object talks only to a single mediator, and the mediator takes care of routing messages and arbitrating the rules**. The chat room is its poster-child example: users don't slip notes directly to other users; they drop the message into the chat room, and the chat room decides who receives it. The GUI dialog is the classic example from the original GoF book: each control only tells the dialog "I changed," and the dialog decides whether the other controls should follow along.

But "add a middle layer" — that little phrase — is in C++ by no means as simple as "new up a new class." There are several pits that are remarkably easy to step in: many people writing a mediator for the first time have the mediator turn around and `#include` all the colleague classes, and the mediator itself becomes an omniscient "god object" — the mesh coupling has merely moved from between the colleagues into the mediator's belly. Others, wanting the event bus to support arbitrary payloads, casually erase types with `void*` or strings, completely throwing away compile-time type checking and only blowing up at run time. So the question this article really sets out to answer is: **how do we let colleague objects depend on one abstract mediator interface only — nobody knowing anybody — while the mediator doesn't degenerate into a god object and type erasure doesn't sacrifice type safety**.

With that, let's proceed step by step: start from the most intuitive way of writing it, see why each step falls short, and finally squeeze out a modern C++ standard answer.

## Step 1: The Most Intuitive Approach — Controls Holding References to Each Other (a Cautionary Tale)

The code most people sketch on reflex the first time they hit "four controls must coordinate" looks like this: every control stuffs pointers to the other controls into itself, and the callbacks reach straight out and modify.

```cpp
class Button;   // Forward-declare each other; in the .cpp files you'll have to #include each other
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

Honestly, in a toy scenario of "few controls, rules that will never change again," this style has nothing wrong with it — don't reflexively slap a pattern on the moment you see one. The trouble starts **the moment requirements begin to expand** — when you add a sixth control, the `bind()` signatures of the three old controls above all must change, because the new control must be visible to them too; when you want to change one coordination rule (say, "submit is only allowed once the agreement checkbox is ticked"), you find that rule scattered across three places — `Textbox::on_text_changed`, `Listbox::on_selection_changed`, `Button::on_click` — and changing one spot means confirming the other two won't collapse along with it.

What's worse is the compile-time problem. For `Textbox` to take a `Button*` it must see the complete definition of `Button` in its implementation, and `Button` must see the complete definition of `Textbox` — forward declarations can only prop up the declaration of a pointer, not the need to call the other's methods inside a member function. So the headers end up `#include`-ing each other and all too easily wind into circular dependencies. Let's actually test in /tmp whether the minimal skeleton of this "mutually held references" style can compile:

```sh
$ g++ -std=c++23 -O2 -Wall -Wextra -pthread /tmp/mediator_circular.cpp -o mediator_circular
OK: compiles, but bind() signatures are a combinatorial nightmare
```

It compiles, but at this price — every control you add forces expanding the parameters of N old controls' `bind()`, a combinatorial explosion; every rule change means flipping through N classes. Once this spider web is woven, every subsequent requirement adds more silk to it rather than adding features.

So this road is a dead end. We have to make the controls not know each other at all.

## Step 2: Introducing the Mediator Interface — Star-Shaped Coupling

Let's converge the problem: since direct references between controls are the root of all evil, let's lay down one rule — **a control is allowed to know exactly one thing, and that is the "mediator."** As for how the mediator routes behind the scenes and whom it forwards messages to, the control doesn't care in the slightest.

First we define an abstract mediator interface. We'll use the most plain-spoken "event name + string" protocol: when a control changes, it tells the mediator "it is I (id) who changed"; how the coordination plays out concretely is the mediator's call:

```cpp
struct IMediator {
    virtual ~IMediator() = default;
    virtual void notify(const std::string& sender, const std::string& event) = 0;
};
```

Then we define the abstract colleague base class. Note the key move here — **the control holds a single `IMediator*`, and no other control type's name appears anywhere**. This one line is the entire secret of "star-shaped coupling":

```cpp
class Widget {
public:
    Widget(std::string id, IMediator* mediator)
        : mediator_(mediator), id_(std::move(id)) {}

    const std::string& id() const { return id_; }

protected:
    void notify(const std::string& event) {
        mediator_->notify(id_, event);   // Talk only to the mediator
    }

    IMediator* mediator_;
    std::string id_;
};
```

See — `Widget` now depends only on the `IMediator` abstraction; it neither knows whether other controls exist nor what they'd be called. However many controls get added later, not one line of `Widget`'s signature here has to change.

### Building a Chat Room with It

The chat room is the cleanest demonstration of this structure. A `User` is a colleague, and `ChatRoom` is the mediator. When a `User` wants to send a message, it just hollers at the mediator — "forward this to so-and-so for me" / "broadcast this for me" — and holds no pointer to any other `User` whatsoever:

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
        mediator_->send_message(name_, to, msg);   // Hand off to the mediator
    }
    void broadcast(const std::string& msg) {
        mediator_->broadcast(name_, msg);          // Hand off to the mediator
    }
    void receive(const std::string& from, const std::string& msg) {
        std::cout << "[" << name_ << "] recv from " << from
                  << ": " << msg << "\n";
    }

private:
    std::string name_;
    IMediator* mediator_;   // ← depends only on the abstraction; unaware other Users exist
};
```

Notice we never produce a signature like `User::send_to(const User&)` — a `User` flat-out doesn't know other `User`s exist in the world. Every decision — "deliver to whom," "what if the target isn't found," "should a log be kept" — has been moved over to the `ChatRoom` side:

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
            // The "target doesn't exist" policy, centralized in the mediator
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

`ChatRoom` holds a `name -> User` table: a direct message is a table lookup plus forwarding, a broadcast is iterating the table (conveniently excluding the sender), and on a miss the mediator itself decides how to handle the fallback. Later, when the product manager says private messages need sensitive-word filtering, all messages need audit logging, or "store an offline message when the recipient isn't online" must be supported — you change exactly one class, `ChatRoom`, and don't touch a single character of `User`. That is the openness star-shaped coupling buys you: rules evolve at the center only, without rippling into the leaves.

Let's first verify that this code actually runs, and that the `User`s really have no direct references to each other:

```sh
$ g++ -std=c++23 -O2 -Wall -Wextra -pthread /tmp/mediator_verify.cpp -o mediator_verify
$ ./mediator_verify
[Bob] recv from Alice: hi Bob!
[Carol] recv from Bob: hello everyone, I'm Bob
[Alice] recv from Bob: hello everyone, I'm Bob
[room] Dave not online (handled by mediator)
```

It runs. Alice's direct message to Bob hits only Bob; Bob's broadcast reaches both Alice and Carol without looping back to himself; Carol's attempt to privately message the nonexistent Dave gets uniformly fielded by the mediator into a single "not online." Through the entire flow there isn't one line of coupling between `User`s — that is what the Mediator pattern is supposed to look like once it has untangled the mesh into a star.

## Step 3: The Classic GoF Scenario — Dialog Control Coordination

The chat room is a gentle example; the real test is that dialog from the original GoF book: several heterogeneous controls (`Textbox`, `Listbox`, `Button`) coordinating with one another, with nontrivial rules to boot. This is precisely the scenario the Mediator pattern took aim at when it was born, so let's complete it.

The control types differ, but they all derive from the same abstract colleague base class `Widget` and uniformly use the single outlet of "notify the mediator." Different controls wrap the notification inside their own business methods, so the interface stays clean for callers:

```cpp
class Textbox : public Widget {
public:
    using Widget::Widget;
    void set_text(const std::string& s) {
        text_ = s;
        notify("changed");    // The text changed — tell the mediator
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

Next comes the concrete mediator, gathering all three of the product manager's rules into one place. Note that the mediator holds pointers to all the controls and orchestrates the coordination between them — that is the role the "center" of a star topology should shoulder, while the controls still don't know each other:

```cpp
class BookSearchDialog : public IMediator {
public:
    BookSearchDialog() {
        // Each control hands itself to the mediator at birth
        title_tb_  = std::make_unique<Textbox>("title",  this);
        author_tb_ = std::make_unique<Textbox>("author", this);
        list_      = std::make_unique<Listbox>("results", this);
        submit_btn_ = std::make_unique<Button>("submit", this);
    }

    void notify(const std::string& sender, const std::string& event) override {
        if (event != "changed") return;

        // Rule 1: input changed -> refilter the candidate list by the current input
        std::cout << "[dialog] refilter by '"
                  << title_tb_->text() << "' / '"
                  << author_tb_->text() << "'\n";

        // Rule 2: submit is allowed only when both title and author are non-empty
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

Using it looks like this — whoever needs coordination deals only with the dialog:

```cpp
int main() {
    BookSearchDialog dlg;
    dlg.submit().click();                    // empty input -> button disabled, the click does nothing
    dlg.title().set_text("C++");             // title only -> still disabled
    dlg.submit().click();
    dlg.author().set_text("Stroustrup");     // both present -> enabled
    dlg.submit().click();                    // this time it works
}
```

Let's run it and see whether the rules actually took effect:

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

Every step checks out: with both input boxes empty, clicking submit is refused; with only the title filled in, the mediator's rule 2 re-disables the button; with both filled in, the mediator lights the button up, and only then does a click truly submit. Throughout the whole process, the three concrete classes `Textbox`, `Listbox`, and `Button` have no `#include` of each other whatsoever — they include only the abstract `IMediator` header. The three rules formerly scattered across three classes now sit neatly stacked in the single function `BookSearchDialog::notify`; later, when the product manager wants the rule changed to "the agreement checkbox must also be ticked," you touch exactly this one function.

## Step 4: The Event Bus — A Type-Erased Mediator

At this point we already have a clean star-shaped structure. But you'll notice a new bottleneck: that string of `if (sender == "title")` inside `BookSearchDialog::notify` routes by string, which is not type-safe in the slightest, and every new kind of event means touching the branches of `notify` in the mediator once more. In other words, this version of the mediator isn't open to "adding new event types" — it violates the other face of the open-closed principle.

The more modern approach is to upgrade the mediator into an "event bus": let the "event type" itself be the protocol, and the mediator's only job becomes "whoever subscribed to which kind of event, I deliver that kind of event to them." Publishers and subscribers share only event types, not any interface. To let "any type serve as a payload," we need type erasure — and since C++17 the standard library's ready-made tool is `std::any`, paired with `std::type_index` as the hash key for bucketing by event type:

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
        // Erase a "concrete-typed handler" into a "uniform handler that eats std::any"
        handlers_[std::type_index(typeid(Event))]
            .push_back([h = std::move(handler)](const std::any& payload) {
                h(std::any_cast<const Event&>(payload));
            });
    }

    template <typename Event>
    void publish(const Event& event) {
        auto it = handlers_.find(std::type_index(typeid(Event)));
        if (it == handlers_.end()) return;   // no subscribers -> safely ignored
        for (auto& h : it->second) h(event);
    }

private:
    using ErasedHandler = std::function<void(const std::any&)>;
    std::unordered_map<std::type_index, std::vector<ErasedHandler>> handlers_;
};
```

The beauty of this code is that the type erasure happens inside `subscribe`: what gets registered on the outside is the strongly typed `std::function<void(const MessageSent&)>`, which a lambda wraps and converts into the uniform signature that eats `std::any` before being stored in the table; the `std::any_cast<const Event&>` recovery likewise happens inside that same lambda's closure, with this `Event` nailed down at compile time. So publishers and subscribers both see strong types when they write code — the erasure is the mediator's internal business and invisible to users.

Events are plain value types, inheriting from no interface:

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

Let's run it:

```sh
$ g++ -std=c++23 -O2 -Wall -Wextra -pthread /tmp/mediator_eventbus.cpp -o mediator_eventbus
$ ./mediator_eventbus
[logger] Alice -> hello world
[presence] Bob online
[logger] Alice -> again
analytics_count = 2 (expect 2)
```

`MessageSent` gets received once each by two subscribers (the logger and the analytics counter); `UserLogin` reaches only the presence subscriber. Note one detail — publishing an event nobody subscribes to (nobody had subscribed before `UserLogin`'s first publish, or the other way around) doesn't error out; the bus just safely ignores it. That is precisely the resilience an event bus has over direct calls: the publisher doesn't need to know whether anyone cares — it just shouts.

The event bus is an essential step forward over the earlier `BookSearchDialog`: **to add a new event type, you change not one line of the mediator**. You just `struct` up a new event, and `subscribe` / `publish` work with it immediately. That's because it demoted the "protocol" from "the mediator's member function signatures" to "event value types," making it thoroughly open to extension.

## Let's Verify Right Here: The Cost of Type Erasure

::: warning Type erasure is not free
The event bus erases the payload with `std::any`, and the price is that **if you write the wrong event type at subscription time, the compiler can't catch it — it waits until the `std::any_cast` moment to throw `std::bad_any_cast`**. Let's deliberately write a snippet to verify this failure mode, so it doesn't ambush you in production:

```cpp
struct Ping { int x; };
struct Pong { double y; };

int main() {
    std::any a = Ping{42};
    try {
        // The subscriber wrongly assumes the payload is Pong — the compiler says nothing
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

Measured behavior: what was stored is a `Ping`, you fetch it as a `Pong`, and at run time it throws `bad_any_cast` — with not one line of compile-time warning. So what the event bus buys is "openness to event-type extension," and the price is that "the type-match check on the subscription side is deferred from compile time to run time." For low-frequency, observable business events (login, message sent, order placed), this cost is entirely acceptable; but for high-frequency hot paths or latency-sensitive paths, you need to weigh whether this layer of `std::any` copying and `type_index` table lookups is worth it.
:::

Also, the event bus has a pit sneakier than type erasure — **lifetimes**. Above, we captured the local variable `analytics_count` with `[&]`, which requires that `analytics_count` still be alive when events are published. In a real system, subscribers are often long-lived objects, while events may be published after the subscriber has been destroyed — at which point the `this` or the references captured in the lambda have become dangling. The event bus won't manage this for you: either have subscribers explicitly unsubscribe upon destruction, or use a weak-reference mechanism to filter out dead subscribers — this is isomorphic to the Observer pattern's lifetime problem; we flag it here and won't expand on it.

## When the Mediator Turns Around and Bites You

At this point we have a clean star-shaped structure, and we've even upgraded it into an event bus that's open to extension. But we're not done — I have to be honest with you: **the Mediator pattern has a notorious backlash: the mediator itself swells into a god object**.

The reason is easy to understand: we moved all the "routing, rules, coordination" logic into the mediator with the best of intentions — but rules grow. Today's `BookSearchDialog` manages only three coordination rules; next month product adds an "advanced filter panel," "recent searches," "result pagination," and "permission checks," and you quite naturally stuff them all into `notify()` too — half a year later, this mediator holds a dozen-plus control pointers, dozens of `if` branches hang off `notify()`, and it has itself become the center of that spider web, all coupling concentrated in its belly. We worked so hard to untangle the mesh into a star, only for the star's center to bloat into a new web.

There are several lines of defense against this backlash. First, **split into multiple mediators by domain** — don't let one mediator run heaven and earth; use one mediator for the search panel's coordination, another for permission checks, with the event bus as the substrate stringing them together. Second, **extract the policies inside the mediator** — that `BookSearchDialog` rule of "submit is allowed only when both inputs are non-empty" can be pulled out into a `SubmitPolicy` strategy object, and the mediator merely feeds control events to the policy and applies the policy's verdict back onto the controls. This is really the Mediator and Strategy patterns combined: the mediator runs the topology, the strategy runs the rules. Third, **prefer the event bus over a giant `notify`** — the event bus opens up "adding new events" as an extension point, which is itself the mechanism that suppresses mediator bloat: new rules join as "subscribe to a new kind of event" instead of stuffing new branches into `notify()`.

One more thing to keep in mind: **the mediator adds a layer of forwarding, and in extremely low-latency scenarios you pay for it**. For ordinary business events, the cost of this `std::function` + `std::any` layer is negligible, but if you wrap a mediator around per-frame rendering or per-packet network hot paths, the cost of that type erasure and indirect call can surface. In such scenarios, either use direct function calls or let the mediator degenerate into compile-time routing (say, templates + `if constexpr` dispatch, skipping the erasure).

## Summary

Let's trace the whole evolutionary path once through:

| Stage | Approach | Why It Still Isn't Enough |
|---|---|---|
| Controls holding references to each other | Every `Widget` holds pointers to the other controls | Mesh coupling; adding a control means changing `bind()` in N places; circular `#include` comes easily |
| Abstract mediator interface | Colleagues hold only `IMediator*`; star-shaped coupling | Rules concentrate in `notify`, but routing is by string and not type-safe |
| GoF dialog | A concrete mediator orchestrates coordination among heterogeneous controls | `notify` is closed to "new event types"; the mediator easily bloats into a god object |
| Event bus | Type erasure with `std::any` + `std::type_index` | Open to event extension; the price is that type checking is deferred to run time |

Jot down these key takeaways:

- **The mediator's core payoff is collapsing mesh coupling into a star**: colleague objects depend on one abstract mediator interface only, nobody knows anybody, and new colleagues don't ripple into old ones.
- **The abstract mediator interface must expose only a protocol the abstract colleague can understand** (event names, message structures); never let the mediator interface turn around and `#include` all the concrete colleague classes — otherwise the mesh coupling has merely moved house.
- **The event bus does type erasure with `std::any` + `std::type_index`**, making "adding new events" open to extension, but the subscription-side type-match check is deferred from compile time to run time (`std::bad_any_cast`).
- **The mediator's biggest backlash is swelling into a god object itself**; the mitigations are splitting into multiple mediators by domain, extracting rules into strategy objects, and preferring the event bus over a giant `notify`.
- Be wary of run-time-erased mediators on hot paths; that layer of `std::function` + `std::any` indirect calling shows up at the per-frame / per-packet level.

::: tip The companion compilable project
This section's examples have a complete compilable project (`.h` + main + `CMakeLists.txt`) in the repository at `code/volumn_codes/vol4/design-patterns/Mediator/`; `cmake -S . -B build && cmake --build build` reproduces the outputs above.
:::

## References

- [cppreference: `std::any`](https://en.cppreference.com/w/cpp/utility/any) (since C++17, a type-erased container)
- [cppreference: `std::type_index`](https://en.cppreference.com/w/cpp/utility/type_index) (used as an `unordered_map` key for bucketing by type)
- [cppreference: `std::bad_any_cast`](https://en.cppreference.com/w/cpp/utility/any/bad_any_cast) (thrown when the types don't match)
- GoF, *Design Patterns: Elements of Reusable Object-Oriented Software* — the Mediator chapter, the prototype of the dialog case
