---
title: "std::filesystem: Directories and Metadata"
description: "C++17 brought directories, paths, and metadata into the standard library: this article tests libstdc++'s directory_iterator, which does not sort at all (the top-level order is the same as the hand-crafted readdir, ext4 large directories use HTree to hash names, and the order is stable across processes and tools), the same ENOENT is treated in four different ways in exists/is_directory/status/file_size (existence queries treat not_found as an answer and clear ec, file_size returns uintmax_t(-1) as a sentinel for errors), two outcomes when a directory is rm -rf during iteration (the flat version consumes the stale list from glibc's 32 KiB buffer, only seeing 1022 out of 3001 entries, the recursive version reports ENOENT at the descent point), nofollow changes link permissions on Linux result in EOPNOTSUPP, space().free and space().available differ by 5.09% exactly matching ext4 root's reserved space (as compared with df), four performance benchmarks for traversing 10,000 files (2.16/3.42/3.43/16.22 ms, strace proves the difference is in constructing fs::path per entry, not in syscalls, is_regular_file uses d_type cache with zero new syscalls, file_size calls stat per entry), operator/ and lexically_normal's counterintuitive behavior, follow_directory_symlink's loop is capped by the kernel's 40-link limit, and the library suppresses ELOOP without reporting (keep.txt is accessed 41 times, ec remains clean throughout), and in a dual-tree diff, mtime misreporting is exactly the same as rsync's quick check cost."
chapter: 8
order: 4
platform: host
difficulty: intermediate
cpp_standard: [17, 20]
reading_time_minutes: 24
prerequisites:
  - "POSIX File I/O: open/read/write and the life of fd"
  - "Page Cache and Persistence: What happens after write() returns"
related:
  - "RAII Paradigm for OS Resources: fd, HANDLE, and the same skeleton for mappings"
  - "Error Handling Paradigm: From errno to expected"
  - "Win32 File I/O: Handles, CreateFileW, and synchronous read/write"
tags:
  - host
  - cpp-modern
  - intermediate
  - system programming
  - POSIX
  - practical
current_status:
  title: Being polished
  detail: This batch was just written and is currently being proofread and polished. The wording and experimental results may change at any time.
  variant: polishing
translation:
  source: documents/vol8-domains/systems-programming/linux/file-io/04-filesystem.md
  source_hash: 2376db9c9f67f93b9377d72d6cd8adfaa965231d6e2d66dcf12a13bf78d98764
  translated_at: '2026-11-22T14:29:30+00:00'
  engine: qwen3:14b
  token_count: 24331
---
# std::filesystem: Directories and Metadata

At the end of the previous article, we left a sentence: the next article will return to the library level. The first three articles in our hands are all raw POSIX things: open/read/write accompanied fd through its life, mmap mapped the file into the address space, and the page cache article even looked at what happened after write() returned. This article will switch to another level: since C++17, directories, paths, and file metadata have been included in the standard library, and the header file is our today's `<filesystem>`. The same code can be compiled on Windows platforms as well. It manages the traversal of directory trees, querying attributes, and path algebra. File content read/write is not its domain; that's still the domain of fd and mappings. At the end of this article's dual-tree diff, we only look at metadata, not reading a single byte of content.

What is under the standard library layer? We will explain the numbering convention here: the experiments in this article are numbered E1 to E6, corresponding one-to-one with the 01 to 06 directories in the repository `code/volumn_codes/vol8/systems-programming/linux/file-io/04-filesystem/`. They are separate from the E series in the previous article, so please do not mix them up. Among them, E4 gives an answer with strace: libstdc++ (the C++ standard library implementation that comes with GCC) directory_iterator and the hand-crafted opendir/readdir have the same sequence of syscalls line by line. So what is really worth looking at is what the layer does outside of the syscall. One end is the work it saves for us, and E4 will show you the number of syscalls saved, and also the cost of the wrapper itself. The other end is the errors it does not report upward: E2's truncation ends silently, and in the output, we can't even find the shadow of ec for the ELOOP event. The two easiest places to misjudge, we have prepared experiments. The first is that we thought the iteration was ordered, but in fact, libstdc++ does not sort at all, and the order is given by the file system (E1). The second is that we thought no error meant the result was correct, but the same rm -rf could make the iteration silently lose two-thirds of the entries (E2).

All original outputs of the six experiments are recorded, and you can refer to the table at any time. As usual, we will clearly explain the environment: the experiments are run on the author's desktop, with an AMD Ryzen 7 9700X CPU, running in the WSL2 environment, with a kernel built as 6.18.33.2-microsoft-standard-WSL2, g++ version 16.2.1, strace version 7.2, and the user is a non-root regular user, and the EACCES test case in E3 relies on it. The root of the data tree `/home/charliechen/l04_scratch` is on a real ext4 disk: we follow the discipline established in the page cache article, `/tmp` is the territory of tmpfs, and we will be confused by it when doing semantic comparisons. The compilation standard is uniformly `-std=c++20 -O2`. The old tools unique_fd, sys_call, and errno_code continue to use the definitions from the [RAII article](../../thinking/01-raii-paradigm.md) and [error handling article](../../thinking/02-error-paradigm.md), and we will not explain them again here. This article only adds one new item: the `DIR*` obtained by opendir is also handed over to RAII, and we name it unique_dir. Its skeleton is the same as unique_fd, and you will be able to recognize it when you see it later.

