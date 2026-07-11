---
title: "Lab 0: mini Reactor echo server — from epoll to a concurrency-ready event loop"
description: "On the socket/epoll/Reactor foundation of the first four pieces, build a minimal Reactor (epoll event loop + handler registry) by hand, as an echo server that survives concurrency. Split into 4 milestones, each introducing exactly one engineering problem, with adversarial acceptance: multi-concurrency without crashing, ET large-burst without losing data, stop without hanging — failing these is not 'runs', it's 'looks like it runs'"
chapter: 8
order: 4
platform: host
difficulty: advanced
cpp_standard: [20]
reading_time_minutes: 8
prerequisites:
  - "Traditional socket programming: the server five-step and TCP handshake"
  - "epoll: Linux I/O multiplexing"
  - "The Reactor pattern"
tags:
  - host
  - cpp-modern
  - advanced
  - 网络编程
  - 异步编程
---

# Lab 0: mini Reactor echo server — from epoll to a concurrency-ready event loop

> This is a hands-on Lab, not a concept tutorial. The first four pieces (00→03) covered socket, epoll, and the Reactor pattern; in this one you get to stitch them into something that runs. The companion project scaffold is at `code/volumn_codes/vol8-labs/lab0-mini-reactor/`.

## Goal

Implement a minimal **Reactor** — an "event loop + handler registry" on top of epoll — and use it to build an echo server that can serve many connections at once. The whole Lab splits into 4 milestones, each introducing **exactly one new engineering problem**:

- **MS1** how the event loop itself spins, how a single connection echoes;
- **MS2** many concurrent connections arriving together, how to stay correct;
- **MS3** a large burst under ET, how not to lose data;
- **MS4** how to shut down gracefully, without hanging.

The key point: **every milestone's acceptance is adversarial** — not "echo runs", but "echo doesn't crash, drop, or hang under concurrency / large burst / shutdown under load". The biggest lie network code tells is "looks like it runs"; this Lab exists to fail those "looks like it runs" implementations at acceptance.

## Prerequisites

- [00 Traditional socket programming](./00-traditional-socket-basics.md) — the server five steps, RAII `UniqueFd`.
- [01 Modern socket wrapping](./01-modern-socket-wrapping.md) — `std::expected`, the C10K cost of thread-per-connection (this Lab is exactly its antidote).
- [02 epoll](./02-epoll-io-multiplexing.md) — interest list / ready list, ET vs LT, loop-read to EAGAIN.
- [03 The Reactor pattern](./03-reactor-pattern.md) — the POSA2 four roles; this Lab implements the Initiation Dispatcher.

## Project scaffold

`code/volumn_codes/vol8-labs/lab0-mini-reactor/` gives you a buildable project:

```text
include/net/reactor.hpp     # Reactor interface (what you implement)
include/net/unique_fd.hpp   # reuse 01's RAII fd
src/reactor.cpp             # ★reference impl (the answer) — your job is to rewrite against the interface
tests/lab0_tests.cpp        # MS1-4 Catch2 adversarial acceptance
CMakeLists.txt              # Catch2 (FetchContent) + normal tests + TSan tests, two targets
```

**How to work**: read the interface in `reactor.hpp`, write your implementation in `src/reactor.cpp`, then `cmake --build`, run the tests, and watch the 4 milestones' tests go green one by one. The current contents of `src/reactor.cpp` is the **reference answer** — there for you to compare approaches against; to actually do the Lab, clear it first, leave only the interface, and write from scratch. This is dogfooding: the interface and tests are the "problem" I give you; the implementation is the "homework" you hand in.

## The final interface

The `Reactor` class you implement (full declaration in `reactor.hpp`):

| Member | Semantics | Used by which MS |
|---|---|---|
| `add(fd, events, handler)` | Register an fd, declare the events of interest (`EPOLLIN`/`EPOLLOUT`/`EPOLLET`...); on readiness call `handler` | MS1 |
| `modify(fd, events)` | Change a registered fd's events (e.g. switch LT→ET, add `EPOLLOUT`) | MS3 |
| `remove(fd)` | Unregister an fd (remove from interest list + delete the handler) | MS1 (on EOF) |
| `run()` | Run the event loop, blocking until `stop()` | MS1 |
| `stop()` | Request a stop (called from another thread / signal handler; must wake a blocking `epoll_wait`) | MS4 |

