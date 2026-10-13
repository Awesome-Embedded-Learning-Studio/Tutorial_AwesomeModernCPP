# 03-page-cache 配套实验

《页缓存与持久性》(vol8 systems-programming/linux/file-io L03)的实验代码与原始输出存档。核心问题一句话:**write() 返回 ≠ 数据在盘上**。六个实验分别给出脏页滞留的直接观察(E1)、write 后进程死亡的可见性(E2)、四条持久化路径的计时(E3)、fsync/fdatasync 的元数据差(E4)、崩溃演示的能力边界(E5)、dd 工具侧旁证(E6)。

## 环境口径(所有 .out 都来自这一轮)

| 项 | 值 |
|---|---|
| 机器 | 台机,AMD Ryzen 7 9700X 8-Core |
| 系统 | WSL2,内核 6.18.33.2-microsoft-standard-WSL2 |
| 编译器 | g++ (GCC) 16.2.1,`-std=c++20 -Wall -Wextra -Wpedantic -O2` |
| 数据盘 | /dev/sdd ext4(WSL2 的虚拟盘,宿主侧是 NVMe 上的 VHDX) |
| 内存 | MemTotal 55522928 kB |
| vm 口径 | dirty_background_ratio=10 dirty_ratio=20 dirty_expire_centisecs=3000 dirty_writeback_centisecs=500 |
| sudo | `sudo -n true` 不可用(需要密码),drop_caches 类观察全部放弃,见 05 |

**/tmp 是 tmpfs**,写它不产生脏页写回,全部实验的数据文件都在 ext4 上的 `/home/charliechen/l03_scratch/`(沿用 L02 的 `l02_scratch` 先例)。这也是路径烧死的:源码与脚本里的绝对路径和 .out 存档一一对应,复跑前要么 `mkdir -p ~/l03_scratch`,要么改源码开头的常量。

## 目录与结论对照

| 目录 | 实验 | 一句话结论 |
|---|---|---|
| `01-dirty-watch/` | E1 脏页观察 | 256 MiB 只 write 不 fsync:Dirty 立刻涨到 ~262 MB,原样滞留 ~31 s,然后在两帧采样之间(<1 s)被内核写回清零——没人调 sync,是 dirty_expire(30 s)到点后 flusher 线程干的 |
| `02-exit-visibility/` | E2 可见性 | write() 后 _exit(0)、写到一半 kill -9:已 write 的字节全部完好可读,块序号连续无撕裂;丢的是进程,不是页缓存。真正丢数据的场景是内核都没机会写回的掉电/崩溃。对照场景 C:stdio 用户态缓冲不 fflush 就 _exit,2400 字节全丢——那是另一层缓冲 |
| `03-durability-bench/` | E3 计时 | 256 MiB 口径:plain 40.4 ms、fsync 60.6 ms、fdatasync 59.6 ms、O_SYNC 301.6 ms(中位数)。fsync≈fdatasync 成立;"明显慢于 a"在本机只差 1.5 倍(快 NVMe 吞掉了大顺序刷盘),真正的悬崖在 O_SYNC 小块:4 KiB 块直写 64 MiB 要 20.9 s(3 MiB/s) |
| `04-fsync-fdatasync/` | E4 元数据差 | 数据覆盖(fixed)场景测不出差:fsync 250.2 vs fdatasync 246.2 µs/次,噪声内;纯元数据(touch 时间戳)场景稳定 4.7 倍差:fsync 746.2 vs fdatasync 159.6 µs/次 |
| `05-sync-dropcaches/` | E5 崩溃近似 | sudo 不可用,drop_caches 与"真崩溃丢失"都做不了,如实记录。可行的观察链:dd 写 64 MiB 不 sync → Dirty=65644 kB 且文件完全可读 → sync → Dirty=72 kB,文件不变 |
| `06-dd-direct/` | E6 dd 旁证 | 同一份 256 MiB:默认 dd 5.9/6.4 GB/s,跑完留下 262 MB 脏页;`oflag=direct` 5.1-5.9 GB/s,跑完 Dirty≈基线;`conv=fsync` 3.9-4.5 GB/s。本机 direct 吞吐不吃亏(见下面的 WSL2 说明),脏页残留才是铁证 |

## E3 计时表(原始数据,单位 ms)

256 MiB / 1 MiB 块 / 3 轮取中位数,计时含 sync 调用本身;每次测量前把 Dirty 压回低位:

| mode | r1 | r2 | r3 | 中位数 | 中位吞吐 |
|---|---|---|---|---|---|
| plain | 129.9 | 40.4 | 38.8 | 40.4 | 6337 MiB/s |
| fsync | 60.6 | 60.0 | 61.4 | 60.6 | 4224 MiB/s |
| fdatasync | 60.3 | 59.3 | 59.6 | 59.6 | 4295 MiB/s |
| osync | 301.6 | 284.4 | 318.6 | 301.6 | 849 MiB/s |
| osync(4 KiB 块, 64 MiB 口径) | 22387.2 | 15162.0 | 20871.5 | 20871.5 | 3 MiB/s |

