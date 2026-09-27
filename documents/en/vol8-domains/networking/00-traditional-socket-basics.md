---
title: "Traditional socket programming: the server's five steps and TCP connection setup — the classic style we learned from Stevens"
description: "Build a working echo server with the plainest C-style BSD socket API, and take the five steps — socket/bind/listen/accept/read-write — plus the kernel internals of the TCP three-way handshake seriously: byte order, listen's two queues and backlog, the difference between a listening fd and a connection fd, and why 'accept completes the handshake' is a common misconception. This is the network foundation that hasn't changed in decades — however flashy Asio or coroutines get, underneath it's still these five steps"
chapter: 8
order: 0
platform: host
difficulty: intermediate
cpp_standard: [20, 23]
reading_time_minutes: 16
related:
  - "Modern socket wrapping: RAII, std::expected, and the C10K that thread-per-connection can't survive"
  - "epoll: Linux I/O multiplexing — from poll's bottleneck to the interest list and ready list"
tags:
  - host
  - cpp-modern
  - intermediate
  - 网络编程
translation:
  source: documents/vol8-domains/networking/00-traditional-socket-basics.md
  source_hash: 6b0de19ae50a44156023de7142e98c25db1f6d12746717ff1750f2d703bd9396
  translated_at: '2026-09-26T04:35:31+00:00'
  engine: anthropic
  token_count: 10300
---

# Traditional socket programming: the server's five steps and TCP connection setup — the classic style we learned from Stevens

Honestly, while writing this networking volume, we felt a real urge to thoroughly teach "the plainest version of it all" first. The reason is simple: no matter how elegant Boost.Asio gets, how smooth C++20 coroutines feel, or how forward-looking std::execution is, when you write a network program on Linux, the layer underneath keeps circling back to the same thing — the **BSD socket API**, settled in 1983. `epoll` is an acceleration of it, `Asio` a wrapper around it, but "how a server actually comes up" has barely changed from Stevens's *UNIX Network Programming* to today's kernel. So in this article we touch none of the modern machinery: just the plainest C-style socket, stepping through this **decades-old foundation** one plank at a time.

This article and the next are a pair: this piece (00) covers the **traditional, C-style** way — raw fds, manual `close`, `errno` plus `perror`, exactly the set Beej's Guide and Stevens teach you; the next one (01) brings in Modern C++ (RAII, `std::expected`) to mop up its dirty work. The split is deliberate — some readers already know the traditional style and only want the modern wrapping, so they can jump straight to 01; others want a socket refresher, and this piece is a stable reference for that. The BSD socket API hasn't changed in decades, so once this article is written it basically never needs touching again.

The code in this article is pure C socket, compilable with `gcc`, and every terminal output pasted below was actually run on this machine. We leave concurrency out entirely (that's 01's business) — just a single-threaded echo server, focused on walking the "five steps" cleanly.

## A minimal target: the echo server

Let's fix a minimal target first, to hang every later concept on: an **echo server** — whatever the client connects and sends, the server sends right back, then waits for the next message. It's the "Hello World" of network programming: small enough to have zero business logic, yet it covers the full lifecycle of a TCP connection, from establishment to send and receive.

From the server's point of view, that full lifecycle is the classic **five steps**: `socket → bind → listen → accept → read/write`. We'll walk them one at a time, and at each step we won't just cover "which function to call" — we'll dig into **what actually happens on the kernel side**, because most of the pitfalls in socket programming come from "you thought this function did X, when it actually did Y".

## Step 1: socket() — ask the kernel for a communication endpoint

```c
int lfd = socket(AF_INET, SOCK_STREAM, 0);
```

What `socket()` does is actually simple: **it asks the kernel for a "communication endpoint" and returns a file descriptor (fd) to represent it**. The three arguments answer "which network protocol family, which type, which specific protocol": `AF_INET` is IPv4, `SOCK_STREAM` is a connection-oriented reliable byte stream (that is, TCP), and a `0` in the third argument means "pick the protocol automatically" (with TCP implied above, it picks TCP).

