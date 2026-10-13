---
title: "共享内存:页面文件后备的命名映射对象"
description: "Windows 侧内存章收官篇:两个进程怎么共享一块内存。答案藏在 W02 露过半面的 INVALID_HANDLE_VALUE 里,页面文件后备的命名映射对象在本篇当主角:同名再 Create 拿到 err=183 连请求尺寸都被静默忽略(要 1MiB 到手的还是 64KiB),名字死于最后一个句柄、对象死于最后一个引用的两段死(没有 unlink,跨进程同样成立),Global\ 的特权门槛只拦 section 不拦互斥体,页文件后备让 SEC_RESERVE 的保留语义归了位;跨进程视图实测基址相差 502 GiB、offset 要按 64KB 粒度对齐而长度只按页,父进程的指针值在子进程 VirtualQuery=MEM_FREE、平级的探针进程解引拿 0xC0000005;命名同步三件套:无锁两进程丢 83411/200000、命名互斥体护住恰好二十万、持锁暴毙等待方收 WAIT_ABANDONED 锁还能接着用;招牌 SPSC 环形队列 100 万条零丢失零乱序,spin 2.88 亿条/s 对逐条通知 471 万条/s(慢约 60 倍,事件只配兜底的实证),hybrid 宁可每条 fence 慢三倍的漏唤醒窗口推演;同机 WSL2 对拍(事件对 212ns/条对 eventfd 264ns/条、Sleep(5) 实睡 12.6ms)加 shm_open 八行对照表收束;e6 一句话:安全属性传 NULL 不等于没有 DACL"
chapter: 8
order: 2
platform: host
difficulty: advanced
cpp_standard: [20]
reading_time_minutes: 23
prerequisites:
  - "虚拟内存:VirtualAlloc 与 VirtualProtect"
  - "文件映射:CreateFileMapping 与 MapViewOfFile"
related:
  - "文件映射:CreateFileMapping 与 MapViewOfFile"
  - "共享内存:shm_open 与映射"
  - "SPSC 与 MPMC 队列"
tags:
  - host
  - cpp-modern
  - advanced
  - 系统编程
  - Win32
  - 内存管理
  - 并发
  - 优化
current_status:
  title: 正在打磨
  detail: 本批刚写完,正在通读打磨,表述与实测口径可能随时调整
  variant: polishing
---

# 共享内存:页面文件后备的命名映射对象

上一篇咱们陪着 VirtualAlloc 走完了保留与提交的两段式,管的自始至终是自己进程的地址空间:地址登记了多少、页给没给,别的进程一概无权过问。这一篇问题换了个方向:两个进程想共享同一块内存,Windows 给的路子是什么?

答案其实在 [文件映射](../file-io/02-file-mapping.md)(后文简称 W02)里露过半面。那边讲 `CreateFileMappingW` 的时候提过一句:文件句柄的位置传 `INVALID_HANDLE_VALUE`,映射就不挂任何文件了,文档的原话是 `CreateFileMapping creates a file mapping object of a specified size that is backed by the system paging file instead of by a file in the file system`。后备存储从磁盘上的文件换成了**系统页面文件**(pagefile.sys 是系统在磁盘上划出的换页文件,物理内存吃紧的时候,放不下的页就暂存到它那里)。当时它只在 SEC_RESERVE 一节路过了一下,名字同样只在 W02 的参数走读里顺嘴点过一句,咱们的实验从没真用过它。本篇咱们把它正式展开,也把名字这个参数头一回填上了。

名字才是跨进程的通道。给映射对象起了名,它就成了**命名对象(named object)**:名字登记在内核的对象命名空间里,文件系统里没有它的踪影,别的进程拿同一个名字,打开的就是同一个对象。Linux 那边的对应物是 shm_open 配 /dev/shm,[Linux 侧的共享内存专篇](../../linux/memory/03-shm.md)已经完稿,两边的对照表咱们在收尾处对齐。本篇同时是内存章 Windows 侧的收官篇,实验从对象生命周期一路打到一条跨进程的消息队列。

咱们的实验按 e1 到 e6 编号,与仓库 `code/volumn_codes/vol8/systems-programming/windows/memory/02-shared-mem/` 下面的 01 到 06 六个目录一一对应,代码与原始输出全都入了册。程序输出里的标签打的是 [E1] 到 [E6],正文咱们叫它们 e1 到 e6,您对表认文件名就不会认错人,与前面各篇的 e 系也互不相干。第五场倒是例外:05 目录装的是 Linux 侧的同机对照,两份输出打的标签是 [Linux 侧] 与 [E4-Linux],全程没用 [E5] 的名号,您认目录就好。环境口径:Win11 26200.9457 的本机,机器还是咱们熟悉的 AMD Ryzen 7 9700X(8 核 16 线程),工具链是 MSYS2 UCRT64 的 g++ 16.1.0,编译的命令行统一是 `-std=c++20 -Wall -Wextra`,只有 e4 的吞吐基准加 `-O2`,而 e3 的丢更新演示刻意留在默认的 -O0,免得编译器把读改写的三步并成一步,把咱们要看的竞态优化没了。数字的捕获日期是 2026-10-03,双进程实验的双方各钉一颗核,落在同一个 CCD(CPU 所在的芯粒,9700X 的两颗各包四个核)的 cpu2 与 cpu3 上,跨系统的跑法,还是 [Win32 文件 I/O](../file-io/01-win32-file-io.md)(W01)交代的 WSL interop 那一套,咱们原样沿用。

公共工具还是老阵容:`unique_handle` 与 `last_error_code` 的定义在 [RAII 范式](../../thinking/01-raii-paradigm.md)与[错误处理范式](../../thinking/02-error-paradigm.md)两篇,`check_win32` 的定义留在 W01,咱们只引用不重写。本篇在存档的 `common/shm_util.hpp` 里添了三件新家伙:`unique_view` 管视图基址的 UnmapViewOfFile,骨架与 W02 的 mapped_view 同构。`qpc_ns` 与 `qpc_ms` 是 QueryPerformanceCounter 的计时封装。顶要紧的是 `spawn_self`,父进程拿 CreateProcessW 把自己按新的命令行再拉一份:

