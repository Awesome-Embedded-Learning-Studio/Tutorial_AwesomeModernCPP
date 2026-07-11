---
title: "Traditional socket programming: the server five-step and TCP handshake — the classic style learned from Stevens"
description: "Walk through an echo server with the plain C-style BSD socket API, taking the five steps — socket/bind/listen/accept/read-write — and the kernel internals of the TCP three-way handshake seriously: byte order, listen's two queues and backlog, the difference between a listening fd and a connection fd, and why 'accept completes the handshake' is a common misconception. This is the network foundation that hasn't changed in decades; however flashy Asio or coroutines get, underneath it's still these five steps"
chapter: 8
order: 0
platform: host
difficulty: intermediate
cpp_standard: [20, 23]
reading_time_minutes: 16
related:
  - "Modern socket wrapping: RAII and std::expected"
  - "epoll: Linux I/O multiplexing"
tags:
  - host
  - cpp-modern
  - intermediate
  - 网络编程
---

# Traditional socket programming: the server five-step and TCP handshake — the classic style learned from Stevens

Honestly, when I started writing this networking volume, I had the urge to teach "the most primitive version" thoroughly first. The reason is simple: no matter how elegant Boost.Asio gets, how smooth C++20 coroutines feel, or how forward-looking std::execution is, when you write a network program on Linux, underneath it all you keep circling back to the same thing — the **BSD socket API**, settled in 1983. `epoll` accelerates it, `Asio` wraps it, but "how a server actually comes up" has barely changed from Stevens's *UNIX Network Programming* to today's kernel. So in this piece we touch none of the modern machinery: just the plain C-style socket, and we step through this **decades-old foundation** one plank at a time.

This piece and the next are paired. This one (00) covers the **traditional, C-style** way — raw fds, manual `close`, `errno` plus `perror`, exactly what Beej's Guide and Stevens teach you. The next one (01) uses Modern C++ (RAII, `std::expected`) to mop up its rough edges. The split is deliberate — some readers already know the traditional style and only want the modern wrapping, so they can jump straight to 01; others want a socket refresher, and this piece is a stable reference. The BSD socket API hasn't changed in decades; once written, this barely needs touching.

The code here is pure C socket, compilable with `gcc`, and every terminal output pasted below was actually run on this machine. We won't touch concurrency (that's 01's job) — just a single-threaded echo server, focused on getting the "five steps" clean.

## A minimal target: the echo server

Let's fix a minimal target to hang all the later concepts on: an **echo server** — a client connects, sends something, the server sends it right back, then waits for the next message. It's the "Hello World" of network programming, small enough to have no business logic, yet it covers the full lifecycle of a TCP connection from establishment to send/receive.

From the server's point of view, that lifecycle is the classic **five steps**: `socket → bind → listen → accept → read/write`. We'll go step by step, and at each one we won't just say "call this function" — we'll dig into **what the kernel actually does**, because most of the pitfalls in socket programming come from "you think this function did X, but it really did Y".

## Step 1: socket() — ask the kernel for a communication endpoint

```c
int lfd = socket(AF_INET, SOCK_STREAM, 0);
```

What `socket()` does is simple: **it asks the kernel for a "communication endpoint" and returns a file descriptor (fd) to represent it**. The three arguments answer "which protocol family, which type, which specific protocol": `AF_INET` is IPv4, `SOCK_STREAM` is a connection-oriented reliable byte stream (i.e. TCP), and the third argument `0` means "pick the protocol automatically" (with TCP decided above, it picks TCP).

There's an unavoidable concept here, though: **what exactly is an fd**. The Unix motto is "everything is a file"; the kernel keeps a "table of open files" per process, and an fd is just an index into that table (a small integer). The fd returned by `socket()` is essentially a new slot in that table, with a kernel socket object hanging off the back of it. When you later call `bind`/`listen`/`accept`/`read`/`write` on that fd, the kernel uses the fd to look up the table, find the socket object behind it, and operate on that. So the fd is just a "handle"; the real state lives in the kernel.