Of course, there's a concept here you can't dodge: **what exactly is an fd**. The Unix motto is "everything is a file"; the kernel keeps a "table of open files" for every process, and an fd is simply an index into that table (a small integer). The fd `socket()` returns is, in essence, a new slot in that table, with a kernel socket object hanging behind the slot. Later, when you call `bind`/`listen`/`accept`/`read`/`write` on that fd, the kernel uses the fd to look up the table, finds the socket object behind it, and operates on that. So the fd is just a "handle" — the real state lives in the kernel.

::: warning On failure, check fd < 0
`socket`, and every later call that returns an fd, returns **-1** and sets `errno` on failure — not 0. The right check is `if (lfd < 0)`. fds 0, 1, and 2 are taken by stdin/stdout/stderr the moment the process starts, so normal allocation begins at 3 — which is why, a bit later, `ss` will show the listening socket's fd as exactly 3.
:::

## Step 2: bind() — pin the fd to a local address

An fd alone can't receive connections yet; you have to tell the kernel "which address and which port this socket listens on". That's `bind`:

```c
struct sockaddr_in addr;
memset(&addr, 0, sizeof(addr));
addr.sin_family      = AF_INET;
addr.sin_addr.s_addr = htonl(INADDR_ANY);   /* listen on all NICs */
addr.sin_port        = htons(PORT);          /* 13013 */
bind(lfd, (struct sockaddr *)&addr, sizeof(addr));
```

`sockaddr_in` is the "IPv4 address" struct, with three fields to fill in: the protocol family (`sin_family = AF_INET`), the IP address (`sin_addr`), and the port (`sin_port`). `INADDR_ANY` is a special value meaning "listen on every NIC's IP on this machine" — your machine may well have several IPs across lo (127.0.0.1), eth0 (192.168.x.x), wlan0, and so on, and `INADDR_ANY` covers them all in one shot; fill in one specific IP instead, and you listen only on that NIC.

Here's a **byte-order pitfall every newcomer steps in**, worth pausing over. Notice the two calls `htonl(INADDR_ANY)` and `htons(PORT)` — they are not decoration. TCP/IP mandates that multi-byte values on the wire use **big-endian**, called network byte order; your x86/ARM CPU, meanwhile, is little-endian. Port 13013 sits in little-endian machine memory as `D5 32 00 00` (low byte first), and in big-endian as `00 00 32 D5` — stuff the host-order port straight into `sin_port`, and the kernel and the peer both parse it as network order, so the port comes out completely wrong. `htonl` (host to network long, 32-bit, for the IP) and `htons` (host to network short, 16-bit, for the port) exist to do exactly this byte flip: a no-op on big-endian machines, a swap on little-endian ones, so writing them is **correct no matter the CPU**.

Hence a rule of thumb that practically writes itself: **every multi-byte integer (IP, port) going into a `sockaddr_in` goes through `htonl`/`htons` first** — never jam in a raw host-order value. In the other direction, reading values out of the kernel for human eyes, flip them back with `ntohl`/`ntohs` (network to host).

## Step 3: listen() — mark it passive, and the two queues take the stage

After `bind`, this socket still can't take connections — so far it's merely "tied to an address". `listen` is what turns it into a **passive listening** socket, telling the kernel "start accepting connections initiated toward this address":

```c
listen(lfd, 64);   /* the second argument is the backlog */
```

What's genuinely worth explaining about `listen` is its second argument, **backlog**, and the **two queues** the kernel maintains behind it. This is the single most misread point in socket programming.

When a client initiates a TCP connection to your server, it first goes through the **three-way handshake**: the client sends SYN, your kernel replies SYN-ACK, the client sends the final ACK — handshake done, connection established. During this process the kernel maintains two queues for the listening socket:

- **SYN queue (half-open connection queue)**: connections that received the client's SYN, replied SYN-ACK, and are still waiting for the client's final ACK. The handshake is **not complete**.
- **Accept queue (fully established queue)**: connections whose three-way handshake is already done, just waiting for you to `accept` them away. The handshake **is complete**.