## E1: The Trio and the Unsorted libstdc++

The main character for single-level traversal is `directory_iterator`: you give it a path, and it will hand over the entries in the directory to us one by one. The type obtained by dereferencing is called `directory_entry`, and it carries all the details of the entry: `path()` gives the full path (the parent directory passed in during construction plus the name), and the type judgment `is_directory()`, `is_regular_file()`, `is_symlink()` are also directly attached to the entry. The main character for recursive traversal is `recursive_directory_iterator`, which does one more thing than the single-level version: when encountering a directory entry, it automatically descends, and the current depth must be asked from the iterator itself, and the `directory_entry` does not have this information.

Of course, it's meaningless to talk about it without evidence. We put the three methods on the same tree: e1_trio built a small tree of five levels, with regular files, hidden files, links pointing to directories and files, and also left a dangling link pointing to nowhere. Then we let the trio go through it. The third one is a hand-crafted opendir/readdir, serving as the POSIX control group:

```cpp
// e1_trio.cpp(节选):同一棵树,fs 与手搓各走一遍
for (auto& e : fs::directory_iterator(kRoot))                    // [A] 单层
    order_a.push_back(e.path().filename().string());

for (fs::recursive_directory_iterator it(kRoot), end; it != end; ++it)  // [B] 递归
    order_b.push_back(it->path().lexically_relative(kRoot).string());  // 为什么用 lexically_relative,E5 讲

unique_dir d{::opendir(kRoot)};                                  // [C] 手搓,DIR* 交给 RAII
while (dirent* de = ::readdir(d.get()))                          // 原始序里含 . 和 ..,要自己滤
    if (std::strcmp(de->d_name, ".") && std::strcmp(de->d_name, ".."))
        order_c.push_back(de->d_name);
```

```text
$ ./e1_trio
tree root: /home/charliechen/l04_scratch/e1_tree  (mid 先建、alpha.txt 最后建;readdir 吐的序两者都不像)

== [A] fs::directory_iterator(单层,默认) ==
  link_mid       type=link->dir
  link_zeta      type=link->file
  alpha.txt      type=file
  mid            type=dir
  broken_link    type=link->file
  zeta.txt       type=file
  .hidden        type=file

== [C] opendir+readdir(顶层,原始 d_name 序,含 . 和 ..) ==
  d_name=link_mid       d_type=DT_LNK
  d_name=link_zeta      d_type=DT_LNK
  d_name=alpha.txt      d_type=DT_REG
  d_name=.              d_type=DT_DIR
  d_name=..             d_type=DT_DIR
  d_name=mid            d_type=DT_DIR
  d_name=broken_link    d_type=DT_LNK
  d_name=zeta.txt       d_type=DT_REG
  d_name=.hidden        d_type=DT_REG

== 判定 ==
directory_iterator 顶层序 == readdir 序(去 . ..): yes
directory_iterator 顶层序 == 字典序:              no(非排序)
recursive 的 depth-0 条目序 == [A] 顶层序:        yes(DFS 先序,顶层兄弟序不动)
```

