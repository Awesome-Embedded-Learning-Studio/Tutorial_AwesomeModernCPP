---
title: "The Reactor pattern: wrapping epoll into an event loop + callbacks, and why it's 'synchronous non-blocking'"
description: "Starting from the scattered epoll if/else of piece 2, this piece explains the Reactor pattern thoroughly (the POSA2 four roles: Handle / Demultiplexer / Event Handler / Initiation Dispatcher), how epoll_wait serves as the Synchronous Event Demultiplexer, the counterintuitive point of 'synchronous non-blocking', and the essential contrast between Reactor (ready notification) and Proactor (completion notification) — laying the load-bearing beam for Boost.Asio simulating Proactor with Reactor later, and for the later-added Windows IOCP"
chapter: 8
order: 3
platform: host
difficulty: intermediate
cpp_standard: [20, 23]
reading_time_minutes: 10
prerequisites:
  - "Linux socket foundations: the server five steps, RAII, and the C10K cost of thread-per-connection"
  - "epoll: Linux I/O multiplexing — from poll's bottleneck to the interest list and ready list"
related:
  - "Boost.Asio intro: from sync to async"
  - "Windows IOCP: completion ports and the Proactor model (to be added)"
tags:
  - host
  - cpp-modern
  - intermediate
  - 网络编程
  - 异步编程
translation:
  source: documents/vol8-domains/networking/03-reactor-pattern.md
  source_hash: 75310342b609b7747b3fb8f896485305280b6d432e4663b9766246081f9625eb
  translated_at: '2026-09-26T04:36:08+00:00'
  engine: anthropic
  token_count: 4600
---

# The Reactor pattern: wrapping epoll into an event loop + callbacks, and why it's 'synchronous non-blocking'

In the previous piece we wrote an echo server with epoll, and one thread can now watch a whole pile of fds. But look back at that event-handling code — it looks like this:

```cpp
for (int i = 0; i < n; ++i) {
    int fd = evs[i].data.fd;
    if (fd == lfd) {
        // logic to accept a new connection ...
    } else {
        // logic to echo an existing connection ...
    }
}
```

With only two kinds of fd right now (listening fd + connection fd), the `if/else` still holds up. But the moment your server has to handle "listening for new connections", "timers", "signals", "Unix domain sockets", or even "pipe notifications" all at once, this scattered `if/else` bloats into a tangled mess — every new kind of fd means editing the event loop's core code. We need a **structure**: decouple "the event loop" from "the handling logic for each kind of fd", so that adding a new fd type never touches the core. That structure is the **Reactor pattern**.

## What Reactor is: the POSA2 four roles

Reactor is the classic name *Pattern-Oriented Software Architecture, Volume 2* (POSA2) gives to event-driven I/O. It has four roles:

| Role | What it is | In this piece |
|---|---|---|
| **Handle** | A kernel identifier for an I/O resource | fd (socket, timerfd, eventfd…) |
| **Synchronous Event Demultiplexer** | Blocks waiting on a set of Handles until one of them is ready | `epoll_wait` (the one piece 2 tore open thoroughly) |
| **Event Handler** | A callback interface for "what to do when a given fd is ready" | `handle_event(fd, events)` |
| **Initiation Dispatcher** = **the Reactor itself** | Keeps the fd→handler registry, runs the event loop, and dispatches ready events to the matching handler | our `EventLoop` class |

The full picture:

```mermaid
flowchart TD
    APP["App: Concrete Event Handlers<br/>(listen handler / echo handler / timer handler ...)"] -->|"register: fd + events of interest"| R["Reactor / Initiation Dispatcher<br/>(fd→handler registry + event loop)"]
    R -->|"epoll_wait blocks waiting for readiness"| DM["Synchronous Event Demultiplexer<br/>(epoll_wait)"]
    DM -->|"returns the ready fd + events"| R
    R -->|"look up by fd, dispatch to the matching handler"| APP
```

The key is **decoupling**: the Reactor itself only does "registry + loop + dispatch" — it **neither knows nor cares** how a particular fd is actually handled; that's the handler's business. Adding a new fd type? Write a new handler, register it with the Reactor, and the event loop doesn't change a single line. That's the value of the pattern.

## Refactoring piece 2's epoll echo into a Reactor