```cpp
// shm_util.hpp:把自己再拉一份(本篇双进程实验的统一起跑方式)
inline bool spawn_self(const wchar_t* args, PROCESS_INFORMATION& pi)
{
    wchar_t path[MAX_PATH * 2];
    if (!GetModuleFileNameW(nullptr, path, MAX_PATH * 2)) { return false; }
    std::wstring cmd = std::wstring(L"\"") + path + L"\" " + args;
    STARTUPINFOW si{};
    si.cb = sizeof si;
    BOOL ok = CreateProcessW(path, cmd.data(), nullptr, nullptr, FALSE,
                             CREATE_UNICODE_ENVIRONMENT, nullptr, nullptr, &si, &pi);
    return ok != FALSE;
}
```

bInheritHandles 咱们给的是 FALSE,一个句柄都不传:本篇的双进程协作全靠同名打开,命名对象本身就是咱们要的通道,这正好对上了本篇的主题。还有两条纪律写在 shm_util.hpp 的头注释里:起跑同步一律用命名事件,父子进程的每个 printf 后面都得跟一句 fflush,管道底下的 stdout 是全缓冲,不冲刷的话存档里两边的输出会乱序。命名对象的名字统一带 pid,免得上一轮崩溃残留的半死对象撞名。

## 创建即打开:撞名、被忽略的尺寸,与两段死

e1 起手就把 W02 没接的线头捡了起来:同一个名字,咱们连着 Create 了三次。头一次请求 64KiB 的时候一切正常,err 给的是 0。第二次还是那个同名的对象,句柄倒是照常有效,GetLastError 给的却是 183,也就是常说的 ERROR_ALREADY_EXISTS。第三次的请求更狠,请求改成了 1MiB,回来的还是 183 加一个有效的句柄,咱们拿 VirtualQuery 量实际映射,量出来的 RegionSize 是 0x10000,还是 64KiB 那个对象:请求的尺寸被静默忽略了。

```cpp
// e1_lifecycle.cpp(节选):同名三连 Create,尺寸静默被忽略
SetLastError(0);
HANDLE h1 = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                               0, 64 * 1024, name.c_str());
SetLastError(0);
HANDLE h2big = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                                  0, 1024 * 1024, name.c_str());   // 同名,请求 1MiB
void* vbig = MapViewOfFile(h2big, FILE_MAP_READ, 0, 0, 0);
MEMORY_BASIC_INFORMATION mbi{};
VirtualQuery(vbig, &mbi, sizeof mbi);   // RegionSize 作证:对象到底多大
```

```text
$ ./e1_lifecycle.exe; echo "exit=$?"
[E1] 命名对象生命周期,pid=20084,名字=Local\SysProgShm_E1_20084

[1] CreateFileMappingW 两次同名 + OpenFileMappingW(括号里是进程句柄总数)
  第1次 Create(请求 64KiB)  -> h=00000000000000f0 err=0  (handles 60->61)
  第2次 Create(同名)       -> h=00000000000000f4 err=183  (183=ERROR_ALREADY_EXISTS,句柄照常有效)
  第3次 Create(同名,请求 1MiB) -> h=00000000000000f8 err=183,映射后 RegionSize=0x10000(64KiB) <- 尺寸被忽略,还是 64KiB 那个对象
  OpenFileMappingW(存在)   -> h=00000000000000f8 err=0  (handles 62->63)
  OpenFileMappingW(不存在) -> h=0000000000000000 err=2  (2=ERROR_FILE_NOT_FOUND)
(段间的空行与收尾的 [E1] 完 一行,见下方小观察一段,略)
```

文档把它的行为写得明明白白,咱们把原话摆上来:`If the object exists before the function call, the function returns a handle to the existing object (with its current size, not the specified size), and GetLastError returns ERROR_ALREADY_EXISTS`。括号里那半句请您多看一眼:拿的是现有对象的当前尺寸,不是您指定的尺寸。所以 CreateFileMappingW 的真实语义是**创建或打开**,到手的对象是不是新建的,得看 GetLastError 的脸色。落到协议上的道理也很实在:两个进程约定一个名字,一头 Create 打的头、另一头 OpenFileMappingW 跟上,不只是风格问题——谁要是拿旧名字 Create 了一个更大的尺寸,它一声不吭地给您旧对象,尺寸错了它不告诉您。

OpenFileMappingW 那两行把另一面补齐了:名字在就打开成功,名字不在的时候,回的就是失败值 NULL 配 err=2。它的文档对“存在”的判据也有意思,原话咱们请了出来:`If there is an open handle to a file mapping object by this name ... the open operation succeeds`。您按名去打开,成不成看的就是这个名字下还有没有打开的句柄,判据就藏在它的里面。这句话埋着本批实验里最值钱的发现:名字与对象死的时刻不一样。

e1 的第二段把两者分开示了。咱们手里攥着 h1、h2、h3 一串句柄,再建一个全景的视图,然后逐个关句柄:关掉 h1 的时候 h2 还在,按名打开照样是成功的,别的进程持有的句柄一样吊着名字。等本进程的句柄全关光,咱们按名再开一次,回来的立刻就是 err=2,名字没了。不过视图还活着:咱们往里写 4096 字节再读回,4096 个字节全部都对上了。对象进入了匿名续命,直到 UnmapViewOfFile 放掉了最后一根引用,它才真的销毁。

```text
[2] 生命周期两段论:名字死于最后一个句柄,对象死于最后一个引用
  MapViewOfFile(h2 全景)   -> base=0000021691e30000
  CloseHandle(h1),h2 还拿着句柄 -> 按名打开: h=00000000000000f0 err=0 (有句柄在,名字就在;h4复用了 刚关掉的 h1 那个句柄槽)
  关掉 h2/h3:句柄数回到 60(基线 60),本进程再无该对象的句柄
  句柄全关后写视图 4096 字节再读回:4096/4096 字节相符 —— 视图是另一类引用,对象还活着
  句柄全关后按名再打开      -> h=0000000000000000 err=2 (2=ERROR_FILE_NOT_FOUND!)名字在最后一个句柄关闭那刻就摘了,对象匿名续命
  UnmapViewOfFile:最后一根引用撒手,对象此刻才销毁(没有显式 unlink;名字已亡,销毁无从按名查证)
```