The judgment line makes it clear: [A] and [C] have the same order (after removing `.` and `..`), and it has nothing to do with lexicographical order. libstdc++ does not sort at all, and the standard only says that the order of directory iteration is unspecified. Its implementation is to take the order given by readdir as is. So who determines this order? We trace it down and find that it is ext4. For large directories, it uses **HTree** (hash index tree, the index format for large directories in ext4, its layout is described in the kernel documentation's directory chapter), and the lookup is based on the hash of the name, so the output order is a byproduct of the hash. As for when small directories become hash-ordered, we didn't dig further, and the kernel documentation doesn't mention switching based on the number of entries. However, our practical test on this machine gives a clear boundary: this small tree at the top has only 7 entries, and the order is neither creation order nor lexicographical order. In E4's directory with 10,000 files, the first few names look like this:

```text
readdir 前8: f00408 f00721 f02711 f02893 f08519 f03842 f05713 f09678
字典序前8: f00000 f00001 f00002 f00003 f00004 f00005 f00006 f00007
```

You can see, f00408 is the first, and we can't find f00000 in the first eight positions. This order is neither creation order (mid was created earlier than alpha.txt, but it comes after it), nor lexicographical order. However, it is completely deterministic for the same set of names: we have taken pictures across processes and tools, and /usr/bin/ls -U and find give the same order, and the order remains unchanged on reruns, because the hash only recognizes the set of names, and is unrelated to who created them or who created them later. What about code that depends on order? We sort ourselves: we collect the names into a vector and sort them, or follow E6's approach and put them into a map, and the red-black tree takes care of the order for us.

> The `ls` on my machine is an alias for eza, and its `-U` semantics are different from GNU ls. The first time I did a comparison, I almost got a wrong judgment, saying that the readdir order was unstable. If you want to capture the original order of a directory, I recommend you use /usr/bin/ls -U.

The [C] column's d_type is something we need to look at more closely: it is the type field brought back by readdir (DT_REG for regular files, DT_DIR for directories, DT_LNK for symbolic links). For non-link entries, the kernel already provides the type in the directory entry, saving a round of stat. For link entries, it's a different story: is_directory() and similar type judgments follow the follow semantics. The cache for the link's type doesn't know, and asking the disk is unavoidable — the link_mid's link->dir label in [A] is exactly how it came about. The man 3 readdir manual page also mentions that d_type is not filled in by every file system, and in cases where it can't be filled, it gives DT_UNKNOWN, and type judgment can only be supplemented with another stat. ext4 does fill it in. How many syscalls can this field save? E4's experiment will calculate it separately.

We omitted the output of [B], but there are two points to note: the recursive traversal goes deeper, and the order of the top-level siblings is exactly the same as [A]. The symbolic link pointing to a directory, link_mid, only reports its name, and there is nothing below it, because the default option does not descend into directory links, so the loop can't even enter. Is this default value safe? We'll keep this question in mind and revisit it in the follow_directory_symlink section.

## E2 (Upper): The Same ENOENT, Four Different Treatments

In terms of error handling, each operation in `<filesystem>` has two faces: the overload without error_code throws a filesystem_error when it fails. The overload with error_code as an output parameter does not throw on failure, and the error code is placed into ec. We feed the same non-existent directory to both faces:

```cpp
// e2_forms.cpp(节选)
try {
    fs::directory_iterator it{kMissing};          // 无 ec 重载:失败即抛
} catch (const fs::filesystem_error& e) {
    // what() / code() / path1() 三个字段都在这儿
}

std::error_code ec;
fs::directory_iterator it{kMissing, ec};          // ec 重载:不抛
// 失败时 ec.value()==2,it 是收尾迭代器(空壳)
```

```text
$ ./e2_forms
== [1] directory_iterator(不存在目录)—— 抛异常版 ==
  caught std::filesystem::filesystem_error
  what()       = "filesystem error: directory iterator cannot open directory: No such file or directory [/home/charliechen/l04_scratch/e2/no_such_dir]"
  code().value()    = 2 (ENOENT=2)
  ...(code().message() = "No such file or directory",略)...
  code() == errc::no_such_file_or_directory : true
  path1()      = "/home/charliechen/l04_scratch/e2/no_such_dir"

== [2] 同一操作,error_code 重载 ==
  构造返回后:ec.value()=2 (ENOENT=2)
  ...(ec.message() 一行同上,略)...
  ec == {} 判失败:true(ec 非默认值即失败,可直接当哨兵)
  失败时拿到的是收尾迭代器:it == end(不抛,得空壳)
```

The thrown filesystem_error is a relative of system_error, but it adds `path1()`/`path2()`, and the paths where the error occurred are directly on the exception. When you report an error, you don't need to拼 the context yourself. The ec version returns a terminating iterator, which is the same convention as the container algorithm, which gives end() on failure, and the loop naturally goes into an empty rotation.

What's really interesting is the non-member functions. We feed the same ENOENT to four query functions, and the treatment is completely different:

```text
== [3] 非成员函数的 error_code 版,同一路径 ==
  fs::exists             ret=false  ec.value()=0   ec.message()="Success"
  fs::is_directory       ret=false  ec.value()=2   ec.message()="No such file or directory"
  fs::status             type=-1 (not_found 枚举值 = -1) ec.value()=2
  fs::file_size          ret=false  ec.value()=2
    file_size 返回值 = 18446744073709551615 (uintmax_t(-1)=错误哨兵)
  ...(部分行的 ec.message() 字段有删节,数值未动,完整版在存档 e2_forms.out)...
```

Let's look at them one by one. In the eyes of `exists`, the fact that the file is not there is the answer itself: it returns false, and ec is also cleared to zero. The cppreference writing is that status is known, so ec.clear() is called, and not_found is also considered a known status. `is_directory` also returns false, but it leaves ENOENT in ec as is. `status` gives the not_found type, and the type itself is its answer, ec only records the reason. `file_size` treats the file not being there as an error, and returns the sentinel value of uintmax_t(-1), which is the long string of 18446744073709551615 in the output. The purpose of the sentinel is to occupy the return position with a special value that is immediately recognizable, so that the failure to find is not mistaken for a file that is really that large. The four attitudes are presented together: you ask if it's there, and not being there is its answer. You ask how big it is, and not being there becomes an error. The same errno under different questions has different identities.

When writing code, you have to choose the function based on the question, and when judging, you have to look at both the return value and ec. If you only look at ret and not ec, we will mix up the failure due to permission being blocked and the answer that it's definitely not there. On the other hand, if you only look at ec, you can't explain why exists erased the information about the query failure. The practical comparison of the two mixed cases, E3's end will show you.

## E2 (Lower): The Directory Was Gone During Iteration

For the second experiment, we change the question: when the directory is deleted by someone else in the middle of iteration, what will the iterator do? The deletion cannot come from ourselves, as we can't control our own deletion, so e2_vanish uses a two-process arrangement: the child process slowly iterates, with a 2 millisecond interval between each entry, and places a marker file when it reaches the 10th entry. The parent process watches for the marker and, when it appears, runs a rm -rf. The large directory has 3000 top-level files and 1 subdirectory, with a baseline of 3001 entries. The small tree has 20 files and two subdirectories, with a baseline of 32 entries. There is also a `./e2_vanish solo` that does not delete, and all 3001 entries are seen throughout, with attribute query results of 3000 successes and 0 failures.

```cpp
// e2_vanish.cpp(节选):慢速迭代,删除来自另一个进程
for (fs::directory_iterator end; it != end;) {
    ++steps;
    if (really_delete && steps == 10) touch(marker_a);   // 通知父进程动手
    std::error_code attr_ec;
    auto sz = it->file_size(attr_ec);   // 不吃缓存的属性:每条现问盘上
    if (attr_ec) ++attr_fail; else if (sz == 0) ++attr_ok;
    std::this_thread::sleep_for(std::chrono::milliseconds(2));   // 慢速,给 rm 让出窗口
    std::error_code inc_ec;
    it.increment(inc_ec);               // 增量的失败数单独记
    if (inc_ec) { ++incr_fail; break; }
}
```

```text
$ ./e2_vanish      # 另有 solo 基线一趟,文中已述
[A] directory_iterator,迭代中外部删除
  step=1     name=f0888      attr_ec=0(Success)
  step=2     name=f2525      attr_ec=0(Success)
  ...(step 3 到 6 同为 Success,略)...
  step=200   name=f1431      attr_ec=2(No such file or directory)
  ...(step 400/600/800/1000 全是 ec=2,略)...
  [A 小结] 共见 1022 条(基线应为 3001);increment 失败 0 次;属性查询成功 84、失败 938
  树还在吗:已被 rm -rf

[B] recursive_directory_iterator,迭代中外部删除
  step=1    depth=0 t00
  ...(step 2 到 16 都是顶层文件,略)...
  step=17   depth=0 sub_a
  step=17 increment 失败:2 (No such file or directory) —— 标准把这算错误,递归下降打不开子目录
  [B 小结] 共见 17 条(基线应为 32 = 20 文件 + 2 目录 + 10 子文件);树还在吗:已被 rm -rf
```

Please read the three numbers in [A] together: the number of failed increments is 0, the number of failed attribute queries is 938, and the number of entries seen is 1022. Although rm -rf has completely removed the tree, the iterator actually did not report any errors. Why? We need to start with glibc's readdir: it leaves a 32 KiB buffer in user space, which is filled by getdents64 (the system call for reading directory contents) one by one. Subsequent readdir calls just take names from the buffer. When the deletion happens, the names in the buffer are those copied before the deletion, so the old names are still output. Attribute queries are different: file_size does not use the cache, and each query asks the current state on the disk, and the disk's contents have already been deleted, so from around the 85th entry, attr_ec all becomes 2. After the buffer is exhausted, the next getdents64 returns EOF on an empty shell: the directory's fd is still open, but the contents have been emptied. The iteration ends here, and out of 3001 entries, only 1022 are seen, "no error, no ec set, no extra step". This is the scene of silent truncation.

The performance of [B] is different. The name of the subdirectory sub_a was copied into the buffer before deletion, and the entry was produced as usual. When it came to descending into sub_a, the directory was already gone, and the increment(ec) really reported ENOENT. The standard considers the inability to open the subdirectory as an error, and the recursive version has a reporting obligation. In the same rm -rf experiment, the flat version silently truncates, while the recursive version reports an error on the spot. You have seen both outcomes.

> This is a soft real-time arrangement with two processes. When you rerun it, the specific numbers like 1022 and 938 will vary with the rhythm, but the three phenomena (old names, ENOENT in attributes, silent ending at EOF) are present in every run, and the README archive also explains it this way.

::: warning No error does not mean the result is correct
When we do list-type iterations, the absence of exceptions at the end does not mean the count is complete. The cheapest verification is to compare the number of entries with the baseline: rerunning the same directory without deletion gives the baseline count, and the difference is the truncation. For important scans, you can verify the existence of the root directory with exists() after completion, or, as in the experiment, check ec for key attributes one by one. If you want to make sure that every step of the recursive traversal is clear, use the ec version for the increment. The flat version has no such protection at all, and its increment does not even check the disk.
:::

## E3: Permissions, Symbolic Links, and 5.09% Reserved

The way to change permissions is `fs::permissions`, and the combination of perms bits and perm_options determines the behavior, "add is additive, remove is removal, replace is full replacement". We check it with bare `::access(X_OK)`:

```cpp
// e3_perm_space.cpp(节选)
fs::permissions(p, fs::perms::owner_exec, fs::perm_options::add);   // 644 -> 744
::access(p.c_str(), X_OK);                                          // 对拍:可执行

fs::symlink_status(link);   // 不跟随:看链接自身(Linux 上恒 0777)
fs::status(link);           // 跟随:看目标
fs::permissions(link, fs::perms::owner_write,
                fs::perm_options::remove | fs::perm_options::nofollow);  // Linux: 不支持

fs::space_info sp = fs::space(dir, ec);   // capacity / free / available 三件
```

```text
$ ./e3
== [1] fs::permissions 三步 ==
  初始            : rw-r--r-- (644)
  add owner_exec  : rwxr--r-- (744)
  ::access(X_OK)  : 可执行
  remove owner_exec: rw-r--r-- (644)

== [2] 符号链接:fs::status(跟随) vs fs::symlink_status(不跟随) ==
  status(link)          type=1 perms=rw-r--r-- (644)   <- 跟随,看到的是 real.txt
  symlink_status(link)  type=3 perms=rwxrwxrwx (777)   <- 链接自身,Linux 恒 0777
  status(dangling)      type=-1 (not_found=-1)  <- 跟随悬空链接 = 文件不存在
  symlink_status(dangling) type=3 (symlink=3)  <- 不跟随,链接本身在
  nofollow 改链接自身权限:ec=95 ("Operation not supported")  <- Linux 对 lchmod 的态度
  目标 real.txt 权限有没有被误伤:rw-r--r-- (644)
  默认(跟随)remove owner_write 后目标权限:r--r--r-- (444)
```

The information in [2] is quite rich, and we will look at it in three parts. The first part is the division of labor between status and symlink_status, and the key difference is whether to follow or not. The former sees the real.txt's 644, and the latter sees the link itself, and the permissions of symbolic links on Linux are always 0777. The comparison of dangling links is even more straightforward: following the link results in the file not existing, and the type is also recorded as not_found. If you don't follow, the link itself is still alive, and the type is just recorded as a symlink. The second part looks at nofollow: if you want to change the permissions of the link itself, Linux returns ec=95, EOPNOTSUPP is its name, which means the operation is not supported. The kernel doesn't even provide a way to change the permissions of a link, and the fchmodat request with AT_SYMLINK_NOFOLLOW is not recognized by it. On this machine, the 6.18 kernel also has this attitude. The permission bits of the link on Linux are always a constant 0777, and no one reads its value. The third part looks at the default version without nofollow: it follows to the target, and remove owner_write really changes the permissions of real.txt, resulting in 444. If you want to change the link, you accidentally affect the target, so Linux either doesn't support this step or changes the wrong object, and the two versions of output exactly show both sides.

We then look at the space query, which gives us three numbers. The division of labor for the three numbers is: capacity records the capacity, free records all the empty spots, and available records the actual amount that ordinary processes can use. The difference between free and available is exactly the emergency space reserved by ext4:

```text
== [3] fs::space("/home/charliechen/l04_scratch") ==
  capacity  = 1081101176832 (1006.85 GiB)
  free      = 1014503858176 (944.83 GiB)   <- 块总量减已用(含 root 预留)
  available = 959511502848 (893.61 GiB)   <- 非特权进程能用到的
  free - available = 54992355328 (5.09%) <- ext4 root 预留的痕迹
  space(不存在路径): ec=2, 字段=18446744073709551615(哨兵 uintmax_t(-1))

== df 对表(同一路径) ==
df: total=1081101176832 used=66597920768 avail=959510900736
```

The difference of 54992355328 bytes divided by capacity is exactly 5.09%. The man mke2fs(8) entry is clear, the default percentage of reserved space is 5%, and the extra 0.09 percentage point in the practical test is due to the statistical口径 difference. We can also match this in df: total and capacity are identical byte by byte, and df's avail and space().available differ by 602112 bytes (about 588 KiB), which is a normal drift between two samples. So if you ever see a situation where the disk still has a lot left but says it can't fit, the reason is actually the reserved space, and the arithmetic is innocent. When the path does not exist, space returns ec=2, and all three fields give sentinel values, which are the same uintmax_t(-1) seen in E2 (Upper).

Finally, we bring out the directory with chmod 000 to complete the last example of the four treatments in E2 (Upper). The directory without permission, the iterator gets ec=13 (EACCES, meaning permission denied) when it is constructed. Under it, exists a file that truly does not exist, ec leaves 13, and in the ENOENT case, ec is 0. The two results that return false look exactly the same in ret, but their identities are completely different: permission denied means it couldn't be queried, and the file not existing means it was queried and indeed not there, and exists only recognizes the latter answer. If you have written logic that creates a file when exists is false, this is where you should look at ec.

## E4: Performance Dissection, the Price is in fs::path, Not in syscall

The question we ask in this experiment is very simple: how much more expensive is the standard library iteration than the hand-crafted one, and where is the cost? e4_bench created a directory with 10,000 files, and we ran four routes three times each, taking the median, preheated, and the page cache was hot. We measured the cost of syscalls and wrapping, and the disk speed was not within the measurement range:

| Route | Three rounds (ms) | Median |
|---|---|---|
| a hand-crafted opendir/readdir (only get names) | 2.15 / 2.16 / 2.16 | 2.16 |
| b directory_iterator (only get names) | 3.40 / 3.42 / 3.48 | 3.42 |
| c b + entry.is_regular_file() | 3.42 / 3.43 / 3.49 | 3.43 |
| d b + entry.file_size(ec) | 15.97 / 16.22 / 17.99 | 16.22 |

The numbers need to be paired with the syscall table to read the flavor, and we ran a single run with strace's single-shot mode, and the sequences of a and b were line by line the same:

```text
# strace -e trace=openat,getdents64,close,newfstatat,statx ./e4_bench one b
openat(AT_FDCWD, "/home/charliechen/l04_scratch/e4_big", O_RDONLY|O_CLOEXEC|O_DIRECTORY) = 3
getdents64(3, 0x618190a10180 /* 1024 entries */, 32768) = 32752
...(getdents64 共 11 趟:32768 字节的缓冲每趟装约 1024 条,10 趟有货)...
getdents64(3, 0x618190a10180 /* 0 entries */, 32768) = 0
close(3)                                = 0
```

The getdents64 of b and a are both 11 times, and the newfstatat is 5 times for the baseline of process startup. We even counted how many entries each run loaded, and they were consistent. So the 1.26 ms extra from a to b is not in the kernel: every time directory_iterator hands over an entry, it has to concatenate the parent directory's path and name into fs::path, and store it in the directory_entry. String allocation eats heap memory, and during destruction, it has to return it. At 10,000 entries, the price per entry is about 126 ns, which is the pure user-space wrapping price. A unique flag difference: glibc's opendir brings O_NONBLOCK, and libstdc++'s own openat does not, and the semantics have no impact.

The speed of c and b is almost the same, is_regular_file gets the d_type cache mentioned in E1 for free, and no new syscalls are added. The boundary for the zero statement is: all entries in this tree are regular files, and if you switch to a tree with links, the follow semantics of type judgment would have to add a stat for each entry. d changes the world: newfstatat's count goes from 5 to 10005, each entry has a stat. file_size has to ask for something that is not in dirent, so it can only ask now. 3.42 ms becomes 16.22 ms, 4.7 times is all the cost of this stat. E1's directory e1_cache verified the same thing with a small directory of 200 entries: the type mode has only 5 baseline newfstatat, and the size mode goes up to 205. The experiment's number is attached to E1, but its evidence is exactly usable here. In engineering practice, on a tree with 10,000 files, just counting names is four times faster than asking for the size of each entry. If we want to make batch scanning faster, the trick is not to ask for things that the directory entry doesn't have. If we really want to ask, we have to include the cost of stat in the budget.

## E5: Path Algebra and a Few Counterintuitive Points

The concatenation of paths uses operator/, which does "attach to the right", and its semantics are not string concatenation, so its behavior is different from intuition. We let e5_paths sweep all the corners:

```text
== [1] operator/ 的四种脾气 ==
  fs::path("base") / "leaf"    -> "base/leaf"
  fs::path("base/") / "leaf"   -> "base/leaf"
  fs::path("base//") / "leaf"  -> "base//leaf"
  fs::path("base") / ""        -> "base/"
  fs::path("base") / "/abs"    -> "/abs"
  fs::path("base") / "./leaf"  -> "base/./leaf"
  p = "root"; p/="sub"; p/="file.txt" -> "root/sub/file.txt"

== [2] lexically_normal(纯词法) ==
  "./a/../b"                   -> "b"
  "a/b/.."                     -> "a/"
  "a/../.."                    -> ".."
  "../b"                       -> "../b"
  "a//b///c"                   -> "a/b/c"
  "a/./b"                      -> "a/b"
  "/../../a"                   -> "/a"
  "./"                         -> "."
  ""                           -> ""
```

The most eye-catching in [1] is `fs::path("base") / "/abs"`: the right side has a root slash, operator/ treats it as an absolute path, and the left side is completely overridden. If the right side is an external input, the result may be the absolute path on the right. The rules for the trailing separator are also worth noting: the base/ at the end is absorbed, and the base// with double slashes is left as is (the library only recognizes the last separator as the boundary, the rest are left), lexically it is equivalent, but the string is ugly. Don't panic if you see it in the log, just scan it with lexically_normal to flatten it. In the case of concatenating an empty string, a trailing slash is added.

[2] is purely lexical normalization, `./a/../b` is reduced to `b` without touching the disk, and it doesn't care whether a is a directory or exists. The normalized result of `/../../a` is `/a`, because the root directory's above is still the root. `../b` at the beginning has no root to cancel, and it is left as is, and the empty string is still empty after normalization. If you really want to parse the path according to the current state on the disk, that's the canonical one, which will access the file system.

```text
== [3] path::value_type 与 c_str()(Linux 侧) ==
  sizeof(path::value_type) = 1
  string_type 即 std::string:yes
  printf("%s", p.c_str()) = /usr/bin/env(可直接喂 C API)
  ...(Windows 侧的括注一行,内容已写进下文,略)...

== [4] 迭代 path 的元素 ==
  "/usr/local/include" -> [/] [usr] [local] [include]
  "a/b/c" -> [a] [b] [c]
  "a/b/" -> [a] [b] []
  "/" -> [/]
  "." -> [.]
  ".." -> [..]
  "/abs/path/../x" -> [/] [abs] [path] [..] [x]
```

[3] gives the Linux side's口径, we look at it line by line: path::value_type is char, string_type is std::string, and c_str() returns `const char*` which can be directly fed to C APIs, and fopen in e1_trio eats it. On the Windows side, value_type is wchar_t, and c_str() gives const wchar_t*. Cppreference clearly states that c_str() and native().c_str() are identical, and changing the writing doesn't change anything. If you really want to feed narrow characters to C APIs, you have to do a narrow conversion explicitly (like string()), or just switch to wide character APIs. If you want to compare these differences, see [Directory Enumeration and NTFS Family](../../windows/file-io/04-dir-enum.md), the mirror article on the Windows side of directory enumeration, which is ready for comparison. [4] hides a small trick: path iteration is segmented by slashes, `a/b/` has an empty segment after the trailing slash, and using it as a filename would be a joke, remember to filter out empty segments when processing segments.

Another unexpected incident I encountered was when using fs::relative to find the relative path: its internal use of weakly_canonical parses the existing parts of the path into real names, and symbolic links are unraveled. e1_trio wanted to show the original path output by the iterator, so it used lexically_relative(), otherwise the entries under link_mid would all show the real path of mid, and the experiment would be self-talk.

## follow_directory_symlink: The Loop Left by E1

In E1, we said that the default option does not descend into directory links, so the loop cannot enter. Let's try to open follow_directory_symlink and see if libstdc++ itself detects loops. The tree in e1_loop is small: the root has the loop directory and the real file keep.txt, and inner in loop is a link pointing to `..`. We look at this `..` from loop, it falls back to the root, not loop itself, and we go in, back to the root, and the loop is formed. The result is:

```text
$ ./e1_loop
== 默认选项:directory_options::none ==
  depth=0 loop (symlink=n)
  depth=1 loop/inner (symlink=Y)
  depth=0 keep.txt (symlink=n)
  共 3 条,正常终止(目录链接不下降,环根本没被走进去)

== follow_directory_symlink ==
  step=1     depth=0   path=loop
  step=2     depth=1   path=loop/inner
  ...(step 3 到 8 每步加深一层,path 在 loop 与 inner 之间往复变长;step 9 到 82 延续爬深,step 83 到 99 已转入 keep.txt 的收拢段,一并略)...
  step=100   depth=46  path=loop/inner/.../loop/inner/keep.txt
  退出时状态:steps=123,it==end ? yes, last_ec.value()=0
  结论:123 步停了,keep.txt(全树就一个真文件)被访问 41 次 —— 同一个环转了 41 圈,遍历结果已被污染
```

It did stop, but the stopping mechanism is three layers of coincidence. The first coincidence is that libstdc++ itself does not detect loops, and we also can't find any requirements in the standard, and the original words of cppreference are that when the directory structure has loops, the end iterator may be unreachable, and the detection is left to the programmer. The second coincidence is in the descent step: it uses openat (parent fd, name) for single-component relative opening, and only parses one symbolic link at each step, never reaching the link layer limit:

```text
openat(80, "inner", O_RDONLY|O_CLOEXEC|O_DIRECTORY) = 81
openat(81, "loop", O_RDONLY|O_CLOEXEC|O_DIRECTORY) = 82
```

What really stops the iterator is the third layer: the attribute query for type. newfstatat (the modern form of the stat family, querying by full path) gets a path where symbolic links are stacked to the 41st, and the kernel returns ELOOP — the maximum number of symbolic links in a path is 40, which is a hard-coded upper limit in path_resolution(7), and the 41st exactly breaks it:

```text
newfstatat(AT_FDCWD, "/home/charliechen/l04_scratch/e1_loop/loop/inner/loop/inner/...(中略,loop/inner 重复了 41 对)...", ...) = -1 ELOOP (Too many levels of symbolic links)
```

The most interesting part is the outcome of this ELOOP: the library suppresses it, and ec is also clean, and no exceptions are thrown. The iterator silently stops descending, and DFS ends at end(). So what you see is the exit state it == end and ec is 0, keep.txt appears 41 times in the result, and it never reports a single error throughout. The three coincidences are all in place for this "exactly stops" outcome, and if we switch to another implementation or caching strategy, it may not stop anymore. If you really want to follow directory links, the self-protection route is standard: we use fs::canonical with a visited set, and when we see a real name, we call it.disable_recursion_pending(), and don't let the iterator cover for you.

## E6 Practical: Dual-Tree Diff and One Misreport of mtime

We have gone through the trio, error forms, permissions, performance, path algebra, and even the loop, and now we will take on a real task: the difference list between two directory trees. The criteria are existence, size, and mtime, and we don't read a single byte of content. The walk to collect the list uses std::map, with the key being the relative path, and the red-black tree solves the order problem for us:

```cpp
// e6_diff.cpp(节选):两棵树各收一张清单
static std::map<std::string, Info> walk(const char* root)
{
    std::map<std::string, Info> out;
    std::error_code ec;
    for (auto& e : fs::recursive_directory_iterator(
             root, fs::directory_options::skip_permission_denied, ec)) {
        Info info;
        info.type = e.is_directory(ec) ? 'd' : e.is_regular_file(ec) ? 'f' : 'l';
        if (info.type == 'f') info.size = e.file_size(ec);
        info.mtime = e.last_write_time(ec);
        out[fs::relative(e.path(), root).string()] = info;
    }
    return out;
}
```

This code ties together the things we've covered in this article: collecting the tree uses recursive iteration, and when querying attributes, we bring ec with us (E2's approach), and the skip_permission_denied option allows the scan to continue even if the directory is inaccessible. The relative in the key has no links to parse in these two trees, and it doesn't cause any trouble. If your tree has links, remember the reminder from E5. A side note: mtime returns a time point from std::chrono::file_clock (introduced in C++17 for file timestamps in `<chrono>`), and when printing, you have to go through clock_cast to convert to the system clock, and the formatting is then handed over to strftime.

