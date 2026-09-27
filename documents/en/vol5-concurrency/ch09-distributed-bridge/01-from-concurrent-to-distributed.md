---
chapter: 9
cpp_standard:
- 17
- 20
description: Understand the fundamental differences between standalone concurrency
  and distributed systems—partial failure, unreliable networks, and clock skew—and
  how these differences shape the choice of concurrency model
difficulty: advanced
order: 1
platform: host
prerequisites:
- Actor Model and Message Passing
- Channels and the CSP Model
- Debugging Techniques for Concurrent Programs
reading_time_minutes: 24
related:
- A First Look at Distributed Consistency Primitives
tags:
- host
- cpp-modern
- advanced
- 进阶
- 异步编程
- atomic
- mutex
title: From Standalone Concurrency to Distributed Systems
translation:
  source: documents/vol5-concurrency/ch09-distributed-bridge/01-from-concurrent-to-distributed.md
  source_hash: 28eff8fc65d0bf1bf7c886faffaf35168405bbb1ff34fe9c22eeb1142cd0048b
  translated_at: '2026-09-26T08:48:09+00:00'
  engine: anthropic
  token_count: 16000
---
# From Standalone Concurrency to Distributed Systems

> ℹ️ **Where this chapter fits**: this chapter is a conceptual tour—no runnable code, no external frameworks. Its goal is to build the mental framework for "standalone concurrency → distributed systems" before you head into the distributed practice in Volume 8—so you know which old habits still work and which have to be torn down and rebuilt.

Throughout this volume we have been talking about concurrency on a single machine—how multiple threads in one process safely share data, how to use atomic operations for lock-free synchronization, and how to use coroutines to keep asynchronous code readable. This knowledge is rock solid, but it all rests on an implicit premise: all threads share the same memory, run on the same operating system, and are managed by the same scheduler.

Reality is harsh. When your service needs to handle more requests or store more data, one machine sooner or later will not be enough—whether CPU power, memory capacity, or network bandwidth, some dimension hits its ceiling first. You have to deploy your service across multiple machines and make them work together. At that point, "concurrency" extends from inside a process out onto the network. What you face is no longer a `std::mutex`, but a lock-coordination service spanning the network; no longer `std::atomic`, but a set of distributed replicas that need to agree on a value.

In this article we talk about what fundamentally changes in the concurrency model when you move from a single machine to a distributed system. We will see that many assumptions taken for granted on a single machine—"messages always arrive", "clocks are always accurate", "an operation either succeeds or fails"—simply do not hold in a distributed environment. This is not to scare you; it is to give you a clear mental framework for facing distributed systems, so you know which old experience still applies and what must be rethought.

## Five Fundamental Differences Between Standalone and Distributed

Let's lay the most critical differences out on the table and walk through them one by one.

### Partial Failure: Others Crash, You Stay Alive

On a single machine, when a thread dies from an uncaught exception or a segfault, the operating system usually takes down the entire process—the process is the basic unit of resource isolation, the thread is not. You can use `std::jthread` (the auto-joining thread introduced in C++20) or write a global signal handler to do some cleanup, but fundamentally, all threads inside a process share the same fate: they live together or die together.

Distributed systems are nothing like that. You have 10 machines, 3 of them suddenly lose power (in reality this happens far more often than you would think), and the remaining 7 must keep serving. This raises a problem that barely exists on a single machine: **partial failure**. An operation may succeed on some machines and fail on others—how do you handle that? Can you safely retry? Do you need to roll back the part that already succeeded?

Trickier still, you cannot always be sure whether the other side actually crashed. You send a request and it times out—did the remote really die, or is the network just slow? Did the request never arrive, or did the response never come back? This **uncertainty** is the most vexing part of distributed systems. In his classic writing on fault-tolerant systems, Jim Gray called these "vanish when you observe them" intermittent faults "Heisenbugs"—by the time you attach a debugger to reproduce one, it may already be gone, because the network happened to recover.

### Unreliable Networks: The Shared-Memory Illusion Vanishes

On a single machine, threads communicate through shared memory. You write a variable, another thread reads it immediately (cache coherence matters, of course, but with correct use of `std::atomic` and memory ordering, this behavior is predictable). The CPU's cache coherence protocol (MESI and its variants) guarantees it. In essence, shared memory is a communication channel that is reliable, ordered, and extremely low latency.