And the `listen` backlog argument **governs the capacity cap of the Accept queue (the fully established queue)** — not, as many people assume, the SYN queue. `listen(lfd, 64)` means: at most 64 connections that have finished the handshake but not yet been taken away by `accept` may queue up; beyond that, newly completed connections are dropped by the kernel or met with a straight RST. The SYN queue's length is a separate matter, governed by `/proc/sys/net/ipv4/tcp_max_syn_backlog`.

Why does this distinction matter? Because the moment you believe backlog governs the SYN queue, you reach for the wrong medicine when debugging "connections stuck in SYN_RECV" or a "SYN flood attack" — those call for tuning `tcp_max_syn_backlog` and SYN cookies, which have nothing to do with backlog. The `man 2 listen` wording itself says only "a limit on the number of sockets in the accept queue" — it never mentions the SYN queue at all.

Once we get this actually running later, we'll use `ss` to snapshot this listening socket's state, and you'll see with your own eyes where the backlog value lands inside the kernel.

## Step 4: accept() — take out a fully handshaked connection

```c
int cfd = accept(lfd, NULL, NULL);
```

What `accept` does is **take one already-handshaked connection out of the Accept queue (the fully established queue)** and return a **brand-new fd** representing that connection. Here's the key point newcomers get most muddled about, and we must nail it down:

**The listening fd (`lfd`) and the connection fd (`cfd`) are two completely different fds doing two completely different jobs.** `lfd` is the "greeter at the door" — its only role is to `accept` new connections out; it **never sends or receives data itself**. `cfd` is "a guest who has already walked in"; you `read`/`write` on that fd to talk with that guest. A server has exactly one `lfd` for its whole life (it may `accept` out thousands of `cfd`s); each incoming connection gets one `accept` and yields one new `cfd`. Mixing the two up (say, `write`-ing data to `lfd`) is a classic rookie blunder.

The last two arguments to `accept` are `NULL` here, meaning "I don't care about this client's address"; if you want to log the client's IP and port, pass in a `sockaddr_in` for the kernel to fill.

And there's an **extremely common misconception** to shatter right here: **many people believe "it's `accept` that completes the three-way handshake" — that's wrong**. The handshake is done by the kernel automatically once `listen` is in place: the client sends SYN, the kernel replies SYN-ACK, the client replies ACK, and this whole process **needs zero participation from your program** — it happens before `accept` is ever called. Connections that finish the handshake first line up in the Accept queue; your `accept` merely "leads away" a connection that's already ready. If your program is stuck elsewhere and doesn't call `accept` for a long while, handshakes still complete in the kernel and connections still pile up in the queue — new ones get refused only when the pile hits the backlog cap. We'll unpack this timing in the next section.

## Step 5: read()/write() — send and receive on the connection fd

Once you hold `cfd`, it's just "an fd you can read and write" — the very same syscalls you use to read and write files:

```c
char buf[4096];
ssize_t n = read(cfd, buf, sizeof(buf));   /* read what the peer sent */
write(cfd, buf, n);                         /* write it right back (echo) */
```

`read` returns **the number of bytes actually read**; a return of `0` means **the peer closed the connection normally** (TCP's FIN has run its course — that's the "end of stream" signal, not an error); `-1` means something went wrong. `write` sends the data back, and it too may write only part of it (especially when non-blocking or moving large volumes); the teaching version here simplifies that away, and you'll see the full loop in the next section.

With that, the five steps are done. Let's chain them into a runnable server and actually run it.

## What actually happens at connection setup: getting the handshake-versus-accept timing straight

Before we paste the full code, one question running through "connection setup" deserves to be untangled on its own — because once the relationship between `connect` (client), the handshake (kernel), and `accept` (server) is made clear, everything after falls into place.

Suppose a client wants to reach our server. The client side calls `connect(fd, server_address, ...)` — the essence of that call is **having the kernel send SYN on your behalf**, kicking off the three-way handshake. Then:

1. The client kernel sends **SYN** → it arrives at the server kernel.
2. The server kernel (because we called `listen`) automatically replies **SYN-ACK**, and at the same time records this "half-open connection" in the **server's SYN queue**.
3. The client kernel receives the SYN-ACK and sends the final **ACK** → it arrives at the server kernel.
4. The server kernel receives the ACK — **the three-way handshake is complete** — and moves this connection from the SYN queue to the **Accept queue**.
5. The server program calls `accept()` → it takes the connection out of the Accept queue and returns a new fd.

Notice that steps 2–4 happen **entirely inside the kernel — your server program hasn't contributed a single line**. Your `accept` only enters the stage at step 5, and all it does is "lead the guest in". That's why "accept completes the handshake" is wrong — the handshake finished at step 4, while `accept` is a step-5 act. Grasp this timing and you can finally see why backlog governs the Accept queue (the step-4 product) rather than the SYN queue (the step-2 product).

## The classic echo server: full code and a real run

Stitch the five steps above together and you have a complete, classic echo server. We write it in the plainest C style — raw fds, manual `close`, `errno` + `perror`:

```c
/* Traditional C-style echo server: raw fds, manual close, errno + perror */
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
    signal(SIGPIPE, SIG_IGN);   /* see "two details that bite" below */

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
        close(cfd);                   /* ★ manual close — miss it and the fd leaks */
    }
}
```

Build it with `gcc -O2 classic_server.c -o server`, run it, and write a client that connects and fires off two messages:

```text
$ ./client "hello from classic client"
echo <- 'hello from classic client'
$ ./client "the quick brown fox"
echo <- 'the quick brown fox'
```

Echo works. Now for something more interesting — while the server is still running, use `ss` to "photograph" this listening socket straight out of the kernel:

```text
$ ss -tlnp | grep 13013
LISTEN 0      64           0.0.0.0:13013      0.0.0.0:*    users:(("server",pid=283539,fd=3))
```

Those columns carry a lot of information; let's match them against the concepts from step 3: `LISTEN` is the socket state; `0.0.0.0:13013` is the address we `bind`-ed (`INADDR_ANY` + port 13013); `fd=3` is exactly the "stdin/stdout/stderr take 0/1/2, so the first new fd is 3" from earlier. The most telling part is the two middle numbers `0` and `64` — for a LISTEN-state socket, **the first column is the Accept queue's current length (0, because it was just taken away by an accept, nothing backlogged), and the second is the backlog cap (64, exactly what we set with `listen(lfd, 64)`)**. See? The "backlog = Accept queue cap" from step 3 is a physical thing you can look at directly here, not an abstract notion.

::: tip Build and verify it yourself
The complete code for this article lives in `code/volumn_codes/vol8/networking/00-traditional-socket/` (`classic_server.c` + `classic_client.c` + `CMakeLists.txt`). Build and run it yourself rather than just reading:

```bash
cd code/volumn_codes/vol8/networking/00-traditional-socket
# Option A: one-line gcc (what this article uses)
gcc -O2 -Wall -Wextra classic_server.c -o echo_server
gcc -O2 -Wall -Wextra classic_client.c -o echo_client
# Option B: CMake
cmake -S . -B build && cmake --build build
```

Then run `./echo_server` in one terminal and `./echo_client "hello"` in another — once you see `echo <- 'hello'`, it works; open a third terminal, run `ss -tlnp | grep 13013`, and double-check that `LISTEN 0 64 ... fd=3` line from above. Port 13013 is above 1024, so no sudo needed.
:::

## Two details that bite: SIGPIPE and SO_REUSEADDR

This classic style runs, but there are two details of the kind that **never misbehave in testing and only bite in production or on restart** — field knowledge of the "it's in the man page, but nobody volunteers it to you" variety, and we have to flag them here.

**The first is SIGPIPE**. There's a classic TCP scenario: the client exits abnormally, and your server keeps dutifully `write`-ing into that connection. Writing to a socket "whose peer has already closed" makes the kernel send you a `SIGPIPE` signal, and `SIGPIPE`'s default disposition is **to terminate the process outright** — your server dies without a single error log, silently gone. That's a pitfall that can eat a whole day of debugging. The fix is the first line of the server's startup: `signal(SIGPIPE, SIG_IGN)` to ignore it, after which `write`-ing to a closed fd instead returns `-1` with `errno = EPIPE`, so you handle it like any ordinary error rather than getting murdered by a signal. That's exactly what the first line of `main` in the code above does.

