# 07-fs-compare —— ext4 vs tmpfs 语义对照(E7)

环境:WSL2 内核 6.18.33.2。同一份探针跑两个文件系统:ext4 上的 `~/l05_scratch/e7/ext4.bin`(设备 8:48)与 tmpfs 上的 `/tmp/l05_e7_tmpfs.bin`(设备 0:77)。

## 结论(对照 `ext4.out` / `tmpfs.out`)

把两份输出的路径、pid 归一化后**逐行一致**:

| 探针 | ext4 | tmpfs |
|---|---|---|
| P1 flock 互斥时序 | B 阻塞 300 ms 后拿到 | 同 |
| P2 同进程重新 open 自冲突 | -1/EWOULDBLOCK | 同 |
| P3 fcntl close 无关 fd → 全释放 | 竞争者从被占变拿到 | 同 |
| P4 /proc/locks 设备号字段 | `08:30:1716962` | `00:4d:11972` |

**锁语义在 VFS 层实现,与具体文件系统无关**——tmpfs 是纯内存文件系统,一样完整支持 flock 与 fcntl 记录锁(以及 /proc/locks 可见)。两份输出唯一可见的差异是 `/proc/locks` 第 6 字段的设备号(ext4 块设备 08:30,tmpfs 的 anonymous 设备 00:4d)与 inode 数量级。

## 复现

```sh
mkdir -p ~/l05_scratch/e7
g++ -std=c++20 -Wall -Wextra -Wpedantic -O2 -I ../common fs_probe.cpp -o /tmp/e7
/tmp/e7 ~/l05_scratch/e7/ext4.bin | tee ext4.out     # 参数 = 数据文件路径
/tmp/e7 /tmp/l05_e7_tmpfs.bin       | tee tmpfs.out  # /tmp 须为 tmpfs(df -T /tmp 确认)
```
