# 03-raii —— E3 unique_file_lock:把锁的生死绑到对象生死上

对照 Linux 侧 E5 的 `file_lock`(flock 版),同一副 `unique_lock` 心智模型:构造加锁、析构放锁、`defer_lock`、`try_lock`、`try_lock_for`、move-only、moved-from 空壳。

## 与 Linux 侧的三处结构差异(都写进了头文件注释)

1. **锁与句柄可分离**:那边一把 flock 锁绑一个 fd,close 即全放;这边 `UnlockFile` 只放锁不关句柄,`CloseHandle` 连锁带句柄一起放。析构走两步:显式 `UnlockFile`(把「放锁」写在时间线上看得见的位置)+ `CloseHandle` 兜底。
2. **try_lock 的 33 不是错误**:`ERROR_LOCK_VIOLATION` 是「锁被占着」的正常答案,返回 false;其余错误理应升级成异常,但 `noexcept` 屋檐下抛就是 terminate,只能吞——与 Linux 侧头文件里的取舍一字不差。
3. **起跑线**:Windows 起一个新进程比 fork 贵一个量级,A 用命名事件(`Local\\l05win_ticket`)通知「锁已到手、ticket 已写」,B 才开始三连试——不设起跑线的话 B 的第一次 try_lock 会抢在 A 拿锁之前,日志不可信。

## 双进程时序(`raii_demo.out`,与 Linux 侧 E5 同形)

```text
A:构造 unique_file_lock,已写 "ticket=42",持锁 700 ms
B:try_lock() = false(锁在 A 手里)
B:try_lock_for(200ms) = false(实际等了 200 ms,超时)
A:作用域将尽,unique_file_lock 析构在即 / 已放锁(显式 UnlockFile)+ CloseHandle
B:try_lock_for(3s) = true(等了 522 ms —— A 一放锁,下一轮询就拿到)
B:读到 "ticket=42"(临界区数据完好)
```

move 半场:lk1 持锁 → move 赋值给 lk2 → 探针仍被挡(moved-from 析构碰不到锁)→ lk2.reset() → 探针拿到。

## 复现

```sh
cd /mnt/c/msys64/tmp/l05win
/mnt/c/msys64/ucrt64/bin/g++.exe -std=c++20 -Wall -Wextra raii_demo.cpp -o raii_demo.exe   # unique_file_lock.hpp 在同目录
./raii_demo.exe demo
```
