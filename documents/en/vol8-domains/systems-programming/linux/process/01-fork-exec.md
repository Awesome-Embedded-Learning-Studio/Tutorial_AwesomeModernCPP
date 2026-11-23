---
title: "Process Creation and Lifecycle: fork/exec/posix_spawn"
description: "How a process is born, and how it exits with grace. This article puts fork's one-call-two-returns on /proc for live measurement (pid/ppid from both sides' views), proves copy-on-write with smaps_rollup's Pss (while the child has not written, parent and child each record 32915/32952 kB, shared pages counted at half each; after the child writes through 64 MiB it rises to 65684 kB, the two sides' sum being about 128 MiB, i.e., a physical copy, while the same variable's address never changed), and lands whole-table fd inheritance on a shared offset (parent and child relay their reads at the same offset; only a separate open starts from 0); a 1 GiB parent takes 31.6 ms to fork while posix_spawn holds constant at 0.48 ms — the cost sits in the page tables, not the pages (136x); all five exec variants land on execve under strace, execlp's PATH search is a user-space loop of 8 ENOENT misses before a hit, and fds without CLOEXEC survive exec with their offsets intact; the zombie's two-stage timeline S→Z→ENOENT, three reaping postures compared (blocking wait collects at 200/401/601ms, WNOHANG polling takes 33 polls with 30 empty-handed, SIGCHLD with SA_RESTART shows zero EINTR for the whole run versus 2 measured without it), exit codes keep only the low 8 bits (0x1234 arrives as 0x34); the orphan's adopter measured as pid 249 Relay rather than systemd, explained via the subreaper mechanism with the textbook account noted; posix_spawn's file_actions across three scenes (addopen redirection, adddup2 hand-off, and the addclose accident of ls reusing fd 0), with vfork covered only by quoting the man page rather than live-testing the danger; and finally the RAII child_process: spawn in the constructor, a four-tier destructor (SIGTERM invitation, SIGKILL after a hundred unheeded milliseconds, then the reaping), move-only handles cleaned 3-for-3 in a vector, with detach's price honestly recorded as State Z."
chapter: 8
order: 1
platform: host
difficulty: intermediate
cpp_standard: [20]
reading_time_minutes: 48
prerequisites:
  - "OS 资源的 RAII 范式:fd、HANDLE 与映射的同一副骨架"
  - "错误处理范式:从 errno 到 expected"
  - "POSIX 文件 I/O:open/read/write 与 fd 的一生"
  - "共享内存:shm_open 与映射"
related:
  - "守护进程、会话与环境"
  - "进程与作业:CreateProcessW 与 Job 对象"
tags:
  - host
  - cpp-modern
  - intermediate
  - 系统编程
  - POSIX
  - RAII
current_status:
  title: 正在打磨
  detail: 本批刚写完,正在通读打磨,表述与实测口径可能随时调整
  variant: polishing
translation:
  source: documents/vol8-domains/systems-programming/linux/process/01-fork-exec.md
  source_hash: 9c40af9e6eabe8b6d816b94b6e3c3e938a438da8b9985107cc6c1e67db3bfdf4
  translated_at: '2026-11-22T15:55:00+00:00'
  engine: glm-5.3
  token_count: 25048
---

# Process Creation and Lifecycle: fork/exec/posix_spawn

Back when the memory management chapter reached shared memory, an old supporting character had already been standing in our experiments without a formal introduction. The race experiment and the SPSC queue in [the shared memory article](../memory/03-shm.md) (SPSC means a single-producer, single-consumer queue) each started a child process, and both relied on the same call: fork. It got passed over in a single sentence at the time — we were busy with memory matters. This article moves it to center stage and walks the whole chain along a process's lifetime: how a process is born, what stays on and what gets replaced when it transforms, in how many steps it exits, and who takes care of the cleanup.

[L01](../file-io/01-posix-file-io.md) left us a thread to pick up. While discussing the fd table and open file descriptions, we said that after a fork, parent and child would relay their writes at the same offset, and saved the details for the process chapter. Today is the day to pick up that thread: what we saw then was the surface, and in this article we measure out the mechanism behind it.

The question starts from a small thing you do every day: you type a command in the shell, and a new process starts running. Where did it come from? When the command finishes and the process is gone, how did it leave? In this article we film the whole journey with /proc and strace — the semantics of fork, the transformation of exec, the reaping of zombies, the adoption of orphans — and finally wrap it all into an RAII child_process.

The experimental baseline is stated at the top, because every later number must be checked against it. All experiments come from my WSL2: the kernel is 6.18.33.2-microsoft-standard-WSL2, the CPU is an AMD Ryzen 7 9700X, the compiler is g++ 16.2.1, everything was built uniformly with `g++ -std=c++20 -Wall -Wextra -O2` and produced zero warnings, glibc is 2.44, and strace is 7.2. PID 1 on this machine is systemd — /proc/1/cmdline says /sbin/init — and that identity takes a twist in E4, which we will examine in detail then. The timing clock is CLOCK_MONOTONIC; WSL2 is a single-NUMA environment where perf is unavailable, so read the timing numbers by order of magnitude rather than by single-digit precision. The code and all raw output live in the six directories e1 through e6 under `code/volumn_codes/vol8/systems-programming/linux/process/01-fork-exec/` in the repository, with reproduction commands written in each directory's README (the letter suffix of a source file marks which experiment it is within its directory — e1d, for example, is the 4th in the E1 directory). The data files used by e1d, e2b, e2c, and e5b are hard-coded to `~/lp01_scratch`; please create that directory before rerunning. Experiment numbers E1 through E6 follow the directories; the numbering that the file I/O and memory chapters use within their own articles (exp1 through exp11, lowercase e1 through e5, uppercase E1 through E6) belongs to those articles alone and has nothing to do with ours — when browsing the archive, go by the directory numbers. As for `unique_fd` and `sys_call` defined in the two Thinking Foundations articles ([the RAII article](../../thinking/01-raii-paradigm.md) and [the error handling article](../../thinking/02-error-paradigm.md)): this article's experiments stay close to the raw shape of the system calls and use bare calls throughout; in E6 we bring the RAII skeleton back and build process-like resources a frame of the same lineage as `unique_fd`.