The two comparison trees we used are built with setup (setup is the mode run by e6_diff when building the tree), and the arrangement is deliberate: each side has a directory and file that only exists on the other side, a mod.txt that has actually changed content, a samesize.txt that has four bytes swapped with four bytes, a keep.txt that has only touched the timestamp, and a stable.txt that is exactly the same in all three aspects:

```text
$ ./e6_diff
left 7 条 / right 7 条;判据 size+mtime,不读内容

== 新增(右有左无) ==
  + dir_new                d size=0 mtime=17:31:04
  + only_right.txt         f size=4 mtime=17:31:04
== 删除(左有右无) ==
  - dir_old                d size=0 mtime=16:31:04
  - only_left.txt          f size=4 mtime=16:31:04
== 修改(两边都有,元数据对不上) ==
  M keep.txt               mtime (5->5 字节,16:31:04->17:31:04)
  M mod.txt                size mtime (5->9 字节,16:31:04->17:31:04)
  M samesize.txt           mtime (4->4 字节,16:31:04->17:31:04)

未变 1 条(stable.txt:内容+size+mtime 全同)。

== 对照:真 diff -rq ==
Only in /home/charliechen/l04_scratch/e6_right: dir_new
Only in /home/charliechen/l04_scratch/e6_left: dir_old
Files /home/charliechen/l04_scratch/e6_left/mod.txt and /home/charliechen/l04_scratch/e6_right/mod.txt differ
Only in /home/charliechen/l04_scratch/e6_left: only_left.txt
Only in /home/charliechen/l04_scratch/e6_right: only_right.txt
Files /home/charliechen/l04_scratch/e6_left/samesize.txt and /home/charliechen/l04_scratch/e6_right/samesize.txt differ
(diff -rq exit=1)
```

