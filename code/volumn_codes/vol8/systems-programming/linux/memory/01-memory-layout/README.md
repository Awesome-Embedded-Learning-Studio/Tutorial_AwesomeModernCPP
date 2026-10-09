# 01-memory-layout —— 进程内存布局与 /proc/pid/maps 配套实验

《进程内存布局与 /proc/pid/maps》的实验代码与原始输出存档,五组实验 E1–E5 对应五个子目录。每个程序都在运行中读**自己的** /proc/self/{maps,smaps},不存在他人视角的竞态;`.out` 均为 2026-10-03 在本仓库目录内编译运行的原样捕获(未加工,含 ASLR 随机地址)。

## 目录与实验对照

| 目录 | 实验 | 主结论 |
|---|---|---|
| `01-maps/` | E1 maps 全图解剖 | 41 段分六类;每个文件模块两个 r-- 段(第二个是 GNU_RELRO);rw 段后的匿名 rw 是 .bss 接续页;本机内核有 [vvar_vclock]、无 [vsyscall];r--p×21 / r-xp×7 / rw-p×13 |
| `02-var-locations/` | E2 变量落位 | 18 类变量 18/18 对表;大 .bss 跨段(起点在文件 rw 尾页、中段进匿名接续页);new 4MB 与 mmap 直配同居匿名区 |
| `03-boundaries/` | E3 段边界动态 | malloc 分水岭实测 **131050**(两层机制:堆顶有富余当场切、chunk 尺寸 roundup(req+8,16) 大于等于 131072 才走 mmap);栈顶不动、低地址端下探;64MB free 后映射立即 munmap,brk 堆会 trim 收缩;free 大块抬高 glibc 动态阈值,3MB 回 [heap] |
| `04-smaps/` | E4 smaps 对照 | Rss/Pss 差别(libc .text 1004kB→Pss 7kB);heap Rss=Private_Dirty;THP=madvise 下 THPeligible=0;刚编译的二进制 .text 是 Private_Dirty(页缓存未回写),sync 后变 Clean |
| `05-aslr/` | E5 ASLR | 5 次全变(exe 跨约 13.9TiB、libc 约 15.5TiB、stack 7.09GiB);setarch -R 5 次逐字节全同(exe=555555554000,heap 紧贴程序,gdb 默认即此世界) |

## 实验环境

- WSL2,内核 6.18.33.2-microsoft-standard-WSL2;glibc 2.44
- g++ 16.2.1(GCC)20260810;编译口径统一 `g++ -std=c++20 -Wall -Wextra -Wpedantic -O2`(零警告)
- THP:`/sys/kernel/mm/transparent_hugepage/enabled` = `madvise`;`ulimit -s` = 8192KB
- pmap(procps)、setarch(util-linux)已装;05 的对照需要 setarch

## 统一编译

每个子目录单独一条命令,无公共头、无 CMake:

```sh
cd 0N-xxx && g++ -std=c++20 -Wall -Wextra -Wpedantic -O2 0N-xxx.cpp -o 0N-xxx && ./0N-xxx
```

各子目录 README 有逐条复现命令与结论细节。与姊妹篇的分工:L02 mmap 篇只用 maps 观察过映射区本身,本篇给整个进程地址空间收口成一张全图。