## E1: fork — One Call, Two Returns

fork's most famous property is that the same call returns twice. It sounds a bit mystical at first hearing; grounded in practice it means: after the call point, your code has two flows of execution, each holding its own return value. The E1 program printed its pid and ppid before and after the call; the raw output is pasted below (the two-line summary at the end elided):

```text
[调用前   ] pid=26157 ppid=26039,准备调用 fork()
[子进程侧] fork() 返回 0 —— 返回 0 说明我是新进程: pid=26158 ppid=26157
[父进程侧] fork() 返回 26158 —— 这是子进程的 pid: pid=26157 ppid=26039
[父进程侧] 子进程 26158 已收尸: WIFEXITED=1 WEXITSTATUS=0
```

Three things to see in this output. On the parent's side, fork hands back the child's pid 26158, which thereafter serves as the credential for kill and waitpid. On the child's side, what comes back is 0 — and please note its pid and ppid: 26158's father is exactly 26157; the world as each process sees it is self-consistent. The third point hides somewhere easy to overlook: the child's world begins the moment fork returns; it never executed a single line of the pre-call code, yet what it holds is a complete copy of the address space — even stdio's user-space buffer got copied. That is why the E1 source dutifully fflush'ed stdout before the fork: otherwise the characters accumulated in the buffer would be flushed once by each side at exit, and the same line would print twice. This temperamental buffer will come back for us later; when E6 builds the wrapper, it is the number-one nuisance. As for the two macros in the output's last line, we might as well announce their use now: WIFEXITED reports that the child exited normally, and WEXITSTATUS fetches the exit code; the full low-8-bit decoding comes in E3.

### Copy-on-Write: The Evidence Pss Provides

Nothing in what fork copies is physical memory. Said aloud, that sounds weightless, so let the data speak. The mechanism is named **COW** (copy-on-write): at fork time the kernel copies the page tables while the pages themselves stay where they lie, and both sides' page-table entries are marked read-only. Whoever writes triggers a page fault, in which the page gets copied — the writer takes the copy, and the other side keeps guarding the original.

The old problem of claims without evidence arises again, so the measurement method must be stated first. What we want to see is whether the physical pages split; the most direct route is reading the page-frame numbers in `/proc/self/pagemap`, but that requires root. We take the root-free equivalent evidence: the **Pss** (proportional set size) in smaps_rollup, defined back when [the memory layout article](../memory/01-memory-layout.md) covered smaps — shared pages are divided by the number of sharing processes, and each process records its fraction. Apply that definition to fork and the inference is clean: while neither parent nor child has written, that large batch of pages is shared by both sides and each page counts as half; once the child writes through the whole buffer, COW has copied every page, and the child's Pss rises back to the full amount. The E1 second experiment uses exactly this method: the parent dirtied a 64 MiB buffer page by page before the fork, and we read smaps_rollup once at each of five moments (Private_Clean stayed 0 throughout and is elided):

```text
全局变量 g_shared:地址=0x5eec80aab0a0 初值=100
64 MiB 缓冲:地址=0x761d333ff010 (逐页校验和=16384)
fork 前:    Rss=  69536 kB  Pss=  65813 kB  Private_Dirty=  65768 kB
父·fork后: Rss=  69600 kB  Pss=  32952 kB  Private_Dirty=     44 kB
子·未写:   Rss=  67204 kB  Pss=  32915 kB  Private_Dirty=     36 kB
子·写后:   Rss=  67268 kB  Pss=  65684 kB  Private_Dirty=  65572 kB
父·子退后: Rss=  69600 kB  Pss=  65814 kB  Private_Dirty=  65768 kB
……(子进程出生、写完 g_shared、退出后父进程复读的三行,以及结尾的解读,删节)……
```

The numbers tell the whole story. Before the fork, the parent alone holds the 64 MiB, and Pss records 65813. After the fork, with nobody having written, the two sides' Pss drop to 32952 and 32915 — each side recording about half — and Private_Dirty shrinks to a few dozen kB: the pages are shared between the two. The child then writes through the buffer; its Pss rises to 65684, essentially the full amount, while on the parent's side, read again after the child exits, Pss returns to 65814. Add the two sides at the last moment and you get about 128 MiB — the physical memory really was copied once. We kept the addresses too: `g_shared`'s address is 0x5eec80aab0a0 on both sides from start to finish; the child changed it to 200, and the parent still reads 100. The address never changed, yet the values split — that is the complete scene of copy-on-write.

> One more exception for the record: in [the virtual memory API article](../memory/02-vm-apis.md) we tested MADV_DONTFORK live — a segment carrying this flag simply vanishes entirely in the child. Who is it for? DMA-class hardware works with physical addresses, but when COW copies a page, the page moves to a new physical address; hardware doing DMA straight from the old address would no longer find the data. Making the segment invisible to the child simply removes the trouble.

### Whole-Table fd Inheritance: The Table Is a Copy, the Offset Is Singular

Now to finish the sentence L01 left behind. A one-line review of the two-level fd structure: each entry of the process-private fd table points to a system-level **open file description**, and it is on the latter that the offset lives. What fork copies is the fd table itself; the open file descriptions that the entries point to remain the same ones. The E1 fourth experiment measures this for you: the parent opens a file and reads to offset 16; after the fork the child continues reading with the inherited fd; after the child finishes, the parent reads again:

