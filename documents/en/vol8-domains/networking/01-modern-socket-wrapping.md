---
title: "Modern socket wrapping: RAII, std::expected, and the C10K that thread-per-connection can't survive"
description: "Modernize the traditional C-style echo from 00 — an RAII unique_fd makes a missed close impossible, std::expected packs the error and the value into the type, and thread-per-connection lets it survive concurrent clients; the finale measures 2000 idle connections eating virtual memory from 83MB to 24GB, puts the C10K pain in your own hands, and sets up epoll"
chapter: 8
order: 1
platform: host
difficulty: intermediate
cpp_standard: [20, 23]
reading_time_minutes: 14
prerequisites:
  - "Traditional socket programming: the server's five steps and TCP connection setup — the classic style we learned from Stevens"
related:
  - "Traditional socket programming: the server's five steps and TCP connection setup — the classic style we learned from Stevens"
  - "epoll: Linux I/O multiplexing — from poll's bottleneck to the interest list and ready list"
tags:
  - host
  - cpp-modern
  - intermediate
  - 网络编程
  - RAII守卫
translation:
  source: documents/vol8-domains/networking/01-modern-socket-wrapping.md
  source_hash: 87c3728cd2d5ae032a7b0023294196a7c0eeea9cb5e67d8811b56da3782db50a
  translated_at: '2026-09-26T04:34:36+00:00'
  engine: anthropic
  token_count: 6200
---

# Modern socket wrapping: RAII, std::expected, and the C10K that thread-per-connection can't survive

In the previous piece (00) we got an echo server running with the plainest C-style socket code, and took the five steps and TCP connection setup apart properly. But honestly, when that code was done, we felt a little uneasy about it — it runs, but it's **dirty**. A raw `int fd` gets passed all over the place, and any `if (...) return 1;` in the middle can jump past the `close(cfd)` at the end, and the fd quietly leaks away; error handling is scattered `errno` plus `perror`, a "glance at the return value, another glance at errno" after every step, and remembering which step blew up is entirely on you. This way of writing is 1983 C style, and it doesn't look after any of that dirty work — that is a job for the languages that came later.

In this piece we clean it up with Modern C++. Two things, concretely: first, weld the fd's lifetime shut with RAII, so that a missed `close` becomes impossible at the type level; second, use `std::expected` to pack the "success value" and the "error with context" into the same return type, replacing the scattered errno. With that cleaned up, we then give the server "thread-per-connection" so it can genuinely serve several clients at once — and then measure a number that will send your blood pressure through the roof, where you will see with your own eyes why "thread-per-connection", a practice that looks only natural, collapses once concurrency scales up. That number is precisely the admission ticket to `epoll` in the next piece.

The code in this piece is C++23 (`std::expected` and `std::print` are both C++23), compiled and run on this machine with GCC 16.1.1, and every terminal output pasted here is real.

## First, where exactly the traditional version's "dirt" sits

Before we start cleaning up, let's point out the dirty spots in that 00 code, so that every fix later on has a target to aim at. Recall 00's core loop:

```c
for (;;) {
    int cfd = accept(lfd, NULL, NULL);
    if (cfd < 0) { if (errno == EINTR) continue; perror("accept"); continue; }

    char buf[4096];
    for (;;) {
        ssize_t n = read(cfd, buf, sizeof(buf));
        if (n <= 0) break;
        write(cfd, buf, n);
    }
    close(cfd);   /* ← manual close */
}
```

Here `cfd` is a raw `int`. It looks fine — but think about it: if some day, inside that `read`/`write` loop, you add an error-handling branch, an early `return`, or throw an exception, the `close(cfd)` line gets skipped. fds are a finite, process-level resource (the default cap is 1048576 — sounds like a lot, but a long-running server that leaks a little at a time accumulates its way to exhaustion); every leaked one is one fewer, with no error reported — this kind of bug is called a **resource leak**. It sails through testing unscathed and only blows up days later with "fds exhausted, `socket()` returns -1", which is miserably painful to track down.

