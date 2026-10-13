# 03-sharemode:dwShareMode 全矩阵配套实验

《Win32 文件 I/O》补课段(dwShareMode 专节)的真机实验与原始输出存档。这是三个补课项里与 POSIX 分歧最大的一块:共享检查挂在内核文件对象上,同进程两次打开也逃不掉。

## 文件与实验对照

| 文件 | 内容 |
|---|---|
| `e3_matrix.cpp/.out` | [1] share x 访问全矩阵 [2] 双向检查 [3] 删除/改名(无 D vs 有 D vs 改名) [4] 同进程约束注记 [5] 指针独立 + 数据可见 [6] 跨进程复验(自重挂子进程 + auto-reset 事件握手) |
| `e3b_nobuffering_stale.cpp/.out` | 缓存一致性附加实验:缓冲写 + FILE_FLAG_NO_BUFFERING 读两种排列,试图复现文档警告的脏读 |

环境:Win11 26200 / MSYS2 UCRT64 g++ 16.1.0 / NTFS 系统盘。

## 关键结论(均有 .out 对应行)

**1. 矩阵本体(第一个句柄固定 GENERIC_READ\|GENERIC_WRITE,第二个句柄 share 给足,只看访问列):**

| 第一句柄 share | 要 READ | 要 WRITE | 要 R\|W | 要 DELETE |
|---|---|---|---|---|
| 0(独占) | 拒 32 | 拒 32 | 拒 32 | 拒 32 |
| R | 成功 | 拒 32 | 拒 32 | 拒 32 |
| W | 拒 32 | 成功 | 拒 32 | 拒 32 |
| R\|W | 成功 | 成功 | 成功 | 拒 32 |
| R\|W\|D | 成功 | 成功 | 成功 | 成功 |

拒 32 = ERROR_SHARING_VIOLATION。规则一句话:**新句柄要的访问得是每个在场句柄 share 放行的;在场句柄正在用的访问,也得是新句柄 share 装得下的——两把尺子都要过。**

**2. 双向检查([2]):**第一句柄访问 R|W、share R|W;第二句柄只要 READ:share=0 → 拒 32;share=R → 拒 32(装不下第一句柄的写访问);share=R|W → 开到句柄。**share 声明的是"我允许别人怎么动这个文件",不是"我能开门"。**

**3. 删除/改名([3],两处与常见资料不同,本机实测为准):**

| 场景 | 实测 |
|---|---|
| share=R|W(无 D)时 DeleteFileW | 失败 err=**32**(不是资料常说的 5/ACCESS_DENIED) |
| share=R|W(无 D)时 MoveFileExW 改名 | 失败 err=32;句柄关闭后同一调用成功 |
| share=R|W\|D 时 DeleteFileW | 成功;FileStandardInfo.DeletePending=1;**句柄照常 ReadFile/WriteFile** |
| 删除后文件名 | **立刻消失**:GetFileAttributesW err=2,新开句柄 err=2(FILE_NOT_FOUND) |
| 最后一个句柄关闭 | 文件真正回收 |
| share=R|W\|D 时 MoveFileExW 改名 | 成功;原句柄继续读到内容(句柄跟文件走);新名字健在 |

Win11 26200 的 DeleteFileW 在 FILE_SHARE_DELETE 配合下走的是"名字立刻摘掉、句柄吊命到最后一关"的近 POSIX 语义;老资料里"名字留到最后一关、新开句柄撞 ACCESS_DENIED"的 classic delete-pending 形态在这台机器上没有出现。

**4. 同进程约束([4]):**[1]-[3] 的"第二个句柄"全部开在同一个进程里——Windows 的共享检查不看出身,自己 open 自己也会被自己拦;POSIX 的 open() 对同一进程再开一次没有任何约束(Linux 侧从无此维度)。

**5. 指针独立 + 数据可见([5]):**share=R|W 双开后,h1 覆写后 pos(h1)=2、pos(h2)=0(一个的偏移推不动另一个);h2 从 0 读立刻读到新数据——两个普通(缓冲)句柄之间缓存一致。

**6. 跨进程复验([6]):**父进程 share=0 挂住文件,真子进程(自重挂 + 事件握手)CreateFileW(READ) → 拒 32;父进程 CloseHandle 后第二轮 → 成功开到句柄。进程边界内外行为一致,矩阵结论对多进程场景直接成立。

**7. 附加(e3b,负结果如实记):**share 全开也不等于什么都同步——把读端换成 FILE_FLAG_NO_BUFFERING(绕缓存直读盘),文档警告缓冲写的脏页没落盘时可能读到旧数据。本机两种排列(写后新开直读句柄 / 直读句柄先开好跨写保持)读到的都是新鲜 BB,又加测 32MiB 整段缓冲写完立刻读尾页,同样 BB。**三种加码都没把旧数据钓出来**:文档的警告仍在(混用前 FlushFileBuffers 是正路),但这台 Win11 26200 / NTFS / NVMe 上"读到什么看缓存"的坏下场没复现。

## 复跑注意

- pid、句柄值每次不同;[6] 的子进程 pid 与父进程无关(Windows 的 pid 分配),别对表。
- e3_matrix 的输出顺序依赖 `setvbuf(stdout, nullptr, _IONBF, 0)`:管道捕获下 stdout 全缓冲,父子两进程的行会乱序,别删这行。
- [3] 的两个"与资料不符"(err=32、名字立刻消失)是 Win11 26200 实测;系统升级后可能漂移,复跑以 .out 重跑结果为准。