咱们把时序排开,就是**两段死**的格局:名字跟着最后一个句柄走,等句柄计数归了零,名字当场就从命名空间里摘掉了,咱们没有 unlink 可调。对象跟的是最后一个引用,句柄与视图都算它的引用,视图吊着对象的命、句柄吊着名字的命。Linux 那边由 shm_unlink 显式地决定名字的生死,与映射的多少不相干。Windows 这边名字的消失是引用计数的副作用。跨进程同样地成立,e2 里咱们还会再见它一次。

咱们还有个小观察,输出里也打了:h4 复用了刚关掉的 h1 的句柄槽,两个值是一模一样的,连笔者都愣了一下。句柄本质是进程句柄表里的槽号,关掉就回收了,日志里的两处句柄同值,什么也证明不了,您别拿它当同一个对象的证据。整场跑完的收尾行还替咱们验了一件小事:进程句柄数回到了基线 60,什么句柄都没漏。

::: warning 两处静默,一处拒绝
同名再 Create 的时候,尺寸被静默地忽略,文档明说了 `with its current size`。句柄全关的时候,名字就静默消失了:没有 unlink,什么都不提前打招呼。两处偏偏都不报错,协议里防它们的法子只有一个:名字带 pid 防残留,Create 之后咱们拿 VirtualQuery 量一遍实际尺寸。跟它相对的是,MapViewOfFile 的越界请求倒是大方拒绝:512KB 的请求对上 256KB 的对象,回的直接就是 NULL 加 err=5,不给您截断的短视图,证据咱们留在 e2。
:::

## Global\ 的特权门槛,与 SEC_RESERVE 的归位

名字还有个前缀的讲究:打 Local\ 头的,对象登记在当前登录会话的命名空间里,本篇实验用的全是它。打 Global\ 头的,对象进的是跨会话的全局命名空间:服务进程与用户进程的相认,靠的就是它。e1 试了一把 Global\ 的门槛,结果有点出乎咱们的意料:在同一个非特权的进程里,CreateFileMappingW 建 Global\ 的页文件后备映射,收的就是 NULL 配 err=5(ACCESS_DENIED),要的 SeCreateGlobalPrivilege 特权咱们没有。紧接着 CreateMutexW 建同前缀的命名互斥体,一次就成功了。

文档把它的分界写得很清楚,原话说的是 `this privilege check is limited to the creation of file mapping objects and does not apply to opening existing ones`,特权检查只管 section 的创建,连打开现成的都不查,更轮不到咱们刚才的互斥体。还有半句咱们也得带上:`from a session other than session zero`,会话 0 里的服务进程建全局对象,本来就不在受检的行列。您要是把 Global\ 要特权记成一刀切,互斥体这一关就过不去了,实测与文档站的是同一边。

```text
[3] 名字空间:Local\ 按会话隔离,Global\ 要特权(section 才要)
  CreateFileMappingW(Global\...section) -> h=0000000000000000 err=5 (5=ERROR_ACCESS_DENIED,SeCreateGlobalPrivilege 未持有)
  CreateMutexW     (Global\...mutex)    -> h=00000000000000f8 err=0 (成功!特权门槛只拦 section,不拦互斥体/事件/信号量)

[4] SEC_RESERVE|PAGE_READWRITE(页文件后备,保留语义的正主)
  4MiB SEC_RESERVE 映射 -> base=0000021692140000 State=MEM_RESERVE Protect=0x0 RegionSize=0x400000
  VirtualAlloc(首页 MEM_COMMIT) -> 0000021692140000,State=MEM_COMMIT Protect=0x4
  提交后写读:*(uint64_t*)base=0x5A5A5A5A5A5A5A5A
```

SEC_RESERVE 在这里也归了位。W02 那边咱们拿真文件配 SEC_RESERVE,文档说的是 no effect,实测的时候它静默退化成了普通映射,连什么错误码都不给,当时留下的判断就是它得配页文件后备用。e1 的 [4] 段兑现了这句:4MiB 的 SEC_RESERVE 页文件后备映射,VirtualQuery 量出来的 State 是 MEM_RESERVE、Protect 是 0,地址登记了、页没给。VirtualAlloc 提交了首页之后,State 就翻成了 MEM_COMMIT,咱们写入 0x5A5A 开头的魔数、回读一字不差。文档的条件句原话是 `If the file mapping object is backed by the operating system paging file`,W02 的意外兑现与这次的保留兑现,分岔点落的正是这个条件。保留与提交的两段式,上一篇刚陪咱们走过一遍,共享内存这边给出的正是同一个两段式。

## 两个进程,一块内存:能传的只有偏移

咱们在 e2 里把第二个进程真正地放进场:父进程 Create 命名对象、spawn_self 拉起子进程、子进程 OpenFileMappingW 按名打开,各自 MapViewOfFile 贴各自的视图。父在偏移 0x1000 的地方写下 0xC0FFEE,子在自己的基址上读回同一个值。子再往偏移 0x20000 回写校验的值,父也读到了。两个进程共享的是对象的内容,不是什么地址。

地址这东西谁也不保证是一样的。咱们故意让子进程映了一块无关的 16MiB 匿名区、再映目标对象,两边的基址当场分了家:父的 0x1835F6F0000、子的 0x200F2E60000,相差的是 539344961536 字节,折出来的有 502 GiB 上下,半个 TB 的量级。两份视图挂的是同一个对象,落地址的时候各看各的分配历史,历史不一样的两边,基址自然也就不一样了。

