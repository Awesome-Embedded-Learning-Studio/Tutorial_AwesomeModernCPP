---
title: "io_uring: Turning I/O Waiting into Harvesting Completion Events"
description: "Can waiting for I/O be transformed into harvesting completion events: This article tests building a ring with raw system calls (numbers 425/426/427 exposed, three mmap calls produce two anon_inode:[io_uring] mappings in /proc/self/maps, manually filling SQE, placing the tail pointer, and one enter call to retrieve user_data=0xC0FFEE), engineering practices with liburing 2.15 (64 reads submitted once, 64 system calls for read(2), io_uring is submit 1 and wait 1, the remaining 63 harvests are all user-space peek), the order and failure propagation of chained requests (read, write, fsync three CQE in order 4096/4096/0, the head of the chain fails with fd=-EBADF while downstream is -ECANCELED, the target file stops at the first 4KiB written by chain A), epoll_ctl's direct EPERM for regular files and O_NONBLOCK's ineffectiveness (asynchronous disk file support is only io_uring's READ), the honest answer on batch economics (16MiB page cache hot path read(2) 4096 calls take 2.1 to 2.8 milliseconds, io_uring 256 per batch takes 2.4 to 2.6 milliseconds, wall-clock is flat while system call count differs by 128 times), timeouts are also requests (TIMEOUT submitted in reverse order, completing according to wall-clock, LINK_TIMEOUT reaches the point and cancels the waiting read with -ECANCELED, data arrives in 100ms and read completes normally while the timeout request is withdrawn, compared to timerfd's fd in the table), SQPOLL's probe on WSL2 without privilege and the environmental boundary of kernel.io_uring_disabled"
chapter: 8
order: 3
platform: host
difficulty: advanced
cpp_standard: [20]
reading_time_minutes: 24
prerequisites:
  - "I/O Multiplexing: select, poll, and epoll's boundaries and costs"
  - "timerfd and eventfd: time and event fd-ization"
  - "Signals (Advanced): real-time signals, signalfd, and pidfd"
related:
  - "epoll: Linux I/O Multiplexing, from poll's bottleneck to interest table and ready queue"
  - "Reactor Pattern: wrapping epoll into event loop + callback, and why it's 'synchronous non-blocking'"
  - "Asynchronous I/O and Event Loop"
tags:
  - host
  - cpp-modern
  - advanced
  - 系统编程
  - POSIX
  - 异步编程
  - 实战
current_status:
  title: Being polished
  detail: This batch has just been written, and is currently being polished and refined, the expression and experimental口径 may change at any time
  variant: polishing
translation:
  source: documents/vol8-domains/systems-programming/linux/io-multiplexing/03-io-uring.md
  source_hash: 00eb1233e113c4510fe8ebecc0f337e6f601e7837e774e0c80377696213dd1a7
  translated_at: '2026-11-22T15:07:25+00:00'
  engine: qwen3:14b
  token_count: 19003
---
# io_uring: Turning I/O Waiting into Harvesting Completion Events

At the end of [L02](./02-timerfd-eventfd.md), our loop is already quite elegant: pipe, eventfd, and timerfd have all been turned into fd, hung on the same epoll table, and a single epoll_wait handles all the sending and receiving. However, if you ask a deeper question, you will find that the table only manages the waiting. epoll reports which fd is ready, but the actual work is still done by us, and read still needs to be called by us personally, and the call itself is synchronous. If the data is not ready, the kernel will hold us on read. So we push the question to the bottom: can the I/O action itself be entirely handed over to the kernel, and we only need to collect the completion notification?

This is the distinction between Reactor and Proactor, and the difference lies in a very specific place: waiting for readiness or waiting for completion. Reactor (reactor) waits for readiness, epoll_wait wakes up, and the reading is still done by you personally. Proactor (completer) waits for completion: you hand over the read request along with the buffer address, and the kernel puts the completion record in a place when it is done, and you go to harvest it. The engineering course on the network's [Reactor section](../../../networking/03-reactor-pattern.md) and vol5's [Asynchronous I/O and Event Loop](../../../../vol5-concurrency/cookbook/b-topics/06-async-io-event-loop.md) have already covered this, we only compare the behavior here, and the pattern course is not repeated. The engineering course for the socket scenario is still left in the network's own io_uring, and this article's file and pipe scenarios each manage a part.

