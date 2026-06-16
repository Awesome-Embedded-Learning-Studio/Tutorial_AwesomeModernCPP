---
chapter: 9
cpp_standard:
- 17
- 20
description: Understand the fundamental differences between standalone concurrency
  and distributed systems—partial failures, unreliable networks, and clock skew, and
  how these differences affect the choice of concurrency models.
difficulty: advanced
order: 1
platform: host
prerequisites:
- Actor 模型与消息传递
- Channel 与 CSP 模型
- 并发程序调试技巧
reading_time_minutes: 24
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
  source: documents/vol5-concurrency/ch09-distributed-bridge/01-from-concurrent-to-distributed.md
  source_hash: 28eff8fc65d0bf1bf7c886faffaf35168405bbb1ff34fe9c22eeb1142cd0048b
  translated_at: '2026-06-16T04:07:04.669391+00:00'
  engine: anthropic
  token_count: 3248
---
# From Standalone Concurrency to Distributed Systems

> ℹ️ **Context**: This chapter is a conceptual overview. It does not include runnable code or introduce external frameworks. Its goal is to help you build a cognitive framework for "Standalone Concurrency → Distributed Systems" before diving into the practical distributed implementation in Volume 8—so you know which past experiences still apply and which need to be completely rethought.

Throughout this volume, we have been discussing concurrency on a single machine—how multiple threads within one process safely share data, how to use atomic operations for lock-free synchronization, and how to use coroutines to make asynchronous code readable. This knowledge is very solid, but it is built on an implicit premise: all threads share the same memory, run on the same operating system, and are managed by the same scheduler.

Reality is harsh. When your service needs to handle more requests and store more data, a single machine eventually won't be enough—whether it's CPU computing power, memory capacity, or network bandwidth, one dimension will hit the ceiling first. You have to deploy services across multiple machines and make them work together. At this point, the problem of "concurrency" expands from intra-process to the network. You are no longer facing a `std::mutex`, but a cross-network lock coordination service; no longer just atomic operations, but a set of distributed replicas that need to agree on a value.

In this article, we discuss the fundamental changes in the concurrency model as you move from standalone to distributed systems. We will see that many assumptions taken for granted on a single machine—such as "messages always arrive," "clocks are always accurate," "an operation either succeeds or fails"—completely fail in a distributed environment. This isn't to scare you, but to give you a clear cognitive framework when facing distributed systems, so you know which old experiences to keep and which must be rethought.

## Five Fundamental Differences Between Standalone and Distributed Systems

Let's lay out the most critical differences and examine them one by one.

### Partial Failure: Others Crash, You Survive

On a single machine, if a thread crashes due to an unhandled exception or a segmentation fault, usually the entire process is killed by the operating system—the process is the basic unit of resource isolation, not the thread. You can use `std::jthread` (automatic thread joining introduced in C++20) or write a global signal handler to do some cleanup, but essentially, all threads within a process share the same fate: either they all live, or they all die.

Distributed systems are completely different. You have 10 machines, and 3 of them suddenly lose power (this happens much more often in reality than you think), and the remaining 7 must continue serving. This introduces a problem that barely exists on a single machine: **partial failure**. An operation might succeed on some machines and fail on others—how do you handle this? Can you safely retry? Do you need to roll back the part that succeeded?

Even trickier, you can't always be sure if the other side has actually crashed. You send a request, and it times out—did the other side really hang, or is the network just slow? Did the request not arrive, or did the response not come back? This **uncertainty** is the most headache-inducing part of distributed systems. In his classic treatise on fault-tolerant systems, Jim Gray called these intermittent faults that "disappear when observed" "Heisenbugs"—when you attach a debugger to reproduce them, they might disappear because the network happens to recover.

### Unreliable Network: The Illusion of Shared Memory Vanishes

