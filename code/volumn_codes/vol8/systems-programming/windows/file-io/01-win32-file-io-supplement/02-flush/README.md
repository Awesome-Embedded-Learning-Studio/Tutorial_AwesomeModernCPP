# 02-flush:FlushFileBuffers 配套实验(含 FILE_FLAG_WRITE_THROUGH 对比)

《Win32 文件 I/O》补课段(FlushFileBuffers)的真机实验与原始输出存档。方法论对齐 Linux 侧 [03-page-cache/03-durability-bench](../../../../linux/file-io/03-page-cache/03-durability-bench/) 的 E3:同一份数据走多条持久化路径,写与刷分开计时,多轮取中位数。

## 文件与实验对照

| 文件 | 内容 |
|---|---|
| `e2_flush_bench.cpp` | 单模式计时程序:`<plain\|flush\|wt\|wt_flush> [totalMiB] [chunkKiB]`,QueryPerformanceCounter,write_ms 与 flush_ms 分列 |
| `e2_run.sh` | 驱动脚本:32MiB x 1MiB 块 x 四路径 x 5 轮 + 中位数汇总;附加 wt + 4KiB 小块口径 |
| `e2_flush_bench.out` | `e2_run.sh` 的完整输出(环境、口径、原始轮次、汇总) |
| `e2b_fflush_layers.cpp/.out` | 与 C 运行时的分层:CRT 层持留(小写不 fflush 旁路看不见)、32MiB 三层计时(fwrite/fflush/FlushFileBuffers)、每步的尺寸可见性 |

环境:Win11 26200 / MSYS2 UCRT64 g++ 16.1.0 / 目标卷 = %TEMP% 所在 NTFS 系统盘(NVMe,WD_BLACK SN7100)。四条路径同 Linux E3 对应:plain=只写不管,flush=结尾一次 FlushFileBuffers,wt=FILE_FLAG_WRITE_THROUGH(对应 O_SYNC),wt_flush=wt 结尾再补一次刷。

## 关键结论(数字 = 5 轮中位数,原始轮次在 .out)

| mode | write_ms | flush_ms | total_ms | write MiB/s | total MiB/s |
|---|---|---|---|---|---|
| plain | 6.772 | — | 6.772 | 4725 | 4725 |
| flush | 6.831 | 8.012 | 14.774 | 4685 | 2166 |
| wt | 14.975 | — | 14.975 | 2137 | 2137 |
| wt_flush | 15.882 | 0.205 | 16.086 | 2015 | 1989 |
| wt_4k(4KiB 块) | 389.295 | — | 389.295 | — | 82 |

**1. plain 与 flush 的差,就是"进了缓存"和"落到盘上"的差:**写循环本身 6.8ms(4725 MiB/s,缓存速度),补一次 FlushFileBuffers 8.0ms,总吞吐掉到 2166 MiB/s——这与 Linux 侧 E3 的 plain(6337)vs fsync(4224)是同一个故事。

**2. FILE_FLAG_WRITE_THROUGH 与"写完一次 FlushFileBuffers"殊途同归:**wt 全程 2137 MiB/s ≈ flush 的总吞吐 2166 MiB/s——都受制于盘的可持续写速度。差别在付款方式:wt 每笔都同步(逐次等盘),flush 是攒一笔月底结。所以 wt + 4KiB 小块直接塌方:82 MiB/s,大块的 1/26(与 Linux 侧 O_SYNC + 4KiB 塌到 3-4 MiB/s 同款形态)。

**3. wt 之后补 FlushFileBuffers 几乎白送(0.205ms):**写穿路径没什么脏页可刷。反过来说,只写不管(plain)的 6.8ms 里没有一分钱花在持久性上。

**4. 分层(e2b):`fflush` 只到 OS,`FlushFileBuffers` 才到盘。**
- CRT 层持留:FILE* 逐字节写 100B,旁路 Win32 句柄看到尺寸 0;fflush 之后才 100——数据这会儿还在 CRT 用户态缓冲里,WriteFile 都没发生。
- 大块 fwrite(1MiB 块)直通 WriteFile:写完 32MiB 旁路立刻看到 33554432——"别的句柄能不能读到"取决于块有没有进 OS,不是取决于 fflush。
- 三层计时:fwrite 32MiB 6.9ms(进缓存)→ fflush 第一次 5.0ms / 第二次(空)0.000ms → FlushFileBuffers 9.2ms(真落盘)。fclose 只顺手做 fflush,**谁也不会替你做 FlushFileBuffers**。

**5. 文件句柄 vs 卷句柄一句话:**`FlushFileBuffers(文件句柄)` 刷这一个文件的脏页;传卷句柄(管理员以 GENERIC_WRITE 打开 `\\.\C:` 得到)刷**整卷**——全系统挂起的写一锅端,没事别碰。本目录只测了文件句柄,卷句柄实验会拖累系统盘,不入册。

## 复跑注意(诚实口径)

- Windows 没有 `/proc/meminfo Dirty` 那样的可观测干净窗:Linux 侧靠 wait_clean 把 Dirty 压回低位再起表,这边只能"每轮删文件 + 上一轮的 flush 已落盘"接近,轮间干扰无法显式排除。原始轮次全贴在 .out,第一轮偶尔偏慢即是此故。
- `e2_flush_bench.exe` 自己把目标文件放在 `%TEMP%`(真实 NTFS),别改成 WSL 路径——9P 协议会把计时变成网络文件系统测试。
- 快盘上 plain 与 flush 的差距只有 ~2.2x;机械盘或写缓存关闭的机器上差距会大得多。数字属于这台机器,相对关系才可迁移。