```text
[父] 打开 fd=3 —— 这一个 fd 背后是一份打开文件描述,偏移量只有一份
[父] 先读 16 字节:0123456789:;<=>? 偏移=16
[子] 用继承的 fd=3 读 16 字节:@ABCDEFGHIJKLMNO 偏移=32(接着父的 16 往下读)
[子] 自己另 open 的 fd=5 读 16 字节:0123456789:;<=>? 偏移=16(从 0 重新开始)
[父] 子读完后我再读 16 字节:PQRSTUVWXYZ[\]^_ 偏移=48(子进程的读把我的偏移也推走了)
```

Watch three relays and one control. The parent reads 16 bytes, offset reaching 16; the child reads again with the inherited fd and gets bytes 16 through 31, showing it picked up the offset the parent left behind. When the parent reads next, it gets 32 through 47 — the child's read pushed the parent's offset along too, because both sides write the same number inside the same description. The control line is the most interesting: the child opens the same file itself, receives fd=5, reads bytes 0 through 15, and its offset starts from zero. A new open creates a new open file description, going its own way apart from the parent's. Set this scene against the dup of L01 and it clicks: dup copies a table entry and shares the description — a single slot's business — while fork does the whole-table edition of the same thing. L01 showed relaying writes at the other side's offset; what we demonstrate here is relay reading. Both reading and writing push the same offset — whoever moves the number in the description, the other side sees it.

## The Cost of fork: A 1 GiB Parent Takes 31.6 ms

COW spares fork the work of copying pages, so what is still expensive about fork? We measure directly. The E1 third experiment fed the parent up to 1 GiB (VmRSS=1052516 kB after page-by-page touching), then measured four creation methods, each warmed up for 3 rounds and timed over 30, median taken:

| Creation method | Small parent | 1 GiB parent |
|---|---:|---:|
| fork, creation only | 232.6 µs | 31585.5 µs |
| vfork, creation only | 71.2 µs | 74.8 µs |
| fork+exec /bin/true | — | 32117.8 µs |
| posix_spawn /bin/true | 486.2 µs | 480.7 µs |

The most eye-catching row is fork: 232.6 µs for a small parent, shooting straight up to 31585.5 µs for a 1 GiB parent — a 136-fold difference. Not a single page was copied (COW just proved that), so where did those 31.6 ms go? Into the page tables. A page-by-page-touched 1 GiB is 262144 4 KiB pages, each with its own entry in the page tables; for the child's address space to become usable, that entire page-table set, together with the kernel's accounting structures, must be copied once. So fork's cost tracks the parent's **touched** memory, not the requested amount — virtual address space that is only requested and never touched does not drive the cost up.

The bottom two rows finish the other half of the story. vfork and posix_spawn are nearly insensitive to the 1 GiB parent — their 74.8 and 480.7 µs sit right against the small-parent numbers — because they do not copy even the page tables; we verify that mechanism in the strace section of E2. The 480.7 µs of posix_spawn also contains the cost of the exec itself, and we can cross-check with numbers already in the table: fork+exec's 32117.8 minus bare fork's 31585.5 leaves 532 µs, the same order as posix_spawn's 480.7 — the difference is exactly the exec plus dynamic linking for one run of /bin/true. Two independently measured numbers agree, and that is what gives us the confidence to use this table.

## E2: The exec Family — Five Variants, One Relay Chain

The child born of fork looks exactly like its father, but we usually start processes to run **other programs**, and that is when exec takes the stage. exec is a large family; the names of the five common variants follow a pattern: a trailing `l` means list — the arguments are enumerated one by one in the call; `v` means vector — the arguments are packed into an array; `e` means you supply the environment-variable array yourself; and `p` means you gave only a filename and it does the searching itself. The E2 first experiment chained the five variants into a relay: the program starts by transforming with execl, the new program hands off the next baton with execv, and so on down to execvp — all five legs completed (program paths in the output shortened to …; elisions inside the block are marked separately):

```text
起点:pid=26434,程序 …/build/e2a_exec_variants
  [probe] execlp(e2a_exec_variants) 失败:No such file or directory(PATH 不含本目录时它真的搜不到)
第 1 棒 execl:完整路径+参数逐个列+NULL 哨兵,变身开始
第 2 棒到达:经 execl 变身,pid=26434 还是同一个进程
      交棒 execv:完整路径+参数打包成 char* 数组
……(probe 的第二条证实行,第 3、4 棒的到达与交棒,第 5 棒的到达与交棒,删节)……
终点:经 execvp 变身。当前 pid=26434,出发时 pid=26434,五次变身 pid 从未变
      出发时塞进环境的变量一路带到终点:exec 换的是程序映像,进程还是那个进程
```

The pid is 26434 the whole way through — that is exec's identity statement: it swaps out the entire program image; code, data, heap, and stack are all new, while the structures that represent the process in the kernel's eyes stay on as they are, and the process itself remains the same process. Leg 4 hides a foreshadowing detail: that execve leg stuffed in the custom environment variable E02A_VIA, which traveled all the way to the finish. The environment is replaced along with the image, and it stays along with the image. The probe failure in the first line also deserves a pause: at startup, PATH did not contain the program's own directory, so execlp could not find itself; the program then prepended its directory to PATH, and only then could legs 4 and 5 be found. The `p` series recognizes only PATH, never the current directory — use it to run a program under a relative path and nine times out of ten you will come up empty.

### CLOEXEC Life and Death, and the Continuity of the Offset

exec replaced the image — what happens to the fd table? [L01](../file-io/01-posix-file-io.md) gave the semantics when it covered the FD_CLOEXEC flag; here we add live shots from before and after an exec. The E2 second experiment opened two fds on the same data file: fd 3 bare, fd 4 carrying O_CLOEXEC. Before the exec we read 8 bytes through fd 3, bringing the offset to 8, and then transformed:

```text
[exec 前] pid=26436,fd 3(普通)与 fd 4(O_CLOEXEC)指向同一个文件
[exec 前] 先从 fd 3 读 8 字节=01234567,偏移=8
[exec 前] 现在调 execl 换一个全新的程序映像……
[exec 后] pid=26436 —— 与 exec 前打印的是同一个数
……(exec 前后的 /proc/self/fd 清单:0/1/2 照旧、fd 3 都在、fd 4 只在 exec 前,删节)……
[exec 后] fd 3 还能用:再读 8 字节=89:;<=>?,偏移=16(接着 exec 前的位置读)
[exec 后] 带 O_CLOEXEC 的那个 fd 不在清单里了:同一个文件,一个穿过 exec 一个被关
```

The fd table survived the exec along with the process. fd 3 is not only still there; its offset continued from 8 to 16 — precisely the continuation of that E1 subsection: the open file description the entry points to was never swapped, so of course the offset remains singular. fd 4 vanished on the spot; at the moment the exec succeeded, the kernel closed it for us. Two fds opened on the same file: one stayed, one was closed, and the difference is a single flag bit. This is also the measured face of L01's advice: in a multithreaded program, open followed by a make-up fcntl has a race window, whereas new code that passes O_CLOEXEC directly at open reduces that window to zero.

### Failure Is an Ordinary Return: exec Must Be Followed by an Exit Path

A successful exec is one-way: the new program runs from the top of main, and the old program's code is no longer in the address space. But what if it fails? In the E2 third experiment we tried three deaths in a row:

```text
活着:pid=26437,开始三次注定失败的 exec
1) execve(路径不存在) 返回 -1,errno=2(No such file or directory)
2) execv(文件在、没 x 权限) 返回 -1,errno=13(Permission denied)
3) execlp(PATH 里翻不到) 返回 -1,errno=2(No such file or directory)
三次失败后我还活着:pid=26437 —— exec 只有成功才有去无回,失败就是普通的函数返回
```

Look at the three results: all returned -1, each with its errno properly in place, and the process kept running unharmed. So in a fork+exec child, the perror plus `_exit` that follow the exec are the only failure path that will ever be executed. Convention reserves 127 for not-found and 126 for no-permission, and the shells follow exactly that numbering. If you write an exec with nothing after it, a failed child continues running with the parent program's memory, most likely re-running the parent's subsequent logic — an accident that is extremely hard to read at the scene.

### strace Forensics: Which Is the Real System Call

One question was left open above: who performs execlp's PATH search. The E2 fourth experiment ran under strace's watch, with strace admitting only four families of calls — execve, execveat, clone, and clone3. We paste the crucial lines of the log (strace renders the Chinese arguments as octal escapes; we converted them back to characters, and environment-pointer fields are elided):

```text
26571 execve("/bin/echo", ["echo", "[e2d] 经 execl 到达 echo"], …) = 0
26572 execve("/home/charliechen/.local/bin/echo", …) = -1 ENOENT (No such file or directory)
……(PATH 里各目录逐个尝试、逐个 ENOENT,共 8 次,删节)……
26572 execve("/usr/sbin/echo", ["echo", "[e2d] 经 execlp(PATH 搜到 ech"...], …) = 0
26570 clone(child_stack=NULL, flags=CLONE_CHILD_CLEARTID|CLONE_CHILD_SETTID|SIGCHLD, …) = 26571
26570 clone3({flags=CLONE_VM|CLONE_VFORK|CLONE_CLEAR_SIGHAND, exit_signal=SIGCHLD, stack=0x76150d8e9000, stack_size=0x9000}, 88) = 26574
```

We hold four pieces of evidence; take them one by one. First: the code says execl, but the kernel log shows only execve — the four suffixes l, v, e, p are all libc wrappers, and the only real system call left is execve; execveat with its directory handle never comes into play for us. The PATH answer is in the next piece: execlp's search is a **user-space loop**. libc assembles the full path from each directory in PATH and tries execve one by one; this round ran into 8 ENOENTs before hitting /usr/sbin/echo. The search never troubled the kernel — what we see is a string of failed system calls. Next, fork and posix_spawn: the fork() in code lands as clone at the system-call layer; the two CLONE_CHILD-prefixed flags in the flags are marks left by the thread library, and SIGCHLD is the signal sent to the father when the child exits. The most interesting is the last line: posix_spawn goes through clone3, with CLONE_VM and CLONE_VFORK among its flags.

**clone3** gets a proper introduction for the first time. It is clone's newer interface, in the kernel since Linux 5.3; the parameters are no longer spread into a long list but packed into a struct and passed as one, and the pair of curly braces in the log is strace expanding that struct for us. The crux is CLONE_VM: parent and child **share the same address space**, so naturally not one page table needs copying — this is exactly the mechanical landing point of posix_spawn's insensitivity in the E1 timing table. CLONE_VFORK, meanwhile, means the father is suspended until the transformation completes — waiting precisely for the child's exec, borrowing vfork's fast lane. When the IPC article builds popen, we will meet this very clone3 again in strace, and you will recognize it on sight.

## E3: Zombies and Reaping — Exit Happens in Two Steps

What happens after a child dies — ninety percent of this question's trouble comes from nobody telling us it is a two-step affair. The first step is the exit itself: exit or `_exit` releases the memory and closes the fds, but the task_struct representing the process in the kernel, along with the exit code, **stays behind**, kept for the father to collect. That left-behind husk is the **zombie** (a process that has died but not yet been waited on); the `<defunct>` you see in ps and the state Z in /proc are its two faces. The second step is our wait or waitpid: the father takes the exit code away, only then is the task_struct truly destroyed, and the /proc entry vanishes on the spot. The E3 first experiment photographed all three moments between the two steps:

```text
[子] pid=26439,先睡 300ms,给父进程留时间读我的 State
[子] 现在调 _exit(42):释放内存、关 fd,但 task_struct 和退出码留下
[父][t1 子还活着] State:	S (sleeping)
[父][t2 子已退、未收尸] State:	Z (zombie)
[父] 同一时刻 ps 眼里:
        PID    PPID STAT COMMAND
      26439   26438 Z    e3a_zombie_life <defunct>
[父] 现在才 waitpid 收尸(第二阶段)……
[父][t3 收尸完成] WIFEXITED=1 WEXITSTATUS=42(退出码从僵尸残骸里取出)
[父] 再读 /proc/26439/status:fopen 失败(No such file or directory)——进程彻底消失,双阶段结束
```