On a single machine, threads communicate through shared memory. You write to a variable, and another thread can read it immediately (of course, considering cache coherence, but with correct use of `std::atomic` and memory ordering, this behavior is predictable). The CPU's cache coherence protocol (MESI and its variants) guarantees this. Essentially, shared memory is a reliable, ordered, and extremely low-latency communication channel.

The network is not. Messages may be delayed (and the delay time can be very uncertain, from a few milliseconds to several seconds), may be lost (network switch packet drops, TCP retransmission timeouts), may be duplicated (caused by application layer retries), or may even arrive out of order (taking different routing paths). TCP solves part of the problem—it guarantees reliable, ordered transmission of byte streams—but it doesn't solve everything: if the remote process crashes, the TCP connection breaks, and your "reliable transmission" is over. Not to mention many distributed protocols run directly on UDP, where reliability must be entirely guaranteed at the application layer.

The consequence of this difference is profound: on a single machine, you can assume a function call either returns a result or throws an exception, a binary choice; in a distributed environment, a remote call might return a result, or it might time out, and if it times out, you don't even know if the other side processed it. Your code must handle this third state—"unknown".

### No Global Clock: Who Came First is Unclear

On a single machine, you can use an `std::atomic` as a global sequence number generator, sorting all operations by sequence number—whichever has the smaller number happened first. The semantics of `std::atomic` combined with the cache coherence protocol guarantee that all cores see the same sequence numbers (we discussed this topic in depth in ch03).

Distributed systems don't have this luxury. Each machine has its own local clock, and these clocks have deviations. Even if you use NTP (Network Time Protocol) for clock synchronization, typically you can only achieve millisecond-level precision, and clocks drift. Google's TrueTime service (used in Spanner) achieves more precise clock synchronization through GPS and atomic clocks, but that is extremely expensive infrastructure, not available to everyone.

The consequence of no global clock is: it is difficult to judge which of two events occurring on different machines happened first. On a single machine, the timestamp of events is clear; in a distributed environment, the timestamps of two events may contradict each other—Machine A says its operation happened at 10:00:00.100, Machine B says its operation happened at 10:00:00.099, but actually A's operation might have happened earlier than B (because A's clock is 2ms fast). This is why distributed systems need to use logical clocks (Lamport clocks, Vector clocks) to establish causal order, rather than relying on physical time.

### Latency Scale Change: From Nanoseconds to Milliseconds

Let's speak with specific numbers. These are numbers every system developer should etch in their brain:

| Operation | Typical Latency |
|------|----------|
| L1 Cache Access | ~1 ns |
| L2 Cache Access | ~5 ns |
| Main Memory Access | ~100 ns |
| Same Datacenter Network Round Trip | ~500,000 ns (0.5 ms) |
| Same City Network Round Trip | ~1-2 ms |
| Cross-Continent Network Round Trip | ~50-80 ms |

Main memory access is about 100 nanoseconds, same datacenter network round trip is about 0.5 milliseconds—a difference of almost 5000 times, three orders of magnitude. If it's cross-continent network, the gap is even larger. Jeff Dean and Peter Norvig originally compiled this latency data, and Jonas Bonér summarized it into a widely circulated reference table. The community made a very intuitive analogy based on these data: if L1 cache access is compared to reaching out to pick up a pen on a desk (1 second), then a datacenter network round trip is equivalent to hiking 94 miles (about 150 km). This isn't just a change in magnitude, it's a change in worldview.

What does this latency difference mean? It means that many optimizations you make on a single machine—like reducing contention on a cache line—might be completely irrelevant in a distributed scenario. Your bottleneck is on the network, not in memory. Similarly, every network round trip in a distributed system is extremely expensive, so you will see distributed protocols tend to use batching and pipelining to amortize the cost of a single request.

### Cost of Consistency: From Locking to Consensus

On a single machine, a standard way to protect shared data is locking—`std::mutex`, `std::shared_mutex`, or lock-free `std::atomic`. The cost of these operations is in nanoseconds (lock/unlock is usually tens to hundreds of nanoseconds), and the semantics are very clear: lock, operate, unlock, three steps.

