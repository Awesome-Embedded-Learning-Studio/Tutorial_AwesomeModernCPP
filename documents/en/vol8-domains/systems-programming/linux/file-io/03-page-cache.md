---
title: "Page Cache and Persistence: What Happens After write() Returns"
description: "write() returns successfully, but it only copies the data into the kernel's page cache; the disk hasn't been touched yet. This article uses /proc/meminfo's Dirty counter to observe 256 MiB of dirty pages lingering for thirty seconds before being flushed (when dirty_expire_centisecs hits the mark and flusher threads take action), tests three scenarios with _exit(0) and kill -9 to see that data remains even after the process dies, measures the performance of four persistence paths (plain/fsync/fdatasync/O_SYNC) (fsync and fdatasync are the same, O_SYNC with 4KiB blocks only achieves 3 MiB/s, the cost of small blocks and direct writes is consistent across machines), and uses fixed/append/touch three methods to measure the metadata difference between fsync and fdatasync as a stable 4.7x signal in the touch scenario, with dd's default mode leaving 262 MB of dirty pages as evidence, and two unexpected findings: rm discards dirty pages and _exit doesn't flush stdio buffers."
chapter: 8
order: 3
platform: host
difficulty: intermediate
cpp_standard: [20]
reading_time_minutes: 18
prerequisites:
  - "POSIX File I/O: open/read/write and the life of fd"
  - "mmap Memory Mapping: Attaching a file to the address space"
related:
  - "RAII Paradigm for OS Resources: fd, HANDLE, and the same skeleton for mappings"
  - "Win32 File I/O: Handles, CreateFileW, and synchronous reads/writes"
tags:
  - host
  - cpp-modern
  - intermediate
  - System Programming
  - POSIX
  - Practical
current_status:
  title: Being Polished
  detail: This batch has just been written and is currently being proofread and refined; the wording and experimental results may change at any time.
  variant: polishing
translation:
  source: documents/vol8-domains/systems-programming/linux/file-io/03-page-cache.md
  source_hash: 197dd00e06cef727d4082f657d99ba3b6738b2198523ab9d54547738d9ffa802
  translated_at: '2026-11-22T14:26:00+00:00'
  engine: qwen3:14b
  token_count: 18800
---
# Page Cache and Persistence: What Happens After write() Returns

In the previous article, we ran through 512 MiB of sequential reads, and in that comparison, the disk never came into play at all: the entire file was sitting in the page cache, and read and mmap were only competing on how the CPU side fetched the bytes. On the read side, the page cache is an accelerator that shields us from the disk. Today, we turn the lens to the write side, and the same page cache changes roles: it becomes a buffer that can mislead you. The question is only one: after write() returns, is the data on the disk?

We already got the answer in [L01](./01-posix-file-io.md): the original words from man 2 write are very clear, a successful write does not provide any guarantee that the data has reached the disk. The only way to confirm is to call fsync(2) once. At that time, that sentence was written as a warning, and in this article, we turn it into something we can verify by hand: watching dirty pages linger for thirty seconds in the Dirty counter, verifying whether the data is actually lost when the process dies, and measuring the time taken for the three persistence paths of fsync, fdatasync, and O_SYNC one by one. Along the way, we also picked up two unexpected discoveries: rm discards dirty pages, and `_exit` leaves stdio buffers untouched, both of which the author personally encountered.

There are six experiments in this article, numbered E1 to E6, corresponding one-to-one with the six directories from 01 to 06 under the `code/volumn_codes/vol8/systems-programming/linux/file-io/03-page-cache/` folder in the repository. The original outputs are also included, and you can refer to them at any time. [L02](./02-mmap-memory-mapping.md) uses lowercase e1 to e5 for its experiments, which are unrelated to the uppercase E here; please don't mix the two sets. The experimental code is all in bare C style, we write open, write, and perror bare-handed, the small programs are only for observing the behavior of the page cache, and no encapsulation is allowed to distract us. The actual writing in real engineering, and how to handle the return value of fsync, we add a segment in E5 with the public tools of the series.

