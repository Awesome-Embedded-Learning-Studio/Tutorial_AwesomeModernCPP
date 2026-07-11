---
title: "Modern socket wrapping: RAII, std::expected, and the C10K that thread-per-connection can't survive"
description: "Modernize the traditional C-style echo from 00 — RAII unique_fd makes 'forgetting close' impossible, std::expected packs errors and values into the type, then add thread-per-connection for concurrency; ending with a measured run where 2000 idle connections eat virtual memory from 83MB to 24GB, putting the C10K pain point in your own hands and motivating epoll"
chapter: 8
order: 1
platform: host
difficulty: intermediate
cpp_standard: [20, 23]
reading_time_minutes: 14
prerequisites:
  - "Traditional socket programming: the server five-step and TCP handshake"
related:
  - "Traditional socket programming: the server five-step and TCP handshake"
  - "epoll: Linux I/O multiplexing"
tags:
  - host
  - cpp-modern
  - intermediate
  - 网络编程
  - RAII守卫
---

# Modern socket wrapping: RAII, std::expected, and the C10K that thread-per-connection can't survive

In the previous piece (00) we got an echo server running with the plainest C-style socket, and walked through the five steps and TCP establishment thoroughly. But honestly, finishing that code left me a little uncomfortable — it runs, but it's **dirty**. A raw `int fd` gets passed around a function, and any `if (...) return 1;` in the middle can skip the trailing `close(cfd)`, silently leaking the fd; error handling is scattered `errno` plus `perror`, a "glance at the return value, glance at errno" at every step, and you have to remember yourself which one blew up. This style is 1983 C; it doesn't handle any of this rough work — that's the job of later languages.

In this piece we clean it up with Modern C++. Two things specifically: first, weld the fd's lifetime shut with RAII, so "forgetting `close`" cannot happen at the type level; second, pack "success value" and "error with context" into a single return type with `std::expected`, replacing the scattered errno. Once that's done, we add "thread-per-connection" so the server can actually serve many clients at once — and then measure a number that spikes my blood pressure, where you'll see with your own eyes why "thread-per-connection", a practice that seems obvious, falls apart once concurrency scales. That number is exactly the entry ticket to `epoll` in the next piece.

The code in this piece is C++23 (`std::expected`, `std::print` are both C++23), compiled and run on this machine with GCC 16.1.1, and every pasted terminal output is real.

## First, where exactly is the traditional version "dirty"

Before we start cleaning, let's point at the dirty spots in 00's code, so every later fix has a target. Recall the core loop of 00:

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

Here `cfd` is a raw `int`. Looks fine — but think: if somewhere in that `read`/`write` loop you later add an error-handling branch, an early `return`, or throw an exception, the `close(cfd)` line gets skipped. fds are a process-level, finite resource (default cap 1048576 — sounds like a lot, but a long-running server leaking a bit at a time runs out), and leaking one shrinks the pool by one with no error — this kind of bug is called a **resource leak**. It's unscathed in testing, blows up days later when "fds exhausted, `socket()` returns -1", and is excruciating to debug.

The error-handling side isn't any better: every syscall failure returns `-1` + sets `errno`, and after every step you check the return, read `errno`, then `perror` to print. The error message and the failure site are decoupled — the "bind" in `perror("bind")` is a hand-written string you stuffed in, with no binding to the actual code; rename it, forget to update, and you mislead yourself.

These two dirty spots — **resources you have to remember to release, errors you have to assemble strings for** — are the fate of C style. Modern C++ offers the corresponding, type-level retort.

## RAII: make "forgetting close" impossible

Modern C++'s core idea for managing resources is **RAII** (Resource Acquisition Is Initialization): bind ownership of a resource to a stack object, **acquire the resource on construction, release it on destruction**. The moment the stack object's scope ends, its destructor is guaranteed to run — whether you reach the end normally, `return` early, or throw an exception, there's no escape. In other words, move "remember to release" out of the programmer's head and into the type system.

Applied to fds, we write a `UniqueFd`: takes ownership of a raw fd on construction, `close`s on destruction, and **no copies, move only** — because an fd is an exclusive resource; two objects can't both think they own the same fd (that's a double close).