In a distributed environment, if you want replicas on multiple machines to agree on a value, you need a **consensus protocol**—like Paxos or Raft. These protocols require multiple rounds of network communication, majority voting, log replication... every "consensus" costs milliseconds, four to six orders of magnitude more expensive than single-machine locking. And implementation is far more complex than mutex—the correctness of a Paxos implementation is enough for a SOSP paper.

This isn't to say distributed systems are necessarily slower than single machines. The value of distributed systems lies in **horizontal scaling**—you can increase throughput by adding machines. But every operation that needs strong consistency is limited by the latency of the consensus protocol. This is why a core problem in distributed system design is: **which operations need strong consistency, and which can accept weak consistency?**

## From mutex to Distributed Locks

Understanding the differences above, let's look at a concrete example: how to move the "mutex" from a single machine to a distributed environment.

### Assumptions of Standalone mutex

A `std::mutex` works because it relies on a set of assumptions taken for granted on a single machine—all threads share the same memory, all threads are scheduled by the same operating system, and the lock holder is definitely still alive (if it dies, the whole process dies, so the lock problem ceases to exist). These assumptions hold on a single machine.

In a distributed environment, none of these assumptions hold: multiple processes run on different machines, each with its own scheduler, and a process may crash at any time while others continue running. So when you need a mutex across machines, you must implement it in a completely different way.

### Redis-based Distributed Lock

The simplest and most common distributed lock implementation is based on Redis. The core idea is to use Redis's `SET` command—`NX` means "set only if key does not exist" (i.e., lock), `EX` sets expiration time (i.e., lock timeout protection). The value is usually a unique identifier (like UUID), used to identify the lock holder, preventing accidental unlocking.

Let's look at a simple distributed lock implemented in C++ using the `hiredis` library.

First is the locking logic:

```cpp
#include <string>
#include <chrono>
#include <random>

/// @brief 基于 Redis 的简单分布式锁
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

    /// @brief 尝试获取锁，成功返回 true
    bool try_acquire()
    {
        // SET lock_key token NX PX timeout
        // NX: 只在 key 不存在时设置
        // PX: 设置过期时间（毫秒）
        // 使用 hiredis 的 %s 格式化参数来避免注入风险
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

    /// @brief 释放锁（只有持有者才能释放）
    void release()
    {
        if (!locked_) {
            return;
        }

        // 用 Lua 脚本保证原子性：
        // 只有当 key 的值等于我们的 token 时才删除
        // 防止误解锁别人的锁
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
        // RAII: 析构时自动释放锁
        release();
    }

private:
    /// @brief 生成唯一的锁持有者标识
    static std::string generate_token()
    {
        // 用随机数 + 时间戳生成唯一 token
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

Let's look at the locking part first. `redisCommand` sends the `SET` command via hiredis's formatting interface. There are a few key points here. First, note that we use hiredis's `%s` placeholder to pass arguments, rather than manually splicing strings—if you directly splice key and token into the command string, once the key contains spaces or special characters, it could lead to command injection issues. Then there is the `NX` option, which guarantees success only if the key does not exist—this is the source of mutual exclusion—whoever sets it successfully first gets the lock. `EX` sets the expiration time, which is a safety net: if the lock holder crashes (process dies, machine loses power), the lock will be automatically released after timeout, preventing it from being held forever. Finally, the value uses a unique token instead of a simple string; this token identifies the lock holder.

The unlock part is more subtle; we use a Lua script to guarantee the atomicity of the two steps "check token then delete key". Why do this? Because if split into two steps (GET to judge, then DEL to delete), another operation might be inserted in between—your GET confirmed this is your lock, but before DEL, the lock happens to time out and is acquired by someone else, and your DEL deletes someone else's lock. Lua scripts are executed atomically in Redis, avoiding this problem.

Usage is very concise:

```cpp
void do_synchronized_work(redisContext* redis)
{
    // 尝试获取分布式锁，超时 5 秒
    RedisDistributedLock lock(redis, "my_resource_lock", 5000);

    if (!lock.try_acquire()) {
        // 没拿到锁，说明有别人在操作
        std::cerr << "获取分布式锁失败，稍后重试\n";
        return;
    }

    // 拿到锁了，安全地操作共享资源
    // ...

    // 离开作用域时，析构函数自动释放锁（RAII）
}
```

Great, everything looks perfect so far. But things are far from over—the real traps are ahead.

### The Fundamental Dilemma of Distributed Locks

What problems does the implementation above have? Many.

**The first problem: Lock timeout and GC pauses.** Suppose the lock timeout is 5 seconds. Your process acquires the lock and then does a time-consuming GC (if you are running Java, Stop-The-World pauses can reach seconds), or is suspended by the operating system scheduler (C++ programs don't GC, but you might encounter page swapping, CPU contention). After 5 seconds, the lock on Redis times out and is taken by someone else. When your process resumes execution, it still thinks it is the lock holder—two processes are operating on the shared resource at the same time, mutual exclusion is broken.

**The second problem: Redlock is also not safe enough.** Redis author Salvatore Sanfilippo proposed the Redlock algorithm—using multiple independent Redis instances for distributed locking, where the client needs to successfully acquire the lock on a majority (N/2 + 1) of instances for it to count as success. But Martin Kleppmann (yes, the one who wrote "Designing Data-Intensive Applications") wrote a very famous article [How to do distributed locking](https://martin.kleppmann.com/2016/02/08/how-to-do-distributed-locking.html) to refute this solution. His core argument is: Redlock's safety relies on the assumption of clock synchronization—it assumes the clock deviation of each Redis node is limited. But clocks in distributed systems are unreliable (as we have already said), so this assumption can be broken in extreme cases. More critically, Redlock does not provide **fencing tokens**—a monotonically increasing number that lets the resource itself judge which lock holder is newer.

> ⚠️ **Pitfall Warning**
> If you use Redis for distributed locking, please understand its applicable scenarios: **efficiency-first** scenarios (like preventing duplicate calculations, rate limiting) are fine; **correctness-first** scenarios (like financial transfers, inventory deduction), Redis distributed locks are not safe enough, and you should use a lock service based on a consensus protocol.

**The third problem: Distributed locks and mutex are fundamentally different.** `std::mutex` provides absolute mutual exclusion guarantees—as long as the lock is held, other threads absolutely cannot get in (unless you have a bug). Distributed locks cannot do this—they can only provide "mutual exclusion in most cases," but in extreme cases like network partitions, clock drift, process pauses, mutual exclusion might be broken. This isn't an implementation problem; this is a fundamental limitation of distributed systems.

So if you need strong guarantees, you should use a coordination service based on a consensus protocol like ZooKeeper or etcd. They use ZAB (ZooKeeper) or Raft (etcd) protocols to guarantee consistency, combined with ephemeral nodes and watchers to implement distributed locks—ephemeral nodes are automatically deleted when the client session disconnects, which is more reliable than Redis's timeout mechanism. At the same time, they natively support fencing tokens (through data version numbers or ZXID), which can avoid the expired lock problem mentioned above.

### Redis vs ZooKeeper/etcd Distributed Lock Comparison

Let's summarize the key differences discussed above into a table to help you choose based on actual scenarios:

| Dimension | Redis (Single Instance/Redlock) | ZooKeeper / etcd |
|------|----------------------|-------------------|
| Consistency Model | Asynchronous replication, possible data loss | Consensus protocol (ZAB/Raft), strong consistency |
| Lock Safety | Relies on clock, not safe enough | Consensus guarantee, can work with fencing token |
| Performance | Extremely high (memory operations) | Lower (requires majority confirmation) |
| Operational Complexity | Low | High (need to maintain consensus cluster) |
| Applicable Scenarios | Efficiency first (prevent duplication, rate limiting) | Correctness first (finance, inventory) |

To summarize: a distributed lock is a useful tool, but it is not an equivalent substitute for `std::mutex`. In a distributed environment, "mutual exclusion" changes from a deterministic guarantee to a probabilistic guarantee—you need to choose the right tool based on business needs, and tolerate inconsistency in extreme cases in design, or use mechanisms like fencing tokens for bottom-line protection.

## Engineering Intuition of the CAP Theorem

Talking about distributed systems inevitably involves the CAP theorem. This conjecture proposed by Eric Brewer in 2000 (proven by Seth Gilbert and Nancy Lynch in 2002) is a basic constraint of distributed system design. Let's not rush to define it, but use a scenario to understand it.

### What are the Three Properties

First, **Consistency**. It requires that all clients see the same data at any moment—you write a value to node A, and immediately read from node B, you should be able to read the latest value. This doesn't mean "eventually consistent," but "consistent at all times," which is the strongest consistency guarantee, equivalent to linearizability.

Next, **Availability**. It requires that every request receives a non-error response—the system does not refuse service, nor does it return an error. Even if the network has problems, every living server will try its best to answer your request. Note, availability only cares about "getting a response," as for whether the data in the response is the latest—that's consistency's job.

Finally, **Partition Tolerance**. When a network partition occurs (when some machines cannot communicate), the system can still continue to work. In distributed systems, network partition is not a question of "will it happen," but "when will it happen"—the network is always unreliable, so partition tolerance is basically a must-have.

### Why You Can't Have All Three

The CAP theorem states: in a distributed system, when a network partition occurs, you can only choose Consistency (C) or Availability (A), not both.

Why? Let's use a specific scenario to explain. Suppose you have two servers, S1 and S2, each holding a copy of the data. Normally, after S1 receives a write, it syncs to S2, and read requests on both sides can return the latest data. Now a network partition occurs—S1 and S2 cannot communicate.

At this point, a client initiates a write request to S1. S1 has two choices:

If S1 chooses to **accept the write but cannot sync to S2**, then S1 has new data, but S2 still has old data. At this point, read requests on S2 will return old data—consistency is broken, but availability is preserved (S2 did not refuse service). This is choosing **AP**.

If S1 chooses to **reject the write (because it cannot sync to S2)**, then consistency is preserved (no write that takes effect on only half the nodes), but availability is broken (the client received an error response). This is choosing **CP**.

There is no third option. You cannot accept a write and guarantee consistency when you cannot sync—this is logically contradictory.

### Choosing Between CP and AP

Understanding the core idea of CAP, let's look at a few actual system choices.

A typical CP system is ZooKeeper. When a network partition occurs, if the ZooKeeper cluster cannot reach a quorum, it will refuse service—better to be unavailable than to return inconsistent data. This is reasonable for its role as a coordination service (storing configuration, doing leader election, providing distributed locks)—these scenarios have extremely high requirements for correctness, better to be briefly unavailable than to make a mistake.

On the other side, Cassandra is a representative of AP systems. Its design philosophy is "always available"—even if the network partitions, each node still accepts read and write requests, just possibly returning old data. After the network recovers, it makes replicas eventually consistent through background read repair and anti-entropy mechanisms. This is reasonable for many internet applications: a one-second delay on social media (seeing old data) is much better than "service unavailable."

> ⚠️ **Pitfall Warning**
> Don't view CAP as a binary either-or choice. In reality, the vast majority of the time the network is normal (no partition), and the system can provide relatively good consistency and availability simultaneously. CAP only tells you that you must choose one when the network is in the extreme case of a partition. Many modern systems support making different choices at different operations and different configuration levels—for example, you can configure Cassandra for QUORUM reads/writes (leaning towards consistency) or ONE reads/writes (leaning towards availability).

## From Inter-Thread Communication to Network Communication

Looking back, although the differences between standalone concurrency and distributed concurrency are huge, from the perspective of the communication model, there is a very elegant transition.

On a single machine, the most natural way for threads to communicate is **shared memory + locks**—this is also the model we discussed most of this volume. But you might remember, in ch07 we discussed the Actor model and CSP/Channel models. The core idea of these models is: **Don't communicate by sharing memory; instead, share memory by communicating**.

This idea is even more important in a distributed environment. Distributed systems have no shared memory—you cannot make processes on two machines share a `std::vector`. They can only coordinate through network messages. So Actor models and CSP models are naturally designed for distributed scenarios: an Actor can be local, or on a remote machine; a message can be an intra-process function call, or a network RPC request. From a programming model perspective, there is no essential difference.

This is why many distributed system frameworks choose the Actor model (like Akka, Orleans)—it defers the decision of "local or remote" to the deployment stage, rather than hardcoding it in program logic. You write an Actor's message handling logic locally, and when deploying, put it on different machines, the code hardly needs to change.

In the modern C++ ecosystem, the key infrastructure connecting "concurrency" and "distributed" is the **RPC framework**, the most mainstream being gRPC. gRPC uses Protocol Buffers to define services and message formats, automatically generates client and server stub code, uses HTTP/2 for transport underneath, and supports streaming communication. It is essentially a cross-network "function call"—you call a remote method just like calling a local function (of course, there are important semantic differences, like timeout and retry).

From a concurrency model perspective, every gRPC call can be seen as a message passing between Actors: the client Actor sends a request message, the server Actor receives the message, processes it, and returns a response message. We wrap gRPC's asynchronous API with C++20 coroutines (this will be shown in the next article), and we can write distributed concurrent code in a very natural way—almost the same structure as writing local coroutines, just the underlying transport changes from function calls to network requests.

## Where We Are

In this article, we did a very important thing: build a cognitive bridge between standalone concurrency and distributed systems. We saw five fundamental differences—partial failure, unreliable network, no global clock, latency scale change, soaring cost of consistency—each difference profoundly affects the choice of concurrency model. Through the concrete case of distributed locks, we understood the evolution from `std::mutex` to Redis to ZooKeeper/etcd, and also understood the key insight that "a distributed lock is not an equivalent substitute for mutex." The CAP theorem gives us the basic constraint framework in distributed design, while the Actor/Channel model provides a programming paradigm for the smooth transition from standalone to distributed concurrency.

But understanding differences is just the first step. In the next article, we will enter the core难题 of distributed systems—**Consistency**. When replicas on multiple machines need to agree on a value, things are far more complex than "just adding a lock." We will see the full spectrum from linear consistency to eventual consistency, understand the core ideas of consensus protocols like Paxos/Raft, and use gRPC + C++20 coroutines to show the direction of writing distributed communication code in C++.

## Reference Resources

- [Designing Data-Intensive Applications — Martin Kleppmann](https://dataintensive.net/) — Recognized as the best introductory book in the field of distributed systems, CAP, consistency, and consensus protocols are explained very thoroughly
- [CAP Theorem — Wikipedia](https://en.wikipedia.org/wiki/CAP_theorem) — Formal definition and history of the CAP theorem
- [How to do distributed locking — Martin Kleppmann](https://martin.kleppmann.com/2016/02/08/how-to-do-distributed-locking.html) — Classic rebuttal to Redlock, introducing the concept of fencing tokens
- [Latency Numbers Every Programmer Should Know — Jonas Bonér](https://gist.github.com/jboner/2841832) — Intuitive comparison of latencies for various operations (original data from Jeff Dean / Peter Norvig)
- [Is Redlock safe? — Salvatore Sanfilippo (antirez)](http://antirez.com/news/101) — Response from Redis author to Kleppmann's criticism
- [Raft Consensus Algorithm](https://raft.github.io/) — Official resources for the Raft protocol, including visualization
