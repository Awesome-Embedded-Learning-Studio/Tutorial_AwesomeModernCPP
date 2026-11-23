---
title: "Shared Memory: shm_open and Mapping"
description: "For two unrelated processes to share the same memory, the mechanism is shm_open erecting a named object in /dev/shm, with the same physical pages mapped into two address spaces. This article measures the named object's full lifecycle (a new object has size 0 and needs ftruncate; a second create before unlink eats EEXIST while rebuilding the same name after unlink goes through; the old mapping still reads 0x1111 while the new entity writes 0x2222, invisible to each other; umask trims permissions; root's 0600 blocks user while CAP_DAC_OVERRIDE grants the reverse direction). A counting race losing 9.62%~23.70% over three runs, plus the assembly showing addq without the lock prefix, establishes that seeing the memory is not the same as using it right; PTHREAD_PROCESS_SHARED mutex at 44.7 ms and process-shared semaphore at 58.1 ms provide the control-group fix. The flagship experiment is an SPSC lock-free ring queue in shm: one million messages with zero loss and zero reordering, where sched_yield backpressure turns out over sixty percent faster than CPU spinning, attributed to cache line ping-pong, with a pinned-cores control ruling out same-core scheduling. A 1 MiB haul runs 7304 versus 2349 MiB/s, about three times a pipe; memfd_create leaves no name and vanishes on close. The ending previews Windows' page-file-backed named mapping"
chapter: 8
order: 3
platform: host
difficulty: advanced
cpp_standard: [20]
reading_time_minutes: 39
prerequisites:
  - "mmap 内存映射:把文件贴进地址空间"
  - "OS 资源的 RAII 范式:fd、HANDLE 与映射的同一副骨架"
related:
  - "文件映射:CreateFileMapping 与 MapViewOfFile"
  - "内存序详解"
  - "无锁编程基础"
tags:
  - host
  - cpp-modern
  - advanced
  - 系统编程
  - POSIX
  - 并发
  - atomic
  - 内存管理
  - 优化
current_status:
  title: 正在打磨
  detail: 本批刚写完,正在通读打磨,表述与实测口径可能随时调整
  variant: polishing
translation:
  source: documents/vol8-domains/systems-programming/linux/memory/03-shm.md
  source_hash: 830f2f39bf7262ef2ba964cf8c6e7083f109417320f5fef8d81cff617b3ac716
  translated_at: '2026-11-22T16:10:53+00:00'
  engine: glm-5.3
  token_count: 21124
---
# Shared Memory: shm_open and Mapping

In the first two memory management articles we kept tidying our own process's address space: the layout article led us to identify text, heap, and stack in `/proc/self/maps`, and the virtual memory API article put mprotect and madvise through their paces. Yet skilled as the craft has become, we have never set up a stretch of memory co-written by two processes beyond our own address space (the previous article's closing process_vm_readv counts only as reconnaissance from behind a system call, one peek from a distance). This article steps across: two processes with no kinship whatsoever will share the same physical memory. At the mechanism level, the kernel maps the same set of physical pages into both processes' address spaces simultaneously; a byte process A writes, process B reads through a pointer, with no kernel copying in between and no system call in the path.

