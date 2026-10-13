# 05-file-lock 配套实验

《文件锁:flock 与 fcntl 记录锁》(vol8 systems-programming/linux/file-io L05)的实验代码与原始输出存档。核心问题一句话:**两族锁的「锁属于谁」不一样——flock 的锁属于打开文件描述,fcntl 记录锁属于进程**;close/fork/exec/dup 的全部行为差异、以及 E2d 那个「close 任意一个 fd 就全放」的 POSIX 陷阱,都是这一句话的推论。

## 环境口径(所有 .out 都来自这一轮)

| 项 | 值 |
|---|---|
| 机器 | 台机,AMD Ryzen 7 9700X 8-Core(16 线程) |
| 系统 | WSL2,内核 6.18.33.2-microsoft-standard-WSL2 |
| 编译器 | g++ (GCC) 16.2.1,`-std=c++20 -Wall -Wextra -Wpedantic -O2`(零警告) |
| 数据盘 | /dev/sdd ext4(设备号 8:48,`/proc/locks` 里写作十六进制 `08:30`) |
| 内存 | 52 GiB |

**路径是烧死的**:源码把数据文件写在 `/home/charliechen/l05_scratch/eN/`(沿用 L02/L03 的 scratch 先例;`common/` 里是 `errno_code`/`sys_call`/`unique_fd` 三件公共工具)。复跑前要么 `mkdir -p ~/l05_scratch/e1 ../e2 …`,要么改各 `.cpp` 开头的 `path` 常量。07 号实验另需 `/tmp` 是 tmpfs(本机如此)。

## 目录与结论对照

| 目录 | 实验 | 一句话结论 |
|---|---|---|
| `01-flock-matrix/` | E1 flock 语义矩阵 | LOCK_EX 互斥 + 解锁瞬间接棒(同毫秒);同 fd 重复加锁 = 转换;dup 同描述不冲突,**重新 open 自冲突、阻塞版自锁死**(2 s alarm 取证);LOCK_NB 冲突 = -1/EWOULDBLOCK;close 最后一个引用即放锁;fork 继承的是同一描述——子进程能替父放锁,子 close 继承 fd 不放锁 |
| `02-fcntl-matrix/` | E2 fcntl 记录锁矩阵 | 字节区间生效([0,100) 与 [50,150) 冲突、[100,200) 成功);F_GETLK 报出对方 pid 与对方锁**自己的区间**(不是交集);R+R 共存、带 W 即冲突;**E2d 陷阱:close 该文件的任意一个 fd → 本进程全部记录锁释放**(锁前/锁后 open 的 fd 都一样);F_OFD_SETLK 没有此陷阱且 F_GETLK 报 l_pid=-1;后锁覆盖重叠段(内核把 [0,100)W + [50,150)R 切成两段);进程退出即释放 |
| `03-proc-locks/` | E3 /proc/locks | 真实行:`19: POSIX ADVISORY WRITE 167263 08:30:1690193 0 99` + `20: FLOCK ADVISORY WRITE … 0 EOF`;被挡的等待请求以 `->` 缩进挂在挡路者序号下;POSIX 区间是**闭区间端点**(l_len=100 → `0 99`);设备号十六进制、**inode 十进制**(man proc_locks(5) 未写明进制,内核源码是 `%02x:%02x:%llu`) |
| `04-flock-vs-fcntl/` | E4 继承对照 | fork:flock 子进程与父共享同一把锁(还能替父 UN),fcntl 子进程撞父锁、F_GETLK 报父 pid;dup:flock 跟引用计数走;exec:flock 锁随 fd 活过 exec(O_CLOEXEC 则当场释放),fcntl 锁随进程活过 exec。**NFS 未测**(WSL2 无 NFS 挂载),理论口径引 man,见该目录 README 的对照表 |
| `05-raii-file-lock/` | E5 RAII 封装 | `file_lock`(构造加锁/析构放锁/defer_lock/try_lock_for 1ms 轮询/move-only);双进程时序:A 持锁 700ms,B `try_lock()` 立刻 false → `try_lock_for(200ms)` 超时 → `try_lock_for(3s)` 在 A 放锁瞬间拿到并读到 `ticket=42`;moved-from 析构不放锁 |
| `06-contention-bench/` | E6 竞争计时 | 8 进程×100 轮×10ms 临界区:locked 中位 **8090.5 ms**(理论串行 8000 ms),free 对照 1009.9 ms——锁把并行睡眠串行化;空临界区 micro 8×2000 轮 lock/unlock 中位 255.7 ms ≈ **16 µs/次交接** |
| `07-fs-compare/` | E7 tmpfs 对照 | 同一套探针跑 ext4 与 tmpfs:P1/P2/P3 结果逐行一致——锁语义在 VFS 层,与文件系统无关;唯一可见差异是 `/proc/locks` 设备号字段(`08:30` ext4 vs `00:4d` tmpfs) |

## 复现

```sh
# 单发(在 05-file-lock 目录下)
g++ -std=c++20 -Wall -Wextra -Wpedantic -O2 -I common 01-flock-matrix/flock_matrix.cpp -o /tmp/e1

# 或整套 CMake
cmake -S . -B build && cmake --build build
```

05-raii-file-lock 多一层头文件目录,单发编译要 `-I common -I 05-raii-file-lock`;07 号要传数据文件路径参数(见其 README)。

## 输出档案口径

全部 `.out` 都是同一轮(台机 9700X,2026-10-02)的捕获,文章引用的数字与它们逐个对得上。输出里的 pid、毫秒时间戳(相对各程序启动)每次复跑都会变,规律不在具体数值。E4 的 exec 场景里,exec 之后的子进程时间戳从 0 重新计——`t0` 是进程映像里的静态变量,exec 换了映像,这是预期行为不是错拍。

## NFS:如实记录「未测」

本机(WSL2)没有 NFS 挂载,E4 对照表里 NFS 一列全部是**理论口径**,出处:man 7 flock(2)(≤2.6.11 不锁;≥2.6.12 客户端把 flock 模拟成 fcntl 区间锁,两族锁因此在 NFS 上会互相作用;2.6.37 起有 local_lock 选项)、man 2 fcntl_locking(2)(NFS 丢锁与 NFSv4 租约 90 s、nfs.recover_lost_locks 默认 0)。文章引用时请标注「未实测」。