那把指针写进共享内存行不行?e2 也替咱们试过这一手了。父把自己槽位的指针值 0x1835F6F1000 当数字写进去,子读出来跟自己的同槽地址并排一摆,是对不上的。子拿 VirtualQuery 问了这个值,State 报的是 MEM_FREE,这地址在子的空间里压根就没映射过。咱们光问还不过瘾,父又另拉起了一个与子进程平级的探针进程,真的去解引用它,退出码给的是 3221225477,正是 0xC0000005 的十进制。文档在这件事上的告诫一句顶一句:`Do not store pointers in the memory mapped file; store offsets from the base of the file mapping so that the mapping can be used at any address`,翻译过来就是别存什么指针,要存的是偏移。咱们的协议从头到尾只有偏移量,连队列头里的槽位数组位置,写的也是偏移。全码咱们靠隔进程的探针拿:GCC 是不认 `__try/__except` 的,进程内接异常的路子 [SEH 与 VEH](../file-io/03-seh-veh.md)(W03)讲过,咱们本篇要撞的地方不止一处,隔进程的方案最省事。

视图怎么切,要求还是 W02 讲过的同一套:MapViewOfFile 的 offset 必须按分配粒度对齐,本机给的是 64KiB。咱们让 e2 在两个进程里各吃了一回:offset=4096 的时候,页是对齐了但粒度不够,回的直接就是 NULL 配 err=1132(ERROR_MAPPED_ALIGNMENT),父子走的是同一套规则:offset=0x10000 配 len=4096,就成功了,VirtualQuery 量出的 RegionSize=0x1000,长度是只按页取整的,基址还是落在了 64KB 边界上。粒度管的是 offset 与基址,可不管什么长度。超长的请求也一样没商量的余地,输出里的两处 err=5 都是它。

```text
$ ./e2_cross_view.exe; echo "exit=$?"
[E2] 跨进程视图,父 pid=13288,对象=Local\SysProgShm_E2_13288(256KB)
[父] 全景视图 base=000001835f6f0000  dwAllocationGranularity=65536(0x10000)
[子] pid=26560,先映一块无关 16MiB 区 base=00000200f2fd0000,再映目标对象 base=00000200f2e60000
[父] 子进程基址 = 0x200F2E60000,我的基址 = 0x1835F6F0000,两边不同——这就是只能传偏移的原因
[父] 偏移 0x1000 写 0xC0FFEE;偏移 0x1008 写父侧指针值 0x1835F6F1000
[父] MapViewOfFile(offset=4096, len=8192) -> 0000000000000000 err=1132(offset 不按 64KB 对齐的下场)
[父] MapViewOfFile(offset=0x10000, len=4096) -> 000001835f430000 err=0(64KB 对齐;长度不必对齐)
[父] 4KB 切片视图 VirtualQuery:State=MEM_COMMIT Protect=0x4 RegionSize=0x1000(长度按页取整就是 4KB;基址 000001835f430000 仍落在 64KB 边界——粒度管 offset 与基址,不管长度)
[父] MapViewOfFile(offset=0, len=512KB 超对象) -> 0000000000000000 err=5(不给截断的短视图,整个请求直接拒绝)
[探针-ptr] 读到父指针值 0x1835F6F1000,现在真的去解引用它...
[父] 探针(ptr)退出码=3221225477(0xC0000005) —— STATUS_ACCESS_VIOLATION,和预告的一样
[探针-pastend] 超长视图 base=0000000000000000,摸偏移 0x40000(对象只有 0x40000)...
[探针-pastend] 视图都没给 err=5
[子] 读偏移 0x1000 的值 = 0xC0FFEE(A 写 B 读成立)
[子] 父指针值=0x1835F6F1000  我的同槽地址=0x200F2E61000  相差 +539344961536
[子] VirtualQuery(父指针值) 在我的空间里:State=MEM_FREE —— 这地址在我这儿根本没映射,解引用必 0xC0000005(父进程另派了探针进程实撞)
[子] MapViewOfFile(offset=4096) -> 0000000000000000 err=1132(对齐规则对谁都是同一套)
[子] 经全景视图读偏移 0x10000:0xFEEDFACE
[父] 子回写校验 echo=0xFE2DAEED(期望 0xFE2DAEED) —— 偏移协议全程自洽
(中段探针 pastend 的退出码一行、尾段的跨进程复盘与 exit 行在存档,下一段复述,略)
```

输出的尾段咱们单独拎出来,e1 的两段死被它推到了跨进程:父进程关光了自己的映射句柄,子进程的手里还攥着一个,按名打开照样是成功的。通知子进程也把句柄关了,名字立刻就消失了,而子的视图往偏移 0x20000 写 0x1234ABCD 依旧成功,父从自己的视图把这个值读了出来。名字亡了,对象倒是还活着,咱们把两段死放到两个进程之间,同样地成立。

## 命名同步三件套:互斥体、事件、信号量

内存共享了,麻烦也共享了。咱们让两个进程对同一块内存做读改写,竞态跟线程间的一模一样,还更凶:连同一个进程里的天然时序都没有了。e3 的头一场就是无锁对照:共享内存里放一个 uint64 计数器、父子各钉一颗核,各做十万次的自增,发令的事件一响,同时就起跑了。三步的读改写撞在一起,终值掉到了 116589,期望的可是 200000,丢了 83411 次更新,丢的比留下的一半还多。

```cpp
for (int i = 0; i < 100000; ++i) {
    if (lock) { WaitForSingleObject(mtx, INFINITE); }
    *counter = *counter + 1;   // 故意非原子:读-改-写三步,没锁就互相踩
    if (lock) { ReleaseMutex(mtx); }
}
```

护住它的正是咱们请来的命名互斥体。CreateMutexW 配上了名字,它就是内核里的一个命名对象,父子两边按名各拿了一个句柄,等的是同一把锁。第二轮加了锁再跑,拿到的终值恰好是 200000,丢的次数是 0。CreateMutexW 的撞名语义与映射对象同一套:同名再 Create 的时候,句柄照样是有效的,err 给的还是 183。这里有个与 Linux 结构性的分岔:pthread 的互斥体默认只管进程内,想跨进程的时候,得带上 PTHREAD_PROCESS_SHARED 的属性,把锁的本体放进共享内存。Windows 的锁不住在共享内存里,它住的是内核,名字就是它的通道。

