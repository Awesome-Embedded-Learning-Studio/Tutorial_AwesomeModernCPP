---
title: A First Look at Distributed Consistency Primitives
description: From linearizability to causal consistency, understand the consistency
  model spectrum and the core ideas behind Paxos/Raft, and build a distributed communication
  skeleton using gRPC + C++20 coroutines.
chapter: 9
order: 2
tags:
- host
- cpp-modern
- advanced
- 进阶
- 异步编程
- atomic
difficulty: advanced
platform: host
reading_time_minutes: 30
cpp_standard:
- 17
- 20
prerequisites:
- 从单机并发到分布式
- promise_type 与 awaitable
related:
- 协程 Echo Server 实战
translation:
  source: documents/vol5-concurrency/ch09-distributed-bridge/02-distributed-primitives.md
  source_hash: d2432028d4126602f13401934d7f183db36e63463af397a5b8803f03cbd5427c
  translated_at: '2026-05-20T04:51:00.478517+00:00'
  engine: anthropic
  token_count: 5073
---
# A First Look at Distributed Consistency Primitives

In the previous article, we explored the five fundamental differences between single-machine concurrency and distributed systems, understanding facts like "networks are unreliable, clocks are inaccurate, and partial failures are inevitable." To be honest, I was stunned the first time I encountered distributed consistency—on a single machine, consistency is almost "free" (costing only a few nanoseconds for lock/unlock), but in a distributed environment, it becomes something you can only achieve with paper-level protocols, multiple rounds of network communication, and majority voting. In this article, we face this core challenge head-on: **consistency**.

Let us build an intuition first: when a piece of data has replicas on multiple machines, will a client read the same value from different replicas? When will it read the latest value? How much can the data on different replicas diverge? The answers to these questions depend on which consistency model the system chooses. Consistency models are not a binary choice (either consistent or inconsistent); rather, they form a spectrum from strong to weak. Understanding this spectrum is fundamental to understanding distributed systems, and it serves as the core thread of this article.

## The Consistency Model Spectrum

What we need to do now is establish this spectrum using four consistency models, ranging from strong to weak. For each model, we will explain it using a concrete scenario rather than just throwing out a definition—understanding *why* a model is needed is far more important than memorizing *how* it is defined.

### Linearizability: The Strongest Guarantee

We start with the strongest. Linearizability is also known as strong consistency or atomic consistency. It means that every operation appears to occur atomically at a **single point in time** between its invocation and its response, and the time points of all operations form a total order. In plain terms—if we treat the distributed system as a black box, from an external observer's perspective, all operations look as if they happened on a single machine. This echoes the ``memory_order_seq_cst`` we discussed in ch03: the strongest memory order on a single machine guarantees that all threads see a consistent operation order, and linearizability is the equivalent guarantee in a distributed environment.

Let us illustrate this with a bank transfer scenario. Suppose you and your roommate share an account with a balance of 1000 yuan. You transfer 800 yuan out on your mobile app, and at the exact moment of the transfer, your roommate checks the balance at an ATM. Under linearizability, your roommate's query has only two possible outcomes: either they see 1000 yuan (your transfer has not taken effect yet) or they see 200 yuan (your transfer has already taken effect). It is absolutely impossible for your roommate to see an "intermediate state" like 500 yuan or 900 yuan.

Even more critical is the guarantee of time ordering: if you completed the transfer operation first (received a "transfer successful" response), and then your roommate initiated the query, your roommate is guaranteed to see 200 yuan—they cannot see the old value. This is the "real-time" guarantee of linearizability: the actual chronological order of operations matches the order presented by the system.

Linearizability is the strongest consistency guarantee, but it is also the most expensive. To implement it, every write operation must wait for acknowledgment from a majority of replicas before returning success, and every read operation must also query the majority for the latest value (or query the Leader and ensure the Leader has not changed). In terms of latency, this means at least one network round trip (usually multiple rounds). In terms of availability, it means that if a majority cannot be reached, the system must refuse service.

Which systems provide linearizability? ZooKeeper (for write operations and synchronous reads), etcd, and Consul, which we mentioned in the previous article, all provide it. Google Spanner achieves external consistency (which is even stronger than linearizability) through the TrueTime API mentioned in the previous article, and many relational databases in single-machine mode are naturally linearizable.

### Sequential Consistency: Relaxing Time Requirements

