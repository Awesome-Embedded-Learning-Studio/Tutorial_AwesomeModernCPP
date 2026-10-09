# 02-shared-mem:《共享内存:页面文件后备的命名映射对象》Windows 侧配套实验

《共享内存》篇(W02 文件映射的续篇:那边讲文件后备映射与 `INVALID_HANDLE_VALUE` 的页文件后备事实,这边成体系讲**页文件后备命名对象的跨进程协作**)的真机实验与原始输出存档。`.out` 全部是当轮机器的原始捕获(`$` 开头的行是当时敲的命令),结论行都能对回 .out 的行号。六个子目录各管一块:命名对象生命周期、跨进程视图与偏移、跨进程同步三件套、SPSC 环形队列招牌实验、Linux 同机对照、ACL 一句话。

## 环境

- Windows 11 26200.9457(26H2 线),物理机 AMD Ryzen 7 9700X(8C/16T),双进程实验双方各钉一颗核(cpu2/cpu3,同 CCD)
- MSYS2 UCRT64 g++(Rev 5)16.1.0,`-std=c++20 -Wall -Wextra`;仅 E4 基准加 `-O2`(吞吐数字要 -O2 才作数),E3 的丢更新演示刻意用 -O0 默认档
- Linux 对照侧:同一台物理机上的 WSL2(内核 6.18.33.2-microsoft-standard-WSL2),g++ 16.2.1,`-std=c++20 -O2 -Wall -Wextra`
- 编译运行(WSL interop,cwd 必须在 WSL 文件系统上;E6 要挂 advapi32):

  ```text
  /mnt/c/msys64/ucrt64/bin/g++.exe -std=c++20 -Wall -Wextra xxx.cpp -o xxx.exe   (E4 加 -O2;E6 加 -ladvapi32)
  chmod +x xxx.exe && ./xxx.exe
  ```

- 双进程实验的统一起跑方式:父进程 `CreateProcessW` 把自己再拉一份(公共工具 `common/shm_util.hpp` 的 `spawn_self`),跨进程全靠同名打开,不传句柄不继承;stdout 管道下全缓冲,每个 printf 后必须 fflush,否则 .out 里父子输出乱序

## 子目录与结论速览

| 目录 | 主题 | 一句话结论 |
|---|---|---|
| [01-lifecycle/](01-lifecycle/) | 命名对象生命周期 | 两次 Create 同名:第二次句柄有效+err=183(创建即打开),连**请求尺寸都被静默忽略**(要 1MiB 拿到的还是 64KiB);生命周期两段论:**名字死于最后一个句柄**(全关后再 OpenFileMappingW 立刻 err=2),**对象死于最后一个引用**(句柄全关后视图照常读写)——没有 unlink,这是与 shm_open 最大的结构差异 |
| [02-cross-view/](02-cross-view/) | 跨进程视图与偏移 | 同名对象父 Create 子 Open,A 写 B 读实时可见;两进程基址实测不同(0x1835F6F0000 vs 0x200F2E60000),父指针值在子进程 VirtualQuery=MEM_FREE、真解引用的探针进程拿 0xC0000005 退场;offset 不按 64KB 对齐 → err=1132;超长请求不给截断直接 err=5 |
| [03-named-sync/](03-named-sync/) | 跨进程同步三件套 | 无锁两进程各 10 万次自增丢 83411 次更新,命名互斥体护驾后恰好 200000;持锁进程暴毙 → 等待方 WAIT_ABANDONED(0x80)且锁还能接着用;手动复位事件一次 SetEvent 双醒(广播),自动复位只放一个(单播);初值 2 的命名信号量管 3 进程,前两个瞬时过闸第三个干等 300ms |
| [04-spsc-ring/](04-spsc-ring/) | 招牌:跨进程 SPSC 环形队列 | 共享内存环形缓冲(tail/head 原子量各占一行缓存行)+ 命名事件兜底:100 万条**零丢失零乱序**(值+tag 逐条验,10 轮全 0 错:spin/hybrid/notify 各 3 轮+burst 单轮);spin 2.88 亿条/s,hybrid 0.93 亿,逐条 SetEvent 只剩 471 万——通知只配兜底,不配逐条 |
| [05-linux-compare/](05-linux-compare/) | Linux 同机对照 | shm_open 的名字/生命周期/对齐/撞名证据 + eventfd 版同构环形队列:同机同构直接可比,spin 1.98 亿条/s、逐条通知 378 万——形状与 Windows 侧一致,notify 模式 Windows 的事件对(212ns/条)比 WSL2 的 eventfd 对(264ns/条)略快 |
| [06-acl/](06-acl/) | ACL 一句话 | lpSecurityAttributes 传 NULL 不等于"没有 DACL":默认 DACL 照样在(SYSTEM+当前用户+登录会话 SID,mask 0xF001F);显式 SDDL 的 DACL 查得出对得上;跨用户拒绝实测留给 ACL 专篇(单机单用户) |