Linux's answer is called io_uring, which entered the mainline with 5.1 in May 2019, and the author is Jens Axboe, the maintainer of the block layer. The ring in the name is literal: two shared ring buffers between user space and the kernel, one is the submission queue (SQ, submission queue), and the other is the completion queue (CQ, completion queue). The items in the queue also have their own names, the submission side is called SQE (submission queue entry), meaning a submission request, and the completion side is called CQE (completion queue entry), meaning a completion record. In fact, before this, Linux had tried two asynchronous paths. POSIX AIO is implemented with a thread pool in glibc, but the request does not really enter the kernel's asynchronous path. Native AIO (io_setup and io_submit) did enter the kernel, but it was temperamental, and without O_DIRECT (read/write bypassing page cache and directly to disk), it could not achieve asynchronous effects, and few people used it, even fewer survived. Let's look at io_uring's approach, which is different from both: it no longer provides a separate call for each operation to enter the kernel, but instead makes the submission and completion themselves shared memory, and waiting for I/O becomes harvesting.

The experiment is numbered E1 to E6, and corresponds one-to-one with the six source codes u1 to u6 in the warehouse archive `code/volumn_codes/vol8/systems-programming/linux/io-multiplexing/03-io-uring/`. The code and all the original outputs are included in the archive, and you can check them at any time. The output blocks in the main text are all full citations, and we have not changed a single word. The deleted parts in the code excerpts are marked with `...`. E in this article only refers to this article: [L01](./01-select-poll-epoll.md) and [L02](./02-timerfd-eventfd.md) each have their own E series, and they are unrelated even if they have the same number. When you look up the archive, just recognize the prefix of the filename: e is the first article, t is the second article, and we use u here. There are also two small source codes for probes in the same directory, their names are probe_uring.c and liburing_hello.cpp, they are not counted in the u numbering, and the first two outputs come from them.

As usual, the environment's口径 is clearly stated, and the numbers after are all matched with it: the experiments are from the author's desktop, the CPU is AMD Ryzen 7 9700X, the system is WSL2 environment, the kernel is 6.18.33.2-microsoft-standard-WSL2 build, g++ is 16.2.1, glibc version is 2.44, the compilation口径 is all `-std=c++20 -O2 -Wall -Wextra` plus `pkg-config --cflags --libs liburing`, the number of warnings is zero. The clock used for timing is always CLOCK_MONOTONIC, the timing experiments each ran a round, the numbers fluctuated but the structure was consistent. The data files are created by the experimental program in the directory under `~/ch04_scratch/`, the path is hard-coded in the top of the source code, you need to create this directory before rerunning. All `.out` are from the same round on 2026-10-04, the mmap address and millisecond-level time change every rerun, we refer to the byte count and order, not any specific address.

## Two Probes Before Getting Started

io_uring is a relatively young facility, and other machines, containers, and sandboxes may not allow us to use it, so we put two probes at the front, and if the environment fails the probe, nothing else can be discussed. The first one is without any library, purely using raw system calls to ask the kernel:

```text
kernel check: io_uring_setup syscall number = 425
io_uring_setup(8, params) = 3  -> OK, ring fd = 3
sq_entries=8 cq_entries=16 flags=0x0 sq_thread_cpu=0 sq_thread_idle=0 features=0x3ffff wq_fd=0
features bits: NODROP=1 SUBMIT_STABLE=1 RW_CUR_POS=1 CUR_PERSONALITY=1 SINGLE_MMAP=1
```

The number 425 is the actual number printed in the local `<sys/syscall.h>`, on x86-64, io_uring uses three: setup is 425, enter is 426, register is 427. You will recognize them if you look familiar, they are in the same lineage as 434 (pidfd_open), 424 (pidfd_send_signal), and 438 (pidfd_getfd) in the number table at the end of the [signal advanced section](../process/05-signal-advanced.md), all are new system calls added in recent years, and glibc has not provided packaging for the three numbers of io_uring to this day, we can only use raw syscalls and liburing if we want to call them. Of the three numbers, setup and enter will be used immediately, register (427) is not used in this article: it registers a set of buffer addresses or file descriptors in advance into the kernel's table, and then SQE no longer contains raw fd, but the index in the registration table, the kernel saves the fd lookup for each request, and it is only used for large-scale hot paths, you will encounter it again in high-traffic services.

The probe also brought back two pieces of information. `features=0x3ffff` is the full bit set in the low 18 bits, we have named NODROP, SUBMIT_STABLE, RW_CUR_POS, CUR_PERSONALITY, SINGLE_MMAP, five of which are set, E1 will name one of them, SINGLE_MMAP. NODROP manages the behavior when the completion queue is full, with it the kernel prefers not to return rather than drop completion events. When requesting 8 entries, the kernel gives sq=8, cq=16, the completion queue is set to twice the capacity of the submission queue by default, leaving room for harvesting. The second probe asks if liburing is present:

```text
liburing version: 2.15
submitted=1 wait=0 cqe res=0 user_data=42
```

The answer is yes, the version reported is 2.15, a NOP goes through the entire chain from building the ring to waiting. So we take a dual track: E1 does not use the library, clearly laying out the mechanism on the surface, and E2 onwards switches to liburing to talk about engineering practices. The reason for using raw syscalls is the same as the explanation in the signal advanced section for `extern "C"`: the mechanism course needs to see the parts, and the library's encapsulation is only considered after the parts are clear.

## E1: Without liburing, Build the Ring Yourself

liburing is indeed convenient, but it hides the shape of the ring. In this article, we don't bring it, only bring the kernel's own uapi header `<linux/io_uring.h>`, and personally go through the process of building the ring, submitting, and harvesting. We start with the first step, setup, and after getting the ring fd, the params structure is filled in by the kernel with a bunch of offsets, and the three mmap calls rely on them:

```cpp
// u1_bare_ring.cpp(节选)
io_uring_params p{};
int fd = (int)syscall(__NR_io_uring_setup, 4, &p);
...
size_t sq_sz = p.sq_off.array + p.sq_entries * sizeof(unsigned);
size_t cq_sz = p.cq_off.cqes + p.cq_entries * sizeof(io_uring_cqe);
void* sq_map = mmap(nullptr, sq_sz, PROT_READ | PROT_WRITE,
                    MAP_SHARED | MAP_POPULATE, fd, IORING_OFF_SQ_RING);
void* cq_map = mmap(nullptr, cq_sz, PROT_READ | PROT_WRITE,
                    MAP_SHARED | MAP_POPULATE, fd, IORING_OFF_CQ_RING);
void* sqe_map = mmap(nullptr, p.sq_entries * sizeof(io_uring_sqe), PROT_READ | PROT_WRITE,
                     MAP_SHARED | MAP_POPULATE, fd, IORING_OFF_SQES);
```

We look at what each of the three mappings is responsible for: the SQ ring holds metadata such as head, tail, and array indices, the CQ ring holds metadata for the completion queue, and the SQE array holds the actual requests. The flags in the mmap include MAP_POPULATE, which fills the page table when the mapping is established, avoiding a page fault when the memory is first accessed. For the ring, this is not an essential thing, shared pages will eventually be accessed, but it's just to avoid one cold start pause. The full output when running is like this:

```text
syscall 编号: __NR_io_uring_setup=425 __NR_io_uring_enter=426 __NR_io_uring_register=427
io_uring_setup(4) = 3, sq_entries=4 cq_entries=8 features=0x3ffff
features: SINGLE_MMAP=1 NODROP=1 SUBMIT_STABLE=1
mmap: SQ 环 208 B @ 0x78d9f1498000, CQ 环 192 B @ 0x78d9f1497000, SQE 数组 256 B @ 0x78d9f1496000
SINGLE_MMAP 置位: SQ/CQ 在同一块区域, 允许一次 mmap 同拿两环
(这里按经典三段式分开映射, 地址自然不同: 0x78d9f1498000 / 0x78d9f1497000; 特性省的是映射次数)
/proc/self/maps 里能看到这两段映射:
  78d9f1496000-78d9f1497000 rw-s 10000000 00:10 6114131                    anon_inode:[io_uring]
  78d9f1498000-78d9f1499000 rw-s 00000000 00:10 6114131                    anon_inode:[io_uring]
环参数: sq_mask=3 cq_mask=7 (容量-1), SQE=64 B, CQE=16 B

塞入 1 个 NOP (user_data=0xC0FFEE), SQ tail 0 -> 1
io_uring_enter(submit=1, wait=1, GETEVENTS) = 1
CQ: head=0 tail=1, 就绪 1 个
  CQE: user_data=0xc0ffee res=0 flags=0x0
收完, CQ head=1 tail=1。提交走 SQ 尾指针, 完成走 CQ: 两个环都是与内核共享的内存。
```

We take entries=4 and go through the three sizes. The SQ ring takes up 208 bytes, which contains metadata plus 4 array indices, the CQ ring takes up 192 bytes, which contains metadata plus 8 CQEs, and the SQE array takes up 256 bytes, just enough to hold 4 SQEs of 64 bytes each. The two `anon_inode:[io_uring]` mappings you see in `/proc/self/maps` are their real identities: the first mapping is at file offset 0x10000000, which holds the SQE array, the other is at offset 0, which holds the SQ ring, and the CQ ring's offset 0x8000000 is not in this dump range. You can also see SINGLE_MMAP=1: the kernel means that the SQ and CQ rings can be mapped into the same area with a single mmap, but the experiment prefers the classic three-segment mapping, so the addresses are naturally different. This feature only saves the number of mappings, and we can still build the ring without it.