The three moments we see: State goes from S to Z, and finally to the disappearance of the whole /proc entry. Think about why the kernel keeps this husk: the exit code exists in one copy, and the father may want it — the kernel cannot throw it away on the father's behalf. A zombie is not a defect, actually; it is just part of the mechanism. The real defect is skipping the reaping. The pid a zombie occupies must be returned; if the father never comes to collect, the child hangs in the process table forever. A runaway server program that spawns hundreds or thousands of children and reaps none fills the process table with zombies — this is the step where it stumbles.

### Three Reaping Postures, One Scenario

Knowing the reaping must happen, how to reap becomes an engineering question. The E3 second experiment staged the same scene: three child processes exiting staggered at a 200ms cadence, reaped with three different postures; we paste one excerpt of each output:

```text
=== 姿势一:阻塞 wait —— 父进程什么都干不了,子进程死一个醒一次 ===
  t=200ms 收到 pid=26470 exit=11
  t=401ms 收到 pid=26471 exit=22
  t=601ms 收到 pid=26472 exit=33

=== 姿势二:waitpid(WNOHANG) 轮询 —— 不阻塞,但要自己一遍遍问 ===
  t=802ms 第 11 次轮询:收到 pid=26473 exit=11
  ……(t=1003ms 与 t=1204ms 的两轮同构,删节)……
  合计轮询 33 次,其中空手而归 30 次——轮询的代价是空转与延迟的折中

=== 姿势三:SIGCHLD 处理器 + SA_RESTART(read 自动重启) ===
  t=1405ms read 拿到字节 a(SA_RESTART 把被信号打断的 read 自动续上了)
    [SIGCHLD 处理器] 收尸 pid=26476 exit=20
  ……(字节 b、c 的两轮同构,以及无 SA_RESTART 的对照组与 SIG_IGN 附送场景,删节)……
  管道活干完(3 字节),处理器共收尸 3 个,EINTR 发生 0 次,僵尸清零
```

Posture one: the father uses blocking wait, and the children die and get collected one by one at the 200/401/601ms cadence; the price is that between two deaths the father can do nothing else. Posture two changes the price: waitpid with WNOHANG does not block — it returns immediately, returning zero if nobody has died — so the father must ask over and over; this round took 33 polls to collect all three, 30 of them empty-handed. What balances busy-spinning against reaping latency is exactly the poll interval. Posture three is the event-driven route: when a child exits, the kernel sends the father SIGCHLD; the father was minding its own business reading a pipe, and when the signal arrives, the handler reaps in passing. A `while (waitpid(-1, …, WNOHANG) > 0)` loop in the handler nets everything — however many arrive together, all get collected.

Posture three hides the SA_RESTART and EINTR that [the error handling article](../../thinking/02-error-paradigm.md) covered — that old pair of acquaintances shows up together. What the handler interrupts is the father's read; with SA_RESTART set, the kernel automatically re-issues the read for us after the handler returns, the father's code never sees the interruption, and the total EINTR count for the run is zero. The experiment also ran a control without SA_RESTART: the interrupted read returned -1 with errno EINTR, measured exactly 2 times, leaving manual retry loops as the only recourse. Let us also record one piece of handler discipline: only async-signal-safe functions may be used inside a handler — write and waitpid are on the whitelist, printf is not — and the experiment source assembled its digits with hand-rolled write. The full list of this discipline gets its own dedicated table in the signal article.

### The Exit Code Keeps Only the Low 8 Bits

The corpse is collected — how should you read the code that comes out? The E3 third experiment lined up four deaths, all observed with the same waitpid, told apart by a few macros:

```text
四种死法,同一个 waitpid,靠宏分辨:
  _exit(42)                pid=26505   WIFEXITED=1   WEXITSTATUS=42 (0x2A)
  _exit(0x1234) 截断     pid=26506   WIFEXITED=1   WEXITSTATUS=52 (0x34)
  [子] abort() 前先喊一嗓子
  abort() 自毁           pid=26507   WIFSIGNALED=1  WTERMSIG=6 (SIGABRT)
  父进程 kill -9        pid=26508   WIFSIGNALED=1  WTERMSIG=9 (SIGKILL)
```

Look at the star of the second row: the program calls `_exit(0x1234)`, and the father's WEXITSTATUS is 52 — 0x34 in hexadecimal. The original text of man 3 exit puts it plainly: of the status passed to exit, only the lowest byte — the result of status bitwise-AND 0xFF — reaches the father's hands; the 0x12 half of 0x1234 is lost along the way. If you use the exit code to carry information beyond 255 — say, treating a byte count as an exit code — the high eight bits vanish silently, and the compiler will not say one word about this kind of error. The shell's `$?` follows the same 8-bit convention: exit 256 and exit 0 are hard to tell apart from a script's point of view.

Rows three and four are another kind of death: the process was executed by a signal, and the reading switches to WIFSIGNALED plus WTERMSIG — abort() corresponds to signal 6, kill -9 to signal 9. The raw status word waitpid hands back is multiplexed between exit code and signal; if you skip the WIFSIGNALED check and go straight to WEXITSTATUS, a child killed by a signal reads as a bizarre exit code. The decoding order is always: let WIFEXITED and WIFSIGNALED pick whichever hits, then fetch the corresponding field.

### Setting SIGCHLD to SIG_IGN: The Kernel Reaps for Us

There is also a one-size-fits-all route: explicitly setting SIGCHLD to SIG_IGN — an explicit setting, that is, not the default ignoring. The original text of man 2 wait says that afterwards children no longer become zombies on exit, and waits through wait and waitpid continue until all children are dead, then return failure with -1 and ECHILD. Our measurements agree: 200 ms after the child's `_exit(55)`, it was already gone from /proc — we never called wait once — and a subsequent waitpid returned -1 plus ECHILD. The price is right on display: the exit code is gone forever. When E6 discusses detach, we will come back to this once more.

