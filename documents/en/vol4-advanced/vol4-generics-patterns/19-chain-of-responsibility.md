---
title: 'Chain of Responsibility Pattern: From a Long Chain of if/else to `next_` Pointers, and Then to the Middleware Onion'
description: 'Starting from the most intuitive "the caller hard-codes who handles what" approach, we work our way step by step to the classic `next_` pointer chain, see exactly what it solves and where it relocates the coupling, and finish with two modern C++ alternatives: a `std::vector` dispatcher and a `std::function` middleware onion'
chapter: 11
order: 19
tags:
  - host
  - cpp-modern
  - intermediate
  - 责任链模式
difficulty: intermediate
platform: host
cpp_standard: [11, 17, 20]
reading_time_minutes: 20
related:
  - 'Singleton Pattern: From Comment-Only Constraints to Meyer''s Singleton'
  - 'Strategy Pattern: From a Heap of if/else to Compile-Time Swappable Policies'
prerequisites:
  - 'Chapter 6: Classes and Object-Oriented Programming'
translation:
  source: documents/vol4-advanced/vol4-generics-patterns/19-chain-of-responsibility.md
  source_hash: 89365238cd48d1cc0e46a2f496ebfcc01e970a484dc84bef12c801e7c16d00e0
  translated_at: '2026-09-26T06:00:13+00:00'
  engine: anthropic
  token_count: 8000
---

# Chain of Responsibility Pattern: From a Long Chain of if/else to `next_` Pointers, and Then to the Middleware Onion

## What problem are we actually solving