The list and the diff -rq report correspond one by one, and the addition and deletion of entire directories are folded into one line, and the granularity is aligned with diff. The most interesting example is keep.txt: the content on both sides is exactly the same, and setup only moved the timestamp on the left side by an hour, and the metadata criteria reported it as M, while diff -rq on the other side didn't report it at all. If this seems familiar, it's because rsync's quick check uses the same criteria of size plus mtime, saving the cost of reading content, but the price is that files with touched timestamps will be transmitted again. samesize.txt complements the other half: the same number of bytes swapped with different content, the size criteria can't catch it, and the mtime is the one that reveals the secret. If even the mtime is completely consistent, the metadata criteria reach the end, and the only way to distinguish is to read the content. The boundary is right here, and you know when you must read the content.

## What the Other Side Sees

std::filesystem is cross-platform, but the underlying layers do not follow suit. On Windows, path::value_type is changed to wchar_t, and c_str() returns const wchar_t*, and the two static assertions in E5 are left as cross-platform comparison points. Directory enumeration on the Win32 side uses FindFirstFileW and FindNextFileW, and the hard links, junctions, and symbolic link family of NTFS, and the hash order and constant 0777 links of ext4 have their own comparisons, and we will read the mirror article on directory enumeration on the Windows side. The Win32 foundation is now in place, and you can see [Win32 File I/O](../../windows/file-io/01-win32-file-io.md).

