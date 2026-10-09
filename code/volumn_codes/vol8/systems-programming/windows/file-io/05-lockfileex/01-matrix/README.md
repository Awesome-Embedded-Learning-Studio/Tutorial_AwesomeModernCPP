# 01-matrix —— E1 LockFileEx 语义矩阵 + 句柄继承 I1..I5

一份二进制多角色(`matrix.exe demo` 串起全部;子角色 `holdA/waitB/shareX/inherit/stale/proxy/sleeper` 由主驱动 CreateProcessA 派出,各自写日志文件、主进程按时间戳归并,`.out` 才是确定的)。数据文件 `C:/msys64/tmp/l05win/matrix.bin`(1000 字节零填充,`[200,EOF)` 类观察要用 EOF 位置)。

## 各节结论(对 `matrix.out` 逐节)

| 节 | 观察 | 结果 |
|---|---|---|
| E1a | 独占锁两进程时序 | A 持 800ms,B 阻塞版 812ms 处(A 放锁同毫秒)接棒 |
| E1b | 共享锁 | 两边同时 TRUE 同持;独占探针 33 进不来 |
| E1c | FAIL_IMMEDIATELY | 同区间 FALSE+GetLastError()=33(ERROR_LOCK_VIOLATION);不相交 TRUE |
| E1w | **锁与读写** | 别的句柄 ReadFile@锁内 = FALSE+33;锁外 TRUE;持锁者自己 TRUE;共享锁下读 TRUE 写 FALSE+33 —— **半强制,不是 POSIX 的咨询锁**(文档另注明映射视图不受限,未测) |
| E1d | 字节区间 | [50,150) 撞 [0,100) 33;[100,200) 过;[99,101) 单字节交叠也 33;**长度 0 = 空区间什么都没锁**(fcntl 的 l_len=0 是锁到 EOF);[200,1MiB) 锁过 EOF 不报错,这回 [900,50) 真被挡 |
| E1e | 同句柄区间语义 | EX 重叠/重复加锁 = 33(无属主豁免,对照 fcntl 替换、flock 转换);UnlockFile 精确匹配:第一次 TRUE 第二次 158,[10,20) 部分解锁 158;**文档特例:同句柄 EX 上叠 SH=TRUE,第一次解锁放独占(EX 探针 33/共享探针过)、第二次放共享** |
| E1f | 同进程两个句柄 | h2 试锁 33;阻塞版 1.5s 未返回(线程取证,g_woken=0)——同进程两句柄自锁死;CloseHandle(h1) 后线程应声拿到 |
| E1g | CloseHandle | 一次 UnlockFile 都不调,句柄一关名下两个区间全放 |
| E1h I1/I2 | 继承句柄 | 子进程自己 open 的新句柄 33;**继承句柄(同一文件对象)也 33**;UnlockFile(继承句柄)=158;父探针仍被挡——继承句柄既加不进也放不掉(对照 flock 子进程能替父放锁) |
| E1h I3 | bInheritHandles=FALSE | 传过去的句柄值 = FALSE+6(ERROR_INVALID_HANDLE),GetFileType=0 —— 句柄值没有跨进程意义 |
| E1h I4 | 锁的寿命 | proxy 上锁→spawn sleeper(继承句柄)→立即退出;proxy 退出当毫秒探针就 TRUE——**进程退出 OS 立刻清算,即便别的进程攥着同一文件对象的句柄**;sleeper 退场后仍 TRUE |
| E1h I5 | 归属单位判别 | DuplicateHandle 复制品(同进程同对象):加锁 33、**UnlockFile TRUE(放得掉原主的锁)**;全新 open(同进程新对象):UnlockFile 158;CloseHandle(h1) 后复制品还开着 → 锁仍在,全关才放 —— **解锁/释放认「进程 × 文件对象」** |

## 文档对应(Microsoft Learn,实测验漏补缺)

- LockFileEx 页 Remarks 原话:"If the file handle is inherited by a process created by the locking process, the child process is not granted access to the locked region."(对应 E1h②)、"Exclusive locks cannot overlap an existing locked region … A shared lock can overlap an exclusive lock if both locks were created using the same file handle … two unlock operations are necessary"(对应 E1e)、"If a process terminates with a portion of a file locked or closes a file that has outstanding locks, the locks are unlocked by the operating system."(对应 E1g/I4)、"Locking a region that goes beyond the current end-of-file position is not an error."(对应 E1d)。
- UnlockFile 页 Remarks:"The region to unlock must correspond exactly to an existing locked region."(对应 E1e)。
- **文档没写的部分**(实测补充):「进程 × 文件对象」的解锁归属(I5 三连测)、零长度=空区间、同句柄 EX+EX 自冲突的明确口径。

## 复现

```sh
cd /mnt/c/msys64/tmp/l05win
/mnt/c/msys64/ucrt64/bin/g++.exe -std=c++20 -Wall -Wextra matrix.cpp -o matrix.exe
./matrix.exe demo          # 全程约 6 s(E1f 的 1.5s 取证与 I4 的 1.7s 等待在内)
```