Design points: **all handlers execute synchronously on the `run()` thread** (single-threaded Reactor). That's the foundation of its "lock-free-ness" — only one handler runs at a time, so shared state can't be mutated concurrently. The only cross-thread safety concern is `stop()` (it has to wake a blocking `epoll_wait`, which needs an eventfd or self-pipe to "poke" it).

## Milestone 1: spin up the event loop, echo one connection

**Goal**: implement the core of `Reactor` — the `epoll` instance + `add`/`run`, register a listening fd, `accept` one connection and echo it.

**Why**: this is the foundation of the whole Lab. `run()` must be a loop that can block-and-wait and dispatch on events; `add` must bind an fd and a handler and store them. Once this works, the next three milestones each add something on top.

**Implementation guidance**:

- `epoll_create1(0)` to build the epoll instance; in `add`, `epoll_ctl(EPOLL_CTL_ADD)` + store the handler in an `unordered_map<int, Handler>`.
- `run()` is a `while` loop: inside, `epoll_wait` blocks for events, and on getting events, looks up the map by `fd` and calls the matching handler.
- Inside the handler, after `accept`-ing a connection, `add` a connection handler (responsible for echo). When the connection handler reads `0` (EOF), it must `remove` itself + `close`.
- ⚠️ **Copy the handler before calling it** (`Handler h = it->second; h(events);`): the connection handler will `remove` itself on EOF, which erases the very `std::function` currently executing from the map — calling `it->second(...)` directly is use-after-free, and TSan catches it every time. This is the classic reactor self-deletion pit; the reference impl in `src/reactor.cpp` handles it exactly this way (this Lab stepped on it itself, so MS2's TSan acceptance isn't for show).

**Verify** (the MS1 case in `tests/lab0_tests.cpp`): start the reactor (in an independent thread), a client connects and sends `"hello-ms1"`, assert the echoed byte count equals what was sent. Connect a second one, assert it passes too — **sequential multi-connection must all be correct**.

## Milestone 2: concurrent clients, all correct (and TSan-clean)

**Goal**: 16 clients connect **at the same time**, send at the same time, all 16 echoes correct.

**Why**: MS1 only tested sequential connections. Once concurrency arrives, if your handler registry or per-connection state has issues (e.g. the handler captures the wrong fd, or the map is mutated concurrently), it shows. A single-threaded Reactor shouldn't have data races by design — so this step's acceptance adds **TSan**: any race goes red.

**Implementation guidance**: if the MS1 impl makes "all handlers run on the loop thread, the map is only mutated on the loop thread" true, MS2 passes naturally. **Don't `std::thread` inside a handler** — that regresses to 01's thread-per-connection, and it'll race the loop thread for the map; TSan flags it immediately.

**Verify**: the MS2 case opens 16 client threads echoing concurrently, asserts all succeed; the **TSan build** (`lab0_tests_tsan`) runs the same case and asserts no race report. What this step really catches is the impl that "runs concurrently but has hidden races" — TSan exists to expose it.

## Milestone 3 (adversarial): ET mode + large burst, not one byte less

**Goal**: the connection is registered as `EPOLLET | EPOLLIN`, the client sends 100KB at once, assert the echo comes back **exactly 100KB**.

**Why**: this is the whole Lab's "don't get fooled by tests" marquee moment, mapping directly to [the "ET-read-once loses 87KB" pit in piece 02](./02-epoll-io-multiplexing.md). ET notifies once only on the "new data arrived" edge; if your handler `read`s only once, the rest sits in the socket buffer and ET never notifies again — testing with small messages (4KB) won't surface it at all; only a large 100KB burst drags this bug out.

**Implementation guidance**:

- In `add`, give the connection `EPOLLIN | EPOLLET`; the connection fd must be `O_NONBLOCK`.
- On receiving an event, the handler **must `for(;;)` loop `read` until it returns `-1` with `errno == EAGAIN`** before finishing this round — drain the buffer completely.
- Each segment read is `write`-en back in a loop (write can also short-write / `EAGAIN`).

**Verify**: the MS3 case sends 100KB, reads the echo (with a 3s timeout), `REQUIRE(got == 100000)`. **Not one byte less** — that's the adversarial acceptance. Skip the loop-read, or forget non-blocking, and this number never reaches 100000.

## Milestone 4: graceful shutdown, `stop()` doesn't hang

**Goal**: call `stop()` from another thread, assert `run()` returns within 2 seconds (doesn't hang).

**Why**: `run()` is blocked on `epoll_wait(-1)` (wait forever). If `stop()` just sets a `stop_` flag, `epoll_wait` knows nothing about it — it keeps blocking, `run()` never returns, and your join hangs. This is the shutdown pit reactor-style code trips on most easily (the old notes have a real "accept blocking causes join to hang" bug).

**Implementation guidance**:

- Create an `eventfd` (or self-pipe), `add` it into epoll.
- In `stop()`: set the `stop_` flag + write one byte to the eventfd — this write immediately wakes the blocking `epoll_wait`.
- `run()` wakes up, finds the eventfd readable (or checks `stop_`), and exits the loop.

**Verify**: the MS4 case puts `run()` in `std::async`, first connects a client (simulating "under load"), then `stop()`s, and uses `future::wait_for(2s)` to assert the state is `ready` — **must return within 2 seconds**. Hang goes red.

## Performance test (optional)

Once the Lab passes, you can compare it against `code/volumn_codes/vol8/networking/01-modern-socket/` (01's thread-per-connection server): open 2000 idle connections on each and watch how much your reactor server's `VmSize`/`Threads` climb. Expected: Threads should barely move (just the one loop thread), and `VmSize` should be nowhere near 01's 24GB — the empirical proof of "serve many connections with few threads". Run it yourself, paste your own numbers, don't copy.

## Extension exercises (bonus, off the main line)

- **Timers**: register a `timerfd` into the reactor and implement a `call_after(duration, fn)`. Hint: a timerfd is also an fd; reading it clears the timer.
- **EPOLLONESHOT**: register connections with `EPOLLONESHOT`, and after handling, `modify` to re-arm — understand the difference from plain ET (why multi-threaded reactors need oneshot).
- **Multi-threaded reactor**: run N worker threads on the same `io_context`, use `strand` to keep one connection's handlers non-concurrent — you're now at Boost.Asio's threshold.

## Self-check

- [ ] MS1: single connection + sequential multi-connection echo all correct?
- [ ] MS2: 16 concurrent echoes all correct, and the TSan build has **no races**?
- [ ] MS3: ET + 100KB burst, echo is **exactly 100000** bytes? (loop-read to EAGAIN, fd non-blocking)
- [ ] MS4: after `stop()`, `run()` returns within 2 seconds? (eventfd wake)
- [ ] all handlers run on the loop thread, no `std::thread` spawned inside a handler?
- [ ] on connection EOF, `remove` + `close`? No leaked fd?

## References

- [man 2 epoll_create1](https://man7.org/linux/man-pages/man2/epoll_create1.2.html) / [epoll_ctl](https://man7.org/linux/man-pages/man2/epoll_ctl.2.html) / [epoll_wait](https://man7.org/linux/man-pages/man2/epoll_wait.2.html)
- [man 2 eventfd](https://man7.org/linux/man-pages/man2/eventfd.2.html) — used in MS4 to wake a blocking `epoll_wait`
- [Catch2](https://github.com/catchorg/Catch2) — the test framework for this Lab
- [ThreadSanitizer](https://clang.llvm.org/docs/ThreadSanitizer.html) — the tool MS2 uses to catch hidden data races
- [The Reactor pattern (series 03)](./03-reactor-pattern.md) — the design pattern this Lab implements
- [epoll (series 02)](./02-epoll-io-multiplexing.md) — where the MS3 ET pit is reproduced in full
