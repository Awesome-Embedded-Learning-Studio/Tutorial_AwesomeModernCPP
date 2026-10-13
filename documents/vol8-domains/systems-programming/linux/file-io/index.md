---
title: "Linux 文件 I/O"
sidebar_order: 10
description: "POSIX 文件 I/O:fd、open/read/write、dup/dup3 重定向、fcntl 两类标志、pread 与稀疏文件探测、页缓存与 fsync,以及 mmap 内存映射"
platform: host
tags:
  - cpp-modern
  - host
  - intermediate
  - 系统编程
  - POSIX
---

# Linux 文件 I/O

<ChapterNav variant="sub">
  <ChapterLink num="1" href="01-posix-file-io" desc="strace 实测 fd 的一生、flags 与 umask、部分读写、fd 表与 dup2/dup3 重定向、fcntl 两类标志的归属、pread 不动偏移、SEEK_DATA/SEEK_HOLE 摸清稀疏文件、页缓存与 fsync 崩溃窗口;unique_fd、sys_call 与 errno_code 沿用思维基石两篇的定义">POSIX 文件 I/O:open/read/write 与 fd 的一生</ChapterLink>
  <ChapterLink num="2" href="02-mmap-memory-mapping" desc="读文件变成拿指针摸内存,read 从页缓存到用户缓冲的那次拷贝被整个省掉;六个参数逐个对上行为后果、mapped_region RAII、实测首摸缺页远贵于二次访问(台机约五十倍、笔记本个位数倍)、/proc/self/maps 看见映射、文件在背后被截短摸越界吃 SIGBUS、mprotect 与 guard page 的两类 si_code、MAP_SHARED 跨进程立刻可见、Dirty 计数把可见与已写回当面分开、写时复制,512 MiB 顺序读的胜负跟着机器走:台机 read 赢约 15%,笔记本 mmap 反超约四分之一">mmap 内存映射:把文件贴进地址空间</ChapterLink>
  <ChapterLink num="3" href="03-page-cache" desc="write() 顺利返回只是把数据拷进了内核页缓存,盘上的事还没发生:Dirty 计数看着 256 MiB 脏页滞留整整三十秒后无人调 sync 骤降(dirty_expire 到点 flusher 动手)、_exit(0) 与 kill -9 三场景实测进程死了数据都在而 stdio 用户态缓冲才是丢字节的那层(fprintf 100 行 _exit 后 0 字节)、plain/fsync/fdatasync/O_SYNC 四路计时(fsync 与 fdatasync 一个价、O_SYNC 配 4KiB 小块只剩 3 MiB/s,小块加直写的代价不挑机器)、fixed/append/touch 三种脏法把 fsync 与 fdatasync 的元数据差测成 touch 场景 4.7 倍稳定信号、dd 默认模式跑完留 262 MB 脏页的旁证,附 rm 扔脏页与 _exit 不刷 stdio 两个意外发现">页缓存与持久性:write() 返回之后发生了什么</ChapterLink>
  <ChapterLink num="4" href="04-filesystem" desc="从裸 POSIX 升到标准库:libstdc++ 的 directory_iterator 实测完全不排序(顶层序与手搓 readdir 逐项相同,ext4 大目录走 HTree 吐名字散列序)、同一个 ENOENT 在 exists/is_directory/status/file_size 手里四种对待、迭代中目录被 rm -rf 的两种结局(平铺版吃 32 KiB 缓冲陈旧名单 3001 条只见 1022 条静默截断,递归版在下降点真报错)、nofollow 改链接权限在 Linux 吃 EOPNOTSUPP、space() 的 free 与 available 差 5.09% 恰是 ext4 root 预留、万文件遍历 2.16/3.42/3.43/16.22 ms 四档解剖(strace 证明差价在每条目构造 fs::path 不在 syscall)、follow_directory_symlink 的环靠内核 40 层链接上限兜住而库把 ELOOP 压住不报、双树 diff 实战中 mtime 误报正是 rsync 快速检查的同款代价">std::filesystem:目录与元数据</ChapterLink>
  <ChapterLink num="5" href="05-file-lock" desc="两个进程抢一个文件,谁让谁:flock 的锁挂在打开文件描述上(同进程重新 open 自冲突、阻塞版 2 秒 alarm 取证从未返回、close 最后一个引用才释放、fork 出的子进程能替父放锁),fcntl 记录锁按字节区间挂在进程上(F_GETLK 报对方锁自己的区间、后锁把重叠段切成两段),全篇最大的陷阱 E2d:同进程 close 该文件的任意一个 fd,全部记录锁当场释放,而 Linux 3.15 起的 F_OFD_SETLK 没有这个问题;/proc/locks 逐字段解码(十进制 inode 的实测教训)、fork/dup/exec 继承对照主表、RAII file_lock 的 try_lock_for 轮询时序、8 进程串行化 8090.5 ms 对 1009.9 ms 与 16 µs 次交接的价、tmpfs 逐行一致的 VFS 层佐证,NFS 未测如实标注">文件锁:flock 与 fcntl 记录锁</ChapterLink>
  <ChapterLink num="6" href="06-inotify" desc="程序怎么知道文件系统发生了什么:inotify 把事件变成 fd 上可读的字节流。实测一次 open(O_CREAT) 挤出 IN_CREATE 与 IN_OPEN 两条、mkdir 的 mask 是 0x40000100(IN_ISDIR 是叠在 mask 里的状态位)、rename 的 FROM/TO 靠 cookie 配对(实测 8596)、sizeof(inotify_event)=16 而 len 向 16 对齐;watch 不递归(touch 孙目录 0 个事件)、绑 inode 不绑路径(文件改名 IN_MOVE_SELF 继续跟、原路径换新 inode 后文件级 watch 全程沉默、旧 inode unlink 三连),文件级 watch 在配置热重载的 rename 顶替面前脱靶;递归监控要动态补挂加整棵兜底 walk、目录整棵改名后 wd 表路径已过期;合并的真条件是四元组 wd/mask/cookie/name 全同且相邻(同文件 200 写折成 1 条而 4 文件轮流 200 条全在),16384 队列被 587 万次写灌爆只读回 16385 条、IN_Q_OVERFLOW 垫在队尾而丢掉的部分没有任何记录;inotify fd 与 timerfd、pipe 同挂一个水平触发 epoll 单循环调度;同设备跨目录 mv 两侧 cookie 配对,跨设备 EXDEV 后 mv 退化为复制加删除、cookie 全程 0;cookie 身份实测为内核全局计数器">inotify 文件监控:把文件系统的动静变成事件流</ChapterLink>
</ChapterNav>
