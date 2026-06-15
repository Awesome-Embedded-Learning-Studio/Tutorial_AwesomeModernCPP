---
chapter: 9
cpp_standard:
- 17
- 20
description: 'Understanding the fundamental differences between standalone concurrency
  and distributed systems: partial failure, unreliable networks, and clock skew, and
  how these differences affect the choice of concurrency models.'
difficulty: advanced
order: 1
platform: host
prerequisites:
- Actor 模型与消息传递
- Channel 与 CSP 模型
- 并发程序调试技巧
reading_time_minutes: 20
related:
- 分布式一致性原语初探
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
  engine: anthropic
  source: documents/vol5-concurrency/ch09-distributed-bridge/01-from-concurrent-to-distributed.md
  source_hash: f3b7488020472c1d0b8699b7c6803c41cef83a3b7271719bd4b78e2de09ad4ef
  token_count: 3256
  translated_at: '2026-06-13T11:52:44.087482+00:00'
---
# From Standalone Concurrency to Distributed Systems

> ℹ️ **Context**: This chapter is a conceptual overview. It does not include runnable code or introduce external frameworks. Its purpose is to help you build a cognitive framework for "Standalone Concurrency → Distributed Systems" before diving into the practical distributed content in Volume 8—so you know which old experiences still apply and which need to be completely rethought.

Throughout this volume, we have been discussing concurrency on a single machine—how multiple threads within one process safely share data, how to use atomic operations for lock-free synchronization, and how to use coroutines to make asynchronous code readable. This knowledge is very solid, but it is built on an implicit premise: all threads share the same memory, run on the same operating system, and are managed by the same scheduler.

Reality is harsh. When your service needs to handle more requests and store more data, a single machine will eventually be insufficient—whether it's CPU computing power, memory capacity, or network bandwidth, one dimension will hit the ceiling first. You have to deploy services across multiple machines and make them work together. At this point, the problem of "concurrency" expands from intra-process to the network. You are no longer facing a `std::mutex`, but a cross-network lock coordination service; no longer `std::atomic`, but a set of distributed replicas that need to agree on a value.

In this article, we will discuss the fundamental changes in the concurrency model as you move from a standalone machine to a distributed system. We will see that many assumptions taken for granted on a single machine—such as "messages always arrive," "clocks are always accurate," "an operation either succeeds or fails"—completely fail in a distributed environment. This isn't to scare you, but to give you a clear cognitive framework when facing distributed systems, knowing which old experiences are still useful and which must be rethought.

## Five Fundamental Differences Between Standalone and Distributed Systems

Let's lay out the most critical differences and examine them one by one.

### Partial Failure: Others Crash, You Survive

On a single machine, if a thread crashes due to an unhandled exception or segmentation fault, usually the entire process is killed by the operating system—the process is the basic unit of resource isolation, not the thread. You can use `std::jthread` (automatic thread joining introduced in C++20) or write a global signal handler to do some cleanup, but essentially, all threads within a process share the same fate: either they all live, or they all die.

Distributed systems are completely different. You have 10 machines, and 3 of them suddenly lose power (this happens much more often in reality than you think), and the remaining 7 must continue to serve. This introduces a problem that barely exists on a single machine: **partial failure**. An operation might succeed on some machines and fail on others—how do you handle this? Can you safely retry? Do you need to roll back the part that succeeded?

Even trickier, you can't always be sure if the other side has actually crashed. You send a request, and it times out—did the other side really hang, or is the network just slow? Did the request not arrive, or did the response not return? This **uncertainty** is the most headache-inducing part of distributed systems. In his classic treatise on fault-tolerant systems, Jim Gray called these intermittent faults that "disappear upon observation" "Heisenbugs"—when you attach a debugger to reproduce them, they might disappear because the network happens to recover.

### Unreliable Network: The Illusion of Shared Memory Disappears

On a single machine, threads communicate through shared memory. You write to a variable, and another thread can read it immediately (of course, considering cache coherence, but with correct use of `std::atomic` and memory order, this behavior is predictable). The CPU's cache coherence protocol (MESI and its variants) guarantees this. Essentially, shared memory is a reliable, ordered, and extremely low-latency communication channel.

Networks are not. Messages may be delayed (and the delay time can be very uncertain, from a few milliseconds to several seconds), may be lost (network switch packet loss, TCP retransmission timeout), may be duplicated (caused by application layer retries), or may even arrive out of order (taking different routing paths). TCP solves part of the problem—it guarantees reliable, ordered transmission of byte streams—but it doesn't solve everything: if the remote process crashes, the TCP connection breaks, and your "reliable transmission" is over. Not to mention many distributed protocols run directly on UDP, requiring reliability to be guaranteed entirely at the application layer.

