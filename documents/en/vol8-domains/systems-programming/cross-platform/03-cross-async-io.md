---
title: "Cross-Platform Asynchronous I/O Abstraction"
description: "After revealing the differences among four platforms, can C++ concepts constrain an asynchronous backend at compile time? This article includes epoll, kqueue, IOCP, and io_uring in an eight-dimensional difference matrix (notification semantics, interest management, waiting cost, regular files, timeouts, wake-up channels, thread models, in-flight scale, all numbers are drawn from five real tests on both sides, the kqueue column is fully annotated with the FreeBSD manual page's documentation), provides five-line AsyncBackend concept and two backend skeletons (EpollBackend uses EPOLLONESHOT to pad readiness into completion, the adapter layer manually reads bytes, IocpBackend's ReadFile adds OVERLAPPED directly, id is hidden in the extended structure and returns with the package), unifies the request's offset field to include the regular file dimension into the type, and runs two real reads on both sides without changing the same drive_one_round (pipe 5+5 bytes go MOD again, file offset 0 and 6 each read 5 bytes), negative examples lack take's LazyBackend and are blocked before instantiation by both sides' GCC 16, the diagnosis states 'the required expression 'b.take()' is invalid' on both sides, the process includes a string_view passing through printf %s causing a segmentation fault as a teaching point, collected from three sources into one completion and handed over to ch07 platform abstraction chapter"
chapter: 8
order: 3
platform: host
difficulty: advanced
cpp_standard: [20]
reading_time_minutes: 27
prerequisites:
  - "I/O Multiplexing: select, poll, and epoll's boundaries and costs"
  - "timerfd and eventfd: Time and event fd-ization"
  - "io_uring: Turning I/O waiting into harvesting completion events"
  - "OVERLAPPED Asynchronous I/O and WaitForMultipleObjects"
  - "IOCP Completion Ports"
  - "Asynchronous I/O and Event Loops"
related:
  - "Asynchronous I/O and Event Loops"
  - "OS Resource RAII Pattern: fd, HANDLE, and the same skeleton mapping"
  - "Error Handling Pattern: From errno to expected"
  - "Reactor Pattern: Wrapping epoll into an event loop + callback, and why it's 'synchronous non-blocking'"
tags:
  - host
  - cpp-modern
  - advanced
  - 系统编程
  - POSIX
  - Win32
  - 异步编程
  - concepts
current_status:
  title: Being Polished
  detail: This batch has just been written, and is currently being read and polished. The expression and real test口径 may change at any time.
  variant: polishing
translation:
  source: documents/vol8-domains/systems-programming/cross-platform/03-cross-async-io.md
  source_hash: ac66f1da1878739c968aeb97714cdb373a681a8ea6167812579342106ff40004
  translated_at: '2026-11-22T14:14:28+00:00'
  engine: qwen3:14b
  token_count: 18257
---
# Cross-Platform Asynchronous I/O Abstraction

We've reached the end of our discussion on both sides in this chapter on I/O multiplexing. On the Linux side, the three articles measured the boundaries of select, poll, and epoll as numbers, integrated timerfd and eventfd into the same table, and finally let io_uring turn waiting into harvesting. On the Windows side, the two articles started from the event harvesting of OVERLAPPED and moved into the completion queue of IOCP. Both sides left a sentence at the end, and both pointed to this article: a one-to-one comparison of the two rings and IOCP, the difference matrix of Reactor and Proactor, which we will fulfill today. And the real question this article wants to answer is only one: after revealing the differences of the four backends, can C++ concepts constrain the shape of "an asynchronous backend" at compile time, allowing the same user code to run on both sides?

