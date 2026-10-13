# 03-proc-locks —— /proc/locks 观察(E3)

环境:WSL2 内核 6.18.33.2,数据文件在 ext4。持锁者给同一文件同时上 flock(LOCK_EX) + F_SETLK W[0,100),另有两个等待者分别挡在 flock 和 F_SETLKW 上;主进程在锁全部在场时读 /proc/locks,按 inode 过滤。

## 真实输出(`proc_locks.out`)

```
19: POSIX  ADVISORY  WRITE 167263 08:30:1690193 0 99
19: -> POSIX  ADVISORY  WRITE 167265 08:30:1690193 50 149
20: FLOCK  ADVISORY  WRITE 167263 08:30:1690193 0 EOF
20: -> FLOCK  ADVISORY  WRITE 167264 08:30:1690193 0 EOF
```

## 逐字段解码

| 字段 | 值例 | 含义 |
|---|---|---|
| 1 | `19:` | 本次读到的序号,每次读从 1 重排,别当稳定 id |
| 2 | `POSIX`/`FLOCK` | 锁类别:FLOCK(BSD flock)、POSIX(fcntl 记录锁)、OFDLCK(OFD 锁)、LEASE/DELEG(租约) |
| 3 | `ADVISORY` | 咨询锁(MANDATORY 是强制锁口径,已废弃) |
| 4 | `WRITE`/`READ` | 锁类型 |
| 5 | `167263` | 属主 pid;**OFD 锁显示 -1** |
| 6 | `08:30:1690193` | 设备号:inode。设备号是**十六进制**(8:48 = /dev/sdd);**inode 是十进制**,拿 `stat -c %i` 直接对 |
| 7/8 | `0 99` / `0 EOF` | 起始/结束字节偏移。POSIX 锁是**闭区间端点**:l_start=0, l_len=100 → `0 99`;到 EOF 写 `EOF`;flock 恒为 `0 EOF` |

被挡住的等待请求以 `->` 缩进挂在**挡路者的序号**下(19 号锁、19 号等待)。全部退场后再读为空。

## 踩坑实录(值得写进文章)

1. **inode 是十进制,设备号才是十六进制**。man 5 proc_locks 对字段 6 只说「冒号分隔的子字段 + inode」,没写进制;内核 `lock_get_status()` 的格式串是 `"%d %02x:%02x:%llu"`——设备号 %02x、inode %llu。拿 `printf '%x' $(stat -c %i)` 的十六进制串去 grep 会一无所获,还容易得出「我的锁不在 /proc/locks」的错误结论(本实验第一版就栽在这)。
2. 等待者的区间也是自己的请求区间(`50 149`),不是被挡区间。
3. 本机常有别家长驻进程(dotnet / VS Code 系)的锁在场,行数会有背景噪声,过滤要看 inode。

## 复现

```sh
mkdir -p ~/l05_scratch/e3
g++ -std=c++20 -Wall -Wextra -Wpedantic -O2 -I ../common proc_locks.cpp -o /tmp/e3
/tmp/e3 | tee proc_locks.out    # 程序自 fork 持锁者与两个等待者,约 1.1 s
```