plain 第 1 轮 129.9 ms 是冷缓存价(页分配),第 2/3 轮 ~40 ms 才是稳态;它"快"的本质是写盘成本被推迟了——E1 显示这 256 MiB 直到 30 s 后才由内核写回。E4 的 per-op 数据补上另一半:本机每次 fsync 的固定开销 250-1300 µs,持久化的代价模型是"每次 sync 的延迟 × 次数",不是吞吐。

## E4 每次 sync 中位耗时(µs,200 次 × 3 轮取中位数)

| scenario | sync | r1 | r2 | r3 | 中位数 |
|---|---|---|---|---|---|
| fixed(覆盖不扩容) | fsync | 257.0 | 248.5 | 250.2 | 250.2 |
| fixed | fdatasync | 246.2 | 246.0 | 246.2 | 246.2 |
| append(扩容写) | fsync | 1013.8 | 1302.3 | 1391.5 | 1302.3 |
| append | fdatasync | 1356.6 | 863.6 | 1023.9 | 1023.9 |
| touch(只改时间戳) | fsync | 746.2 | 738.4 | 844.0 | 746.2 |
| touch | fdatasync | 156.9 | 159.6 | 161.2 | 159.6 |

append 场景三轮方差极大(fsync 1013-1392,fdatasync 863-1357,区间重叠),两组差异在噪声内,如实记录;能稳定测出差的是 touch:fsync 必须把 inode 时间戳走日志刷下去,fdatasync 可以跳过,4.7 倍。理论差(教科书口径):fsync 连元数据一起刷,fdatasync 只刷数据与"读回数据所必需"的元数据(比如文件大小)。

## 复现

```sh
# 单发编译(在本目录下;脚本会 cd 到自己所在目录找同名二进制)
g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -o 01-dirty-watch/e1_dirty  01-dirty-watch/e1_dirty.cpp
g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -o 02-exit-visibility/e2_exit 02-exit-visibility/e2_exit.cpp
g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -o 03-durability-bench/e3_bench 03-durability-bench/e3_bench.cpp
g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -o 04-fsync-fdatasync/e4_metadata 04-fsync-fdatasync/e4_metadata.cpp

mkdir -p ~/l03_scratch          # 数据文件目录(必须 ext4,不能是 /tmp)
./01-dirty-watch/e1_run.sh      # ~2 分钟(观察窗 80 s)
./02-exit-visibility/e2_exit    # 秒级
./03-durability-bench/e3_run.sh 256 1024 3
./04-fsync-fdatasync/e4_run.sh 200
./05-sync-dropcaches/e5_run.sh
./06-dd-direct/e6_run.sh 256

# 或整套 CMake
cmake -S . -B build && cmake --build build
# (CMake 产物在 build/ 里,脚本按 ./eN_xxx 同目录找二进制,需自行拷到各子目录或用上面的单发编译)
```

计时类脚本(E3/E4/E6)每次测量前都会 `sync` 并等 Dirty 落回低位再开测,避免上一轮残留脏页污染下一轮的 sync 计时;复跑时别同时跑别的写盘任务。

## 复跑注意(踩过的坑,都在数字里)

- **/tmp 是 tmpfs**:写它 Dirty 永远不动,写回永远等不来。数据文件必须放 ext4。本仓 L02 的 07-dirty-msync 也记了同一条。
- **删文件会连带扔掉它的脏页**:writer 写完 256 MiB 不 sync,一条 `rm` 下去 Dirty 瞬间归零——脏页跟着 inode 一起没了,根本不用写盘。所以 E1 的观察窗内不要动那个文件,E3 的 `rm -f` 只放在计时区间外。
- **`_exit()` 不刷 stdio 缓冲**:e1_dirty 第一版用 printf + `_exit(0)`,两行输出无声消失,自己撞上了 E2 场景 C 讲的坑。已在 `_exit` 前 `fflush(stdout)`,源码留了注释。
- **WSL2 的"盘"是宿主 NVMe 上的 VHDX**:单次大 fsync 只要 ~60 ms(4+ GB/s 吸收掉了),所以 E3 里 fsync 只比 plain 慢 1.5 倍——在慢盘或关掉盘上写缓存的机器上,这个比值会大得多,E6 的 direct 与 buffered 吞吐差同理。文章引用这些数字时要带机器口径。
- **写回窗口短到抓不住**:256 MiB 的写回在 <1 s 内完成(1 s 采样密度也没抓到 Writeback>0,见 `e1_rerun_1s.out`),Dirty 在相邻两帧采样之间直接从 263012 掉到 96 kB。慢盘上会看到一段 Writeback>0 的平台期。
- **sudo 不可用**:`drop_caches` 需要 root。别为了演示去借 root,`sync` 前后的 Dirty 对比(E5)已经够说明"谁在什么时候把数据写下去"。
