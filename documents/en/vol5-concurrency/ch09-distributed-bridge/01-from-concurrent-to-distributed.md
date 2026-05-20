---
title: From Single-Node Concurrency to Distributed Systems
description: Understanding the fundamental differences between single-machine concurrency
  and distributed systems—partial failures, unreliable networks, and clock skew—and
  how these differences affect the choice of concurrency models
chapter: 9
order: 1
tags:
- host
- cpp-modern
- advanced
- 进阶
- 异步编程
- atomic
- mutex
difficulty: advanced
platform: host
reading_time_minutes: 25
cpp_standard:
- 17
- 20
prerequisites:
- Actor 模型与消息传递
- Channel 与 CSP 模型
- 并发程序调试技巧
related:
- 分布式一致性原语初探
translation:
  source: documents/vol5-concurrency/ch09-distributed-bridge/01-from-concurrent-to-distributed.md
  source_hash: 2df0dc0314055906be0c6aff6c7a34567311fc79641c540d77515276c351557b
  translated_at: '2026-05-20T04:50:48.328816+00:00'
  engine: anthropic
  token_count: 3230
---
# From Single-Machine Concurrency to Distributed Systems

Throughout this volume, we have focused on concurrency on a single machine—how multiple threads within a single process safely share data, how to use atomic operations for lock-free synchronization, and how to use coroutines to make asynchronous code readable. This knowledge is solid, but it rests on an implicit premise: all threads share the same memory, run on the same operating system, and are managed by the same scheduler.

Reality is harsh. When your service needs to handle more requests and store more data, a single machine will eventually fall short—whether it is CPU power, memory capacity, or network bandwidth, one dimension will hit a ceiling first. You have to deploy the service across multiple machines and make them work together. At this point, the "concurrency" problem expands from within a process to across a network. You are no longer dealing with a `std::mutex`, but rather a cross-network lock coordination service; no longer with a `std::atomic`, but a set of distributed replicas that need to agree on a value.

In this chapter, we explore the fundamental shifts in the concurrency model when you move from a single machine to a distributed system. We will see that many assumptions taken for granted on a single machine—such as "messages always arrive," "clocks are always accurate," or "an operation either succeeds or fails"—completely fall apart in a distributed environment. This is not meant to scare you, but to provide a clear cognitive framework when facing distributed systems, so you know which past experiences still apply and which must be rethought.

## Five Fundamental Differences Between Single-Machine and Distributed Systems

Let us lay out the most critical differences and examine them one by one.

### Partial Failure: Others Crash, You Keep Running

On a single machine, if a thread crashes due to an uncaught exception or a segmentation fault, the entire process is usually killed by the operating system—a process is the basic unit of resource isolation, while a thread is not. You can use a `std::jthread` (auto-joining threads introduced in C++20) or write a global signal handler to do some cleanup, but essentially, all threads within a process share the same fate: either they are all alive, or they are all dead.

Distributed systems are completely different. If you have 10 machines and 3 of them suddenly lose power (this happens much more often in reality than you might think), the remaining 7 must continue serving. This introduces a problem that barely exists on a single machine: **partial failure**. An operation might succeed on some machines but fail on others—how do you handle this? Can you safely retry? Do you need to roll back the part that already succeeded?

What is even more tricky is that you cannot always be certain whether the other party has actually crashed. You send a request, and it times out—did the other side really go down, or is the network just slow? Did the request not arrive, or did the response not come back? This **uncertainty** is the most headache-inducing part of distributed systems. In his classic treatise on fault-tolerant systems, Jim Gray referred to such intermittent faults that "disappear while observing" as "Heisenbugs"—when you attach a debugger to reproduce them, they might vanish because the network happens to recover.

### Unreliable Networks: The Illusion of Shared Memory Vanishes

On a single machine, threads communicate through shared memory. You write to a variable, and another thread can immediately read it (of course, you must consider cache coherence, but given the correct use of `std::atomic` and memory order, this behavior is predictable). The CPU's cache coherence protocol (MESI and its variants) guarantees this. Essentially, shared memory is a reliable, ordered, and extremely low-latency communication channel.

Networks are not. Messages might be delayed (and the delay can be highly unpredictable, ranging from a few milliseconds to several seconds), lost (packet drops by network switches, TCP retransmission timeouts), duplicated (caused by application-level retries), or even arrive out of order (taking different routing paths). TCP solves some of these problems—it guarantees reliable, in-order transmission of byte streams—but it does not solve everything: if the remote process crashes, the TCP connection breaks, and your "reliable transmission" comes to an end. Not to mention, many distributed protocols run directly on UDP, where reliability must be entirely guaranteed at the application layer.