We clearly state the experimental environment once and for all, and the numbers we use later will all be matched with it: the CPU of the desktop is AMD Ryzen 7 9700X, the system is WSL2, the kernel is 6.18.33.2-microsoft-standard-WSL2, the compiler is g++ 16.2.1, and the compilation configuration is always `-std=c++20 -O2 -Wall -Wextra -Wpedantic`. Two environmental facts directly determine the success or failure of the re-run, please take a look now. The first thing is the location of the data file, we put all the data on ext4 (`/home/charliechen/l03_scratch/` under ext4, you need to create this directory before re-running). /tmp is the territory of tmpfs, its files are already living entirely in memory, writing back is not a thing for it, the Dirty count will remain unchanged, and the data placed there would be a waste of testing, [L02](./02-mmap-memory-mapping.md) has already established the same discipline for its msync experiment. Another environmental fact is the disk itself: the disk that WSL2 sees is actually a VHDX virtual disk file on the Windows side, the real landing point is the NVMe on the machine, and 256 MiB of concentrated disk writes are completed in tens of milliseconds, so in this article, the multiple times of fsync, we have to carry the machine's factors. Another slightly unglamorous honest confession: the author's sudo requires a password, drop_caches (it is the root knob under /proc/sys/vm that can completely empty the page cache) and real crash demonstrations cannot be done, so in E5, we downgrade and make the parts we can do solid.

## E1: Only write, no sync, 256 MiB of dirty pages linger for thirty seconds

We explain the moment when write() returns with the mechanism: write only hands the data over to the kernel. The kernel copies the bytes from your buffer into the corresponding pages in the page cache, marks these pages as dirty, and then returns. **Dirty pages** refer to the pages in the page cache that have been modified but not yet written to the disk. The action of writing them back is called **writeback**, and the kernel threads that do this work are called flusher threads. To observe them, [L02](./02-mmap-memory-mapping.md) has already used: the **Dirty** field in /proc/meminfo records the total number of dirty pages in the entire system, in units of kB, and the **Writeback** field counts the pages currently being written back. These terms will be used frequently later, and we explain them here once.

The data writing program for E1 is written very straightforwardly: 256 MiB, 1 MiB per block, looping write, and after the loop ends, we `_exit(0)` and leave immediately. We do not fsync, do not close, and do no cleanup at all. The sampling is handled by the outer e1_run.sh, which reads the pair of numbers every 2 seconds:

```cpp
// e1_dirty.cpp(节选):写循环加一句告别,观察交给外层脚本
std::vector<char> buf(static_cast<size_t>(chunk) * 1024);
for (long long off = 0, blk = 0; off < total << 20; off += chunk << 10, ++blk) {
    std::memcpy(buf.data(), &blk, sizeof(blk));   // 块号写在头 8 字节
    long long done = 0;
    while (done < chunk << 10) {
        ssize_t n = write(fd, buf.data() + done, static_cast<size_t>((chunk << 10) - done));
        if (n < 0) { perror("write"); return 1; }
        done += n;
    }
}
std::printf("[e1_dirty] write 循环结束。不 fsync、不 close,直接 _exit(%d) —— 数据生死交给页缓存\n", 0);
std::fflush(stdout);  // 教训:_exit 不刷 stdio 缓冲,不 fflush 这两行 printf 就会无声消失
_exit(0);
```

The comment on that line of fflush in the source code, please take a close look, it is a lesson the author personally learned, we will revisit it in E2. The current sampling looks like this (the header of the archived file has a few lines of environment and vm readings, which we omit):

```text
$ ./e1_run.sh
# 压基线: sync,等 Dirty 落回低位
# 基线 Dirty=112 kB Writeback=408 kB

# 起 writer: 256 MiB,只 write(),随后 _exit(0)
[e1_dirty] path=/home/charliechen/l03_scratch/e1.bin total=256MiB chunk=1024KiB writes=256 write_loop=129.1ms (1983 MiB/s)
[e1_dirty] write 循环结束。不 fsync、不 close,直接 _exit(0) —— 数据生死交给页缓存

# writer 已退出。开始采样(相对秒  Dirty[kB]  Writeback[kB]):
# 0  262144  240
# 2  262208  0
# 4  262300  0
# 6  262472  0
...（8 到 28 秒共 11 行:Dirty 从 262528 缓慢爬到 263188,Writeback 除第 18 秒一帧 4 kB 外全 0,咱们略去）...
# 30  263328  0
# 32  192  0
# 34  300  0
...（36 秒起 Dirty 回到几百 kB 量级的小幅起伏,观察窗共 80 秒,后面略）...
```

The numbers themselves do the arithmetic, 262144 kB is exactly the number for 256 MiB. We see this number, and it remains steady in the sampling window for thirty seconds, while the person who wrote it has already `_exit`ed. The act of writing the data down, from start to finish, has no user-space process involved.