## E4: Orphans and Adoption — The Textbook Says PID 1, We Measured 249

When the father dies before the child, the child becomes an orphan, and the textbook's standard answer is that orphans get adopted by init (PID 1). We do not recite the book's answer; we measure it ourselves. The experiment's formation is three generations: A is the observer, which forked the temporary father B; B forked C and then exited the stage at once, leaving C fatherless from then on:

```text
[A] 观察者 pid=26512
[A] 本机 PID 1 是什么:/proc/1/comm = systemd
[A]               /proc/1/cmdline = /sbin/init
[C] 出生:pid=26514 ppid=26513(临时父 B)
[B] 临时父 pid=26513,C 已出生,我退场——C 从此没爹
[A] B 已被 A 收尸。A 试着多管闲事去 wait 孙辈 C(pid=26514):返回 -1,errno=10(ECHILD:不是我的孩子,没资格)
[A] 只能旁观:
[A] t+0.0s:State:	S (sleeping)
……(孤儿 C 的一句自述删节)……
[C] ppid 变成了 249,收养者的 comm = Relay(252)
[A] t+1.1s:/proc/26514 消失(ENOENT)——C 退出后没以僵尸滞留,收养者收了尸
[A] 机制:C 的 ppid 实测变成 249(Relay(252)),不是 PID 1(systemd)!
……(机制段的续四行,删节)……
```

PID 1 on this machine is unmistakably systemd, yet the orphan's measured ppid became 249, and the adopter's comm is Relay(252). Who is 249? It is the WSL session-level /init relay, under whose name every shell in this session hangs. By what right does it adopt? By its identity as a **subreaper**. Let us unpack the word: prctl has a PR_SET_CHILD_SUBREAPER option, supported by the kernel since 3.4, and a process marked with it gains adoption eligibility. The original text of man 2 prctl says that from then on, whenever one of its descendants becomes an orphan, the child is passed to the nearest living subreaper. PID 1 is merely the fallback for when no subreaper can be found. The WSL session /init happens to have marked itself, so our orphan never reached systemd. The textbook sentence holds in environments directly attached to init with no intermediate subreaper; when writing code, please do not treat ppid equal to 1 as the criterion of adoption — in environments like containers and desktop sessions, subreapers are everywhere.

Why did the orphan not become a zombie? The output gives the answer: 1.1 seconds after C's exit, /proc already reported ENOENT — no Z-state lingering at any point. A zombie is by definition dead and un-waited, while the adopter is a process that always waits; once the child is in its hands, exit and reaping are practically seamless. The ECHILD that A received is also useful: the observer wanted to reap a grandchild, and the kernel refused outright — reaping eligibility recognizes only direct parent-child relations; even one generation apart is not allowed.

> The first version of this experiment died mysteriously: A closed the read ends of the two report pipes too early, the grandchild's write into the pipe took a SIGPIPE straight away, and it died silently midway — the adoption line in the output vanished into thin air. Only after attaching strace did I see the cause of death clearly; the fix was to grip the read ends firmly until the second read was finished. The experiment's apparatus itself became a live example of the SIGPIPE mechanism, and we will summon it back as evidence when the IPC article covers pipes.

## E5: posix_spawn — Done in One Shot, and Why vfork Should Not Be Hand-Written

With fork's and exec's mechanisms both measured, we turn back to choosing tools. The E5 first experiment sent the same task down two roads: task one runs printenv with a custom environment; task two runs `sh -c "exit 7"` to observe the exit code — each executed once through fork+execve and once through posix_spawn. The results came back completely identical: both printenv runs returned hello-spawn-env, and both exit codes were 7 — no behavioral difference whatsoever. The difference is all in what the code looks like:

```cpp
// fork + execve 三件套(节选):分叉点、失败路径、收尸,一样都不能少
pid_t pid = fork();
if (pid == 0) {
    execve(path, argv, envp);   // 只有失败才会返回
    _exit(127);                 // 127 = 找不到,习惯法
}
if (pid < 0) { /* fork 本身失败 */ }
int raw = 0; waitpid(pid, &raw, 0);   // 第三件:收尸

// posix_spawn:一个调用,上面三件事都在里面
// (e5a 的实际源码里 file_actions 与 attr 两个参数传的都是 nullptr,全形态下一节出场)
int rc = posix_spawn(&pid, path, nullptr, nullptr, argv, envp);
```

We have written all three pieces of the trio, and every piece has its branching paths; the child's code and the parent's code crowd into the same function, diverging on the value of pid. posix_spawn gathers these into one call, borrows its speed from the clone3 fast lane seen in E2, and has the E1 timing table vouching for it — the speed is independent of the parent's memory size. As for error reporting, we state it per the glibc documentation and did not measure it in this article: the error code's carrier switches to the **return value**, not following the errno convention; per the documentation, even the child's failure in the exec phase is reported back through this return value, the delivery riding glibc's internal machinery.

### file_actions: Redirection Grows on the Child

Now look at another trick of posix_spawn: file_actions, one that fork+exec does not imitate well. It is a list we hand over in the call, specifying fd operations performed on the child's behalf **after its birth and before exec**; all the redirection work thereby falls on the child, and the parent need not open, close, or later restore a single fd of its own. The E5 second experiment laid out three scenes (file paths in the output shortened to …; the permission, owner, and date columns of the ls lines elided; fd numbers and link targets verbatim):