Let's not rush into a definition. Picture a very concrete scenario: you've written a network service, and now you need to process incoming requests. A request arrives, and you want it to pass through a few gates in a fixed order — first check whether it carries a valid auth token, and reject it outright if it doesn't; once authenticated, write an access log entry; after the log is written, hand it to the real business logic to compute the result. The order of these steps is fixed, and "should we keep going" is each step's own call (if authentication fails, that's the end of the road; the later steps should never run at all).

The most intuitive way to write it is for the caller to hard-code that order itself:

```cpp
void handle_request(const Request& req) {
    if (!check_auth(req)) {
        reject(req);
        return;
    }
    log_access(req);
    if (!run_business(req)) {
        respond_error(req);
        return;
    }
}
```

At first this is fine. Then things start to spiral. Product wants rate limiting added (at most 100 requests per IP per minute), so you insert it before authentication. Two days later they want metrics instrumentation (per-step timing), so you wedge in another layer. Later still there's a canary rollout to do (only certain users take the new logic) — another layer. Now when you open `handle_request`, you're staring at a long, ordered sequence of steps, each of which "might intercept or might pass", where the slightest ordering mistake is a production incident — and every new layer of logic forces you to **open this function and rework its internal structure**.

The root of the problem: **"who handles a request, in what order, and when to stop" is welded into the caller**. The caller doesn't just know which gates exist — it knows their sequence, and it knows which one can halt which. What we want is the reverse: **the caller's only job is to drop the request into a chain, and each node on the chain decides for itself "do I handle this, or toss it to the next one"**. How the chain is assembled, how many layers it has, in what order — the caller knows none of it.

That is exactly the problem the Chain of Responsibility pattern solves. GoF's intent in one sentence: **"give more than one object a chance to handle a request, chain these objects together, and pass the request along the chain until one of them handles it"** — the goal being to decouple the **sender** of a request from its **receiver**: the sender doesn't know, and doesn't need to know, who ends up handling it.

But "chaining them together" can be implemented several ways in C++, and each has its own traps. Let's go step by step, starting from the dumbest version, and see how each style is forced into existence by the previous one's pain points.

## Step 1: The most primitive approach — the caller hard-codes the order (a cautionary example)

We've already seen this opening; let's zoom in a little and see exactly where the pain sits:

```cpp
void handle_request(const Request& req) {
    if (!rate_limit(req)) { reject_429(req); return; }
    if (!check_auth(req)) { reject_401(req); return; }
    log_access(req);
    if (!run_business(req)) { respond_error(req); return; }
    respond_ok(req);
}
```

It runs, but the defects pile up. **First**, every new gate means going back and editing this function — it is "closed for extension", violating the open-closed principle. **Second**, the ordering between gates is implicit (you only learn that rate limiting comes before authentication by reading top to bottom); there's no single place where you can see at a glance "what this chain looks like". **Third**, and sneakiest — these gates are hard-coded function calls. If you want to assemble a chain dynamically at runtime based on configuration (say, "no rate limiting in the internal test environment"), it's simply impossible without piling on yet another heap of `if`.

The root reason this road dead-ends: **"which handlers exist" and "how the caller uses these handlers" are kneaded together inside one function**. What we really want is to pull "the set of handlers" and "how they get chained together" out of the caller and turn them into a structure that can be assembled independently. Each handler answers exactly two questions: **"Can I handle this request?" and "If not, hand it to the next one."**

That is precisely what the classic `next_` pointer chain does.

## Step 2: The classic pointer chain — a `next_` pointer plus handle-or-forward

Let's jump straight to the code, then take it apart line by line to see why it's written this way. This is the classic GoF pointer-chain implementation: an abstract `Handler` that every concrete handler inherits from, each holding a `next_` pointer to the next handler:

```cpp
#include <iostream>
#include <memory>
#include <string>

class Handler {
public:
    virtual ~Handler() = default;

    void set_next(std::shared_ptr<Handler> next) {
        next_ = std::move(next);
    }

    // Template method: the "forwarding" logic is welded into the base class; subclasses only implement process
    void handle(const std::string& req) {
        const bool handled = process(req);
        if (!handled && next_) {
            next_->handle(req);          // toss it to the next one
        } else if (!handled && !next_) {
            std::cout << "[chain end] nobody handled: " << req << "\n";
        }
    }

protected:
    virtual bool process(const std::string& req) = 0;  // returning true means "I handled it"

private:
    std::shared_ptr<Handler> next_;
};

class AuthHandler : public Handler {
protected:
    bool process(const std::string& req) override {
        if (req == "auth") {
            std::cout << "AuthHandler handled\n";
            return true;
        }
        return false;
    }
};

class LogHandler : public Handler {
protected:
    bool process(const std::string& req) override {
        if (req == "log") {
            std::cout << "LogHandler handled\n";
            return true;
        }
        return false;
    }
};
```

Using it looks like this:

```cpp
int main() {
    auto auth = std::make_shared<AuthHandler>();
    auto log = std::make_shared<LogHandler>();
    auth->set_next(log);

    auth->handle("log");   // auth declines -> forwards to log -> log handles it
    auth->handle("auth");  // auth handles it itself
    auth->handle("xxx");   // nobody takes it all the way; the chain end reports "nobody handled"
}
```

Compile and run (GCC 16.1.1):

```sh
$ g++ -std=c++23 -O2 -Wall chain_verify.cpp -o chain_verify
$ ./chain_verify
=== pointer chain ===
LogHandler handled
AuthHandler handled
[chain end] nobody handled: xxx
```

Notice that the caller knows only `auth->handle(req)` — it has **no idea** whether there are more nodes behind it, how many, or who they are. That's the core payoff of the chain of responsibility: sender and receiver are decoupled. The request flows along the chain; each node either swallows it itself (returns `true`) or tosses it to `next_`, until someone swallows it or the chain runs out.

### The clever part of this design: why `handle` and `process` are separate

You might ask: why split `handle` and `process` into two functions? Wouldn't it be simpler to write a single virtual `handle` in each subclass and let it decide for itself whether to forward?

You could, but then the burden of "forwarding to `next_`" is pushed onto **every single subclass** — every concrete handler has to remember to write that lump of "if I didn't handle it and there is a `next_`, call `next_->handle(req)`". Sooner or later somebody gets it wrong (forgets to forward, or forwards wrongly), and the compiler says nothing.

The classic pointer chain cures this with a particularly elegant move: **hoist the "forwarding" logic into a non-virtual `handle` in the base class and weld it there, while subclasses expose only a pure virtual `process` that answers "did I handle this request"**. Note that `handle` is **non-virtual** in the base — it is a template method, controlling the fixed skeleton of "let the subclass try first; if it can't, forward". Subclasses cannot touch that skeleton; they can only fill in the `process` slot. Written this way, "forwarding" exists exactly once and can never be forgotten.

This arrangement has a name: the **template method + Chain of Responsibility** combination. It cleanly separates **what stays fixed (the forwarding skeleton)** from **what varies (each node's decision logic)**. When you write a chain of responsibility and find every node hand-writing its own forwarding logic, that's the sign this split wasn't done right.

## Let's verify first: does the forwarding logic really run only once

Talk is cheap, so let's print the forwarding process and confirm that "a request is either swallowed by some node or walks all the way to the end of the chain", with no double processing in between. We add prints inside each node's `handle` to watch the request's trajectory:

```cpp
#include <iostream>
#include <memory>
#include <string>

class Handler {
public:
    virtual ~Handler() = default;
    void set_next(std::shared_ptr<Handler> next) { next_ = std::move(next); }

    void handle(const std::string& req) {
        std::cout << "  -> enter " << name() << " with '" << req << "'\n";
        if (process(req)) {
            std::cout << "  <- " << name() << " handled it, chain stops\n";
            return;
        }
        std::cout << "  <- " << name() << " passed (no match)\n";
        if (next_) next_->handle(req);
        else std::cout << "  <- chain end, nobody handled\n";
    }

protected:
    virtual bool process(const std::string& req) = 0;
    virtual const char* name() const = 0;

private:
    std::shared_ptr<Handler> next_;
};

class AuthHandler : public Handler {
protected:
    bool process(const std::string& req) override { return req == "auth"; }
    const char* name() const override { return "Auth"; }
};

class LogHandler : public Handler {
protected:
    bool process(const std::string& req) override { return req == "log"; }
    const char* name() const override { return "Log"; }
};

int main() {
    auto auth = std::make_shared<AuthHandler>();
    auto log = std::make_shared<LogHandler>();
    auth->set_next(log);

    std::cout << "[request: log]\n";   auth->handle("log");
    std::cout << "[request: auth]\n";  auth->handle("auth");
    std::cout << "[request: xxx]\n";   auth->handle("xxx");
}
```

Run it:

```sh
$ g++ -std=c++23 -O2 -Wall chain_trace.cpp -o chain_trace
$ ./chain_trace
[request: log]
  -> enter Auth with 'log'
  <- Auth passed (no match)
  -> enter Log with 'log'
  <- Log handled it, chain stops
[request: auth]
  -> enter Auth with 'auth'
  <- Auth handled it, chain stops
[request: xxx]
  -> enter Auth with 'xxx'
  <- Auth passed (no match)
  -> enter Log with 'xxx'
  <- Log passed (no match)
  <- chain end, nobody handled
```

The trajectory is clear: the request moves one way along the chain, either stopping at some node (its `process` returns true and the chain terminates immediately) or walking all the way to the end and reporting "nobody handled" — it **never backtracks and never re-enters the same node**. This linear-flow property is the most important invariant of the classic chain of responsibility, and what the later "middleware onion" and the "rollback-capable chain of responsibility" do is precisely to break, and renegotiate, this invariant.

## Pitfall warning: the `next_` pointer chain is less decoupled than you think

::: warning The next_ pointer chain moves the coupling somewhere else — it doesn't eliminate it
The pointer chain genuinely decouples the **caller** from the **concrete handlers** — the caller doesn't know who is downstream. But it moves the coupling **between nodes**. Every node clutching its own `next_` pointer brings three very real engineering pains.

**First, inserting a node in the middle means manually relinking pointers.** Suppose you have `auth -> log` and now want to insert `metrics` in between. You can't just drop `metrics` in — you must change `auth`'s `next_` to point at `metrics`, and point `metrics`'s `next_` at `log`. Let's verify this relinking cost in the compiler:

```cpp
auto metric = std::make_shared<MetricsHandler>();
metric->set_next(log);      // metrics picks up the original tail
auth->set_next(metric);     // auth's next is re-pointed at metrics
// now: auth -> metric -> log
```

The run (full code in the companion project):

```sh
$ ./chain_verify
=== inserting middle node into pointer chain: relink cost ===
MetricsHandler handled
LogHandler handled
```

The relink worked, but look at the price: **to insert one node, you touched the internal state of an unrelated node on the chain (`auth`)**. If `auth` is maintained by someone else and all you hold is a pointer, this simply cannot be changed.

**Second, the chain's shape is scattered across every node's `next_`; there's no single place where you can see "what the whole chain looks like" at a glance.** To print the chain while debugging, you have to follow `next_` from the head all the way down. A break in the middle, an accidental cycle, a wrong link — none of it is detectable at compile time; you find out by crashing into it at runtime.

**Third, the `next_` pointer makes nodes hold each other.** Every node on the chain `shared_ptr`s the next one, and it takes one careless moment to create a cycle (`A->B, B->A`) — and once there's a cycle, you have a memory leak, because the `shared_ptr` reference counts can never drop to zero. Textbook examples of the pointer chain never mention this, but in real engineering, assembling and tearing down the chain is exactly where things blow up.
:::

So here is the pointer chain's honest ledger: **it solves "the caller shouldn't know who handles the request", at the cost of scattering the chain-assembly responsibility across every node**. In simple scenarios with few nodes and a mostly static chain, that cost doesn't matter; but once nodes come and go dynamically and the chain must be assembled at runtime, the pointer chain gets stretched thin. We need a different style that "gathers up" the chain.

## Step 3: Gathering the chain into a `std::vector` — turning the chain into a collection

Since the `next_` pointer's defect is "the chain is scattered across every node", the most direct antidote is: **gather the chain into a collection and let a dedicated dispatcher manage it uniformly**. The shape of the whole chain is no longer hidden inside the nodes' `next_` fields — it sits plainly in a `std::vector`. That's exactly how the companion compilable project does it, so let's follow its approach:

```cpp
#pragma once
#include <memory>
#include <print>
#include <vector>

struct Message {
    enum Type { kDisk, kConsole, kGuiScreen };
    Message(Type t, std::string msg) : type(t), text(std::move(msg)) {}
    const Type type;
    const std::string text;
};

struct Handler {
    virtual ~Handler() = default;
    virtual bool can_accept(const Message& m) = 0;
    virtual void process(const Message& m) = 0;
};

struct DiskHandler : Handler {
    bool can_accept(const Message& m) override { return m.type == Message::kDisk; }
    void process(const Message& m) override { std::println("From Disk: {}", m.text); }
};

struct ConsoleHandler : Handler {
    bool can_accept(const Message& m) override { return m.type == Message::kConsole; }
    void process(const Message& m) override { std::println("From Console: {}", m.text); }
};

struct GuiHandler : Handler {
    bool can_accept(const Message& m) override { return m.type == Message::kGuiScreen; }
    void process(const Message& m) override { std::println("From GUI: {}", m.text); }
};

struct HandlerChain {
    HandlerChain() {
        handlers_.emplace_back(std::make_shared<DiskHandler>());
        handlers_.emplace_back(std::make_shared<ConsoleHandler>());
        handlers_.emplace_back(std::make_shared<GuiHandler>());
    }

    void dispatch(const Message& m) {
        for (const auto& h : handlers_) {
            if (h->can_accept(m)) {
                h->process(m);
            }
        }
    }

private:
    std::vector<std::shared_ptr<Handler>> handlers_;
};
```

Using it:

```cpp
#include "OutputHandler.h"

int main() {
    HandlerChain chain;
    chain.dispatch({Message::kDisk, "Hello, World"});
    chain.dispatch({Message::kConsole, "Hello, World"});
    chain.dispatch({Message::kGuiScreen, "Hello, World"});
}
```

Run it:

```sh
$ g++ -std=c++23 -O2 -Wall chain_verify.cpp -o chain_verify
$ ./chain_verify
=== vector dispatcher ===
From Disk: Hello, World
From Console: Hello, World
From GUI: Hello, World
```

We need to be clear about how this version differs from the pointer chain, because it **is not a drop-in equivalent — the semantics changed**.

### The key difference here: broadcast vs. first-match-wins

Look closely at the `HandlerChain::dispatch` loop: it **iterates over all handlers**, calling `process` once for every handler whose `can_accept` returns true — **there is no break**. Which means: if two handlers can both accept the same message, both get triggered. That is **broadcast** semantics.

The classic pointer chain, by contrast, is **first-match-wins**: the first node whose `process` returns true swallows the request, and the nodes behind it never even learn the message came by.

These two are not the same thing, and picking the wrong one causes incidents. Logging frameworks (one log entry may write to a file and ship over the network at the same time) are usually broadcast; approval workflows (once the manager signs the expense report, the director doesn't need to see it) are usually first-match-wins. If you want first-match-wins semantics, the loop above needs one extra `break`:

```cpp
void dispatch_first_match(const Message& m) {
    for (const auto& h : handlers_) {
        if (h->can_accept(m)) {
            h->process(m);
            break;   // the first taker terminates the request; the rest don't run
        }
    }
}
```

So remember this: **the "vector dispatcher" itself doesn't mandate broadcast or first-match-wins — it hands the decision back to how `dispatch` is written**. Whichever you want, it's just a question of whether the loop has a `break`. That's actually one advantage of the vector version over the pointer version: the pointer chain's "first-match-wins" is baked into the skeleton, and changing it to broadcast means modifying the base class, while the vector version changes semantics by changing one loop. The companion Playground project uses broadcast semantics (iterate over all handlers, no break) — be aware of that.

### What the vector version solves, and what it still lacks

Once the vector version gathers the chain into a collection, **all three of the pointer chain's pain points from earlier are solved**: inserting a middle node is just `handlers_.insert(it, new_handler)`, touching no existing node; the whole chain's shape is visible at a glance (it's a vector — just print it); and nodes no longer hold `next_` pointers to each other, so the cycle-leak hazard is gone too.

But the vector version also introduces a new restriction: **the node's autonomy over "forwarding" is taken away**. In the pointer chain, a node can decide inside `process` to "handle half of it, then actively pass the request on down the line" — it has full control over forwarding. In the vector version, "whether to keep going" is the dispatcher's call (the `dispatch` loop), and the node answers only the single boolean question "do I accept or not". For simple pipelines where each step independently judges "can I do this", that's enough; but for more elaborate orchestration like "after this step finishes, I want to decide whether to continue based on the result", the vector version strains.

In the next section we'll look at a style designed precisely for the latter — the middleware onion.

## Step 4: The middleware onion — each node decides its own before, after, and whether to forward

In the three styles above, a node answers one question only: "do I handle this request or not". But real middleware (you've used the pipelines of Express.js, Koa, or ASP.NET, right?) does far more — a middleware wants to do something **before invoking the next one** (record a start timestamp), do something again **after the next one returns** (compute the elapsed time, write a log), or even **never invoke the next one at all** (short-circuit outright on failed authentication). This demand — every node having a before-phase, an after-phase, and the ability to short-circuit — turns the chain of responsibility from a one-way straight line into an "onion": the request bores in layer by layer, and the response bores back out layer by layer.

The cleanest way to implement this in C++: **the node no longer answers a boolean question — it becomes a function that "receives next and decides for itself how to use it"**. One unified dispatcher is responsible for feeding "the next one" to each node in order:

```cpp
#include <functional>
#include <iostream>
#include <vector>

class MiddlewareChain {
public:
    // Each middleware: receives a reference to "the next one" and decides how to orchestrate
    using Middleware = std::function<void(MiddlewareChain&)>;

    void use(Middleware m) { middlewares_.push_back(std::move(m)); }

    void next() {
        if (index_ < middlewares_.size()) {
            auto m = middlewares_[index_++];
            m(*this);   // hand itself (that is, "how to continue") to the middleware
        }
    }

private:
    std::vector<Middleware> middlewares_;
    std::size_t index_ = 0;
};
```

Note the two design points here. **First, the middleware's signature is `void(MiddlewareChain&)`: what it receives is not "the next middleware" but a reference to the entire chain**, and it advances things itself by calling `chain.next()` — this hands the power of "should we go on" entirely to the middleware. **Second, there is an `index_` cursor inside `next()`**, advancing one slot per call; this avoids the pointer chain's "every node holds a pointer to the next" coupling while preserving the node's autonomy over its own forwarding.

Using it:

```cpp
int main() {
    MiddlewareChain chain;
    chain.use([](MiddlewareChain& c) {
        std::cout << "before: auth\n";
        c.next();                 // proactively pass the request on
        std::cout << "after: auth\n";
    });
    chain.use([](MiddlewareChain& c) {
        std::cout << "before: logging\n";
        c.next();
        std::cout << "after: logging\n";
    });
    chain.use([](MiddlewareChain&) {
        std::cout << "final handler\n";
        // no c.next() call; the chain naturally ends here
    });
    chain.next();
}
```

Compile and run (GCC 16.1.1):

```sh
$ g++ -std=c++23 -O2 -Wall chain_proxy.cpp -o chain_proxy
$ ./chain_proxy
=== proxy/middleware chain ===
before: auth
before: logging
final handler (no proceed -> chain stops)
after: logging
after: auth done
```

Look at the order of this output — all the `before`s go in layer by layer, it turns around at `final handler`, and then all the `after`s come back out layer by layer. That's the "onion": the request travels from the outside in, the response travels from the inside out, and every middleware can naturally do both its "before" and "after" work. None of the three earlier styles could do this.

### Want to short-circuit? Just don't call `next()`

The onion model's most practical feature is **short-circuiting**: a middleware whose authentication failed simply doesn't call `c.next()`, the chain stops right there, and the middlewares behind it (the business logic) are never triggered. Let's verify:

```cpp
chain.use([](MiddlewareChain& c) {
    std::cout << "A: 认证失败,不调 next\n";
    // deliberately not calling c.next() -> the chain stops here
    (void)c;
});
chain.use([](MiddlewareChain&) {
    std::cout << "B: 这一行不该出现\n";
});
chain.next();
```

The run:

```sh
=== middleware can SHORT-CIRCUIT by not calling proceed ===
A: 认证失败,不调 next
```

B never showed up. **The short-circuit is achieved by "doing nothing" — just don't call `next()`. No `return false`, no `break`; the control flow is clean to the point of being almost implicit.** That is the onion model's biggest expressiveness advantage over the vector dispatcher: a middleware can decide the whole chain's fate based on its own judgment (did auth pass, did the rate limit blow), and that judgment lives entirely inside the middleware — the dispatcher knows nothing about it.

::: warning Don't treat the index_ cursor as a cure-all
That `index_` cursor in the onion model hides a trap: **it is one-shot**. After a `MiddlewareChain` has run once, `index_` is already pressed against `middlewares_.size()`, and further calls to `next()` do nothing at all. If you want to use the same chain for a second request, you must first reset `index_` back to 0 (the `MiddlewareChain` above exposes no reset — you'd have to add one yourself). This differs from the pointer chain — the pointer chain is stateless (every `handle` starts from the head), while the onion model is stateful (the cursor advances). Web frameworks usually solve this by "new-ing up a chain per request", but you need to know this statefulness exists, or when you reuse a chain you'll be baffled by "why won't the chain run".

The sneakier trap: **if the same middleware calls `c.next()` twice, `index_` advances twice**, and the order of the middlewares behind it falls apart. The compiler cannot catch this kind of reentrancy error; only discipline can. So as good as the onion model is, the rule "each middleware calls next() exactly once, appropriately" must hold.
:::

## The variants, and when to use which

At this point we have accumulated three main styles (pointer chain / vector dispatcher / middleware onion), plus a few variants that only pay off in special scenarios. Let's lay out where each fits:

| Style | Core mechanism | Fits | Doesn't fit |
|---|---|---|---|
| `next_` pointer chain | each node holds `next_`, handle-or-forward | few nodes, mostly static chains, textbook CoR | chains assembled dynamically, nodes added/removed |
| `std::vector` dispatcher | one collection dispatched uniformly, `can_accept` decides | pipelines, broadcast, chains whose shape must be visible at a glance | needs before/after logic, needs to decide continuation from results |
| Middleware onion | `std::function` + `index_` cursor, nodes hold a `next` reference | web middleware, needs before/after/short-circuit | scenarios where a simple check suffices (overkill) |

Beyond these three, several more variants serve narrower demands:

**Strategy chain (Chain + Strategy)**: a node is no longer a class but a `std::function<bool(const Request&)>`. The upside is not having to write a class per node; the downside is `std::function`'s type-erasure overhead (possible heap allocation, indirect calls), plus readability dropping as the logic scatters across a pile of lambdas. Fits rule-engine scenarios with many nodes of short logic.

**Tree-shaped chain**: the request doesn't travel in just one direction but is broadcast to multiple child nodes (GUI event bubbling being the classic case). It extends the chain from a one-dimensional line into a tree. We won't expand on it in this article, but when you see "events traveling from the root window toward the child controls", that's a tree-shaped chain.

**Circular chain**: the tail connects back to the head, forming a closed loop. Naturally suited to schedulers and round-robin polling — scenarios of "keep passing until a condition is met". **It must have a man-made termination condition, or it's an infinite loop** — that's a hard constraint, no negotiating.

**Rollback-capable chain of responsibility**: the chain supports not only forward passing — when a node fails, it can also **roll back along the chain**, executing the compensation actions of the earlier nodes. This is the model of database transactions and distributed Sagas. In essence it is no longer plain "request passing" but a bidirectional chain of "do forward + undo backward".

## Pitfall warning: don't be fooled by "async chain of responsibility" examples

::: warning A widely circulated "async chain of responsibility" example is actually serial
In Chinese-language material on the chain of responsibility you often run into an "async chain of responsibility" example, which roughly wraps each handler into a function returning `std::future` and runs them with `std::async`:

```cpp
class AsyncChain {
public:
    using Handler = std::function<std::future<void>()>;
    void add(Handler h) { handlers_.push_back(std::move(h)); }

    void run() {
        std::future<void> fut = std::async(std::launch::async, [this] {
            for (auto& h : handlers_) {
                h().get();   // <-- the key point: .get() here blocks waiting for the current one to finish
            }
        });
        fut.get();
    }
private:
    std::vector<Handler> handlers_;
};
```

This code **is called "async", but it never runs the handlers concurrently**. The problem is the `h().get()` inside the loop — `.get()` **blocks**: it waits for the current handler's future to finish before returning, and only then does the loop enter the next round. In other words, the handlers execute **serially, one after another**; `std::async` merely tosses each handler onto another thread and then immediately blocks waiting for it to finish. That's nearly indistinguishable from calling the handlers in sequence directly, except you've added thread-switching overhead on top.

Let's verify this judgment in the compiler — two handlers, each sleeping 300 ms. If truly concurrent, total elapsed time should be close to 300 ms; if serial (like the code above), close to 600 ms:

```sh
$ g++ -std=c++23 -O2 -Wall chain_async.cpp -o chain_async
$ ./chain_async
Step 1 start
Step 1 done
Step 2 start
Step 2 done
--- total elapsed: 600 ms (serial ~600, concurrent ~300)
```

600 ms. **Measured: it is serial.** So don't be fooled by the example's name — it's called an "async chain of responsibility", but it does not run the handlers on the chain concurrently. A genuinely async chain should advance handlers **simultaneously** on separate futures (for instance, call `h()` first to collect all the futures, then `.get()` them together), or simply use coroutines (`co_await`) to express "wait for this step to finish". If some resource recommends the code above to you as "the async version of the chain of responsibility", remember its actual behavior is serial — don't adopt it as a concurrency scheme.
:::

## Summary

Let's walk the whole evolution path once more:

| Stage | Approach | Why it still isn't enough |
|---|---|---|
| Caller hard-codes the order | a long run of `if (!step) return;` | handler set and caller logic kneaded together, violates open-closed, no runtime reassembly |
| `next_` pointer chain | each node holds `next_`, stops when `process` returns true | caller decoupled, but nodes are coupled; inserting a node means relinking; cycles come easily |
| `std::vector` dispatcher | one collection managed uniformly, `can_accept` decides | chain gathered, but nodes lose forwarding autonomy; broadcast vs first-match-wins is your call |
| Middleware onion | `std::function` + `index_`, nodes hold a `next` reference | most expressive, but the chain is stateful and the cursor is one-shot — overkill for small jobs |

Note down these key conclusions:

- The chain of responsibility exists to **decouple the sender of a request from its receiver**: the sender knows nothing about who is on the chain or in what order — it just drops the request at the head, and the chain itself decides where the passing stops. GoF's intent in one line is "give multiple objects a chance to handle the same request, chain them together, until one of them handles it".
- The classic pointer chain cures the "every subclass must remember to forward" pit with the **"template method + Chain of Responsibility"** combination: **a non-virtual `handle` welds the forwarding skeleton in place, and a pure virtual `process` answers only "did I handle it"**. This is the easiest point to get wrong when writing a chain of responsibility; with the split done right, the forwarding logic is written exactly once.
- The pointer chain is less decoupled than you think — **it moves the coupling from "caller ↔ handler" to "node ↔ node"**: inserting a middle node requires relinking pointers, the chain's shape is scattered across every `next_`, and mutual `shared_ptr` ownership invites cycle leaks. Where nodes come and go dynamically, switch to the vector dispatcher.
- Keep the semantic difference between the vector dispatcher and the pointer chain straight: **broadcast (iterate over all matches) vs first-match-wins (break at the first taker)** — it's purely a question of whether the `dispatch` loop has a `break`, and the companion project uses broadcast.
- The middleware onion (`std::function` + cursor) is the most expressive style — **before/after/short-circuit, it does them all** — but it is stateful (the cursor is one-shot). It fits web-pipeline scenarios where "every node needs both before/after work and the ability to short-circuit"; don't crack a walnut with a sledgehammer on a simple pipeline.
- **That widely circulated "async chain of responsibility" example is serial** — the loop's `h().get()` blocks waiting for each handler to finish, so the handlers never run concurrently. A real async chain needs coroutines, or collecting all the futures first and then calling `.get()` — don't copy that serial example.

::: tip A companion compilable project
The `Message` / `Handler` / `HandlerChain` from this article (the broadcast-style vector dispatcher with `DiskHandler` / `ConsoleHandler` / `GuiHandler`) has a complete CMake project in this repository — clone it and it runs in one shot: [ResponsibilityChain / OutputHandler](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/tree/main/code/volumn_codes/vol4/design-patterns/ChainOfResponsibility). The version in the repo uses broadcast semantics (iterate over all handlers, no break); you can casually change it to first-match-wins (`break`) to feel out the difference between the two semantics.
:::

## References

- [cppreference: `std::function`](https://en.cppreference.com/w/cpp/utility/function) (since C++11, the type-erasure vehicle in the middleware onion)
- [cppreference: `std::shared_ptr`](https://en.cppreference.com/w/cpp/memory/shared_ptr) (since C++11, how `next_` nodes are held in the pointer chain)
- [cppreference: `std::future` / `std::async`](https://en.cppreference.com/w/cpp/thread/async) (since C++11, the concurrency primitives involved in async chains; note that `.get()` blocks)
- GoF, *Design Patterns: Elements of Reusable Object-Oriented Software* — the original definition of the Chain of Responsibility pattern (Intent: avoid coupling the sender of a request to its receiver)
- Companion compilable project: [ResponsibilityChain](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/tree/main/code/volumn_codes/vol4/design-patterns/ChainOfResponsibility)