The consequence of this difference is profound: on a single machine, you can assume a function call either returns a result or throws an exception, a binary choice; in a distributed environment, a remote call might return a result, or it might time out, and if it times out, you don't even know if the other side actually processed it. Your code must handle this third state—"unknown".

### No Global Clock: Who is First is Unclear

On a single machine, you can use a `std::atomic` as a global sequence generator; all operations are sorted by sequence number, and the smaller the number, the earlier it happened. The semantics of `std::memory_order_release` combined with the cache coherence protocol guarantee that all cores see the same sequence number (we discussed this topic in depth in ch03).

Distributed systems don't have this luxury. Every machine has its own local clock, and these clocks have deviations. Even if you use NTP (Network Time Protocol) for clock synchronization, typically you can only achieve millisecond-level precision, and clocks will drift. Google's TrueTime service (used in Spanner) achieves more precise clock synchronization through GPS and atomic clocks, but that is extremely expensive infrastructure, not available to everyone.

The consequence of no global clock is: it is difficult to judge which of two events occurring on different machines happened first. On a single machine, the timestamp of an event is clear; in a distributed environment, the timestamps of two events may contradict each other—Machine A says its operation happened at 10:00:00.100, Machine B says its operation happened at 10:00:00.099, but actually A's operation might have happened earlier than B (because A's clock is 2ms fast). This is why distributed systems need to use logical clocks (Lamport clocks, Vector clocks) to establish causal order, rather than relying on physical time.

### Latency Scale Change: From Nanoseconds to Milliseconds

Let's speak with specific numbers. These are numbers every system developer should etch into their brain:

| Operation | Typical Latency |
|------|----------|
| L1 Cache Access | ~1 ns |
| L2 Cache Access | ~5 ns |
| Main Memory Access | ~100 ns |
| Same Datacenter Network Round Trip | ~500,000 ns (0.5 ms) |
| Same City Network Round Trip | ~1-2 ms |
| Cross-Continental Network Round Trip | ~50-80 ms |

Main memory access is about 100 nanoseconds, same datacenter network round trip is about 0.5 milliseconds—a difference of almost 5000 times, three orders of magnitude. If it's cross-continental, the gap is even larger. Jeff Dean and Peter Norvig originally compiled this latency data, and Jonas Bonér summarized it into a widely circulated reference table. The community made a very intuitive analogy based on this data: if L1 cache access is compared to reaching out to pick up a pen on a desk (1 second), then a datacenter network round trip is equivalent to hiking 94 miles (about 150 km). This isn't a change in magnitude, this is a change in worldview.

What does this latency difference mean? It means that many optimizations you make on a single machine—such as reducing contention on a cache line—might be completely irrelevant in a distributed scenario. Your bottleneck is on the network, not in memory. Similarly, every network round trip in a distributed system is extremely expensive, so you will see distributed protocols tend to use batching and pipelining to amortize the cost of single requests.

### Cost of Consistency: From Locking to Consensus

On a single machine, a standard way to protect shared data is locking—`std::mutex`, `std::shared_mutex`, or lock-free `std::atomic`. The cost of these operations is in the nanosecond range (lock/unlock is usually tens to hundreds of nanoseconds), and the semantics are very clear: lock, operate, unlock, three steps.

In a distributed environment, if you want replicas on multiple machines to agree on a value, you need a **consensus protocol**—such as Paxos or Raft. These protocols require multiple rounds of network communication, majority voting, log replication... every "consensus" costs milliseconds, four to six orders of magnitude more expensive than single-machine locking. And implementation is far more complex than a mutex—the correctness of a Paxos implementation is enough for a SOSP paper.

This isn't to say distributed systems are necessarily slower than single machines. The value of distributed systems lies in **horizontal scaling**—you can increase throughput by adding machines. But every operation that requires strong consistency is limited by the latency of the consensus protocol. This is why a core issue in distributed system design is: **which operations need strong consistency, and which can accept weak consistency?**

## From mutex to Distributed Locks

Having understood the differences above, let's look at a concrete example: how to move a "mutex" from a single machine to a distributed environment.

### Assumptions of Standalone mutex

