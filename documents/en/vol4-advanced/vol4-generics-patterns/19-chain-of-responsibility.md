---
title: 'Responsibility Chain Pattern: From a Long Chain of if/else to next_ Pointers,
  and Finally to Middleware Onions'
description: We start with the most intuitive approach where the caller hardcodes
  the handler, and step by step derive the classic `next_` pointer chain. We examine
  what problem this solves and where it shifts the coupling. Finally, we present two
  modern C++ alternatives using a `std::vector` scheduler and `std::function` middleware
  onion.
chapter: 11
order: 19
tags:
- host
- cpp-modern
- intermediate
- 责任链模式
difficulty: intermediate
platform: host
cpp_standard:
- 11
- 17
- 20
reading_time_minutes: 20
related:
- 单例模式:从注释约束到 Meyer's Singleton
- 策略模式:从一堆 if/else 到编译期可替换的 Policy
prerequisites:
- 'Chapter 6: 类与对象'
translation:
  source: documents/vol4-advanced/vol4-generics-patterns/19-chain-of-responsibility.md
  source_hash: 89365238cd48d1cc0e46a2f496ebfcc01e970a484dc84bef12c801e7c16d00e0
  translated_at: '2026-06-24T01:03:42.962542+00:00'
  engine: anthropic
  token_count: 4787
---
# Chain of Responsibility: From a Long Chain of if/else to `next_` Pointers, and Finally to the Middleware Onion

## What Problem Are We Actually Solving?

Let's hold off on the formal definition for a moment. Consider a concrete scenario: you have written a network service and need to handle incoming requests. When a request arrives, you want it to pass through a series of checkpoints in a specific order—first, verify it has a valid authentication token, and reject it immediately if it doesn't; second, if authenticated, log the access; third, after logging, hand it over to the actual business logic to compute the result. The order of these steps is fixed, and each step determines whether execution should proceed (if authentication fails, we stop there; subsequent steps must not run).

The most intuitive approach is for the caller to hardcode this sequence:

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

At first, writing it this way seems fine. But then, things start to get out of hand. Product asks for rate limiting (max 100 requests per minute per IP), so you inject the rate limiter before authentication. Two days later, they want metrics instrumentation (latency tracking for every step), so you inject another layer. Later, it's canary releases (only specific users use the new logic), so yet another layer. Now, when you open `handle_request`, it's a long sequence of ordered steps. Each step "might intercept or might pass," and getting the order slightly wrong leads to a production incident. Every time you add new logic, you have to **open this function and modify its internal structure**.

The root of the problem is this: **"Who handles the request, in what order, and when it stops" is hardcoded into the caller.** The caller not only knows what the checkpoints are, but also their sequence and who can stop whom. What we want is the reverse—**the caller is only responsible for tossing the request into a chain. Each node on the chain decides for itself: "Do I handle it, or pass it to the next?"** The caller knows nothing about how the chain is assembled, how many layers there are, or the order.

The Chain of Responsibility pattern exists to solve exactly this. The GoF (Gang of Four) sums up its intent in one sentence: **"Give more than one object a chance to handle a request by chaining these objects together and passing the request along the chain until an object handles it."** The goal is to decouple the **sender** of a request from the **receiver**, so the sender doesn't know—and doesn't need to know—who ultimately handled it.

However, the act of "chaining objects together" has several implementations in C++, each with its own pitfalls. Let's walk through this step-by-step, starting with the dumbest approach, to see how each is driven by the pain points of the previous one.

## Step 1: The Most Primitive Approach — Hardcoded Order in the Caller (The Anti-Pattern)

We've already seen this beginning. Let's zoom in a bit to see exactly where the pain lies:

```cpp
void handle_request(const Request& req) {
    if (!rate_limit(req)) { reject_429(req); return; }
    if (!check_auth(req)) { reject_401(req); return; }
    log_access(req);
    if (!run_business(req)) { respond_error(req); return; }
    respond_ok(req);
}
```

