---
title: "inotify File Monitoring: Turning File System Activity into Event Streams"
description: "How programs know what's happening in the file system: inotify turns events into readable byte streams on a file descriptor. This article tests that a single open(O_CREAT) generates two events, IN_CREATE and IN_OPEN, mkdir's mask is 0x40000100 (IN_ISDIR is a state bit embedded in the mask, not an event), rename's FROM/TO is paired via cookie (tested as 8596), sizeof(inotify_event) is 16 and len is aligned to 16; watches are not recursive (touching a grandchild directory generates 0 events), binding to inodes not paths (file rename generates IN_MOVE_SELF, continues to follow, original path with new inode generates no events for file-level watches, old inode unlink generates IN_ATTRIB, IN_DELETE_SELF, IN_IGNORED in sequence); recursive monitoring requires dynamic addition of watches for the entire tree (grandchild directories from mkdir -p already exist before events arrive), after renaming a directory, descendants are silent while the wd table path is outdated; the real condition for merging is that the quadruple wd/mask/cookie/name is identical and adjacent (readers follow 10 writes and merge 10, back-to-back 200 writes collapse into 1, while 200 writes across 4 files remain 200, IN_MODIFY cannot act as a write counter), after 5,870,000 writes, a queue with a depth of 16384 is filled and only 16385 entries are read back, with IN_Q_OVERFLOW (wd=-1) at the end and no record of the lost part; inotify fd is connected with timerfd and pipe in a single epoll loop with level-triggered events; cookie identity is a global kernel counter."
chapter: 8
order: 6
platform: host
difficulty: intermediate
cpp_standard: [20]
reading_time_minutes: 25
prerequisites:
  - "POSIX File I/O: open/read/write and the life of a file descriptor"
  - "std::filesystem: Directories and Metadata"
related:
  - "RAII Paradigm for OS Resources: fd, HANDLE and the same skeleton mapping"
  - "Error Handling Paradigm: From errno to expected"
  - "epoll: Linux I/O Multiplexing, from poll's bottleneck to interest table and ready queue"
tags:
  - host
  - cpp-modern
  - intermediate
  - System Programming
  - POSIX
  - Practical
current_status:
  title: Being Polished
  detail: This batch has just been written and is currently being proofread and refined. The expression and experimental results may change at any time.
  variant: polishing
translation:
  source: documents/vol8-domains/systems-programming/linux/file-io/06-inotify.md
  source_hash: 04e7b4eedbd92d3ac4aa1ad8667ab98ba4f82078308c2f5d4c6898a0f2863d42
  translated_at: '2026-11-22T14:44:29+00:00'
  engine: qwen3:14b
  token_count: 25119
---
# inotify File Monitoring: Turning File System Activity into Event Streams

[L01](./01-posix-file-io.md) mentioned in the beginning that file locks and inotify would be covered in later chapters of this volume. The chapter on file locks has already been completed, and now it's time for inotify. In the first five chapters, we've been actively querying the file system: open and read query content, mmap queries bytes, fsync queries whether data has reached the disk, readdir queries names, and file locks query ownership. In this chapter, we'll reverse the direction: the file system side itself has changed, and our program needs to know how to detect it.

The easiest way is to poll, periodically stat the directory, and compare the snapshots of the old and new lists. However, once the directory becomes deep, the polling interval becomes a delay, and shortening the interval burns CPU in vain. Linux's answer is to make notifications into a file descriptor: inotify (short for inode notify) gives us a new fd. When a registered location has activity, the kernel writes events one by one into its buffer, and what we read out is a structured byte stream. The fd techniques you practiced in [L01](./01-posix-file-io.md), such as the read loop and dup2 redirection, are directly applicable here. The old saying "everything is a file descriptor" is once again realized in file monitoring.

Event streams can do quite practical things: daemon processes use them to monitor configurations, and reload them when they change. Build tools use incremental compilation triggered by them, and when the source directory changes, the list of what needs to be recompiled is generated. Synchronizers use them to monitor the working directory, and new changes are immediately processed. Desktop search indexes are also maintained this way. This chapter's six experiments follow a single line: what events look like (E1), how to set up watches (E2), how to set up recursive monitoring (E3), what the queue merging and overflow look like (E4), how to connect with epoll (E5), and whether we can recognize moves across directories and devices (E6).

