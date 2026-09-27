---
title: "Lab 0: mini Reactor echo server — from epoll to a concurrency-ready event loop"
description: "On the socket/epoll/Reactor foundation laid by the first four pieces, build a minimal Reactor by hand (epoll event loop + handler registry) and turn it into an echo server that can take concurrency. Split into 4 milestones, each introducing exactly one engineering problem, with adversarial acceptance: no crash under many concurrent connections, no data lost on an ET large burst, no hang on stop — fail these and it isn't 'runs', it's 'looks like it runs'"
chapter: 8
order: 4
platform: host
difficulty: advanced
cpp_standard: [20]
reading_time_minutes: 8
prerequisites:
  - "Traditional socket programming: the server's five steps and TCP connection setup — the classic style we learned from Stevens"
  - "epoll: Linux I/O multiplexing — from poll's bottleneck to the interest list and ready list"
  - "The Reactor pattern: wrapping epoll into an event loop + callbacks, and why it's 'synchronous non-blocking'"
tags:
  - host
  - cpp-modern
  - advanced
  - 网络编程
  - 异步编程
translation:
  source: documents/vol8-domains/networking/lab0-mini-reactor.md
  source_hash: 205e7c5ec9a62878e542347f9537cc469095b015603de9a2da5f892c8bec3bc6
  translated_at: '2026-09-26T04:45:24+00:00'
  engine: anthropic
  token_count: 6000
---

# Lab 0: mini Reactor echo server — from epoll to a concurrency-ready event loop

> This is a hands-on Lab, not a concept tutorial. In the first four pieces (00→03) we covered sockets, epoll, and the Reactor pattern; this time it's your turn to stitch them into something that actually runs. The companion project scaffold lives at `code/volumn_codes/vol8-labs/lab0-mini-reactor/`.

## Goal

Implement a minimal **Reactor** — an "event loop + handler registry" on top of epoll — and use it to build an echo server that can serve a large number of connections simultaneously. The Lab breaks into 4 milestones, and each milestone introduces **exactly one new engineering problem**:

- **MS1** how the event loop itself gets spinning, and how a single connection echoes;
- **MS2** many concurrent connections arriving at once, and how to stay correct;
- **MS3** a large burst under ET, and how not to lose data;
- **MS4** how to shut down gracefully, without hanging.

The point: **every milestone's acceptance is adversarial** — not "echo runs", but "echo doesn't crash, doesn't drop, doesn't hang under concurrency / large burst / shutdown under load". The biggest lie in network code is "looks like it runs", and this Lab exists to fail those "looks like it runs" implementations at acceptance.

## Prerequisites

- [00 Traditional socket programming](./00-traditional-socket-basics.md) — the server's five steps, the RAII `UniqueFd`.
- [01 Modern socket wrapping](./01-modern-socket-wrapping.md) — `std::expected`, the C10K cost of thread-per-connection (this Lab is precisely its antidote).
- [02 epoll](./02-epoll-io-multiplexing.md) — interest list / ready list, ET vs LT, loop-reading until EAGAIN.
- [03 The Reactor pattern](./03-reactor-pattern.md) — the POSA2 four roles; what this Lab implements is the Initiation Dispatcher.

## Project scaffold

`code/volumn_codes/vol8-labs/lab0-mini-reactor/` hands you a buildable project:

```text
include/net/reactor.hpp     # the Reactor interface (what you implement)
include/net/unique_fd.hpp   # reuse 01's RAII fd
src/reactor.cpp             # ★reference implementation (the answer) — your job is to rewrite it here against the interface
tests/lab0_tests.cpp        # Catch2 adversarial acceptance for MS1-4
CMakeLists.txt              # Catch2 (FetchContent) + two targets: normal tests and TSan tests
```

**How to work**: read the interface in `reactor.hpp`, write your own implementation in `src/reactor.cpp`, then `cmake --build`, run the tests, and watch the 4 milestones' tests go green one by one. What sits in `src/reactor.cpp` right now is the **reference answer** — it's there so you can compare approaches; if you're actually doing the Lab, clear it out first, keep only the interface, and write from scratch. This is called dogfooding: the interface and the tests are the "problem" I hand you, and the implementation is the "homework" you hand back.

## The final interface

The `Reactor` class you implement (full declaration in `reactor.hpp`):