We met half of the mutual-recognition problem in the [mmap memory mapping](../file-io/02-mmap-memory-mapping.md) article (by the series' abbreviation convention, we call it L02 below): writes to MAP_SHARED land directly in the page cache, the forked child opens and mmaps the file on its own, and the X it writes, the parent immediately reads in its own mapping. But that pair is parent and child, and the sharing medium is a file that really exists. If server and client are independently started processes that do not even know each other's names, on what grounds can we expect them to find the same memory? Our answer is to give the memory a name: shm_open registers, in a place every process can see, an object that can be opened by name. A shared memory object with a name, we call a named object in what follows.

The name solves recognition; the real trouble is only beginning. When two processes read and write the same memory, nobody is in charge of ordering: all the kernel does is map the pages in; it does not know the integer you wrote is a queue's head, much less which of your bytes should land after which. The speed of shared memory is precisely the absence of a kernel standing guard in the middle, and its price is that the synchronization work falls entirely back into our own hands. This article walks the whole chain: the life of a named object, races in measurement, two sharing routes, a ring queue without a lock, then a throughput showdown against pipe, and the last stop is memfd_create, which leaves no name.

We state the experiment environment clearly, since every number below must be checked against it. All experiments ran on the author's WSL2: the same kernel build 6.18.33.2-microsoft-standard-WSL2, an AMD Ryzen 7 9700X showing 16 logical cores from WSL2's viewpoint, g++ 16.2.1, uniformly compiled with `g++ -std=c++20 -Wall -Wextra -O2`, with the race and queue experiments adding `-pthread`, and glibc 2.44. The code and all the raw outputs are kept in the repository directory `code/volumn_codes/vol8/systems-programming/linux/memory/03-shm/`; the README also carries every re-run command. The output blocks pasted in the text are excerpts; the archive's original text is authoritative. Experiments are numbered E1 through E6, following the archive directories. The `unique_fd`, `mapped_region`, and `sys_call` defined in the two thinking-foundation articles (the [RAII article](../../thinking/01-raii-paradigm.md) and the [error handling article](../../thinking/02-error-paradigm.md)) again go unused: our experiments stick to the bare shape of the system calls, all raw calls.

## Named objects: giving memory a name

shm_open's prototype is more compact than we might imagine; it wants the header `<sys/mman.h>`, oflag's values come from `<fcntl.h>`, and mode's type comes from `<sys/stat.h>`. man 3 shm_open prescribes one portable form for the name: it starts with a single slash, the characters after it may contain no other slash, and the whole name string, slash included, stays within the NAME_MAX (255) character limit. What it hands back is an fd, not a pointer, so the whole craft of open carries straight over; that is exactly what makes this POSIX interface likeable. The old-timer System V does the same job too; shmget's id-plus-key scheme is a school of its own, and we do not get involved: this article walks the POSIX file route only.

What stands behind this fd? The man page says Linux places shared memory objects into a dedicated tmpfs filesystem, usually mounted under the name `/dev/shm`. tmpfs is a filesystem whose content lives entirely in memory; L02's Dirty experiment already noted that `/tmp` is its territory. Since the object is an entity lying in a filesystem, we can see it with `ls`, and that is exactly what E1's opening move aims at:

```cpp
// e1_lifecycle.cpp(节选):shell() 负责打印并执行 ls -l /dev/shm,fstat 读尺寸
int fd = shm_open("/lm03_demo", O_RDWR | O_CREAT | O_EXCL, 0600);  // 建立命名对象
struct stat st {};
fstat(fd, &st);         // size=0:新对象的尺寸是 0
ftruncate(fd, 65536);   // 定尺寸,尺寸 0 的对象直接 mmap 会吃 EINVAL
void* p = mmap(nullptr, 65536, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
auto* gen1 = static_cast<unsigned long long*>(p);
gen1[0] = 0x1111ULL;    // 写入第一代标记
```

```text
$ ./e1_lifecycle
$ ls -l /dev/shm
total 0
[1] shm_open("/lm03_demo", O_CREAT|O_EXCL|O_RDWR, 0600) 成功,fd=3
    刚创建时 fstat:size=0 mode=0600(新对象尺寸为 0,不 ftruncate 直接 mmap 会 EINVAL/长度 0)
[2] ftruncate(fd, 65536) 后:size=65536
[3] mmap MAP_SHARED 完成,写入第一代标记 0x1111 + 文本 "generation-1"
$ ls -l /dev/shm
total 4
-rw------- 1 charliechen charliechen 65536 Oct  3 10:49 lm03_demo
```

Let us read the output slowly. At the first `ls`, `/dev/shm` is completely empty. Once shm_open has built the object and ftruncate has set the size, we list again and `lm03_demo` is lying right there: from owner and permission bits to byte count, it looks exactly like an ordinary file. The namespace refers to the directory entries in `/dev/shm`; taking that phrase literally is fine. Size 0 is a new object's factory state, and the man page spells out that the object's size can be set with ftruncate(2). If we mmap without truncating first, a zero-length mapping returns EINVAL under man 2 mmap's terms; we did not eat this one in the experiment, but it is a boundary written out clearly in e1's archived output, and knowing it can save you a detour when you try it yourself.

One old-versus-new difference in compiling and linking, stated up front: man 3 shm_open's page to this day still says to link the real-time library (`-lrt`); that is an old almanac. Since glibc 2.34, linking no longer needs it. To accommodate pre-2.34 systems, keeping `-lrt` does no harm either; on new systems it is harmless.

> A note on where the old almanac comes from: librt's symbols were merged into libc in glibc 2.34. On the author's glibc 2.44, the link command passes without `-lrt`; a `nm` look at `librt.so.1` no longer finds shm_open in it.

One more permission detail: shm_open's mode parameter, like open's, passes through the process's umask. The umask is a permission mask; when a new object is created, it strips the corresponding bits from mode. In E1's side experiment we requested 0666 both times: with umask 0022 the effective permission is 0644, and only after zeroing the umask do we get the full 0666. For objects meant to grant others read-write bits, this umask layer must be figured into the plan; when necessary we push it aside with `umask(0)` and put the original value back afterwards.

## shm_unlink: the name is gone, the entity remains

With the object named, a new problem follows the name: what if two processes fight over the same name? When a second create happens while the name still exists, the referee is O_CREAT paired with O_EXCL. E1's step 4 measured it: without unlinking, we fire the same-named create once more, `shm_open("/lm03_demo", O_RDWR|O_CREAT|O_EXCL, 0600)`, and the return is -1 with errno 17 (EEXIST, file exists), the same temper as open's O_EXCL in [POSIX file I/O](../file-io/01-posix-file-io.md) (L01). The reverse, step 5: we open purely by name without O_CREAT, it succeeds, and the second mapping reads 0x1111; the identity of the same entity is beyond doubt.

The real show comes when we move the camera onto unlink: `shm_unlink("/lm03_demo")` does the one job of deleting the name, and everything interesting lives in the part after the deletion:

```text
[6] shm_unlink("/lm03_demo") 返回 0,名字没了
  readlink(/proc/self/fd/3) -> /dev/shm/lm03_demo (deleted)
$ ls -l /dev/shm
total 0
[7] unlink 后旧映射:仍读到 0x1111 "generation-1"
    继续写 gen1[1]=42 也成功——unlink 只删名字,实体活到所有映射/fd 关闭
[8] unlink 后同名 O_CREAT|O_EXCL 重建成功(fd=4),写入 0x2222 "generation-2"
    同一时刻旧映射看到的还是 0x1111 "generation-1"——同名的两个对象互不相干,证明 unlink 后重建的是新实体
```

Let us go over it line by line. We look at fd 3 with readlink; the path it leads to carries a `(deleted)` suffix, the same presentation as the `(deleted)` on a maps line after file deletion in L02: the name has been plucked from the directory, while the fd and the mapping still grip their references to the entity. Step 7's result: the old mapping still reads 0x1111, and writing 42 into gen1[1] succeeds just the same. Step 8 is the star of the show: with the name freed, the same-named O_CREAT|O_EXCL rebuild succeeded in one try. We write the second-generation marker 0x2222 into the new entity, look back at the old mapping, and still see 0x1111.

Two same-named objects live at the same time, and neither can see the other. String the evidence chain together: unlink deletes the name, not the entity; the entity's lifetime is decided by all fds and mappings, and only when the last reference closes does it truly disappear. The name is merely a registration in the directory: whoever comes by name gets the entity then hanging on that name, and once the name is freed and re-registered, what gets registered is a new entity. man 3 shm_unlink's original text takes the same line: after unlink, opening the same name without O_CREAT fails, and what O_CREAT builds is a brand-new object. One easily-reversed point to flag: EEXIST appears in the second-create-before-unlink case, while rebuilding after unlink is unobstructed; do not get the two directions backwards.

Do the permission bits really bite in this mechanism? We pit two uids against each other for real (E1's side experiment, recorded in e1_perm.cpp). The cross-uid session borrows WSL2's interop channel: `wsl.exe -u root --` enters a uid-0 shell; `/dev/shm` is shared by the whole distribution, so the root and user sessions see the same set of entities:

```text
[1] root 会话建两个对象:0600 与 0666(umask 0,mode 原样生效)
$ ls -l /dev/shm
-rw------- 1 root root 4096 Oct  3 10:50 lm03_perm600
-rw-rw-rw- 1 root root 4096 Oct  3 10:50 lm03_perm666

[2] user 进程(uid=1000)分别去开:
open /lm03_perm600:失败 uid=1000 errno=13 (Permission denied)
open /lm03_perm666:成功 uid=1000 读到"written-by-uid-0"

[3] 反向:user 建一个 0600 对象,root 去开
open /lm03_user600:成功 uid=0 读到"written-by-uid-1000"
```

For the 0600 object root created, we let the user process try to open it; errno comes back 13 (EACCES), so the permission bits really do block. The 0666 object opens fine for user, which then reads the content root wrote in. The reverse direction is the most interesting: the 0600 object user created, root still opens all the same. That is not a failure of permissions; what grants the pass is the CAP_DAC_OVERRIDE in root's hand, the one in Linux's capability system that steps over the routine permission check — special treatment for privileged processes, which we record as it is. The recipe for cross-uid sharing is now clear: the creator grants a full mode and pushes the umask aside, or simply lets the low-privilege side be the creator and the high-privilege side open by name.

## After sharing, addition stops adding up

The object exists and the two processes recognize each other, so we immediately summon shared memory's most famous accident: the counter losing updates. E2's design is this: in one region of shared memory, the head holds the synchronization primitives and the middle lies one `unsigned long long` counter; two child processes each increment it one million times, and the expected final value is two million. The substrate is anonymous `MAP_SHARED|MAP_ANONYMOUS` with fork; the comparison of the two sharing routes gets its own section next, and the race itself we are watching is independent of which route is used:

```cpp
// e2_race.cpp(节选):共享头与三种加一姿势,起跑线 start 统一放行
struct Shared {
    pthread_mutex_t mtx;                  // mutex 版用
    sem_t sem;                           // sem 版用
    volatile unsigned long long counter; // volatile 只挡编译器,不提供原子性
    std::atomic<int> ready;              // 子进程就绪计数
    std::atomic<int> start;              // 起跑线,计时口径统一
};
// 两个子进程各跑 n 次,循环体按模式三选一:
//   mutex : pthread_mutex_lock / counter = counter + 1 / pthread_mutex_unlock
//   sem   : sem_wait / counter = counter + 1 / sem_post
//   nosync: 裸的 counter = counter + 1
```

```text
== E2 跨进程计数竞态:三版各 3 轮(每人 1,000,000 次,期望 2,000,000)==

--- nosync ---
nosync  每人 1000000 次:结果 1526047 / 期望 2000000(丢失 473953 次,23.70%) 耗时 1.9 ms(104 万次/秒)
nosync  每人 1000000 次:结果 1628983 / 期望 2000000(丢失 371017 次,18.55%) 耗时 1.5 ms(130 万次/秒)
nosync  每人 1000000 次:结果 1807586 / 期望 2000000(丢失 192414 次,9.62%) 耗时 1.3 ms(158 万次/秒)

--- mutex ---
mutex   每人 1000000 次:结果 2000000 / 期望 2000000(无丢失) 耗时 60.4 ms(3 万次/秒)
mutex   每人 1000000 次:结果 2000000 / 期望 2000000(无丢失) 耗时 44.7 ms(4 万次/秒)
mutex   每人 1000000 次:结果 2000000 / 期望 2000000(无丢失) 耗时 39.8 ms(5 万次/秒)

--- sem ---
sem     每人 1000000 次:结果 2000000 / 期望 2000000(无丢失) 耗时 57.5 ms(3 万次/秒)
sem     每人 1000000 次:结果 2000000 / 期望 2000000(无丢失) 耗时 60.7 ms(3 万次/秒)
sem     每人 1000000 次:结果 2000000 / 期望 2000000(无丢失) 耗时 58.1 ms(3 万次/秒)
```

Lay the three rounds out: 1526047, 1628983, 1807586, with lost updates oscillating between 9.62% and 23.70%. Three rounds gave three numbers; even the amount lost is unstable. That is the true face of a race: how much gets lost depends on how the two processes' steps interleave at run time, and every round answers differently.

How do updates get lost? At the machine-code level, `counter = counter + 1` is a three-step job: read the memory value into a register, add one to the register, write the result back to memory. Two processes each run their own three steps; as long as A has read the old value and not yet written back while B reads the same old value, two additions collapse into one. Claiming this in the abstract is useless, so we summon the `-O2` assembly, which the end of E2's output already carries:

```text
== 附:nosync 计数循环的汇编(确认每轮都是回内存的读改写,不是寄存器累加)==
    1523:	f0 83 43 50 01       	lock addl $0x1,0x50(%rbx)
    153b:	48 83 43 48 01       	addq   $0x1,0x48(%rbx)
    155a:	48 83 43 48 01       	addq   $0x1,0x48(%rbx)
    1580:	48 83 43 48 01       	addq   $0x1,0x48(%rbx)
    ↑ 计数是 addq $0x1,内存 的非原子读改写(无 lock 前缀);对比 ready.fetch_add 编出的是 lock addl
```

Look at the three lines from 0x153b down: the counter compiles entirely into `addq $0x1,0x48(%rbx)`, adding directly against memory. Without the lock prefix it is an ordinary read-modify-write: two cores executing it at the same time do not greet each other either. The `lock addl` in the first line is the control group: that is what ready's fetch_add in the same piece of code compiles to, the hallmark of an x86 atomic instruction; an instruction wearing the lock prefix makes this read-modify-write indivisible. The same increment, differing by one prefix, yet the atomicity is worlds apart.

Here we must hold down a widespread misconception: volatile cannot stop this accident. Its target is the compiler, forcing a real memory read and a real write every time, preventing the load and add from being hoisted outside the loop and batched up. E2's counter does carry volatile; without it, under `-O2` and from a single process's viewpoint, the counter loop could be optimized into one read, add N, one store, and the lost-update show could not go on. But volatile has no say at the level of inter-core synchronization: the read and the write remain two independent actions, the interleaving stays as it was, and the updates keep getting lost. If we truly want atomicity, the two locks in the next two sections are the answer; the other road is the atomic instruction with the lock prefix.

The first fix uses a mutex. We place a `pthread_mutex_t` into shared memory, and the two processes lock/unlock the very same lock. The crux is `pthread_mutexattr_setpshared`: a mutex's default attribute is PTHREAD_PROCESS_PRIVATE, recognizing only this process's threads; for the lock's body to be shared across processes, we must set the attribute to PTHREAD_PROCESS_SHARED. In man 3 pthread_mutexattr_setpshared's words, with it set, any thread that can access this memory can operate the lock, including threads in other processes. The precondition for the lock to be reachable we have also stated: the lock itself must live in shared memory; parking it on any process's own heap is useless. E2's lock lives at the head of the Shared structure, and the result is three straight rounds of 2000000 with not a single lost update, at the cost of a median 44.7 ms, roughly thirty times dearer than the one-or-two milliseconds of the bare increment.

The second fix goes through a process-shared semaphore. The second parameter of `sem_init(&sem, 1, 1)` is pshared; giving 1 means inter-process sharing, the same idea as the mutex's PROCESS_SHARED with a different primitive. A semaphore is a counting synchronization primitive: its wait decrements and leaves when it sees a count above zero, and its post action increments and releases. E2 used it as a pure mutex, and the three-round result is again all hits, with a median of 58.1 ms, a bit slower than the mutex version: the counting ability went unused, yet the overhead was paid in full. If we only want a single counter atomic without entering lock territory, switching to `std::atomic`'s fetch_add finishes the job; what it compiles to is exactly the lock addl we saw in the assembly. Atomicity for a single variable we now have; the place that truly tests atomic craftsmanship falls on data structures, and in the flagship experiment later, we do it for real.

## Two routes: anonymous mapping with fork, named objects with strangers

We pull the topic back to recognition: when shared memory is to enter a process, there are in fact two doorways. E3 puts both into the same experiment as a contrast.

Route A is the anonymous shared mapping: `MAP_SHARED` with `MAP_ANONYMOUS`, fd passed as -1, attached to no file. It has no name, and its only propagation is fork inheritance: a mapping built before fork is inherited as-is by the child, and writes interpenetrate instantly. Anonymous mappings each built after the fork are two unrelated entities; an anonymous thing has no name to be found by, so other processes have no way to recognize it. E3's output plays both facts at once:

```text
== 途径 A:匿名 MAP_SHARED|MAP_ANONYMOUS(fork 前映射,子进程继承)==
父进程读 fork 前的共享映射:0xC0DE(子进程写的,继承链通了)
父进程读 fork 后自己建的匿名映射:0x0(子进程那份是另一个实体,匿名没有名字无从相认)
```

Carrying on: the child wrote 0xC0DE into the pre-fork mapping, the parent read it, so the inheritance chain works. The child then built its own anonymous shared mapping and wrote 0xDEAD into it; in the mapping the parent built after its fork, the read is still 0. The two mappings' calls differ by not a single flag bit; the only difference is the order relative to the fork. E2 used exactly route A: parent and two children sharing the same region.

Route B we know even better; it is this article's protagonist: the named object. Server and client are two independent processes pulled up one after the other by run_e3.sh, with no parent-child relation between them; what they rely on is the name `shm_open("/lm03_two")` to recognize each other. The server builds the object and writes a message, sets an atomic flag to 1; the client also opens the object by name, polls for the flag's change, and replies with an ack once the message is read:

```text
--- 命名对象:server 与 client 是两个独立进程(无 fork 亲缘)---
[server pid=41045] shm_open 建好 /lm03_two,写消息并置 msg_ready,等 ack(10s 超时)...
[client pid=41047] 与 server 无亲缘,靠 shm_open("/lm03_two") 相认,读到 "hello-from-server-pid-41045",回 ack
[server] 收到 ack,消息送达;unlink 收尾
```

Note the pids as well: there is no kinship between the two; the name is the only thing recognition rests on. The broadcast power of named objects shows itself: it asks for no family ties between the two processes, nor for synchronized start times. Even a late-starting client fears nothing: we have it retry by name a hundred times, resting 100 milliseconds each round, and it always reaches the moment the server has built the object.

There is a third road: the fd itself can also serve as a token to pass. UNIX domain sockets' sendmsg with SCM_RIGHTS (the mechanism that stuffs an fd into ancillary messages) can hand an already-open fd directly to another process; what the peer's recvmsg obtains is a new fd the kernel copied on its behalf, pointing at the same entity. In E3's accompanying fdpass demo, the child receives the passed fd, and after mmap reads exactly what the parent wrote: the fd arrived, so did the entity, and not even a name is needed. The detailed treatment stays in the IPC article; we will come back for it in the memfd section.

## Flagship experiment: the SPSC lock-free ring queue

Locks do keep the peace, and we have measured the price: 44.7 ms. On the real battlefield of high-performance IPC, what people use is mostly a different skeleton: the lock-free ring queue. E4 is about exactly that: a configuration of one producer facing one consumer, named single producer single consumer in English, abbreviated to SPSC. The whole queue lives inside the entity shm_open produced, and between the two processes there is not a single lock.

We read the queue's skeleton in code. The head position is a small control area: the start line, ready flags, statistics. Further back are two counters each occupying one cache line: head and tail. A word on the cache line: it is the minimal unit of transfer between the CPU cache and memory, 64 bytes per line on x86-64. We use `alignas(64)` to give head and tail one exclusive cache line each; no crowding together. Without this arrangement, two processes alternately writing two variables in the same cache line would have the hardware invalidating the copy the other side just warmed, throwing away bandwidth for nothing; everyone calls this phenomenon false sharing, and this volume's article on alignment and huge pages will come back to measure it. Further back still are 1024 slots, each holding one 32-byte message:

```cpp
// e4_spsc.cpp(节选):head/tail 各占一条 cache line,防伪共享
struct alignas(64) ProdLine { std::atomic<uint64_t> head; char pad[64 - sizeof(std::atomic<uint64_t>)]; };
struct alignas(64) ConsLine { std::atomic<uint64_t> tail; char pad[64 - sizeof(std::atomic<uint64_t>)]; };

struct Header {
    std::atomic<uint32_t> start;   // 起跑线
    std::atomic<uint32_t> ready;   // 消费者就绪
    uint64_t nmsgs;                // 计划发送条数
    uint64_t received, order_err, corrupt;  // 消费者填,父进程收尾读
    int32_t consumer_pid;
    ProdLine p;
    ConsLine c;
};
struct Msg { uint64_t seq; uint64_t checksum; uint64_t payload[2]; };

// lock-free(address-free)原子操作跨进程才成立——静态断言把关
static_assert(std::atomic<uint64_t>::is_always_lock_free);
```

The write right over head belongs to the producer alone, and over tail to the consumer alone; this is the foundation of all SPSC correctness: every counter forever has exactly one writer. All communication between the sides rides on memory ordering: after writing a slot, the producer stores head with release semantics, equivalent to announcing the message is complete; the consumer loads head with acquire semantics, and every slot before the loaded head is guaranteed fully written. The same holds for tail in the other direction. What acquire/release actually guarantees was covered from the ground up in the vol5 concurrency volume's [memory ordering in detail](../../../../vol5-concurrency/chapter05-atomic-memory-model/04-choosing-memory-orders.md); here we only do the engineering landing and do not reopen the theory class. On the cross-process road there is one more hard precondition, the static_assert in the code: if some platform implements atomic with an internal lock, the lock lives in the library's own territory, entirely out of the other process's reach; only lock-free atomic operations are address-independent, so the same memory works fine even at different addresses in two processes.

The core loops of both sides, together with the backpressure strategy:

```cpp
// e4_spsc.cpp(节选):满/空时的等待策略二选一
const auto backoff = [yield_mode] {
    if (yield_mode) sched_yield();   // yield 版:让出 CPU
    else cpu_relax();                // spin 版:x86 的 pause 指令
};

// ---- 生产者(父进程):满则等(背压),写槽,head 以 release 发布 ----
uint64_t head = 0;
for (uint64_t seq = 0; seq < n; ++seq) {
    while (head - h->c.tail.load(std::memory_order_acquire) >= kSlots) backoff();
    Msg& m = slots[head & (kSlots - 1)];    // 1024 是 2 的幂,掩码取模
    m.seq = seq; m.checksum = mix64(seq);   // 载荷加校验和
    ++head;
    h->p.head.store(head, std::memory_order_release);
}

// ---- 消费者(子进程):空则等,核对序号与校验和,tail 以 release 发布 ----
uint64_t t = 0;
for (uint64_t expected = 0; expected < ch->nmsgs; ++expected) {
    while (t == ch->p.head.load(std::memory_order_acquire)) backoff();
    const Msg& m = cslots[t & (kSlots - 1)];
    if (m.seq != expected) ++got_order_err;         // 乱序计数
    if (m.checksum != mix64(m.seq)) ++got_corrupt;  // 损坏计数
    ++t;
    ch->c.tail.store(t, std::memory_order_release);
}
```

Both the full and empty checks use monotonic counting: full is `head - tail >= kSlots`, empty is `tail == head`. Our head and tail never wrap; they just keep climbing, and slot indexing takes the modulo with the 1024 mask; unsigned wraparound happens to make the subtraction come out right. With a uint64_t counter, our one million messages sit thirteen orders of magnitude away from the roughly 1.8e19 needed to circle back to zero; the headroom is ample.

```text
== E4 SPSC 无锁环形队列:两进程 1,000,000 条 × 32 B,各 3 轮 ==

--- spin(满/空时的等待策略:CPU 自旋 + pause)---
spin  N=1000000 槽=1024×32B(共 32960B 实体):received=1000000 order_err=0 corrupt=0(子进程 pid=41159) 耗时 6.7 ms,148.16 百万条/秒,有效载荷 4521.5 MiB/s
spin  N=1000000 槽=1024×32B(共 32960B 实体):received=1000000 order_err=0 corrupt=0(子进程 pid=41161) 耗时 6.1 ms,163.97 百万条/秒,有效载荷 5003.9 MiB/s
spin  N=1000000 槽=1024×32B(共 32960B 实体):received=1000000 order_err=0 corrupt=0(子进程 pid=41166) 耗时 7.0 ms,142.28 百万条/秒,有效载荷 4342.1 MiB/s

--- yield(满/空时的等待策略:sched_yield 让出)---
yield N=1000000 槽=1024×32B(共 32960B 实体):received=1000000 order_err=0 corrupt=0(子进程 pid=41169) 耗时 3.8 ms,264.33 百万条/秒,有效载荷 8066.7 MiB/s
yield N=1000000 槽=1024×32B(共 32960B 实体):received=1000000 order_err=0 corrupt=0(子进程 pid=41171) 耗时 3.7 ms,267.57 百万条/秒,有效载荷 8165.4 MiB/s
yield N=1000000 槽=1024×32B(共 32960B 实体):received=1000000 order_err=0 corrupt=0(子进程 pid=41173) 耗时 4.0 ms,248.63 百万条/秒,有效载荷 7587.6 MiB/s
```

Correctness first: received filled up to one million, the out-of-order and corruption counters are both zero, and the two versions' results are fully consistent. Every message the consumer receives gets its sequence number and checksum verified; the release/acquire promise was honored between two processes. The interesting part is speed: the spin version's median is 148.2 million messages per second, the yield version's 264.3; the version that yields the CPU is more than sixty percent faster. Intuition says spinning is fastest — goods in hand should be grabbed immediately — but the numbers spared that intuition no face.

Now we hunt for the cause. Suspect number one is scheduling: if the two processes keep being scheduled onto the same core, spinning is pure dry burning, squandering the very time it should be running. The pinned-cores control (E4's side experiment, parent pinned to CPU0, child to CPU1) eliminated this suspect: spin's median is 161.7, yield's 266.3, and with each side fixed on its own core, the gap stays right where it was. The real culprit is in the cache, the very phenomenon we mentioned while separating cache lines: the spinning producer at full load acquire-loads tail over and over, and tail is precisely the variable the consumer writes on every received message. Two cores alternately demanding exclusive ownership land on the same cache line; it invalidates and re-fetches back and forth between the two cores — the trade calls this cache line ping-pong — and the bandwidth that should carry messages gets eaten by the round trips. sched_yield quiets the producer down when the queue is full, giving the consumer a chance to catch its breath and receive a batch in one stretch; the cache line holding tail rests peacefully on the consumer's side, the ping-pong stops, and throughput goes up instead.

Let us draw its scope of applicability clearly: what was measured is throughput. For single-message latency, spinning responds fastest; the CPU-yielding kind must wait for the scheduler to invite the process back, so on latency it necessarily loses. For throughput scenarios we choose yield; latency scenarios call for a different calculation. Queue depth and whether consumption keeps pace all change the answer, so run the measurement once on your own workload before it counts.

## Hauling 1 MiB: 7304 versus 2349

Asking only whether the queue is fast is half the matter; when choosing, the question you really ask is: compared with the ready-made IPC, how much faster? E5 sends the same workload down two roads. The workload is 1 MiB in volume, sliced into 1024 chunks of 1024 bytes each, verified chunk by chunk on both sides. Road one is E4's queue with bigger slots (1024 bytes × 256 slots); road two is pipe: the parent writes a chunk, the child reads a chunk, likewise 1024 writes plus 1024 reads. We let neither side cheat:

```text
== E5 同一 1 MiB:共享内存环形队列 vs pipe 分 1024 次写读,各 3 轮 ==

shm    1 MiB(1024 B × 1024 块):received=1024 corrupt=0 耗时 0.136 ms → 7343 MiB/s
shm    1 MiB(1024 B × 1024 块):received=1024 corrupt=0 耗时 0.137 ms → 7304 MiB/s
shm    1 MiB(1024 B × 1024 块):received=1024 corrupt=0 耗时 0.255 ms → 3917 MiB/s

pipe   1 MiB(1024 B × 1024 块):received=1024 corrupt=0 耗时 0.426 ms → 2349 MiB/s
pipe   1 MiB(1024 B × 1024 块):received=1024 corrupt=0 耗时 0.461 ms → 2169 MiB/s
pipe   1 MiB(1024 B × 1024 块):received=1024 corrupt=0 耗时 0.418 ms → 2390 MiB/s
```

Lay the medians out: shm's median is 7304 MiB/s, pipe's is 2349 MiB/s, a gap beyond three times. shm's third round dropped to 3917; we take the median convention, and a single round's jitter cannot mask the order of magnitude. Keep the gap's provenance in mind: for every chunk through pipe, the producer's write copies data from user space into the kernel, and the consumer's read copies it back; the round trip costs two system calls plus two copies. On the shm side, the producer writes the very memory the consumer will read, publishes head, and is done: not one copy, not one system call. The extra speed is exactly the two copies saved.

Of course, pipe is not free either: it provides boundary and blocking semantics; one read yields exactly one chunk, framing comes naturally, and buffer management is waived on both ends. With shared memory everything is on you: queue, synchronization, framing, backpressure — the full set of crafts is exactly what this article has been practicing. In real engineering, with small frequent messages and throughput under strain, the shm queue is the answer at that end. For large, sparse messages, or settings that cross machines, pipe and sockets are far less trouble.

## memfd_create: shared memory without a name

Having used them, the named object's convenience is real, and so is the burden the name brings: the global namespace is visible to everyone, and collisions, permissions, and forgotten cleanup each become a matter. Since Linux 3.17 there is another road: memfd_create, which creates an anonymous file and hands us an fd. Calling it anonymous means it has a name in no directory, not even a shadow of it in `/dev/shm`. Calling it a file means the ftruncate, mmap, and read/write interfaces all work; the content stays in memory, and its behavior is indistinguishable from a tmpfs file. E6 played it through once:

```text
== E6 memfd_create:匿名内存的 fd 化 ==

[1] memfd_create("lm03_memfd", MFD_CLOEXEC) = fd 3
[2] ftruncate 4096 + mmap MAP_SHARED + 写入 "hello-memfd" 完成
  readlink(/proc/self/fd/3) -> /memfd:lm03_memfd (deleted)
$ ls -l /dev/shm
total 0
    ↑ /dev/shm 里没有它:memfd 不占名字空间,名字只是 /proc 里的标签
[3] 同名再建一个 memfd(fd=4):两个实体互不影响——fd1 处是 "hello-memfd",fd2 处是 "second-object"
  readlink(/proc/self/fd/4) -> /memfd:lm03_memfd (deleted)
[4] 收尾:close 两个 fd,引用计数归零对象即消失——没有 shm_unlink 这一步……
```

Three things to look at. The path readlink gives is `/memfd:lm03_memfd (deleted)`; man 2 memfd_create is quite plain: the name serves debugging purposes only, shown in the `/proc/self/fd` symlink, always carrying the memfd: prefix, with no effect on behavior whatsoever. Creating another with the same name yields two unrelated entities, the same reasoning as the post-unlink rebuild in E1, down to the identical `(deleted)` look. The lifecycle is even crisper: there is no unlink step; once closes bring the reference count to zero, the object vanishes on the spot, name and all. Want to hand it to another process? The fd itself is the entire token: one SCM_RIGHTS pass and it arrives, or simply go through fork inheritance. The man page also lists a serious use this article does not need: fcntl's file sealing, which can seal shared memory against further truncation by the other side, guarding against exactly the SIGBUS kind of accident from L02; when you need to defend against an untrusted peer, come back and look it up then.

Two lines on the adoption threshold: the kernel needs 3.17 or later, and glibc's wrapper function 2.27 or later. The man page's line is to `#define _GNU_SOURCE` before compiling; that is aimed at C compilers. The g++ we use already defines it on Linux, and the archive's compile line passed without carrying it; the header is still `<sys/mman.h>`.

So when do we use it, and when shm_open? As we understand it: shm_open's name is public, a fit for loose coupling; a process started today can find the object registered yesterday by name, and the recognition work is left to the name. memfd is point-to-point: whoever receives the fd holds the right to use it; a process without the fd does not even know it exists, it occupies no global name at all, and ownership of permissions follows the fd. In security-sensitive settings, or lifecycles tied to a particular connection, memfd gets even handier.

## How the other side sees it

Now Windows: there is no shm_open over there; the same job goes through CreateFileMappingW. The first parameter should take a file handle, but we pass INVALID_HANDLE_VALUE, the mapping attaches to no file, and the system's page file (page file, Windows' virtual-memory backing file) serves as storage; that is the customary shape of Win32 shared memory, and the [file mapping](../../windows/file-io/02-file-mapping.md) article (W02) already covered it once as a mapping object. The mirror article [Shared Memory: Page-File-Backed Named Mapping Objects](../../windows/memory/02-shared-mem.md) is already drafted and has been checked line by line against this one; here we only preview the lifetime fork most worth remembering. The two sides are actually isomorphic: both objects live to the last reference; on the Linux side fds and mappings both count, on the Windows side handles and views both count. The real fork is in how the name dies: on our side unlink is explicit, and the name's life and death have nothing to do with how many mappings exist. Over there no unlink is callable; the name hangs in the system's object namespace and follows the handle count, and the moment the last handle closes, the name is plucked off on the spot, while the object can still live on anonymously through its views until the final reference. The full text on sharing and synchronization meets us again in the mirror article.

<ReferenceCard title="Reference Resources">
  <ReferenceItem
    :id="1"
    title="shm_open(3)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man3/shm_open.3.html"
  />
  <ReferenceItem
    :id="2"
    title="shm_unlink(3)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man3/shm_unlink.3.html"
  />
  <ReferenceItem
    :id="3"
    title="pthread_mutexattr_setpshared(3)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man3/pthread_mutexattr_setpshared.3.html"
  />
  <ReferenceItem
    :id="4"
    title="sem_init(3)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man3/sem_init.3.html"
  />
  <ReferenceItem
    :id="5"
    title="memfd_create(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/memfd_create.2.html"
  />
  <ReferenceItem
    :id="6"
    title="ftruncate(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/ftruncate.2.html"
  />
  <ReferenceItem
    :id="7"
    title="mmap(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/mmap.2.html"
  />
  <ReferenceItem
    :id="8"
    title="capabilities(7)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man7/capabilities.7.html"
  />
</ReferenceCard>
