# 04-align-hugepage 配套实验

《对齐分配与大页》(vol8 systems-programming/linux/memory L04)的实验代码与原始输出存档。核心问题一句话:**对齐是硬件的合同(_mm256_load 的 32 字节、cache line 的 64 字节),大页是页表的合同(2 MiB vs 4 KiB),这两份合同在本机上分别值多少、由谁守住、又由谁撕毁**。六个实验:对齐分配三件套的正确性与报错口径(E1)、对齐的两个物理代价——对齐版指令段错误与伪共享(E2)、THP 透明大页的正反对照与 WSL2 的进程级开关(E3)、显式大页 hugetlb 在无特权环境的能力边界(E4)、Overcommit 三模式的实测阶梯与 VmSize/VmRSS 数字链(E5)、OOM killer 的只读观察与安全余量内的占用/回收演示(E6)。

malloc 的分水岭行为(malloc 何时走 mmap、mmap_threshold 爬升)是本篇前置,在 memory/01 已讲,这里不重复。伪共享的机制理论在 vol5 讲过,这里只出本机数字。

## 环境口径(所有 .out 都来自这一轮)

| 项 | 值 |
|---|---|
| 机器 | 台机,AMD Ryzen 7 9700X 8-Core(16 逻辑核),avx2 可用 |
| 系统 | WSL2 Arch Linux,内核 6.18.33.2-microsoft-standard-WSL2 |
| 编译器 | g++ (GCC) 16.2.1,`-std=c++20 -Wall -Wextra -Wpedantic -O2`(e2_avx 的 AVX 靠函数级 `target("avx2")` 属性,无全局 -mavx2) |
| glibc | 2.44 |
| 内存 | MemTotal 55522928 kB(~53 GiB)+ Swap 16777216 kB(16 GiB),RAM+swap ≈ 69.4 GiB |
| THP sysfs | enabled=`always [madvise] never`,defrag=`... [madvise] ...`,hugepages-2048kB/enabled=`always [inherit] madvise never`(继承全局),更小的 mTHP 档全部 [never],shmem_enabled=[never] |
| vm 口径 | overcommit_memory=0(启发式),overcommit_ratio=50,nr_hugepages=0 |
| 进程级 THP | **prctl(PR_GET_THP_DISABLE)=1**:WSL2 的 init 链(systemd 前的微软 init)给整个进程树设了 MMF_DISABLE_THP,出厂状态下 THP 对用户进程全部失效,详见下面 E3 |
| sudo | `sudo -n true` 不可用(需要密码),sysfs/sysctl 写入类全部放弃,如实记录 |
| scratch | 源码不写盘,无需数据目录;`.out` 均为程序自含输出(路径无烧死) |

## 目录与结论对照