| Member | Semantics | Used by which MS |
|---|---|---|
| `add(fd, events, handler)` | Register an fd, declaring the events you care about (`EPOLLIN`/`EPOLLOUT`/`EPOLLET`...); when ready, call `handler` | MS1 |
| `modify(fd, events)` | Change a registered fd's events (e.g. switch LT→ET, add `EPOLLOUT`) | MS3 |
| `remove(fd)` | Unregister an fd (remove from the interest list + delete the handler) | MS1 (on EOF) |
| `run()` | Run the event loop, blocking until `stop()` | MS1 |
| `stop()` | Request a stop (called from another thread / signal handler; must be able to wake a blocking `epoll_wait`) | MS4 |

Design key point: **all handlers execute synchronously on the `run()` thread** (single-threaded Reactor). This is the root of its "no locks needed" property — only one handler is running at any moment, so shared state can't be mutated concurrently. The only cross-thread safety you need is for `stop()` (it has to wake a blocking `epoll_wait`, which calls for an eventfd or a self-pipe to give it a "poke").

## Milestone 1: get the event loop spinning, echo one connection

**Goal**: implement the core of `Reactor` — the `epoll` instance + `add`/`run`, register a listening fd, `accept` one connection out of it, and echo it.

**Why**: this is the foundation of the whole Lab. `run()` has to be a loop that blocks waiting and dispatches when events arrive; `add` has to bind an fd and a handler together and store them. Once this step works, the next three milestones are all just adding things on top of it.

**Implementation guidance**:

- `epoll_create1(0)` creates the epoll instance; in `add`, `epoll_ctl(EPOLL_CTL_ADD)` + store the handler in an `unordered_map<int, Handler>`.
- `run()` is a `while` loop: inside, `epoll_wait` blocks waiting for events, and once events arrive, look up the map by `fd` and call the corresponding handler.
- Inside the handler, after `accept`-ing a connection, `add` a connection handler (the one responsible for echo). When the connection handler reads `0` (EOF), it must `remove` itself + `close`.
- ⚠️ **Copy the handler before `run()` calls it** (`Handler h = it->second; h(events);`): on EOF the connection handler will `remove` itself, which erases from the map the very `std::function` that is **currently executing** — calling `it->second(...)` directly is use-after-free, and TSan catches it every single time. This is the reactor's classic self-deletion pit, and the reference implementation in `src/reactor.cpp` handles it exactly this way (this Lab stepped on it itself, which is why MS2's TSan acceptance is not for show).

**Verify** (the MS1 case in `tests/lab0_tests.cpp`): start the reactor (in its own thread), a client connects and sends `"hello-ms1"`, assert the echoed byte count equals what was sent. Then connect a second one and assert it also passes — **sequential multiple connections must all be correct**.

## Milestone 2: concurrent clients, all correct (and TSan-clean)

**Goal**: 16 clients connect **at the same time** and send at the same time; all 16 echoes come back correct.

**Why**: MS1 only tested sequential connections. When concurrency all arrives at once, anything wrong in your handler registry or per-connection state (say, a handler capturing the wrong fd, or a map mutated concurrently) will surface. A single-threaded Reactor should have no data races by design — so this step's acceptance adds **TSan** on top: any race goes red.

**Implementation guidance**: if the MS1 implementation already ensures "all handlers run on the loop thread, the map is only mutated on the loop thread", MS2 passes naturally. **Do not `std::thread` inside a handler** — that regresses to 01's thread-per-connection, and it would fight the loop thread over the map; TSan reports the race immediately.

**Verify**: the MS2 case opens 16 client threads echoing concurrently and asserts all succeed; the **TSan build of the tests** (`lab0_tests_tsan`) runs the same cases and asserts no race reports. What this step really catches is the implementation that "concurrently looks like it runs but actually has hidden races" — TSan is there to expose it.

## Milestone 3 (adversarial): ET mode + large burst, not one byte less

**Goal**: register the connection as `EPOLLET | EPOLLIN`, have the client send 100KB in one shot, and assert the echo comes back **exactly 100KB**.

**Why**: this is the whole Lab's "don't get fooled by your tests" marquee moment, mapping directly onto [that "ET-read-once loses 87KB" pit from piece 02](./02-epoll-io-multiplexing.md). ET notifies only once, on the "new data has arrived" edge; if your handler `read`s only once, the remaining data just sits in the socket buffer and ET never notifies again — tests with small messages (4KB) can't surface this at all; only a big 100KB burst drags the bug out into the open.