A `std::mutex` works because it relies on a set of assumptions taken for granted on a single machine—all threads share the same memory, all threads are scheduled by the same operating system, and the lock holder is definitely still alive (if it dies, the whole process dies, and the lock problem ceases to exist). These assumptions hold on a single machine.

In a distributed environment, none of these assumptions hold: multiple processes run on different machines, each with its own scheduler, and a process may crash at any time while others continue running. So when you need a mutex across machines, you must implement it in a completely different way.

### Redis-based Distributed Lock

The simplest and most common distributed lock implementation is based on Redis. The core idea is to use Redis's `SET` command—`SET key value NX` means "set only if key does not exist" (i.e., lock), `EX` sets an expiration time (i.e., lock timeout protection). The value is usually a unique identifier (like a UUID), used to identify the lock holder and prevent accidental unlocking by others.

Let's look at a simple distributed lock implemented in C++ using the `hiredis` library.

First is the locking logic:

```cpp
// ... (Code implementation details would go here) ...
```

Let's look at the locking part first. `redisCommand` sends the `SET` command through hiredis's formatting interface. There are a few key points here. First, note that we use hiredis's `%s` placeholder to pass arguments, rather than manually splicing strings—if you directly splice the key and token into the command string, once the key contains spaces or special characters, it could lead to command injection issues. Then there is the `NX` option, which guarantees success only if the key does not exist—this is the source of mutual exclusion—whoever sets it successfully first gets the lock. `EX` sets the expiration time, which is a safety net: if the lock holder crashes (process dies, machine loses power), the lock will be automatically released after timeout, preventing it from being held forever. Finally, the value uses a unique token instead of a simple string; this token identifies the lock holder.

Releasing the lock is more subtle; we use a Lua script to guarantee the atomicity of "check token then delete key". Why do this? Because if split into two steps (GET to judge, then DEL to delete), another operation might be inserted in between—your GET confirmed this is your lock, but before DEL, the lock happens to time out and is acquired by someone else, and your DEL deletes someone else's lock. Lua scripts are executed atomically in Redis, avoiding this problem.

Usage is very concise:

```cpp
// ... (Usage example code would go here) ...
```

Great, everything looks perfect so far. But things are far from over here—the real pitfalls are ahead.

### The Fundamental Dilemma of Distributed Locks

What problems does the implementation above have? Many.