There is another detail in the way of taking indices, we continue to look. The positioning uses tail & mask, the mask value is capacity minus one, for 4 items, the mask is 3, when tail increases to 4, 5, 6, the positioning returns to 0, 1, 2, and the ring turns around. The SQ ring actually holds the indices, the real SQE is in the array, and the kernel retrieves the real SQE in the array according to the index.

The four parameters of enter are also worth our recognition: the ring's fd and the number of SQEs to submit, the number of completions to wait for and flags. In E1, we pass (1, 1, GETEVENTS), a single call packages both submission and waiting. On our regular ring, without GETEVENTS, the third parameter is not looked at by the kernel, and it returns immediately after submission, and the waiting for completion needs to enter the kernel again (the ring with IOPOLL is an exception).

After the ring is built, we submit a request in four steps: read the tail pointer, fill the SQE, fill the array index, and place the tail pointer. All four steps are done in user space, and only the last enter is actually entered into the kernel, and enter itself is a system call:

```cpp
// u1_bare_ring.cpp(节选,续)
unsigned tail = READ_ONCE(*sq_tail_p);
io_uring_sqe* sqe = &sqes[tail & sq_mask];
std::memset(sqe, 0, sizeof(*sqe));
sqe->opcode = IORING_OP_NOP;
sqe->user_data = 0xC0FFEE;
sq_array[tail & sq_mask] = tail & sq_mask;
WRITE_ONCE(*sq_tail_p, tail + 1);                 // 尾指针一放, 内核就看得见了
...
long r = syscall(__NR_io_uring_enter, fd, 1, 1, IORING_ENTER_GETEVENTS, nullptr);
```

We read the tail pointer, fill the SQE, fill the array index, and place the tail pointer, and the four steps are done. `WRITE_ONCE` expands to an atomic write with release semantics, ensuring that the content of the SQE is fully in place before the tail pointer takes effect. The kernel reads the tail pointer with acquire semantics, and gets the complete request. From placing the tail pointer to enter, we didn't spend a single system call, the request quietly lies in the shared memory until `io_uring_enter` enters the kernel and says: submit 1, and also wait for 1 completion. On the harvesting side, the kernel advances the CQ's tail, and the head is left for the user to advance, so after the experiment, head=tail=1, this is the handshake method of the ring.

There is one field worth getting familiar with now: `user_data` is 64 bits wide, the content is up to you to fill, and CQE will bring it back as is. In E1, we filled 0xC0FFEE as a trademark, and it came back exactly as it was in the output. It is the credential for batch requests: 100 reads are entered into the ring, and the completions come back out of order, who is who is all determined by it, and E2 will use it immediately.

What a read request's SQE looks like, we can piece it together by looking at the fields: opcode is filled with IORING_OP_READ, the remaining fd, addr, len, off four fields, respectively, hold the target file, buffer address, length, and offset. In E2, prep_read is folded into this set of fields, you can look back and have a sense.

There is also an engineering explanation. The experimental code is written with raw syscalls and liburing, ring fd, pipe fd, and file fd are all raw ints in the excerpts, in the engineering, their destination is [RAII section](../../thinking/01-raii-paradigm.md) unique_fd and [error handling section](../../thinking/02-error-paradigm.md) expected, we only reference, not re-open the course in this article.

## E2: Start with liburing, Submit 64 Reads at Once

Raw syscalls are the mechanism course, and in engineering, no one manually fills SQE anymore. liburing's three-piece `get_sqe`, `prep_*`, and `submit` are a thin layer wrapped around the ring, the four steps in E1 are folded into small functions like `io_uring_prep_read` and `io_uring_submit`. It's not really another runtime library, the encapsulation is the ring itself, and if something goes wrong, you can still use E1's knowledge to flip the ring out and see. We go straight to batch: a 256KiB data file is cut into 64 pieces, each 4KiB filled with verifiable patterns, we submit 64 reads at once:

```cpp
// u2_batch_read.cpp(节选)
static unsigned char bufs[kBlocks][kBlockSz];
for (int i = 0; i < kBlocks; ++i) {
    io_uring_sqe* sqe = io_uring_get_sqe(&ring);
    io_uring_prep_read(sqe, fd, bufs[i], kBlockSz, (long long)i * kBlockSz);
    io_uring_sqe_set_data64(sqe, i);              // 块号当凭证
}
int submitted = io_uring_submit(&ring);           // 一次 submit, 64 个请求全部进环
...
while (reaped < kBlocks) {
    io_uring_cqe* cqe = nullptr;
    int r;
    if (reaped == 0) {
        // 批量等待: 这次调用会真正进内核等 64 个完成
        r = io_uring_wait_cqes(&ring, &cqe, kBlocks, nullptr, nullptr);
    } else {
        r = io_uring_peek_cqe(&ring, &cqe);       // 后续直接从共享环里拿, 不进内核
    }
    ...
}
```