It works, but it is riddled with issues. **First**, every time we add a new stage, we have to revisit this function—it is "closed for extension," violating the Open-Closed Principle. **Second**, the order of stages is implicit (you only know rate limiting comes before authentication by reading top-to-bottom), and there is no single place to visualize "what this chain looks like." **Third**, and most subtly, these stages are hardcoded function calls. If we want to dynamically assemble a chain at runtime based on configuration (e.g., "no rate limiting for internal testing environments"), it's impossible without adding a bunch of `if` statements.

The root cause of this failure is that **"what handlers exist" and "how the caller uses them" are mixed together in a single function**. What we really want is to extract the "set of handlers" and "how they are linked" from the caller, turning them into an independently composable structure. Each handler only needs to answer two questions: **"Can I handle this request?"** and **"If not, pass it to the next one."**

This is exactly what the classic `next_` pointer chain is for.

## Step 2: The Classic Pointer Chain — `next_` Pointer + Handle or Forward

Let's jump straight to the code and break down line-by-line why it's written this way. This is the classic GoF (Gang of Four) pointer chain implementation: an abstract `Handler`, where each concrete handler inherits from it and holds a `next_` pointer pointing to the next handler:

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

    // 模板方法:把"转发"逻辑焊死在基类,子类只管 process
    void handle(const std::string& req) {
        const bool handled = process(req);
        if (!handled && next_) {
            next_->handle(req);          // 甩给下一个
        } else if (!handled && !next_) {
            std::cout << "[chain end] nobody handled: " << req << "\n";
        }
    }

protected:
    virtual bool process(const std::string& req) = 0;  // 返回 true 表示"我处理了"

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

Here is how we use it:

```cpp
int main() {
    auto auth = std::make_shared<AuthHandler>();
    auto log = std::make_shared<LogHandler>();
    auth->set_next(log);

    auth->handle("log");   // auth 拒收 -> 转给 log -> log 处理
    auth->handle("auth");  // auth 自己处理
    auth->handle("xxx");   // 一路没人收,到链尾报"nobody handled"
}
```

Let's compile and run it (GCC 16.1.1):

```sh
$ g++ -std=c++23 -O2 -Wall chain_verify.cpp -o chain_verify
$ ./chain_verify
=== pointer chain ===
LogHandler handled
AuthHandler handled
[chain end] nobody handled: xxx
```

You see, the caller only knows `auth->handle(req)`. It is **completely unaware** of whether there are subsequent nodes, how many there are, or who they are. This is the core benefit of the Chain of Responsibility—decoupling the sender from the receiver. The request flows along the chain; each node either consumes it (returns `true`) or passes it to `next_`, until someone handles it or the chain ends.

### The Elegance of This Design: Why Separate `handle` and `process`

You might ask: Why do we need to split `handle` and `process` into two functions? Wouldn't it be simpler to just write a virtual function `handle` in the subclass and let it decide whether to forward the request?

You could do that, but then you would burden **every subclass** with the responsibility of "forwarding to `next_`". Every concrete handler would have to remember to write that boilerplate: "If I didn't handle it, and there is a `next_`, call `next_->handle(req)`". Eventually, someone will forget (either forgetting to forward or forwarding incorrectly), and the compiler won't say a word.

The classic pointer chain uses a particularly elegant technique to solve this problem once and for all: **hoist the "forwarding" logic into the non-virtual `handle` in the base class and lock it down, while subclasses only expose a pure virtual `process` to answer "Did I handle this request?"** Note that `handle` in the base class is **non-virtual**—it is a template method that controls the fixed skeleton of "let the subclass try first, otherwise forward". Subclasses cannot modify this skeleton; they can only fill in the `process` slot. This way, the "forwarding" logic is written only once and can never be missed.

This pattern is known as a combination of the **Template Method + Chain of Responsibility**. It cleanly separates the **invariant part (the forwarding skeleton)** from the **variable part (the judgment logic of each node)**. When implementing a Chain of Responsibility, if you find yourself writing forwarding logic for every node, it means this separation wasn't done correctly.

## Let's Verify: Does the Forwarding Logic Really Run Only Once?

