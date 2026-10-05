# 02-file-mapping:《文件映射》Windows 侧配套实验

[02-file-mapping.md](../../../../../../../documents/vol8-domains/systems-programming/windows/file-io/02-file-mapping.md) 的真机实验与原始输出存档。五个子目录各管一块:视图保护语义(VirtualProtect)、SEC_RESERVE 两段式、Prefetch/Offer 一族、ReadFile vs 映射选型实测、大页特权门槛。`.out` 全部是当轮机器的原始捕获(`$` 开头的行是当时敲的命令),结论行都带 .out 对应。

## 环境

- Windows 11 26200(26H2 线),测试文件放 `%TEMP%`(C: 盘 NTFS,NVMe,WD_BLACK SN7100,PowerShell `Get-PhysicalDisk` 口径;盘型在程序内两条路都探不到,见意外发现第 7 条)
- 物理内存 61.7 GiB(空载可用约 35 GiB;映射基准的"暖缓存"有充足空间,别在内存紧张的机器上复刻同样的暖数字)
- MSYS2 UCRT64 g++(Rev 5)16.1.0,x86_64-w64-mingw32;除 e4 基准加 `-O2` 外全部 `-std=c++20 -Wall -Wextra`
- 编译运行(WSL interop,cwd 必须在 WSL 文件系统上):
  ```text
  /mnt/c/msys64/ucrt64/bin/g++.exe -std=c++20 -Wall -Wextra xxx.cpp -o xxx.exe
  chmod +x xxx.exe && ./xxx.exe
  ```
- 崩溃阶段(0xC0000005)经 WSL interop 回来的 `$?` 是 5:`0xC0000005` = 3221225477,shell 只留低 8 位。要看全码得在程序里自己打(SetUnhandledExceptionFilter 就是干这个的)

## 子目录与结论速览

| 目录 | 主题 | 一句话结论 |
|---|---|---|
| [01-virtualprotect/](01-virtualprotect/) | 视图上的 VirtualProtect | 只读视图硬写 → 0xC0000005(写冲突);保护只能在 MapViewOfFile 授予范围内变,FILE_MAP_READ 视图升 RW 一律 err=87,哪怕映射本身 PAGE_READWRITE;范围内降级/升回全成功且 lpflOldProtect 如实出参 |
| [02-sec-reserve/](02-sec-reserve/) | SEC_RESERVE 先保留后提交 | 保留态 State=MEM_RESERVE(Protect 实测 0)、访问 → 0xC0000005;VirtualAlloc(MEM_COMMIT) 逐段提交后读写全对,空洞保持保留态;这就是 Windows 版懒分配,对照 Linux 匿名 mmap 的 overcommit |
| [03-prefetch-offer/](03-prefetch-offer/) | Prefetch/Offer 一族 | 预取不接管工作集缺页(扫已预取区照样每页 +1),做的是把页拉进 standby list;offer 后工作集 36→4 MiB 立竿见影,Discard 后 8192/8192 页全零 |
| [04-readfile-vs-map/](04-readfile-vs-map/) | ReadFile vs 映射选型 | 256MiB 同文件:暖顺序映射快 2.8×、随机 4KiB 快 5.8×;但映射首触一轮 93ms 比 ReadFile 还慢——首触成本要靠复访摊销 |
| [05-large-pages/](05-large-pages/) | 大页特权门槛 | GetLargePageMinimum=2MiB;AdjustTokenPrivileges 返回 TRUE 但 GetLastError=1300(经典陷阱),大页申请随后 1314——需要特权未测,如实入档 |

## E4 计时表(256MiB 同文件,3 轮中位,QueryPerformanceCounter)

| 场景 | 中位 | 带宽 / 均摊 | 备注 |
|---|---|---|---|
| ReadFile 顺序整读(1MiB 块) | 51.08 ms | 5012 MiB/s | 暖缓存 |
| MapViewOfFile 顺序扫 | 18.18 ms | 14081 MiB/s | 暖;**轮1 含首触 93.45 ms** |
| ReadFile 随机 4KiB ×10000 | 31.93 ms | 3.19 µs/op | 暖缓存;SetFilePointerEx+ReadFile 两次系统调用/op |
| MapViewOfFile 随机 4KiB ×10000 | 5.54 ms | 0.55 µs/op | 暖;轮1 含首触 15.27 ms |
| ReadFile 顺序(NO_BUFFERING) | 82.20 ms | 3114 MiB/s | 绕过缓存的冷读侧写,全表最慢 |