We'll fix the source and name once and for all, and the rest of the text will use these names. The three articles on the Linux side are [L01](../linux/io-multiplexing/01-select-poll-epoll.md), [L02](../linux/io-multiplexing/02-timerfd-eventfd.md), and [L03](../linux/io-multiplexing/03-io-uring.md), and the two articles on the Windows side are [A01](../windows/async-io/01-overlapped.md) and [A02](../windows/async-io/02-iocp.md). Every number in the matrix comes from the archives of these five articles, and we will not retest any of them. Our own experiments only arranged two groups: e1 is responsible for compiling and running the concept skeleton on both sides, and e2 uses a defective backend to verify the compile-time interception. The numbering of the two groups is unrelated to the e series of the five articles on both sides. When you look up the archives, just recognize the directories. Our content is all collected in the warehouse's `code/volumn_codes/vol8/systems-programming/cross-platform/03-cross-async-io/` directory, and all files starting with e are from this article. We also explain the output block's口径: the two blocks of e1 are fully quoted, and the diagnosis of e2 is excerpted. When you match, refer to the archives.

We also explain the three boundaries in advance, so you don't have to wait for things you can't see. The architecture of the event loop (while + timer + wait + dispatch) and the connection with coroutines is the main course of vol5's [Asynchronous I/O and Event Loop](../../../vol5-concurrency/cookbook/b-topics/06-async-io-event-loop.md), which points out a path: the interface is unified, and each platform implements its own implementation underneath, and libuv and Asio both follow this path. We implement it in this article, but we only collect the "backend" layer, and we don't repeat the course on the other side. The differences in the kernel layer, such as interest tables, ready queues, LT and ET, are covered in [epoll I/O multiplexing of the network volume](../../networking/02-epoll-io-multiplexing.md), and we use the link without expanding. The unified representation of handles and the unified reporting of errors are the main topics of ch07 platform abstraction chapter. In this article, the request.target uses uintptr_t roughly to carry, which is left for ch07 to collect.

We explain the environment口径 separately on both sides, and the output later must match it. On the Linux side, it comes from the author's WSL2, with the kernel running on 6.18.33.2-microsoft-standard-WSL2 build, and g++ uses 16.2.1. The compilation command is `g++ -std=c++20 -O2 -Wall -Wextra -D_FORTIFY_SOURCE=2`, and the number of warnings obtained is zero. On the Windows side, it comes from the author's Win11 26200, using MSYS2 UCRT64's g++ 16.1.0, called from WSL through interop, and the compilation command is `/mnt/c/msys64/ucrt64/bin/g++.exe -std=c++20 -Wall -Wextra`, and the number of warnings obtained is also zero. The interop link and products need to add execution details, which [the first article on Windows file I/O](../windows/file-io/01-win32-file-io.md) has explained, and we won't repeat it. The skeleton is self-contained: the unique_fd, sys_call from the beginning of the volume, as well as the unique_handle and check_win32 on the other side, we didn't invite any of them this time. The standard library's header plus the platform's header can be compiled, aiming to put the shape of the concept in the open. All outputs are captured from the same round on 2026-10-04.

## Eight Dimensions, Placing Four Backends into a Table

We selected eight dimensions from the five real tests, and each number in the grid can be traced back to its source. The kqueue column is entirely from the FreeBSD kqueue(2) manual page. Our local machine has neither BSD nor macOS, and we have no real test numbers. We will explain the口径 in the later text.

| Dimension | epoll (real test) | kqueue (document) | IOCP (real test) | io_uring (real test) |
| --- | --- | --- | --- | --- |
| Notification Semantics | Ready, wakes up to report who can read, but the action is still up to you | Ready, EVFILT_READ reports readable | Completion, the package itself carries bytes, and taking it is immediate | Completion, CQE's res is the number of bytes |
| Interest Management | epoll_ctl is permanently resident in the kernel, fdinfo's tfd line is visible | changelist is passed with each kevent, registration and harvesting are combined | No interest table, the handle is hung on the port once | No registration, the request itself carries fd and offset |
| Waiting Cost and Batch | All three levels are about 990ns, unrelated to N, and ready batch is recovered | Waiting and registration are the same call, two steps combined into one | WFMO has a 64 limit, 65 and above will result in WAIT_FAILED plus 87 | Submit once plus wait once, the rest of the harvesting is user-space peek |
| Regular Files | ADD directly returns EPERM, O_NONBLOCK semantics are not effective | vnode can be hung, not at EOF, opposite to epoll | Native, offset is in OVERLAPPED | Native, READ carries offset |
| How Timeout Enters the Mechanism | timerfd is converted into fd and added to the table | EVFILT_TIMER is a filter, ident is a custom identifier | GQCS's deadline parameter | Timeout itself is a request, equal to read |
| Wake-up Channel | eventfd is added to the table | EVFILT_USER plus NOTE_TRIGGER | PQCS sends a package to the bare port | (This volume has not tested) |
| Thread Model and Completion Order | Engineering is mainly single-threaded event loop | (Manual page does not cover) | Multiple threads sleep on the same port, concurrency is the upper limit, completion order follows feeding rhythm | Completion is based on wall clock, unrelated to submission order |
| In-flight Scale and Handle | Registration scale is subject to fd quota, WSL2 factory is 1048576 | (Manual page does not cover) | 100 in-flight on one port, zero scanning | Ring capacity is self-defined, entries give 8 to kernel sq=8 cq=16 |