```text
$ ./e3_named_sync.exe mutex; echo "exit=$?"
[E3-mutex-无锁] 两进程各 10 万次共享计数自增
[父] 终值=116589 / 期望 200000 -> 丢更新(差 83411 次)
[worker pid=2248] 完成(互斥体护驾),此刻计数=200000

[E3-mutex-锁] 两进程各 10 万次共享计数自增
[父] 终值=200000 / 期望 200000 -> 一个不丢(差 0 次)
exit=0

$ ./e3_named_sync.exe mutex abandoned; echo "exit=$?"
[abandoner pid=26028] 拿到互斥体(等待结果 0),啥也不干直接 ExitProcess
[E3-abandoned] 持锁暴毙:子进程抱着命名互斥体 ExitProcess
[父] 子进程退出码=1,现在去等那把没人放的互斥体...
[父] WaitForSingleObject 返回 128(WAIT_ABANDONED=0x80) —— 互斥体没坏,拿到手还能继续用
exit=0
(无锁一轮 worker 的完成提示与首次 CreateMutexW 的 err 记录在存档,略)
```

abandoned 一场是 Windows 给的独门戏,子进程拿到了互斥体,啥也不干就 ExitProcess 了,抱着锁暴毙了。父进程去等这把没人会放的锁,WaitForSingleObject 返回的是 128,代号 **WAIT_ABANDONED** 的数值是 0x80,abandon 是弃置的意思,持有者没放锁就死了。文档的原话交代了后续:`Ownership of the mutex object is granted to the calling thread and the mutex state is set to nonsignaled`,所有权直接判给了等待方,锁的状态也复位了,接着用是没有问题的。它还跟了一句 `If the mutex was protecting persistent state information, you should check it for consistency`,锁倒是没坏,可它保护的共享数据可能改到一半,您拿到锁得检查现场。咱们对照 Linux:pthread 互斥体遇上持有者死亡,默认的反应是死等——想要 EOWNERDEAD,得另配 robust 的属性。而命名互斥体的孤儿接管,内核是白送的。

三件套的第二件是事件 CreateEventW。一个 bManualReset 的参数,定的是它当广播用还是当单醒用。手动复位(TRUE)设了信号就一直有信号,咱们只 SetEvent 一次,所有等待者全都被放了行,直到有人调了 ResetEvent。自动复位(FALSE)的有信号状态恰好够放行一个,放完就自动复位了,第二个等待者就得等下一发了。e3 各请了两个等待进程,父进程只 SetEvent 了一次:手动复位的那一轮,两个 waiter 的等待时长都是 59 毫秒上下,是同时放行的。自动复位的那轮,第一发放行的只有一个,父再补了一发 SetEvent,另一个这才出了场。

三件套的第三件是信号量,管的是名额。CreateSemaphoreW 开的初值是 2、上限也是 2,咱们放三个进程来排队:头两个的排队时长是 1 微秒,瞬时就过了闸,第三个干等了 304982 微秒,直到父进程在 300 毫秒后补了 ReleaseSemaphore(1),它才拿到了名额。释放前的 prev 计数是 0,名额确实被前两个占光了,这个出参就是最好的证人。

这场实验还有个初版的假阴性,值得咱们记一笔。头一版里早到的 waiter 出门就把名额 ReleaseSemaphore 还回去,而 W01 交代的 \wsl.localhost 链路(正式的叫法是 UNC 路径,也就是 Windows 的 \\服务器\共享 式网络路径)拉进程慢了半拍,迟到的第三个到场时名额已经被还回来了,它一微秒就过了闸,全程没体验过什么叫堵——实验看起来全都通过了,想示出来的状态根本没示出来。改成的版本是攥着名额等父进程的收工令,堵的状态才稳定出现。多进程实验的时序设计,光想同步原语是不够的,还得想进程启动的延迟,笔者在这上面栽过一回。

```text
$ ./e3_named_sync.exe event; echo "exit=$?"
[手动复位事件] 两个等待进程就位,父进程只 SetEvent 一次
[父] SetEvent 一次
[父] waiter0 等待时长 59424 微秒(结果 0)
[父] waiter1 等待时长 59418 微秒(结果 0)
[父] 手动复位:一次 SetEvent,两个 waiter 都醒——广播

[自动复位事件] 两个等待进程就位,父进程只 SetEvent 一次
[父] SetEvent 一次
[父] 第一发只放行了 waiter 1;再 SetEvent 一发才轮到另一个
[父] 自动复位:一次 SetEvent 只放行一个;第二个得等下一次
exit=0

$ ./e3_named_sync.exe semaphore; echo "exit=$?"
[sem-waiter 0 pid=12892] 过闸:等待结果=0,排队 1 微秒(名额到手,攥着不放直到父进程放行)
[sem-waiter 1 pid=23288] 过闸:等待结果=0,排队 1 微秒(名额到手,攥着不放直到父进程放行)
[sem-waiter 2 pid=10844] 过闸:等待结果=0,排队 304982 微秒(名额到手,攥着不放直到父进程放行)
[E3-semaphore] 初值 2 的命名信号量,3 个进程排队
[父] 300ms 后 ReleaseSemaphore(1) -> ret=1 prev=0 err=0(0=释放前名额已被两个 waiter 占光)
[父] 前两个瞬时过闸、第三个干等 300ms+——计数信号量跨进程管并发名额
exit=0
```

## 招牌实验:SPSC 环形队列住进共享内存

同步讲完了,咱们把这一章攒下的工具全部用上,做一件真东西:一条跨进程的消息队列。队列的本体是 SPSC 环形缓冲(SPSC 是 Single Producer Single Consumer 的缩写,单生产者对单消费者的结构),无锁版本的理论与内存序,vol5 的 [SPSC 与 MPMC 队列](../../../../vol5-concurrency/ch04-concurrent-data-structures/04-lock-free-queues.md)已经完整讲过,咱们这里只做工程落地:把环搬进页文件后备的对象,生产者与消费者是两个分开的进程,两边各钉了一颗核。