读法(对照 [Linux 侧 L02](../../../../../../../documents/vol8-domains/systems-programming/linux/file-io/02-mmap-memory-mapping.md) 的 512MiB 双机基准口径——那边同样是"数字只在那两台机器上成立"):

1. **本机数字只代表本机**:NVMe + 61.7 GiB 内存,暖缓存全表都是内存速度,换台机器绝对值全变,相对关系(随机场景映射占优的幅度)更有参考价值。
2. 暖顺序:映射赢在少一次内核到用户缓冲的拷贝(18 vs 51 ms);但**首触一轮 93 ms 比谁都慢**——65536 个缺页(哪怕全是软缺页)每个约 1 µs,映射的账要复访才划算。
3. 随机 4KiB:映射赢 5.8×,这就是"指针即访问"对"seek+read 两次系统调用"的差距,数据越零碎映射越占优。
4. NO_BUFFERING 冷读 82 ms ≈ 映射首触 93 ms:一个真从盘上搬,一个补缺页,数量级相同——冷场景下两种读法差距远没有暖场景戏剧化。
5. 校验和全对上(顺序 34225520640、随机 5237682176,两法各自与理论值一致),读的确实是同一份数据。

## 0xC0000005 异常码路径(SEH 篇的衔接素材)

三个崩溃点(E1 两个、E2 一个)走的是同一条路,由各程序安装的 `SetUnhandledExceptionFilter` 统一记录:

| 触发 | ExceptionInformation[0] | 含义 |
|---|---|---|
| 只读视图写入(E1 ro-write) | 1 | 写访问冲突 |
| PAGE_NOACCESS 视图读(E1 noaccess-read) | 0 | 读访问冲突 |
| SEC_RESERVE 保留态写入(E2 touch) | 1 | 写访问冲突 |

- 异常码统一是 `0xC0000005`(STATUS_ACCESS_VIOLATION,十进制 3221225477);区分读写看 `ExceptionInformation[0]`(0=读、1=写、8=DEP 执行),目标数据地址在 `[1]`,触发指令地址在 `ExceptionRecord->ExceptionAddress`。
- 现象记录到此为止;接住它(__try/__except / VEH / 向量化处理)是 [03-seh-veh](../../../../../../../documents/vol8-domains/systems-programming/windows/file-io/03-seh-veh.md) 的事。
- 两个顺带观察:崩溃时 stdout(管道下全缓冲)里没冲刷的内容直接丢——崩溃前要 `fflush`;WSL interop 下进程退出码 3221225477 被 shell 截成 `$?=5`。

## MinGW 头文件情报(要不要 GetProcAddress)

本机 MSYS2 UCRT64 g++ 16.1.0,**以下 API 全部在头文件里声明、import 库里可直接链接、kernel32 确实导出**(头文件直取地址与 GetProcAddress 交叉验证一致,见 03-prefetch-offer 的 probe 阶段):`PrefetchVirtualMemory` / `OfferVirtualMemory` / `ReclaimVirtualMemory` / `DiscardVirtualMemory` / `QueryVirtualMemoryInformation`。机制:`_mingw.h` 默认 `_WIN32_WINNT=0xA00`,而这套 API 的 `#if` 门槛(Win8/Win8.1)全被盖过。**不需要 GetProcAddress 应对**。真正要记录的坑:

1. **枚举常量名与 Windows SDK 文档不同**:MinGW 头是 `VmOfferPriorityVeryLow/Low/BelowNormal/Normal`,SDK 文档写 `VMOfferPriority*`(就中间那个 m 的大小写不同)——照文档抄代码在 MinGW 编不过。
2. `FILE_STORAGE_INFO`(winbase.h)是**截断版**,缺 SDK 尾部的 `BusType/FileSystemType/MediaType` 三字段;要用得自己补全结构体。
3. `SE_LOCK_MEMORY_NAME` 在不定义 UNICODE 时是窄字符,配 `LookupPrivilegeValueW` 编不过,直接写 `L"SeLockMemoryPrivilege"`。
4. 其余老朋友:`std::exchange` 要 `<utility>`;wprintf 与 printf 混用会让流取向反转、后续输出静默丢失。