::: warning Check fd < 0 on failure
`socket` and every later call that returns an fd returns **-1** and sets `errno` on failure — not 0. The right check is `if (lfd < 0)`. fds 0, 1, 2 are taken by stdin/stdout/stderr when the process starts, so normal allocation begins at 3 — which is why, later, when we use `ss`, you'll see the listening socket's fd is 3.
:::

## Step 2: bind() — pin the fd to a local address

An fd alone can't receive connections yet; you have to tell the kernel "this socket listens on which address, which port". That's `bind`:

```c
struct sockaddr_in addr;
memset(&addr, 0, sizeof(addr));
addr.sin_family      = AF_INET;
addr.sin_addr.s_addr = htonl(INADDR_ANY);   /* listen on all NICs */
addr.sin_port        = htons(PORT);          /* 13013 */
bind(lfd, (struct sockaddr *)&addr, sizeof(addr));
```

`sockaddr_in` is the "IPv4 address" struct, with three fields to fill: protocol family (`sin_family = AF_INET`), IP address (`sin_addr`), port (`sin_port`). `INADDR_ANY` is a special value meaning "listen on every NIC's IP" — your machine may have several IPs across lo (127.0.0.1), eth0 (192.168.x.x), wlan0, and so on; `INADDR_ANY` grabs them all at once. If you fill in one specific IP, it only listens on that NIC.

There's a **byte-order pitfall every newcomer hits**, worth pausing on. Note the two calls `htonl(INADDR_ANY)` and `htons(PORT)` — they're not decoration. TCP/IP mandates multi-byte values on the wire use **big-endian** (network byte order), while your x86/ARM CPU is little-endian (host byte order). Port 13013 in little-endian memory is laid out as `05 33 00 00` (low byte first), and in big-endian as `00 00 33 05` — if you stuff the host-order port straight into `sin_port`, the kernel and the peer both parse it as network order, and the port is completely wrong. `htonl` (host to network long, 32-bit, for IPs) and `htons` (host to network short, 16-bit, for ports) do this byte swap — no-op on big-endian machines, swap on little-endian — so writing them is **correct regardless of CPU**.

So the rule of thumb: **every multi-byte integer (IP, port) going into a `sockaddr_in` goes through `htonl`/`htons`**, never a raw host-order value. The other direction — reading from the kernel for human display — uses `ntohl`/`ntohs` (network to host) to flip back.

## Step 3: listen() — mark as passive, and the two queues enter

After `bind`, this socket still can't take connections — it's only "tied to an address". `listen` turns it into a **passive listening** socket, telling the kernel "start accepting connections initiated toward this address":

```c
listen(lfd, 64);   /* second argument is backlog */
```

What's really worth explaining about `listen` is its second argument, **backlog**, and the **two queues** the kernel keeps behind it. This is one of the most misunderstood points in socket programming.

When a client initiates a TCP connection to your server, it first goes through the **three-way handshake**: the client sends SYN, your kernel replies SYN-ACK, the client replies ACK — handshake done, connection established. During this, the kernel maintains two queues for this listening socket:

- **SYN queue (half-open queue)**: connections that received a client SYN, replied SYN-ACK, and are still waiting for the client's final ACK. The handshake is **not complete**.
- **Accept queue (fully-established queue)**: connections whose three-way handshake has completed, waiting for you to `accept` them away. The handshake **is complete**.

And the `listen` backlog argument **governs the capacity of the Accept queue — not, as many assume, the SYN queue**. `listen(lfd, 64)` means: at most 64 fully-handshaked-but-not-yet-`accept`ed connections may queue; beyond that, newly completed connections get dropped or RSTed by the kernel. The SYN queue length is a separate matter, governed by `/proc/sys/net/ipv4/tcp_max_syn_backlog`.

Why does this distinction matter? Because the moment you think backlog governs the SYN queue, you reach for the wrong knob when debugging "connections stuck in SYN_RECV" or "SYN flood" — those need `tcp_max_syn_backlog` and SYN cookies, nothing to do with backlog. The `man 2 listen` wording itself only says "a limit on the number of sockets in the accept queue" — it never mentions the SYN queue.

