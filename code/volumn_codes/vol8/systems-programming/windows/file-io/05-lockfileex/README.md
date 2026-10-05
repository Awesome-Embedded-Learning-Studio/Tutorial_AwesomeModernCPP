# 05-lockfileex 配套实验

《LockFileEx》(vol8 systems-programming/windows/file-io L05,镜像对照 Linux 侧 L05《文件锁:flock 与 fcntl 记录锁》)的实验代码与原始输出存档。核心问题一句话:**问 LockFileEx「锁属于谁」,它给出的是 flock(描述)与 fcntl(进程)之外的第三种答案——冲突判定没有属主豁免,连同一句柄对自己的第二次加锁都吃 33;而解锁与释放的权限认「进程 × 文件对象」**。另一条 Linux 侧没有的性质:区间锁会真挡**别的句柄**的 ReadFile/WriteFile(半强制,POSIX 两族都是咨询锁)。

## 环境口径(所有 .out 都来自这一轮)

| 项 | 值 |
|---|---|
| 机器 | 与 Linux 侧 05 存档同一台台机(AMD Ryzen 7 9700X) |
| 系统 | 宿主 Windows 11 26200(26H2 线),数据文件在 NTFS 系统盘 C: |
| 编译器 | MSYS2 UCRT64 g++(Rev 5)16.1.0,`-std=c++20 -Wall -Wextra`(零警告) |
| 跑法 | WSL 侧 `cd /mnt/c/msys64/tmp/l05win && /mnt/c/msys64/ucrt64/bin/g++.exe … && ./xxx.exe demo` |
| 数据 | `C:/msys64/tmp/l05win/`(烧死在各源码常量里,复跑先建目录) |

计时对照的注意:Linux 侧数字出自 WSL2 内 ext4,Windows 侧出自宿主 NTFS,同机不同层,量级可比、绝对值不可比(尤其 `Sleep(10)` 在本机默认计时粒度下实睡约 16 ms,见 04)。

## 目录与结论对照

| 目录 | 实验 | 一句话结论 |
|---|---|---|
| `01-matrix/` | E1 语义矩阵(a-w + I1..I5) | 独占互斥、放锁同毫秒接棒;共享两边同持、其上独占进不来(33);FAIL_IMMEDIATELY=FALSE+33;字节区间按交叠判冲突([99,101) 单字节也挡),**长度 0 实测是空区间、不锁到 EOF**(fcntl 的 l_len=0 相反),锁过 EOF 不报错;同句柄 EX 重复/重叠加锁=33(无属主豁免),唯一例外是文档写明的「同句柄 EX 上叠 SH」,此时解锁要两次;UnlockFile 必须精确匹配区间(部分解锁/重复解锁=158);**同进程两个句柄互相挡,阻塞版自锁死 1.5 s 取证**;CloseHandle 放掉该句柄名下全部区间;别的句柄 read/write 锁内区间直接失败 33(持锁者自己不受影响,共享锁读放行写挡);继承句柄(bInheritHandles=TRUE)加锁撞父(33)、放不掉父锁(158),bInheritHandles=FALSE 时句柄值=6 号错误;**持锁进程退出锁当场被 OS 清算**(即便别的进程攥着同一文件对象的继承句柄);DuplicateHandle 复制品(同进程同对象)加不进但放得掉,全新 open(同进程新对象)放不掉——解锁认「进程×文件对象」 |
| `02-try-lock/` | E2 有限等待 | LockFileEx 没有超时参数(与 flock/F_SETLKW 同病):a) `FAIL_IMMEDIATELY + Sleep(10ms)` 轮询模拟 try_lock_for,场景 1 第 32 次尝试在 500 ms 处拿到、场景 2 如期超时;b) **加餐:FILE_FLAG_OVERLAPPED 句柄上冲突返回 FALSE+997(ERROR_IO_PENDING) 挂账,`WaitForSingleObject(hEvent, 超时)` 就是原生 try_lock_for,超时侧 `CancelIoEx` 撤单收 995(ERROR_OPERATION_ABORTED),不留幽灵锁** |
| `03-raii/` | E3 RAII 封装 | `unique_file_lock`(构造加锁/析构「显式 UnlockFile+CloseHandle」双保险/defer_lock/try_lock_for 10ms 轮询/move-only):双进程 ticket=42 时序与 Linux 侧同形(B 限期 200ms 超时、限期 3s 在 A 放锁后 16ms 接棒并读到 `ticket=42`);moved-from 是空壳,析构碰不到新主人的锁 |
| `04-contention/` | E4 争用计时 | 8 进程×100 轮:locked 中位 **12870.7 ms**(理论串行 800×15.96=12768 ms,只多 102.7 ms)、free 1596.0 ms——互斥把并行睡眠排成一条队,与 Linux 侧 8090.5/1009.9 同构;**Windows 侧 Sleep(10) 实睡 ≈15.96 ms**(默认计时粒度),所以绝对值比 Linux 大六成,比值仍是 ~8:1;micro 空临界区 16000 次交接 110.0 ms ≈ **6.9 µs/次**(Linux flock 是 16 µs);单句柄无争抢 1M 对 = 1.120 µs/一对(Linux 0.675 µs) |

## 三方对照矩阵(E5,文章主表素材)