The consequence of this difference is profound: on a single machine, you can assume that a function call either returns a result or throws an exception, one of the two; in a distributed environment, a remote call might return a result, or it might time out, and if it times out, you do not even know whether the other side processed it. Your code must handle this third state—"unknown."

### No Global Clock: Who Came First Is Unclear

On a single machine, you can use a `std::atomic<uint64_t>` as a global sequence number generator, ordering all operations by their sequence numbers—the smaller the number, the earlier the operation. The semantics of `memory_order_seq_cst`, combined with the cache coherence protocol, guarantee that all cores see the same ordering (we discussed this topic in depth in ch03).

Distributed systems do not have this luxury. Each machine has its own local clock, and these clocks have offsets. Even if you use NTP (Network Time Protocol) for clock synchronization, you can typically only achieve millisecond-level precision, and clocks will drift. Google's TrueTime service (used in Spanner) achieves more precise clock synchronization through GPS and atomic clocks, but that is extremely expensive infrastructure, not something anyone can just use.

The consequence of having no global clock is that it is very difficult to determine which of two events, occurring on different machines, happened first. On a single machine, event timestamps are definitive; in a distributed environment, the timestamps of two events might contradict each other—Machine A says its operation happened at 10:00:00.100, Machine B says its operation happened at 10:00:00.099, but in reality, A's operation might have happened before B's (because A's clock was 2ms fast). This is why distributed systems need to use logical clocks (Lamport clocks, vector clocks) to establish causal order, rather than relying on physical time.

### Latency Scale Shift: From Nanoseconds to Milliseconds

Let us speak with concrete numbers. These are numbers that every systems developer should have memorized:

| Operation | Typical Latency |
|------|----------|
| L1 cache access | ~1 ns |
| L2 cache access | ~5 ns |
| Main memory access | ~100 ns |
| Same data center network round trip | ~500,000 ns (0.5 ms) |
| Same city network round trip | ~1-2 ms |
| Cross-continent network round trip | ~50-80 ms |

Main memory access is about 100 nanoseconds, while a same-data-center network round trip is about 0.5 milliseconds—a difference of almost 5,000 times, three orders of magnitude. For cross-continent networks, the gap is even larger. Jeff Dean and Peter Norvig originally compiled these latency numbers, and Jonas Bonér summarized them into a widely circulated reference table. The community created a very intuitive analogy based on these numbers: if L1 cache access is compared to reaching for a pen on your desk (1 second), then a data center network round trip is equivalent to walking 94 miles (about 150 kilometers). This is not a change in magnitude; it is a paradigm shift.

What does this latency difference mean? It means that many optimizations you make on a single machine—such as reducing contention on a cache line—might be completely irrelevant in a distributed scenario. Your bottleneck is on the network, not in memory. Similarly, every network round trip in a distributed system is extremely expensive, so you will see distributed protocols leaning towards batching and pipelining to amortize the cost of individual requests.

### The Cost of Consistency: From Locking to Consensus

On a single machine, a standard way to protect shared data is locking—`std::mutex`, `std::shared_mutex`, or lock-free `std::atomic`. The cost of these operations is in the nanosecond range (lock/unlock is usually tens to hundreds of nanoseconds), and the semantics are very clear: lock, operate, unlock, three simple steps.

In a distributed environment, if you want replicas on multiple machines to agree on a value, you need a **consensus protocol**—such as Paxos or Raft. These protocols require multiple rounds of network communication, majority voting, log replication... The cost of each "consensus" operation is in the millisecond range, four to six orders of magnitude more expensive than single-machine locking. And they are far more complex to implement than a mutex—the correctness of a Paxos implementation is enough to warrant a SOSP paper.

This is not to say that distributed systems are necessarily slower than single-machine systems. The value of distributed systems lies in **horizontal scaling**—you can increase throughput by adding more machines. But every operation that requires strong consistency is bottlenecked by the latency of the consensus protocol. This is why a core question in distributed system design is: **which operations need strong consistency, and which can accept weak consistency?**

## From Mutex to Distributed Locks

Having understood the differences above, let us look at a concrete example: how to transplant the "mutex" from a single machine into a distributed environment.

### Assumptions of a Single-Machine Mutex

A `std::mutex` works because it relies on a set of assumptions that are taken for granted on a single machine—all threads share the same memory, all threads are scheduled by the same operating system, and the lock holder is definitely still alive (if it dies, the whole process dies, so the lock problem ceases to exist). These assumptions hold true on a single machine.

In a distributed environment, none of these assumptions hold: multiple processes run on different machines, each with its own independent scheduler, and a process might crash at any time while other processes continue running. Therefore, when you need a cross-machine mutex, you must implement it in a completely different way.

### Redis-Based Distributed Locks

The simplest and most common distributed lock implementation is based on Redis. The core idea is to use Redis's `SET key value NX PX timeout` command—`NX` means "set only if the key does not exist" (i.e., acquire the lock), and `PX` sets an expiration time (i.e., lock timeout protection). The value is typically a unique identifier (such as a UUID), used to identify the lock holder and prevent accidental unlock by others.

Let us look at a simple distributed lock implemented in C++ using the `hiredis` library.

First, the locking logic:

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

Let us look at the locking part first. `try_acquire()` sends the `SET lock_key token NX PX timeout` command through hiredis's formatted interface. There are a few key points here. First, note that we use hiredis's `%s` placeholder to pass parameters, rather than manually concatenating strings—if you directly splice the key and token into the command string, and the key contains spaces or special characters, it could lead to command injection issues. Next is the `NX` option, which guarantees that the set operation succeeds only if the key does not exist—this is the source of mutual exclusion: whoever sets it first gets the lock. `PX timeout` sets the expiration time, which is a safety net: if the lock holder crashes (the process dies, the machine loses power), the lock will be automatically released after the timeout, so it will not be held forever. Finally, the value uses a unique token instead of a simple string; this token identifies the lock holder.

The unlock part is more subtle. We use a Lua script to guarantee the atomicity of the two steps: "check the token, then delete the key." Why do we do this? Because if split into two steps (first GET to check, then DEL to delete), another operation could be inserted in between—your GET confirms this is your lock, but before your DEL, the lock happens to time out and is acquired by someone else, and your DEL ends up deleting someone else's lock. Lua scripts are executed atomically in Redis, avoiding this problem.

The usage is very concise:

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

Great, so far everything looks perfect. But the story is far from over—the real pitfalls lie ahead.

### The Essential Dilemma of Distributed Locks

What is wrong with the implementation above? Many things.

**The first problem: lock timeouts and GC pauses.** Suppose the lock timeout is 5 seconds. After your process acquires the lock, it performs a time-consuming GC (if you are running Java, a Stop-The-World pause can reach the second level), or it gets suspended by the operating system scheduler (C++ programs do not GC, but you might encounter page swapping or CPU contention). After 5 seconds, the lock on Redis times out and is taken by someone else. When your process resumes execution, it still thinks it is the lock holder—two processes are operating on the shared resource simultaneously, and mutual exclusion is broken.

**The second problem: Redlock is still not safe enough.** Redis's creator, Salvatore Sanfilippo, proposed the Redlock algorithm—using multiple independent Redis instances for distributed locking, where the client must successfully acquire the lock on a majority (N/2 + 1) of instances for it to be considered successful. But Martin Kleppmann (yes, the one who wrote *Designing Data-Intensive Applications*) wrote a very famous article, [How to do distributed locking](https://martin.kleppmann.com/2016/02/08/how-to-do-distributed-locking.html), to rebut this approach. His core argument is that Redlock's safety relies on the assumption of clock synchronization—it assumes the clock offset among Redis nodes is bounded. But clocks in distributed systems are unreliable (as we discussed earlier), so this assumption can be broken in extreme cases. More critically, Redlock does not provide a **fencing token**—a monotonically increasing number that allows the resource itself to determine which lock holder is newer.

> ⚠️ **Pitfall Warning**
> If you use Redis for distributed locking, please make sure you understand its applicable scenarios: it is acceptable for **efficiency-first** scenarios (such as preventing duplicate computations or rate limiting); for **correctness-first** scenarios (such as financial transfers or inventory deduction), Redis distributed locks are not safe enough, and you should use a lock service based on a consensus protocol.

**The third problem: distributed locks and mutexes are fundamentally different.** A `std::mutex` provides an absolute mutual exclusion guarantee—as long as the lock is held, other threads absolutely cannot enter (unless you have a bug). A distributed lock cannot achieve this—it can only provide "mutual exclusion in most cases," but in extreme situations like network partitions, clock drift, or process pauses, mutual exclusion might be broken. This is not an implementation issue; it is a fundamental limitation of distributed systems.

So, if you need strong guarantees, you should use a coordination service based on a consensus protocol, such as ZooKeeper or etcd. They use the ZAB (ZooKeeper) or Raft (etcd) protocol to guarantee consistency, and they implement distributed locks using ephemeral nodes and watchers—when a client session disconnects, the ephemeral node is automatically deleted, which is more reliable than Redis's timeout mechanism. At the same time, they natively support fencing tokens (through data version numbers or ZXID), which can avoid the expired lock problem mentioned above.

### Comparison of Redis vs. ZooKeeper/etcd Distributed Locks

Let us summarize the key differences discussed above into a table, making it easier for you to choose based on your actual scenario:

| Dimension | Redis (Single Instance/Redlock) | ZooKeeper / etcd |
|------|----------------------|-------------------|
| Consistency model | Asynchronous replication, possible data loss | Consensus protocol (ZAB/Raft), strong consistency |
| Lock safety | Relies on clocks, not safe enough | Consensus guarantee, can be paired with fencing token |
| Performance | Extremely high (in-memory operations) | Relatively low (requires majority confirmation) |
| Operational complexity | Low | High (requires maintaining a consensus cluster) |
| Applicable scenarios | Efficiency-first (deduplication, rate limiting) | Correctness-first (finance, inventory) |

To summarize: a distributed lock is a useful tool, but it is not an equivalent drop-in replacement for a `std::mutex`. In a distributed environment, "mutual exclusion" changes from a deterministic guarantee to a probabilistic one—you need to choose the right tool based on business requirements, and either tolerate inconsistency in extreme cases in your design, or use mechanisms like fencing tokens as a safety net.

## Engineering Intuition Behind the CAP Theorem

You cannot talk about distributed systems without mentioning the CAP theorem. This conjecture, proposed by Eric Brewer in 2000 and proven by Seth Gilbert and Nancy Lynch in 2002, is a fundamental constraint in distributed system design. Let us not rush to give the definition, but instead understand it through a scenario.

### What Are the Three Properties

First, **Consistency**. It requires that all clients see the same data at any given moment—if you write a value to node A and immediately read from node B, you should be able to read the latest value. This does not mean "eventually consistent," but "consistent at all times." This is the strongest consistency guarantee, equivalent to linearizability.

Next, **Availability**. It requires that every request receives a non-error response—the system does not refuse service or return an error. Even if there are network issues, every surviving server will do its best to answer your request. Note that availability only cares about "whether a response can be obtained"; whether the data in the response is the latest is the concern of consistency.

Finally, **Partition Tolerance**. When a network partition occurs (some machines cannot communicate with each other), the system can still continue to operate. In distributed systems, a network partition is not a question of "whether it will happen," but "when it will happen"—networks are always unreliable, so partition tolerance is essentially mandatory.

### Why You Cannot Have All Three

The CAP theorem states that in a distributed system, when a network partition occurs, you can only choose Consistency (C) or Availability (A), but not both simultaneously.

Why? Let us explain with a concrete scenario. Suppose you have two servers, S1 and S2, each holding a copy of the data. Under normal circumstances, after S1 receives a write, it synchronizes to S2, and read requests on both sides can return the latest data. Now a network partition occurs—S1 and S2 cannot communicate with each other.

At this point, a client sends a write request to S1. S1 has two choices:

If S1 chooses to **accept the write but cannot synchronize to S2**, then S1 has the new data, but S2 still has the old data. At this time, read requests on S2 will return the old data—consistency is broken, but availability is preserved (S2 did not refuse service). This is choosing **AP**.

If S1 chooses to **reject the write (because it cannot synchronize to S2)**, then consistency is preserved (there is no write that takes effect on only half of the nodes), but availability is broken (the client received an error response). This is choosing **CP**.

There is no third option. You cannot both accept a write and guarantee consistency when synchronization is impossible—this is a logical contradiction.

### Choosing Between CP and AP

Having understood the core idea of CAP, let us look at the choices of a few actual systems.

A typical CP system is ZooKeeper. When a network partition occurs, if the ZooKeeper cluster cannot reach a quorum, it refuses service—it would rather be unavailable than return inconsistent data. This is reasonable for its role as a coordination service (storing configuration, performing leader election, providing distributed locks): these scenarios have extremely high correctness requirements, and brief unavailability is far better than an error.

On the other side, Cassandra is a representative AP system. Its design philosophy is "always available"—even if a network partition occurs, every node still accepts read and write requests, though it might return stale data. After the network recovers, it uses background read repair and anti-entropy mechanisms to make the replicas eventually consistent. This is reasonable for many internet applications: a one-second delay on social media (seeing stale data) is much better than "service unavailable."

> ⚠️ **Pitfall Warning**
> Do not treat CAP as a binary, either-or choice. In reality, the network is normal (no partition) the vast majority of the time, and the system can simultaneously provide good consistency and availability. CAP only tells you that you must choose one or the other in the extreme case of a network partition. Many modern systems support making different choices for different operations and at different configuration levels—for example, you can configure Cassandra for QUORUM reads and writes (leaning towards consistency) or ONE reads and writes (leaning towards availability).

## From Inter-Thread Communication to Network Communication

Looking back, although the differences between single-machine concurrency and distributed concurrency are huge, from the perspective of the communication model, there is a very elegant transition.

On a single machine, the most natural way for threads to communicate is **shared memory + locks**—this is also the model we have discussed for most of this volume. But you might remember that in ch07, we discussed the Actor model and the CSP/Channel model. The core idea of these models is: **do not communicate by sharing memory; instead, share memory by communicating**.

This idea is even more important in a distributed environment. Distributed systems do not have shared memory—you cannot make processes on two different machines share a `std::mutex`. They can only coordinate through network messages. Therefore, the Actor model and the CSP model are naturally designed for distributed scenarios: an Actor can be local, or it can be on a remote machine; a message can be an in-process function call, or it can be an RPC request over the network. From a programming model perspective, there is no essential difference between them.

This is why many distributed system frameworks have chosen the Actor model (such as Akka, Orleans)—it defers the decision of "local or remote" to the deployment phase, rather than hardcoding it into the program logic. You write an Actor's message handling logic locally, and when you deploy it, you put it on different machines; the code barely needs to change.

In the modern C++ ecosystem, the key infrastructure connecting "concurrency" and "distributed systems" is the **RPC framework**, with gRPC being the most mainstream. gRPC uses Protocol Buffers to define services and message formats, automatically generates client and server stub code, uses HTTP/2 for transport under the hood, and supports streaming communication. It is essentially a cross-network "function call"—you call a remote method just like calling a local function (of course, there are important semantic differences, such as timeouts and retries).

From the perspective of the concurrency model, each gRPC call can be viewed as a message passing between Actors: the client Actor sends a request message, the server Actor receives the message, processes it, and returns a response message. By wrapping gRPC's asynchronous API with C++20 coroutines (which we will demonstrate in the next chapter), we can write distributed concurrent code in a very natural way—with almost the same structure as writing local coroutines, only the underlying transport changes from function calls to network requests.

## Where We Are

In this chapter, we did something very important: we built a cognitive bridge between single-machine concurrency and distributed systems. We saw five fundamental differences—partial failure, unreliable networks, no global clock, latency scale shifts, and the skyrocketing cost of consistency—each of which profoundly influences the choice of concurrency model. Through the concrete case study of distributed locks, we understood the evolution from `std::mutex` to Redis and then to ZooKeeper/etcd, and we grasped the key insight that "a distributed lock is not an equivalent replacement for a mutex." The CAP theorem gave us a basic constraint framework in distributed design, while the Actor/Channel model provided a programming paradigm for a smooth transition from single-machine concurrency to distributed concurrency.

But understanding the differences is only the first step. In the next chapter, we will dive into the core难题 of distributed systems—**consistency**. When replicas on multiple machines need to agree on a value, things are far more complex than "just adding a lock." We will see the full spectrum from linearizability to eventual consistency, understand the core ideas of consensus protocols like Paxos/Raft, and use gRPC + C++20 coroutines to demonstrate the direction for writing distributed communication code in C++.

## References

- [Designing Data-Intensive Applications — Martin Kleppmann](https://dataintensive.net/) — Widely recognized as the best introductory book in the distributed systems field, covering CAP, consistency, and consensus protocols very thoroughly
- [CAP Theorem — Wikipedia](https://en.wikipedia.org/wiki/CAP_theorem) — The formal definition and history of the CAP theorem
- [How to do distributed locking — Martin Kleppmann](https://martin.kleppmann.com/2016/02/08/how-to-do-distributed-locking.html) — The classic rebuttal to Redlock, introducing the concept of fencing tokens
- [Latency Numbers Every Programmer Should Know — Jonas Bonér](https://gist.github.com/jboner/2841832) — An intuitive comparison of latencies for various operations (original data from Jeff Dean / Peter Norvig)
- [Is Redlock safe? — Salvatore Sanfilippo (antirez)](http://antirez.com/news/101) — The Redis author's response to Kleppmann's critique
- [Raft Consensus Algorithm](https://raft.github.io/) — The official resource for the Raft protocol, including a visual demonstration