The network is not. Messages can be delayed (and the delay is wildly unpredictable—anywhere from milliseconds to seconds), lost (switches dropping packets, TCP retransmission timeouts), duplicated (caused by retries at the application layer), or even arrive out of order (having taken different routing paths). TCP solves part of the problem—it guarantees reliable, ordered delivery of a byte stream—but it cannot solve everything: if the remote process crashes, the TCP connection breaks, and your "reliable transport" ends right there. Not to mention that many distributed protocols run directly over UDP, where reliability is entirely your own job at the application layer.

The consequence of this difference is profound: on a single machine, you can assume a function call either returns a result or throws an exception—pick one of the two. In a distributed environment, a remote call may return a result, or it may time out—and when it times out, you do not even know whether the other side processed it. Your code must handle this third state: "unknown".

### No Global Clock: You Cannot Tell What Happened First

On a single machine, you can use a `std::atomic<uint64_t>` as a global sequence-number generator: all operations are ordered by their numbers, and whoever has the smaller number happened first. The semantics of `memory_order_seq_cst`, together with the cache coherence protocol, guarantee that all cores see the same sequence numbers (we dug into this topic in ch03).

Distributed systems have no such luxury. Every machine has its own local clock, and these clocks are skewed. Even with NTP (Network Time Protocol) synchronization, you typically get millisecond-level accuracy at best, and clocks drift. Google's TrueTime service (used in Spanner) achieves tighter clock synchronization with GPS and atomic clocks, but that is extraordinarily expensive infrastructure—not everyone can have it.

The consequence of having no global clock: it is very hard to tell which of two events on different machines happened first. On a single machine, a timestamp is unambiguous; in a distributed setting, two events' timestamps can contradict each other—machine A says its operation happened at 10:00:00.100, machine B says its operation happened at 10:00:00.099, yet A's operation may actually have happened earlier than B's (because A's clock runs 2 ms fast). That is why distributed systems build causal order with logical clocks (Lamport clocks, vector clocks) instead of relying on physical time.

### Latency Changes by Orders of Magnitude: From Nanoseconds to Milliseconds

Let's talk in concrete numbers. These are the numbers everyone who builds systems should have burned into their brain:

| Operation | Typical latency |
|------|----------|
| L1 cache access | ~1 ns |
| L2 cache access | ~5 ns |
| Main memory access | ~100 ns |
| Same-datacenter network round trip | ~500,000 ns (0.5 ms) |
| Same-city network round trip | ~1-2 ms |
| International network round trip | ~50-80 ms |

A main-memory access takes about 100 nanoseconds; a same-datacenter network round trip takes about 0.5 milliseconds—nearly a 5,000x gap, three orders of magnitude. Cross international borders and the gap grows even larger. Jeff Dean and Peter Norvig were the first to compile these latency numbers, and Jonas Bonér collected them into the widely circulated reference table. From these numbers the community built a wonderfully intuitive analogy: if an L1 cache access is reaching for a pen on your desk (1 second), then one datacenter network round trip is a 94-mile (roughly 150 km) hike. This is not a change of degree; it is a change of worldview.

What does this latency gap mean? It means many of the optimizations you make on a single machine—say, reducing contention on one cache line—may be completely irrelevant in a distributed setting. Your bottleneck is the network, not memory. Likewise, every network round trip in a distributed system is extremely expensive, which is why distributed protocols lean on batching and pipelining to amortize the per-request cost.

### The Cost of Consistency: From Locking to Consensus

On a single machine, the standard way to protect shared data is locking—`std::mutex`, `std::shared_mutex`, or the lock-free route with `std::atomic`. These operations cost nanoseconds (lock/unlock is typically tens to hundreds of nanoseconds), and their semantics are crystal clear: lock, operate, unlock—three steps.

In a distributed environment, when you want replicas on multiple machines to agree on a value, what you need is a **consensus protocol**—Paxos or Raft, for example. These protocols require multiple rounds of network communication, majority voting, log replication... each act of "consensus" costs milliseconds, four to six orders of magnitude more expensive than a local lock. And they are far harder to implement than a mutex—the correctness of a Paxos implementation is enough to publish a SOSP paper.