```text
场景一 addopen:子退出码=0,stdout 被换进了文件:
  …/e5b_out1.txt 的内容:场景一:我明明是 echo,输出却进了文件
场景二 adddup2:子退出码=0(父进程的 fd 3 没动,还是开着的):
  …/e5b_out2.txt 的内容:场景二:输出经父进程开的 fd 落地
场景三 对照一,不做任何动作,子进程 ls 自己的 fd 清单:
  0 -> /dev/null   1 -> …/e5b.out   2 -> …/e5b.out   3 -> /proc/26523/fd
场景三 对照二,addclose(0) 之后,同一清单:
  0 -> /proc/26524/fd   1 -> …/e5b.out   2 -> …/e5b.out
```

Scene one uses addopen: between its birth and exec, the child itself opened the output file and installed it into fd 1; echo knew nothing about it, and the output went into the file. Scene two uses adddup2: the parent opened the fd and handed it to the list; what the list executes on the child is a dup2 — install into slot 1, then close the original fd — while the parent's own fd 3 is never touched. Scene three is the control group, and we preserved both surprise scenes for you in the archived output. The first surprise involves sh: to demonstrate addclose(0), we could not wrap things in sh -c for convenience, because a shell that finds a gap among its three standard fds at startup mends them automatically to /dev/null — the demonstration would have been self-healed away by the shell. Hence scene three runs ls directly, bypassing the shell. The second hides in the second listing above: addclose(0) vacated slot 0; ls's own opendir needed a directory handle and picked exactly the lowest free number — slot 0 got reused. The link target /proc/26524/fd exposes the whole story: that is no longer the inherited stdin.

### vfork: Temptingly Fast, but the man Page Talks You Out of It

In the E1 timing table, vfork also took only 74.8 µs for the 1 GiB parent, sharing posix_spawn's CLONE_VM fast lane. But we do not test its behavior live in this article; for the dangerous part we quote only the original text of man 2 vfork — four sentences copied whole below, each carrying its own warning:

```text
"vfork() differs from fork(2) in that the calling thread is suspended until the child terminates (either normally, by calling _exit(3), or abnormally, after delivery of a fatal signal), or it makes a call to execve(2)."
"Until that point, the child shares all memory with its parent, including the stack."
"The behavior is undefined if the process created by vfork() either modifies any data other than a variable of type pid_t used to store the return value from vfork(), or returns from the function in which vfork() was called, or calls any other function before successfully calling _exit(3) or one of the exec(3) family of functions."
"This system call will be eliminated when proper system sharing mechanisms are implemented. Users should not depend on the memory sharing semantics of vfork()."
```

Take them one by one. The first sentence speaks of suspension: the calling thread is suspended until the child terminates or execs. The second speaks of sharing: in the window before a successful exec, child and father share all memory — even a single shared stack. The most ruthless is the third: if the child modifies anything beyond the pid_t holding the return value, or returns from the function that called vfork, or calls any other function before a successful `_exit` or exec, the behavior is undefined. The fourth is the 4.2BSD manual's verdict — translated, it says that once proper sharing mechanisms are implemented, this system call will be eliminated in the future, and the sentence right after advises everyone not to depend on its memory-sharing semantics. What does sharing one stack mean? One extra function call by the child before exec writes on the very stack where the father lies suspended, putting return values and local variables on the same rope. POSIX.1-2008 has already dropped the vfork specification. So our path follows the man page: use fork when you need isolation, use posix_spawn when you need speed, and let glibc drive the fast lane — as for legitimate scenarios of calling bare vfork in application code, I cannot think of a single one.

## E6: child_process — Wrapping the Whole Lifecycle in a Type

The mechanisms are all told, and the closing work is our old trade. [The RAII article](../../thinking/01-raii-paradigm.md) built the same move-only skeleton for fds, handles, and memory mappings; for a resource like a process, we build along that same skeleton. The child_process E6 builds does the spawn in its constructor and completes E3's reaping in its destructor; the core is two pieces:

```cpp
class child_process {
public:
    child_process(const std::string& path, std::vector<std::string> args, bool detached = false) {
        std::fflush(stdout);  // 孩子直写同一个 fd,咱们的用户态缓冲不冲出去就会反超
        // ……(argv 从 string 到 char* 的搬运,与 attr 的设置,删节)……
        int rc = posix_spawn(&pid_, path.c_str(), nullptr, &attr, argv.data(), environ);
        if (rc != 0)
            throw std::system_error(rc, std::generic_category(), "posix_spawn(" + path + ")");
        detached_ = detached;
    }

    ~child_process() { dispose(); }

    child_process(const child_process&) = delete;      // 进程句柄是独占的,与 unique_fd 同理
    child_process& operator=(const child_process&) = delete;
    child_process(child_process&& other) noexcept;     // 移动交接句柄,源对象归 -1
    // ……(wait/try_wait/detach/terminate/alive,删节)……
private:
    void dispose() noexcept {
        if (pid_ <= 0) return;                       // 第一档:手里没孩子,没事
        if (detached_) { /* 第二档:已解除承诺,不管 */ }
        int raw = 0;
        if (waitpid(pid_, &raw, WNOHANG) == pid_) {  // 第三档:已死未收,只差收尸
            pid_ = -1; return;
        }
        (void)kill(pid_, SIGTERM);                   // 第四档:还活着,礼貌地请它退场
        for (int i = 0; i < 10; ++i) {               // 给 100ms 优雅退场
            if (waitpid(pid_, &raw, WNOHANG) == pid_) { pid_ = -1; return; }
            usleep(10000);
        }
        (void)kill(pid_, SIGKILL);                   // 不肯退,强制手段
        if (waitpid(pid_, &raw, 0) == pid_) { /* 收尸 */ }
        pid_ = -1;
    }
    pid_t pid_ = -1;
    bool detached_ = false;
};
```