Piece 2's `epoll_lt.cpp` is actually **already a minimal Reactor** — the roles just aren't spelled out separately. Let's refactor it into the shape the pattern calls for, so the structure stands out clearly:

```cpp
// Event handler interface: handle_event is called when an fd is ready
class EventHandler {
public:
    virtual ~EventHandler() = default;
    virtual void handle_event(uint32_t events) = 0;
    virtual int fd() const = 0;
};

// The Reactor itself: registry + event loop
class Reactor {
public:
    void add(int fd, uint32_t events, std::unique_ptr<EventHandler> h) {
        epoll_event ev{}; ev.events = events; ev.data.fd = fd;
        ::epoll_ctl(ep_, EPOLL_CTL_ADD, fd, &ev);
        handlers_[fd] = std::move(h);          // fd → handler registry
    }
    void run() {
        for (;;) {
            int n = ::epoll_wait(ep_, evs_.data(), evs_.size(), -1);  // Demultiplexer
            for (int i = 0; i < n; ++i) {
                int fd = evs[i].data.fd;
                handlers_[fd]->handle_event(evs[i].events);           // dispatch
            }
        }
    }
private:
    int ep_{::epoll_create1(0)};
    std::array<epoll_event, 128> evs_;
    std::unordered_map<int, std::unique_ptr<EventHandler>> handlers_; // registry
};
```

Now "listening" and "echo" are two independent handlers, each implementing `handle_event`, registered into the Reactor. Want to add a timer? Write a `TimerHandler`, register it, and `Reactor::run` stays untouched. **The core loop and the business logic are cleanly separated** — that's where Reactor beats scattered `if/else`.

Piece 2's `epoll_lt` (and that ET-loses-data counterexample) is the flesh and blood of this pattern: the Reactor pattern is only a skeleton — ET/LT, non-blocking, loop-read-to-EAGAIN, all these **engineering-correctness details live in the handlers**. The pattern doesn't guarantee correctness for you; it only guarantees structure.

## "Synchronous non-blocking": a counterintuitive name

Reactor is often called a "**synchronous non-blocking**" event-driven design, a name that looks contradictory at first glance — "synchronous and non-blocking, both at once?". Unpack it and it clicks:

- **Non-blocking**: every fd is `O_NONBLOCK` (piece 2 covered this: mandatory for ET, recommended for LT as well). `read`/`write` never stalls the thread.
- **Synchronous**: the event loop invokes handlers **in the same thread, synchronously**. `epoll_wait` returns ready events synchronously, and each handler runs to completion before control returns to the loop — **no cross-thread hops, no "fire a separate callback once the operation completes"**. The whole server runs its event loop in one (or a few) threads, processing events in order.

So "synchronous" refers to the **processing model** (single-threaded, sequential dispatch), not to "I/O blocking". And its difference from true "asynchronous (Proactor)" is exactly the core of the next section.

## Reactor vs Proactor: ready notification vs completion notification

This is the one pair of concepts in network programming most worth clearing up once and for all, and it's also the **load-bearing beam** for the rest of this series:

- **Reactor (ready notification)**: the kernel tells you "this fd **is readable now**" (the state is ready), and **you do the `read` yourself** to haul the data. How fast you haul, how much you haul — your business. Linux's epoll is a Reactor. This entire piece is one.
- **Proactor (completion notification)**: you tell the kernel "**read this fd's buffer for me**", the kernel **reads the data for you**, and when it's done it **notifies you: "finished, the data is here"**. Windows' **IOCP** is a native Proactor.

```mermaid
flowchart LR
    subgraph Reactor["Reactor (epoll / Linux) — ready notification"]
        R1["kernel: fd is readable"] --> R2["you: read() and haul it yourself"]
    end
    subgraph Proactor["Proactor (IOCP / Windows) — completion notification"]
        P1["you: issue the read request"] --> P2["kernel: reads it for you"] --> P3["notifies you: done"]
    end
```

The essence of the difference: **who executes that actual `read`/`write` syscall**. In Reactor it's you (the `read` lives in the handler); in Proactor it's the kernel (you only issue the request). The Linux kernel **has no native general-purpose Proactor interface** (io_uring is half of one — dedicated piece later), so a Linux networking library that wants to offer a Proactor-style API can only **simulate it with a Reactor** — which is exactly what the next piece, **Boost.Asio**, does: what it exposes upward is a Proactor style (`async_read` registering a completion callback), but underneath, on Linux, it's implemented with epoll (Reactor). Only on Windows does it land on native IOCP.