None of this means a distributed system is necessarily slower than a single machine. The value of a distributed system is **horizontal scaling**—you raise throughput by adding machines. But every operation that needs strong consistency is bound by the latency of the consensus protocol. Hence one of the core questions in distributed system design: **which operations need strong consistency, and which can accept weaker consistency?**

## From mutex to Distributed Locks

With those differences in mind, let's look at a concrete example: how to move the single-machine "mutex" into a distributed environment.

### The Assumptions Behind a Standalone mutex

A `std::mutex` works because it relies on an entire set of assumptions that are taken for granted on a single machine—all threads share the same memory, all threads are scheduled by the same operating system, and the lock holder is definitely still alive (if it died, the whole process died, and the lock is moot anyway). These assumptions hold on a single machine.

In a distributed environment, not one of them holds: multiple processes run on different machines, each with its own independent scheduler, and any process can crash at any moment while the others keep running. So when you need mutual exclusion across machines, it must be implemented in a completely different way.

### A Redis-Based Distributed Lock

The simplest and most common distributed lock implementation is based on Redis. The core idea is Redis's `SET key value NX PX timeout` command—`NX` means "set only if the key does not exist" (that is the locking), and `PX` sets an expiry time (the lock's timeout safety net). The value is usually a unique identifier (a UUID, for instance) that identifies the lock holder and prevents mistaken release.

Let's look at a simple distributed lock implemented in C++ through the `hiredis` library.

First, the acquisition logic:

```cpp
#include <string>
#include <chrono>
#include <random>

/// @brief A simple Redis-based distributed lock
class RedisDistributedLock {
public:
    RedisDistributedLock(redisContext* context,
                         const std::string& lock_key,
                         int timeout_ms)
        : context_(context)
        , lock_key_(lock_key)
        , timeout_ms_(timeout_ms)
        , token_(generate_token())
        , locked_(false)
    {}

    /// @brief Try to acquire the lock; returns true on success
    bool try_acquire()
    {
        // SET lock_key token NX PX timeout
        // NX: set only if the key does not exist
        // PX: set an expiry time (in milliseconds)
        // Use hiredis' %s format arguments to avoid injection risks
        auto* reply = static_cast<redisReply*>(
            redisCommand(context_, "SET %s %s NX PX %d",
                         lock_key_.c_str(), token_.c_str(), timeout_ms_));

        if (reply == nullptr) {
            return false;
        }

        bool success = (reply->type == REDIS_REPLY_STATUS
                       && std::string(reply->str) == "OK");
        freeReplyObject(reply);
        locked_ = success;
        return success;
    }

    /// @brief Release the lock (only the holder may release it)
    void release()
    {
        if (!locked_) {
            return;
        }

        // Use a Lua script to guarantee atomicity:
        // delete the key only when its value equals our token
        // prevents releasing someone else's lock by mistake
        const char* lua_script = R"(
            if redis.call("GET", KEYS[1]) == ARGV[1] then
                return redis.call("DEL", KEYS[1])
            else
                return 0
            end
        )";

        auto* reply = static_cast<redisReply*>(
            redisCommand(context_,
                "EVAL %s 1 %s %s",
                lua_script, lock_key_.c_str(), token_.c_str()));

        if (reply != nullptr) {
            freeReplyObject(reply);
        }
        locked_ = false;
    }

    ~RedisDistributedLock()
    {
        // RAII: automatically release the lock on destruction
        release();
    }

private:
    /// @brief Generate a unique lock-holder identifier
    static std::string generate_token()
    {
        // Build a unique token from a random number + a timestamp
        std::random_device rd;
        std::mt19937_64 gen(rd());
        auto now = std::chrono::steady_clock::now().time_since_epoch().count();

        return std::to_string(now) + "-" + std::to_string(gen());
    }

    redisContext* context_;
    std::string lock_key_;
    int timeout_ms_;
    std::string token_;
    bool locked_;
};
```

