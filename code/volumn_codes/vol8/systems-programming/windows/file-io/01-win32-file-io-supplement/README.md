# 01-win32-file-io-supplement:《Win32 文件 I/O》补课段配套实验

[01-win32-file-io.md](../../../../../../../documents/vol8-domains/systems-programming/windows/file-io/01-win32-file-io.md) 补课项(SetFilePointerEx / GetFileSizeEx / FlushFileBuffers / dwShareMode 专节)的真机实验与原始输出存档。三个子目录各管一个补课项,`.out` 全部是当轮机器的原始捕获(`$` 开头的行是当时敲的命令),结论行都带 .out 对应。

## 环境

- Windows 11 26200(26H2 线),NTFS 系统盘(NVMe,WD_BLACK SN7100)
- MSYS2 UCRT64 g++(Rev 5)16.1.0,x86_64-w64-mingw32
- 编译运行(WSL interop,cwd 必须在 WSL 文件系统上):
  ```text
  /mnt/c/msys64/ucrt64/bin/g++.exe -std=c++20 -Wall -Wextra xxx.cpp -o xxx.exe
  chmod +x xxx.exe && ./xxx.exe
  ```
- 目标文件一律放 `%TEMP%`(真实 NTFS):实验程序自己解析 GetTempPathW,绕开 `\wsl.localhost` 的 9P 路径,否则测的是网络文件系统

## 子目录与结论速览

| 目录 | 补课项 | 一句话结论 |
|---|---|---|
| [01-pointer-size/](01-pointer-size/) | SetFilePointerEx / GetFileSizeEx | 三基准与 lseek 同构;越过 EOF 写出零洞;指针共享语义 CreateFileW x2=独立 / DuplicateHandle=共享,与 POSIX open x2 / dup 逐条对齐(双 OS 实测) |
| [02-flush/](02-flush/) | FlushFileBuffers(+ FILE_FLAG_WRITE_THROUGH) | plain 4725 MiB/s vs flush 总吞吐 2166 MiB/s,差距就是缓存与盘的差;wt 与"写完刷一次"殊途同归,但小块 wt 塌到 82 MiB/s;fflush 只到 OS,FlushFileBuffers 才到盘 |
| [03-sharemode/](03-sharemode/) | dwShareMode 专节 | 全矩阵 + 双向检查两把尺子;同进程两次打开也受限(POSIX 无此维度);FILE_SHARE_DELETE 下 DeleteFileW 走近 POSIX 语义(名字立刻摘、句柄吊命);无 D 时删除/改名拒 32 |

## 意外发现(细节见各 README)

1. `FILE_FLAG_NO_BUFFERING` 越过 EOF 写,文档说可能 ERROR_INVALID_PARAMETER(87),本机实测成功且洞读为零。
2. 默认 NTFS 文件的洞照占 AllocationSize(非稀疏);`FSCTL_SET_SPARSE` 之后才像 ext4 那样只算实写区段。
3. 无 FILE_SHARE_DELETE 时 DeleteFileW/MoveFileExW 拒的是 32(SHARING_VIOLATION),不是资料常说的 5(ACCESS_DENIED)。
4. Win11 26200 的 DeleteFileW + FILE_SHARE_DELETE:文件名立刻消失(新开句柄 err=2),句柄照常读写到最后关闭——老资料的 classic delete-pending(名字留到最后一关、撞 5)没有出现。
5. 缓冲写 + NO_BUFFERING 读的文档级脏读,三种加码没复现(负结果如实入册)。

## 相关目录

- 契约工具(last_error_code / check_win32 / unique_handle)的定义与初轮实验:[../../thinking/](../../thinking/)
- Linux 侧对照(fsync / O_SYNC / 页缓存):[../../../linux/file-io/03-page-cache/](../../../linux/file-io/03-page-cache/)
- 同文章 SEH 篇的实验存档:[../03-seh-veh/](../03-seh-veh/)
