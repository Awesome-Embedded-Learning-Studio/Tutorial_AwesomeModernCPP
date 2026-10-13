# 01-virtualalloc 配套实验

《虚拟内存:VirtualAlloc/VirtualProtect》的实验代码与原始输出存档,`.out` 全部是当轮机器的原始捕获(含 `$` 开头的命令行回显)。本篇是**纯内存视角**(无文件参与);映射视图视角的 VirtualProtect 与 SEC_RESERVE 两段式在 W02(`file-io/02-file-mapping`),VEH 工具链与退出码读取口径在 W03(`file-io/03-seh-veh`)。

## 环境

- Windows 11 26200(26H2 线);MSYS2 UCRT64 g++(Rev 5)16.1.0,动态链接
- 编译:`g++ -std=c++20 -Wall -Wextra eN_*.cpp -o eN_*.exe`(全部默认 -O0,零警告)
- 程序开头一律 `setvbuf(stdout, NULL, _IONBF, 0)`:崩溃前的输出不经缓冲
- WSL 直跑 `$?` 是 wait status 低 8 位(e3b overflow 的 0xC00000FD 截成 253)

## 目录与实验对照

| 目录 | 实验 | 一句话结论 |
|---|---|---|
| `01-reserve-commit/` | E1 保留/提交全景 | `MEM_RESERVE 1GiB` 约 2~5 微秒到手,CommitCharge 分文不动(对应 Linux overcommit);提交 72KiB 后 CommitCharge +80KiB、触碰 18 页后 WS 才 +72KiB —— 提交是承诺,触碰才占物理;`MEM_DECOMMIT` 退回 RESERVE 且可再提交;`MEM_RELEASE` 必须**整块基址 + dwSize=0**(非基址/带尺寸都实测报 87 ERROR_INVALID_PARAMETER) |
| `02-granularity/` | E2 两组粒度 | 返回地址一律 64KiB 对齐(dwAllocationGranularity=65536),**RegionSize 却按页取整**:1 字节→1 页、0xFFFF→16 页、0x10001→17 页、0x100001→257 页;MEM_TOP_DOWN 从 0x7FF4 带往下发、两笔精确间隔 0x100000,默认模式在低带(e1 落 0x02xx、e2 落 0x01xx),td1-low≈126.5TiB≈用户态全程的 99%,几乎从头穿到尾(e2 .out 末行括注的"半个"是打印口误,以换算为准) |
| `03-page-guard/` | E3 一次性陷阱 | `e3`:写前 Protect=0x104 → VEH 收 0x80000001 → 重放成功后 VQ 实读 **0x004(GUARD 位自灭)**,同页第二次写零异常,VirtualProtect 再武装又能响一次 —— 与 NOACCESS 持续封路的对照。`e3b layout`:主线程栈 = 2MiB 预约里的 COMMIT 段 + **guard 带(实测 2~3 页 0x104)** + RESERVE;压栈 768KiB 后 guard 带下移 764KiB,扩展期间 VEH 零命中(内核在 guard 异常上静默长栈)。`e3b overflow`:256KiB 工作线程递归 depth=122 预约耗尽,VEH 收 0xC00000FD 后进程收场 |
| `04-virtualquery-scan/` | E4 Windows 版 maps | 193 步扫完 128TiB;非 FREE 区段 156 个(COMMIT/IMAGE 99、COMMIT/MAPPED 16、COMMIT/PRIVATE 17、RESERVE 24),FREE 空洞 37 个合计 128TiB。对照 Linux:同体量程序 `/proc/self/maps` 约 25~55 行且**每行带后备文件路径**;VQ 只给 Type 不给主人 |
| `05-heap-layers/` | E5 三层一张表 | malloc(32) 与默认堆 HeapAlloc(32) 同 AllocBase —— CRT malloc 坐在进程堆上;尺寸阶梯三档:≤384KiB 主堆段内 / 416KiB~1016KiB 新堆段(多块共享,free 后段保留、页退订或缩成 1 页头)/ **≥1MiB 直发 VirtualAlloc**(独立区段,HeapFree 后整段 FREE);GetProcessHeaps 建堆前 2 个、建后 3 个 |