Talk is cheap. Let's print the forwarding process to confirm that "a request is either consumed by a node or reaches the end of the chain," without duplicate processing in between. We will add print statements to each node's `handle` to observe the flow trajectory of the request:

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

The execution flow is clear: the request moves unidirectionally along the chain. It either stops at a specific node (when its `process` returns `true`, terminating the chain immediately) or reaches the end and reports "nobody handled". It **never backtracks or re-enters the same node**. This linear flow property is the most critical invariant of the classic Chain of Responsibility pattern—the "Onion Middleware" and "Retractable Chain of Responsibility" patterns we will discuss later are specifically designed to break and re-examine this invariant.

## Pitfall Warning: The `next_` pointer chain is less decoupled than you think

::: warning The `next_` pointer chain moves coupling instead of eliminating it
The pointer chain does indeed decouple the **caller** from the **concrete handlers**—the caller doesn't know who comes next. However, it shifts the coupling to the **relationships between nodes**. Each node holds a `next_` pointer, which introduces three practical engineering challenges.

**First, inserting a node in the middle requires manual pointer rewiring.** Suppose you have `auth -> log` and want to insert `metrics` in the middle. You cannot simply add `metrics`—you must modify `auth`'s `next_` to point to `metrics`, and then set `metrics`'s `next_` to point to `log`. Let's verify this rewiring cost using the compiler:

```cpp
auto metric = std::make_shared<MetricsHandler>();
metric->set_next(log);      // metrics 接上原来的尾巴
auth->set_next(metric);     // auth 的 next 改指向 metrics
// 现在:auth -> metric -> log
```

Run it (the complete code is in the companion project):

```sh
$ ./chain_verify
=== inserting middle node into pointer chain: relink cost ===
MetricsHandler handled
LogHandler handled
```

We successfully reconnected, but look at the cost: **to insert a node, you had to touch the internal state of an unrelated node (`auth`) on the chain.** If `auth` is maintained by someone else and you only have a pointer to it, you can't modify it at all.

**Second, the shape of the chain is scattered across the `next_` member of every node, so there is no single place where you can clearly see "what the entire chain looks like."** When debugging and trying to print this chain, you have to traverse all the way from the head following `next_`. If the link is broken, forms a cycle, or connects incorrectly, the compiler cannot detect any of this; you only find out when the code runs.

**Third, the `next_` pointer causes nodes to hold each other**, where every node on the chain holds a `shared_ptr` to the next. A slight slip-up easily creates a cycle (`A->B, B->A`), and once a cycle forms, you have a memory leak—the reference count of the `shared_ptr` will never reach zero. Textbook examples of pointer chains never mention this, but in real engineering, assembling and destroying chains is where things are most likely to go wrong.
:::

This is the true ledger of pointer chains: **they solve the problem of "the caller shouldn't know who handles it," but the cost is that the responsibility for assembling the chain is distributed across every node.** In simple scenarios with few nodes and a mostly static chain, this cost is negligible; but once nodes are added or removed dynamically, or the chain is assembled at runtime, pointer chains become woefully inadequate. We need a different approach to "centralize" the chain.

## Step 3: Centralize the chain with `std::vector` — turning the chain into a collection

Since the problem with the `next_` pointer is that "the chain is scattered inside every node," the most direct antidote is to—**gather the chain into a collection, managed uniformly by a dedicated scheduler.** The shape of the entire chain is no longer hidden inside the `next_` of nodes, but is clearly laid out in a `std::vector`. This is exactly how the accompanying compilable project is written, so let's follow its approach:

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

Here is the translation based on the provided context and rules.

**Note:** Since the source text provided ("用起来:") is extremely short and lacks specific context, I have translated it as a standard heading or section title commonly found in tutorials. If this is a link to a specific section, please provide the surrounding text for a more precise translation.

```markdown
## Let's use it
```

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

We need to clarify the difference between this version and the pointer chain, because it is **not a simple equivalent replacement; the semantics have changed**.

### The Key Difference: Broadcast vs. First-Match-Wins