The error-handling side is no better: every failed syscall returns `-1` and sets `errno`, so after each step you have to check the return value, read `errno`, then `perror` to print. The error message and the failure site are split apart — the "bind" inside `perror("bind")` is a string you typed by hand and stuffed in, with no binding to the actual code; rename something, forget to update it, and you have misled yourself.

These two dirty spots — **resources whose release depends on a human remembering, errors assembled from hand-written strings** — are the fate of C style. Modern C++ provides the matching fix, at the type level.

## RAII: making a missed close impossible

The core idea Modern C++ uses for managing resources is called **RAII** (Resource Acquisition Is Initialization): bind ownership of a resource to a stack object — **the object acquires the resource at construction and releases it at destruction**. The moment the stack object's scope ends, the destructor is guaranteed to be called — whether control reaches the end normally, `return`s early, or an exception is thrown, there is no escape. In other words, "remembering to release" moves out of the programmer's head and into the type system.

Applied to fds, we write a `UniqueFd`: it takes over a raw fd at construction and `close`s it at destruction, and it is **non-copyable, move-only** — because an fd is an exclusively-owned resource, and two objects must not both believe they own the same fd (that would be a double close).

```cpp
class UniqueFd {
public:
    UniqueFd() = default;
    explicit UniqueFd(int fd) : fd_{fd} {}
    ~UniqueFd() { reset(); }                              // destructor = close

    UniqueFd(const UniqueFd&) = delete;                   // exclusive, no copies
    UniqueFd& operator=(const UniqueFd&) = delete;

    UniqueFd(UniqueFd&& other) noexcept : fd_{other.fd_} { other.fd_ = -1; }
    UniqueFd& operator=(UniqueFd&& other) noexcept {
        if (this != &other) { reset(); fd_ = other.fd_; other.fd_ = -1; }
        return *this;
    }

    void reset() { if (fd_ >= 0) { ::close(fd_); fd_ = -1; } }
    int  get() const { return fd_; }
    explicit operator bool() const { return fd_ >= 0; }
private:
    int fd_{-1};                                          // -1 = empty, destructor does nothing
};
```

A few design points are worth spelling out. `fd_{-1}` is the "empty state" (a legitimate fd is never -1), so "an empty UniqueFd destructing" is safe — `reset()` sees `fd_ < 0` and does nothing; it will never `close(-1)`. The move constructor steals the other side's fd and sets the other side to empty (-1), guaranteeing that at any moment only one `UniqueFd` holds a given fd. With this in hand, that loop from 00 becomes:

```cpp
for (;;) {
    int raw = ::accept(listener->get(), nullptr, nullptr);
    if (raw < 0) {
        if (errno == EINTR)
            continue;
        /* ... */
            continue;
    }
    UniqueFd conn{raw};        // ← takes ownership; from here on, the fd is valid as long as conn lives
    // ... read/write on conn ...
}   // ← loop body ends, conn destructs, auto-close — a leak is impossible
```

`conn` is a stack object: no matter how many `return`s or exceptions get added to this loop body in the future, its destructor will certainly `close`. **A missed `close` has gone from a thing a human had to remember to a thing that cannot happen.** That is the power of RAII, and the most fundamental line dividing Modern C++ from C.

## std::expected: packing the error and the value into one type

With resources sorted, on to error handling. The C-style error is a "return value + errno" pair; Modern C++'s answer is `std::expected<T, E>` — it holds either a **success value T** or an **error E**, both inside one and the same return type, and the type system forces you to handle the error instead of pretending you didn't see it.

We define a small context-carrying struct for errors, bringing "which step blew up" along with the errno:

```cpp
struct SysError {
    int errno_value;
    std::string context;     // "socket" / "bind" / "listen" — which step the failure happened in
};
```

Then the three steps `socket + bind + listen` get wrapped into a single function returning `std::expected<UniqueFd, SysError>`:

```cpp
std::expected<UniqueFd, SysError> make_listener(std::uint16_t port) {
    int raw = ::socket(AF_INET, SOCK_STREAM, 0);
    if (raw < 0) return std::unexpected(SysError{errno, "socket"});
    UniqueFd fd{raw};

    int yes = 1;
    if (::setsockopt(fd.get(), SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0)
        return std::unexpected(SysError{errno, "setsockopt(SO_REUSEADDR)"});

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port        = htons(port);
    if (::bind(fd.get(), reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0)
        return std::unexpected(SysError{errno, "bind"});

    if (::listen(fd.get(), 64) < 0)
        return std::unexpected(SysError{errno, "listen"});

    return fd;   // success: return the UniqueFd directly
}
```

What the caller receives is a box that "may succeed or may fail": a single `if (!listener)` tells at a glance, and on failure `listener.error().context` tells you directly "the bind stage, errno 98" — the context travels bound to the error, with no need to hand-write a disconnected string like `perror("bind")`. Compared with 00's `perror`, the biggest difference here is: **the error message is no longer a scattered, human-maintained string, but a first-class citizen of the type, traveling with the error object**.

This is the paradigm upgrade Modern C++ brings to I/O code — cut from the same cloth as RAII: both take "the conventions living in the programmer's head" and move them into the type system to be enforced.

## Adding concurrency: thread-per-connection

00's traditional server is **single-threaded** — `accept` one connection, finish the echo, `close`, then `accept` the next. That has a fatal weakness: if a client connects and never sends anything (just hangs there), the whole server gets stuck on `read` waiting for it, and every later connection is shut out. It runs, but it can serve only one guest at a time.

The most intuitive upgrade is **for every incoming connection, spawn a thread dedicated to serving it**. After `accept` yields a new fd, hand it to an independent thread, and the main loop immediately goes back to `accept` the next one — so multiple clients are handled in parallel by multiple threads, none of them blocking the others. Combined with the RAII we just wrote, the fd is handed to the thread with `std::move`, and ownership transfers cleanly:

```cpp
void handle_session(UniqueFd conn) {       // take by value: the thread owns this fd
    std::array<char, 4096> buf;
    for (;;) {
        ssize_t n = ::read(conn.get(), buf.data(), buf.size());
        if (n == 0) break;                 // peer closed
        if (n < 0) { if (errno == EINTR) continue; break; }
        /* ... write back ... */
    }
}   // conn destructs, auto-close

int main() {
    /* ... make_listener ... */
    for (;;) {
        int raw = ::accept(listener->get(), nullptr, nullptr);
        if (raw < 0) { if (errno == EINTR) continue; /* ... */ continue; }
        UniqueFd conn{raw};
        std::thread{handle_session, std::move(conn)}.detach();   // thread-per-connection
    }
}
```

`std::thread{handle_session, std::move(conn)}.detach()` — `detach` lets the thread run independently in the background while the main loop doesn't wait for it. `conn` hands over ownership via `std::move`; after the move, the main loop's `conn` is empty, the fd belongs entirely to the thread, and when the thread function returns, `conn` destructs and `close`s automatically. No double close, and no missed close.

Run it, open three clients connecting at the same time, and all three get their echo immediately — the concurrency problem is solved, and everyone looks happy. But the story doesn't end here; the real pitfall is still ahead.

## But how much concurrency can it take

"Thread-per-connection" looks only natural — one connection, one thread, so intuitive. But let's put a different question to it: **if the concurrency scales up — say a few thousand, or ten thousand connections — can it still hold up?** Let's actually run it, not go by feel.

While the server is running, we open a client that makes 2000 **idle connections** (connected but sending nothing, just hanging), and meanwhile read the server's `/proc/<pid>/status` to watch how its virtual memory, resident memory, and thread count change:

```text
[idle]       VmSize:  85352 kB    VmRSS:  4212 kB    Threads: 1
[2000 conns] VmSize: 25081508 kB  VmRSS: 28888 kB    Threads: 2001
```