What happened at the end of the thirtieth second? We go to /proc/sys/vm and look at that row of knobs, the local machine's reading is `dirty_background_ratio=10 dirty_ratio=20 dirty_expire_centisecs=3000 dirty_writeback_centisecs=500`. The units for the latter two are centiseconds, 3000 translates to 30 seconds, and 500 translates to 5 seconds. dirty_expire_centisecs refers to how long dirty pages are considered old, the local machine's number is 30 seconds, dirty_writeback_centisecs refers to how often the flusher thread wakes up, the local machine's number is 5 seconds. So the timeline is: dirty pages are considered expired after 30 seconds, and then wait for the next wake-up of the flusher thread (up to 5 seconds), the expired batch is written. We ran two rounds independently, and the drop both fell just after 30 seconds, exactly in the middle of the theoretical window. However, this set of knobs gives a typical behavior, the timing is not a promise from the kernel, we will talk about it in detail in E5.

Why didn't the flusher care for the first thirty seconds? Because there is another threshold in front: dirty_background_ratio=10, which means that the background writeback will only start when the total amount of dirty pages exceeds 10% of the memory. The local machine's MemTotal is about 53 GiB, a 10% threshold is about 5 GiB, our 256 MiB is not even close to the threshold. If we rerun on a machine with small memory, the Dirty may have already dropped in the middle, we have reminded of the same thing in the msync experiment of [L02](./02-mmap-memory-mapping.md). Another small observation: Dirty slowly climbs from 262144 to 263328, the kilobytes that climbed out are not ours, this count is for the entire system, and dirty pages written by other processes are also mixed in, so we look at the magnitude and the drop location, not the absolute value of the units.

We still find the 2-second frame too coarse, and we ran it again with a 1-second frame density (the sampling script is modified according to e1_run.sh, only the interval is changed to 1 second, the variant is not included in the catalog):

```text
$ ./e1_dirty   # 第二轮,采样出自外层脚本的 1s 变体
# 基线 Dirty=0 kB
[e1_dirty] path=/home/charliechen/l03_scratch/e1.bin total=256MiB chunk=1024KiB writes=256 write_loop=145.1ms (1765 MiB/s)
[e1_dirty] write 循环结束。不 fsync、不 close,直接 _exit(0) —— 数据生死交给页缓存
# 28  262972  0
# 29  262988  0
# 30  263012  0
# 31  96  0
# 32  120  0
（0 到 27 秒与首轮同型,Dirty 稳在 262148 到 262932,咱们略去;这轮 46 帧采样里,Writeback 没有一帧大于 0）
```

The drop occurred between the 30th and 31st seconds, Dirty dropped from 263012 to 96 in one step. You can see the Writeback column again: in forty-some frames of sampling, Writeback was never greater than 0. The writeback of 256 MiB didn't even give us a 1-second window on this machine, the writeback itself was so fast that we couldn't catch it, what we could catch were only the two static states before and after the writeback. If you try it on a slower disk, you will see a different picture: a platform period where Writeback is greater than 0, and Dirty drops in segments.

Another unexpected discovery is worth noting separately. We encountered it while debugging the script: after the writer wrote 256 MiB, Dirty was still high, and a rm deleted the file, Dirty dropped to zero instantly. It's not mysterious once you understand it—dirty pages are attached to the file's inode (inode, the node in the kernel that manages file metadata, size, timestamps, and disk block positions are all recorded on it), when the file is gone, this batch of dirty pages is gone, the kernel will not write a file that doesn't exist. So in the observation window of E1, there is a rule: don't touch that file. The script in E3 puts `rm -f` outside the timing interval to prevent it.

## E2: kill -9 can kill the process, but not the page cache

We bring the lens back to the end of E1: the data has been hanging in the page cache for thirty seconds, and the process that wrote it is no longer there. This naturally leads to that widely spread question: when the process dies, will the data be lost? What about kill -9? It's not interesting to discuss it in the air, we set up three scenarios for the death method, and verify them one by one. Scenario A is the most straightforward, the child process writes 44 bytes and immediately `_exit(0)`. Scenario B is a segmented write, the child process writes 4 KiB numbered blocks in a loop, and is killed by kill -9 halfway through. Scenario C is for comparison, the child process writes 100 lines with fprintf, and the closing action is also `_exit(0)`:

```cpp
// e2_exit.cpp(节选):场景 B 的子进程,块头 8 字节写编号,其余按块号填充
for (uint64_t i = 0;; ++i) {
    std::memcpy(blk.data(), &i, 8);
    unsigned char fill = static_cast<unsigned char>(0x41 + i % 26);
    std::memset(blk.data() + 8, fill, 4096 - 8);
    ssize_t n = write(fd, blk.data(), blk.size());
    if (n != (ssize_t)blk.size()) _exit(51);
    timespec ts{0, 200 * 1000};   // 每块睡 200µs 压速度,让 kill 落在写到一半的途中
    nanosleep(&ts, nullptr);
}

// 场景 C1:100 行 x 24 字节,小于 stdio 的 4096 缓冲,全攒在用户态
for (int i = 0; i < 100; ++i) fprintf(fp, "STDIO_BUFFERED_LINE_%03d\n", i);
_exit(0);   // 不 fclose、不 fflush;C2 只多一行 fflush(fp),其余一字不差
```

The parent process is responsible for killing, collecting the body, and reading back for verification, the output we get is only a few lines:

```text
$ ./e2_exit
== 场景 A: write() 后立刻 _exit(0),不 fsync ==
  子进程 A 已退出 (waitpid status=0x0)。父进程现在 read 同一文件:
  read() 返回 44 字节,内容: "WRITTEN_VIA_WRITE_SYSCALL_NO_FSYNC_THEN_EXIT"
  判定: 可见 —— write() 返回时数据已在页缓存(内核),进程死不死它都在

== 场景 B: 写到一半 kill -9 ==
  子进程被 kill -9 (waitpid status=0x9, SIGKILL=9)。父进程读回校验:
  文件大小: 1728512 字节 (422 个完整 4KiB 块, 尾部零头 0 字节)
  校验: 422/422 个完整块编号与填充模式全部完好, 序号 0..421 连续
  判定: kill -9 杀掉的是进程;凡是 write() 已经返回的数据,一个字节都没丢

== 场景 C(对照): stdio 用户态缓冲,同样 _exit(0) ==
  C1 fprintf 后 _exit(0): 文件大小 0 字节 (100 行 x 24 字节本应 2400)
  C2 fprintf + fflush 后 _exit(0): 文件大小 2400 字节
  判定: 丢数据的另一层是 stdio 用户态缓冲;write() 返回后那一层已经不存在了
```

We look at each scenario one by one. In scenario A, the child process didn't even do a close, and the parent process still read back the 44 bytes as they were. The scene in scenario B is even more brutal, when the file was interrupted by kill -9, it still had 422 complete blocks, numbered 0 to 421, continuous, we checked the byte-by-byte fill pattern within the block, and there was no tear. We combine the two scenarios and explain the mechanism directly: the page cache is a kernel thing, not a process thing. And once write() returns, the bytes have moved into the kernel's page cache, the life and death of the process has nothing to do with it, SIGKILL can only terminate the process itself.

But the result of scenario C is exactly the opposite. The same child process plus `_exit(0)`, fprintf writes 100 lines, each line 24 bytes, and reads back 0 bytes. Adding a line of fflush, 2400 bytes are neatly in the file. The data loss is not from the page cache, but from the user-space buffer of stdio: the buffer in the `FILE*` lives in your process's own memory, fprintf only accumulates bytes in it, and only when it's full or flushed, it actually calls write(2) into the kernel. `_exit()` is a bare system call, it doesn't run the functions registered by atexit, nor does it flush any stdio stream for us, the flushing of the stream is the job of `exit(3)`. The bytes accumulated in the user space are evaporated with the process.

The author has a say on this matter: the initial version of the E1 program used printf to print two lines and then directly `_exit(0)`, after running, you would see that the two lines in the output had silently disappeared, the experimental code itself walked into the scene of scenario C. The fix you've already seen in the excerpt of E1 is to do a proper fflush before `_exit`. This is also why the two child processes in E2 that verify the write semantics do not write stdio at all, all outputs go through write.