环的布局咱们全部定在共享内存里,开头的 4KB 是队列头:

```cpp
struct alignas(64) RingHead {                 // 共享内存里的队列头(首 4KB)
    uint64_t magic;
    uint64_t nmsgs;
    uint64_t capacity;
    uint64_t mask;
    std::atomic<uint64_t> tail;               // 生产者写(消费者读)
    char pad0[64 - sizeof(std::atomic<uint64_t>)];
    std::atomic<uint64_t> head;               // 消费者写(生产者读)
    char pad1[64 - sizeof(std::atomic<uint64_t>)];
    std::atomic<uint64_t> sleeping;           // hybrid:消费者要睡了
    // 统计字段(消费者写,生产者收尾读)从略
};
struct Slot { uint64_t value; uint64_t tag; };   // 16B 消息,4096 槽
```

tail 与 head 各占一整行的缓存行,中间的填充字节不是摆设:两个进程各写各的索引,要是挤在了同一行里,这一行就得在两颗核之间来回地失效,咱们的队列还没跑起来,这一行就已经自己跟自己打起来了,vol5 给它起的名字就是伪共享。咱们把消息定成 16 字节,value 放的是序号,tag 放序号搅出来的校验值,消费侧逐条地核对,丢了一条或者乱了一条,当场就藏不住了。槽位的数组从偏移 0x1000 开始,还是咱们那句老话,协议里放着的只有偏移。

队列头不需要咱们专门写初始化协议。文档对页文件后备的对象给过一句承诺:`The initial contents of the pages in a file mapping object backed by the operating system paging file are 0`,初始的内容全是零。lock-free 的原子量零值本来就合法,生产者进程用 placement new 在全零页上把 RingHead 重建了一遍,再填上容量与魔数的初值,等消费者打开对象的时候,看到的头一眼就是完整合法的队列。

数据可见性全靠 tail 与 head 的 acquire/release,咱们连 CAS(compare-and-swap、比较并交换)都不用,是 vol5 讲过的单写单读、双索引环形队列的原样。真正要设计的只剩一件事:消费者发现队列为空的时候怎么办。e4 给咱们准备了四种答案,各是一套跑法的对照:

- spin:空了就原地自旋
- notify:每条消息 SetEvent 一次
- hybrid:自旋为主,真睡得着才睡
- burst:生产者每万条歇 5 毫秒,逼着消费者真走一遍睡眠与唤醒

咱们再来看 hybrid:消费者怎么睡得着,又怎么睡不丢,协议是这么写的(节选,生产者的 if/else 两枝并成了一枝):

```cpp
// 消费者:空转 2 万次还空,才立牌位睡觉
for (int i = 0; i < 20000 && h == t; ++i) {
    _mm_pause();
    t = rh->tail.load(std::memory_order_acquire);
}
if (h == t) {
    rh->sleeping.store(1, std::memory_order_seq_cst);   // 牌位:我要睡了
    t = rh->tail.load(std::memory_order_acquire);        // 立完再查一遍 tail
    if (h == t) { WaitForSingleObject(data, INFINITE); ++sleeps; }
    rh->sleeping.store(0, std::memory_order_seq_cst);
}
// 生产者:发布 tail 之后,fence 再查牌位
rh->tail.store(tail, std::memory_order_release);
if (!m_spin && !m_notify) {                              // hybrid 路线
    std::atomic_thread_fence(std::memory_order_seq_cst);
    if (rh->sleeping.load(std::memory_order_relaxed)) { SetEvent(data); }
}
```

咱们把两边的 seq_cst 配成一对,缺了任何一边就有一个窗口:消费者在消费掉最后一条与立牌位之间,生产者恰好发布完、查了一眼牌位,看到的是 0,事件就漏发了。消费者立完牌位去复查 tail 的时候,又恰好读到的是旧值,睡下去就再没人叫它了。消费者的复查与生产者的 fence 合在一起,保证两边至少有一侧看得到对方的动作,这套配对就是 hybrid 睡眠不丢唤醒的全部依靠。

一百万条消息、4096 个槽的规模,咱们让 spin、hybrid、notify 在 Windows 侧各跑三轮取中位,burst 单独跑了一轮,每一轮消费侧的核对错误都是 0 条,值与 tag 逐条全都是对的,交出零丢失零乱序的成绩。计时表咱们连同 WSL2 侧的同构版本一起摆:

| 模式 | Windows 原生 | WSL2 同机 | 说明 |
| --- | --- | --- | --- |
| spin(双方纯自旋) | 3.47 ms / 2.88 亿条/s | 5.04 ms / 1.98 亿条/s | 内存速度的上界,两侧编译器不同,差距含代码生成因素 |
| hybrid(自旋为主,空了才睡) | 10.77 ms / 0.93 亿条/s | 30.38 ms / 0.33 亿条/s | 每条发布后的 seq_cst fence 是主要开销,本轮 0 次睡眠 |
| notify(每条 SetEvent) | 212.37 ms / 471 万条/s | 264.47 ms / 378 万条/s | 每条一次通知加一次等待,比 spin 慢约 60 倍 |
| burst(每万条歇 5ms) | 1259.80 ms,睡眠 197 次 | 534.00 ms,睡眠 197 次 | 兜底通道真被走过的证明,零丢失 |

表里的四行数字,差出来的是四种开销的来源。咱们看 spin 的成绩是 3.47 纳秒一条,这是内存速度的上界。notify 的开销在每条两次内核过渡上:SetEvent 一次、WaitForSingleObject 一次,每条摊到的就是 212 纳秒,比 spin 慢了约 60 倍——事件的通知能力拿来逐条使唤,开销就全落在了这两次过渡上,所以说事件只配兜底,是不配逐条的,表里的数字就是实证。hybrid 平时全靠的是原子索引,一条的成本是 10.77 纳秒,比 spin 慢了三倍,burst 付的则是定时器的粒度,细节咱们往下再说。