**The first problem: Lock timeout and GC pauses.** Assume the lock timeout is 5 seconds. Your process acquires the lock and then does a time-consuming GC (if you are running Java, Stop-The-World pauses can reach seconds), or is suspended by the operating system scheduler (C++ programs don't GC, but you might encounter page swapping, CPU contention). After 5 seconds, the lock on Redis times out and is taken by someone else. When your process resumes execution, it still thinks it is the lock holder—two processes are operating on the shared resource at the same time, mutual exclusion is broken.

**The second problem: Redlock is also not safe enough.** Redis author Salvatore Sanfilippo proposed the Redlock algorithm—using multiple independent Redis instances for distributed locking, requiring the client to successfully acquire the lock on a majority (N/2 + 1) of instances to count as success. But Martin Kleppmann (yes, the one who wrote *Designing Data-Intensive Applications*) wrote a very famous article [How to do distributed locking](https://martin.kleppmann.com/2016/02/08/how-to-do-distributed-locking.html) to refute this solution. His core argument is: Redlock's safety relies on the assumption of clock synchronization—it assumes the clock deviation of each Redis node is limited. But clocks in distributed systems are unreliable (as we have already said), so this assumption can be broken in extreme cases. More critically, Redlock does not provide **fencing tokens**—a monotonically increasing number that lets the resource itself judge which lock holder is newer.

> ⚠️ **Pitfall Warning**
> If you use Redis for distributed locking, please understand its applicable scenarios: **efficiency-first** scenarios (such as preventing duplicate calculations, rate limiting) are acceptable; **correctness-first** scenarios (such as financial transfers, inventory deduction), Redis distributed locks are not safe enough, and you should use a lock service based on a consensus protocol.

**The third problem: Distributed locks and mutex are fundamentally different.** `std::mutex` provides absolute mutual exclusion guarantees—as long as the lock is held, other threads absolutely cannot enter (unless you have a bug). Distributed locks cannot achieve this—it can only provide "mutual exclusion in most cases," but in extreme cases such as network partitions, clock drift, process pauses, mutual exclusion may be broken. This isn't an implementation issue, this is a fundamental limitation of distributed systems.

So if you need strong guarantees, you should use a coordination service based on a consensus protocol like ZooKeeper or etcd. They use ZAB (ZooKeeper) or Raft (etcd) protocols to guarantee consistency, combined with ephemeral nodes and watchers to implement distributed locks—ephemeral nodes are automatically deleted when the client session disconnects, which is more reliable than Redis's timeout mechanism. At the same time, they natively support fencing tokens (through data version numbers or ZXID), which can avoid the expired lock problem mentioned above.

### Redis vs ZooKeeper/etcd Distributed Lock Comparison

Let's summarize the key differences discussed above into a table to help you choose based on actual scenarios:

| Dimension | Redis (Single Instance/Redlock) | ZooKeeper / etcd |
|------|----------------------|-------------------|
| Consistency Model | Asynchronous replication, possible data loss | Consensus protocol (ZAB/Raft), strong consistency |
| Lock Safety | Depends on clock, not safe enough | Consensus guarantee, can work with fencing token |
| Performance | Extremely high (memory operations) | Lower (requires majority confirmation) |
| Operational Complexity | Low | High (need to maintain consensus cluster) |
| Applicable Scenarios | Efficiency priority (prevent duplication, rate limiting) | Correctness priority (finance, inventory) |

To summarize: a distributed lock is a useful tool, but it is not an equivalent substitute for `std::mutex`. In a distributed environment, "mutual exclusion" changes from a deterministic guarantee to a probabilistic guarantee—you need to choose the right tool based on business requirements and tolerate inconsistency in extreme cases in design, or use mechanisms like fencing tokens for bottom-line protection.

## Engineering Intuition of the CAP Theorem

Talking about distributed systems inevitably involves the CAP theorem. This conjecture proposed by Eric Brewer in 2000 (proven by Seth Gilbert and Nancy Lynch in 2002) is a basic constraint in distributed system design. Let's not rush to define it, but use a scenario to understand it.

### What are the Three Properties

First, **Consistency**. It requires that all clients see the same data at any time—you write a value to node A, and immediately read node B, you should be able to read the latest value. This doesn't mean "eventually consistent," but "consistent at all times," which is the strongest consistency guarantee, equivalent to linearizability.

Next, **Availability**. It requires that every request receives a non-error response—the system does not refuse service, nor does it return an error. Even if the network has problems, every living server will try its best to answer your request. Note, availability only cares about "getting a response," whether the data in the response is the latest—that is consistency's job.

Finally, **Partition Tolerance**. When a network partition occurs (a group of machines cannot communicate), the system can still continue to work. In distributed systems, network partition is not a question of "will it happen," but "when will it happen"—networks are always unreliable, so partition tolerance is basically a must-have.

### Why You Can't Have All Three

The CAP theorem states: in a distributed system, when a network partition occurs, you can only choose Consistency (C) or Availability (A), not both.

Why? Let's use a specific scenario to explain. Suppose you have two servers, S1 and S2, each holding a copy of the data. Normally, after S1 receives a write, it syncs to S2, and read requests on both sides can return the latest data. Now a network partition occurs—S1 and S2 cannot communicate.

At this point, a client initiates a write request to S1. S1 has two choices:

If S1 chooses to **accept the write but cannot sync to S2**, then S1 has new data, S2 still has old data. At this point, read requests on S2 will return old data—consistency is broken, but availability is preserved (S2 did not refuse service). This is choosing **AP**.

If S1 chooses to **reject the write (because it cannot sync to S2)**, then consistency is preserved (no write that only takes effect on half the nodes), but availability is broken (the client received an error response). This is choosing **CP**.

There is no third option. You cannot accept writes and guarantee consistency while unable to sync—this is logically contradictory.

### Choosing Between CP and AP

Having understood the core idea of CAP, let's look at a few actual system choices.

A typical CP system is ZooKeeper. When a network partition occurs, if the ZooKeeper cluster cannot reach a quorum, it will refuse service—better to be unavailable than to return inconsistent data. This is reasonable for its role as a coordination service (storing configuration, doing Leader election, providing distributed locks): these scenarios have extremely high requirements for correctness, better to be briefly unavailable than to be wrong.

On the other side, Cassandra is a representative of AP systems. Its design philosophy is "always available"—even if the network partitions, each node still accepts read and write requests, although it might return old data. After the network recovers, it makes replicas eventually consistent through background read repair and anti-entropy mechanisms. This is reasonable for many internet applications: a one-second delay on social media (seeing old data) is much better than "service unavailable".

> ⚠️ **Pitfall Warning**
> Don't treat CAP as an either/or binary choice. In reality, in the vast majority of time the network is normal (no partition), and the system can provide relatively good consistency and availability at the same time. CAP only tells you that you must choose one when the network is partitioned in extreme cases. Many modern systems support making different choices at different operations and different configuration levels—for example, you can configure Cassandra for QUORUM reads/writes (leaning towards consistency) or ONE reads/writes (leaning towards availability).

## From Inter-Thread Communication to Network Communication

Looking back, although the difference between standalone concurrency and distributed concurrency is huge, from the perspective of the communication model, there is a very elegant transition.

On a single machine, the most natural way of communication between threads is **shared memory + locks**—this is also the model we discussed most of this volume. But you might remember, in ch07 we discussed the Actor model and CSP/Channel models. The core idea of these models is: **Don't communicate by sharing memory; instead, share memory by communicating**.

This idea is even more important in a distributed environment. Distributed systems have no shared memory—you cannot make processes on two machines share a `std::vector`. They can only coordinate through network messages. So Actor models and CSP models are naturally designed for distributed scenarios: an Actor can be local, or it can be on a remote machine; a message can be an intra-process function call, or it can be a network RPC request. From a programming model perspective, there is no essential difference.

This is why many distributed system frameworks chose the Actor model (such as Akka, Orleans)—it defers the decision of "local or remote" to the deployment stage, rather than hardcoding it in program logic. You write an Actor's message handling logic locally, and when deploying, put it on different machines, the code hardly needs to change.

In the modern C++ ecosystem, the key infrastructure connecting "concurrency" and "distributed" is the **RPC framework**, the most mainstream being gRPC. gRPC uses Protocol Buffers to define services and message formats, automatically generates client and server stub code, uses HTTP/2 for transport underneath, and supports streaming communication. It is essentially a cross-network "function call"—you call a remote method just like calling a local function (of course, there are important semantic differences, such as timeout and retry).

From a concurrency model perspective, every gRPC call can be seen as a message passing between Actors: the client Actor sends a request message, the server Actor receives the message, processes it, and returns a response message. We use C++20 coroutines to wrap gRPC's asynchronous API (this will be shown in the next article), and we can write distributed concurrent code in a very natural way—almost the same structure as writing local coroutines, just the underlying transport changes from function calls to network requests.

## Where We Are

In this article, we did a very important thing: build a cognitive bridge between standalone concurrency and distributed systems. We saw five fundamental differences—partial failure, unreliable network, no global clock, latency scale change, soaring consistency costs—each difference profoundly affecting the choice of concurrency model. Through the concrete case of distributed locks, we understood the evolutionary lineage from `std::mutex` to Redis to ZooKeeper/etcd, and also understood the key insight that "distributed locks are not an equivalent substitute for mutex". The CAP theorem gives us the basic constraint framework in distributed design, while the Actor/Channel model provides a programming paradigm for the smooth transition from standalone concurrency to distributed concurrency.

But understanding differences is just the first step. In the next article, we will enter the core difficulty of distributed systems—**consistency**. When replicas on multiple machines need to agree on a value, things are far more complex than "adding a lock". We will see the full spectrum from linearizability to eventual consistency, understand the core ideas of consensus protocols like Paxos/Raft, and use gRPC + C++20 coroutines to show the direction of writing distributed communication code in C++.

## Reference Resources

- [Designing Data-Intensive Applications — Martin Kleppmann](https://dataintensive.net/) — Recognized as the best introductory book in the field of distributed systems, CAP, consistency, and consensus protocols are explained very thoroughly
- [CAP Theorem — Wikipedia](https://en.wikipedia.org/wiki/CAP_theorem) — Formal definition and history of the CAP theorem
- [How to do distributed locking — Martin Kleppmann](https://martin.kleppmann.com/2016/02/08/how-to-do-distributed-locking.html) — Classic rebuttal to Redlock, introducing the concept of fencing tokens
- [Latency Numbers Every Programmer Should Know — Jonas Bonér](https://gist.github.com/jboner/2841832) — Intuitive comparison of latencies for various operations (original data from Jeff Dean / Peter Norvig)
- [Is Redlock safe? — Salvatore Sanfilippo (antirez)](http://antirez.com/news/101) — Redis author's response to Kleppmann's criticism
- [Raft Consensus Algorithm](https://raft.github.io/) — Official resources for the Raft protocol, including a visual demo
