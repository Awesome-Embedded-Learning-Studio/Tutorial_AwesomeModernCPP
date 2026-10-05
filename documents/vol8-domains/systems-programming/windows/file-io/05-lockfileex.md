---
title: "文件锁:LockFileEx"
description: "Windows 侧镜像 Linux 文件锁的一篇:LockFileEx 与 UnlockFile 按字节区间上锁,实测给出 flock(挂打开文件描述)与 fcntl(挂进程)之外的第三种答案——冲突判定没有属主豁免,同一个句柄对自己的第二把独占锁也回 33(ERROR_LOCK_VIOLATION),唯一特例是文档写明的同句柄独占叠共享、解锁要两次。解锁与释放的权限认进程与文件对象的组合:子进程继承的句柄加不进也放不掉(33/158)、DuplicateHandle 复制品放得掉、同进程全新 open 放不掉、持锁进程一退出锁就被系统当场收走。区间锁还会真挡别的句柄的 ReadFile/WriteFile(咱们叫它半强制,POSIX 两族都是咨询锁)。另有长度 0 实测是空区间(fcntl 的 l_len=0 锁到 EOF,方向相反)、锁过 EOF 不报错、UnlockFile 区间要精确匹配(158)、有限等待两路(FAIL_IMMEDIATELY 轮询与 FILE_FLAG_OVERLAPPED 加事件的原生等待,后者 Linux 侧没有)、unique_file_lock 的 RAII 收口,以及同一台机器上 flock 与 LockFileEx 的对读计时:Sleep(10) 实睡约 16 ms 撑起全部绝对差,归一后两边都是约 8:1 的串行比"
chapter: 8
order: 5
platform: host
difficulty: intermediate
cpp_standard: [20]
reading_time_minutes: 20
prerequisites:
  - "Win32 文件 I/O:句柄、CreateFileW 与同步读写"
  - "OS 资源的 RAII 范式:fd、HANDLE 与映射的同一副骨架"
related:
  - "文件锁:flock 与 fcntl 记录锁"
  - "错误处理范式:从 errno 到 expected"
  - "文件映射:CreateFileMapping 与 MapViewOfFile"
tags:
  - host
  - cpp-modern
  - intermediate
  - 系统编程
  - Win32
  - mutex
current_status:
  title: 正在打磨
  detail: 本批刚写完,正在通读打磨,表述与实测口径可能随时调整
  variant: polishing
---

# 文件锁:LockFileEx

从 W01 的 ReadFile 到 W02 的文件映射,再到上一篇的目录枚举,文件这一侧咱们始终是一个进程在单干:读写、贴视图、接异常、点目录,连第二个竞争者的影子都没出现过。真实的程序迟早要面对另一个进程:同一份配置被两个实例打开,A 写到一半的时候,B 偏偏这时候进来读,读到的就是半份 ticket。W01 里咱们见过 dwShareMode,那是 `CreateFileW` 门口的共享声明:演示程序里那个叫 keeper 的句柄用 0 共享模式把文件挂住,第二个句柄哪怕要的只是读,也被 32 拒在了门外。可它管的是打开那一刻的静态限制,句柄活着的时候声明也就原样挂着,它表达不了 A 改这一段的时候请 B 暂时别进来这样的动态互斥。能补上这块的,Windows 侧就只有 LockFileEx 了。

Linux 那边的答案,咱们在镜像篇里实测过两套:flock 的锁挂在打开文件描述上,fcntl 的记录锁挂在进程身上,完整的证据链在 [文件锁:flock 与 fcntl 记录锁](../../linux/file-io/05-file-lock.md)里。LockFileEx 给出的归属比两边都陌生,值得您在动手以前就知道:它的冲突判定不看属主,同一个句柄对自己的第二把锁也照挡。解锁的权限认的是进程与文件对象的组合,子进程就算拿着继承来的句柄,也放不掉父进程立下的锁。它的锁还会真挡别的句柄的 ReadFile 与 WriteFile,而 POSIX 那两族是咨询锁,只挡同样调锁的进程。本篇咱们就在同一台机器的 NTFS 上,照 Linux 篇的剧本把语义矩阵一组一组拍下来。

编号与环境的口径,咱们交代在开头。本篇实验的编号是 e1 到 e4,对应存档的 01-matrix、02-try-lock、03-raii、04-contention 四个目录,代码与原始输出都收在仓库的 `code/volumn_codes/vol8/systems-programming/windows/file-io/05-lockfileex/` 下面。e1 的大输出里,小节的标号是 E1a 到 E1h 再加一个 E1w,另有 I3 到 I5 的三段,标号沿用存档的原样:中间跳过的字母没有对应的小节,E1w 在输出里的位置夹在 E1c 与 E1d 之间,存档里的 I1 与 I2 压根不存在,您对着文件找的时候按实数来对就好。机器是 Win11 26200 的本机,也就是 Linux 侧 05 存档用的同一台(Ryzen 7 9700X),编译器是 MSYS2 UCRT64 的 g++ 16.1.0,编译统一给的是 `-std=c++20 -Wall -Wextra`,拿到的是零警告,输出的捕获日期是 2026-10-02。数据文件的路径烧死在 `C:/msys64/tmp/l05win/`(NTFS 系统盘),编译与运行咱们都从 WSL 经 interop 调起,链路的要点 W01 讲过了。多进程的时间线有个纪律:时间戳的零点跟着命令行传给每个子进程,各自把日志写进自己的文件,主进程收尾的时候再按时间戳归并,谁拿到了锁谁落笔,出来的顺序才做不了假。咱们用的工具这回一件都不新:`unique_handle` 定义在 [OS 资源的 RAII 范式](../../thinking/01-raii-paradigm.md)里,`last_error_code` 的定义在[错误处理范式](../../thinking/02-error-paradigm.md)里。倒是有一处您别误会,冲突时的 FALSE 在本篇是正常的答案而不是错误,所以 e 系列没有套 check_win32,只把返回值连同 GetLastError 直接打了出来。

## 上手:六个参数,两个 flag,一个必填的结构

`LockFileEx(HANDLE, dwFlags, dwReserved, lenLow, lenHigh, lpOverlapped)` 的参数表不长,咱们捡要紧的过。头一个说的是句柄,文档要求句柄的权限至少带着 `GENERIC_READ` 或 `GENERIC_WRITE` 里的一样。`dwFlags` 里挑的是锁型与等待方式:`LOCKFILE_EXCLUSIVE_LOCK` 请求独占,不给它的话就是共享。`LOCKFILE_FAIL_IMMEDIATELY` 决定冲突时的行为,给了它就立刻返回 FALSE,而同步句柄上的调用会睡进去等,文档的原话是 `"If the file handle was not opened for asynchronous I/O and the lock is not available, this call waits until the lock is granted or an error occurs, unless the LOCKFILE_FAIL_IMMEDIATELY flag is specified"`。失败的长相是 FALSE 加 GetLastError,冲突时拿到的错误码是 33,它的名字叫 `ERROR_LOCK_VIOLATION`,意思是另一个进程已经锁住了文件的这一段。您拿它对表 Linux 侧:flock 冲突回的是 -1 加 EWOULDBLOCK(11),fcntl 记录锁回的是 -1 加 EAGAIN,Windows 这边的形状是 FALSE 加 33。