慢出来的三倍贵在哪?贵在每条发布后的那道 seq_cst fence,而本轮的消费者全程跟得上,压根没睡过什么觉,兜底的通道根本没启用,等于咱们每条都付了 fence 的开销,而当轮的唤醒一次都没用上它。笔者动过把 fence 省掉的念头:环非空的时候干脆跳过,消费者明显是醒着的,何必每条都立什么牌位。咱们推演下去,它是不成立的:消费者从消费掉最后一条到复查完 tail 之间仍有间隙,生产者的 relaxed 读在 fence 缺席时可能飘到牌位写入之前,拿到旧的 0,SetEvent 也就不发了。偏偏复查又看到旧的 tail,睡死就成立了。漏唤醒的窗口没有消除,只是变窄了,窄到平时压不出什么事,真按 burst 的节奏跑起来才会现形。优化是不成立的,咱们如实记档,教科书协议的这道 fence,是省不得的。

burst 这一轮正是咱们给兜底通道做的验证。生产者的节奏是每写一万条歇一个 5 毫秒的 Sleep,消费者 197 次真的睡着了,又被事件叫醒了 197 次,一百万条零丢失的战绩,SetEvent 总共发生了 15552 次。兜底的唤醒协议从头到尾真走了一遍。

```text
$ ./e4_spsc_ring.exe notify; echo "exit=$?"
[E4] 模式=notify 消息数=1000000 槽数=4096(16B/槽) 对象=Local\SysProgShm_E4_21560
[生产者 pid=21560 cpu2] 1000000 条,212.37 ms,吞吐 471 万条/s(212.37 ns/条),遇满自旋 244 次
[消费者 退出码=0] 1000000 条,核对错误 0 条(值+tag 逐条验),212.37 ms,吞吐 471 万条/s,等待 923162 次
[E4] notify:SetEvent 共 1000000 次;100 万条零丢失零乱序
exit=0

$ ./e4_spsc_ring.exe burst; echo "exit=$?"
[E4] 模式=burst 消息数=1000000 槽数=4096(16B/槽) 对象=Local\SysProgShm_E4_22096
[生产者 pid=22096 cpu2] 1000000 条,1259.80 ms,吞吐 79 万条/s(1259.80 ns/条),遇满自旋 246 次
[消费者 退出码=0] 1000000 条,核对错误 0 条(值+tag 逐条验),1259.80 ms,吞吐 79 万条/s,睡眠 197 次
[E4] burst:SetEvent 共 15552 次;100 万条零丢失零乱序
exit=0
(spin 与 hybrid 各三轮的完整输出在存档,中位数字见表)
```

咱们还碰到一个 Windows 特有的发现:Sleep(5),它实际睡了约 12.6 毫秒。默认的系统定时器分辨率是 15.6 毫秒一档,5 毫秒的请求向上取整到下一档,实验里咱们故意没调 timeBeginPeriod,如实地入了档。它就是 burst 一轮 Windows 1259 毫秒、WSL2 同款实验 534 毫秒的主因:一百次的歇脚,每次都多睡了 7 毫秒上下,差值正好是对得上的。至于队列的本体,WSL2 侧咱们用 eventfd(Linux 把一个事件计数化成的 fd)配同一套环写了同构版本,形状与 Windows 侧的是一致的:spin 的成绩是 1.98 亿条/s,逐条通知的那一档是 378 万条/s。咱们单看逐条通知的一档,Windows 的事件对 212 纳秒一条,比 WSL2 的 eventfd 对 264 纳秒一条略快,两边打的是同一场内核过渡的仗,差的是进内核的成本。

## 同一台机器的两套答案:与 shm_open 对拍

Linux 侧的共享内存专篇就在隔壁,咱们提前把对照要的证据在同一台机器上跑齐了,这就是第五场 e5:家当在存档的 05-linux-compare 里,linux_probe 干的就是这个活。几行输出把 shm_open 一侧的判据全摆了出来:名字是 /dev/shm 下一个真文件,stat 查得到它的存在。撞名时 O_CREAT|O_EXCL 给的是 EEXIST。尺寸靠的是 ftruncate 显式给。mmap 的 offset 按页对齐、页加 8 就 EINVAL。shm_unlink 显式地摘名字,与映射的多少无关,摘完之后旧映射照常地读写。

```text
$ /tmp/linux_probe; echo "exit=$?"
[Linux 侧] pid=77291 页大小=4096 名字=/sysprog_e5_77291(POSIX shm 名字空间)
  shm_open(O_CREAT|O_RDWR|O_EXCL) -> fd=3 errno=0(Success)
  stat(/dev/shm/sysprog_e5_77291) -> 0 size=0 —— 名字就是 /dev/shm 里一个真文件
  shm_open(第二次,带 O_EXCL)  -> fd=-1 errno=17(File exists)
  shm_open(第二次,不带 O_EXCL)-> fd=4 errno=0 —— 同一对象另一个 fd,没有"已存在"信号
  ftruncate(256KiB) 后 stat size=262144 —— 大小是 ftruncate 显式给的(Windows 在 CreateFileMapping 参数里给)
  mmap(MAP_SHARED) -> 0x7293cc4f2000
  mmap(offset=页+8) -> 0xffffffffffffffff errno=22(Invalid argument) —— offset 必须按页对齐(Windows 要 64KB)
  shm_unlink(/sysprog_e5_77291) -> 0 名字显式摘除,与还有多少映射/多少 fd 无关
  unlink 后再 shm_open -> fd=-1 errno=2(No such file or directory) —— 名字没了
  unlink 后旧映射读写:回读 4096/4096 相符,再写 4096 字节也成功 —— 对象活到 munmap
[Linux 侧] 完
exit=0
```

两套答案都凑齐了,咱们把它们排成八行对照,每一行的证据都能对回本批存档的输出:

| 对照维度 | Linux:shm_open + mmap | Windows:CreateFileMappingW 页文件后备 | 证据 |
| --- | --- | --- | --- |
| 名字住在哪 | /dev/shm 下的真文件,stat 看得见 | 内核对象命名空间,文件系统里没有 | linux_probe 对 e1 全程 |
| 创建还是打开 | 两个调用分立,O_CREAT\|O_EXCL 撞名 EEXIST | 合一,Create 也能打开,新没新建看 err=183 | probe 第二次 shm_open 对 e1 第 2 次 Create |
| 尺寸谁定 | 创建后 ftruncate 显式定 | Create 的参数里定,同名再 Create 被忽略 | probe 的 ftruncate 对 e1 的 RegionSize |
| 名字何时消失 | 显式 shm_unlink,与映射数无关 | 隐式,最后一个句柄关闭即摘,没有 unlink API | probe 的 unlink 对 e1 的 err=2 |
| 对象何时消失 | 最后一份引用撒手,fd 与映射都算 | 最后一个引用撒手,句柄与视图都算 | probe unlink 后旧映射对 e1 的视图续命 |
| 映射偏移对齐 | 页(4KB),页加 8 给 EINVAL | 分配粒度(64KB),4096 给 err=1132,长度不必对齐 | probe 的 offset 对 e2 的 offset |
| 跨进程同步 | pthread 锁默认进程内,PROCESS_SHARED 放进共享内存 | 互斥体/事件/信号量是内核命名对象,天然跨进程 | Linux 侧专篇对 e3 |
| 名字空间隔离 | 单一 /dev/shm(挂载命名空间除外) | Local\ 按会话,Global\ 跨会话且 section 要特权 | probe 对 e1 的 [3] 段 |

八行里咱们挑两处多说两句。名字的生死,是两边差得最远的一处:shm_unlink 把决定权交给您,unlink 之后旧映射接着用、同名的新实体可以重建,名字与实体是解耦的。而 CreateFileMappingW 这边,名字的消失是引用计数的副作用,您手里不攥着句柄,名字就攥不住了。咱们在同步那一行再多说两句:Linux 把锁做成了普通内存。想跨进程的时候,咱们就得请它住进共享内存,持有者死了,锁的状态也死在那块内存里,得 robust 的属性才换回 EOWNERDEAD。Windows 把锁做成了内核命名对象:锁的住处不在共享内存,通道走的是名字,持有者暴毙的时候,内核替它把锁判给了等待方。本篇的 WAIT_ABANDONED 与那恰好二十万,都是同一条路线带来的。

## NULL 的安全属性,不等于没有 DACL

还有一个参数咱们没交代:lpSecurityAttributes。咱们全程传 NULL,图的是省事,不过 NULL 并不等于没有访问控制。e6 拿 GetSecurityInfo 查了 NULL 创建出来的对象的 DACL(自主访问控制列表的英文是 Discretionary Access Control List,记录的是谁有权打开这个对象),查出来的结果是 present、三条 ACE(访问控制条目、DACL 名册里的每一行),NT AUTHORITY\SYSTEM、当前用户与登录会话各占了一条,mask 给的都是 0xF001F,合起来给的正是 SECTION_ALL_ACCESS。显式给 SDDL(安全描述符定义语言)字符串创建的另一个对象,它的 DACL 同样是查得出、对得上的。默认的门是关着的,只是默认放行的名单,就是 SYSTEM、当前用户与登录会话的三个身份。跨用户的拒绝路径,单机单用户的咱们测不了,ACL 的展开咱们记在候补清单里,咱们在这里把口径立住:NULL 拿到的是默认 DACL,想要一个没有 DACL 的对象,得显式地另给。

```text
$ ./e6_acl.exe; echo "exit=$?"
[E6] lpSecurityAttributes:NULL 的默认门禁 vs 显式 SDDL
  NULL 安全属性创建 -> h=00000000000000f8 err=0,查它的 DACL:
  [NULL] DACL:present,AceCount=3(+NT AUTHORITY\SYSTEM(mask=0xF001F),+DESKTOP-65DBAA7\CharlieChen114514(mask=0xF001F),+NT AUTHORITY\LogonSessionId_0_1154262(mask=0xF001F))
  ...(账户名与 SID 每机不同;显式 SDDL 一路的 DACL 同样查得出、对得上,三行 ACE 略)
  理论口径:DACL 没授权的用户 OpenFileMappingW → err=5(单机单用户,实测留给专篇)
exit=0
```

内存章 Windows 侧的两篇,到这里就走完了。咱们回头看,手里已经有一套能用的双进程实验台:spawn_self 拉进程、命名事件发令、共享内存传数、命名互斥体管秩序,单机内存的故事,到这一篇就收住了。

咱们把进程这个词用了两篇,却始终没正经讲过它自己的事。spawn_self 里那行 CreateProcessW 已经陪咱们跑熟了,可进程创建之后内核里发生了什么、句柄怎么继承、作业对象怎么把一群进程圈在一起管起来、控制台事件怎么当 Windows 侧的信号使唤,这些都排在下一章的日程上,轮到进程自己登场了。

<ReferenceCard title="参考资源">
  <ReferenceItem
    :id="1"
    title="CreateFileMappingW function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-createfilemappingw"
  />
  <ReferenceItem
    :id="2"
    title="OpenFileMappingW function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-openfilemappingw"
  />
  <ReferenceItem
    :id="3"
    title="MapViewOfFile function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-mapviewoffile"
  />
  <ReferenceItem
    :id="4"
    title="CreateMutexW function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-createmutexw"
  />
  <ReferenceItem
    :id="5"
    title="WaitForSingleObject function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-waitforsingleobject"
  />
  <ReferenceItem
    :id="6"
    title="CreateEventW function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-createeventw"
  />
  <ReferenceItem
    :id="7"
    title="CreateSemaphoreW function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-createsemaphorew"
  />
  <ReferenceItem
    :id="8"
    title="Kernel Object Namespaces"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/TermServ/kernel-object-namespaces"
  />
  <ReferenceItem
    :id="9"
    title="shm_open(3)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man3/shm_open.3.html"
  />
</ReferenceCard>