**Implementation guidance**:

- In `add`, give the connection `EPOLLIN | EPOLLET`; the connection fd must be `O_NONBLOCK`.
- When the handler gets an event, it **must `for(;;)` loop over `read` until `read` returns `-1` with `errno == EAGAIN`** before ending this round of handling — drain the buffer completely.
- `write` each segment read back out in a loop (write can also short-write / hit `EAGAIN`).

**Verify**: the MS3 case sends 100KB, reads the echo (with a 3s timeout), `REQUIRE(got == 100000)`. **Not one byte less** — that is the adversarial acceptance. Skip the loop-read, or forget non-blocking, and this number never reaches 100000.

## Milestone 4: graceful shutdown, `stop()` must not hang

**Goal**: call `stop()` from another thread and assert `run()` returns within 2 seconds (no hang).

**Why**: `run()` is blocked on `epoll_wait(-1)` (wait forever). If `stop()` merely sets a `stop_` flag, `epoll_wait` knows nothing about it — it keeps blocking, `run()` never returns, and your join hangs forever. This is the shutdown pit that reactor-style classes trip on most easily (the old notes contain a real "blocking accept causes join to hang" bug).

**Implementation guidance**:

- Create an `eventfd` (or a self-pipe) and `add` it into the epoll.
- Inside `stop()`: set the `stop_` flag + write one byte to the eventfd — this write immediately wakes the blocking `epoll_wait`.
- `run()` wakes up, finds the eventfd readable (or checks `stop_`), and exits the loop.

**Verify**: the MS4 case puts `run()` into `std::async`, first connects one client (simulating "under load"), then calls `stop()`, and uses `future::wait_for(2s)` to assert the state is `ready` — **it must return within 2 seconds**. Hang goes red.

## Performance test (optional)

Once the Lab runs through, you can run a comparison against `code/volumn_codes/vol8/networking/01-modern-socket/` (01's thread-per-connection server): open 2000 idle connections on each and watch how much your reactor server's `VmSize`/`Threads` climb. Expected: Threads should barely move (just the one loop thread), and `VmSize` stays far below 01's 24GB — that's the empirical proof of "serve many connections with few threads". Run the numbers yourself and paste your own; don't copy.

## Extension exercises (bonus, off the main line)

- **Timers**: register a `timerfd` into the reactor and implement a `call_after(duration, fn)`. Hint: a timerfd is also an fd; reading it clears the timer.
- **EPOLLONESHOT**: register the connection with `EPOLLONESHOT`, and after handling, `modify` to re-arm it — understand how it differs from plain ET (why multi-threaded reactors need oneshot).
- **Multi-threaded reactor**: run N worker threads on the same `io_context`, using `strand` to keep one connection's handlers from running concurrently — that's the threshold of Boost.Asio you've just stepped onto.

## Self-check

- [ ] MS1: single connection + sequential multiple connections, echo all correct?
- [ ] MS2: 16 concurrent echoes all correct, and the TSan build has **no races**?
- [ ] MS3: ET + 100KB burst, echo is **exactly 100000** bytes? (loop-read until EAGAIN, fd non-blocking)
- [ ] MS4: after `stop()`, `run()` returns within 2 seconds? (eventfd wake-up)
- [ ] All handlers run on the loop thread, no threads spawned inside handlers?
- [ ] On connection EOF, `remove` + `close`? No leaked fds?

## References

- [man 2 epoll_create1](https://man7.org/linux/man-pages/man2/epoll_create1.2.html) / [epoll_ctl](https://man7.org/linux/man-pages/man2/epoll_ctl.2.html) / [epoll_wait](https://man7.org/linux/man-pages/man2/epoll_wait.2.html)
- [man 2 eventfd](https://man7.org/linux/man-pages/man2/eventfd.2.html) — used in MS4 to wake a blocking `epoll_wait`
- [Catch2](https://github.com/catchorg/Catch2) — the test framework for this Lab
- [ThreadSanitizer](https://clang.llvm.org/docs/ThreadSanitizer.html) — the tool MS2 uses to catch hidden data races
- [The Reactor pattern (this series, 03)](./03-reactor-pattern.md) — the design pattern this Lab implements
- [epoll (this series, 02)](./02-epoll-io-multiplexing.md) — where the MS3 ET pit is reproduced in full