<ReferenceCard title="Reference Resources">
  <ReferenceItem
    :id="1"
    title="std::filesystem"
    publisher="cppreference"
    url="https://en.cppreference.com/w/cpp/filesystem"
  />
  <ReferenceItem
    :id="2"
    title="std::filesystem::directory_iterator"
    publisher="cppreference"
    url="https://en.cppreference.com/w/cpp/filesystem/directory_iterator"
  />
  <Reference项
    :id="3"
    title="std::filesystem::recursive_directory_iterator"
    publisher="cppreference"
    url="https://en.cppreference.com/w/cpp/filesystem/recursive_directory_iterator"
  />
  <ReferenceItem
    :id="4"
    title="std::filesystem::exists"
    publisher="cppreference"
    url="https://en.cppreference.com/w/cpp/filesystem/exists"
  />
  <ReferenceItem
    :id="5"
    title="readdir(3)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man3/readdir.3.html"
  />
  <ReferenceItem
    :id="6"
    title="getdents64(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/getdents.2.html"
  />
  <ReferenceItem
    :id="7"
    title="path_resolution(7)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man7/path_resolution.7.html"
  />
  <ReferenceItem
    :id="8"
    title="ext4 Directory Structure"
    publisher="The Linux kernel documentation"
    url="https://docs.kernel.org/filesystems/ext4/directory.html"
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