## 坑与发现(全部实测)

1. **VirtualQuery 探针语义**:`BaseAddress` 是探测页下取整,**`RegionSize` 是"该页到区段尾"的剩余量** —— 探针不在区段头时,读数不是整段大小。e3b 最初沿栈下探时相邻两次查询出现"嵌套读数"就是这么来的,后改成逐页采样分组。
2. **VQ 的分组键含 AllocationBase**:相邻、同 State/Protect/Type 的两笔独立分配**不合并**成一段(E1 的 p1/p2);释放 p1 后 p2 的视图纹丝不动 —— "每笔 VirtualAlloc 是独立预约,相邻≠一体"。
3. **直接 MEM_COMMIT 的粒度块尾巴是 FREE**(不是隐藏预约):要 1 页只提交 1 页,同粒度块剩 60KiB 是 FREE,下一笔紧挨着继续发(所以连发地址常常连号;本轮空洞被 printf 的堆扩段插队时会断开,E2 步骤2 有探针)。
4. **Win11 的栈 guard 是 2~3 页的带**,不是教科书单页;栈扩展异常由内核静默消化,用户态 VEH 全程零命中,只在预约耗尽时看到 0xC00000FD。
5. **CreateThread 的 dwStackSize 是初始提交量**:请求 256KiB,顶层 64 页直接 COMMIT,预约则继承 PE 头的 2MiB(mingw 链接器默认);SetThreadStackGuarantee(64KiB) 给 VEH 留了保底栈,溢出时的 printf 才能活到落盘。
6. **每个 Win11 进程都有 4GiB+128KiB 与 32MiB 两笔 RESERVE/PRIVATE**(地址随 ASLR 变、大小恒定)。对照组:绕开 WSL interop 直连 cmd 跑、以及 `-static` 剥掉 mingw 运行库跑,两笔都在 → 系统 DLL 链的预约;VQ 见形不见主,问归属要 NT 层。
7. **E1 的 CommitCharge 是进程级总量**,有 stdio/堆的噪声(退提交后采样反而 +136KiB),要看阶段差,别拿单点对单点。
8. **E4 对账口径**:CommitCharge(1004KiB) > COMMIT/PRIVATE 合计(376KiB),差额是 DLL/映射段的共享提交与内核侧记账 —— 对照 Linux 的 Private 与 VmSize 之差。

## Working Set(E6,一句话)

E1 的三段采样就是 working set 观察(提交≠物理);`SetProcessWorkingSetSize`/`K empty working set` 本篇不展开,内存优先级 Offer/Reclaim 见 W02 `03-prefetch-offer`。

## 复现

```sh
# 在各子目录里;g++ 是 /mnt/c/msys64/ucrt64/bin/g++.exe
g++ -std=c++20 -Wall -Wextra e1_reserve_commit.cpp -o e1_reserve_commit.exe && ./e1_reserve_commit.exe

# e3b 两个模式
./e3b_stack_guard.exe layout
./e3b_stack_guard.exe overflow   # 进程必崩:$? = 253(0xC00000FD 的低 8 位)
```

## 复跑注意

- 输出里的地址全吃 ASLR,与你的机器不同是正常的;引规律(粒度、状态机、阈值档位),不引具体数值。
- e3/e3b 复跑保持 `-O0`:摸页写指令的位置是证据链的一部分,别让优化器挪动它(W03 的 -O2 教训是 SEH 的 `__try1` 宏,与本篇是两回事)。
- E4 的对照组(直连 cmd / `-static`)数字只记在本 README,不单独立档。
- 堆阈值(E5)依赖默认堆当时的状态,档位边界(384K/416K 之间、1016K/1024K 之间)以本机本轮为准,复跑同量级。