长度的传递按 64 位拆成两半,夹在中间的 `dwReserved` 按文档要求保持 0。区间的起点不在参数表里,它住在末尾那个 `lpOverlapped` 指的结构里,而且文档写明这个参数是 required。**OVERLAPPED** 是 Win32 描述一次 I/O 请求的小结构,本篇是它头一回真正出场:区间的起点放在 `Offset` 与 `OffsetHigh` 两个成员里,异步完成的通知则交给 `hEvent`,这个成员的用场留到讲有限等待的章节再说。解锁这边对应的是 `UnlockFile(HANDLE, offLow, offHigh, lenLow, lenHigh)`,五个参数全是明面上的数字,区间的起点也写在明面上,咱们不用再借结构。实验里咱们把调用封装成了两个小函数:

```cpp
// matrix.cpp(节选):上锁与解锁的封装,区间起点住在 OVERLAPPED 里
LockR lockx(HANDLE h, unsigned long long off, unsigned long long len, DWORD flags)
{
    OVERLAPPED ov{};              // lpOverlapped 是必填项:区间起点住在 Offset/OffsetHigh
    ov.Offset = (DWORD)off;
    ov.OffsetHigh = (DWORD)(off >> 32);
    SetLastError(0);
    BOOL ok = LockFileEx(h, flags, 0, (DWORD)len, (DWORD)(len >> 32), &ov);
    return {ok, ok ? 0 : GetLastError()};
}

LockR unlockx(HANDLE h, unsigned long long off, unsigned long long len)
{
    SetLastError(0);
    BOOL ok = UnlockFile(h, (DWORD)off, (DWORD)(off >> 32),
                         (DWORD)len, (DWORD)(len >> 32));
    return {ok, ok ? 0 : GetLastError()};
}
```

基本行为咱们起手拍三组,输出取自 e1 的存档,时间戳是相对程序启动的毫秒数:

```text
==== E1a  LOCKFILE_EXCLUSIVE_LOCK 互斥:A 拿 → B 阻塞 → A 放 → B 接棒 ====
  [     15 ms] A(pid=20400):LockFileEx([0,1048576) EX,阻塞版) = TRUE,持锁
  [    265 ms] B:LockFileEx(同区间,阻塞版)调用中 …
  [    812 ms] A:UnlockFile [0,1048576) = TRUE → B 的拿锁时刻应紧贴这一行
  [    812 ms] B(pid=8004):阻塞版返回 = TRUE,拿到锁(A 一放就接棒)

==== E1b  共享锁(不带 EXCLUSIVE):两边同时持有 ====
  [    828 ms] 共享A(pid=24772):LockFileEx([0,1048576) 不带 EXCLUSIVE) = TRUE,共享锁到手(与另一边同时持有)
  [   1093 ms] 共享B(pid=25132):LockFileEx([0,1048576) 不带 EXCLUSIVE) = TRUE,共享锁到手(与另一边同时持有)
  [   1234 ms] 探针C:两个共享锁都在场时,EXCLUSIVE|FAIL_IMMEDIATELY 试同区间 = FALSE, GetLastError()=33(ERROR_LOCK_VIOLATION)(共享之上独占进不来)

==== E1c  LOCKFILE_FAIL_IMMEDIATELY:冲突 = FALSE + GetLastError()=33 ====
  [   1703 ms] A:hA: LockFileEx [0,100) EX = TRUE
  [   1703 ms] B:hB: LockFileEx [0,100) EX|FAIL_IMMEDIATELY = FALSE, GetLastError()=33(ERROR_LOCK_VIOLATION)(同区间被占,立刻回来)
  [   1703 ms] B:hB: LockFileEx [100,200) EX|FAIL_IMMEDIATELY = TRUE(不相交就放行)
```

E1a 的时序值得您多看一眼:A 放锁的 UnlockFile 与 B 阻塞版返回 TRUE 落在同一个 812 ms 里,放锁与接棒之间没有看得见的缝隙,这与 Linux 侧 E1a 的观察同款,交接的价钱留到计时的小节再算。E1b 说的是共享:两个进程的共享锁可以同时在场,独占的请求想插进来就吃 33,跟读写锁的规则是同构的。E1c 把非阻塞的口径定下来了:带上 `LOCKFILE_FAIL_IMMEDIATELY` 的调用,冲突的时候立刻回 FALSE 加 33,不带它的场合就睡进去,这就是咱们后面试锁的探针统一用的姿势。

区间的冲突按字节交叠判得半点不含糊,咱们直接看 E1d 的输出:

```text
==== E1d  字节区间:A 锁 [0,100),B 试 [50,150) 冲突、[100,200) 成功 ====
  [   1703 ms] A:hA: LockFileEx [0,100) EX = TRUE
  [   1703 ms] B:hB: LockFileEx [50,150) EX|FAIL = FALSE, GetLastError()=33(ERROR_LOCK_VIOLATION)(与 [0,100) 交叠 50 字节)
  [   1703 ms] B:hB: LockFileEx [100,200) EX|FAIL = TRUE(相邻不相交 → 拿到)
  [   1703 ms] B:hB: LockFileEx [99,101) EX|FAIL = FALSE, GetLastError()=33(ERROR_LOCK_VIOLATION)(单字节交叠也算冲突)
```

`[50,150)` 与 `[0,100)` 交叠了 50 个字节,挡了。`[100,200)` 跟 `[0,100)` 是相邻而不相交的关系,也就放行了。最狠的是 `[99,101)`:只借了人家一个字节,得到的照样是 33。一个文件的不同区段可以交给不同进程各管一段,这样的分片用法咱们在 LockFileEx 与 fcntl 记录锁身上都见得到,整文件一把锁的 flock 反而给不了。

## 同一个句柄,也挡自己

到本篇头一个重头了。咱们把同一个进程、同一个句柄,对着自己已持有的锁再锁一次,会发生什么?flock 的答案是转换,重复调用会把旧锁换成了新锁。fcntl 的答案是改写,后到的锁把重叠段接管了下来。LockFileEx 的答案,您看输出:

```text
==== E1e  同句柄区间语义:重复加锁、重叠、部分解锁 ====
  [   1703 ms] 本进程:h1: LockFileEx [0,100) EX = TRUE
  [   1703 ms] 本进程:h1: 同句柄再锁 [50,150) EX(带 FAIL_IMMEDIATELY 防挂死)= FALSE, GetLastError()=33(ERROR_LOCK_VIOLATION)
  [   1703 ms] 本进程:   → 对照:fcntl 同进程后锁直接改写重叠段、flock 同 fd 是转换,LockFileEx 连自己名下都挡
  [   1703 ms] 本进程:h1: UnlockFile [0,100) = TRUE(唯一的锁就是它,整段放干净)
  [   1703 ms] 探针:h2 试 [0,50) = TRUE;[50,150) = TRUE(确认场上已无锁)
  [   1718 ms] 本进程:h1: LockFileEx [0,100) EX = TRUE
  [   1718 ms] 本进程:h1: 原地再锁 [0,100) EX|FAIL = FALSE, GetLastError()=33(ERROR_LOCK_VIOLATION)(第二把没立起来)
  [   1718 ms] 本进程:h1: UnlockFile [0,100) 第一次 = TRUE
  [   1718 ms] 探针:h2 试 [0,100) = TRUE(放一次就干净 —— 没有计数、没有第二把)
  [   1718 ms] 本进程:h1: UnlockFile [0,100) 第二次 = FALSE, GetLastError()=158(ERROR_NOT_LOCKED)
  [   1718 ms] 本进程:h1: UnlockFile [10,20)(部分解锁)= FALSE, GetLastError()=158(ERROR_NOT_LOCKED)
  [   1718 ms] 本进程:h1: UnlockFile [0,100)(整段)= TRUE
  [   1718 ms] 本进程:h1: LockFileEx [0,100) EX = TRUE
  [   1718 ms] 本进程:h1: 同句柄再锁 [0,100) 共享(不带 EXCLUSIVE,带 FAIL)= TRUE(文档特例:同句柄可叠)
  [   1718 ms] 本进程:h1: UnlockFile [0,100) 第一次 = TRUE
  [   1718 ms] 探针:EX 试 [0,100) = FALSE, GetLastError()=33(ERROR_LOCK_VIOLATION);共享试 [0,100) = TRUE(看剩下的是哪一把)
  [   1718 ms] 本进程:h1: UnlockFile [0,100) 第二次 = TRUE
  [   1718 ms] 探针:EX 试 [0,100) = TRUE(两把都该放干净了,EX 探针才作数)
```

咱们拿同一个句柄给自己的第二把独占锁,回的也是 33。这可不是实现上的偶然,文档的原话把它写得干干净净:`"Exclusive locks cannot overlap an existing locked region of a file"`,独占锁不能压在已锁的区间上,原话里没有属主的豁免,任何现存的锁都算数,包括自己刚才立的那把。三种机制在同一个小实验上给出了三种脾气:flock 那边同描述的重复调用是转换,fcntl 那边同进程的后锁改写重叠段,LockFileEx 谁的面子都不给,冲突的判定只看区间交不交叠。这在工程上有实打实的后果:同一段代码对同一句柄上两次锁,在 Linux 的两族里都能活着回来,搬到 Windows 就成了 FALSE,搬代码的朋友最容易在这里栽。

唯一例外的路,文档也写明了:`"A shared lock can overlap an exclusive lock if both locks were created using the same file handle"`,同一个句柄的名下,共享锁反而能叠在独占锁之上。输出末尾那组就是它的实测:EX 之上再叠一把共享,返回的是 TRUE。文档连解锁的次序都给了:`"If the same range is locked with an exclusive and a shared lock, two unlock operations are necessary to unlock the region; the first unlock operation unlocks the exclusive lock, the second unlock operation unlocks the shared lock"`。第一次 UnlockFile 放掉的是独占,咱们拿探针验过,此刻仍被挡住的是 EX 探针,而共享探针畅通无阻,说明场上剩下的正是那把共享,第二次的调用才真正清了场。

解锁的规则里还有一条硬要求:区间要精确地匹配。咱们拿 `[10,20)` 去部分解锁,回的是 FALSE 加 158(`ERROR_NOT_LOCKED`),整段一次放掉的回执才是 TRUE。文档的原话说 `"The region to unlock must correspond exactly to an existing locked region"`,而且反过来也成立,两段相邻的锁不能拼成一次解锁。对照 fcntl 那边 `F_UNLCK` 随便拆段合并的自由,而 Windows 这边没有同等的自由:上锁时记下的区间,解锁的时候一个字节都不能差。

## 锁属于谁:进程加文件对象的组合

归属的问题一旦定下来,close、继承、退场的全部行为就都跟着定了,flock 认的是打开文件描述,fcntl 认的是进程,LockFileEx 认什么?文档没有给出一句直接的回答,咱们靠 E1f、E1g、E1h、I3、I4、I5 这六段输出把它量出来。

头一组的实验里,咱们让同一个进程开两个句柄。h1 锁了 `[0,100)`,同进程第二次 `CreateFileA` 得到的 h2 再去试同区间:

```text
==== E1f  两个句柄:同进程 CreateFileA 两次,互相冲突吗 ====
  [   1718 ms] 本进程:h1: LockFileEx [0,100) EX = TRUE
  [   1718 ms] 本进程:h2(同进程第二次 open):LockFileEx [0,100) EX|FAIL = FALSE, GetLastError()=33(ERROR_LOCK_VIOLATION)
  [   1718 ms] 本进程:   → 同进程两个句柄互相挡:冲突判定没有属主豁免,进程身份救不了
  [   3218 ms] 本进程:1.5 s 过去,对 h2 的阻塞版 LockFileEx 仍未返回(线程 g_woken=0)
  [   3218 ms] 本进程:   → 放锁的人永远不来:同进程两句柄,自己挡死了自己
  [   3531 ms] 本进程:CloseHandle(h1) 之后:g_woken=1 —— 阻塞线程应声拿到并已放掉:它等的正是「另一个句柄」

==== E1g  CloseHandle:该句柄名下所有区间一起放 ====
  [   3531 ms] A:hA: [0,100) = TRUE,[200,300) = TRUE,两把都上身
  [   3531 ms] B:hB 试 [0,100) = FALSE, GetLastError()=33(ERROR_LOCK_VIOLATION);[200,300) = FALSE, GetLastError()=33(ERROR_LOCK_VIOLATION)
  [   3531 ms] B:hA CloseHandle(一次 UnlockFile 都没调)后:试 [0,100) = TRUE;[200,300) = TRUE
```