## E5 对照表:shm_open vs CreateFileMappingW(INVALID_HANDLE_VALUE)

每行的证据都能对回本批 .out 的行号:

| 对照维度 | Linux:shm_open + mmap | Windows:CreateFileMappingW 页文件后备 | 证据 |
|---|---|---|---|
| 名字住在哪 | `/dev/shm` 下的真文件,stat/ls 看得见 | 内核对象命名空间(`\BaseNamedObjects\`),文件系统里没有 | linux_probe.out:4(stat 到文件) vs e1 全程(只有名字没有文件) |
| 创建还是打开 | Create/Open 分立:`O_CREAT\|O_EXCL` 撞名 → -1 EEXIST;不带 O_EXCL 再开无任何"已存在"信号 | 合一:CreateFileMappingW 也能打开,新没新建看 GetLastError(183) | linux_probe.out:8-9 vs e1_lifecycle.out:6 |
| 尺寸谁定 | 创建后 ftruncate 显式定 | CreateFileMappingW 参数里定;同名再 Create 的尺寸**被忽略** | linux_probe.out:10 vs e1_lifecycle.out:7 |
| 名字何时消失 | **显式** shm_unlink,与还有多少映射无关 | **隐式**:最后一个句柄关闭那刻自动摘名,没有 unlink API | linux_probe.out:13-14 vs e1_lifecycle.out:16 |
| 对象何时消失 | 最后一个 munmap(映射是引用;fd 关了映射还在) | 最后一个引用(句柄**或视图**)撒手;视图吊着对象、句柄吊着名字 | linux_probe.out:15-16 vs e1_lifecycle.out:15-17,e2_cross_view.out:26-29 |
| 映射偏移对齐 | 页(4KB),offset=页+8 → EINVAL | allocation granularity(64KB,粗 16 倍),offset=4096 → err=1132;长度不必对齐 | linux_probe.out:12 vs e2_cross_view.out:7、9 |
| 跨进程同步原语 | pthread 锁默认进程内,要 PTHREAD_PROCESS_SHARED 放进共享内存;robust 属性才换回 EOWNERDEAD | 互斥体/事件/信号量是内核命名对象,天然跨进程、不用放进共享内存;持有者暴毙等待方自动收 WAIT_ABANDONED | e3_named_sync.out:17(无需任何属性) |
| 名字空间隔离 | 单一 /dev/shm(挂载命名空间除外) | `Local\` 按登录会话 / `Global\` 跨会话(section 要 SeCreateGlobalPrivilege——实测非特权进程 err=5,**同前缀的互斥体却创建成功**) | e1_lifecycle.out:20-21 |

## E4 计时表(100 万条 × 16B 消息,4096 槽,生产/消费各钉一颗核,3 轮中位)

| 模式 | Windows 原生 | WSL2 同机 | 说明 |
|---|---|---|---|
| spin(双方纯自旋) | 3.47 ms / 2.88 亿条/s | 5.04 ms / 1.98 亿条/s | 内存速度上界;两侧编译器不同(16.1 MinGW vs 16.2 Linux),spin 差距含代码生成因素,别过度解读 |
| hybrid(自旋为主,空了才睡) | 10.77 ms / 0.93 亿条/s | 30.38 ms / 0.33 亿条/s | 每次发布后的 seq_cst fence(防错过唤醒的牌位协议)是主要开销;本轮消费者全程跟上,0 次睡眠 |
| notify(每条 SetEvent/eventfd) | 212.37 ms / 471 万条/s | 264.47 ms / 378 万条/s | 每条一次通知调用+一次等待,比 spin 慢 ~60 倍——"事件只配兜底"的实证 |
| burst(每万条歇 5ms 逼睡) | 1259.80 ms,睡眠 197 次 | 534.00 ms,睡眠 197 次 | 兜底通道真被走过的证明:零丢失;Windows 慢出一倍见"意外发现"第 6 条(Sleep 精度) |

全部 10 轮消费侧"核对错误 0 条"(spin/hybrid/notify 各 3 轮,burst 单轮)(值+tag 逐条验,丢一条/乱一条都会暴露)。

## 意外发现 / 坑

1. **名字与对象分两段死**(本批最有价值的实测):句柄全关后按名打开立刻 err=2,但视图照常读写——名字跟着最后一个**句柄**走,对象跟着最后一个**引用**(句柄或视图)走。E2 把它推到跨进程:子进程还拿着句柄时父进程关光自己的也能再打开;子进程一关,名字没了、子的视图还能写。对照 shm_unlink:那边名字生死是**显式决定**,这边是**引用计数副作用**。
2. **同名再 Create 连尺寸都忽略**:第三次 Create 传 1MiB,返回 err=183 + 有效句柄,VirtualQuery 量出来还是 64KiB——"拿现有对象,参数白给"。协议里"先 Create 后 Open"不只是风格,尺寸错了它不告诉你。
3. **超长视图请求直接拒绝**:MapViewOfFile 请求 512KB 而对象 256KB → NULL + err=5(ACCESS_DENIED),**不会**给你截断的短视图(两个进程独立复现)。
4. **Global\ 的特权门槛只拦 section**:非特权进程建 `Global\` 页文件后备映射 err=5(SeCreateGlobalPrivilege),同一进程建 `Global\` 命名互斥体一次成功——别把"Global 要特权"记成一刀切。
5. **hybrid 的 fence 有真实价签**:per-publish 的 seq_cst fence 让 hybrid(10.77ms)比 spin(3.47ms)慢 3 倍,而本轮它一次都没派上用场(0 次睡眠)。曾试过"环非空就跳过 fence"的优化,推演发现漏唤醒窗口(消费者消费最后一条与立牌位之间),不成立——教科书协议的这笔开销省不得。
6. **Sleep(5) 实际睡了 ~12.6ms**:burst 模式 100 个批次把 Windows 侧拉到 1259ms(Linux usleep 同款 534ms)——默认系统定时器分辨率 15.6ms 在起作用,实验故意不调 timeBeginPeriod,如实入档。
7. **UNC 路径上 CreateProcess 能用**:WSL interop 下 exe 挂在 `\\wsl.localhost\...`,GetModuleFileNameW+CreateProcessW 自我复制没问题(先冒烟验证过才敢让全部实验依赖它)。
8. **GCC 没有 __try/__except**:撞地址的"死给我们看"改用隔进程方案——探针进程(与 child 平级)专门解引用父指针,父进程拿 GetExitCodeProcess 读全码 3221225477(0xC0000005);直接在进程里 __try 的路,W03 用 mingw `<excpt.h>` 的 `__try1` 宏,但一个翻译单元只能一处,本篇三处要撞,隔进程更省事。
9. **e3 信号量初版的假阴性**:先到的 waiter 出门就 ReleaseSemaphore 还名额,UNC 拉进程的迟到的第三个 waiter 到场时名额已被还回,根本没体验过"堵"——改成"攥着名额等收工令"才把堵的状态稳定示出来。多进程实验的时序设计不能只想同步原语,还得想进程启动延迟。
10. **句柄槽复用**:h1 关掉后紧接着 OpenFileMappingW 拿到的 h4 与 h1 同值(0xF8)——句柄值是表槽号,会回收;日志里对比"同一个句柄"不能说明"同一个对象"。

## 复跑注意

- 每个 `.cpp` 头部有编译/运行命令与观察点清单;E3 四个子命令、E4 四种模式各是独立一轮,`.out` 里的顺序就是当时敲的顺序。
- E4 复跑:同模式连跑 3 轮取中位;绝对毫秒每轮必浮动(±5%),数量级与相对关系稳定。
- 命名对象名都带 pid,上一轮崩溃残留不会撞名;所有程序退场前自清(Local\ 对象随句柄归零消亡,无需手工删)。
- 05-linux-compare 两个程序在 WSL2 侧编译运行(命令在各自 .out 首行),产物放 /tmp,别把 Linux 二进制落进仓。
- E6 复跑要 `-ladvapi32`;输出里的账户名/SID 每机不同,规律可对,绝对值别对。

## 相关目录

- 契约工具(unique_handle/unique_view/spawn_self/qpc)定义:[common/shm_util.hpp](common/shm_util.hpp)(形态沿用 [thinking/](../../thinking/) 两篇的 win_util 族)
- W02 文件映射(文件后备映射、SEC_RESERVE 的文件后备意外兑现):[../../file-io/02-file-mapping/](../../file-io/02-file-mapping/)
- SEH/VEH(`__try1` 宏与异常码路径):[../../file-io/03-seh-veh/](../../file-io/03-seh-veh/)
- Linux 侧 mmap 全景(页对齐/匿名 overcommit 的另一批证据):[../../../linux/file-io/02-mmap-memory-mapping/](../../../linux/file-io/02-mmap-memory-mapping/);Linux 共享内存专篇(memory/03)的实验入册时,05-linux-compare 的证据可以平移引用