Linux 侧两列的依据是 `linux/file-io/05-file-lock/` 的存档(flock=BSD 族挂打开文件描述、fcntl=POSIX 记录锁挂进程、F_OFD_SETLK 挂描述),Windows 列全部来自本目录 `01-matrix/matrix.out`:

| 维度 | flock(2) | fcntl(2) 记录锁(F_SETLK 族) | LockFileEx |
|---|---|---|---|
| 作用域 | 整个文件 | 任意字节区间,l_len=0 到 EOF | 任意字节区间;**长度 0 实测=空区间,什么都不锁**;锁过 EOF 不是错误 |
| 冲突判定单位 | 打开文件描述 | 进程(同进程永不自冲突) | **没有属主豁免**:任何现存重叠锁都挡,包括同一句柄自己的上一把(E1e) |
| 冲突时非阻塞返回 | -1 + EWOULDBLOCK(11) | -1 + EAGAIN(=EWOULDBLOCK) | FALSE + GetLastError()=33(ERROR_LOCK_VIOLATION) |
| 同句柄/同描述重复加锁 | 转换(EX↔SH,非原子) | 替换/合并(后锁改写重叠段) | EX+EX 吃 33;唯一例外:同句柄 EX 上叠 SH 可以(文档特例),解锁要两次,先放独占 |
| 同进程第二次 open | 自冲突,阻塞版自锁死 | 并入名下,若无其事 | 自冲突,阻塞版自锁死(E1f 线程 1.5 s 取证) |
| 解锁 | LOCK_UN,全放 | F_UNLCK 可任意拆段合并 | UnlockFile 区间**精确匹配**才放:部分解锁、重复解锁都是 158(ERROR_NOT_LOCKED) |
| 谁能放锁 | 持有该描述的任何人(含 fork 出的子) | 本进程(经任何 fd) | **进程×文件对象**:同进程 DuplicateHandle 复制品放得掉(I5),子进程继承句柄放不掉(E1h③),同进程新 open 的句柄也放不掉(I5 h3) |
| close 的语义 | 描述最后一个引用关闭才放 | 任意一个 fd close → 全放(E2d 陷阱);OFD 版无此陷阱 | 该进程在该文件对象上的句柄清零才放(I5:原主关了、复制品还开着 → 锁还在);一次 UnlockFile 都不调也行(E1g) |
| 进程退出 | 随最后一个引用 | 属主没了,全放 | OS 清算,**立刻**(I4:proxy 退场当毫秒探针就拿到了,尽管 sleeper 攥着继承句柄);文档提醒网络场景清算有时延 |
| 子进程/继承 | fork 继承同一描述,子能替父放锁 | 不继承:子撞父锁,F_GETLK 报父 pid | CreateProcess+bInheritHandles=TRUE:继承句柄**加不进也放不掉**(33/158),文档原话“the child process is not granted access”;bInheritHandles=FALSE:句柄值就是 6 号错误,没有跨进程意义 |
| 对 read/write | 咨询锁:不挡 | 咨询锁:不挡 | **半强制**:别的句柄 read/write 锁内区间直接失败 33(E1w);持锁者自己不受影响;共享锁读放行、写挡;映射视图不受限(文档,未测) |
| 探测对手 | 无,只能试 | F_GETLK 报 pid 与对方锁区间;OFD 报 l_pid=-1 | 无公开 API(没有 F_GETLK 对应物),只能 FAIL_IMMEDIATELY 试 |
| 有限等待 | 无原生,轮询 | 无原生,轮询 | 无原生,轮询;或 OVERLAPPED+事件+CancelIoEx(E2b 加餐,不轮询) |
| 锁列表观察 | /proc/locks FLOCK 行 | /proc/locks POSIX/OFDLCK 行 | 无公开接口 |
| 网络 FS | NFS 客户端模拟成区间锁 | NFS 有丢锁风险(租约) | SMB 3.0 等文档标支持(未测) |

## 复现

```sh
# 在 WSL 侧(源码先放到 C:/msys64/tmp/l05win/ 下,或改各 .cpp 烧死的路径)
cd /mnt/c/msys64/tmp/l05win
mkdir -p l05win                     # 其实就是本目录;程序只建文件不建目录
/mnt/c/msys64/ucrt64/bin/g++.exe -std=c++20 -Wall -Wextra matrix.cpp -o matrix.exe
./matrix.exe demo

# 03-raii 带同目录头文件
/mnt/c/msys64/ucrt64/bin/g++.exe -std=c++20 -Wall -Wextra raii_demo.cpp -o raii_demo.exe
```

## 复跑注意

- **路径烧死**:数据文件与子进程日志都在 `C:/msys64/tmp/l05win/`(`kDir` 常量),复跑前确保目录存在。
- 输出里的 pid、毫秒时间戳每次都变,规律不在具体数值;多进程时间线走「各写日志文件+主进程按时间戳归并」,`.out` 里各节内部顺序是确定的。
- E1f 的取证线程在自锁死取证后故意不回收(进程退出兜底),1.5 s 的等待是实验的一部分。
- E1w 的 write 测试往数据文件 [50,60) 写了 10 个零字节,复跑多次无损(文件本就是零填充)。
- Windows 计时粒度:`Sleep(10)` 实睡 ≈16 ms(未调 timeBeginPeriod 的默认状态),E2 轮询与 E4 计时都带着这个口径,别拿 Linux 的 10.1 ms 直接比绝对值。