E1f 的三段输出,咱们连起来读才有味道。进程的身份救不了 h2,试锁的结果照样是 33。阻塞版的下场更绝,咱们派了个线程对 h2 调不带 FAIL_IMMEDIATELY 的 LockFileEx,等了 1.5 秒,线程纹丝没醒:能放掉 h1 那把锁的人就是咱们自己,可咱们的线程正堵在 h2 的等待里,而放锁的动作永远不会到来。这是 flock 那边 E1b 见过的局面:自己挡住了自己。3531 ms 那一行给了答案,等到 CloseHandle(h1) 一落地的时候,阻塞线程应声就醒了,它等的从来不是另一个进程,只是另一个句柄名下的锁。E1g 补的是 CloseHandle 的语义:两把区间锁上了身,咱们一次 UnlockFile 都不调,句柄关上的那一刻,名下的全部区间就一起放了,连善后都不用咱们操心。

第二组咱们看句柄的继承:`CreateProcess` 带上 `bInheritHandles=TRUE`,子进程拿到的继承句柄与父进程指向同一个文件对象,它能干点什么?

```text
==== E1h  句柄继承(bInheritHandles):继承来的句柄能干什么 ====
  [   3531 ms] 父:可继承句柄 hL(SA.bInheritHandle=TRUE):LockFileEx [0,100) EX = TRUE
  [   3531 ms] 子(pid=28016):① 自己 CreateFileA 的新句柄:LockFileEx [0,100) EX|FAIL = FALSE, GetLastError()=33(ERROR_LOCK_VIOLATION) —— 「我是它孩子」不算数
  [   3531 ms] 子:② 继承来的句柄值(与父指向同一文件对象):LockFileEx [0,100) EX|FAIL = FALSE, GetLastError()=33(ERROR_LOCK_VIOLATION)
  [   3531 ms] 子:③ UnlockFile(继承句柄,[0,100)) = FALSE, GetLastError()=158(ERROR_NOT_LOCKED)
  [   3547 ms] 父:子进程退场后,父探针试 [0,100) = FALSE, GetLastError()=33(ERROR_LOCK_VIOLATION)
  [   3547 ms] 父:   → ②③ 连起来读:继承句柄既加不进去(冲突判定不豁免任何人)、也放不掉父的锁(解锁认进程)

---- I3 对照:bInheritHandles=FALSE,句柄值照样传过去 ----
  [   3547 ms] 父:普通句柄(不可继承):LockFileEx [0,100) EX = TRUE
  [   3547 ms] 子(pid=17940):LockFileEx(父传来的句柄值) = FALSE, GetLastError()=6(ERROR_INVALID_HANDLE);GetFileType=0(错误 6) —— bInheritHandles=FALSE 时,句柄值没有跨进程的意义
  [   3547 ms] 父:对照组的锁毫发无损:父探针 [0,100) = FALSE, GetLastError()=33(ERROR_LOCK_VIOLATION)(应仍被挡)
```

子进程的三连测把路全堵死了:自己的新句柄去试锁,回的是 33,亲缘关系在这里没有任何的效力。继承来的句柄试锁,回的还是 33,哪怕它与父句柄指向的是同一个文件对象。咱们再拿继承句柄去 UnlockFile 放父亲的锁,回的是 158(`ERROR_NOT_LOCKED`),在系统看来那把锁根本就不在子进程的名下。文档对这件事有一句对应的原话:`"If the file handle is inherited by a process created by the locking process, the child process is not granted access to the locked region. If the locking process opens the file a second time, it cannot access the specified region through this second handle until it unlocks the region"`。您拿它对照 flock:flock 的子进程经继承 fd 与父共享同一把锁,一句 LOCK_UN 的调用就能替父放掉,而 Windows 这边整个走不通。I3 顺带补了个对照,在 `bInheritHandles=FALSE` 的场合,句柄值传了过去也只是个数字,子进程拿它一用就收到了 6 号错误,跨进程的意义上一个都没有。

第三组咱们问锁的寿命:持锁的进程退场了,锁跟着谁走?I4 的安排是这样:proxy 进程在可继承句柄上锁住偏移 1000、长度 200 的区间,spawn 出的 sleeper 继承这个句柄去睡 1200 ms,proxy 随即就退了场,全程的 UnlockFile 一次都没落地:

```text
---- I4 锁的寿命:持锁进程退出,继承句柄还在别人手里 ----
  [   3562 ms] proxy(pid=27840):可继承句柄上锁 [1000,200) EX = TRUE,接着 spawn sleeper、自己立即退出(全程不 UnlockFile)
  [   3562 ms] sleeper(pid=20468):攥着继承句柄睡 1200 ms,什么都不做
  [   3562 ms] 探针:proxy 进程已退出(全程没 UnlockFile,但 sleeper 攥着同一文件对象的句柄):试 [1000,200) = TRUE
  [   5265 ms] 探针:sleeper 也退场后,再试 [1000,200) = TRUE
```

锁的主人退场,系统当场就把锁收走了,sleeper 手里攥着同一文件对象的继承句柄,也帮不上任何的忙,探针在同一毫秒就拿到了。文档的原话是 `"If a process terminates with a portion of a file locked or closes a file that has outstanding locks, the locks are unlocked by the operating system"`,它同时补了一句保留:`"However, the time it takes for the operating system to unlock these locks depends upon available system resources"`,清理要花的时间看资源,文档还劝咱们退出以前显式解锁。本机的实测里清理是当毫秒完成的,不过那是本地 NTFS,网络文件系统的口径咱们不替它打包票。

第四组验的是归属判定的最后一环:咱们让同一个进程备下两份句柄,一份是 DuplicateHandle 复制来的,另一份是全新 open 的,谁放得掉 h1 立的锁?

```text
---- I5 归属单位判别:DuplicateHandle、新 open、关句柄次序 ----
  [   5265 ms] 本进程:h1(新 open): LockFileEx [0,100) EX = TRUE
  [   5265 ms] 本进程:DuplicateHandle 复制出 h2(与 h1 指向同一文件对象)
  [   5265 ms] 本进程:h2: LockFileEx [0,100) EX|FAIL = FALSE, GetLastError()=33(ERROR_LOCK_VIOLATION)(同进程同对象,照样加不进)
  [   5265 ms] 本进程:h2: UnlockFile [0,100)(放 h1 立的那把)= TRUE —— 同进程的复制品放得掉
  [   5265 ms] 本进程:h1: 重新 LockFileEx [0,100) EX = TRUE
  [   5265 ms] 本进程:h3(全新 open,同进程): UnlockFile [0,100) = FALSE, GetLastError()=158(ERROR_NOT_LOCKED)
  [   5265 ms] 本进程:   → 解锁认进程+文件对象,新 open 的句柄放不掉
  [   5265 ms] 本进程:CloseHandle(h1) 后(h2 还开着同一对象),新句柄试 [0,100) = FALSE, GetLastError()=33(ERROR_LOCK_VIOLATION)
  [   5265 ms] 本进程:h2/h3 也全关后,再试 = TRUE(清场确认)
```

