# 04-contention —— E4 锁争用计时(对照 Linux 侧 E6)

口径与 Linux 侧一致:8 个工作进程(各自独立 `CreateFileA`)、命名事件对齐起跑线、每轮 独占锁→睡 10ms→解锁、100 轮;free 对照不拿锁照睡;micro 是 8×2000 轮空临界区量纯交接;另附单句柄无争抢 100 万对。3 轮取中位。

## 本轮数字(`contention.out`)

| 模式 | r1 | r2 | r3 | 中位 |
|---|---|---|---|---|
| locked | 12765.0 ms | 12870.7 ms | 12947.3 ms | **12870.7 ms** |
| free | 1506.8 ms | 1596.0 ms | 1601.2 ms | **1596.0 ms** |
| micro(16000 次交接) | 87.5 ms | 110.0 ms | 112.6 ms | **110.0 ms** |

- 单句柄无争抢:1000000 对 LockFileEx/UnlockFile → **1.120 µs/一对**。
- `Sleep(10)` 真实粒度:100 次 → **15.97 ms/次**(Win11 默认计时分辨率,没调 timeBeginPeriod——这是本表所有绝对值的口径注脚)。

## 与 Linux 侧同机对读(9700X 台机;那边 ext4 in WSL2,这边宿主 NTFS)

| 口径 | Linux 侧(flock) | Windows 侧(LockFileEx) |
|---|---|---|
| locked 中位 | 8090.5 ms | 12870.7 ms |
| free 中位 | 1009.9 ms | 1596.0 ms |
| 每轮实际睡眠 | ~10.11 ms(usleep 精确) | ~15.96 ms(Sleep(10) 默认粒度) |
| locked − 理论串行 | +90.5 ms | +102.7 ms(12870.7 − 800×15.96) |
| 带睡醒的交接价 | ≈113 µs/次 | ≈128 µs/次 |
| micro 空临界区交接 | 255.7 ms/16000 ≈ **16 µs** | 110.0 ms/16000 ≈ **6.9 µs** |
| 单句柄 1M 对 | 0.675 µs/对 | 1.120 µs/对 |

读法:绝对时长差六成几乎全是 `Sleep(10)` 实睡 15.96ms 撑出来的——把两边都除以「每轮实际睡眠」,locked/free 的 **~8:1 比值一模一样**(800 份临界区被排成一条队);多出的 102.7ms 摊到 800 次交接 ≈128µs/次,与 Linux 侧 113µs 同量级,这就是「把睡满 10ms 的进程唤醒过来的路程」。有意思的反差在 micro:Windows 的空锁交接 6.9µs 反而比 flock 的 16µs 快一倍多,而单句柄无争抢又比 flock 慢(1.120 vs 0.675µs)——传递便宜、单步贵,NTFS 锁路径与 flock 的 VFS 实现各有各的强项。

工程结论与 Linux 侧同条:临界区 10ms 时锁杂费占比不到 1%,怎么写都不亏;临界区趋零时 6.9µs 的交接地板会露出来,别拿文件锁当高频小锁使。

## 复现

```sh
cd /mnt/c/msys64/tmp/l05win
/mnt/c/msys64/ucrt64/bin/g++.exe -std=c++20 -Wall -Wextra contention.cpp -o contention.exe
./contention.exe demo          # 全程约 50 s(3×12.9s locked + 3×1.6s free + micro + 1M 对)
```
