# 02-vm-apis 配套实验(虚拟内存 API 全景:mprotect/madvise/mlock)

《虚拟内存 API 全景》一文的实验代码与原始输出存档。环境:WSL2,内核 6.18.33.2-microsoft-standard-WSL2,g++ 16.2.1 `-std=c++20 -Wall -Wextra -O2`。源文件名与文章实验编号一一对应(`e1.cpp`~`e6.cpp`);`common/` 是从 file-io 卷 L01/L02 原样搬来的三件工具(`unique_fd`/`sys_call`/`errno_code` 与信号安全的 `write_all`/`write_hex`),本组实验主要用到 `sigout.hpp`。

与 L02 mmap 篇的分工:L02 讲过 mprotect 的基本语义(整页生效、addr 页对齐、`SEGV_ACCERR`/`SEGV_MAPERR` 两类病因、文件视图权限切换);本组从那里接着往下深讲。

## 目录与实验对照

| 目录 | 实验 | 一句话结论 |
|---|---|---|
| `01-mprotect-deep/` | E1 mprotect 深讲 | si_addr 逐字节跟随出错的那个字节(不按页取整);W^X 三步切换 + Linux 放行 RWX(int3 实测 `SIGTRAP/si_code=SI_KERNEL(128)/si_addr=0`);text/rodata/heap/stack 加宽全部 rc=0,动不得的是 [vvar](EACCES) 与 [vdso](EINVAL),[vsyscall] 本机无此映射;另附 GOT 懒解析撞降权 .data 页的坑(seg8) |
| `02-madvise/` | E2 madvise 全家 | RANDOM 关预读(恰好 1024 KiB 驻留)、NORMAL 自适应(3088↔8192 KiB 抖动,见 rerun)、WILLNEED 整段预读 8192 KiB;DONTNEED 匿名页归零 + Rss 8192→4096 kB;DONTFORK 子进程映射消失 + `SEGV_MAPERR`;REMOVE 私有匿名 EINVAL,ext4/tmpfs 共享映射都能真打洞(st_blocks 24→16,man 页"仅 tmpfs/shmem"的说法在这台内核上不成立) |
| `03-mlock/` | E3 mlock | mlock 顺手预故障(未写过 mincore 已 11000000);munlock 只解锁不逐出;RLIMIT_MEMLOCK 本机 64 MiB(恰好等于限额成功、+1 页 ENOMEM、锁满后再补一页 ENOMEM);mlockall(MCL_CURRENT) 成功(RSS 4 MB < 限额);swap 现状 16 GiB /dev/sdc 未用、swappiness=60 |
| `04-guarded-buffer/` | E4 招牌:越界检测器 | `guarded_buffer<T>` 尾对齐 guard 页:越界 1 字节 → `si_addr == 请求地址 == guard 首字节`,handler 直接报"越界第 1/4096 字节";越界读同样拦;对照组无 guard 静默写脏;siglongjmp 让进程活着继续 |
| `05-mincore/` | E5 mincore | 驻留位图全程;只读未写的页 mincore=1 而 smaps Rss 不涨(共享零页口径分叉);文件映射 mincore 问页缓存(WILLNEED 后 11 而 Rss=0);MADV_DONTNEED 文件页只丢页表,页缓存还在,真逐出要 fadvise(10 = 映射页受保护) |
| `06-process-vm-readv/` | E6 跨进程读 | 父进程 32 字节直读子进程改动(父副本 COW 不动);未映射远端地址 EFAULT、假 pid ESRCH |

## 构建

需要 Linux(WSL2 可)。两种方式:

```sh
# 单发(在 code/volumn_codes/vol8/systems-programming/linux/memory/02-vm-apis 下)
g++ -std=c++20 -Wall -Wextra -O2 -I common 04-guarded-buffer/e4.cpp -o /tmp/e4

# 或整套 CMake
cmake -S . -B build && cmake --build build
```

`-O2` 不只是性能开关:e1 的 seg8(GOT 懒解析坑)、rodata 常量折叠坑,都是在 `-O2` 下才以这个形态现形的。

## 复跑注意

- **数据文件路径烧死在源码里**:e2 要往 `/home/charliechen/lm02_scratch/` 写 64 MiB 的 `ra.bin`(ext4 真盘,tmpfs 上预读实验不成立),e5 在同目录建 2 页临时文件。复跑前 `mkdir -p ~/lm02_scratch`,或改源码开头的 `kRaPath` 常量。
- **地址每次都变**:输出里的映射基址、si_addr 都吃 ASLR;文章引用的规律是"si_addr == 出错字节地址"这类等式,不是具体数值。
- **e2 的 (A) 部分数字会抖**:readahead 窗口是自适应的,NORMAL 在 3088~8192 KiB 之间跳(主档 + 两个 rerun 就是三组实测);RANDOM 恒等于 1024 KiB、SEQUENTIAL 稳定在 3080 KiB 档。单次数字别当常数引用。
- **e1 的 seg5/seg8 依赖"进程还没读过 errno"**:复跑时保持原顺序;换成 `-Wl,-z,now` 全急解析,seg8 就演示不出来了。
- **e3 依赖本机 RLIMIT_MEMLOCK=64 MiB**:限额不同的机器上,超限边界那三行的数字要按 `getrlimit` 输出重算。
- **swap 换出延迟没做成实测**:本机有 swap 但 Used=0,确定性 forcing swap-out 需要 root(drop_caches)或危险的水位压力,输出里如实记为"recorded as-is"。
- **e4 输出里 int 那幕的头行文案有瑕疵**:`== guarded_buffer<int>(10): data[10] is 4 bytes into guard ==` 与 handler 报的"越界第 1/4096"字面上打架——按实现,`di[10]` 的首字节恰好落在 guard 首字节,handler 的口径是对的,头行想说的其实是"一个 4 字节的访问落进了 guard"。源码没改,读档时以 handler 行为准;文章引块已略去这行头。