**The second is SO_REUSEADDR**. You kill the server and want to restart immediately, and you frequently run into `bind: Address already in use` — even though that previous process is clearly gone. The reason is that some of the old server's connections are still in the **TIME_WAIT** state (the active closer holds on for roughly 60 seconds, making sure the peer received the ACK for its final FIN), and during that window the port is still "occupied". The fix is `setsockopt(SO_REUSEADDR)` before `bind`, allowing reuse of addresses in TIME_WAIT — that's the line right after `socket` in the code above. A must for the restart-heavy development loop. Note that it only solves TIME_WAIT; it cannot let two processes `listen` on the same port simultaneously (that takes `SO_REUSEPORT`, a different mechanism).

Put plainly, the common thread of these two is: **neither makes your program "crash in front of you right away"** — SIGPIPE kills the process silently, TIME_WAIT bites only on restart. That's exactly why they slip past testing so easily, and get stepped on in production so often.

## Wrap-up: it runs, but it's dirty

In this article we used the plainest C-style socket to walk the server's five steps and TCP connection setup from scratch. Let's collect the key items: `socket` obtains an fd, `bind` pins the address, `listen` turns on listening and sets backlog (which governs the Accept queue), `accept` takes out a fully handshaked connection and returns a new fd, `read`/`write` do the sending and receiving; the listening fd and the connection fd are two different things; the three-way handshake is finished by the kernel before `accept` — `accept` just leads the guest in; byte order needs `htonl`/`htons`; and SIGPIPE and SO_REUSEADDR are two server staples.

But you should also catch a whiff of this style's smell — **it runs, but it's dirty**. A raw `int fd` gets passed around everywhere, and any early `return` in the scope skips the trailing `close(cfd)`, so the fd leaks; error handling is scattered `errno` + return codes, every step comes with the "return value + errno" two-piece set, and remembering which step blew up is on you; once `perror` has printed its line, error recovery rests entirely on the programmer's discipline. The BSD socket API was designed in 1983 and it doesn't handle this dirty work — that's the business of later languages and libraries.

In the next article (01) we mop it all up with Modern C++: an RAII `unique_fd` makes "forgetting close" impossible, and `std::expected` packs errors and values into the type system. Once that's written, look back at this traditional code and you'll find that for every place it's "dirty", Modern C++ has a matching, type-safe way to close it out. Further on we'll come back to this server and ask it one question: if the clients multiply, can "handle each connection as it arrives" still hold? — that's the key that leads us to `epoll`.

## References

- [man 2 socket](https://man7.org/linux/man-pages/man2/socket.2.html) / [bind](https://man7.org/linux/man-pages/man2/bind.2.html) / [listen](https://man7.org/linux/man-pages/man2/listen.2.html) / [accept](https://man7.org/linux/man-pages/man2/accept.2.html) — the authoritative definitions of the five-step API; the `listen` backlog wording literally says "accept queue"
- [man 7 socket](https://man7.org/linux/man-pages/man7/socket.7.html) / [man 7 tcp](https://man7.org/linux/man-pages/man7/tcp.7.html) — socket options and the TCP state machine (TIME_WAIT included)
- [Beej's Guide to Network Programming](https://beej.us/guide/bgnet/) — the classic introduction to traditional C socket programming; this article's style and content orientation are deeply influenced by it
- [W. Richard Stevens, *UNIX Network Programming, Volume 1*](https://www.pearson.com/en-us/subject-catalog/p/unix-network-programming-volume-1-the-sockets-networking-api/P20000000C9XXX) — the original source of the five-step model and the two queues
- [ss(8)](https://man7.org/linux/man-pages/man8/ss.8.html) — the tool for inspecting the kernel's socket tables; this article uses it to snapshot the LISTEN socket's backlog
- [Modern socket wrapping: RAII and std::expected (next article, 01)](./01-modern-socket-wrapping.md) — using Modern C++ to mop up this article's raw fds and scattered errno