咱们把这六段输出摆在一起,归属的判定规则就能读出来了。解锁的权限认的是**进程与文件对象的组合**:h2 是同进程经 DuplicateHandle 得到的、指向同一文件对象的句柄,所以放得掉。h3 是同进程全新 open 的,指向的是另一个文件对象,所以放不掉,回的是 158。子进程的继承句柄,对象同一个、进程换人了,加锁与放锁两头都落了空。CloseHandle 的语义也顺着这个模型走:该进程在该文件对象上的句柄要是清了零,锁而后才释放。而 h1 关了、复制品 h2 还开着的时候,锁还稳稳地在,等全都关了才清场。冲突的判定干脆连属主都不看,眼里只有区间的交叠。您拿这套模型回看 E1f 里自己挡自己的僵局,而一切都对得上:堵住 h2 上阻塞调用的,是 h1 名下的锁,而在系统的登记里,h1 与 h2 是两个不同的条目。

## 锁真的挡得住别的句柄的读写

Linux 篇讲 flock 与 fcntl 的时候,咱们交代过咨询锁的性质:不调锁的进程直接 write,内核是不拦的。而 LockFileEx 不是咨询锁,咱们直接拿 ReadFile 试:

```text
==== E1w  锁与读写:别的句柄 read/write 真会被挡吗(咨询锁吗) ====
  [   1703 ms] A:hA: LockFileEx [0,100) EX = TRUE
  [   1703 ms] B:hB: ReadFile@[50,10) = FALSE, GetLastError()=33(ERROR_LOCK_VIOLATION) —— 锁内,被挡
  [   1703 ms] B:hB: ReadFile@[250,10) = TRUE(锁外,畅通)
  [   1703 ms] A:hA(持锁者): ReadFile@[50,10) = TRUE(自己不受影响)
  [   1703 ms] B:hB: 放锁后再读 @[50,10) = TRUE(恢复)
  [   1703 ms] A:hA: LockFileEx [0,100) 共享 = TRUE
  [   1703 ms] B:hB: 共享锁下 ReadFile@[50,10) = TRUE(读放行)
  [   1703 ms] B:hB: 共享锁下 WriteFile@[50,10) = FALSE, GetLastError()=33(ERROR_LOCK_VIOLATION) —— 写被挡
```

B 其实什么锁都没调,它只是拿自己的句柄读文件,读的位置落进了锁内区间,ReadFile 就直接回了 FALSE 加 33。同一个句柄挪到锁外的 `[250,10)`,就畅通了。持锁的 A 自己去读锁内,是不受影响的。共享锁的场合,别的句柄读放行、写被挡。文档把这层语义写成了两句话,咱们原样收着:独占的那句是 `"Locking a portion of a file for exclusive access denies all other processes both read and write access to the specified region of the file"`,共享的那句是 `"Locking a portion of a file for shared access denies all processes write access to the specified region of the file, including the process that first locks the region"`,共享锁连头一个上锁者的本人都不许写,这一半句咱们的存档没有单测,咱们按文档口径带过。

咱们管这个性质叫**半强制**。咱们说它强制,是因为不合作的 I/O 真的会被挡回来,POSIX 的咨询锁给不了这层保证,内核那边废弃已久的强制锁才有类似的脾气。咱们说它只算一半,是因为挡住的只是走 ReadFile 与 WriteFile 的路径,而文档里另有一句 `"Locking a region of a file does not prevent reading or writing from a mapped file view"`。照这句原话的说法,经文件映射视图的读写不受限,映射视图这一头咱们没有实测,未测两个字如实写在了存档里。工程上的读法是现成的:别指望锁能拦住所有的读者,但您可以用它保护配额文件、日志翻卷这类合作的写入方都会走 LockFileEx 的场景,挡 I/O 这层在 Linux 上是不存在的福利。

## 长度为 0 的区间是空的,越过 EOF 不是错误

有两处边界值得咱们单独拎出来:拿 fcntl 的习惯直接搬过来,是会翻车的。头一处咱们看长度给 0 的场合:

```text
  [   1703 ms] A:hA: LockFileEx [200,长度0) EX = TRUE(零长度是「空区间」还是「到 EOF」?往下看)
  [   1703 ms] B:探针试 [900,50)(文件 1000 字节,在 200 之后)= TRUE
  [   1703 ms] B:探针试 [250,10)(紧挨 200)= TRUE
  [   1703 ms] 本进程:   → 长度 0 = 空区间,什么都没锁(对照:fcntl 的 l_len=0 是锁到 EOF,Windows 这里不是)
  [   1703 ms] A:hA: LockFileEx [200,1048576) EX = TRUE(把长度给足,锁过 EOF 也没问题)
  [   1703 ms] B:探针再试 [900,50) = FALSE, GetLastError()=33(ERROR_LOCK_VIOLATION)(这回真挡了)
```

fcntl 那边 `l_len=0` 的含义是锁到 EOF,咱们在 Linux 篇的矩阵表里用过它。LockFileEx 的长度 0 实测是空区间,锁住的字节一个都没有,探针紧挨着 200 的位置都畅通无阻。文档对这个取值的说明一个字都没有,答案也就只能靠实测了。想把区间一路锁到文件尾的话,您就得显式给一个足够大的长度,或者用 `GetFileSizeEx` 现算出的长度,照 fcntl 的习惯写 0,得到的是没锁。第二处倒是与 fcntl 一致:锁过 EOF 不是错误,文档的原话是 `"Locking a region that goes beyond the current end-of-file position is not an error"`。咱们把长度给到了 1048576,而文件本身只有 1000 字节,锁还是成立的,后面的写入长出了 EOF 也还在保护圈内。

## 想有限地等:轮询一路,OVERLAPPED 一路

LockFileEx 没有超时的参数,阻塞的版本一等到底,这跟 flock 与 `F_SETLKW` 患的是同一个毛病。Linux 篇那边的解法是 LOCK_NB 加 1 毫秒小步轮询,Windows 这边咱们有两条路。

头一条咱们照旧走轮询,`LOCKFILE_FAIL_IMMEDIATELY` 加 Sleep(10) 的小步,e2 的两场输出:

```text
==== 场景 1:持锁者握 600 ms,waiter 限期 3000 ms ====
  [     15 ms] holder(pid=4360):LockFileEx([0,4096) EX) = TRUE,持锁 600 ms
  [    625 ms] holder:UnlockFile = TRUE,离场
  [    625 ms] waiter:try_lock_for(3000ms) = true:第 32 次尝试拿到(实际等了 500 ms,其余 31 次都是 FALSE+33)

==== 场景 2:持锁者握 1200 ms,waiter 限期 200 ms → 如期超时 ====
  [   1031 ms] holder(pid=10964):LockFileEx([0,4096) EX) = TRUE,持锁 1200 ms
  [   1343 ms] waiter:try_lock_for(200ms) = false:14 次尝试全部 ERROR_LOCK_VIOLATION(33),限期一到就返回,没有死等
  [   2234 ms] holder:UnlockFile = TRUE,离场
```

场景 1 里 waiter 的第 32 次尝试恰好在 500 ms 处拿到,前面的 31 次全是 FALSE 加 33。场景 2 如期地超时,14 次尝试用完了 200 ms 的限期,到点就返回了。轮询的分辨率就是 Sleep 的间隔,这个代价咱们躲不掉。

第二条路是 Windows 原生的,也是 Linux 侧没有的东西,咱们单独讲。咱们用 `FILE_FLAG_OVERLAPPED` 把句柄按异步打开,再对它调不带 FAIL_IMMEDIATELY 的 LockFileEx,冲突的请求不再阻塞,而是返回 FALSE 加 997(`ERROR_IO_PENDING`,重叠 I/O 尚未完成的标准回执),请求转而挂在系统里排队,批准的时刻 `OVERLAPPED.hEvent` 里的事件被置位。而文档把整条链路写得很全:`"The LockFileEx function operates asynchronously if the file handle was opened for asynchronous I/O, unless the LOCKFILE_FAIL_IMMEDIATELY flag is specified. If an exclusive lock is requested for a range of a file that already has a shared or exclusive lock, the function returns the error ERROR_IO_PENDING. The system will signal the event specified in the OVERLAPPED structure after the lock is granted"`。于是 `WaitForSingleObject(事件, 超时)` 天然就是带限期的 try_lock,咱们一次轮询都不用做:

```cpp
// try_lock_overlapped.cpp(节选):异步句柄上,冲突改排队,事件置位即锁到手
HANDLE h = open_async(file);                 // CreateFileA 带 FILE_FLAG_OVERLAPPED
OVERLAPPED ov{};
ov.Offset = 0;
HANDLE ev = CreateEventA(nullptr, TRUE, FALSE, nullptr);   // 手动重置事件
ov.hEvent = ev;
BOOL ok = LockFileEx(h, EX, 0, (DWORD)kRange, 0, &ov);     // FALSE + 997:请求在队列里
DWORD w = WaitForSingleObject(ev, 3000);                   // 限时就交给它
if (w == WAIT_OBJECT_0) {
    DWORD nx = 0;
    BOOL g = GetOverlappedResult(h, &ov, &nx, TRUE);       // 锁到手
}
```

两场实测的输出同样收在 e2 的存档,咱们原样贴上:

```text
==== 场景 1:持锁者握 600 ms,waiter WaitForSingleObject(事件, 3000 ms) ====
  [      0 ms] holder(pid=32904):LockFileEx([0,4096) EX) = TRUE,持锁 600 ms
  [    109 ms] waiter:LockFileEx(异步句柄,不带 FAIL_IMMEDIATELY)= FALSE, GetLastError()=997(ERROR_IO_PENDING) —— 冲突不再阻塞,改挂账排队
  [    609 ms] holder:UnlockFile = TRUE,离场
  [    609 ms] waiter:事件等了 500 ms 置位,GetOverlappedResult = TRUE —— 锁到手,一次也没轮询
  [    609 ms] waiter:UnlockFileEx = TRUE(显式放锁后关句柄)

==== 场景 2:持锁者握 1200 ms,waiter 限期 200 ms → 超时撤单 ====
  [    953 ms] holder(pid=26828):LockFileEx([0,4096) EX) = TRUE,持锁 1200 ms
  [   1062 ms] waiter:LockFileEx = FALSE, GetLastError()=997(ERROR_IO_PENDING),请求在队列里挂着
  [   1281 ms] waiter:WaitForSingleObject = WAIT_TIMEOUT(0x102),等了 219 ms,限期到
  [   1281 ms] waiter:CancelIoEx(定向撤这一笔) = TRUE
  [   1281 ms] waiter:收尾 GetOverlappedResult = FALSE, GetLastError()=995(ERROR_OPERATION_ABORTED) —— 请求作废,锁没到手
  [   2578 ms] 探针:holder 退场后,新句柄试同区间 = TRUE —— 被撤销的请求没有留下锁
```

超时的一侧还差一步收尾:咱们用 `CancelIoEx` 定向撤掉排在队列里的那个请求,收尾的 GetOverlappedResult 回的是 FALSE,给的错误码是 995(`ERROR_OPERATION_ABORTED`)。真正要紧的是探针的最后一行,holder 退场后新句柄立刻拿到了同区间,可见被撤销的请求没有留下任何一把来历不明的锁,队列也干干净净了。异步等待的好处咱们数得出两条:头一条是 CPU 一次轮询都不用做,第二条是等待的分辨率落在了内核的调度粒度上,不再受 Sleep 档位的限制。解锁的这侧也要配套,异步句柄配的是 `UnlockFileEx`,区间照旧拆进了参数与 OVERLAPPED 里。而 Linux 那边没有对应物,flock 与 `F_SETLKW` 的有限等待只能轮询,这一点 Windows 反倒占了上风。

## unique_file_lock:把锁的生死绑进对象

机制咱们看得差不多了,该收进类型里了。Linux 篇那边咱们写过 file_lock,把一把 flock 锁的生死绑在 fd 的生死上。Windows 这边有一处结构性的差异得在动手以前交代:那边的锁与 fd 是一体的,这边的锁与句柄则是两件事,UnlockFile 放锁而不关句柄,而 CloseHandle 连锁带句柄一起放。所以析构走的是两步,显式的 UnlockFile 打头,而 CloseHandle 负责兜底:

```cpp
// unique_file_lock.hpp(节选):构造加锁,析构两步,move-only
class unique_file_lock
{
public:
    explicit unique_file_lock(const char* path, bool exclusive = true,
                              std::uint64_t off = 0, std::uint64_t len = 4096)
        : h_{open_or_die(path)}, off_{off}, len_{len}
    {
        lock_blocking(exclusive);
    }

    ~unique_file_lock()
    {
        if (h_ != INVALID_HANDLE_VALUE) {
            if (locked_) {
                unlock();          // 显式放锁:排查时时间线上有这句可对
            }
            CloseHandle(h_);       // 兜底:句柄一关,名下残余的锁也会被内核放掉
        }
    }

    bool try_lock(bool exclusive = true) noexcept;   // FAIL_IMMEDIATELY 试锁,33 算正常答案

    template <class Rep, class Period>
    bool try_lock_for(std::chrono::duration<Rep, Period> d, bool exclusive = true)
    {
        constexpr DWORD kPollMs = 10;   // 等待分辨率 = 轮询间隔
        const auto deadline = std::chrono::steady_clock::now() + d;
        for (;;) {
            if (try_lock(exclusive)) { return true; }
            if (std::chrono::steady_clock::now() >= deadline) { return false; }
            ::Sleep(kPollMs);
        }
    }

    unique_file_lock(unique_file_lock&& other) noexcept;   // 句柄过户,moved-from 成空壳
    // 其余成员与完整定义见存档 03-raii/unique_file_lock.hpp
};
```

析构里那句显式 unlock 其实是多余的,而 CloseHandle 本来就会放锁,咱们留着它,图的是把放锁写在时间线上看得见的位置,真出了事排查的时候有句可对,这与 Linux 侧 file_lock 的取舍是同一个。`try_lock_for` 这边咱们走的是轮询一路,分辨率就是 10 ms 的档。双进程的时序跑给您看,输出取自 e3 的存档,A 构造完了就持锁写数据,B 把三种姿势各试了一遍:

```text
==== 双进程时序:A 拿独占锁写数据,B 有限等待接棒 ====
  [      0 ms] A(pid=3860):构造 unique_file_lock,已写 "ticket=42",持锁 700 ms
  [      0 ms] B(pid=24272):try_lock() = false(锁在 A 手里)
  [    203 ms] B:try_lock_for(200ms) = false(实际等了 200 ms,超时)
  [    703 ms] A:作用域将尽,unique_file_lock 析构在即
  [   703 ms] A:已放锁(显式 UnlockFile)+ CloseHandle,退场
  [   719 ms] B:try_lock_for(3s) = true(等了 522 ms —— A 一放锁,下一轮询就拿到)
  [   719 ms] B:读到 "ticket=42"(临界区数据完好)

==== move 语义:锁跟着新主人走,moved-from 析构不放锁 ====
  [    734 ms] 本进程:lk1 构造(默认独占 [0,4096)):owns_lock()=1
  [    734 ms] 探针:lk1 持锁中:新开句柄试同区间 = FALSE(锁被占)
  [    734 ms] 本进程:move 后:lk1.alive()=0(moved-from 空壳);lk2.owns_lock()=1
  [    734 ms] 探针:lk1 已析构(moved-from,交出了句柄,析构碰不到锁):仍 = FALSE(锁被占)
  [    734 ms] 探针:lk2 reset 后:= TRUE(拿到)
```

时序与 Linux 侧是同形的:B 立刻问,答了 false。限期 200 ms 的那次如期超时,限期 3 s 的那次,在 A 放锁后的下一轮轮询才拿到,间隔的读数是 16 ms,正是 Sleep(10) 档位的价,还读到了 A 写进临界区的 ticket=42。move 的那半边,moved-from 的 lk1 析构时探针依旧被挡,它已经交出了句柄,析构碰不到新主人的锁,骨架与 `unique_lock` 的心智模型同构,咱们就不多话了。

## 同一台机器,两边各跑一遍

价钱的比较,咱们照 Linux 篇 E6 的剧本复刻:e4 起了 8 个子进程(Windows 这边换成了 CreateProcess),每个子进程都开自己的句柄,每轮都是拿锁、睡 10 ms 模拟临界区、放锁的三步(100 趟一轮),对照组的安排是不拿锁照睡,另外的一组用空临界区打 2000 轮纯交接,咱们各跑三轮取中位:

```text
模式        r1            r2            r3
locked     12765.0 ms   12870.7 ms   12947.3 ms  | 中位  12870.7 ms,16088.3 µs/人次
free        1506.8 ms    1596.0 ms    1601.2 ms  | 中位   1596.0 ms,1995.0 µs/人次
        (free 折算:每轮 Sleep(10) 实际 ≈ 15.96 ms —— 并行睡眠的单份成本)

空临界区(纯锁传递开销):8 进程 × 2000 轮 lock/unlock
micro         87.5 ms     110.0 ms     112.6 ms  | 中位    110.0 ms

单句柄无争抢:1000000 对 LockFileEx/UnlockFile → 1.120 µs/一对
Sleep(10) 真实粒度:100 次 → 15.97 ms/次(计时口径的注脚)
```

咱们把 Linux 侧的数并排放(用的还是同一台机器:flock 跑在 WSL2 的 ext4 里,LockFileEx 跑在宿主的 NTFS 上,同机不同层的口径):

| 指标 | Linux flock | Windows LockFileEx |
| --- | --- | --- |
| locked 中位 | 8090.5 ms | 12870.7 ms |
| free 中位 | 1009.9 ms | 1596.0 ms |
| locked 与 free 之比 | 约 8.0 比 1 | 约 8.1 比 1 |
| 空临界区 16000 次交接 | 255.7 ms,约 16 µs 一次 | 110.0 ms,约 6.9 µs 一次 |
| 单句柄无争抢 100 万对 | 0.675 µs 一对 | 1.120 µs 一对 |

头一回把两列摆在一起的时候,笔者也愣了一下:Windows 的 locked 比 Linux 多出六成,锁就这么贵吗?咱们把算术摊开,差额几乎全落在了睡眠上。Windows 的 `Sleep(10)` 在默认计时粒度下实睡约 15.96 ms,输出末行那 100 次的实测就是注脚,所以理论串行总时长是 800 趟乘 15.96 等于 12768 ms,实测的中位是 12870.7,只多出了 102.7 ms,平摊到 800 次的交接上约 128 微秒,这就是扣掉睡眠本体之后剩下的交接杂费。free 那一行的成绩同样全是睡眠:100 趟乘以 15.96 得到的正是 1596 ms,分毫都不差地对上了。把睡眠的成本从两边各自减掉,结构性的读数就露出来了:互斥把并行的睡眠排成了一条队,locked 与 free 的比值两边都是 8 比 1,这才是机制本身的形状,绝对值反而是计时粒度的形状。这给咱们的教训已经超出了锁这个题目:跨平台对拍计时的时候,头一件事就是把两边平台各自的时间粒度量出来,不然量到的其实是粒度的差,机制的差藏在归一后的比值里。