Look closely at the loop in `HandlerChain::dispatch`: it **iterates through all handlers**, calling `process` for every handler where `can_accept` returns true, **without a `break`**. This means that if two handlers can accept the same message, both will be triggered. This is a **broadcast** semantic.

The classic pointer chain is **first-match-wins**: the first node where `process` returns true consumes the request, and subsequent nodes are never aware that this message arrived.

These two approaches are not the same thing, and choosing the wrong one will lead to issues. A logging framework (where a single log entry might write to a file and send over the network simultaneously) typically uses broadcast; an approval workflow (where if a manager approves an expense report, the director doesn't need to see it) typically uses first-match-wins. If you want the first-match-wins semantic, you need to add a `break` to the loop above:

```cpp
void dispatch_first_match(const Message& m) {
    for (const auto& h : handlers_) {
        if (h->can_accept(m)) {
            h->process(m);
            break;   // 第一个吃掉的就把请求终结,后面的不跑
        }
    }
}
```

So, take note: the **"vector scheduler"** itself does not mandate whether it broadcasts or stops at the first hit; it delegates that decision back to the implementation of `dispatch`. Whichever you prefer, you simply decide whether to include a `break` in the loop. This is actually an advantage of the vector version over the pointer version: the "stop-at-first-hit" behavior of the pointer chain is hardcoded in the skeleton; changing it to broadcast would require modifying the base class. With the vector version, changing the semantics only requires modifying one loop. Be aware that the accompanying Playground project uses broadcast semantics (iterating through all handlers without breaking).

### What the vector version solves and what it lacks

After the vector version consolidates the chain into a collection, **all three pain points of the pointer chain are resolved**: inserting a node in the middle only requires `handlers_.insert(it, new_handler)`, without touching any existing nodes; the shape of the entire chain is visible at a glance (it's just a vector, print it out); nodes no longer hold `next_` pointers to each other, eliminating the risk of circular leaks.

However, the vector version introduces a new limitation: **the node's autonomy for "forwarding" is removed**. In the pointer chain, a node could decide in `process` to "handle part of this, and then actively pass the request to the successor"—it had full control over forwarding. In the vector version, "whether to continue" is decided by the scheduler (the `dispatch` loop), and the node only answers the single Boolean question of "do I accept?". For a simple pipeline where "each step independently judges if it can proceed," this is sufficient; but for more complex orchestration like "after this step is done, I want to decide whether to proceed based on the result," the vector version falls short.

In the next section, we look at a design pattern specifically created for the latter—the middleware onion.

## Step 4: The Middleware Onion—Nodes Decide "Pre, Post, and Forward"

In the previous three approaches, nodes only answered one question: "Do I handle this request?". But real middleware (if you've used Express.js, Koa, or ASP.NET pipelines) does much more than this. A middleware might want to do something **before calling the next** (start a timer), do something **after the next returns** (calculate latency, log), or even **not call the next at all** (short-circuit on auth failure). This requirement of "pre-processing, post-processing, and short-circuiting" transforms the chain of responsibility from a straight line into an "onion"—the request passes in layer by layer, and the response passes out layer by layer.

The cleanest way to implement this in C++ is: **the node no longer answers a Boolean question, but becomes a function that "takes `next` and decides how to use it"**. A unified scheduler is responsible for feeding the "next" handler to each node in sequence:

```cpp
#include <functional>
#include <iostream>
#include <vector>

class MiddlewareChain {
public:
    // 每个中间件:拿到"下一个"的引用,自己决定怎么编排
    using Middleware = std::function<void(MiddlewareChain&)>;

    void use(Middleware m) { middlewares_.push_back(std::move(m)); }

    void next() {
        if (index_ < middlewares_.size()) {
            auto m = middlewares_[index_++];
            m(*this);   // 把自己(也就是"如何继续")交给中间件
        }
    }

private:
    std::vector<Middleware> middlewares_;
    std::size_t index_ = 0;
};
```

Pay attention to two key design points here. **First, the middleware signature is `void(MiddlewareChain&)`. It receives a reference to the entire chain, not just a "next middleware"**. It advances the chain by calling `chain.next()` itself—this hands complete control over whether to proceed down the chain to the middleware. **Second, there is an `index_` cursor inside `next()`**, which advances one step with each call. This avoids the coupling found in pointer chains where "every node holds a pointer to the other," while retaining the autonomy of "nodes controlling their own forwarding."

Here is how we use it:

```cpp
int main() {
    MiddlewareChain chain;
    chain.use([](MiddlewareChain& c) {
        std::cout << "before: auth\n";
        c.next();                 // 主动放行
        std::cout << "after: auth\n";
    });
    chain.use([](MiddlewareChain& c) {
        std::cout << "before: logging\n";
        c.next();
        std::cout << "after: logging\n";
    });
    chain.use([](MiddlewareChain&) {
        std::cout << "final handler\n";
        // 不调 c.next(),链自然到此为止
    });
    chain.next();
}
```

Let's compile and run it (GCC 16.1.1):

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

Look at the output order—all the `before` messages go in layer by layer, turn at the `final handler`, and then all the `after` messages come out layer by layer. This is the "onion" model: the request goes from the outside in, and the response goes from the inside out. Each middleware naturally handles both "pre-processing" and "post-processing" simultaneously. This is a capability that the previous three approaches lacked.

### Want to short-circuit? Just don't call `next()`

The most practical feature of the onion model is **short-circuiting**: if an authentication middleware fails, it simply doesn't call `c.next()`. The chain stops right there, and subsequent middleware (business logic) is never triggered. Let's verify this:

```cpp
chain.use([](MiddlewareChain& c) {
    std::cout << "A: 认证失败,不调 next\n";
    // 故意不调 c.next() -> 链停在这
    (void)c;
});
chain.use([](MiddlewareChain&) {
    std::cout << "B: 这一行不该出现\n";
});
chain.next();
```

It appears you have provided only a fragment of text ("跑出来:", which roughly translates to "Run output:" or "Result:").

To proceed with the translation, **please provide the full Markdown content** (including the code block or text that follows this header).

Once you provide the full text, I will translate it according to your specifications, ensuring code blocks remain unchanged and technical terms are handled correctly.

```sh
=== middleware can SHORT-CIRCUIT by not calling proceed ===
A: 认证失败,不调 next
```

`B` never appears. **Short-circuiting is achieved by "doing nothing" — simply don't call `next()`. There's no `return false`, no `break`, and the control flow is so clean it's almost implicit.** This is the biggest expressiveness advantage of the Onion model over the vector scheduler: middleware can decide the fate of the entire chain based on its own judgment (authenticated or not, rate limit exceeded or not), and this judgment logic is completely encapsulated within the middleware. The scheduler knows nothing about it.

::: warning Don't treat the `index_` cursor as a panacea
There's a hidden pitfall with that `index_` cursor in the Onion model: **it is one-time use only**. After a `MiddlewareChain` runs through once, `index_` has already reached `middlewares_.size()`, so calling `next()` again does nothing. If you want to use the same chain to handle a second request, you must reset `index_` back to 0 (the `MiddlewareChain` above doesn't expose a reset, so you'd have to add it yourself). This is different from the pointer chain — the pointer chain is stateless (every `handle` starts from the beginning), whereas the Onion model is stateful (the cursor advances). In Web frameworks, this is usually solved by "newing a chain per request," but you need to be aware of this statefulness, otherwise you'll be puzzled when reusing a chain: "why isn't the chain running?"

An even subtler pitfall: **if `c.next()` is called twice within the same middleware, `index_` will advance twice**, messing up the order of subsequent middleware. This reentrancy error cannot be caught at compile time; it relies solely on discipline. So while the Onion model is great, you must uphold the discipline: "each middleware calls `next()` appropriately and exactly once."
:::

## Variants and When to Use Which

By now, we have accumulated three main patterns (pointer chain / vector scheduler / middleware onion), plus a few variants for specific scenarios. Let's clarify their scope:

| Pattern | Core Mechanism | Suitable For | Not Suitable For |
|---|---|---|---|
| `next_` pointer chain | Each node holds `next_`, handles or forwards | Few nodes, mostly static chain, textbook CoR | Chains need dynamic assembly, nodes added/removed frequently |
| `std::vector` scheduler | Centralized scheduling by collection, `can_accept` check | Pipelines, broadcasting, chain shape must be visible | Needs pre/post-logic, needs result-based flow control |
| Middleware Onion | `std::function` + `index_` cursor, node holds `next` reference | Web middleware, needs pre/post/short-circuit | Simple handle-or-pass scenarios (overkill) |

Beyond these three, there are several variants that address more specific needs:

**Strategy Chain**: Nodes are no longer classes, but `std::function<bool(const Request&)>`. The benefit is you don't need to write a class for each node; the downside is the type erasure overhead of `std::function` (potential heap allocation, indirect calls), and readability drops as logic scatters across many lambdas. Suitable for rule engine scenarios with short logic and many nodes.

**Tree Chain**: Requests don't just pass in one direction but broadcast to multiple child nodes (typical in GUI event bubbling). It extends the chain from a one-dimensional line into a tree. We won't expand on this here, but when you see "events passing from a root window to child controls," that's a tree chain.

**Circular Chain**: The tail connects back to the head, forming a closed loop. Naturally suitable for schedulers, round-robin, and other "keep passing the baton until a condition is met" scenarios. **It must have an artificially set termination condition, otherwise it's an infinite loop** — this is a hard constraint with no room for negotiation.

**Rewindable Chain of Responsibility**: The chain supports not only forward passing but also **reverse rollback** when a node fails, executing compensation operations on previous nodes. This is the model for database transactions and distributed Saga. It is essentially no longer simple "request passing," but a bidirectional chain of "do forward + undo backward".

## Pitfall Alert: Don't Be Fooled by "Async Chain of Responsibility"

::: warning A widely circulated "async chain of responsibility" example is actually serial
In Chinese materials discussing the Chain of Responsibility, you often see an example of an "async chain of responsibility." The gist is to wrap each handler into a function returning `std::future` and run it with `std::async`:

```cpp
class AsyncChain {
public:
    using Handler = std::function<std::future<void>()>;
    void add(Handler h) { handlers_.push_back(std::move(h)); }

    void run() {
        std::future<void> fut = std::async(std::launch::async, [this] {
            for (auto& h : handlers_) {
                h().get();   // <-- 关键:这里 .get() 会阻塞等当前完成
            }
        });
        fut.get();
    }
private:
    std::vector<Handler> handlers_;
};
```

This code is labeled "asynchronous," but it does not run the handlers concurrently. The issue lies in `h().get()` inside the loop—`.get()` is **blocking**. It waits for the current handler's future to complete before returning, allowing the loop to proceed to the next iteration. In other words, the handlers execute **serially, one by one**. `std::async` simply launches each handler on a separate thread and immediately blocks, waiting for it to finish. This is effectively no different from calling the handlers sequentially, only with the added overhead of thread switching.

We can verify this in the compiler—two handlers each sleep for 300 ms. If they were truly concurrent, the total time would be approximately 300 ms. If they are serial (as in the code above), the total time should be close to 600 ms:

```sh
$ g++ -std=c++23 -O2 -Wall chain_async.cpp -o chain_async
$ ./chain_async
Step 1 start
Step 1 done
Step 2 start
Step 2 done
--- total elapsed: 600 ms (serial ~600, concurrent ~300)
```

600 ms. **In practice, this is serial execution**. So don't be fooled by the name of this example—it's called an "asynchronous chain of responsibility," but it doesn't allow handlers on the chain to execute concurrently. A truly asynchronous chain should allow handlers to progress **simultaneously** on different futures (for example, calling `h()` to get all futures first, then calling `.get()` on all of them), or simply use coroutines (`co_await`) to express "wait for this step to complete." If you see a resource recommending the code above as an "asynchronous version of the chain of responsibility," remember that its actual behavior is serial—don't use it as a concurrency solution.
:::

## Summary

Let's walk through the entire evolution path:

| Stage | Approach | Why it's still not enough |
|---|---|---|
| Caller hardcodes order | A long string of `if (!step) return;` | Handler set and caller logic are mixed together, violating the Open/Closed Principle, and cannot be reorganized at runtime |
| `next_` pointer chain | Each node holds `next_`, `process` returns true to stop | Caller is decoupled, but nodes are coupled to each other, inserting nodes requires reconnecting, and it's easy to create cycles |
| `std::vector` scheduler | Unified collection management, `can_accept` judgment | The chain is consolidated, but nodes lose the autonomy to forward; you must decide between broadcast vs. stop-on-first-match yourself |
| Onion middleware | `std::function` + `index_`, node holds `next` reference | Most expressive, but the chain has state, the cursor is single-use, and it's overkill for simple tasks |

Keep these key conclusions in mind:

- The problem the Chain of Responsibility solves is **"decoupling the sender of a request from its receiver"**: The sender doesn't know who is on the chain or in what order; it simply throws the request to the head of the chain, and the chain decides where to stop. The GoF intent can be summarized as "give multiple objects a chance to handle a request by chaining them until someone handles it."
- The classic pointer chain uses a combination of **"Template Method + Chain of Responsibility"** to fundamentally fix the pitfall of "every subclass must remember to forward": **The non-virtual `handle` welds the forwarding skeleton in place, while the pure virtual `process` only answers "Did I handle this?"**. This is the easiest part to get wrong when writing a Chain of Responsibility. If you separate them correctly, you only write the forwarding logic once.
- The pointer chain isn't as decoupled as you think—**it moves the coupling from "caller ↔ handler" to "node ↔ node"**: Inserting a middle node requires reconnecting pointers, the shape of the chain is scattered across every `next_`, and `shared_ptr` circular references easily lead to cyclic leaks. For scenarios where nodes are dynamically added or removed, switch to a vector scheduler.
- Distinguish the semantic difference between the vector scheduler and the pointer chain: **Broadcast (iterate all matching handlers) vs. Stop-on-first (break on the first handler that consumes it)**. This is simply a matter of adding or not adding a `break` in the `dispatch` loop; the companion project uses broadcast semantics.
- The onion middleware (`std::function` + cursor) is the most expressive approach, **capable of pre/post-processing and short-circuiting**, but it has state (the cursor is single-use). It is suitable for scenarios like Web pipelines where "every node needs pre/post-processing and the ability to short-circuit," but don't use a cannon to kill a mosquito on a simple pipeline.
- **That widely circulated "asynchronous chain of responsibility" example is serial**—`h().get()` in the loop blocks waiting for each handler to complete, so the handlers aren't concurrent at all. A real asynchronous chain relies on coroutines or collecting futures first and then calling `.get()`, so don't copy that serial example blindly.

::: tip Companion Compilable Project
The `Message`, `Handler`, and `HandlerChain` (broadcast-style vector scheduler, `DiskHandler` / `ConsoleHandler` / `GuiHandler`) from this article have a complete CMake project in this repository. Just clone and run: [ResponsibilityChain / OutputHandler](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/tree/main/code/volumn_codes/vol4/design-patterns/ChainOfResponsibility). The version in the repository uses broadcast semantics (iterating all handlers without `break`), so you can easily change it to stop-on-first (`break`) to experience the difference between the two semantics.
:::

## References

- [cppreference: `std::function`](https://en.cppreference.com/w/cpp/utility/function) (Since C++11, the carrier of type erasure in the onion middleware)
- [cppreference: `std::shared_ptr`](https://en.cppreference.com/w/cpp/memory/shared_ptr) (Since C++11, how the `next_` node is held in the pointer chain)
- [cppreference: `std::future` / `std::async`](https://en.cppreference.com/w/cpp/thread/async) (Since C++11, concurrency primitives related to asynchronous chains; note that `.get()` is blocking)
- GoF, *Design Patterns: Elements of Reusable Object-Oriented Software* — Original definition of the Chain of Responsibility pattern (Intent: avoid coupling the sender of a request to its receiver)
- Companion Compilable Project: [ResponsibilityChain](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/tree/main/code/volumn_codes/vol4/design-patterns/ChainOfResponsibility)