This "Proactor implemented with a Reactor" load-bearing beam welds the series' three pure-Linux foundation pieces (socket → epoll → Reactor) together with the Asio that follows and the later-added Windows IOCP: you've hand-written a Reactor by now; Asio tells you "I wrapped that Reactor you hand-wrote into a Proactor interface", and IOCP fills in "the native half of Proactor".

## Graceful shutdown: the Reactor's engineering close-out

A usable Reactor server is still missing one last piece: **graceful shutdown**. You can't just `Ctrl+C` and kill the process — in-flight connections get brutally `close`d, and the peer receives an RST. The right way:

1. A **signal handler** changes `epoll_wait`'s timeout from `-1` (block forever) to a short timeout, or wakes it with an eventfd.
2. **Stop accepting new connections** (remove the listening fd from the interest list).
3. **Drain**: finish sending the remaining data to existing connections, then wait for them to close naturally or time out.
4. Finally, exit the loop.

This part involves signals cooperating with the event loop (a signal can interrupt `epoll_wait` at any moment, returning `EINTR`), plus the lifecycle question of "how do in-flight handlers wrap up" — plenty of pitfalls here (our old notes contain one real bug where "a blocking accept made `join` hang"). These **engineering details are exactly what this series' Lab 0 trains in its MS4 (graceful shutdown) adversarial acceptance**: after SIGTERM, no hanging `join`, no leaked fd, no RST to the peer.

## Wrap-up

- **The Reactor pattern** decouples "the event loop" from "fd-handling logic": the Reactor itself manages registry + loop + dispatch, handlers manage the concrete handling. A new fd type means writing a handler, not touching the core.
- **The POSA2 four roles**: Handle (fd) / Synchronous Event Demultiplexer (`epoll_wait`) / Event Handler (`handle_event`) / Initiation Dispatcher (the Reactor itself). Piece 2's `epoll_lt.cpp` is a minimal Reactor.
- **"Synchronous non-blocking"**: non-blocking means every fd is `O_NONBLOCK`; synchronous means single-threaded, sequential event dispatch — not "I/O blocking".
- **Reactor (ready notification) vs Proactor (completion notification)**: in the former the kernel announces "readable" and you read yourself (epoll); in the latter the kernel reads for you and then announces completion (IOCP). The essence of the difference is **who executes that read/write syscall**.
- **The load-bearing beam**: Linux has no native Proactor, so networking libraries (Boost.Asio) simulate Proactor with Reactor (epoll); Windows IOCP is a native Proactor. This is what welds the three pure-Linux foundation pieces to the later Asio/IOCP.
- **Graceful shutdown** is the Reactor's engineering close-out (signal → stop accepting → drain → exit), and the adversarial acceptance for Lab 0's MS4.

With this, the three Linux networking foundation pieces (socket → epoll → Reactor) are complete. Next up we first add **io_uring** — Linux's new completion-driven primitive, completing the "backend tour" (epoll ready-driven + io_uring completion-driven); only after that do we enter **Boost.Asio**, where you'll see it wrap this hand-written epoll/Reactor into a cross-platform Proactor-style API, upgrading this piece's "event loop + callback" into "issue an async operation + register a completion callback".

## References

- [POSA2 — Reactor pattern (Doug Schmidt)](https://www.dre.vanderbilt.edu/~schmidt/PDF/Reactors.pdf) — the original Reactor pattern paper, the source of the four-role definition
- [Reactor - An Object Behavioral Pattern for Demultiplexing...](https://www.dre.vanderbilt.edu/~schmidt/PDF/POSA2.pdf) — the relevant POSA2 chapter
- [Boost.Asio — The Proactor Design Pattern: Concurrency Without Threads](https://www.boost.org/doc/libs/1_91_0/doc/html/boost_asio/overview/core/async.html) — Asio's own account of "Proactor implemented with Reactor", the official statement of the load-bearing beam
- [epoll: the I/O multiplexing foundation (the previous piece in this series)](./02-epoll-io-multiplexing.md) — Reactor's Demultiplexer is epoll
- io_uring: Linux's new completion-driven primitive (next piece, to be written) — completes the backend tour; after that comes Boost.Asio, wrapping this piece's Reactor into a Proactor-style API
