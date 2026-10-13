---
title: "Linux 进程内存"
sidebar_order: 20
description: "进程地址空间:/proc/pid/maps 全图、虚拟内存 API、共享内存与对齐大页"
platform: host
tags:
  - cpp-modern
  - host
  - intermediate
  - 系统编程
  - POSIX
  - 内存管理
---

# Linux 进程内存

内存这一章咱们从地址空间本身起步:第一篇把一个 C++ 程序在 `/proc/self/maps` 上的 41 段认全,后面的虚拟内存 API、共享内存与对齐大页,全都踩着它的底子。全系列公共工具(`unique_fd`、`sys_call`、`errno_code`)沿用[思维基石](../../thinking/)两篇的定义,本目录的文章只引用、不再重定义。

<ChapterNav variant="sub">
  <ChapterLink num="1" href="01-memory-layout" desc="一个 C++ 程序跑起来,地址空间里每一段住的是谁:41 段归六个大类,认出每个文件模块的第二个 r-- 段是 GNU_RELRO 圈出、动态链接器重定位完 mprotect 只读的那一页;18 类变量对表 18/18 一致,.bss 跨段(开头借文件 rw 段尾页落脚、中段进匿名接续页),&printf 落在 libc 的 .text;malloc 分水岭实测 131049/131050,补测 strace 揭出真机制是堆顶有富余当场切、chunk 尺寸 roundup(请求+8,16) 大于等于 131072 才走 mmap,free 掉 4MB 还会把 glibc 动态阈值抬到 4MB;brk 会 trim 收缩、栈顶不动低地址端下探;Rss 与 Pss 把 libc .text 的 1004kB 摊派成 7kB,刚编译二进制的 .text 竟是 Private_Dirty,sync 之后转 Clean;ASLR 五跑全变,setarch -R 五跑逐字节全同,正是 gdb 默认看到的那个世界">进程内存布局:/proc/pid/maps 全图</ChapterLink>
  <ChapterLink num="2" href="02-vm-apis" desc="拿到一段虚拟内存之后的三层管理:mprotect 改页权,si_addr 逐字节跟随出错的那个字节(修正 L02 的页首粗读法),W^X 三步曲里 Linux 放行 RWX 而 macOS 回 EPERM,text/rodata/heap 加宽全部 rc=0 走 COW,真拒绝的是 [vvar] 的 EACCES 与 [vdso] 的 EINVAL;降权 .data 页之后首次冷调用死于 ld.so 写 GOT 的懒解析,handler 进门第一件事是把页权恢复;madvise 全家,RANDOM 恰好零预读、NORMAL 在 3088/8192 KiB 之间自适应抖动、DONTNEED 匿名页读回零且 Rss 减半、DONTFORK 让子进程整段消失、REMOVE 在 ext4 也真打洞;mlock 顺手预故障,贴着 RLIMIT_MEMLOCK 边界量出 ENOMEM;招牌 guarded_buffer<T> 用尾对齐 guard 页让越界第几字节当场报出,mincore 的 1 有零页/页缓存/真驻留三种口径,process_vm_readv 跨进程读一眼,文末预告 Windows PAGE_GUARD 的一次性陷阱">虚拟内存 API 全景:mprotect/madvise/mlock</ChapterLink>
  <ChapterLink num="3" href="03-shm" desc="两个没有亲缘的进程共用同一片物理页:shm_open 的名字空间就是 /dev/shm 的目录项,新对象尺寸为 0 要 ftruncate,未 unlink 二次创建吃 EEXIST 而 unlink 后同名重建畅通,旧映射读 0x1111 新实体写 0x2222 互不可见,名字与实体就此解耦,umask 截权限(root 的 0600 挡住 user,CAP_DAC_OVERRIDE 反向放行);竞态三版实测:nosync 三轮丢 9.62%~23.70%,汇编佐证 addq 无 lock 前缀,PTHREAD_PROCESS_SHARED 互斥 44.7 ms 全中、进程间信号量 58.1 ms 对照;两条共享途径(匿名映射配 fork 继承、命名对象配陌生进程,SCM_RIGHTS 传 fd 一句带过);招牌 SPSC 无锁环形队列一百万条零丢失零乱序,sched_yield 背压比 CPU 自旋快六成以上,归因 cache line 乒乓、钉核对照排除同核调度;1 MiB 搬运 7304 对 2349 MiB/s 约三倍于 pipe;memfd_create 不留名字 close 即消失">共享内存:shm_open 与映射</ChapterLink>
  <ChapterLink num="4" href="04-align-hugepage" desc="对齐三件套 aligned_alloc/posix_memalign/对齐 new 全采样达标而报错口径分三家(NULL+errno、EINVAL 当返回值、bad_alloc 异常);未对齐地址喂 _mm256_load_si256 立即 SIGSEGV,那是 #GP 不是缺页;THP 是重头:出厂状态 MADV_HUGEPAGE 只拿到 AnonHugePages=0,顺诊断链摸出 WSL2 的 init 给进程树设了 MMF_DISABLE_THP,压过 sysfs 的第三层开关,prctl 清掉后 1 GiB 全部大页化,首触写快 5.43 倍;显式大页 MAP_HUGETLB 三档全 ENOMEM;Overcommit 的数字链:512 GiB 被拒、MAP_NORESERVE 立过、VmSize 涨满而 VmRSS 不动;OOM Killer 以只读观察加安全演示收章">对齐分配与大页:32 字节的对齐、2 MiB 的页与一次被拒的 512 GiB</ChapterLink>
</ChapterNav>

咱们 Linux 侧的内存四篇到这里就齐了,路线与[总纲](../../00-overview.md)的学习路线对齐。