And after write() returns, what kind of death will actually lose data? We go through the candidates: normal process exit, kill -9, and close, none of them can touch the bytes that have already been written (close's return value is not even worth looking at—RAII section has already mentioned, data is not its concern). The only one that will actually lose data is: the kernel hasn't had time to write back, the machine is gone—power failure, kernel panic, or in the WSL2 scenario, the layer of Windows that carries it is completely dead. The typical length of this window, E1 has already measured for us: thirty seconds. The way to close it to zero is the main character of E3.

## E3: plain, fsync, fdatasync, O_SYNC, one by one timing

The three ways to truly deliver the data to the disk, plus one non-persistent control group. The plain approach is to write and not care, the fsync approach is to run a loop and then add an fsync (timing includes it), the fdatasync approach is the same, but using fdatasync instead, the O_SYNC approach is to bring the flag when opening, and each write is synchronously written to the disk. The total for one round is 256 MiB, the single write is 1 MiB, before each round starts, we press Dirty back to a low level, we run three rounds, and take the median of each:

```cpp
// e3_bench.cpp(节选):计时含 sync 调用本身
long long t0 = now_ns();
for (long long off = 0; off < total; off += chunk) {
    long long done = 0;
    while (done < chunk) {
        ssize_t n = write(fd, buf.data() + done, static_cast<size_t>(chunk - done));
        if (n < 0) { perror("write"); return 1; }
        done += n;
    }
}
if (do_fsync && fsync(fd) != 0) { perror("fsync"); return 1; }
if (do_fdatasync && fdatasync(fd) != 0) { perror("fdatasync"); return 1; }
long long t1 = now_ns();   // O_SYNC 模式则全程都在等盘
```

```text
mode              r1_ms        r2_ms        r3_ms      median_ms median_MiB_s
plain             129.9         40.4         38.8           40.4         6337
fsync              60.6         60.0         61.4           60.6         4224
fdatasync          60.3         59.3         59.6           59.6         4295
osync             301.6        284.4        318.6          301.6          849
osync_4k        22387.2      15162.0      20871.5        20871.5            3
```

osync_4k is an additional group: O_SYNC with 4 KiB small blocks, total 64 MiB, the rest of the parameters are the same.

The first thing worth looking at is the first round of plain, it ran 129.9 ms, the following two rounds were only about 40. This cold start price you've already seen the same version of, E1's write_loop is 129.1 ms, the first round of dd in E6 also falls into the 2 GB/s category, the three numbers match, talking about the same thing: the first time the page cache is paged, and various registrations are done, all require extra spending. The steady-state numbers, we look at the second and third rounds.

The results of fsync and fdatasync are 60.6 vs 59.6, the difference is on the edge of milliseconds. In the scenario of sequential writing a large file and centralized brushing at the end, the prices of the two are basically the same. Are they slower than plain? On this machine, they are only 1.5 times slower. This number you must remember with the details of the machine: the disk behind this is a VHDX virtual disk file on Windows, the actual landing point is the NVMe on the machine, and 256 MiB of centralized disk writes are completed in tens of milliseconds. If you run on a mechanical disk or an SSD with the disk write cache turned off, the multiple times will be several circles. How much your machine is worth, run e3_run.sh once and you'll know. The real heavyweights are in the last line of the table.

The literal meaning of O_SYNC is that each write must wait for the data and metadata to actually land on the hardware before returning. man 2 open writes it as having the same semantics as a write followed by an fsync, and it has a sibling that only guarantees data, called O_DSYNC, its counterpart is fdatasync, Linux has truly separated the two since 2.6.33. 256 MiB, 1 MiB block O_SYNC took 301.6 ms, seven times the steady-state of plain, but this is not the worst. We shrink the block to 4 KiB, total to 64 MiB: 20871.5 ms, throughput drops to 3 MiB/s. We divide 64 MiB by 4 KiB, getting 16,384 writes, each time paying a full round-trip to the disk, the average cost per write is about 1.3 milliseconds. The reason for dropping to 3 MiB/s is not O_SYNC itself, but O_SYNC with small blocks: this is the real situation in databases where each transaction must be persisted, and that's why they accumulate logs and batch writes. And the cost of small blocks plus direct writes is not machine-specific, the depth of 3 MiB/s is bound to this machine's about 1.3 millisecond round-trip latency, you just need to recalculate it based on your own disk when you rerun.

So the cost of persistence must be calculated by multiplying the delay of each sync by the number of times, and the relationship with the total amount is not that big. E4 will give you the delay for each time, here we check again by total: the same 256 MiB needs to be sent to the disk, plain delays the disk write until thirty seconds later, let the kernel do it for us, fsync pays it off on the spot, only about 20 ms more. The path of O_SYNC with small blocks slices a large transmission into 16,000 synchronous small transmissions, each time waiting for a complete round-trip to the disk, resulting in a comparison of 849 MiB/s vs 3 MiB/s, a difference of more than two hundred times.

> We also register the names on the other side: the one corresponding to fsync on Windows is called FlushFileBuffers, we have already mentioned it when discussing the msync mirror of file mapping in W02, for details, see the [W02 file mapping article](../../windows/file-io/02-file-mapping.md). The write-through mechanism corresponding to O_SYNC on the Windows side is also there, called `FILE_FLAG_WRITE_THROUGH`, for details, see the [W01 refresher experiment](../../windows/file-io/01-win32-file-io.md).

## E4: Where is the difference between fsync and fdatasync — three ways to dirty

The man 2 fsync says, we pick the two important sentences: fsync flushes the file's data and metadata together, and blocks until the device reports the transmission complete. fdatasync only requires the data to be written to the disk, and only the metadata that affects the correctness of subsequent reads must be followed: the change in file size must be flushed, the change in access time and modification time can be skipped. And on ext4, the metadata must go through the file system's journal before it can be written to the disk, so the saved part saves a round trip of the journal submission.

It's not interesting to recite these two sentences in the air, we construct three ways to dirty the file to specifically make the difference between the two syncs visible. The fixed approach is to expand to 1 MiB in advance and stabilize, each round pwrite 4 KiB to a fixed offset, only the data pages are dirty. The append approach is to pwrite 4 KiB to the end of the file each round, the data pages and the size are both dirty. The touch approach is to write no bytes at all, each round uses utimensat to change the timestamps, only the metadata is dirty. For each of the three ways to dirty, we pair two syncs, each round runs 200 times, and we only time the sync:

```cpp
// e4_metadata.cpp(节选):三种脏法,只给 sync 计时
for (long long i = 0; i < iters; ++i) {
    if (!strcmp(scen, "fixed")) {
        pwrite(fd, blk.data(), blk.size(), 4096);        // 固定偏移,大小不变
    } else if (!strcmp(scen, "append")) {
        off_t end = lseek(fd, 0, SEEK_END);
        pwrite(fd, blk.data(), blk.size(), end);         // 文件尾,大小在涨
    } else {                                             // touch:只改时间戳,不写数据
        timespec now[2]{};
        clock_gettime(CLOCK_REALTIME, &now[0]);
        now[1] = now[0];
        utimensat(AT_FDCWD, path, now, 0);
    }
    long long t0 = now_ns();
    if (datasync ? fdatasync(fd) : fsync(fd)) { perror("sync"); return 1; }
    per_op_ns.push_back(now_ns() - t0);
}
```

| Scenario | Sync | Three original values (µs/round) | Median |
|---|---|---|---|
| fixed (only data is dirty) | fsync | 257.0 / 248.5 / 250.2 | 250.2 |
| fixed | fdatasync | 246.2 / 246.0 / 246.2 | 246.2 |
| append (data and size) | fsync | 1013.8 / 1302.3 / 1391.5 | 1302.3 |
| append | fdatasync | 1356.6 / 863.6 / 1023.9 | 1023.9 |
| touch (only metadata is dirty) | fsync | 746.2 / 738.4 / 844.0 | 746.2 |
| touch | fdatasync | 156.9 / 159.6 / 161.2 | 159.6 |

The difference measured in the fixed scenario is only 250.2 vs 246.2, the 4 µs difference is completely submerged in the noise. Actually, this is not strange, we look at what is dirty: only the 4 KiB of data pages, the difference in timestamps on the metadata is negligible in this measurement, the two syncs do almost the same job.

The median ratio in the append scenario is 1302 to 1024, it looks like fdatasync is getting the benefit, we lay out the three original values again: fsync's three rounds are 1013.8, 1302.3, 1391.5, fdatasync's three rounds are 1356.6, 863.6, 1023.9, the two groups' intervals are completely overlapping, neither has a stable appearance. Here we truthfully record: the difference is within the noise, and a stable difference cannot be measured. However, the absolute value of both sides has risen to the 1 ms level — every time the file grows, the metadata of the size must be written to the disk, this job is something both syncs cannot avoid. The log of appending is brushed once per entry, paying this price, the append scenario is really expensive for this item.

The touch scenario is the one that stably opens the difference, fsync's three rounds are 746.2, 738.4, 844.0, fdatasync's three rounds are 156.9, 159.6, 161.2, the ratio per round is between 4.6 and 5.2, the median is 4.7. We look at the three rounds, and in each round, the difference is stably about four to five times, unlike the jitter measured on the same machine. The mechanism also matches: touch only dirties the inode's timestamp, the cost of fsync is to write this inode through the journal, fdatasync looks and sees that there is no metadata affecting the read, checks once and can finish, the 160 µs is basically the time it takes to check.

What do these numbers mean for us who are writing code? Most programs need something very simple: the data cannot be lost, the timestamp being late is not a big deal. In such scenarios, fdatasync is a free benefit, the more frequent the writes, the smaller the data per write, the more you get back. If you really need strict synchronization of metadata (such as audit requirements), you have to ask fsync to serve. There is also a supporting practice that is useful when creating a new file: the file must exist as an entry in the directory, just syncing the file is not enough, you also need to fsync the directory's fd once, this step's details are discussed in L01, we won't repeat it.

## E5: Crash window, how far can we demonstrate

We write here, the experiment we most want to do is the crash itself: write a piece of data, don't sync, then we unplug the power, see how many bytes are missing from the surviving file. We can't demonstrate it on this machine, the reasons are only two, both are given to you. One is that unplugging the power in WSL2 is equivalent to making the outer layer of Windows completely crash, the author can't find a safe way to create it, and it's not right to gamble with our work machine. The second is that to approximate the observation, you need to use root, /proc/sys/vm/drop_caches can throw away the entire page cache and force subsequent reads to go to the disk, its threshold is root, and the author's sudo requires a password, the experimental script probed with `sudo -n true`, and failed. In E5's output, it is recorded truthfully:

```text
$ ./e5_run.sh
== 步骤 0: sudo / drop_caches 可用性 ==
# sudo -n true: 不可用 —— 提示: sudo: a password is required
（脚本接下来两行是它自己的文字说明,交代 drop_caches 需要 root、真崩溃无法安全演示,咱们略去）
== 步骤 1: sync 压基线 ==
# 基线 Dirty=108 kB

== 步骤 2: dd 写 64MiB,不 sync,立刻观察 ==
#   67108864 bytes (67 MB, 64 MiB) copied, 0.0379661 s, 1.8 GB/s
# dd 返回后: Dirty=65644 kB(≈64MiB 脏页滞留), 文件大小 67108864 字节, 前 8 字节 0000000000000000
#            —— 文件此刻"完全可读",但这 64MiB 只在内存里,盘上一个字节都还没有

== 步骤 3: sync 之后 ==
# sync 返回后: Dirty=72 kB, 文件大小 67108864 字节, 前 8 字节 0000000000000000
#            —— sync 返回 = 内核已把这批脏页写回存储;从这一刻起,断电才不丢
```

These lines read together: dd took 0.038 seconds to write 64 MiB, Dirty rose to 65644 kB, which is exactly the 64 MiB amount. The file is currently completely readable, the size is accurate to the last byte, but no bytes are on the disk. And when sync returns, Dirty drops back to 72 kB, from the moment of return, power failure will not lose it. How long is the typical window? E1 has already measured it for us: thirty seconds. Thirty seconds is not a promise, it's the default knob's mouth, when the memory is tight, the kernel has another line of defense, dirty_ratio=20, the dirty pages pile up to about 10 GiB (the memory of this machine), the process that writes the data will be paused by the kernel, and only after the writeback catches up will it be allowed to proceed. If you read elsewhere that "data is definitely on the disk after thirty seconds," please help the author cross out that "definitely" in your mind.

We go to real engineering, there is only one thing left: the return value of fsync itself. If the writeback fails, the disk is full, hardware errors will lead to this step, and fsync returns -1, the common errno is EIO. man 2 fsync's ERRORS clearly states that it reports this error if an error occurs during synchronization, and this error may come from any fd writing to the same file. Whether the data is actually on the disk, this is your last chance to ask, only after asking can you be at ease. Our series of writing:

```cpp
sys_call("fsync", ::fsync, fd.get());   // 失败抛 system_error,what 带 "fsync:" 前缀
```

We hand the fd over to unique_fd to manage, the failure path also closes, the form of this writing is already defined in the [RAII section](../../thinking/01-raii-paradigm.md) and [error handling section](../../thinking/02-error-paradigm.md), we only cite it in this article. A sentence of advice from man 2 close is also related, the RAII section has fully explained it: the return value of close is not checked, I/O errors may silently slip through. If you really care about the data, the question you should ask is fsync, not close — this is the confidence of the RAII section: the return value of close in the destructor can be discarded, and fsync leaves it for the caller to ask.

## E6: dd's evidence and engineering trade-offs

Finally, we invite dd to the stage, and see where the costs fall in three modes: the default mode goes through the page cache, `oflag=direct` bypasses the page cache and writes directly (the O_DIRECT relative mentioned in L01, it comes with a series of alignment requirements), and `conv=fsync` is to run and then add an fsync. The total for one round is 256 MiB, we look at three rounds:

```text
$ ./e6_run.sh 256
# 口径: dd if=/dev/zero of=... bs=1M count=256,3 轮;吞吐取 dd 自报值

# round 1 buffered: pre_dirty=448kB post_dirty=262296kB dd_report=[1.8 GB/s]
# round 1 direct: pre_dirty=4kB post_dirty=4kB dd_report=[5.9 GB/s]
# round 1 fsync_only: pre_dirty=4kB post_dirty=248kB dd_report=[3.9 GB/s]
...（第 2、3 轮同型:buffered 吞吐 5.9、6.4 GB/s,跑完脏页 262408、262248 kB;
     direct 吞吐 5.1、5.7 GB/s,脏页 176、156 kB;fsync_only 吞吐 4.5、4.5 GB/s,
     脏页 104、100 kB;每轮另有一行 dd 原始报告,咱们略去）...
```

We lay out the numbers, and the reasoning becomes clear. The default mode dd reports 5.9 to 6.4 GB/s (the first round's 1.8 GB/s is the cold start price, we only look at the steady-state of the last two rounds), the moment it finishes, Dirty has 262 MB, the "disk speed" reported by dd is actually the memory speed, the real disk write is pending, thirty seconds later it is supplemented by the kernel, it's not dd's concern at all. The direct mode falls between 5.1 to 5.9 GB/s for three rounds (the first round's 5.9 is in the excerpt), the Dirty after completion is level with the baseline, each byte really went to the disk. The conv=fsync result is in the middle, steady at 3.9 to 4.5 GB/s, the time of the fsync is included in the numbers dd reports itself. On this machine, direct doesn't eat the throughput loss, relying on the VHDX base, direct on slow disks is often significantly slower, but the 262 MB of dirty pages left after the default mode completes is not machine-specific. You can see it clearly: the usual dd speed test you run at hand, the "instant" completion of large file copying, what we measured is mostly the page cache, not the real disk.

So, in engineering, when should we sync, and at what granularity? We convert what we measured in this article into a few actionable judgments. For transaction-level persistence, the WAL (write-ahead log, pre-write log) of the database, and the storage of orders, each transaction is paired with an fdatasync, the 1 ms level of the append scenario is the ceiling for single-threaded throughput, so the database is trying to do group commits, accumulating many transactions into one disk write. For configuration files that should not be lost after modification, after modification, add an fsync, and also add one for the directory, a few hundred microseconds, don't hesitate. Logs and caches that can be rebuilt if lost, plain is the way to go: the 30-second window facing the cost of rebuilding, in most scenarios, rebuilding is cheaper. The worst is the state stuck in the middle, each write wants to be synchronous, but uses small block writes, the 3 MiB/s from E3 is exactly how it comes. We either accumulate small blocks into large blocks before syncing, or just save the synchronization altogether, don't let your program stop at this configuration.

Finally, we stop at a real place. You now look back and run scenario A of E2 again: the 44 bytes written into the file, you can read them immediately, wait until the process dies and read again, they are still there. We wait for half a minute, the kernel's flusher will write them down, this E1 has already measured for you, from start to finish, no process has ever called sync. What happened after write() returned? The six experiments in this article give the scene: the data entered the page cache, Dirty increased, the life and death of the process has nothing to do with it, the kernel writes back according to its own schedule, if you want to know the exact time, fsync gives you a place where you can ask for the answer. The next article we return to the library level, and continue to talk about the directory and metadata of std::filesystem.

<ReferenceCard title="Reference Resources">
  <ReferenceItem
    :id="1"
    title="fsync(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/fsync.2.html"
  />
  <ReferenceItem
    :id="2"
    title="open(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/open.2.html"
  />
  <Reference项
    :id="3"
    title="write(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/write.2.html"
  />
  <ReferenceItem
    :id="4"
    title="_exit(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/_exit.2.html"
  />
  <ReferenceItem
    :id="5"
    title="exit(3)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man3/exit.3.html"
  />
  <ReferenceItem
    :id="6"
    title="proc_meminfo(5)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man5/proc_meminfo.5.html"
  />
  <ReferenceItem
    :id="7"
    title="Documentation/admin-guide/sysctl/vm.rst"
    publisher="The Linux kernel documentation"
    url="https://docs.kernel.org/admin-guide/sysctl/vm.html"
  />
  <ReferenceItem
    :id="8"
    author="Michael Kerrisk"
    title="The Linux Programming Interface"
    publisher="No Starch Press"
    :year="2010"
    url="https://man7.org/tlpi/"
  />
</ReferenceCard>