As usual, the environment's specifications are clearly stated, and the following numbers should be matched against it: the experiments were conducted on the author's desktop, with an AMD Ryzen 7 9700X CPU, a WSL2 environment, a 6.18.33.2-microsoft-standard-WSL2 kernel build, and g++ 16.2.1. The compilation specifications are all `-std=c++20 -O2 -Wall -Wextra -Wpedantic`, resulting in zero warnings. The data is written under `/home/charliechen/l06_scratch/` with e1 to e6 (E6's cross-device side uses `/dev/shm`), and you need to create `~/l06_scratch` before rerunning the experiments. The experiment numbers are E1 to E6, corresponding one-to-one with the 01 to 06 directories under `code/volumn_codes/vol8/systems-programming/linux/file-io/06-inotify/`. All code and original outputs are archived, and you can refer to them at any time. [L03](./03-page-cache.md), [L04](./04-filesystem.md), and [L05](./05-file-lock.md) also use the uppercase E numbering, but those refer to the experiments in each respective chapter. We only recognize E in this chapter, so when you refer to the archive, just recognize the directory number. The three old tools `unique_fd`, `sys_call`, and `errno_code` are reused from the [RAII chapter](../../thinking/01-raii-paradigm.md) and [error handling chapter](../../thinking/02-error-paradigm.md), and we only reference them. The experiments also have three inotify-specific small tools (reading sysctl limits, decoding mask bits, and event stream readers), all placed in the `common/article.hpp` file in the experiment directory. We use them as needed in the main text.

## One fd, One Watch Table, One Event Stream

```c
int inotify_init(void);                                             /* 拿事件流的 fd */
int inotify_add_watch(int fd, const char *pathname, uint32_t mask); /* 登记监视项 */
int inotify_rm_watch(int fd, int wd);                               /* 摘除监视项 */
```

We can run it in three steps. `inotify_init()` returns a new fd, which is the outlet for all subsequent events. If we want non-blocking or close-on-exec, we use `inotify_init1(flags)`. The meanings of `IN_NONBLOCK` and `IN_CLOEXEC` are the same as the corresponding bits in open. All the fds in this chapter's experiments are equipped with `IN_NONBLOCK`. `inotify_add_watch()` registers a path, and the object can be a directory or a single file. The mask is the set of events you want to subscribe to, and the returned wd (watch descriptor, the ID number of the watch) is its ID in the instance. Events reported in the event stream are for it. If the same instance adds a watch for the same inode again, the kernel modifies the existing watch's mask, and the returned wd is still the original one. Therefore, using wd as the key for the map is safe. The removal is handled by `inotify_rm_watch()`, but in E1, we will see with our own eyes that there is a category of watches that we don't need to remove ourselves.

The kernel also uses a structure to inform us of the shape of the events:

```c
struct inotify_event {
    int      wd;      /* 事件属于哪只 watch */
    uint32_t mask;    /* 事件位;状态位 IN_ISDIR 也叠在这里 */
    uint32_t cookie;  /* 同一次 rename 的两条事件用它配对,其余恒 0 */
    uint32_t len;     /* name 占的字节数,含结尾 '\0',向 sizeof 对齐 */
    char     name[];  /* 被操作的条目名;watch 对象是自己时无名字 */
};
```

On x86-64, `sizeof(struct inotify_event)` is 16: four integer fields fill it up, and `name` is a flexible array that doesn't take up space. The calculation of `len` is easy to misread. It counts the number of bytes in `name` including the trailing null byte, and then pads it to the integer multiple of the structure size. The man 7 inotify also writes this in two steps. We calculate with `f.txt`: 5 bytes plus the trailing 1, making 6, which is padded to 16. In the complete output archive, there is also a 32-character long name, which adds 1 to make 33, padded to 48. `name` only appears when the watch is on a directory and the operation is on an entry within it. If the watch is on a file itself, the value of `len` is always 0, and E2's output will provide evidence.

The behavior of read is also clear: a successful read returns an integer number of events, and the buffer contains one or more complete structures. If the buffer is too small to hold the largest event, read returns EINVAL directly. Therefore, our read buffer of 4096 is more than enough, because the longest name is limited by NAME_MAX (the byte limit for a single name, 255 on Linux), and at most 16+256 bytes. When the queue is empty, the blocking version of read sleeps. We have `IN_NONBLOCK`, and an empty queue gives us EAGAIN, which we use as a signal that "there is nothing left." The skeleton of the reader is like this:

```cpp
// common/article.hpp(节选):把当前排队的事件一次取干净
std::vector<decoded_event> drain(int fd)
{
    std::vector<decoded_event> out;
    for (;;) {
        char buf[4096];
        ssize_t n = ::read(fd, buf, sizeof buf);            // 一次 read = 整数条事件
        if (n == -1 && errno == EINTR) {
            continue;
        }
        if (n == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            break;                                          // 队列空了,非阻塞版的“没了”
        }
        if (n == -1) {
            throw std::system_error{errno_code(), "read(inotify fd)"};
        }
        decode_append(buf, static_cast<std::size_t>(n), out);
        if (static_cast<std::size_t>(n) < sizeof buf) {
            break;                                          // 短读即读干
        }
    }
    return out;
}
```

For decoding, we use `memcpy` to copy the fixed-length header out, rather than directly casting a `char` array pointer. Buffer alignment is not our concern, and the complete implementation is in the archive.

The kernel's own limits for inotify are placed under `/proc/sys/fs/inotify/`. Our experimental program prints them every time it starts:

| Knob | Local Value | What It Controls |
|---|---|---|
| `max_user_watches` | 524288 | The total number of watches that can be attached for each real user across all instances |
| `max_user_instances` | 1024 | The number of instances (fds) that each real user can initialize |
| `max_queued_events` | 16384 | The depth of the event queue for a single instance, exceeding this reports IN_Q_OVERFLOW |

The first two control resource limits, and we will test them with 7 watches in E3. The third controls the queue depth, and E4's D group will overflow it.

> inotify can only see events triggered by local processes via file system APIs. Events that occur on remote file systems like NFS are not received, and man 7 inotify explicitly advises you to revert to polling. It also has a larger relative called fanotify(7), which monitors the entire file system and requires higher permissions. Tools for virus scanning and auditing use it, and for directory monitoring within our application, inotify is sufficient.

## E1: What Does the Event Stream Look Like: One open Generates Two Events, the State Bit is Embedded in the Mask

E1 proceeds as follows: we attach the watch to the test directory tree with a mask of `IN_ALL_EVENTS`, and the child process follows a ten-step script to gradually generate events. The parent process sends commands through a pipe, and the child process sends an ack (acknowledgment byte) after completing the steps. At this point, we read the events. We don't use sleep for synchronization, but use pipe handshaking, because the enqueueing of events occurs before the write-side syscall returns. When the ack arrives, the events are definitely in the queue. After the ten steps are completed, the event stream looks like this (the complete output is in the archive, with some deletions):

```text
$ ./e1
watched: /home/charliechen/l06_scratch/e1/tree  mask: IN_ALL_EVENTS
/proc/sys/fs/inotify/: max_user_watches=524288 max_user_instances=1024 max_queued_events=16384
sizeof(struct inotify_event) = 16
inotify_add_watch("/home/charliechen/l06_scratch/e1/tree", IN_ALL_EVENTS) = wd 1

==== 第  1 步:open(f.txt, O_WRONLY|O_CREAT|O_TRUNC) ====
  wd=1   mask=0x00000100 IN_CREATE                                    cookie=0      len=16  name='f.txt'
  wd=1   mask=0x00000020 IN_OPEN                                      cookie=0      len=16  name='f.txt'

==== 第  2 步:write(fd, "hello", 5) ====
  wd=1   mask=0x00000002 IN_MODIFY                                    cookie=0      len=16  name='f.txt'

==== 第  3 步:close(fd) ====
  wd=1   mask=0x00000008 IN_CLOSE_WRITE                               cookie=0      len=16  name='f.txt'

==== 第  4 步:open O_RDONLY + read 2 字节 + close ====
  wd=1   mask=0x00000020 IN_OPEN                                      cookie=0      len=16  name='f.txt'
  wd=1   mask=0x00000001 IN_ACCESS                                    cookie=0      len=16  name='f.txt'
  wd=1   mask=0x00000010 IN_CLOSE_NOWRITE                             cookie=0      len=16  name='f.txt'

...(第 5 步 chmod 报 IN_ATTRIB、第 7 步 unlink 报 IN_DELETE、第 9 步 rmdir 报 IN_DELETE|IN_ISDIR,略)...

==== 第  6 步:rename(f.txt, g.txt)  同目录改名 ====
  wd=1   mask=0x00000040 IN_MOVED_FROM                                cookie=8596   len=16  name='f.txt'
  wd=1   mask=0x00000080 IN_MOVED_TO                                  cookie=8596   len=16  name='g.txt'

==== 第  8 步:mkdir(sub) ====
  wd=1   mask=0x40000100 IN_CREATE|IN_ISDIR                           cookie=0      len=16  name='sub'

==== 第 10 步:rmdir(tree)  被监控目录本身被删 ====
  wd=1   mask=0x00000400 IN_DELETE_SELF                               cookie=0      len=0   name=''
  wd=1   mask=0x00008000 IN_IGNORED                                   cookie=0      len=0   name=''

目录没了之后再 inotify_rm_watch(wd=1) = -1, errno = 22 (Invalid argument)
```

We look at it step by step. The first step is enough for us to see: only one `open(O_CREAT)` is called, but two events are in the queue: IN_CREATE says "a child was added to the directory," and IN_OPEN says "this file was opened." The granularity of the events is the action, not the system call, so don't count the number of events to count the number of syscalls. Close also splits: closing a write-open generates IN_CLOSE_WRITE, and closing a read-only open generates IN_CLOSE_NOWRITE. The boundary is significant in E6.

Step 6 is the standard photo of rename. We see two: IN_MOVED_FROM reports f.txt, and IN_MOVED_TO reports g.txt, with both cookies being 8596. The cookie is usually 0, and non-zero only occurs in moves. The kernel uses it as a secret code to pair related events. The man page wording is "an integer that connects related events." Step 8's mask is 0x40000100, which we break down bit by bit: IN_CREATE (0x100), with IN_ISDIR (0x40000000) stacked on top. IN_ISDIR refers to the object, not the action. This time, the action was on a directory. Its role is only to stack on the event bit, declaring the identity of the object. So the same rmdir gets IN_DELETE|IN_ISDIR. When we rename an entire tree in E3, we'll see IN_MOVED_FROM stacked with IN_ISDIR. When judging the event type, we filter out IN_ISDIR from the mask, and the remaining is the action.

Step 10 focuses on the directory being monitored itself. When tree is deleted, two events enter the event stream: IN_DELETE_SELF and IN_IGNORED. Both have len 0 and name empty, because the subject is itself, and no introduction is needed. The semantics of IN_IGNORED, according to man, is that the watch was explicitly removed or automatically removed with the object's demise. That is, the kernel has already removed the watch for us, and if we later add `inotify_rm_watch`, we get -1 and EINVAL. If you have built a table with wd, you should synchronize the removal when you receive IN_IGNORED. The code in E3 does exactly that.

We compile the commonly used event bits into a table, and the values are all matched with the actual output:

| Event Bit | Value | Meaning |
|---|---|---|
| `IN_ACCESS` | 0x00000001 | File was read |
| `IN_MODIFY` | 0x00000002 | File was written |
| `IN_ATTRIB` | 0x00000004 | Metadata changed (permissions, timestamps, link count) |
| `IN_CLOSE_WRITE` | 0x00000008 | Close of a write-open |
| `IN_CLOSE_NOWRITE` | 0x00000010 | Close of a read-only open |
| `IN_OPEN` | 0x00000020 | File was opened |
| `IN_MOVED_FROM` | 0x00000040 | Moved out, paired with cookie |
| `IN_MOVED_TO` | 0x00000080 | Moved in, paired with cookie |
| `IN_CREATE` | 0x00000100 | An entry was created in the directory |
| `IN_DELETE` | 0x00000200 | An entry was deleted from the directory |
| `IN_DELETE_SELF` | 0x00000400 | The object being watched was deleted |
| `IN_MOVE_SELF` | 0x00000800 | The object being watched was moved |
| `IN_Q_OVERFLOW` | 0x00004000 | Queue overflow, wd=-1 |
| `IN_IGNORED` | 0x00008000 | Watch was removed, explicitly or automatically |
| `IN_ISDIR` | 0x40000000 | State bit: object is a directory |

## E2: Where is the Watch Attached: Non-Recursive, Bound to Inode

E2's subject is two-level directories A/B and several files. We attach watches step by step and perform actions step by step. The first observation is aimed at our path intuition: we attach the watch only to A, and touch a grandchild file A/B/y:

```text
$ ./e2  (节选)
/proc/sys/fs/inotify/: max_user_watches=524288 max_user_instances=1024 max_queued_events=16384
wd_a=1 → A/(目录)

==== 第 3 步:touch A/B/y(watch 只挂在 A 上) ====
  (0 个事件)
```

The answer we get is 0 events. Our watch is attached to A, and the action is on A's grandchild A/B/y. The kernel does not report a single byte to us. inotify's watch only covers the directory that is registered, and the subdirectories do not benefit, non-recursive is its semantics, not a defect in implementation. If we want to see the activity of the grandchild, we need to attach the watch to B's head. After attaching, the same action is viewed again:

```text
wd_b=2 → A/B/(目录) —— 现在子目录也有自己的表了
==== 第 3 步:touch A/B/y(watch 只挂在 A 上) ====
  wd=2   mask=0x00000020 IN_OPEN                                      cookie=0      len=16  name='y'
  wd=2   mask=0x00000008 IN_CLOSE_WRITE                               cookie=0      len=16  name='y'
```

wd_a on A remains silent, reporting wd_b on B. The attribution of events is calculated based on the directory being watched, not the path prefix. This is the foundation of our recursive monitoring, and E3 will attach a watch to each directory.

We also tested two edge cases. We passed `IN_ONLYDIR` to `add_watch`, and tried to attach it to a regular file, which returned -1 and ENOTDIR (errno 20). Its role is a "I only want directories" safeguard, useful when you want to attach to a directory but fear that the path might be changed to a file in the middle. Conversely, attaching a watch to a single file still reports events, but the name value is always empty, and the len value is also 0. We explained this in E1: the name of the directory watch reports the entry being operated on, while the file watch reports its own activity.

The next three steps together can clarify what the watch is bound to. The original wording from man 7 inotify is `Inotify monitoring is inode-based`, and we provide three pieces of evidence from the experiment.

The first piece of evidence is the scene of renaming. We rename A/f to A/f.bak, and the directory-side wd_a reports IN_MOVED_FROM and IN_MOVED_TO (cookie 8619 paired), while the file-side wd_f reports IN_MOVE_SELF: the watch follows the inode, and even after changing the name, it doesn't let go.

```text
==== 第 7 步:rename A/f → A/f.bak ====
  wd=1   mask=0x00000040 IN_MOVED_FROM                                cookie=8619   len=16  name='f'
  wd=1   mask=0x00000080 IN_MOVED_TO                                  cookie=8619   len=16  name='f.bak'
  wd=3   mask=0x00000800 IN_MOVE_SELF                                 cookie=0      len=0   name=''
```

We then look at the second piece of evidence: the scene of the original path being replaced. We place a new file (new inode) and write to it on the original A/f path. The directory-side wd_a reports CREATE, OPEN, MODIFY, CLOSE_WRITE all at once, while the file-side wd_f remains silent, not giving us a single event: the old inode it is bound to is still alive, but no longer resides on this path, and the new inode is unrelated to it. This is the other side of binding to the inode, the target has changed, and the monitoring side may not know.

The third piece of evidence is given at the end. The old inode at A/f.bak is unlinked, and wd_f receives three in sequence: IN_ATTRIB (unlink changes the link count, classified as a metadata event), IN_DELETE_SELF, IN_IGNORED. Then we call `inotify_rm_watch` on it, and we get the same EINVAL.

```text
==== 第 9 步:unlink A/f.bak(被 watch 的旧 inode 被删) ====
  wd=3   mask=0x00000004 IN_ATTRIB                                    cookie=0      len=0   name=''
  wd=3   mask=0x00000400 IN_DELETE_SELF                               cookie=0      len=0   name=''
  wd=3   mask=0x00008000 IN_IGNORED                                   cookie=0      len=0   name=''
  wd=1   mask=0x00000200 IN_DELETE                                    cookie=0      len=16  name='f.bak'
```

The engineering implications of these three steps need to be stopped separately. The standard posture for atomically replacing a configuration file is to write a temporary file and then rename it up, and log rotation also follows the same path, which is exactly "the original path is replaced with a new inode." Programs that only watch specific files will switch to the old inode when the file is replaced, and will not receive any events from the new file. To follow "who is now on this path," the watch should be attached to the directory, and the name recognition should be left to the event stream, and the pairing of moves should be left to the cookie.

## E3: Recursive Monitoring: Supplemental Watches Need to Be Entire Trees, the wd Table Needs to Be Modified by Yourself

Since the kernel is not recursive, recursive monitoring must be set up by us. Our setup is three tasks. We start by walking the entire tree, giving each directory its own watch. When we see `IN_CREATE|IN_ISDIR` in the event, we supplement the new directory. When we see IN_IGNORED, we remove the wd from the table. The core code for supplementing watches is not long:

```cpp
// recursive_watch.cpp(节选):对新目录整棵补挂
void add_tree(const fs::path& dir, bool announce)
{
    add_one(dir, announce);            // 起点自己也要挂:迭代器只遍历孩子
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(
             dir, fs::directory_options::skip_permission_denied, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) { break; }
        if (!it->is_directory(ec)) { continue; }
        add_one(it->path(), announce);
    }
}
```

The easy-to-misunderstand part is the depth of the supplement. We need to be careful. The event-driven idea sounds like "new directory comes, attach it," but `mkdir -p a/b/c` creates three layers at once. When our program receives a from a, the grandchild a/b/c is already on the disk. Only attaching a would miss the subsequent activities of b and c. So the supplement is not add_one but add_tree, from the new directory down to the entire tree. The actual output clearly shows this race condition:

```text
$ ./e3  (节选)
起手:walk 整棵树,每个目录挂一只 watch
  [watch] add_watch(wd=1) /home/charliechen/l06_scratch/e3/tree
起手 watch 数:1

==== 第 1 步:mkdir -p a/b/c(嵌套树) ====
  [event] wd=1  IN_CREATE|IN_ISDIR  /home/charliechen/l06_scratch/e3/tree/a
  [watch] add_watch(wd=2) /home/charliechen/l06_scratch/e3/tree/a
  [watch] add_watch(wd=3) /home/charliechen/l06_scratch/e3/tree/a/b
  [watch] add_watch(wd=4) /home/charliechen/l06_scratch/e3/tree/a/b/c

==== 第 2 步:touch a/1.txt a/b/2.txt a/b/c/3.txt ====
  [event] wd=2  IN_CREATE  /home/charliechen/l06_scratch/e3/tree/a/1.txt
  [event] wd=3  IN_CREATE  /home/charliechen/l06_scratch/e3/tree/a/b/2.txt
  [event] wd=4  IN_CREATE  /home/charliechen/l06_scratch/e3/tree/a/b/c/3.txt
```

The event we get is only one, and the watches for a/b and a/b/c are all supplemented by the bottom-up walk. We then touch three files of different depths, and wd=2, wd=3, wd=4 each report their own, and the value of the bottom-up walk is in these three lines.

Another counterintuitive scene is the entire directory being renamed. When `mv deep` is renamed to `deep2`, the parent directory reports a pair of `IN_MOVED_FROM|IN_ISDIR` and `IN_MOVED_TO|IN_ISDIR`, and deep's own watch (wd=5) reports IN_MOVE_SELF, while the descendants deep/x, deep/x/y, whose paths have clearly changed, we don't get a single event: the kernel only reports MOVE_SELF for the renamed inode, and the descendants do not benefit.

```text
==== 第 5 步:mv deep → deep2(整目录改名) ====
  [event] wd=1  IN_MOVED_FROM|IN_ISDIR  /home/charliechen/l06_scratch/e3/tree/deep
  [event] wd=1  IN_MOVED_TO|IN_ISDIR    /home/charliechen/l06_scratch/e3/tree/deep2
  [event] wd=5  IN_MOVE_SELF            /home/charliechen/l06_scratch/e3/tree/deep
```

The three watches are still valid after the fact, because they are bound to the inode, but the paths in our wd table are outdated. If you really want to track the path, you need to change the table when you receive MOVE_SELF, replacing the entire subtree's path prefix under it. No one can change it for you. The cost was also measured: the entire experiment attached 7 directories, 7 watches, occupying 0.0013% of the local `max_user_watches` (524288). One watch corresponds to one kernel object, and the usage is linear with the number of directories. Common sizes for editor project views and synchronizer source directories are in the thousands to tens of thousands, far from the upper limit. However, this quota is calculated globally for real users, and all processes on a single machine share it.

## E4: Queue: Real Conditions for Merging, No Records After Overflow

E4 is the heaviest group of experiments in this chapter. The commonly circulated saying is "IN_MODIFY will be merged by the kernel," but this is actually a misstatement. We use four groups of experiments to measure the conditions for merging and the consequences of overflow. We quote the original wording from man 7 inotify: if two consecutive events in the event stream are exactly the same, man writes that they are `same wd, mask, cookie, and name`, and the previous one hasn't been read yet, the kernel folds them into one. The first three groups test different parts of this sentence, and the fourth group fills the queue to overflow.

Group A is the case where the reader is following up. Our child process writes one beat at a time, and the parent process reads immediately. We go through ten rounds:

```text
==== A 读者跟上:每写一拍读一拍 ====
10 次写 → 10 条 IN_MODIFY:读者跟上,一条不少
```

Why no merging? We look at Group A's approach: the read process always empties the queue, and when new events come in, there are no objects in the queue to stack on. The merging condition is not met from the root. This also explains why we rarely encounter merging in daily life: event-driven programs that read are willing to read, and the queue is usually empty.

Group B is the case where the reader is not reading. We write the same file back-to-back 200 times:

```text
==== B 读者不读:200 次背靠背写 solo/same.txt ====
200 次写 → 1 条事件:
  wd=1   mask=0x00000002 IN_MODIFY                                    cookie=0      len=0   name=''
```

200 writes in the event stream result in only one single entry. The quadruple of wd, mask, cookie, and name are all the same and adjacent, and the kernel stacks them down. The practical implication of the test is hard: IN_MODIFY cannot act as a write counter. If you want to know how many times a file was written, there is no such number in the inotify mechanism. If you really want the number, you have to go to the kernel's audit subsystem, and audit and auditd are used for this.

We use Group C to correct the misstatement of "all similar events are merged." We still don't read, and four files take turns writing, totaling 200 times:

```text
==== C 读者不读:轮流写 c0..c3 共 200 次 ====
200 次写 → 200 条事件(首尾各 3):
  wd=2   mask=0x00000002 IN_MODIFY                                    cookie=0      len=16  name='c0.txt'
  wd=2   mask=0x00000002 IN_MODIFY                                    cookie=0      len=16  name='c1.txt'
  wd=2   mask=0x00000002 IN_MODIFY                                    cookie=0      len=16  name='c2.txt'
  ...
  wd=2   mask=0x00000002 IN_MODIFY                                    cookie=0      len=16  name='c1.txt'
  wd=2   mask=0x00000002 IN_MODIFY                                    cookie=0      len=16  name='c2.txt'
  wd=2   mask=0x00000002 IN_MODIFY                                    cookie=0      len=16  name='c3.txt'
```

We read back 200 complete entries. Looking only at the event type, the four IN_MODIFYs are exactly the same, but the names are rotated, and the quadruple at the end of the queue never matches the incoming guest, and the number of folds we count is zero. So the accurate statement is what we tested: completely identical and adjacent events are merged, but only the type being the same is not enough, and the ones separated by other events are not counted either. When B and C are put together, this sentence has both conditions verified.

Group D fills the queue. We start four writer processes, each responsible for 128 files, and write furiously for 1.5 seconds. We let the parent process not read at all, and then read all at once after finishing:

```text
==== D 灌爆队列:4 写者 × 128 文件 × 1500 ms,父进程全程不读 ====
max_queued_events = 16384
  [写者 1] 完成写次数 1474816
  [写者 3] 完成写次数 1480192
  [写者 2] 完成写次数 1477120
  [写者 0] 完成写次数 1442560
读出 16385 条事件,其中 IN_Q_OVERFLOW 1 条,首次出现在第 16384 条:
  wd=2   mask=0x00000002 IN_MODIFY                                    cookie=0      len=16  name='w1_0.bin'
  wd=2   mask=0x00000002 IN_MODIFY                                    cookie=0      len=16  name='w0_0.bin'
  ...
  wd=-1  mask=0x00004000 IN_Q_OVERFLOW                                cookie=0      len=0   name=''
```

The four writers complete a total of 5,874,688 writes. We read back a total of 16,385 events: the first 16,384 are all IN_MODIFY, plus one IN_Q_OVERFLOW with wd=-1 at the end of the queue (the 16,384th entry from 0). The depth of the queue is `max_queued_events` of 16,384. After the queue is full, the kernel discards all subsequent events and only adds this overflow marker at the end of the queue. The most important part is the second half of the timeline: after the queue is full, the writers are still writing, but no new events have entered the event stream. The five million plus writes that are missing, along with those merged into the first 16,384, have no records to check.

::: warning What to do after receiving IN_Q_OVERFLOW
The wd of the overflow entry is -1, which does not match any of the watches we have attached. It says that it has been lost, but it does not say what or how many were lost. The correct response is to treat it as a signal that the event stream is no longer trustworthy, and immediately perform a full rescan of the monitored tree, taking the current status as the new baseline. It is common for the read to be slow, and periodic checks on the application layer cannot be skipped, as they prevent unnoticed overflows. If you continue to process subsequent events as usual, it's like using a missing page change list to update the index.
:::

Reading quickly is our most basic defense, and E5's epoll loop does exactly this.

## E5: Connecting to epoll: Three Types of fd, One Loop

We have discussed up to this point, inotify has one unused property: its event stream is a regular fd, and it naturally fits into epoll. "There are events to read" is the same as "read end is ready" in the kernel's eyes, with no privilege difference from data coming in a pipe or timerfd expiring. In E5, we connect three types of fd into the same level-triggered epoll: one is inotify's fd, one is a timerfd that expires every 500 milliseconds, and one is a pipe where the child process writes a line every 300 milliseconds.

```cpp
// epoll_loop.cpp(节选):三类 fd 同挂一个 epoll,单循环统一调度
auto add = [&](int fd, const char* tag) {
    epoll_event ev{};
    ev.events = EPOLLIN;   // 默认水平触发:只要还有的读,每次 epoll_wait 都报
    ev.data.fd = fd;
    sys_call("epoll_ctl", ::epoll_ctl, ep.get(), EPOLL_CTL_ADD, fd, &ev);
};
add(ifd.get(), "inotify");
add(tfd.get(), "timerfd ");
add(msg_r.get(), "pipe");

while (!bye) {
    epoll_event out[8];
    int n = static_cast<int>(
        sys_call("epoll_wait", ::epoll_wait, ep.get(), out, 8, 5000));
    for (int i = 0; i < n && !bye; ++i) {
        const int fd = out[i].data.fd;     // 三类 fd 混在同一个 events[] 里回来
        // ...按 fd 分发:pipe 读消息,timerfd 读到期数,inotify 读事件...
    }
}
```

```text
$ ./e5  (节选)
epoll_ctl(ADD) fd=3 (inotify), events=EPOLLIN(水平触发)
epoll_ctl(ADD) fd=4 (timerfd ), events=EPOLLIN(水平触发)
epoll_ctl(ADD) fd=5 (pipe     ), events=EPOLLIN(水平触发)

[  302 ms] pipe    :"tick 1-1"
[  502 ms] timerfd :到期 1 次
[  902 ms] pipe    :"tick 1-3"
[ 1002 ms] timerfd :到期 1 次
[ 1102 ms] inotify :wd=1   mask=0x00000100 IN_CREATE         cookie=0      len=16  name='note.txt'
[ 1102 ms] inotify :wd=1   mask=0x00000002 IN_MODIFY         cookie=0      len=16  name='note.txt'
[ 1102 ms] inotify :wd=1   mask=0x00000008 IN_CLOSE_WRITE    cookie=0      len=16  name='note.txt'
...(中间数轮 pipe 与 timerfd 各就各位,略)...
[ 3304 ms] inotify :wd=1   mask=0x00000040 IN_MOVED_FROM     cookie=8809   len=16  name='note.txt'
[ 3304 ms] inotify :wd=1   mask=0x00000080 IN_MOVED_TO       cookie=8809   len=16  name='note2.txt'
[ 3502 ms] timerfd :到期 1 次
[ 3604 ms] pipe    :"BYE"
```

The three types of events on the timeline are interleaved, and we use a single `epoll_wait` loop to uniformly schedule them. At 1102 milliseconds, three file system events are inserted between two ticks. At 3304 milliseconds, the two rename events and the timerfd expiration arrive in sequence, with their own timelines. We won't re-explain the mechanism of epoll itself here, as the [epoll I/O multiplexing chapter in the networking volume](../../../networking/02-epoll-io-multiplexing.md) covers everything from the bottleneck of poll to the interest table and ready queue. If you need to review, please move there. In the later chapters of this volume on I/O multiplexing, we will also see timerfd and eventfd, which also convert themselves into fd and enter the same loop. We only mention the connection point here: level-triggered epoll with inotify is a convenient choice, read empties the queue, and the next epoll_wait naturally does not report, even saving the discipline of looping reads until EAGAIN in edge-triggered mode. If we really want to use edge-triggered mode, our drain originally reads the queue dry, and short reads and EAGAIN are its conclusion.

## E6: Across Directories and Devices: What Cookie Can and Cannot Do

Moving is the hardest action to recognize in the event stream, and we test it in two scenarios. Scenario one is moving across directories on the same device: the object file is f_samefs, A and B are both on ext4, and we have attached a watch to both sides. The move command is `mv A/f_samefs B/`:

```text
$ ./e6  (节选)
statfs(/home/charliechen/l06_scratch/e6/A) = ext2/3/4 (f_type=0xef53)
statfs(/dev/shm/l06_e6_C               ) = tmpfs (f_type=0x1021994)
wd_a=1 → A/,wd_b=2 → B/,wd_c=3 → C/

==== mv A/f_samefs B/  (ext4 → ext4,rename(2) 一步到位) ====
  wd=1   mask=0x00000040 IN_MOVED_FROM                                cookie=8787   len=16  name='f_samefs'
  wd=2   mask=0x00000080 IN_MOVED_TO                                  cookie=8787   len=16  name='f_samefs'
```

The records are under the name of wd=1, and the incoming records are under the name of wd=2. The cookie pairs the two as a single move, and the pairing is across watches. Man has a very honest warning about this pairing, using the wording "inherently racy": events other than FROM and TO may be inserted in the middle, and the two are not guaranteed to be adjacent, and even may only have one left when the queue overflows. So when writing pairing logic, we should follow the idea of "saving a window of FROM and waiting for TO," and don't expect them to always be shoulder to shoulder.

Scenario two is moving across devices. A remains on the ext4 side, and C is on the `/dev/shm` tmpfs. We also clearly recognize the two with statfs(2) (the call to check the file system type). We directly call rename(2):

```text
==== rename(2) 跨设备直接调会怎样 ====
rename(A/f_xdev, C/f_xdev) = -1, errno = 18 (Invalid cross-device link)
  (0 个事件)
```

We get back errno 18 (EXDEV). rename(2) is not allowed to cross file systems, and the mv command is prepared for this, it goes back to copy and delete. We manually perform the mv process as described, and the event stream is like this:

```text
==== mv 走的退路:复制 + 删除(自己照 mv 的做法演一遍) ====
  wd=1   mask=0x00000020 IN_OPEN                                      cookie=0      len=16  name='f_xdev'
  wd=3   mask=0x00000100 IN_CREATE                                    cookie=0      len=16  name='f_xdev'
  wd=3   mask=0x00000020 IN_OPEN                                      cookie=0      len=16  name='f_xdev'
  wd=1   mask=0x00000001 IN_ACCESS                                    cookie=0      len=16  name='f_xdev'
  wd=3   mask=0x00000002 IN_MODIFY                                    cookie=0      len=16  name='f_xdev'
  wd=3   mask=0x00000008 IN_CLOSE_WRITE                               cookie=0      len=16  name='f_xdev'
  wd=1   mask=0x00000010 IN_CLOSE_NOWRITE                             cookie=0      len=16  name='f_xdev'
  wd=3   mask=0x00000004 IN_ATTRIB                                    cookie=0      len=16  name='f_xdev'
  wd=1   mask=0x00000200 IN_DELETE                                    cookie=0      len=16  name='f_xdev'
```

You scan the cookie column from head to tail, and see all zeros. The source side looks like "read and then deleted," and the destination side looks like "a new file came in," both telling the truth, but this move is not present in the event stream. For cross-device moves, the monitoring side can only guess based on time windows and heuristic for the same name, and if it guesses wrong, there's no way to verify. The boundary mentioned in E1 is exactly the clue here: the source side ends with CLOSE_NOWRITE, and the destination side ends with CLOSE_WRITE. If we really want to write the identification logic, the hardest feature is these two.

We also tested the identity of the cookie: it is a global counter in the kernel. E1's rename got 8596, E2 got 8619, E6 got 8787, three different programs, three different inotify instances, but the count is continuing to go up, clearly counting the entire kernel's thing. The 8809 in E5's output is even larger than E6's, but it's not random: the order of the output is sorted by capture time, and E6's round of programs actually captured earlier than E5's, which is not synchronized with the experiment number. So the numerical value of the cookie itself should not be used as information, its only use is to compare equality within the same event stream. Pairing can only be done within our own fd's event stream: we don't know what's in other people's streams or when they arrive, and cross-process pairing, cookie can't do it.

The next stop on the Linux line is memory management, and we will spread out the address space of the process.

## What the Other Side Sees

Windows does not have inotify, and the same task is handed over to `ReadDirectoryChangesW`: we open the directory handle with `FILE_LIST_DIRECTORY` permissions, and when we call `ReadDirectoryChangesW`, we submit a buffer, and we receive `FILE_NOTIFY_INFORMATION` formatted records. The action codes are a set of `FILE_ACTION_` prefixed constants, familiar ones include ADDED, MODIFIED, REMOVED, and the old and new names for renaming each have their own codes. We look at the places where it is clearly different from inotify: one is that passing `bWatchSubtree=TRUE` makes it inherently recursive, which we manually set up in E3. Another difference is that notifications can also be lost, and the remedy after losing is the same: a full rescan of the entire tree. Asynchronous results are handled by overlapped I/O or completion ports, which are the main doors for asynchronous models on the Windows side. Detailed explanations are left for the later asynchronous I/O chapter (not yet written), and the foundation is in [Win32 File I/O](../../windows/file-io/01-win32-file-io.md).

<ReferenceCard title="Reference Resources">
  <ReferenceItem
    :id="1"
    title="inotify(7)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man7/inotify.7.html"
  />
  <ReferenceItem
    :id="2"
    title="inotify_init(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/inotify_init.2.html"
  />
  <ReferenceItem
    :id="3"
    title="inotify_add_watch(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/inotify_add_watch.2.html"
  />
  <ReferenceItem
    :id="4"
    title="inotify_rm_watch(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/inotify_rm_watch.2.html"
  />
  <ReferenceItem
    :id="5"
    title="read(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/read.2.html"
  />
  <ReferenceItem
    :id="6"
    title="epoll(7)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man7/epoll.7.html"
  />
  <ReferenceItem
    :id="7"
    title="fanotify(7)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man7/fanotify.7.html"
  />
  <ReferenceItem
    :id="8"
    title="rename(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/rename.2.html"
  />
  <ReferenceItem
    :id="9"
    author="Michael Kerrisk"
    title="The Linux Programming Interface"
    publisher="No Starch Press"
    :year="2010"
    url="https://man7.org/tlpi/"
  />
</ReferenceCard>