Okay, linearizability is the strongest, but also the most expensive. If we relax the requirements slightly—no longer requiring the actual chronological order of operations to match the order presented by the system, but only requiring that all processes see the same operation order—we get sequential consistency. Specifically, the operation order seen by all processes is a total order, but this order does not have to match the actual physical time of occurrence, as long as each process's own operations maintain the order specified in the program.

Returning to the bank transfer example. Suppose you transfer 800 yuan out on your phone, and then your roommate transfers 500 yuan out at an ATM. Under sequential consistency, the system can present the order as "your roommate transfers 500 first, then you transfer 800"—this is the opposite of your actual physical chronological order. But the key point is: all observers see the same order. No one will say "transferred 800 first" while someone else says "transferred 500 first."

The difference between sequential consistency and linearizability lies precisely in that "real-time" constraint: linearizability requires the system's presented order to match actual time, while sequential consistency does not. However, both require a globally consistent arrangement of all operations. This difference may seem subtle, but it is hugely significant in implementation—linearizability requires some form of global clock or consensus protocol to synchronize time, while sequential consistency only needs to guarantee the atomic broadcast order of operations.

### Causal Consistency: Only Causality, Not Global Order

If we relax the constraints even further, no longer requiring a total order for all operations, but only requiring that **causally related** operations be seen by all processes in the same order, while causally unrelated operations can be seen in different orders—that is causal consistency.

What does "causally related" mean? Simply put, if operation B reads a value written by operation A, then A and B have a causal relationship—A "caused" B. Or if operation C occurs after operation B (within the same process), and B causally depends on A, then C also causally depends on A. Beyond these direct and indirect dependency relationships, two operations are **concurrent**—there is no causal relationship between them.

Let us explain this with a social media scenario. User Alice posts: "The weather is great today!" (Operation A). User Bob sees Alice's post and replies: "Indeed it is!" (Operation B). Operation B causally depends on Operation A—because Bob replied only after seeing Alice's post. Under causal consistency, any user will definitely see Alice's post first, and then see Bob's reply—it is impossible to see Bob's reply without seeing Alice's post, as that would make no semantic sense.

At the same time, User Carol also posts: "Had hotpot today." (Operation C). Operation C and Operation A are concurrent—there is no causal relationship between them. Under causal consistency, different users can see A and C in different orders: some might see the weather post first and then the hotpot post, others the reverse—both are fine, because there is no "who caused whom" relationship between them.

Causal consistency is the practical choice for many distributed databases because its implementation cost is much lower than linearizability—you do not need global consensus, you only need to track and propagate causal relationships (usually with vector clocks) to guarantee semantic correctness. Dynamo-style systems (Amazon Dynamo, Apache Cassandra, Riak) provide eventual consistency with causal session guarantees under certain configurations. Strictly speaking, this is stronger than "pure" eventual consistency, but weaker than strict causal consistency.

### Eventual Consistency: The Weakest but Fastest

At the very bottom of the spectrum is eventual consistency. Its guarantee is very weak: if there are no new writes, eventually ("eventually" is a vague point in time, which could be milliseconds, seconds, or even minutes) all replicas will converge to the same value. Before convergence, different replicas may return different values—you might read the latest write from one replica, and a five-second-old stale value from another.

This guarantee sounds unreliable, but it is sufficient in many scenarios. DNS is a classic example of eventual consistency: when you update a DNS record, DNS servers worldwide may take minutes or even hours to fully update—but in most cases, this is perfectly acceptable. Like counts on social media, follower lists, comment counts—updating these data by a second or two has no disastrous consequences.

The advantage of eventual consistency lies in performance and availability: because there is no need to synchronously wait for other replicas, writes can return success immediately, and reads only need to access the local replica. In the event of a network partition, each replica can serve requests independently—availability is maxed out.

### The Hierarchy of Consistency Models

Great, now let us look at all four models together. They form a hierarchy from strong to weak:

```mermaid
flowchart TD
    A["Linearizability"] -->|"Satisfies linearizability → implies all below"| B["Sequential Consistency"]
    B -->|"Satisfies sequential consistency → implies all below"| C["Causal Consistency"]
    C -->|"Satisfies causal consistency → implies all below"| D["Eventual Consistency"]
```

The hierarchical relationship means: a system that satisfies linearizability also satisfies sequential consistency, causal consistency, and eventual consistency. Conversely, a system that satisfies eventual consistency does not necessarily satisfy causal consistency. For each step up the hierarchy, you gain a stronger consistency guarantee, but you also pay a higher price in latency and availability.