There is one place where it is different from read(2), you can see the prep parameters and understand: each read has its own offset, equivalent to the usage of pread, and does not go through the file's position. 64 reads are made on the same fd, each reading its own offset. The full output is here:

```text
一次 io_uring_submit 提交了 64 个读请求 (各 4 KiB, 各自带偏移)
收割 64 个 CQE (wait 真等 1 次, 其余 63 次是用户态 peek), 校验全部通过 (块号-偏移-内容对上), 共 262144 字节
系统调用次数对照: read(2) 逐块 = 64 次; io_uring = submit 1 次 + wait 1 次 + peek 不进内核
```

We put the numbers together to see. The same job, read(2) uses 64 system calls, io_uring uses submit once plus wait once, the number of times entering the kernel drops from 64 to 2. `io_uring_wait_cqes` with a quota of 64, this call will actually enter the kernel to wait for 64 completions, and the remaining 63 harvests are all done through peek, which is direct reading of the shared CQ ring in user space, without the kernel's share. The verification is all passed: the block number, offset, and content are all matched, the res of CQE is 4096, and the completion event brings the actual number of bytes.

The reason why peek does not enter the kernel is clear when we return to the ring: the completion event is written into the shared CQ ring by the kernel, `io_uring_peek_cqe` only does reading of user space memory, and `io_uring_cqe_seen` only does advancing the head by one, leaving the position. The real waiting only happens when the ring is empty: one wait brings a batch of completions, and the subsequent harvests are all pure user-space actions, and the batch thinning is just like that.

In [L01](./01-select-poll-epoll.md), select, poll, and epoll report readiness, you read when it's ready. io_uring's return is completion, it gives you the result after reading. These two sentences sound like nitpicking in the socket scenario, but for regular files, the difference becomes whether there is a path to go, and E4 will specifically verify this segment.

## E3: Chained Requests, Ordered into the Kernel, Failures Propagate Downstream

You can only write after you read, and only fsync after you write, for I/O with dependencies, the user-space approach is to wait for the first completion and then submit the next, and we have to keep an eye on the back and forth. io_uring provides another way: adjacent requests in the same submission are linked with `IOSQE_IO_LINK`, and the order is guaranteed by the kernel. The experiment ran two chains, chain A is read followed by write followed by fsync:

```cpp
// u3_chained.cpp(节选):链A
io_uring_sqe* s1 = io_uring_get_sqe(&ring);
io_uring_prep_read(s1, sfd, buf, 4096, 0);
io_uring_sqe_set_data64(s1, 1);
s1->flags |= IOSQE_IO_LINK;                       // 与下一个绑成链

io_uring_sqe* s2 = io_uring_get_sqe(&ring);
io_uring_prep_write(s2, dfd, buf, 4096, 0);
io_uring_sqe_set_data64(s2, 2);
s2->flags |= IOSQE_IO_LINK;

io_uring_sqe* s3 = io_uring_get_sqe(&ring);
io_uring_prep_fsync(s3, dfd, 0);
io_uring_sqe_set_data64(s3, 3);                   // 链尾不用再挂 LINK
int n = io_uring_submit(&ring);
```

We change the first read's fd in chain B to -1, specifically waiting for it to fail. Full output:

```text
链A 一次提交 3 个 (read->write->fsync 挂链)
  链A 的三个完成, 顺序与提交一致:
  操作1 res=4096 (成功)
  操作2 res=4096 (成功)
  操作3 res=0 (成功)

链B 一次提交 2 个 (read(bad fd)->write)
  链B 的两个完成: 失败向下游传播成取消:
  操作1 res=-9 (坏的 fd)
  操作2 res=-125 (链断了, 被取消)

链A 的 write 确实生效: 前 16 字节 = 00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f ...
链B 的 write 没有执行: 目标文件偏移 4096 处仍是 0 (只写了前 4KiB)
```

We look at chain A's three CQEs, which complete in the order of submission, res reports 4096, 4096, 0, write reads the 4KiB that read put into buf, the first 16 bytes 00 to 0f pattern matches, fsync also ends with res=0. The entire sequence guarantee is on the kernel side, user space has no line of sorting code, read and write share the same buf and there is no race condition problem, because the chain ensures that write starts running when read has completed.