Once we actually run this later, we'll use `ss` to snapshot this listening socket's state, and you'll see exactly where backlog lands inside the kernel.

## Step 4: accept() — pull out a fully-handshaked connection

```c
int cfd = accept(lfd, NULL, NULL);
```

What `accept` does is **take one already-handshaked connection off the Accept queue** and return a **brand-new fd** to represent that connection. There's a key point newcomers get muddy on, and we must nail it:

**The listening fd (`lfd`) and the connection fd (`cfd`) are two completely different fds doing two completely different jobs.** `lfd` is the "greeter at the door"; its only job is to `accept` new connections out — it **does not send or receive data itself**. `cfd` is "a guest who has already come in"; you `read`/`write` on that fd to talk to that guest. A server has exactly one `lfd` for its entire life (it may `accept` out thousands of `cfd`s); each incoming connection gets one `accept`, yielding one new `cfd`. Mixing these two up (say, `write`-ing data to `lfd`) is a classic beginner blunder.

The two trailing arguments to `accept` are `NULL` here, meaning "I don't care about this client's address"; if you want to log the client's IP and port, pass in a `sockaddr_in` for the kernel to fill.

There's also an **extremely common misconception** to break here: **many people think "it's `accept` that completes the three-way handshake" — that's wrong**. The handshake is done by the kernel automatically after `listen`: client sends SYN, kernel replies SYN-ACK, client replies ACK, this whole process **needs no participation from your program whatsoever**, and happens before `accept` is ever called. Handshaked connections first queue in the Accept queue; your `accept` just "leads away" an already-ready connection from the queue. If your program is stuck somewhere else and doesn't call `accept` for a long time, handshakes still complete in the kernel and connections still pile up in the queue — they only get refused once the queue hits the backlog cap. We'll unpack this timing in the next section.

## Step 5: read()/write() — send and receive on the connection fd

Once you have `cfd`, it's just "an fd you can read and write" — same syscalls as reading and writing files:

```c
char buf[4096];
ssize_t n = read(cfd, buf, sizeof(buf));   /* read what the peer sent */
write(cfd, buf, n);                         /* write it back (echo) */
```