> ⚠️ **Pitfall Warning**
> In reality, very few systems "purely" implement only one consistency model—I learned this the hard way, initially assuming a certain database "was" eventually consistent, only to discover that under specific configurations it actually provided stronger consistency guarantees. Many systems offer tunable consistency levels; for example, Cassandra supports THREE read/write consistency levels: ONE, QUORUM, and ALL, and you can choose at each operation. QUORUM reads and writes can guarantee reading the latest written value (because the majority for writes and the majority for reads must overlap), but this does not strictly guarantee linearizability—truly strict linearizability requires additional mechanisms (like Raft's ReadIndex or lease read). Understanding what guarantees your system provides under what configuration is far more important than memorizing theoretical definitions.

## Core Ideas of Paxos/Raft

After understanding the spectrum of consistency models, a natural question arises: if we need strong consistency (such as linearizability), how exactly do we implement it? The answer is through **consensus protocols**. In the world of distributed systems, the core problem that consensus protocols solve is: getting a group of machines to agree on a value—even if some of those machines might crash or the network might partition. This shares a similar spirit with the atomic operations we discussed in ch03—both are about making multiple execution units (threads or machines) reach an agreement on the state of a value. The difference is that atomic operations rely on the CPU's cache coherence protocol, while distributed consensus relies on multiple rounds of network communication and voting.

Let us be clear upfront: we do not intend to give a complete protocol description of Paxos or Raft here (that is truly the workload of a full paper—Lamport's Paxos paper reads like a Greek myth, and while the Raft paper is much clearer, it is still over thirty pages). Instead, we will focus on the core ideas so you understand *why* they are designed this way.

### Why a Quorum?

The cornerstone of consensus protocols is the **quorum**. Suppose we have `$N$` machines, and a value needs to be accepted by at least `$\lfloor N/2 \rfloor + 1$` machines (i.e., a majority) to be considered "decided." Your first reaction might be—why a majority? Why not require unanimous agreement?

The core insight is: any two majorities must overlap. If there are 5 machines, a majority is at least 3. No matter how you choose them, any two groups of 3 machines share at least 1 machine in common. This overlap means: if a previous value has already been accepted by one majority, then any new majority must contain at least one machine that knows the previous value. As long as the protocol is designed properly, this "witness" machine can guarantee that the new value will not overwrite the previously decided value.

From this insight, tolerating `$f$` machine crashes requires at least `$2f + 1$` machines. In other words, to tolerate 1 crash you need 3 machines (`$3 = 2 \times 1 + 1$`), and to tolerate 2 crashes you need 5 machines (`$5 = 2 \times 2 + 1$`). This is why you often see coordination services like ZooKeeper, etcd, and Consul recommend 3-node or 5-node deployments—3 nodes tolerate 1 node failure, and 5 nodes tolerate 2 node failures.

### Leader Election: Who Gives the Orders

Having understood the principle of quorums, let us now look at Raft. Raft's design philosophy can be summarized in one sentence: "understandability first." When Diego Ongaro and John Ousterhout designed Raft, they explicitly made "easy to understand" a goal equally important as "correctness," forming a stark contrast with Paxos's "correct but unreadable" style. Raft decomposes consensus into three sub-problems: Leader election, log replication, and safety. Let us look at Leader election first.

In Raft, there is at most one Leader in the cluster at any time—all write requests are handled by the Leader, and all logs are replicated from the Leader to Followers. This "strong Leader" design is easier to understand and implement than Paxos's "multi-Proposer" model.

Leader election is driven by **terms** and **heartbeats**. Each term is a monotonically increasing integer, and there is at most one Leader per term. Under normal circumstances, the Leader periodically sends heartbeats (AppendEntries RPCs, even empty ones when there are no logs to replicate) to all Followers. If a Follower does not receive a heartbeat within an election timeout period, it assumes the Leader has failed and starts a new election.

The election process, in plain terms, is "a group of people voting for a leader": the Follower increments the current term, becomes a Candidate, votes for itself first, and then sends a RequestVote RPC to all other nodes. The voting rules for other nodes are: at most one vote per term, first-come-first-served (with one restriction: the Candidate's log must be at least as up-to-date as the voter's). If a Candidate receives votes from a majority, it becomes the new Leader and immediately starts sending heartbeats to prevent others from initiating further elections.

This process has a very clever randomization mechanism: each node's election timeout is randomly chosen within a range. This greatly reduces the probability of multiple nodes initiating elections simultaneously and causing a "split vote"—because their timeout durations differ, the node that times out first will usually initiate the election ahead of the others and win the majority of votes.

### Log Replication: The Leader Speaks, Followers Follow

Once the Leader is elected, log replication is quite straightforward—the core of the entire process is "the Leader says one thing, the Followers repeat it." The client sends a write request to the Leader, the Leader appends the operation to its own log, and then replicates this log entry to all Followers (via AppendEntries RPC). When the Leader confirms that this log entry has been accepted by a majority (including itself), it **commits** the log entry, applies it to the state machine, and returns success to the client.

The key safety guarantee is that committed logs are never overwritten. Raft achieves this through a simple constraint—when the Leader sends an AppendEntries, it carries the index and term of the previous log entry. Upon receiving it, the Follower checks whether the corresponding position in its own log matches. If it does not match, the Follower rejects the log entry, and the Leader backs off and retries until it finds a position where both sides agree, and then overwrites from there.

This mechanism guarantees that if two log entries have the same term number at the same index position on any Follower, their contents must be identical (because a Leader only creates one log entry at one index position within a term), and all log entries before that entry are also identical (through recursive matching checks). This is log consistency.

To summarize the entire Raft process with an analogy: imagine a committee (the cluster) whose members communicate via letters (network messages). They need to reach agreement on a series of decisions (the log). Raft's approach is to first elect a chairperson (Leader election), have the chairperson propose all decisions (log replication), and require majority approval for a decision to take effect (majority voting). If the chairperson loses contact, the committee votes to elect a new chairperson to continue the work. Although this analogy is rough, it captures the core design philosophy of Raft—the key to consensus is not "everyone agrees," but "a majority agreeing is enough," and the intersection of majorities guarantees the transfer of information.

## C++ Practice Directions

We have covered quite a bit of theory; now let us look at something practical. After understanding the theoretical foundations of distributed consistency, let us explore the directions for actually writing distributed communication code in C++. To be clear—we will not implement a complete distributed protocol (that is the scale of an independent project; a correct implementation of Raft alone can take weeks of work). Instead, we will show how to use gRPC + C++20 coroutines to build the basic skeleton for communication between distributed services. This utilizes the coroutine knowledge we learned in ch06, effectively connecting our previous accumulations.

### gRPC Basics: Defining Services with Protobuf

gRPC uses Protocol Buffers (protobuf) to define service interfaces and message formats. This is the key infrastructure in the modern C++ ecosystem that connects "concurrency" and "distributed systems," as we mentioned in the previous article. Suppose we want to implement a simple distributed key-value store service; the proto file would look something like this:

```protobuf
// kv_store.proto
syntax = "proto3";

package kvstore;

// 键值存储服务
service KvStoreService {
    // 获取指定 key 的值
    rpc Get(GetRequest) returns (GetResponse);

    // 设置 key-value
    rpc Put(PutRequest) returns (PutResponse);

    // 删除指定 key
    rpc Delete(DeleteRequest) returns (DeleteResponse);
}

message GetRequest {
    string key = 1;
}

message GetResponse {
    bool found = 1;
    string value = 2;
    int64 version = 3;    // 因果版本号，类似向量时钟的单调版本
}

message PutRequest {
    string key = 1;
    string value = 2;
    int64 expected_version = 3;  // 乐观并发控制：期望的当前版本
}

message PutResponse {
    bool success = 1;
    int64 new_version = 2;
}

message DeleteRequest {
    string key = 1;
}

message DeleteResponse {
    bool success = 1;
}
```

After compiling with the ``protoc`` compiler to generate C++ code, you will get a bunch of ``.pb.h`` and ``.pb.cc`` files, along with a ``.grpc.pb.h`` and ``.grpc.pb.cc``—the latter contains the gRPC server base class and client stub code. Do not be intimidated by this pile of generated files; what you truly need to care about are only the base class and the stub class.

### Server Implementation: Handling RPC Requests

Next, let us look at the server implementation—inheriting the generated ``KvStoreService::Service`` base class and overriding each RPC method. We use a simple in-memory map as the storage backend, paired with a ``std::shared_mutex`` for thread-safety protection. If you remember the read-write lock pattern discussed in ch02, this is its direct application.

```cpp
// kv_store_server.h
#pragma once

#include <grpcpp/grpcpp.h>
#include "kv_store.grpc.pb.h"

#include <string>
#include <unordered_map>
#include <shared_mutex>
#include <optional>

/// @brief 分布式键值存储的 gRPC 服务端实现
class KvStoreServer final : public kvstore::KvStoreService::Service {
public:
    KvStoreServer() = default;

    /// @brief 处理 Get 请求
    grpc::Status Get(grpc::ServerContext* context,
                     const kvstore::GetRequest* request,
                     kvstore::GetResponse* response) override
    {
        // 读锁：允许多个并发读
        std::shared_lock lock(mutex_);

        auto it = store_.find(request->key());
        if (it == store_.end()) {
            response->set_found(false);
            return grpc::Status::OK;
        }

        response->set_found(true);
        response->set_value(it->second.value);
        response->set_version(it->second.version);
        return grpc::Status::OK;
    }

    /// @brief 处理 Put 请求（带乐观并发控制）
    grpc::Status Put(grpc::ServerContext* context,
                     const kvstore::PutRequest* request,
                     kvstore::PutResponse* response) override
    {
        // 写锁：独占访问
        std::unique_lock lock(mutex_);

        auto it = store_.find(request->key());

        // 乐观并发控制：
        // 如果客户端发送了 expected_version，
        // 检查当前版本是否匹配
        if (request->expected_version() > 0) {
            if (it == store_.end()
                || it->second.version != request->expected_version()) {
                response->set_success(false);
                return grpc::Status::OK;
            }
        }

        int64_t new_version = (it != store_.end())
            ? it->second.version + 1
            : 1;

        store_[request->key()] = {request->value(), new_version};

        response->set_success(true);
        response->set_new_version(new_version);
        return grpc::Status::OK;
    }

    /// @brief 处理 Delete 请求
    grpc::Status Delete(grpc::ServerContext* context,
                        const kvstore::DeleteRequest* request,
                        kvstore::DeleteResponse* response) override
    {
        std::unique_lock lock(mutex_);

        auto erased = store_.erase(request->key());
        response->set_success(erased > 0);
        return grpc::Status::OK;
    }

private:
    struct StoreEntry {
        std::string value;
        int64_t version;
    };

    std::unordered_map<std::string, StoreEntry> store_;
    std::shared_mutex mutex_;    // 读写锁保护 store_
};
```

This code demonstrates several important design points. We used ``std::shared_mutex`` instead of ``std::mutex`` to protect the storage—read operations (Get) use a shared lock (``std::shared_lock``), and write operations (Put/Delete) use an exclusive lock (``std::unique_lock``). This is consistent with the read-write lock pattern we discussed in ch02: in read-heavy, write-light scenarios, a shared lock can significantly improve concurrency. Another point worth noting is the ``expected_version`` field in the Put request—this is an implementation of Optimistic Concurrency Control (OCC). When a client reads a value, it gets a version number, and when it writes back after modification, it includes this version number. If the server finds that the current version number does not match the client's expectation, it means someone else has already modified this value, and the write is rejected—the client needs to re-read, re-modify, and re-submit. This is much lighter than using a distributed lock, and it avoids the various security issues of distributed locks that we discussed in the previous article.

The code to start the server is also very concise:

```cpp
// main.cpp（服务端）
#include "kv_store_server.h"

int main()
{
    std::string server_address("0.0.0.0:50051");
    KvStoreServer service;

    grpc::ServerBuilder builder;
    builder.AddListeningPort(
        server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);

    std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
    std::cout << "KvStore 服务端启动，监听: "
              << server_address << "\n";

    server->Wait();
    return 0;
}
```

### Asynchronous gRPC: Wrapping CompletionQueue with Coroutines

So far, we have been using gRPC's **synchronous API**—every RPC call blocks the current thread until it completes. This is fine in low-concurrency scenarios, but if you use the synchronous model in high-concurrency scenarios (for example, a server needs to handle thousands of requests simultaneously), the number of threads will skyrocket, and context switching will directly become a bottleneck—this is the same problem as "why we need asynchrony" that we discussed in ch06.

gRPC provides an asynchronous API, the core of which is the ``CompletionQueue`` (CQ)—an event loop where all completed asynchronous operations post a completion event to the CQ, and you need a thread to continuously pull events from the CQ and process them. This model is very similar to the asynchronous I/O we discussed in ch06: essentially, it is event-driven + callbacks. But writing code directly with CQ is extremely tedious—you need to manually manage the lifecycle of request objects, manually handle various state transitions, and manually chain callbacks together. If we use C++20 coroutines to wrap the CQ, we can dramatically improve code readability. Let us look at a simplified example of a coroutine-based gRPC client call.

```cpp
#pragma once

#include <grpcpp/grpcpp.h>
#include "kv_store.grpc.pb.h"

#include <coroutine>
#include <iostream>
#include <memory>

/// @brief 用于包装 gRPC 异步调用的协程 awaitable
/// 这是一个简化版，展示了核心思路
template<typename ResponseType>
struct GrpcAwaitable {
    grpc::ClientContext context;
    ResponseType response;
    grpc::Status status;
    std::unique_ptr<grpc::ClientAsyncResponseReader<ResponseType>> reader;

    /// @brief 协程是否需要挂起（总是挂起，等待 gRPC 完成）
    bool await_ready() const noexcept { return false; }

    /// @brief 挂起时启动异步 RPC 调用
    void await_suspend(std::coroutine_handle<> handle)
    {
        // 启动异步调用，完成后恢复协程
        reader->StartCall();

        // Finish() 会在 CQ 上投递一个完成事件
        // 我们用一个 tag 来关联协程 handle
        reader->Finish(&response, &status,
                       reinterpret_cast<void*>(handle.address()));
    }

    /// @brief 协程恢复时返回响应
    ResponseType await_resume()
    {
        if (!status.ok()) {
            throw std::runtime_error(
                "gRPC 调用失败: " + status.error_message());
        }
        return std::move(response);
    }
};

/// @brief 协程化的 gRPC 键值存储客户端
class KvStoreCoroutineClient {
public:
    explicit KvStoreCoroutineClient(std::shared_ptr<grpc::Channel> channel)
        : stub_(kvstore::KvStoreService::NewStub(channel))
        , cq_()
    {}

    /// @brief 启动 CompletionQueue 事件循环（在独立线程中运行）
    void start_event_loop()
    {
        void* tag = nullptr;
        bool ok = false;
        while (cq_.Next(&tag, &ok)) {
            // 从 tag 恢复对应的协程
            auto handle = std::coroutine_handle<>::from_address(tag);
            if (handle && !handle.done()) {
                handle.resume();
            }
        }
    }

    /// @brief 异步 Get：协程化调用
    GrpcAwaitable<kvstore::GetResponse> get(const std::string& key)
    {
        GrpcAwaitable<kvstore::GetResponse> awaitable;

        kvstore::GetRequest request;
        request.set_key(key);

        awaitable.reader = stub_->AsyncGet(
            &awaitable.context, request, &cq_);

        return awaitable;
    }

    /// @brief 异步 Put：协程化调用
    GrpcAwaitable<kvstore::PutResponse> put(
        const std::string& key,
        const std::string& value,
        int64_t expected_version = 0)
    {
        GrpcAwaitable<kvstore::PutResponse> awaitable;

        kvstore::PutRequest request;
        request.set_key(key);
        request.set_value(value);
        request.set_expected_version(expected_version);

        awaitable.reader = stub_->AsyncPut(
            &awaitable.context, request, &cq_);

        return awaitable;
    }

    grpc::CompletionQueue& completion_queue() { return cq_; }

private:
    std::unique_ptr<kvstore::KvStoreService::Stub> stub_;
    grpc::CompletionQueue cq_;
};
```

The core of this code lies in the ``GrpcAwaitable`` struct—it is an object that satisfies the C++20 coroutine ``awaitable`` constraint, which is the exact mechanism we discussed in depth in ch06. When the coroutine ``co_await`` this object, ``await_suspend`` is called, which starts the gRPC asynchronous call and registers the coroutine handle as a tag on the ``CompletionQueue``. When the gRPC asynchronous operation completes, the CQ's event loop pulls out this tag (which is actually the coroutine handle), and then ``resume()`` resumes the coroutine execution. After the coroutine resumes, it retrieves the response result in ``await_resume``—the entire process follows the exact same pattern as the hand-written awaitable we did in ch06.

At the application layer, you can use it like this:

```cpp
/// @brief 示例：使用协程化的 gRPC 客户端
Task<void> demo_usage(KvStoreCoroutineClient& client)
{
    try {
        // 写入一个键值对
        auto put_resp = co_await client.put("hello", "world");
        std::cout << "Put 成功，新版本: "
                  << put_resp.new_version() << "\n";

        // 读取回来
        auto get_resp = co_await client.get("hello");
        std::cout << "Get 结果: found=" << get_resp.found()
                  << ", value=" << get_resp.value()
                  << ", version=" << get_resp.version() << "\n";

        // 乐观并发控制：带版本写入
        auto occ_resp = co_await client.put(
            "hello", "updated_world", get_resp.version());
        if (occ_resp.success()) {
            std::cout << "OCC 写入成功，新版本: "
                      << occ_resp.new_version() << "\n";
        } else {
            std::cout << "OCC 写入失败：版本冲突\n";
        }
    }
    catch (const std::exception& e) {
        std::cerr << "gRPC 错误: " << e.what() << "\n";
    }
}
```

You see, the application layer code is almost indistinguishable from writing a local function call—``co_await`` makes the asynchronous gRPC call look as linear and fluent as synchronous code, but underneath it is completely asynchronous: while waiting for the gRPC response, the current thread does not block, but instead goes to handle other coroutines or CQ events. This is the value of coroutines that we repeatedly emphasized in ch06—not making code faster, but making asynchronous code readable and maintainable.

> ⚠️ **Pitfall Warning**
> The ``GrpcAwaitable`` above is a simplified example that demonstrates the core idea of coroutine-based gRPC; do not take it straight into a production environment. In production, you need to handle more details: graceful shutdown of the CQ event loop, timeout control, retry logic, connection state management, thread-safe CQ access, and so on. If you do not want to build the wheel yourself (the author strongly recommends you do not), check out the [agrpc](https://github.com/Tradias/agrpc) library—it provides production-grade asynchronous gRPC wrappers based on Boost.Asio's C++20 coroutine support.

## Summary: The Journey of Volume 5

With this, the final article of Volume 5 is complete. Looking back at the learning path of this volume, we have traveled from "what is a thread" all the way to "how distributed systems communicate"—this has indeed been quite a journey.

**ch00 Concurrency Fundamentals** — We established a basic understanding of concurrency: concurrency and parallelism are not the same thing, Amdahl's Law and Gustafson's Law help us understand the upper and lower bounds of speedup, the trade-off between throughput and latency guides architectural choices, and some scenarios do not need concurrency at all. Correctness first, performance second—this is the principle that runs through the entire volume.

**ch01 Thread Lifecycle and RAII** — We got to know the lifecycle of ``std::thread``, understood the difference between ``join()`` and ``detach()``, and learned to use RAII guards to manage thread resources, ensuring threads do not leak or get forgotten. This is the basic skill of concurrent programming.

**ch02 Synchronization Primitives** — ``std::mutex``, ``std::condition_variable``, ``std::shared_mutex``... these are the toolbox of concurrent programming. We learned to use them to protect shared data, coordinate execution order between threads, and implement the producer-consumer pattern. We also saw their limitations: lock granularity is hard to control, deadlocks are easy to trigger, and performance is not ideal under high contention.

**ch03 Atomic Operations and Memory Model** — This is one of the hardest-core parts of Volume 5, and also the most enjoyable part for the author to write. Starting from the basic usage of ``std::atomic``, we dove deep into the six memory orders of the C++ memory model (``memory_order_relaxed``, ``memory_order_consume``, ``memory_order_acquire``, ``memory_order_release``, ``memory_order_acq_rel``, ``memory_order_seq_cst``), understood the reordering rules of compilers and CPUs, and mastered the reasoning method for happens-before relationships. This knowledge lets you know what you are doing when writing lock-free code.

**ch04 Concurrent Data Structures** — We applied the synchronization primitives and atomic operations learned earlier to specific data structures: thread-safe queues, concurrent maps, and ring buffers. We saw the trade-offs of different strategies: coarse-grained locking, fine-grained locking, read-write locking, and lock-free approaches.

**ch05 Tasks, Futures, and Thread Pools** — We elevated ourselves from the level of "raw threads" to the level of "tasks." ``std::async``, ``std::future``, and ``std::promise`` provide higher-level concurrency abstractions, while thread pools allow us to reuse thread resources and control concurrency levels. The task mindset is more suitable for most application scenarios than the thread mindset.

**ch06 Asynchrony and Coroutines** — C++20 coroutines represent a major paradigm shift in concurrent programming. Starting from the basic mechanisms of coroutines (``co_await``, ``co_return``, ``co_yield``, ``promise_type``, ``awaitable``), we learned to rewrite callback-based asynchronous code into a linear, readable form using coroutines. Coroutines are not a silver bullet, but they genuinely take the maintainability of asynchronous code to the next level.

**ch07 Actors and Channels** — We stepped out of the "shared memory + locks" model and explored message-passing-based concurrency paradigms. The Actor model and the CSP/Channel model avoid data races by "sharing nothing, communicating only via messages," making them naturally suited for multi-core and distributed scenarios.

**ch08 Debugging and Performance** — Concurrency bugs are the hardest bugs to debug. We learned to use ThreadSanitizer to detect data races, used profiling tools to locate lock contention, and understood performance pitfalls like false sharing and lock convoys.

**ch09 Distributed Bridging** — Which is these two articles. Starting from the boundaries of single-machine concurrency, we saw the five fundamental differences of distributed systems, understood the spectrum of consistency models, learned the core ideas of the Paxos/Raft consensus protocols, and finally used gRPC + C++20 coroutines to demonstrate the direction for writing distributed communication code in C++.

Looking back, none of the steps are isolated. The RAII mindset from ch01 runs through the entire volume—from thread management to lock management to connection management; the memory model knowledge from ch03 is the foundation for understanding the consistency models in ch09 (``memory_order_seq_cst`` and linearizability essentially answer the same question); the coroutine mechanism from ch06 is the cornerstone of the asynchronous gRPC wrapping in ch09; and the Actor model from ch07 gains its greatest value in a distributed environment—location transparency allows local code to be deployed to multiple machines with almost no modifications.

The study of concurrent programming will never be "complete"—this is a field that requires continuous practice, continuous stumbling into pitfalls, and continuous building of intuition. But if you have followed Volume 5 to this point, you should already have a solid theoretical foundation and enough practical experience to face the vast majority of concurrency scenarios. What remains is to hone your skills in real projects.

### Directions for Further Learning

If you want to continue deepening the foundation built in Volume 5, here are some directions the author has personally tested and recommends.

**Book Recommendations**: Martin Kleppmann's *Designing Data-Intensive Applications* is universally recognized as the best introductory book in the distributed systems field, covering core topics like consistency, consensus, and replication—the author strongly recommends reading at least the first five chapters. Anthony Williams's *C++ Concurrency in Action* is the authoritative reference for C++ concurrent programming; the second edition covers the C++17 standard (the third edition is expected to cover C++20), and it is a "dictionary" you can keep on your desk for随时 consultation. If you are particularly interested in lock-free programming, Herlihy and Shavit's *The Art of Multiprocessor Programming* is a classic textbook—but this book is rather academic and has a certain barrier to entry.

**Open Source Projects**: If you want to see real distributed consensus protocol implementations, etcd's Raft implementation (in Go, about 2000 lines of core code) is the best starting point—detailed comments, clear logic, and every concept from the Raft paper can be found in the code, making it a very comfortable read. In the C++ ecosystem, Apache brpc is a C++ RPC framework open-sourced by Baidu, with built-in components like bvar (concurrent variables) and bthread (coroutine scheduling), making it great material for learning production-grade C++ concurrent code.

**Practice Directions**: If you want to dive deeper into distributed systems development in C++, you can try implementing a simple distributed key-value store using gRPC + a Raft library (like ``libraft``)—this is a classic lab project from MIT 6.824 (Distributed Systems), with a moderate engineering workload but broad coverage. Once you complete it, your understanding of consensus protocols will be completely different.

## Reference Resources

- [Designing Data-Intensive Applications — Martin Kleppmann](https://dataintensive.net/) — The "bible" of distributed systems, covering all core topics including consistency, consensus, and replication
- [C++ Concurrency in Action, 2nd Edition — Anthony Williams](https://www.manning.com/books/c-plus-plus-concurrency-in-action-second-edition) — The authoritative reference for C++ concurrent programming (the third edition is expected to cover C++20)
- [In Search of an Understandable Consensus Algorithm (Raft paper)](https://raft.github.io/raft.pdf) — The Raft paper by Diego Ongaro and John Ousterhout, 100 times more readable than the Paxos paper
- [The Part-Time Parliament (Paxos paper) — Leslie Lamport](https://lamport.azurewebsites.net/pubs/lamport-paxos.pdf) — The original Paxos paper, which tells the story of a consensus protocol through an ancient Greek parliament
- [Jepsen Consistency Models](https://jepsen.io/consistency/models) — A visual hierarchy diagram and detailed explanation of consistency models
- [agrpc — gRPC with C++20 Coroutines](https://github.com/Tradias/agrpc) — An asynchronous coroutine wrapper library for gRPC based on Boost.Asio
- [C++20 Coroutines for Asynchronous gRPC Services — Dennis Hezel](https://medium.com/3yourmind/c-20-coroutines-for-asynchronous-grpc-services-5b3dab1d1d61) — How to adapt gRPC's CompletionQueue to C++20 coroutines
- [MIT 6.824 Distributed Systems](https://pdos.csail.mit.edu/6.824/) — MIT's distributed systems course, including a Lab to implement Raft
