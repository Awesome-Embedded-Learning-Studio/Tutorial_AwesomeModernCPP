---
title: "Windows 文件 I/O"
sidebar_order: 10
description: "Win32 文件 I/O:句柄、CreateFileW、ReadFile/WriteFile,以及文件映射 CreateFileMapping 与 MapViewOfFile"
platform: host
tags:
  - cpp-modern
  - host
  - intermediate
  - 系统编程
  - Win32
---

# Windows 文件 I/O

<ChapterNav variant="sub">
  <ChapterLink num="1" href="01-win32-file-io" desc="失败值两套约定、公共工具沿用思维基石两篇的定义(check_win32 定义在本篇)、dwShareMode 五行矩阵与 FILE_SHARE_DELETE 的删除语义、SetFilePointerEx 三基准与越过 EOF 的零洞、FlushFileBuffers 三层计时、句柄泄漏计数、SetStdHandle 与 CRT 的分家、手搓复制器胜 copy_file 四倍">Win32 文件 I/O:句柄、CreateFileW 与同步读写</ChapterLink>
  <ChapterLink num="2" href="02-file-mapping" desc="两步走:登记映射对象、再贴视图;偏移按 64 KiB 分配粒度对齐而不是 4 KiB 页、失败值是 NULL 不是 INVALID_HANDLE_VALUE、映射句柄提早关视图照活、FILE_MAP_COPY 私有副本不进文件、FlushViewOfFile 只刷脏页不刷元数据、SIGBUS 剧本在 Windows 拍不成(文件在映射之下砍不动);VirtualProtect 只能在视图授权范围内收紧(升 RW 一律 err=87)、SEC_RESERVE 保留-提交两段式(真文件句柄配它在 Win11 上静默退化)、Prefetch/Offer 没替咱们挡掉工作集缺页、256 MiB 选型基准暖顺序映射快 2.8 倍但首触 93 ms 比 ReadFile 还慢;mapped_view RAII 收口">文件映射:CreateFileMapping 与 MapViewOfFile</ChapterLink>
  <ChapterLink num="3" href="03-seh-veh" desc="Windows 侧访问出错的完整的机制链:0xC0000005 的异常记录解剖(info[0] 0=读/1=写/8=DEP 执行、info[1] 精确到字节)、VEH 头插链序与修现场后同一条写指令重放、MinGW 没有 __try 关键字只有 __try1 宏加 UCRT 变体 filter ABI、没人处理时退出码就是异常码本身、IN_PAGE_ERROR 的剧本被 ERROR_USER_MAPPED_FILE 拦死而越文件尾读到零填充、MinGW throw 走 0x20474343 且 SEH 作用域截得住穿越的 C++ 异常">结构化异常:SEH 与 VEH</ChapterLink>
  <ChapterLink num="4" href="04-dir-enum" desc="Linux 侧 std::filesystem 的镜像篇:FindFirstFileW 三件套把名字、属性、三时间与 64 位大小一次带回(4.5 GiB 实证 nFileSizeHigh 有货),NTFS 的枚举序实测是大小写折叠后的字典序($I30 的 B+ 树)而文档明说 does no sorting(ext4 散列序的对照面)、错误码三分法(目录不存在 3、模式无匹配 2、空目录挂 * 吐出 . 与 ..、文件挂 * 是 267、尾杠被当模式)、*.htm 靠 8.3 短名捎带 longname.html、宽字符名的九种输出姿势矩阵里只有手动 WideCharToMultiByte(CP_UTF8) 全对(C locale 的 %ls 静默丢字还报成功、.UTF8 对代理对 emoji 仍丢,wprintf/printf 混用弄哑在 UCRT 不复现)、递归对 junction 默认不下钻而 follow 的环转 22 圈由 MAX_PATH 拦停(Linux 是内核 40 层 ELOOP,自备的深度闸没轮上)、NTFS 家族矩阵(硬链接同 FileIndex 与 nNumberOfLinks、符号链接无特权 1314、悬空 junction 的查询压根不跟目标、跨卷可枚举、稀疏 1 GiB 实占 128 KiB、exFAT 负对照全拒 err=1)、FindFirstFileExW 的 LARGE_FETCH 稳定快约 19% 但提示位不是承诺位、LongPathsEnabled=1 只放行带 longPathAware 清单的应用而设备前缀抬到 32767、尾杠三家收一家不收、fs::path 对拍与 Linux E5 同款;unique_find 与 to_utf8 两件新工具">目录枚举与 NTFS 家族</ChapterLink>
  <ChapterLink num="5" href="05-lockfileex" desc="Windows 侧的文件锁:LockFileEx 按字节区间上锁,给出 flock(挂打开文件描述)与 fcntl(挂进程)之外的第三种答案——冲突判定没有属主豁免,同一个句柄对自己的第二把独占锁也回 33(ERROR_LOCK_VIOLATION),唯一特例是同句柄独占叠共享、解锁要两次。解锁与释放的权限认进程与文件对象的组合:子进程继承的句柄加不进也放不掉(33/158)、DuplicateHandle 复制品放得掉、全新 open 放不掉、持锁进程一退出锁被系统当场收走。区间锁还会真挡别的句柄的 ReadFile/WriteFile(半强制,POSIX 两族是咨询锁)。另有长度 0 实测是空区间、锁过 EOF 不报错、有限等待两路(轮询与 FILE_FLAG_OVERLAPPED 加事件)、unique_file_lock 的 RAII 收口、同机对读计时(Sleep(10) 实睡约 16 ms 撑起绝对差,归一后两边都是约 8:1),收束进三方对照主表">文件锁:LockFileEx</ChapterLink>
</ChapterNav>
