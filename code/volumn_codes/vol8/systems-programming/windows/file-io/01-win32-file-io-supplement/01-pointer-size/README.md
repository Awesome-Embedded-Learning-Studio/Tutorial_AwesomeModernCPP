# 01-pointer-size:SetFilePointerEx / GetFileSizeEx 配套实验

《Win32 文件 I/O》补课段(指针三件套)的真机实验与原始输出存档:`.out` 全部是当轮机器的原始捕获(`$` 开头的行是当时敲的命令)。

## 文件与实验对照

| 文件 | 内容 |
|---|---|
| `e1_pointer_moves.cpp/.out` | FILE_BEGIN/FILE_CURRENT/FILE_END 三基准 + 正负偏移;查询当前位置惯用法(距离 0 + FILE_CURRENT);越过 EOF 移动不改尺寸;EOF 处 ReadFile = TRUE + 0 字节;FILE_BEGIN 负偏移 → ERROR_NEGATIVE_SEEK(131) |
| `e1b_hole.cpp/.out` | 越过 EOF 再写:洞出现、尺寸 1MiB+1、洞读为零;EndOfFile vs AllocationSize;`FSCTL_SET_SPARSE` 前后的分配对照;`FILE_FLAG_NO_BUFFERING` 越过 EOF 写 |
| `e1c_size_sources.cpp/.out` | GetFileSizeEx / GetFileInformationByHandle / FileStandardInfo 三路读数对账;B 句柄写大 A 句柄跟读;`nFileIndex` 的同一文件证明 |
| `e1d_pointer_sharing.cpp/.out` | 两次 CreateFileW 指针独立 vs DuplicateHandle 指针共享(双向推进/回退互见) |
| `e1e_linux_side.cpp/.out` | Linux 侧对照(WSL 原生 g++ 编译):open x2 独立偏移、dup 共享偏移、lseek 越 EOF 写出零洞、st_blocks 只算实写 |

环境:Win11 26200 / MSYS2 UCRT64 g++ 16.1.0 / NTFS 系统盘(NVMe);`e1e` 在同一台机器的 WSL2(ext4)里跑。编译运行命令见各 `.cpp` 头部注释。

## 关键结论(均有 .out 对应行)

**1. 指针三件套的行为边界:**

| 操作 | 实测 |
|---|---|
| FILE_BEGIN +3 / FILE_CURRENT ±2 / FILE_END −3 | 各落 3/5→4/7,与 lseek 三基准同构 |
| 查询当前位置(距离 0 + FILE_CURRENT) | 成功,指针不动 |
| 越过 EOF 移动(+103、FILE_END +1000000) | 合法,GetFileSizeEx 仍是 10 |
| 在越过 EOF 处 ReadFile | TRUE + 0 字节,EOF 不是错误 |
| FILE_BEGIN 配负偏移 | 失败,ERROR_NEGATIVE_SEEK(131),指针原地 |

**2. 洞与记账(NTFS 与 ext4 的真差异在 AllocationSize):**"AB" + 1MiB 处 "Z" 后 EndOfFile=1048577、洞读为零,两边一致;但默认(非稀疏)NTFS 文件的 AllocationSize=1052672 —— 洞区照样记账;`FSCTL_SET_SPARSE` 之后同款洞 AllocationSize=131072(两个实写区段)。ext4 的洞默认就不占:e1e 实测 st_blocks=16(8KiB)装下 1MiB+1 的文件。**一句话:洞读出来都是零;省不省空间,ext4 天生省,NTFS 要显式开稀疏。**

**3. NO_BUFFERING 越过 EOF 写:文档说可能炸 87,本机实测成功且洞是零**(e1b [4]:ret=1 写到 4096,洞读回全 00)。官方 File Buffering 文档的口径是"零填充要靠缓存管理器,非缓冲写越过 EOF 会 ERROR_INVALID_PARAMETER",Win11 26200 的 NTFS 上没有兑现——如实记录,别按老文档赌这个错误路径。

**4. 尺寸挂在文件上,不挂在句柄上:**三个来源(GetFileSizeEx / BY_HANDLE_FILE_INFORMATION 64 位拼装 / FileStandardInfo.EndOfFile,外加老 32 位 GetFileSize)读数全一致;句柄 B 在 1MiB 处写 1 字节,句柄 A 原地重读,三路全部变成 1048577。BY_HANDLE_FILE_INFORMATION 额外给卷序列号 + nFileIndex:两次 CreateFileW 的 nFileIndex 相同(同一文件),指针独立是文件对象层面的事(e1d)。

**5. 指针共享语义,Windows 与 POSIX 逐条对齐(两侧都实测):**

| 动作 | Windows | POSIX(e1e) |
|---|---|---|
| 各自打开 | CreateFileW x2 → 指针独立(h1 读 4 字节后 pos(h2) 仍 0) | open x2 → 偏移独立 |
| 复制句柄/描述符 | DuplicateHandle → 共享(h3 读 4 字节把 h1 推到 10,h3 回退 −4 时 h1 跟着回) | dup → 共享(同一个打开文件描述) |
| 继承 | 句柄可继承 + bInheritHandles(本实验未展开) | fork 子进程共享父进程的偏移 |

机制上 Windows 的"文件对象"对应 POSIX 的"打开文件描述(open file description)":CreateFileW 各造一个,DuplicateHandle/dup 只是复制指向它的表项。**没有"每次 CreateFileW 共享指针"一说。**

## 复跑注意

- pid、句柄值(nFileIndex、h=0x…)每次不同,别拿绝对值对表。
- e1b 的 AllocationSize 反映当轮 NTFS 分配策略,稀疏文件的 131072(两个 64KiB 区段)是本机观测值,复跑数量级应一致、数值可能不同。
- `e1e_linux_side.cpp` 用 WSL 原生 `g++` 编译,不走 MSYS2。