| 目录 | 实验 | 一句话结论 |
|---|---|---|
| `01-aligned-alloc/` | E1 对齐三件套 | aligned_alloc/posix_memalign/对齐 new 三者在 align=64/4096、size∈{64,100,4096,10000} 全部 16 组采样里 `addr % align == 0` 全绿;非法对齐口径分家——aligned_alloc 非二次幂返回 NULL+errno=EINVAL,posix_memalign 把 EINVAL 当**返回值**(不动 errno),对齐 new 抛 std::bad_alloc;size 非对齐倍数在 glibc 2.44 被接受(C17 放宽口径) |
| `02-why-align/` | E2a 对齐版指令 | 未对齐地址(%%32=8)喂 `_mm256_load_si256` → 立即 SIGSEGV,si_code=SI_KERNEL(128)、si_addr 无意义(这是 #GP 不是缺页);同一地址喂 `_mm256_loadu_si256` 完好。汇编证据在 e2_disasm.txt:前者 `vmovdqa (%rdi),%ymm`、后者 `vmovdqu`,对齐要求写在指令编码里 |
| | E2b 伪共享 | 两线程各 4 亿次 volatile 自增:相邻(同 64B line)529.0 ms vs alignas(64) 隔开 74.4 ms(中位数),**7.11x**。理论见 vol5,此处为本机复现 |
| `03-thp/` | E3 THP | 两幕结构。第一幕(出厂):MADV_HUGEPAGE 生效到 VMA(smaps VmFlags 出现 `hg`)但 AnonHugePages=0、THPeligible=0、MADV_COLLAPSE=EINVAL——**进程级 MMF_DISABLE_THP 压过 sysfs**;第二幕(`prctl(PR_SET_THP_DISABLE,0)` 清掉开关,本内核允许):同一操作 AnonHugePages=1048576 kB(1 GiB 全部大页化,512 个 2 MiB 页),首触写 55.4 vs 300.5 ms(**5.43x**,262144 次→512 次缺页),全量 memset 39.7 vs 40.7 ms(1.02x,流写下 TLB 红利吃不到,如实记录)。诊断链与 MADV_COLLAPSE 同步塌缩(0→32768 kB)在 e3_thp_probe |
| `04-hugetlb/` | E4 显式大页 | nr_hugepages=0 且非特权 open 写 → EACCES;MAP_HUGETLB(2 MiB/1 GiB/默认页大小三档)全部 ENOMEM——预留池为空,mmap 层直接拒;对照组普通匿名 mmap 同尺寸秒成。结论:显式大页要管理员预留(启动参数或 root 写 sysctl / 挂 hugetlbfs),无特权环境的大页路径只剩 E3 的 THP |
| `05-overcommit/` | E5 Overcommit | 模式 0 下单次 malloc 阶梯:512/256/128 GiB 全部 NULL+ENOMEM,**64 GiB 成功**——红线就是 RAM+swap≈69.4 GiB(单次申请大于它即拒);同一 512 GiB 走 MAP_NORESERVE 立即成功(明示不做承诺记账)。数字链:映射后 VmSize 7304→536878216 kB 而 VmRSS 不动(4044 kB),只写 1 GiB 后 VmRSS=1052620 kB,munmap 后双双回基线 |
| `06-oom/` | E6 OOM | 只读+受控演示,不真触发。oom_score 基线 666(本机所有进程都显示 666,相对分摊语义),touch 26.6 GiB(=MemAvailable 的 60%)后涨到 924,free 后回落;oom_score_adj 自写 +250/归零成功、-1000 → EPERM(无特权只能调"更该杀",不能"免死");VmRSS 4208→27939332→4212 kB、MemAvailable 46.5→19.4→46.8 GiB 同步开合,占用/回收链闭合。dmesg 全量扫描 0 条 OOM 记录(见 e6_dmesg.txt) |

## E3 的 WSL2 大发现:THP 被进程级开关关死

按 sysfs(`enabled=[madvise]`)口径,MADV_HUGEPAGE 应当生效;实测全部失效。诊断链(e3_thp_probe.out 一步步排除):

1. sysfs 正常:全局 [madvise],2048kB 档 [inherit] 继承全局;
2. madvise 正常:VMA 的 VmFlags 出现 `hg`(NOHUGEPAGE 则 `nh`),说明调用到了;
3. 但 smaps `THPeligible: 0`、/proc/vmstat 的 thp_fault_alloc/fallback 全程 +0(内核连尝试都没有),MADV_COLLAPSE 直接 EINVAL;
4. 根因:`prctl(PR_GET_THP_DISABLE) = 1`——WSL2 的 init 链(systemd 之前的微软 init)给整个进程树设了 MMF_DISABLE_THP。该标志跨 fork/exec 继承,sysfs 看不见它;
5. 逃逸口:本内核(6.18)允许 `prctl(PR_SET_THP_DISABLE, 0)` 清掉开关(传 0 即清除)。清完 THP 立即复活:同一程序同一操作,AnonHugePages 从 0 变满配。

普通裸机 Linux 没有这一层;WSL2 读者跑 THP 实验拿到 0 时,先查 `prctl(PR_GET_THP_DISABLE)` 再怀疑人生。另注:本机所有小进程的 /proc/self/oom_score 都显示 666(E6),同样是"相对值"语义的体现,touch 26 GiB 后才拉开差距。

## E5 的一个测量坑(记给复跑的人)

`gcc -O2` 会把"只做空判"的 malloc/free 对**整个优化掉**(strace 里根本没有对应 mmap),初版 e5 的阶梯曾因此记出 256 GiB 假成功。修正:把指针本身 `%p` 打出来(编译器无法折叠),并用 strace 复核每个档位真发了 syscall。repo 里这版是修正后的。

## 复现

```sh
cd code/volumn_codes/vol8/systems-programming/linux/memory/04-align-hugepage
cmake -B build && cmake --build build -j

./build/e1_aligned_alloc            # ~0 s
./build/e2_avx; echo "exit=$?"      # exit=139(段错误是实验本体,handler 打完证据才退)
./build/e2_false_sharing            # ~10 s
./build/e3_thp_probe                # ~1 s,诊断链
./build/e3_thp                      # ~20 s,两幕计时
./build/e4_hugetlb                  # ~0 s
./build/e5_overcommit               # ~1 s
./build/e6_oom                      # ~30 s,calloc+touch MemAvailable 的 60%

# 单发编译(等价口径):
# g++ -std=c++20 -Wall -Wextra -Wpedantic -O2 -o e2_avx 02-why-align/e2_avx.cpp
# g++ -std=c++20 -Wall -Wextra -Wpedantic -O2 -pthread -o e2_false_sharing 02-why-align/e2_false_sharing.cpp

# 汇编证据复核:
# g++ -std=c++20 -O2 -c 02-why-align/e2_avx.cpp -o /tmp/e2_avx.o && objdump -d /tmp/e2_avx.o | grep vmovdqa
```

E6 的 60% 安全余量口径是刻意的:先读 MemAvailable 再决定 touch 量,用完立释,全程不逼近系统上限,不做任何可能打挂 WSL2 的演示。全部 .out 是 2026-10-03 单轮原始输出,未 commit。