Look at that `VmSize`: **it rockets from 83MB to nearly 24GB**. 2000 connections, and virtual memory ate 24GB. Per connection, that's `(25081508 - 85352) / 2000 ≈ 12.5 MB/connection`. Where does that 12.5MB come from? — **each thread's default 8MB stack** (the glibc default), plus glibc's per-thread internal mappings, TLS, and guard pages, stacking up to roughly 12MB of virtual address space per thread. The `Threads` column is even more direct: `1 → 2001` — one more thread for every connection that arrives.

The interesting one is `VmRSS` (resident physical memory), which rose only `(28888 - 4212) / 2000 ≈ 12 KB/connection` — because the kernel only allocates physical memory for stack pages that are "actually touched" (lazy allocation), and idle blocked threads barely touch their stacks. So **RSS looks modest, but the virtual address space has already been eaten clean by stack reservations**. That is the choke point of the C10K problem (Dan Kegel's classic 1999 proposition: how does one machine hold up ten thousand concurrent connections): thread-per-connection → 10k connections = 8MB × 10000 = **80GB of virtual address space**; and for every thread, the kernel must additionally maintain a `task_struct` + kernel stack, so ten thousand mostly-idle threads (most of them blocked on `read`, waiting for data) put pointless load on the scheduler and the memory subsystem — **using "thread", a heavy entity, to correspond to a connection that may sit idle for a long time is a terrible use of resources**.

Put plainly, the sin of "thread-per-connection" is not that it "can't run", but that **it uses the heaviest resource (a thread) to serve the lightest work (a connection that spends most of its time waiting on I/O)**. With few connections you don't feel it; the moment volume arrives, it blows up.

So what's the fix? The direction is clear: **use a few threads to serve a great many connections** — let one or two threads keep watch over thousands of fds at once, and handle whichever fd has data, instead of assigning every connection a dedicated thread standing guard. That is exactly the problem the next piece, **epoll / I/O multiplexing**, solves — and it is the threshold where we step across from "synchronous blocking, thread-per-connection" into "event-driven".

## Wrap-up

In this piece we reworked 00's traditional server with Modern C++. Let's collect the key pieces:

- **RAII `UniqueFd`**: the fd's lifetime is welded onto a stack object — destructor means `close`, non-copyable and move-only. A missed `close` goes from depending on human memory to being impossible. This is the most fundamental line dividing Modern C++ from C.
- **`std::expected<T, E>`**: the success value and the context-carrying error live in one and the same return type; `if (!x)` decides at a glance, and the error message travels with the error object, replacing scattered errno plus hand-written `perror` strings.
- **Thread-per-connection**: lets the server hold up under concurrent clients; the fd is handed to the thread with `std::move`, and ownership stays clean.
- **C10K, measured**: 2000 idle connections took virtual memory from 83MB to 24GB (~12MB per connection, mostly the 8MB thread stack), and Threads climbed to 2001. The root problem is "using a heavy entity (a thread) to serve light work (a connection waiting on I/O)"; once connections scale up, it blows up.
- **The way out**: a few threads serving a great many connections — epoll, in the next piece.

With this piece, the Linux socket story has gone from "traditional C style" to "modern C++ plus concurrency", and we have laid our own hands on the ceiling of the synchronous model. The next piece climbs over that wall: how epoll lets one thread keep watch over thousands of fds.

## References

- [cppreference: std::expected](https://en.cppreference.com/w/cpp/utility/expected) — C++23 error handling (`std::unexpected` constructs the error value)
- [cppreference: std::unique_ptr / RAII](https://en.cppreference.com/w/cpp/memory/unique_ptr) — the RAII paradigm; `UniqueFd` is the same idea applied to fds
- [The C10K problem (Dan Kegel)](https://kea.dev/notes/the-c10k-problem) — "how one machine holds up ten thousand concurrent connections"; this piece's measurement is exactly its motivation
- [Traditional socket programming: the server's five steps and TCP connection setup (series 00)](./00-traditional-socket-basics.md) — the target this piece modernizes
- [epoll: Linux I/O multiplexing (next in this series)](./02-epoll-io-multiplexing.md) — serving a great many fds with a few threads, solving the C10K pain this piece ends on