Chain B is the main event of this session. The head of the chain's read gets res=-9, which corresponds to -EBADF, and the downstream write immediately gets res=-125, which is -ECANCELED. The execution of write never happened, the target file stopped at the first 4KiB left by chain A, and the 4096 offset section that chain B was supposed to write never happened. The failure propagation rule is written into the chain like this: any failure in one link cancels the entire subsequent segment, and each CQE reports -ECANCELED in sequence, leaving no intermediate state for you to clean up. If you've ever written manually linked asynchronous callbacks, you probably remember that each level has to check whether the previous one succeeded, but here the judgment and skipping are all handled by the kernel.

We顺便 recognize two related flags. Requests with the IOSQE_IO_DRAIN flag wait for all in-flight requests to complete before starting, and the IOSQE_IO_HARDLINK linked chain is also tight, but if the previous link fails, the next one still runs, and the failure does not propagate. Chain A's use of HARDLINK is also valid, but in the case of chain B's once read failure, the subsequent write will run in vain.

## E4: Regular Files, Non-blocking Doesn't Work, epoll Rejects

The sentence left in the body of [File I/O's first article](../file-io/01-posix-file-io.md) and [the previous one](./01-select-poll-epoll.md) says that epoll cannot manage regular files. Two versions of the story have been heard by the author: one says it is always ready, the other says it is silently ignored. Before the experiment, the author bet on the former, thinking that ADD would always be successful, and wait would report endlessly, but the first step was rejected. We split the actual test into two halves, starting with the O_NONBLOCK half:

```text
[普通文件 fd=3]
fcntl F_SETFL O_NONBLOCK = 0 (这个标志本身设得上去)
O_NONBLOCK 下 read = 4096 (有数据直接给, 不会拿 EAGAIN 说"现在没有")

epoll_ctl ADD 普通文件 = -1 errno=1 (Operation not permitted)
man 2 epoll_ctl 对 EPERM 的解释: 目标 fd 不支持 epoll (内核里没有 poll 支持的文件类型)

[对照: 空管道 + O_NONBLOCK] read = -1 errno=11 (Resource temporarily unavailable)
空管道 epoll_wait(0) 返回 0, 管道在其中报就绪: 否

结论: 普通文件两头都关死 -- 非阻塞语义不生效(read 永不 EAGAIN), epoll 直接拒收(EPERM);
对磁盘文件做统一的异步, 只能走 io_uring 的 READ (完成事件带真实字节数, 见 E2)。
```

We look at each part one by one. fcntl sets O_NONBLOCK on, returns 0, the flag itself is set, but the semantics are ineffective: on regular files, read gets 4096, gives data directly when available, and never gets EAGAIN. The control group's empty pipe is the familiar script: read reports -1 plus errno=11, and epoll_wait is also obedient, the empty pipe is not on the ready list. The next item is epoll_ctl ADD regular file, returns -1 plus errno=1, reporting EPERM. man 2 epoll_ctl's explanation for this error code is only one sentence, saying that the target fd does not support epoll. The mechanism in the kernel for this part is our added note: these files have no poll implementation, so the registration step naturally fails.

Why is it designed to reject? Regular files have no waiting. Data is either in the page cache or the kernel will get it for you from the disk, read is always able to complete, and it is always ready, the readiness notification has no information for it. Instead of letting every path report an always-true readiness, the kernel chose to say no at the registration step. Thus, regular files are closed on both ends: the non-blocking path is not recognized by read, and the readiness notification path is not accepted by epoll. For disk files to have a unified asynchronous, the only remaining path is io_uring's READ, which has been experienced in E2, and the completion event still brings the actual number of bytes. And why does READ work? Because it only needs completion, not readiness. After the request is submitted, the part of the page cache hit is completed by the kernel immediately, and the part that cannot be completed is handed over to the kernel's internal worker threads to wait, and completed as usual back to CQ, and we see only the completion event from the ring.

## E5: Where Does the Savings Come From, Wall-clock Doesn't Win on Page Cache Hot Path

By now, you may have already given io_uring a credit in your mind. So let's put it on the timing stage, and the scenario is one that is very favorable to it: a 16MiB file, 4KiB a piece, a total of 4096 pieces, we read the entire file once, feed the page cache with hot data, and then let read(2)'s piece-by-piece call and io_uring's 256 per batch face off, the control side's actual test uses pread with offset, exactly matching io_uring's offset read, each side takes the median of 5 rounds, and then runs a full round again:

```text
16 MiB (4096 x 4KiB), 页缓存已预热, 5 轮取中位:
read(2) 逐块 :      2.1 ms (4096 次系统调用)
io_uring 256 一批:      2.4 ms (submit 调用 16 次 + wait 调用 16 次, 其余收割是用户态 peek)
每块均摊: read 509 ns, io_uring 591 ns

SQPOLL 探针: 建环成功, submit=1 (只写共享环, 不催内核)
SQPOLL: 内核线程 已把 NOP 干完 (res=0)
```

Our full round:

```text
16 MiB (4096 x 4KiB), 页缓存已预热, 5 轮取中位:
read(2) 逐块 :      2.8 ms (4096 次系统调用)
io_uring 256 一批:      2.6 ms (submit 调用 16 次 + wait 调用 16 次, 其余收割是用户态 peek)
每块均摊: read 692 ns, io_uring 636 ns

SQPOLL 探针: 建环成功, submit=1 (只写共享环, 不催内核)
SQPOLL: 内核线程 已把 NOP 干完 (res=0)
```

Putting the two rounds side by side, the first round read(2) wins 2.1 to 2.4, and the rerun switches to io_uring winning 2.6 to 2.8. The numbers are evenly matched, the differences are within the noise range, and our honest口径 is just one word: flat. The wall-clock is flat, but the number of system calls is a real 4096 to 32, a difference of 128 times. The source of 32 is also on the stage: 4096 pieces cut into 256 batches is 16 batches, each batch takes one submit and one wait, multiplied together is exactly 32. Why so many calls are saved, but the wall-clock remains unchanged? When the page cache hits, the data is already safely in memory, the read path only has the system call's entry and exit and one copy, the average per piece is 509 to 692 nanoseconds. The 128 times saved is the fixed threshold of each entry into the kernel, and this threshold is already cheap on our machine, the saved part is eaten back by filling SQE and harvesting CQE.

So the statement that io_uring is faster needs to be limited to scenarios. On the page cache's hot path, it wins in the number of system calls and the asynchronous completion path, but the wall-clock is not won. The wall-clock victory has to be found on slow devices: when data is on the disk, on the network, or in transit, the CPU also has free hands to do other things, and the batch thinning is truly realized, but the fully hit E5 scenario is not measurable.

The end of the output also carries a probe: SQPOLL. When building the ring with `IORING_SETUP_SQPOLL`, the kernel will start a dedicated thread to guard the SQ ring for us, the submitted data goes through the shared ring, saving the trip to the kernel each time, the NOP in the probe is done by the kernel thread itself. However, the statement "not even need to enter" needs to be narrowed: when the kernel thread is asleep, the SQ ring's flags will raise the NEED_WAKEUP flag, at this moment, liburing's submit will replace us to send an enter (with the IORING_ENTER_SQ_WAKEUP flag) to wake the thread up, the data transfer still only relies on the shared ring. The note in parentheses in the output says "only write the shared ring, don't prompt the kernel", that is the hardcoded口径 in the program, you can see that wake-up enter with strace, we narrow the口径 according to the actual situation. When the kernel thread is idle, it will sleep for sq_thread_idle milliseconds, but it is always polling while it is alive, the essence is to trade CPU for submission latency, which is only worth it in low-latency scenarios, like this article's occasional single job program is not worth it. SQPOLL's treatment has a historical threshold: kernels before 5.12 require CAP_SYS_NICE or CAP_SYS_ADMIN, from 5.13 onwards it is completely free of privileges, on this machine, 6.18 can build without privileges. We also explain the other end of the boundary, from 6.6 onwards, the kernel added `kernel.io_uring_disabled` this sysctl, three levels are fully open 0, only privileged 1, fully closed 2, some distributions set 2 by default at factory, container and sandbox strategies also often block the three numbers. When you can't run it in your own environment, remember to look at this knob, the experimental code will truthfully print out the errno when building the ring, that's not an experimental failure, but the environment is speaking.

## E6: Timeout is also a Request, TIMEOUT and LINK_TIMEOUT

[L02](./02-timerfd-eventfd.md) turns the timer into a timerfd, the fd enters the poll table, and is on equal footing with other events. io_uring's answer is more thorough: timeout itself becomes a request, enters the ring, its completion is a CQE, and even saves the trouble of occupying an fd. We split the experiment into three parts, part A submits two independent TIMEOUTs in reverse order, 350 milliseconds in front, 100 milliseconds behind:

```text
甲: 反序提交两个 TIMEOUT (350ms 先提交, 100ms 后提交):
  t+ 100 ms 收到 100ms 的完成 (res=-62, 到点)
  t+ 350 ms 收到 350ms 的完成 (res=-62, 到点)

乙: read(空管道) 挂 LINK_TIMEOUT 300ms, 没人写数据:
  t+ 650 ms 操作2 res=-62 (到点)
  t+ 650 ms 操作1 res=-125 (被超时取消)

丙: 同样的组合, 100ms 时写入 5 字节 (预算 500ms):
  t+ 750 ms 操作1 res=5 (读到了)
  t+ 750 ms 操作2 res=-125 (被撤掉)
```

In part A, the order of completion is reversed from the submission order, following the wall-clock: t+100 receives the 100 millisecond one, t+350 receives the 350 millisecond one, res gives -62, which is our -ETIME. You can connect the four moments of 100, 350, 650, 750 together, and see that expiration and completion both follow the wall-clock, the submission order does not participate in sorting. The third parameter of prep_timeout is the count, we fill 0 for pure time-based timeout, and if we fill a positive number, it changes its behavior, and counts as expired when enough completions are waited for.

Part B is the killer application of timeout, we pair an unresponsive read with `IORING_OP_LINK_TIMEOUT`, two set_data64 fill in 1 and 2, the "operation 1, operation 2" in the output is their return with user_data:

```cpp
// u6_timeout_requests.cpp(节选):乙
io_uring_sqe* s1 = io_uring_get_sqe(&ring);
unsigned char buf[16];
io_uring_prep_read(s1, pp[0], buf, sizeof buf, 0);
io_uring_sqe_set_data64(s1, 1);
s1->flags |= IOSQE_IO_LINK;
io_uring_sqe* s2 = io_uring_get_sqe(&ring);
__kernel_timespec ts{0, 300000000};
io_uring_prep_link_timeout(s2, &ts, 0);
io_uring_sqe_set_data64(s2, 2);
io_uring_submit(&ring);
```

The pipe is empty, the read is stuck there, at 300 milliseconds, operation 2 reports -62, at the same time, operation 1's read is also canceled, it is hit with -125's -ECANCELED. You can check the timeline: part B is submitted after part A's two timeouts are completed, t+650 is exactly the sum of 350 and 300. Part C changes the script, the same combination budget is relaxed to 500 milliseconds, at 100 milliseconds, 5 bytes are written into the pipe: read completes normally, res is 5, and the timeout request is withdrawn, leaving the field with -125. On both sides, one completes normally, the other is automatically canceled, both CQEs arrive, and we are not left with orphan requests in the ring.

We compare the timeout request approach with timerfd, and the difference is clear. On the timerfd side, it is a fd that enters the table, expiration is a readable event on the table, we still have to read it ourselves to get the count. On this side, it is a request that enters the ring, expiration reports a completion event, and it has no difference in status from a read completion or a fsync completion, and the harvest is all through the same CQ. If the server experiment for graceful shutdown in the [signal advanced section](../process/05-signal-advanced.md) is moved to the ring, shutting down the timeout does not need a separate table, just attach an SQE when submitting each time.

## What the Other Side Sees

On the Windows side, asynchronous I/O has always been the completion notification approach since the NT era: the OVERLAPPED structure is submitted with the read request, and the completion is harvested by the I/O completion port (IOCP). If you look back at the output of E1 and think about IOCP's completion queue, you will find that the behavior on both sides is the same, Proactor is the native tradition of Windows. The two articles on Windows-side asynchronous I/O are already on the disk, the main course on IOCP is in [that article](../../windows/async-io/02-iocp.md). The item-by-item comparison between the two rings and IOCP is handed over to the cross-platform article on the other side, we will collect it into the same matrix there.

<ReferenceCard title="Reference Resources">
  <ReferenceItem
    :id="1"
    title="io_uring_setup(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/io_uring_setup.2.html"
  />
  <ReferenceItem
    :id="2"
    title="io_uring_enter(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/io_uring_enter.2.html"
  />
  <ReferenceItem
    :id="3"
    title="io_uring(7)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man7/io_uring.7.html"
  />
  <ReferenceItem
    :id="4"
    author="Jens Axboe"
    title="liburing"
    publisher="GitHub (axboe/liburing)"
    url="https://github.com/axboe/liburing"
  />
  <ReferenceItem
    :id="5"
    title="Documentation for /proc/sys/kernel"
    publisher="The Linux Kernel documentation"
    url="https://docs.kernel.org/admin-guide/sysctl/kernel.html"
  />
  <ReferenceItem
    :id="6"
    author="Michael Kerrisk"
    title="The Linux Programming Interface"
    publisher="No Starch Press"
    :year="2010"
    url="https://man7.org/tlpi/"
  />
</ReferenceCard>