Look at the acquisition path first. `try_acquire()` sends the `SET lock_key token NX PX timeout` command through hiredis' formatting API, and several points matter here. First, note that we pass arguments through hiredis' `%s` placeholders instead of concatenating strings by hand—if you splice the key and token directly into the command string, a key containing spaces or special characters can lead to command injection. Next, the `NX` option guarantees the set succeeds only when the key does not yet exist—that is where the mutual exclusion comes from: whoever sets it first gets the lock. `PX timeout` sets the expiry time, a safety net: if the lock holder crashes (the process dies, the machine loses power), the lock frees itself after the timeout instead of being occupied forever. Finally, the value is a unique token rather than some plain string; the token identifies the lock holder.

The release path is subtler: we use a Lua script to make "check the token, then delete the key" atomic. Why do it this way? Because if it were two separate steps (GET to check, then DEL to delete), another operation could slip in between—your GET confirms the lock is yours, but before your DEL lands, the lock happens to time out and gets acquired by someone else, and your DEL deletes their lock. Redis executes Lua scripts atomically, which sidesteps this problem.

Using it is straightforward:

```cpp
void do_synchronized_work(redisContext* redis)
{
    // Try to acquire the distributed lock with a 5-second timeout
    RedisDistributedLock lock(redis, "my_resource_lock", 5000);

    if (!lock.try_acquire()) {
        // Didn't get the lock; someone else is at work
        std::cerr << "获取分布式锁失败，稍后重试\n";
        return;
    }

    // Lock acquired; safely operate on the shared resource
    // ...

    // Leaving the scope: the destructor releases the lock automatically (RAII)
}
```

Great—so far everything looks perfect. But this is far from the end of the story; the real pitfalls are still ahead.

### The Essential Dilemma of Distributed Locks

What is wrong with the implementation above? Plenty.

**Problem one: lock timeouts versus GC pauses.** Suppose the lock timeout is 5 seconds. After your process acquires the lock, it goes through a long GC (if you are running Java, a Stop-The-World pause can reach seconds), or it gets suspended by the OS scheduler (C++ programs do not GC, but you can still hit page swapping or CPU contention); 5 seconds later, the lock in Redis times out and someone else takes it. When your process resumes execution, it still believes it is the lock holder—two processes are now operating on the shared resource at the same time, and mutual exclusion is broken.