## 意外发现 / 坑

1. **视图保护只降不升(超出 MapViewOfFile 授权的升级一律 err=87)**——包括"文件读写打开 + 映射 PAGE_READWRITE + 视图 FILE_MAP_READ"这种看似只差临门一脚的组合。教训:要写的区段一开始就按 FILE_MAP_ALL_ACCESS 拿授权,VirtualProtect 只用来收紧。
2. VirtualProtect 失败时 `lpflOldProtect` 出参**不是没动**:实测两次失败调用都把它写成了 1(PAGE_NOACCESS)。别信失败调用的出参。
3. **真文件句柄 + SEC_RESERVE 在 Win11 26200 上静默成功**:CreateFileMapping 不报错,视图直接 MEM_COMMIT/PAGE_READWRITE,可写,文件被零扩展到映射对象大小(16 KiB → 1 MiB)。文档口径是 SEC_RESERVE 属页文件后备;实测文件后备会把"保留"直接兑现成"提交"。别指望它报 ERROR_INVALID_PARAMETER。
4. 保留态的 `VirtualQuery.Protect` 实测是 **0**,不是 PAGE_NOACCESS(State=MEM_RESERVE 才是判据)。
5. **PrefetchVirtualMemory 的收益边界**:它把缺页"提前"到自己调用内同步发生,但不减少之后扫描的工作集缺页计数;暖缓存下扫预取区/未预取区计时无差(0.026 vs 0.026 s),冷侧写(NO_BUFFERING 建文件)下 A 比 B 快约三成(0.024 vs 0.034 s)——本机 fast IO 能吃掉大部分预取红利,可观察上限就这么多。进程 IO 计数器(ReadTransferCount)看不到文件映射的调页,两个区都记 0。
6. **Offer/Reclaim 的丢弃路径需要真实内存压力**:32 MiB 私有内存 offer(VeryLow)→ reclaim ×8 轮,内容 8/8 完好,offer 后工作集立刻 36→4 MiB 是唯一稳定可观察量;DiscardVirtualMemory 则是确定性自弃,8192 页全零。
7. **盘型探测两条路都走不通**(普通权限):`IOCTL_STORAGE_QUERY_PROPERTY` 对文件句柄 err=87(两段式也 err=1),`GetFileInformationByHandleEx(FileStorageInfo)` err=50,卷句柄 `\\.\C:` err=5(要管理员);环境口径最后用 PowerShell `Get-PhysicalDisk` 落的。
8. E1 重开文件验证落盘时,原句柄 dwShareMode=0 独占着,重开拿的是 INVALID_HANDLE_VALUE(sharing violation,读回空白)——先关映射和文件句柄再重开。

## 复跑注意

- 每个 `.cpp` 头部有编译/运行命令与观察点清单,崩溃阶段单独跑、`$?` 记录在 .out。
- E4 复跑顺序照 .out:`envinfo → create → readfile-seq → map-seq → readfile-rand → map-rand → readfile-nobuf`,场景间共享暖缓存,别拆到不同会话乱序跑;跑完 `%TEMP%\sysprog-e4-256m.bin`(256 MiB)记得删。
- 指针值/句柄值/进程环境浮动,规律与结论行可对;E4 绝对毫秒数每轮必浮动,别追着复刻。

## 相关目录

- 契约工具(unique_handle / last_error_code)的定义与初轮实验:[../../thinking/](../../thinking/)
- 同卷 Win32 文件 I/O 补课实验(指针/flush/sharemode):[../01-win32-file-io-supplement/](../01-win32-file-io-supplement/)
- SEH/VEH 篇实验存档:[../03-seh-veh/](../03-seh-veh/)
- Linux 侧 mmap 对照(L02 双机基准 + 匿名映射 overcommit):[../../../linux/file-io/02-mmap-memory-mapping/](../../../linux/file-io/02-mmap-memory-mapping/)