`read` returns **the number of bytes actually read**; `0` means **the peer closed the connection normally** (TCP's FIN has been processed, this is "end of stream", not an error); `-1` means an error. `write` sends data back, and it too may write only partially (especially with non-blocking or large volumes); the teaching version simplifies, and the next section shows the full loop.

That completes the five steps. Let's chain them into a runnable server and actually run it.

## What really happens during establishment: getting the handshake-accept timing straight

Before pasting the full code, there's a question running through "connection establishment" that's worth untangling on its own — because once it makes the relationship between `connect` (client), the handshake (kernel), and `accept` (server) clear, everything after falls into place.

Say a client wants to connect to our server. The client calls `connect(fd, server_addr, ...)` — the essence of that call is **telling the kernel to send SYN for you**, kicking off the three-way handshake. Then:

1. The client kernel sends **SYN** → arrives at the server kernel.
2. The server kernel (because we `listen`ed) auto-replies **SYN-ACK**, and records this "half-open connection" in the **server's SYN queue**.
3. The client kernel receives SYN-ACK, replies the final **ACK** → arrives at the server kernel.
4. The server kernel receives ACK — **the three-way handshake is complete** — and moves this connection from the SYN queue to the **Accept queue**.
5. The server program calls `accept()` → takes this connection off the Accept queue and returns a new fd.

Note steps 2–4 happen **entirely in the kernel; your server program hasn't lifted a finger**. Your `accept` only enters at step 5 — it just "leads the guest in". That's why "accept completes the handshake" is wrong — the handshake finished at step 4, `accept` is at step 5. Grasp this timing, and you understand why backlog governs the Accept queue (the step-4 product) rather than the SYN queue (the step-2 product).

## The classic echo server: full code and a real run

Stitch the five steps together and you get a complete classic echo server, written in the plainest C style — raw fds, manual `close`, `errno` + `perror`:

```c
/* Classic C-style echo server: raw fds, manual close, errno + perror */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PORT    13013
#define BACKLOG 64
#define BUFSZ   4096

int main(void) {
    signal(SIGPIPE, SIG_IGN);   /* see "two biting details" below */

    int lfd = socket(AF_INET, SOCK_STREAM, 0);          /* step 1 */
    if (lfd < 0) { perror("socket"); return 1; }

    int yes = 1;
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port        = htons(PORT);
    if (bind(lfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {  /* step 2 */
        perror("bind"); return 1;
    }
    if (listen(lfd, BACKLOG) < 0) { perror("listen"); return 1; } /* step 3 */

    printf("classic echo server on 0.0.0.0:%d (pid %d)\n", PORT, getpid());

    for (;;) {
        int cfd = accept(lfd, NULL, NULL);               /* step 4 */
        if (cfd < 0) { if (errno == EINTR) continue; perror("accept"); continue; }

        char buf[BUFSZ];
        for (;;) {                                        /* step 5 */
            ssize_t n = read(cfd, buf, BUFSZ);
            if (n <= 0) break;        /* 0 = peer closed, <0 = error */
            write(cfd, buf, n);
        }
        close(cfd);                   /* ★ manual close — miss this and you leak an fd */
    }
}
```

Build it with `gcc -O2 classic_server.c -o server`, run it, and write a client to connect and send a couple of messages:

```text
$ ./client "hello from classic client"
echo <- 'hello from classic client'
$ ./client "the quick brown fox"
echo <- 'the quick brown fox'
```

Echo works. Now do something more interesting — while the server is running, use `ss` to "snapshot" this listening socket out of the kernel:

```text
$ ss -tlnp | grep 13013
LISTEN 0      64           0.0.0.0:13013      0.0.0.0:*    users:(("server",pid=283539,fd=3))
```

There's a lot in those columns. Matching them against the concepts from step 3: `LISTEN` is the socket state; `0.0.0.0:13013` is the address we `bind` (`INADDR_ANY` + port 13013); `fd=3` is exactly "stdin/stdout/stderr took 0/1/2, so the first new fd is 3". The key part is the two middle numbers `0` and `64` — for a LISTEN-state socket, **the first column is the Accept queue's current length (0, since it was just `accept`ed away with no backlog), and the second is the backlog cap (64, exactly what we set with `listen(lfd, 64)`)**. See — the "backlog = Accept queue cap" from step 3 is something you can see directly here, not an abstract concept.

::: tip Build and verify it yourself
The full code for this piece is in `code/volumn_codes/vol8/networking/00-traditional-socket/` (`classic_server.c` + `classic_client.c` + `CMakeLists.txt`). Build and run it yourself — don't just read:

```bash
cd code/volumn_codes/vol8/networking/00-traditional-socket
# option A: one-line gcc (what the article uses)
gcc -O2 -Wall -Wextra classic_server.c -o echo_server
gcc -O2 -Wall -Wextra classic_client.c -o echo_client
# option B: CMake
cmake -S . -B build && cmake --build build
```

Then run `./echo_server` in one terminal, `./echo_client "hello"` in another; seeing `echo <- 'hello'` means it works. Open a third terminal and run `ss -tlnp | grep 13013` to double-check that `LISTEN 0 64 ... fd=3` line. Port 13013 is above 1024, so no sudo needed.
:::

## Two details that bite: SIGPIPE and SO_REUSEADDR

This classic style runs, but there are two details that **never show up in testing and only bite in production or on restart** — they're the "it's in the man page but no one warns you" kind of field knowledge, and I have to flag them here.

**The first is SIGPIPE**. There's a classic TCP scenario: the client exits abnormally, and your server blithely keeps `write`-ing to that connection. Writing to a "peer already closed" socket makes the kernel send you a `SIGPIPE` signal, and `SIGPIPE`'s default action is **to terminate the process outright** — your server dies with no error log, just silently gone. That's a pitfall that can cost you a whole day to debug. The fix is for the server to `signal(SIGPIPE, SIG_IGN)` on its very first line and ignore it; afterwards, `write`-ing to a closed fd instead returns `-1` with `errno = EPIPE`, and you handle it like any ordinary error instead of getting murdered by a signal. That's what the first line of `main` above does.

**The second is SO_REUSEADDR**. You kill the server and try to restart immediately, and you often hit `bind: Address already in use` — even though the previous process is clearly gone. The reason is that some connections from the previous server are still in the **TIME_WAIT** state (the active closer holds for about 60 seconds, to make sure the peer got the ACK for its final FIN), and during that window the port is still "occupied". The fix is `setsockopt(SO_REUSEADDR)` before `bind`, allowing reuse of addresses in TIME_WAIT — that's the line after `socket` above. Essential for the frequent restarts of development. Note it only solves TIME_WAIT; it can't let two processes `listen` on the same port simultaneously (that needs `SO_REUSEPORT`, a different mechanism).

The common thread between these two: **neither makes your program "crash in front of you right away"** — SIGPIPE silently kills the process, TIME_WAIT only bites on restart. So they're especially easy to leave outside of testing, and especially often stepped on in production.

## Wrap-up: this runs, but it's dirty

In this piece we used the plainest C-style socket to walk through the server's five steps and TCP establishment from scratch. A few key takeaways: `socket` gets an fd, `bind` pins the address, `listen` starts listening and sets backlog (governing the Accept queue), `accept` pulls out a handshaked connection and returns a new fd, `read`/`write` send and receive; the listening fd and the connection fd are different things; the three-way handshake is done by the kernel before `accept`, and `accept` just leads the guest in; byte order needs `htonl`/`htons`; SIGPIPE and SO_REUSEADDR are two server staples.

But you should also be able to smell this style — **it runs, but it's dirty**. A raw `int fd` gets passed everywhere, and any early `return` in scope skips the trailing `close(cfd)`, leaking the fd; error handling is scattered `errno` + return codes, a "return value + errno" pair at every step, and you have to remember yourself which one blew up; after `perror` prints the log, error recovery is entirely on the programmer's conscience. The BSD socket API was designed in 1983 and doesn't care about any of this — that's the job of later languages and libraries.

In the next piece (01) we clean it up with Modern C++: an RAII `unique_fd` makes "forgetting `close`" impossible, and `std::expected` packs errors and values into the type system. Once that's done, looking back at this traditional code, you'll see every place it's "dirty", Modern C++ has a corresponding, type-safe retort. Later we'll come back to this server and ask it one question: if there are many clients, can "handle one connection at a time" hold up? — that's the key that leads us to `epoll`.

## References

- [man 2 socket](https://man7.org/linux/man-pages/man2/socket.2.html) / [bind](https://man7.org/linux/man-pages/man2/bind.2.html) / [listen](https://man7.org/linux/man-pages/man2/listen.2.html) / [accept](https://man7.org/linux/man-pages/man2/accept.2.html) — the authoritative definitions of the five-step API; the `listen` backlog wording literally says accept queue
- [man 7 socket](https://man7.org/linux/man-pages/man7/socket.7.html) / [man 7 tcp](https://man7.org/linux/man-pages/man7/tcp.7.html) — socket options and the TCP state machine (including TIME_WAIT)
- [Beej's Guide to Network Programming](https://beej.us/guide/bgnet/) — the classic intro to traditional C socket programming; this piece's style and orientation are deeply influenced by it
- [W. Richard Stevens, *UNIX Network Programming, Volume 1*](https://www.pearson.com/en-us/subject-catalog/p/unix-network-programming-volume-1-the-sockets-networking-api/P20000000C9XXX) — the original source of the five-step model and the two queues
- [ss(8)](https://man7.org/linux/man-pages/man8/ss.8.html) — the tool for inspecting the kernel's socket tables; this piece uses it to snapshot the LISTEN socket's backlog
- [Modern socket wrapping: RAII and std::expected (next piece, 01](./01-modern-socket-wrapping.md)) — using Modern C++ to clean up this piece's raw fds and scattered errno