**Problem two: Redlock is not safe enough either.** Salvatore Sanfilippo, the author of Redis, proposed the Redlock algorithm—run the distributed lock over multiple independent Redis instances, and the client must successfully acquire the lock on a majority (N/2 + 1) of the instances to count as success. But Martin Kleppmann (yes, the one who wrote *Designing Data-Intensive Applications*) published a famous rebuttal, [How to do distributed locking](https://martin.kleppmann.com/2016/02/08/how-to-do-distributed-locking.html). His central argument: Redlock's safety depends on an assumption of clock synchronization—it assumes the clock skew across Redis nodes is bounded. But distributed-system clocks are unreliable (as we said earlier), so this assumption breaks in extreme cases. More critically, Redlock provides no **fencing token**—a monotonically increasing number that lets the resource itself tell which lock holder is the newer one.

> ⚠️ **Pitfall Warning**
> If you use Redis for distributed locking, make sure you understand where it fits: **efficiency-first** scenarios (preventing duplicate computation, rate limiting) are fine; for **correctness-first** scenarios (financial transfers, inventory deduction), the Redis distributed lock is not safe enough—use a lock service built on a consensus protocol.

**Problem three: a distributed lock is fundamentally different from a mutex.** A `std::mutex` provides an absolute guarantee of exclusion—while the lock is held, no other thread can possibly get in (unless you have a bug). A distributed lock cannot do that—it can only provide exclusion "in most cases", and under extreme conditions such as network partitions, clock drift, or process pauses, the exclusion may be broken. This is not an implementation problem; it is a fundamental limitation of distributed systems.

So if you need strong guarantees, you should use a consensus-based coordination service such as ZooKeeper or etcd. They guarantee consistency with the ZAB (ZooKeeper) or Raft (etcd) protocol, and implement distributed locks with ephemeral nodes and watchers—when the client session disconnects, the ephemeral node is deleted automatically, which is more reliable than Redis's timeout mechanism. They also support fencing tokens natively (through data version numbers or the ZXID), which avoids the expired-lock problem mentioned above.

### Comparing Redis and ZooKeeper/etcd Distributed Locks

Let's summarize the key differences discussed above into one table, to help you choose for your actual scenario:

| Dimension | Redis (single instance/Redlock) | ZooKeeper / etcd |
|------|----------------------|-------------------|
| Consistency model | Asynchronous replication, may lose data | Consensus protocol (ZAB/Raft), strongly consistent |
| Lock safety | Depends on clocks, not safe enough | Consensus-backed, can be paired with fencing tokens |
| Performance | Extremely high (in-memory operations) | Lower (requires majority acknowledgment) |
| Operational complexity | Low | High (you maintain a consensus cluster) |
| Suitable scenarios | Efficiency-first (dedup, rate limiting) | Correctness-first (finance, inventory) |

To sum up: the distributed lock is a useful tool, but it is not an equivalent replacement for `std::mutex`. In a distributed environment, "mutual exclusion" turns from a deterministic guarantee into a probabilistic one—you need to choose the right tool based on business requirements, and either design to tolerate inconsistency under extreme conditions, or use a mechanism like the fencing token as a last line of defense.

## Engineering Intuition for the CAP Theorem

There is no talking about distributed systems without the CAP theorem. Conjectured by Eric Brewer in 2000 (and proved by Seth Gilbert and Nancy Lynch in 2002), it is a fundamental constraint on distributed system design. Let's not rush into the definition—let's understand it through a scenario first.

### What the Three Properties Are

Start with **Consistency**. It requires that all clients see the same data at every moment—you write a value to node A, immediately read from node B, and you should get the latest value. This is not "eventually consistent"; it is "consistent at all times", the strongest consistency guarantee, equivalent to linearizability.

Next, **Availability**. It requires that every request receive a non-error response—the system neither refuses service nor returns errors; even when the network is in trouble, every live server does its best to answer your request. Note that availability only cares about "can I get a response"; whether the data in that response is up to date is consistency's business.

Finally, **Partition Tolerance**. When a network partition occurs (some machines can no longer communicate with each other), the system keeps working. In a distributed system, a network partition is not a question of "whether it will happen" but of "when it will happen"—the network is always unreliable, so partition tolerance is essentially mandatory.

### Why You Cannot Have All Three

The CAP theorem says: in a distributed system, when a network partition occurs, you can choose only Consistency (C) or Availability (A); you cannot guarantee both at once.

Why? A concrete scenario explains it. Suppose you have two servers, S1 and S2, each holding a replica of the data. Under normal conditions, S1 forwards writes to S2, and read requests on both sides return the latest data. Now the network partitions—S1 and S2 can no longer communicate.

At this point a client sends a write request to S1. S1 has two options:

If S1 chooses to **accept the write even though it cannot sync to S2**, then S1 has the new data while S2 still has the old. Read requests on S2 will now return stale data—consistency is broken, but availability survives (S2 did not refuse service). That is the **AP** choice.

If S1 chooses to **reject the write (because it cannot sync to S2)**, consistency survives (no write takes effect on only half the nodes), but availability is broken (the client receives an error response). That is the **CP** choice.

There is no third option. You cannot both accept writes and guarantee consistency while unable to synchronize—that is a logical contradiction.

### Choosing Between CP and AP

With the core idea of CAP in mind, let's look at how a few real systems choose.

A typical CP system is ZooKeeper. When a network partition occurs and the ZooKeeper cluster cannot reach a quorum, it refuses service—better unavailable than returning inconsistent data. That is reasonable for its role as a coordination service (storing configuration, running leader election, providing distributed locks): these scenarios place an extremely high premium on correctness, and being briefly unavailable beats being wrong.

On the other side, Cassandra is the representative of AP systems. Its design philosophy is "always available"—even during a network partition, every node keeps accepting reads and writes, except that it may return stale data. Once the network recovers, background read repair and anti-entropy mechanisms bring the replicas back into eventual agreement. For many internet applications this is reasonable: a one-second delay on social media (seeing stale data) is far better than "service unavailable".

> ⚠️ **Pitfall Warning**
> Do not treat CAP as a binary either-or choice. In reality, the network is fine (no partition) the overwhelming majority of the time, and the system can deliver decent consistency and availability simultaneously. CAP only forces the either-or choice under the extreme condition of a network partition. Many modern systems support different choices at the level of different operations and different configurations—for example, you can configure Cassandra with QUORUM reads/writes (consistency-leaning) or ONE reads/writes (availability-leaning).

## From Inter-Thread Communication to Network Communication

Looking back, the gap between standalone concurrency and distributed concurrency is huge—but from the perspective of communication models, there is a very elegant transition between them.

On a single machine, the most natural way for threads to communicate is **shared memory plus locks**—also the model most of this volume has discussed. But you may remember that in ch07 we discussed the Actor model and the CSP/Channel model. The core idea of these models: **don't communicate by sharing memory; instead, share memory by communicating**.

This idea matters even more in a distributed environment. Distributed systems have no shared memory—you cannot have processes on two machines share one `std::mutex`. They can only coordinate through network messages. That is why the Actor model and the CSP model are naturally designed for distributed scenarios: an Actor can live locally or on a remote machine; a message can be an in-process function call or an RPC request over the network. From the programming model's point of view, there is no essential difference between them.

This is why many distributed-system frameworks chose the Actor model (Akka and Orleans, for example)—it defers the "local or remote" decision to deployment time instead of hard-coding it into program logic. You write an Actor's message-handling logic locally, place it on different machines at deployment, and the code barely needs to change.

In the modern C++ ecosystem, the key infrastructure connecting "concurrency" and "distributed" is the **RPC framework**, and the most mainstream of these is gRPC. gRPC uses Protocol Buffers to define services and message formats, automatically generates client- and server-side stub code, uses HTTP/2 as the transport underneath, and supports streaming communication. In essence it is a "function call" across the network—you invoke a remote method just like calling a local function (with important semantic differences, of course, such as timeouts and retries).

From the concurrency model's point of view, every gRPC call can be seen as message passing between Actors: the client Actor sends a request message, and the server Actor receives the message, processes it, and returns a response message. Wrap gRPC's asynchronous API in C++20 coroutines (the next article shows this), and you can write distributed concurrent code in a very natural way—almost the same structure as writing a local coroutine, except the underlying transport has changed from a function call to a network request.

## Where We Are

In this article we did something very important: we built the cognitive bridge between standalone concurrency and distributed systems. We saw the five fundamental differences—partial failure, unreliable networks, no global clock, latency changing by orders of magnitude, and the soaring cost of consistency—each one deeply shaping the choice of concurrency model. Through the concrete case of the distributed lock, we understood the evolution from `std::mutex` to Redis to ZooKeeper/etcd, and grasped the key insight that "a distributed lock is not an equivalent replacement for a mutex". The CAP theorem gave us the basic constraint framework for distributed design, and the Actor/Channel models provide the programming paradigm for a smooth transition from standalone concurrency to distributed concurrency.

But understanding the differences is only the first step. The next article enters the core difficulty of distributed systems—**consistency**. When replicas on multiple machines need to agree on a value, things are far more complicated than "just add a lock". We will see the full spectrum from linearizability to eventual consistency, get to know the core ideas of consensus protocols such as Paxos/Raft, and use gRPC + C++20 coroutines to show the direction for writing distributed communication code in C++.

## References

- [Designing Data-Intensive Applications — Martin Kleppmann](https://dataintensive.net/) — widely regarded as the best introductory book on distributed systems; its treatment of CAP, consistency, and consensus protocols is superb
- [CAP Theorem — Wikipedia](https://en.wikipedia.org/wiki/CAP_theorem) — the formal definition and history of the CAP theorem
- [How to do distributed locking — Martin Kleppmann](https://martin.kleppmann.com/2016/02/08/how-to-do-distributed-locking.html) — the classic rebuttal to Redlock; introduces the concept of the fencing token
- [Latency Numbers Every Programmer Should Know — Jonas Bonér](https://gist.github.com/jboner/2841832) — an intuitive comparison of operation latencies (original numbers from Jeff Dean / Peter Norvig)
- [Is Redlock safe? — Salvatore Sanfilippo (antirez)](http://antirez.com/news/101) — the Redis author's response to Kleppmann's critique
- [Raft Consensus Algorithm](https://raft.github.io/) — official resources for the Raft protocol, including a visualization demo