```cpp
class UniqueFd {
public:
    UniqueFd() = default;
    explicit UniqueFd(int fd) : fd_{fd} {}
    ~UniqueFd() { reset(); }                              // destruct = close

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
    int fd_{-1};                                          // -1 = empty, destruct is a no-op
};
```

A few design points are worth explaining. `fd_{-1}` is the "empty state" (a legal fd is never -1), so "an empty UniqueFd destructing" is safe — `reset()` sees `fd_ < 0` and does nothing, never `close(-1)`. The move constructor steals the other's fd and zeroes the other to empty (-1), guaranteeing only one `UniqueFd` ever holds a given fd. With this, the loop in 00 becomes:

```cpp
for (;;) {
    int raw = ::accept(listener->get(), nullptr, nullptr);
    if (raw < 0) {
        if (errno == EINTR)
            continue;
        /* ... */
            continue;
    }
    UniqueFd conn{raw};        // ← takes ownership; from here, fd is valid while conn lives
    // ... read/write on conn ...
}   // ← loop body ends, conn destructs, auto-close — impossible to forget
```

`conn` is a stack object; no matter how many `return`s or exceptions get added to this loop body later, its destructor will `close`. **"Forgetting `close`" has gone from a thing you have to remember to a thing that cannot happen.** That's the power of RAII, and the single most fundamental thing separating Modern C++ from C.

## std::expected: pack the error and the value into one type

Resources sorted, now error handling. C-style errors are a "return value + errno" pair; Modern C++'s answer is `std::expected<T, E>` — it holds either a **success value T** or an **error E**, both in the same return type, using the type system to force you to handle the error rather than pretend you didn't see it.

We give the error a small context-carrying struct, bringing along "which step blew up" alongside errno:

```cpp
struct SysError {
    int errno_value;
    std::string context;     // "socket" / "bind" / "listen" — where the failure happened
};
```

Then the three steps `socket + bind + listen` get wrapped into a function returning `std::expected<UniqueFd, SysError>`:

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

The caller gets a box that "might succeed or fail", judges with one `if (!listener)`, and on failure `listener.error().context` tells you directly "it was the bind stage, errno 98" — context and error travel together, no need to hand-write a decoupled string like `perror("bind")`. The biggest difference from 00's `perror`: **the error message is no longer a scattered, manually maintained string but a first-class citizen of the type, carried by the error object**.

This is the paradigm upgrade Modern C++ gives to I/O code — of a piece with RAII, both "moving a convention out of the programmer's head into the type system to enforce it".

## Adding concurrency: thread-per-connection

00's traditional server is **single-threaded** — `accept` one connection, echo it, `close`, `accept` the next. That has a fatal weakness: if a client connects and never sends (just hangs there), the whole server blocks on `read` waiting for it, and every connection after can't get in. It runs, but it can serve only one guest at a time.

The most intuitive upgrade is **spawn a thread for each incoming connection, dedicated to serving it**. After `accept` yields a new fd, hand it to an independent thread, and the main loop immediately goes back to `accept` the next — so multiple clients get handled in parallel by multiple threads, none blocking the others. Paired with the RAII above, the fd moves into the thread via `std::move`, with ownership transferring cleanly:

```cpp
void handle_session(UniqueFd conn) {       // by value: the thread owns this fd
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

`std::thread{handle_session, std::move(conn)}.detach()` — `detach` lets the thread run independently in the background; the main loop doesn't wait for it. `conn` transfers ownership via `std::move`, and after the move the main loop's `conn` is empty; the fd belongs entirely to the thread, and when the thread function returns, `conn` destructs and auto-`close`s. No double close, no missed close.

Run it, open three clients at once, all three get echoed immediately — concurrency solved, looks like a happy ending. But we're not done; the real pitfall is next.

## But how much concurrency can it take?

"Thread-per-connection" looks self-evident — one connection, one thread, so straightforward. But let's ask it a different question: **if concurrency climbs — say several thousand, or ten thousand connections — does it still hold up?** Let's actually run it, not go by feel.

While the server runs, we open a client that makes 2000 **idle connections** (connects but sends nothing, just hangs), and meanwhile read the server's `/proc/<pid>/status` to watch its virtual memory, resident memory, and thread count:

```text
[idle]       VmSize:  85352 kB    VmRSS:  4212 kB    Threads: 1
[2000 conns] VmSize: 25081508 kB  VmRSS: 28888 kB    Threads: 2001
```

Look at `VmSize`: **it jumps from 83MB to nearly 24GB**. 2000 connections, and virtual memory ate 24GB. Per connection that's `(25081508 - 85352) / 2000 ≈ 12.5 MB/connection`. Where does that 12.5MB come from? — **each thread has a default 8MB stack** (glibc default), plus glibc's per-thread internal mappings, TLS, guard page, adding up to about 12MB of virtual address space per thread. The `Threads` column is even more direct: `1 → 2001`, one more thread per connection.

The interesting one is `VmRSS` (resident physical memory), up only `(28888 - 4212) / 2000 ≈ 12 KB/connection` — because the kernel only allocates physical memory for stack pages "actually touched" (lazy allocation), and idle blocked threads barely touch their stack. So **RSS looks modest, but the virtual address space has already been eaten by stack reservations**. This is the crux of the C10K problem (Dan Kegel's classic 1999 proposition: how does one machine handle ten thousand concurrent connections): thread-per-connection → 10k connections = 8MB × 10000 = **80GB of virtual address space**; and the kernel has to keep a `task_struct` + kernel stack per thread, so ten thousand mostly-idle threads (most of them blocked on `read` waiting for data) burden the scheduler and memory subsystem for nothing — **using a heavy entity ("thread") to serve a light job (a connection that mostly waits on I/O) is wildly inefficient**.

Put plainly, thread-per-connection isn't wrong because it "can't run"; it's wrong because **it uses the heaviest resource (a thread) to serve the lightest work (a connection that mostly waits on I/O)**. With few connections you don't feel it; the moment it scales, it blows up.

So what's the fix? The direction is clear: **serve many connections with few threads** — let one or two threads watch thousands of fds at once, and handle whichever fd has data, rather than assigning each connection a dedicated thread on standby. That's exactly what the next piece, **epoll / I/O multiplexing**, solves, and the threshold we cross from "synchronous blocking, thread-per-connection" into "event-driven".

## Wrap-up

In this piece we reworked 00's traditional server with Modern C++. A few key takeaways:

- **RAII `UniqueFd`**: the fd's lifetime welded to a stack object, destruct = `close`, no copies, move only. "Forgetting `close`" goes from something you remember to something impossible. This is the single most fundamental thing separating Modern C++ from C.
- **`std::expected<T, E>`**: success value and context-carrying error packed into one return type; `if (!x)` judges at a glance, and the error message travels with the error object, replacing scattered errno + hand-written `perror` strings.
- **Thread-per-connection**: lets the server handle concurrent clients; the fd goes to the thread via `std::move`, ownership clean.
- **C10K, measured**: 2000 idle connections eat virtual memory from 83MB to 24GB (~12MB per connection, mostly the 8MB thread stack), Threads climbs to 2001. The root problem is "using a heavy entity (thread) to serve light work (an I/O-waiting connection)"; once connections scale, it blows up.
- **The way out**: serve many connections with few threads — epoll, next.

With this piece, we've gone from "traditional C style" to "modern C++ plus concurrency" on the Linux socket front, and felt the ceiling of the synchronous model with our own hands. The next piece turns that wall over: how epoll lets one thread watch thousands of fds.

## References

- [cppreference: std::expected](https://en.cppreference.com/w/cpp/utility/expected) — C++23 error handling (`std::unexpected` constructs the error value)
- [cppreference: std::unique_ptr / RAII](https://en.cppreference.com/w/cpp/memory/unique_ptr) — the RAII paradigm; `UniqueFd` is the same idea applied to fds
- [The C10K problem (Dan Kegel)](https://kea.dev/notes/the-c10k-problem) — "how one machine serves ten thousand concurrent connections"; this piece's measurement is its motivation
- [Traditional socket programming: the server five-step and TCP handshake (series 00)](./00-traditional-socket-basics.md) — what this piece modernizes
- [epoll: Linux I/O multiplexing (series, next)](./02-epoll-io-multiplexing.md) — serving many fds with few threads, solving the C10K pain at the end of this piece