剩下的两行,咱们看真正的机制价。空临界区的交接,Windows 一次的开销约 6.9 微秒,比 flock 的 16 微秒便宜一半还多,在八进程抢一把空锁的场景里,Windows 的吞吐反倒占优。可单句柄无争抢的 100 万对,Windows 的一对要 1.120 微秒,又比 Linux 的 0.675 微秒贵。两笔读数的方向正好相反,咱们如实各记各的,您可别拿一行的数字给整个机制贴标签。

## 三方对照:flock、fcntl 记录锁、LockFileEx

单点的语义咱们都实测过了,下面咱们把三方放进同一张表里对齐,Windows 列出自 e1 与 e2 的存档(有限等待那一行的证据落在 e2),Linux 的两列出自 Linux 篇的存档。表里的 OFD 锁说的是 Linux 3.15 起的 `F_OFD_SETLK`,它把记录锁从进程挪到了打开文件描述上,表头的 F_SETLK 族与正文用过的 F_SETLKW 这类缩写,您都可以在 Linux 镜像篇里查到全名:

| 维度 | flock(2) | fcntl(2) 记录锁(F_SETLK 族) | LockFileEx |
| --- | --- | --- | --- |
| 作用域 | 整个文件 | 任意字节区间,l_len=0 到 EOF | 任意字节区间,长度 0 实测是空区间,什么都不锁,锁过 EOF 不是错误 |
| 冲突判定单位 | 打开文件描述 | 进程,同进程永不自冲突 | 没有属主豁免,任何现存重叠锁都挡,包括同一句柄自己的上一把 |
| 冲突时非阻塞返回 | -1 加 EWOULDBLOCK(11) | -1 加 EAGAIN(同 11) | FALSE 加 GetLastError()=33(ERROR_LOCK_VIOLATION) |
| 同句柄重复加锁 | 转换,EX 与 SH 互换,非原子 | 替换合并,后锁改写重叠段 | EX 叠 EX 吃 33,唯一例外是同句柄 EX 上叠 SH(文档特例),解锁要两次,放独占在头一回 |
| 同进程第二次 open | 自冲突,阻塞版自锁死 | 并入名下,若无其事 | 自冲突,阻塞版自锁死,E1f 有 1.5 s 取证 |
| 解锁 | LOCK_UN,全放 | F_UNLCK 可任意拆段合并 | 区间精确匹配才放,部分解锁与重复解锁都是 158 |
| 谁能放锁 | 持有该描述的任何人,含 fork 出的子 | 本进程,经任何 fd | 进程加文件对象的组合:DuplicateHandle 复制品放得掉,子进程的继承句柄放不掉,同进程新 open 也放不掉 |
| close 的语义 | 描述最后一个引用关闭才放 | 任意一个 fd close 就全放,OFD 版没有这个陷阱 | 该进程在该文件对象上的句柄清零才放,原主关了、复制品还开着,锁就还在 |
| 进程退出 | 随最后一个引用 | 属主没了,全放 | 系统当场收走,即便别的进程攥着继承句柄,文档提醒网络场景的清理有时延 |
| 子进程与继承 | fork 继承同一描述,子能替父放锁 | 不继承,子挡于父锁,F_GETLK 报父 pid | 继承句柄加不进也放不掉,33 与 158,bInheritHandles=FALSE 时句柄值就是 6 号错误 |
| 对 read 与 write | 咨询锁,不挡 | 咨询锁,不挡 | 半强制,别的句柄读写锁内区间直接 33,持锁者自己不受影响,共享锁读放行写挡,映射视图不受限(文档,未测) |
| 探测对手 | 无,只能试 | F_GETLK 报 pid 与对方锁区间,OFD 报 l_pid=-1 | 无公开 API,只能 FAIL_IMMEDIATELY 试 |
| 有限等待 | 无原生,轮询 | 无原生,轮询 | 轮询,或 FILE_FLAG_OVERLAPPED 加事件加 CancelIoEx,不用轮询 |
| 锁列表观察 | /proc/locks 的 FLOCK 行 | /proc/locks 的 POSIX 与 OFDLCK 行 | 无公开接口 |
| 网络 FS | NFS 客户端模拟成区间锁 | NFS 有丢锁风险,租约 | SMB 3.0 文档标支持,未测 |

表里最有分量的还是归属那一块:三种机制对同一个问题的三种答案,而且互相都换不来。flock 的子进程能替父放锁,而 Windows 做不到。fcntl 那边经任何的 fd 都放得掉,Windows 认的却是文件对象。LockFileEx 挡得住不调锁的读写,这是那两边都做不到的事。您做跨平台的设计,这些差异是折中不了的,只能按各自的地界各写各的。

## 另一侧怎么看

咱们把三个答案摆在一起收场。flock 把锁挂在了打开文件描述上,锁的生死跟着最后一个引用走。fcntl 的记录锁挂在进程身上,换来的是任意 fd 都能放锁、也埋着 close 全释放的陷阱。LockFileEx 的冲突判定不看属主,连自己的锁都挡,解锁的权限认进程与文件对象的组合,进程的退出由系统收尾,外加一层咨询锁没有的挡 I/O 能力。可观测性倒是反着的:Linux 有 /proc/locks 把全系统的锁摊开看,有 F_GETLK 报出对手的 pid 与区间,Windows 这两样都没有公开的对应物,咱们想探测,能用的路子只剩 `FAIL_IMMEDIATELY` 硬试一途。有限等待的处境也反着:flock 和 `F_SETLKW` 的等待只能轮询,而 LockFileEx 配上 `FILE_FLAG_OVERLAPPED` 与事件,倒是有一等一的原生方案。另一侧的完整证据链,请您移步 [文件锁:flock 与 fcntl 记录锁](../../linux/file-io/05-file-lock.md)。

<ReferenceCard title="参考资源">
  <ReferenceItem
    :id="1"
    title="LockFileEx function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-lockfileex"
  />
  <ReferenceItem
    :id="2"
    title="UnlockFile function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-unlockfile"
  />
  <ReferenceItem
    :id="3"
    title="UnlockFileEx function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-unlockfileex"
  />
  <ReferenceItem
    :id="4"
    title="OVERLAPPED structure"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/minwinbase/ns-minwinbase-overlapped"
  />
  <ReferenceItem
    :id="5"
    title="WaitForSingleObject function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-waitforsingleobject"
  />
  <ReferenceItem
    :id="6"
    title="CancelIoEx function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/ioapiset/nf-ioapiset-cancelioex"
  />
  <ReferenceItem
    :id="7"
    title="DuplicateHandle function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/handleapi/nf-handleapi-duplicatehandle"
  />
  <ReferenceItem
    :id="8"
    title="flock(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/flock.2.html"
  />
  <ReferenceItem
    :id="9"
    title="fcntl_locking(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/fcntl_locking.2.html"
  />
</ReferenceCard>