The semantics of notification is the first dimension, and it is the dimension that divides the four backends into two groups. epoll wakes up to report who can act, but the action is still up to you, while io_uring's CQE directly fills in the number of bytes read this time, in L03's E2, 64 4KiB reads all have res of 4096. IOCP's completion package is also taken immediately, A02's e6 used 100 in-flight to do the same scenario comparison: each round of event-based waking up requires asking each of the 100 events one by one, and the probe records each round of 100 times, while the scanning count on the port side is zero. L03 introduced the names of these two schools at the beginning, calling the ones waiting for readiness Reactor (reactor), and the ones waiting for completion Proactor (proactor). The differences in the matrix after this are almost all derived from this dimension.

The residence of the interest table determines how much to move each round of waiting. Select modifies the parameters passed in as draft paper, and if the bitmap is not rebuilt, events are missed, and in L01's E4, the unrebuilt bitmap missed the later fd's event. Poll moves the entire table in and out each round, and for N=500, it's 4000 bytes, and for 4096, it's 32768 (L01's E2 and E3). Epoll moves the interest table into the kernel's permanent residence, and the tfd line in fdinfo is the registration table (L01 E6). IOCP干脆 skips the interest table, and hanging the handle on the port once is enough (A02 talks about building the port and hanging the handle), and io_uring skips registration altogether, and the request itself carries fd, buffer, and offset, the writing method can be seen in L03 E2's prep_read.

The cost of the waiting call itself, epoll proves with three levels of flat lines that it is unrelated to N: three levels of N=64, 500, 4096, each round's read is 994, 994, 985 nanoseconds (L01 E3), and epoll_wait also recovers the batch of ready, returning several, and only processes several (L01 E2). The hard upper limit on the Windows side is in WFMO: 64 is passed, and from 65, the return is WAIT_FAILED plus 87 (87=ERROR_INVALID_PARAMETER), and the MsgWait family with message queues gives another quota, 63 is the only one that is well-behaved (A01 e4). GQCS combines waiting and packet retrieval into the same call, and batch retrieval has another GQCSEx, A02 mentions the name but doesn't test, and we annotate according to the document's口径. The batch of io_uring is measured in L03's E2: 64 reads only cost submit once plus wait once, and the rest of the 63 harvests are all user-space peek, without entering the kernel.

The treatment of regular files is the row with the greatest contrast in the matrix. epoll_ctl's ADD directly returns EPERM, and the O_NONBLOCK flag is set, but the semantics are not effective, and read will never tell you with EAGAIN that there is nothing now (L03 E4). IOCP and io_uring are both native: one's offset is in OVERLAPPED (A02 e1's three out-of-order offset reads), and the other's READ carries offset (L03 E2). kqueue's vnode can also be hung, and it is exactly the opposite of epoll's approach, we will explain it when we talk about kqueue. This article's e1 on the Windows side will do it again, reading 5 bytes each from offset 0 and 6 of the same file.

The mechanisms for timeout and wake-up are different on both sides. timerfd converts the timer into an fd and adds it to the table, the median for 1ms is 999.3µs, p99 is 1029µs (L02 E4), L02 also cited an old real test on the same machine's Windows side, Sleep(5) actually slept about 12.6 milliseconds, the difference in precision is itself a lesson on the platform. io_uring makes timeout a request, independent TIMEOUT is based on wall clock, LINK_TIMEOUT and the object being timed out are mutually canceled, res gives -62 and -125 (L03 E6). GQCS's deadline is in the parameters (A02 e1's 800ms event). For the wake-up channel, Linux uses eventfd, one write(5) under the combination of LT and semaphore can wake up 5 times, L02's E2 calls it the busy notification level, and the corresponding Windows is PostQueuedCompletionStatus (we follow A02's call it PQCS), just send a package to the bare port (A02 e4's shutdown sentinel). We didn't test the wake-up channel of io_uring in this volume, left it blank in the matrix, and didn't compile the numbers.

The differences in thread models and scale, we only read the real tests. IOCP allows us to let multiple threads sleep on the same port, and the concurrency is the upper limit rather than the quota: when feeding at intervals, 4 worker threads 8 packages all entered one thread, when feeding 8 packages at once, the concurrency 0 level had four threads split the packages in the same tick (A02 e3). The completion order follows the feeding rhythm rather than the submission order, the submission order is 1..6, the completion order is 6 5 4 3 2 1 (A02 e2, and the same scenario in A01 e5 is a mirror image), and the completion of io_uring is also based on wall clock (L03 E6, the two TIMEOUTs submitted in reverse order arrived at 100 and 350). On the epoll side, the main engineering form is a single-threaded event loop, we follow the口径 of the event loop chapter in vol5, and we haven't tested the behavior of multiple threads waiting for the same epfd in this volume, we don't write. In terms of scale, we look at the last two entries: the same 100 in-flight, the event-based is forced into two segments by the 64 upper limit, each segment wakes up and still needs a full rescan, while IOCP has zero scanning on one port (A02 e6). The registration scale of epoll is ultimately subject to fd quota, L01's E1 measured the real rlimit, only poll actually checked, when pressed to soft=1024, 1050 directly gave EINVAL, and WSL2's factory value is 1048576. The capacity of io_uring's ring is self-defined, in L03's first probe, entries give 8, and the kernel is configured with sq=8, cq=16.

## Five-Line Concept, Shape Fixed in Completion

The path pointed out in the event loop chapter of vol5, we now take. A unified interface must be established, and the shape selection is unavoidable. The four backends here are divided into two schools: epoll and kqueue are readiness-based, IOCP and io_uring are completion-based, and the only common intersection is the completion-based shape. Readiness can be adapted into completion: when readiness arrives, the adapter layer takes the data back by itself, and the upper layer receives a completion event. The reverse is not possible: completion-based backends never report who is ready to you, they directly do the work, and you have no intermediate product of readiness to pass to the interface. So the concept is fixed as completion-based, IOCP and io_uring are direct recordings, and epoll needs a layer. The part of libuv on Unix has the layer we padded, and this article makes it compilable evidence.

We now look at the body of the concept, which is written in five lines:

```cpp
// e1_backend_concept.cpp(节选)
template <typename B>
concept AsyncBackend = requires(B& b, typename B::request r, int timeout_ms) {
    typename B::completion;
    typename B::request;
    { b.submit(r) } -> std::same_as<bool>;                              // 交出请求
    { b.wait(timeout_ms) } -> std::same_as<int>;                        // 等完成进队
    { b.take() } -> std::same_as<std::optional<typename B::completion>>;// 逐个取走
};
```

We then add two types, along with that particular offset field:

```cpp
// e1_backend_concept.cpp(节选,注释为行文所加)
struct completion {
    std::uint64_t id;      // 哪一发请求完成了
    std::size_t   bytes;   // 本次搬运的字节数
    int           error;   // errno 或 GetLastError, 0 为成功
};

struct read_request {
    std::uint64_t      id;      // 咱们自己的关联凭证
    std::uintptr_t     target;  // fd 或 HANDLE, 两侧含义不同
    void*              buf;
    std::size_t        len;
    unsigned long long offset;  // 文件语义必填, 流设备忽略
};
```

The offset field is worth a closer look. The offset of IOCP is in OVERLAPPED, and io_uring's READ also requires an explicit offset, while epoll only has stream devices, and pipes and sockets have no concept of offset. A field that collects, is the difference in the regular file dimension in the matrix: the two completion-based ones can both handle files, but our implemented EpollBackend cannot, so its offset is always idle. We will use it immediately in this article's e1 on the Windows side.

## EpollBackend: Padding a Layer of Adaptation

The Linux backend, submit does the job of arming our interest:

```cpp
// e1_backend_concept.cpp(节选)
bool submit(request r) {
    ::epoll_event ev{};
    ev.events = EPOLLIN | EPOLLONESHOT;
    ev.data.u64 = r.target;
    int op = armed_.count(r.target) ? EPOLL_CTL_MOD : EPOLL_CTL_ADD;
    if (::epoll_ctl(epfd_, op, static_cast<int>(r.target), &ev) != 0)
        return false;
    armed_[r.target] = true;
    inflight_[r.target] = r;
    return true;
}
```

EPOLLONESHOT does the job of aligning one registration with one completion: the event is reported once, the interest is automatically released, and the next submit re-submits using MOD. The semantics of completion require one-to-one correspondence, and we follow the network volume's reference for the differences between LT and ET, the skeleton's ONESHOT has already managed the lifecycle.

Wait is the layer we padded:

```cpp
int wait(int timeout_ms) {
    ::epoll_event evs[8];
    int n = ::epoll_wait(epfd_, evs, 8, timeout_ms);
    int got = 0;
    for (int i = 0; i < n; ++i) {
        auto it = inflight_.find(evs[i].data.u64);
        if (it == inflight_.end()) continue;  // 已撤单的兴趣,跳过
        request r = it->second;
        completion c{r.id, 0, 0};
        ssize_t nb = ::read(static_cast<int>(r.target), r.buf, r.len);
        if (nb < 0) {
            c.error = errno;
        } else {
            c.bytes = static_cast<std::size_t>(nb);
        }
        done_.push_back(c);
        ++got;
    }
    return got;
}
```

You can see where this layer is padded. epoll_wait wakes up to report only which one can read, the read in the loop is the adapter layer's own hand, bytes are moved by the adapter layer, not from the kernel, and error is also filled with the errno of this read. We hide the intermediate product of readiness from the upper layer, and hand over a completion. Take then takes from the done_ queue one by one, there's not much trick. The skeleton only demonstrates reading, the real adapter layer must also receive EPOLLERR and EPOLLHUP (L01 E6 real test kernel automatically supplements), and must manage the write half and non-blocking retries, we don't pretend it's complete.

## IocpBackend: No Translation, Direct Recording

The submit on the Windows side doesn't need us to pad, it directly records ReadFile plus OVERLAPPED:

```cpp
// e1_backend_concept.cpp(节选)
struct ovx {                    // OVERLAPPED 扩展: 身份随完成包回来
    OVERLAPPED ov;
    std::uint64_t id;
};

bool submit(request r) {
    inflight_.push_back(ovx{});
    ovx& x = inflight_.back();
    x.ov.Offset = static_cast<DWORD>(r.offset & 0xFFFFFFFFull);
    x.ov.OffsetHigh = static_cast<DWORD>(r.offset >> 32);
    x.id = r.id;
    BOOL ok = ::ReadFile(reinterpret_cast<HANDLE>(r.target), r.buf,
                         static_cast<DWORD>(r.len), nullptr, &x.ov);
    if (!ok && ::GetLastError() != ERROR_IO_PENDING) {
        inflight_.pop_back();
        return false;
    }
    return true;
}
```

We split the request's offset into Offset and OffsetHigh, and then hang our own id next to the OVERLAPPED. When the handle has FILE_FLAG_OVERLAPPED, the returned FALSE plus 997 (ERROR_IO_PENDING) is a normal in-flight receipt, not a failure, which is the same as the receipt recorded in A02.

We didn't touch anything on the wait side, we just did the GQCS work:

```cpp
int wait(int timeout_ms) {
    DWORD bytes = 0;
    ULONG_PTR key = 0;
    LPOVERLAPPED pov = nullptr;
    BOOL ok = ::GetQueuedCompletionStatus(port_, &bytes, &key, &pov,
                                          static_cast<DWORD>(timeout_ms));
    if (pov == nullptr) return 0;  // 超时,没有包
    completion c{0, static_cast<std::size_t>(bytes), 0};
    if (!ok) c.error = static_cast<int>(::GetLastError());
    auto it = find_by_ov(pov);
    if (it != inflight_.end()) {
        c.id = it->id;
        inflight_.erase(it);  // 完成取走,OVERLAPPED 这才可以回收
    }
    done_.push_back(c);
    return 1;
}
```

Bytes are filled in by the kernel and returned, we didn't write a single line of code for transportation, this is the difference between direct recording and adaptation. Identity goes through pov: the three-piece set in the completion package, key is handle-level, can't distinguish which one of the same handle, A02's e1 tested pov and key's two-level division with pointers, so our id is hidden in the extended structure, and after the completion package returns, we find it back. Lifecycle is another important matter: OVERLAPPED must live through the entire in-flight, if it is moved before the completion is taken, the driver may still be writing to that memory (A02's advice on closing the handle), so `inflight_` uses `std::list`, inserting and deleting don't move the address of the elements, vector expansion can't guarantee. Attaching the handle to the port is a separate step, remember to bring FILE_FLAG_OVERLAPPED when you open the handle. Both sides' class tails have a `static_assert(AsyncBackend<...>)`, if it doesn't compile, it flips immediately, and the assertion passes on both sides' compilations.

## e1: The Same User Code, Running Two Rounds on Both Sides

The generic drive called by constraints is drive_one_round, this code we didn't change a single word on both sides:

```cpp
// e1_backend_concept.cpp(节选)
template <AsyncBackend B>
bool drive_one_round(B& backend, typename B::request r, std::size_t expect_bytes,
                     std::string_view expect_data, std::string_view tag) {
    if (!backend.submit(r)) {
        std::printf("[%s] submit 失败\n", tag.data());
        return false;
    }
    int n = backend.wait(2000);
    auto c = backend.take();
    if (!c) {
        std::printf("[%s] wait=%d, take: 无完成\n", tag.data(), n);
        return false;
    }
    std::printf("[%s] backend=%s wait=%d take: id=%llu err=%d bytes=%zu data='%.*s'\n",
                tag.data(), B::name, n, static_cast<unsigned long long>(c->id),
                c->error, c->bytes, static_cast<int>(c->bytes),
                static_cast<const char*>(r.buf));
    return c->error == 0 && c->bytes == expect_bytes &&
           std::memcmp(r.buf, expect_data.data(), expect_bytes) == 0;
}
```

The compilation commands are the two we explained at the beginning, both sides received zero warnings, and the output of the run we paste side by side:

```text
Linux 侧(e1_linux.out):
[轮1] backend=epoll wait=1 take: id=1 err=0 bytes=5 data='hello'
[轮2] backend=epoll wait=1 take: id=2 err=0 bytes=5 data='cross'
PASS: 两轮完成事件与数据全部对上

Windows 侧(e1_windows.out):
[轮1] backend=iocp wait=1 take: id=1 err=0 bytes=5 data='hello'
[轮2] backend=iocp wait=1 take: id=2 err=0 bytes=5 data='cross'
PASS: 两轮完成事件与数据全部对上
```

The only difference in the two outputs is the backend= column. The two rounds on Linux come from two writes to the pipe, round 2 tests the lifecycle after MOD is re-armed after ONESHOT. The two rounds on Windows come from two offsets of the same data file, the file is created by the program itself, writes 20 bytes of hello cross-platform, and then reopens it with FILE_FLAG_OVERLAPPED, hanging on the port, reading 5 bytes each from offset 0 and 6, and the returned results are exactly hello and cross. The offset field is effective in the file's semantics, and the two completion packages answer it. id, err, bytes, data are all aligned on both sides, our same user code runs two real reads on both kernels.

## e2: A Backend Missing take is Blocked at Compile Time

Having only positive examples is not enough, we also need to provide a negative example that doesn't meet the constraints, otherwise the concept is just a decoration. LazyBackend provides submit and wait, but lacks take:

```cpp
// e2_negative.cpp(节选)
struct LazyBackend {
    using completion = ::completion;
    using request = ::read_request;
    bool submit(request) { return true; }
    int wait(int) { return 0; }
};

static_assert(!AsyncBackend<LazyBackend>,
              "LazyBackend 不许满足 AsyncBackend —— 缺 take 编译期就该现形");

// 与 e1 相同的受约束驱动
template <AsyncBackend B>
int drive(B& backend) {
    return backend.wait(1000) > 0 ? 0 : 1;
}
```

The file has two checks, one positive and one negative. `static_assert(!AsyncBackend<LazyBackend>)` this line compiles, the concept truthfully answers that it is not satisfied, its answer is not ambiguous, and it is a mirror image of the positive example assertion in this article's e1. And the step of passing LazyBackend to drive is destined to be uncompileable, we use `-c` to compile only and not link on both sides, we want the diagnosis itself. The diagnostics from both sides' GCC 16, we extract the key lines, the content from Linux 16.2.1 and MSYS2 UCRT64 16.1.0 is consistent, the differences are all in the shape: the quotes in the Linux archive are curved, the ones on Windows are straight, our excerpt below is arranged with straight quotes, and the Linux archive has an extra line of depth hint at the end (the full text is in the two .err archives):

```text
e2_negative.cpp: In function 'int main()':
e2_negative.cpp:64:17: error: no matching function for call to 'drive(LazyBackend&)'
  • there is 1 candidate
    • candidate 1: 'template<class B>  requires  AsyncBackend<B> int drive(B&)'
      • template argument deduction/substitution failed:
        • constraints not satisfied
          ...
          • the required expression 'b.take()' is invalid
            e2_negative.cpp:40:13:
               40 |     { b.take() } -> std::same_as<std::optional<typename B::completion>>;
```

The diagnosis points out what is missing: `the required expression 'b.take()' is invalid`, even bringing out the original text from the concept. We compare it with the old #ifdef platform switching method, when the type is missing a member, the error is exposed at an uncertain step, it may compile, and only be seen at link time, or it may be delayed until runtime when an error occurs in a certain call. The interception point of the concept is certain: the constraint gives a verdict before instantiation, even pointing out which item is missing. We don't give a unified judgment on the exposure of the old method in each stage, the evidence we want is in the diagnosis above.

## kqueue: The Entire Column is the Documentation's口径

The口径 of the kqueue column in the matrix, we explain separately: we don't have a single machine of BSD or macOS, each statement comes from the FreeBSD kqueue(2) manual page, the version口径 is 15.1-RELEASE, the date of reference is 2026-10-04, we didn't test any number, and we won't pretend to have run it.

The parts in the manual page that can be directly matched with our matrix are as follows. kevent's one call simultaneously receives changelist and eventlist, registration and harvesting are combined in the same call, the manual's original words are `All changes contained in the changelist are applied before any pending events are read from the queue`, it is a structural difference from epoll_ctl plus epoll_wait's two steps. We also look at the seven fields of struct kevent, `ident`, `filter`, `flags`, `fflags`, `data`, `udata`, and the `ext` array, the value of `udata` is not changed by the kernel, which is the same as `epoll_event.data`. The trigger method is not borrowed from epoll's words in the manual: the default reports the current state, the event with EV_CLEAR is reset after being taken, suitable for the type of filter that reports state changes, we translate it with epoll's words as level trigger and edge reset, EV_ONESHOT and epoll have the same name and meaning, and EV_DISPATCH disables after sending.

The comparison of the filter family is also worth a look. EVFILT_READ will change according to the type of the descriptor: for pipes, data directly brings back the number of bytes that can be read, and for vnode (ordinary files), it can also be hung, it reports when the file pointer is not at EOF, which is exactly the opposite of epoll's EPERM rejection of ordinary files, and for sockets, there is also NOTE_LOWAT to lower the water level. EVFILT_TIMER also follows the filter's path, the manual says ident uses a custom identifier instead of fd, not occupying a descriptor is what we follow ident, and its comparison with timerfd is in the matrix's timeout row. EVFILT_USER plus NOTE_TRIGGER is a user-level wake-up channel, the same as eventfd and PQCS. We haven't mentioned EVFILT_VNODE yet, it is the same as inotify, EVFILT_SIGNAL reports signals, and EVFILT_AIO's registration follows POSIX AIO's sigevent. We haven't tested any of these, you refer to the manual page. Some day when you get on Mac, use the concept in this article to add a third backend, it's a ready-made exercise: its readiness temperament is the same as epoll, the padded layer will look very similar.

## string_view Can't Pass Through printf's %s

We had to redo one place before finalizing the skeleton. The name of the backend was initially written as static constexpr std::string_view, and in drive_one_round, it was entirely passed to printf's %s. varargs doesn't recognize types, it only recognizes character pointers, string_view passing through %s is undefined behavior. The compile-time noise is only -Wformat and similar warnings, but it can't become an error, we didn't stop at that time, Linux side ran and immediately had a segmentation fault, the exit code left was 139.

The fix is unobtrusive, just change the name to static constexpr char name[] and it's done, and both sides are clean after the change. Fortunately, we also have a comparison in the same code, tag is passed to %s using tag.data(), which is the correct exit for string_view, the data pointer is its obvious member. The practice of putting the entire object into varargs bets on the first member being a pointer by chance, but chance is not equal to guarantee. When you write cross-platform skeletons, interfaces like printf must run on both sides, tag.data() must be recognized on both sides.

## Three Sources Combined into One Completion

We look back at the three fields of completion, their origins in each backend are different:

| Field | EpollBackend (e1 real test) | IocpBackend (e1 real test) | io_uring if connected (L03 real test) |
| --- | --- | --- | --- |
| id | Adapter layer's assigned number | Hidden in the extended structure and returned with the package | user_data returned as is |
| bytes | Bytes moved by the adapter layer | Filled in by the kernel in the GQCS package | CQE's res |
| error | errno of read | GetLastError, brought out with the failure package | res is negative when it's an error code |

id on the epoll side is our own流水号, on the IOCP side it is hidden in the extended structure and returned with the completion package, if we really add io_uring, it is the SQE's user_data, in L03's E1, 0xC0FFEE returns exactly as is. The three origins of bytes are also clear: moved by the adapter, filled in by the kernel, CQE's res. The error line, epoll side is filled with errno, IOCP side is filled with GetLastError, io_uring directly uses the negative value of res as the error code. The three sources combined into the same shape, concept collects this layer: differences are pressed into the backend internally, user code only recognizes completion.

Another thing we roughly carried through the entire article is: request.target's uintptr_t. The body of fd is int, and the body of HANDLE is pointer width, using an integer width to forcibly accommodate both is sufficient in the skeleton, and it is left for future work in engineering. The unified representation of handles and the unified reporting of errors, the two pieces of thinking foundation ([RAII Pattern](../thinking/01-raii-paradigm.md) and [Error Handling Pattern](../thinking/02-error-paradigm.md)) have given parts, and the integration into the main topic is the job of ch07 platform abstraction chapter. The backend layer of asynchronous I/O, this article ends here, the numbers in the matrix are in the archives of the five articles on both sides, the five lines of concept are in the main text, you can write your own backend anytime, can it accept it?

<ReferenceCard title="Reference Resources">
  <ReferenceItem
    :id="1"
    title="kqueue(2)"
    publisher="FreeBSD Manual Pages"
    url="https://man.freebsd.org/cgi/man.cgi?query=kqueue&sektion=2"
  />
  <ReferenceItem
    :id="2"
    title="Constraints and concepts"
    publisher="cppreference.com"
    url="https://en.cppreference.com/w/cpp/language/constraints.html"
  />
  <ReferenceItem
    :id="3"
    title="requires expression"
    publisher="cppreference.com"
    url="https://en.cppreference.com/w/cpp/language/requires.html"
  />
  <ReferenceItem
    :id="4"
    title="std::same_as"
    publisher="cppreference.com"
    url="https://en.cppreference.com/w/cpp/concepts/same_as"
  />
  <ReferenceItem
    :id="5"
    title="libuv"
    publisher="GitHub (libuv/libuv)"
    url="https://github.com/libuv/libuv"
  />
</ReferenceCard>