The constructor goes through posix_spawn — the safe channel E5 verified for us; on failure it throws system_error and never leaves behind a half-constructed object. The fflush at the top is forced by stdio's buffering: the child inherits the same output fd; if our buffer holds unflushed characters while the child's output writes straight to the fd, the two sides' outputs interleave. That fflush in E1, and the `setvbuf(stdout, nullptr, _IONBF, 0)` at the top of [the file lock article](../file-io/05-file-lock.md), fight the same trouble: stdio's user-space buffer knows nothing of process boundaries, and when the buffered characters get written out is something we must manage ourselves. The destructor's dispose has four tiers — no child in hand, promise released, dead but unreaped, and still alive — each taking its own path. For the alive tier, the opening move is SIGTERM with a full 100 ms to exit gracefully; if it refuses, SIGKILL kills it forcibly, and a blocking waitpid finally collects the corpse clean. No exceptional path leaks. wait and try_wait decode E3's status word into exit_status, with the WIFEXITED and WIFSIGNALED checks wrapped inside — what the caller receives is plain human language.

Of the five demonstrations, we pick three:

```text
=== 演示 1:作用域结束自动收尸,不留僵尸 ===
spawn /bin/sleep 30 → pid=26526
  作用域内:State:	S (sleeping)
  [~child_process] pid=26526 还活着 → SIGTERM
  [~child_process] pid=26526 SIGTERM 生效,收尸完成
  作用域已出,验证 /proc/26526:进程没了,僵尸零残留

=== 演示 3:move-only,进程句柄进容器 ===
  static_assert 三条全过:拷贝删除,移动可用
  第 1 个孩子 pid=26528,被移动后原对象 pid()=-1(句柄已交接)
  ……(第 2、3 个孩子的同构出生行,删节)……
  离开作用域,vector 析构逐个清理:
  [~child_process] pid=26528 还活着 → SIGTERM
  [~child_process] pid=26528 SIGTERM 生效,收尸完成
  ……(26529、26530 各两条同构收尸行,删节)……
  验证:3/3 个孩子全部从 /proc 消失,零僵尸

=== 演示 5:detach——放它走,以及它必须知道的代价 ===
spawn detached /bin/sleep 2 → pid=26532(已在新进程组)
  作用域内:State:	S (sleeping)
  [~child_process] pid=26532 已 detach,析构不管它
  作用域出了,它还活着吗?还在跑
  等 2.5s 让它自然退出。我们承诺过不 wait,它死在我们前面会怎样:
  它退出后的状态:State:	Z (zombie)
  僵尸出现了:detach 只是解除承诺,内核不会替我们 wait
  ……(尾行的两条免责建议删节,正文下一段展开)……
```

Watch the everyday life of demonstration 1: the spawned child outlives the scope, the destructor sends SIGTERM and reaps, and /proc is left clean. Demonstration 3 verifies move-only: static_assert writes the deleted copy into compile time, the three children enter the vector via std::move with the source objects' pid going to -1, and when the scope ends the container's destructor cleans them up one by one — 3/3 all gone. This is exactly the RAII article's move semantics replayed on processes: the handle is exclusive, transferable but not shareable.

I deliberately preserved demonstration 5's output verbatim. What detach releases is the promise to reap (along the way, POSIX_SPAWN_SETPGROUP sends the child into a new process group to set up its own household), but the kernel will not wait on our behalf just because we released the promise. When the child dies before us and nobody reaps it, it becomes a zombie; the Z in the State column is measured, and we record it honestly. To walk the detach stretch cleanly, there are two proper answers: one is E3's bonus scene — setting SIGCHLD to SIG_IGN globally, letting the kernel reap automatically for us, at the price of giving up every child's exit code; the other is guaranteeing that detached children always outlive us, letting the adopter catch the fallback. The wrapper can make up a forgotten reaping for us, but nobody can perform the wait after detach for you — this stretch is the only part of child_process that requires you to keep the manual at hand.

## How the Other Side Looks

The Windows side has no fork. The claims of this section come with their provenance stated: they are drawn from documentation and from the Windows-side [Process and Jobs article](../../windows/process/01-createprocess.md), not from this article's own measurements — both sides of the comparison have experiments backing them. CreateProcessW creates and starts the process in a single call; there is only one return, no fork-style double return, the creation and exec work merged into the same call. The resource shapes change too: what you get back are two handles, one for the process and one for the main thread; waiting goes through WaitForSingleObject, exit codes through GetExitCodeProcess — and over there the full 32 bits come back, with no such thing as 8-bit truncation. A CLOEXEC counterpart does not exist either: handle inheritance is declared in one shot at creation time via bInheritHandles, and for per-handle granularity you must summon `PROC_THREAD_ATTRIBUTE_HANDLE_LIST`, the extended attribute that selects handles one by one. On the reaping side, the Windows Job object can fence a batch of processes into a single kernel object for unified management — one-to-many against this article's child_process — yet another contrast of two designs, with the details collected in that article.

This article has walked the full course of a process's birth, transformation, and exit, and three later places come back to use it. [The daemon article](./02-daemon.md) takes the fork-with-setsid combination to assemble the skeleton of a background service, finishing environment variables and resource limits along the way. The IPC article's pipes hold one fd at each end, and the lifecycle alignment rests on this article's reaping. The signal article expands SIGCHLD into the full sigaction machinery, and the subreaper 249 will make another appearance in the daemon article.

<ReferenceCard title="References">
  <ReferenceItem
    :id="1"
    title="fork(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/fork.2.html"
  />
  <ReferenceItem
    :id="2"
    title="execve(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/execve.2.html"
  />
  <ReferenceItem
    :id="3"
    title="wait(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/wait.2.html"
  />
  <ReferenceItem
    :id="4"
    title="exit(3)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man3/exit.3.html"
  />
  <ReferenceItem
    :id="5"
    title="posix_spawn(3)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man3/posix_spawn.3.html"
  />
  <ReferenceItem
    :id="6"
    title="vfork(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/vfork.2.html"
  />
  <ReferenceItem
    :id="7"
    title="prctl(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/prctl.2.html"
  />
  <ReferenceItem
    :id="8"
    title="sigaction(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/sigaction.2.html"
  />
</ReferenceCard>
