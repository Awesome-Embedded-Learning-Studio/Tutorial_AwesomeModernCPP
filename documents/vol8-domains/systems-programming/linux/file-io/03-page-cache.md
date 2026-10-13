---
title: "页缓存与持久性:write() 返回之后发生了什么"
description: "write() 顺利返回,只是把数据拷进了内核的页缓存,盘上的事还没发生:本篇用 /proc/meminfo 的 Dirty 计数看着 256 MiB 脏页滞留整整三十秒后无人调 sync 骤降(dirty_expire_centisecs 到点、flusher 线程动手)、用 _exit(0) 与 kill -9 的三场景实测进程死了数据都在而真正丢字节的是 stdio 用户态缓冲(fprintf 100 行 _exit 后 0 字节)、给 plain/fsync/fdatasync/O_SYNC 四条路径计时(fsync 与 fdatasync 一个价、O_SYNC 配 4KiB 小块只剩 3 MiB/s,小块加直写的代价不挑机器)、用 fixed/append/touch 三种脏法把 fsync 与 fdatasync 的元数据差测成 touch 场景 4.7 倍稳定信号,外加 dd 默认模式跑完留 262 MB 脏页的旁证,与 rm 扔脏页、_exit 不刷 stdio 两个意外发现"
chapter: 8
order: 3
platform: host
difficulty: intermediate
cpp_standard: [20]
reading_time_minutes: 18
prerequisites:
  - "POSIX 文件 I/O:open/read/write 与 fd 的一生"
  - "mmap 内存映射:把文件贴进地址空间"
related:
  - "OS 资源的 RAII 范式:fd、HANDLE 与映射的同一副骨架"
  - "Win32 文件 I/O:句柄、CreateFileW 与同步读写"
tags:
  - host
  - cpp-modern
  - intermediate
  - 系统编程
  - POSIX
  - 实战
current_status:
  title: 正在打磨
  detail: 本批刚写完,正在通读打磨,表述与实测口径可能随时调整
  variant: polishing
---

# 页缓存与持久性:write() 返回之后发生了什么

上一篇咱们把 512 MiB 的顺序读跑完了,咱们那场对比里,盘从头到尾就没出过场:文件整个躺在了页缓存里,read 与 mmap 争的只是 CPU 这一侧怎么把字节取走。读的这一侧,页缓存是替咱们挡盘的加速器。今天咱们把镜头转向写,同一个页缓存就换了身份:它成了一道会让您误判的缓冲。问题只有一句——write() 返回了,数据在盘上吗?

咱们在 [L01](./01-posix-file-io.md) 已经拿到过答案:man 2 write 的原话很硬,一次成功的 write,不提供任何数据已到盘的保证,您想确认的话,唯一的确认方式就是您调一次 fsync(2)。当时那一句是当作警告写下的,本篇咱们把它变成能动手验证的东西:亲眼看着脏页在 Dirty 计数里滞留三十秒,验一验进程死了数据到底丢不丢,再把 fsync、fdatasync、O_SYNC 三条持久化路径的耗时,一条一条地量出来。路上咱们还能捡到两个意外的发现:rm 会把脏页直接扔掉,_exit 会放过 stdio 的缓冲区,两个都是笔者亲身遇上的。

本篇的实验一共六个,咱们按 E1 到 E6 编号,与仓库 `code/volumn_codes/vol8/systems-programming/linux/file-io/03-page-cache/` 下的 01 到 06 六个目录一一对应,原始的输出也都收在里面,您随时能对表。[L02](./02-mmap-memory-mapping.md) 自己用的小写 e1 到 e5,是它那篇的实验编号,与咱们这里的大写 E 互不相干,您别把两套对混了。实验代码清一色的裸 C 风格,open、write、perror 咱们全裸着写,小程序只为观察页缓存的行为,不让任何封装分了心。落到真实工程里的写法,以及 fsync 的返回值怎么接,E5 末尾咱们拿系列的公共工具补一段。

实验环境咱们一次说清楚,后面的数字,咱们都要拿它对表:台机的 CPU 是 AMD Ryzen 7 9700X、系统是 WSL2,内核用的是 6.18.33.2-microsoft-standard-WSL2,编译器挑的是 g++ 16.2.1,编译的配置一律 `-std=c++20 -O2 -Wall -Wextra -Wpedantic`。两条环境事实直接决定复跑的成败,请您现在就过一眼。头一件事是数据文件的位置,咱们把数据全放在 ext4 上(`/home/charliechen/l03_scratch/` 下的 ext4,复跑前您得把这个目录建出来)。/tmp 是 tmpfs 的地界,它的文件本来就整份活在内存里,写回对它是压根不存在的事,Dirty 的计数会纹丝不动,数据放那儿就白测了,[L02](./02-mmap-memory-mapping.md) 的 msync 实验立过同一条纪律。另一条环境事实是盘本身:WSL2 看到的这块盘,其实是 Windows 侧的一个 VHDX 虚拟盘文件,真正落点是机器里的那块 NVMe,而 256 MiB 的集中刷盘,几十毫秒就被它写完了,所以本篇里 fsync 的若干倍数,咱们都得带着机器的因素念。还有一条不太光彩的诚实交代:笔者的 sudo 要密码,drop_caches(它是 /proc/sys/vm 下能把页缓存整个倒掉的 root 旋钮)和真崩溃演示都做不了,E5 那里咱们降级处理,把能做的部分做扎实。

## E1:只 write 不 sync,256 MiB 脏页滞留三十秒

咱们把 write() 返回那一刻的事,用机制直说一遍:write 只是把数据交给了内核。内核把您缓冲区里的字节,拷进页缓存里的对应页,跟着就把这些页标成了脏,然后就返回了。**脏页**(dirty page)说的就是已修改、还没写到盘上的那类页缓存页,把它们写下去的动作,咱们叫**写回**(writeback),干这个活的是内核线程,咱们跟着叫它 flusher 线程。看它们的仪器,[L02](./02-mmap-memory-mapping.md) 已经用过:/proc/meminfo 的 **Dirty** 字段,记的是全系统当前脏页的总数,单位记的是 kB,旁边的 **Writeback** 字段,数的是正在写回途中的页。这些词后面咱们天天要用,咱们在这里一次说清。

E1 的写数据程序,咱们写得特别干脆:256 MiB、1 MiB 一块、循环 write,循环一结束咱们就 `_exit(0)` 拔腿走人,咱们不 fsync、不 close,什么善后都不做。采样由外层的 e1_run.sh 负责,每 2 秒替咱们读一次那对数:

```cpp
// e1_dirty.cpp(节选):写循环加一句告别,观察交给外层脚本
std::vector<char> buf(static_cast<size_t>(chunk) * 1024);
for (long long off = 0, blk = 0; off < total << 20; off += chunk << 10, ++blk) {
    std::memcpy(buf.data(), &blk, sizeof(blk));   // 块号写在头 8 字节
    long long done = 0;
    while (done < chunk << 10) {
        ssize_t n = write(fd, buf.data() + done, static_cast<size_t>((chunk << 10) - done));
        if (n < 0) { perror("write"); return 1; }
        done += n;
    }
}
std::printf("[e1_dirty] write 循环结束。不 fsync、不 close,直接 _exit(%d) —— 数据生死交给页缓存\n", 0);
std::fflush(stdout);  // 教训:_exit 不刷 stdio 缓冲,不 fflush 这两行 printf 就会无声消失
_exit(0);
```

源码里那行 fflush 的注释,请您多看一眼,它是笔者亲手换来的教训,咱们到 E2 再回头看它。眼前的采样长成这样(存档的头部另有环境与 vm 读数几行,咱们略去):

```text
$ ./e1_run.sh
# 压基线: sync,等 Dirty 落回低位
# 基线 Dirty=112 kB Writeback=408 kB

# 起 writer: 256 MiB,只 write(),随后 _exit(0)
[e1_dirty] path=/home/charliechen/l03_scratch/e1.bin total=256MiB chunk=1024KiB writes=256 write_loop=129.1ms (1983 MiB/s)
[e1_dirty] write 循环结束。不 fsync、不 close,直接 _exit(0) —— 数据生死交给页缓存

# writer 已退出。开始采样(相对秒  Dirty[kB]  Writeback[kB]):
# 0  262144  240
# 2  262208  0
# 4  262300  0
# 6  262472  0
...（8 到 28 秒共 11 行:Dirty 从 262528 缓慢爬到 263188,Writeback 除第 18 秒一帧 4 kB 外全 0,咱们略去）...
# 30  263328  0
# 32  192  0
# 34  300  0
...（36 秒起 Dirty 回到几百 kB 量级的小幅起伏,观察窗共 80 秒,后面略）...
```

数字自己就把算术做完了,262144 kB 恰好就是 256 MiB 的那个数。咱们看着这个数,在采样窗里稳稳挂了三十秒,而写它的人早就 _exit 了。把数据写下去的这件事,从头到尾没有一个用户态的进程参与。

那第三十秒末发生了什么?咱们去 /proc/sys/vm 里看那排旋钮,本机的读数是 `dirty_background_ratio=10 dirty_ratio=20 dirty_expire_centisecs=3000 dirty_writeback_centisecs=500`。后面两个的单位都是百分之一秒,3000 折成的是 30 秒,500 折成的是 5 秒。dirty_expire_centisecs 说的是脏页放多久算老,本机给的数是 30 秒,dirty_writeback_centisecs 说的是 flusher 线程多久醒一次,本机给的数是 5 秒。所以时间线是:脏页满 30 秒算到期,再等 flusher 的下一次醒来(至多 5 秒),过期的那批就被写掉。咱们两轮独立跑,骤降都落在了 30 秒出头,正好落在理论窗口的当中。不过这套旋钮给的是典型行为,时点并不是内核的承诺,细话咱们 E5 再说。

那前三十秒 flusher 为什么一直不管?因为前面还有另一重门槛:dirty_background_ratio=10,说的是脏页总量得超过按内存算下来的一成上下,后台的写回才会启动。本机的 MemTotal 约 53 GiB,一成就是 5 GiB 上下的门槛,咱们这 256 MiB 连零头都够不上。换到小内存的机器上重跑,Dirty 可能中途自己就往下走了,咱们在 [L02](./02-mmap-memory-mapping.md) 的 msync 实验里提醒过同一件事。还有一个小的观察:Dirty 从 262144 慢慢爬到 263328,爬出来的这一千来 kB 不是咱们的,这个计数是全系统的,别的进程写下的脏页也混在里面,所以咱们看量级、看骤降的位置,不抠个位的绝对值。

咱们还嫌 2 秒一帧太粗,回头用 1 秒一帧的密度又跑了一遍(采样脚本照着 e1_run.sh 的样子改,只把间隔换成了 1 秒,变体是没入册的):

```text
$ ./e1_dirty   # 第二轮,采样出自外层脚本的 1s 变体
# 基线 Dirty=0 kB
[e1_dirty] path=/home/charliechen/l03_scratch/e1.bin total=256MiB chunk=1024KiB writes=256 write_loop=145.1ms (1765 MiB/s)
[e1_dirty] write 循环结束。不 fsync、不 close,直接 _exit(0) —— 数据生死交给页缓存
# 28  262972  0
# 29  262988  0
# 30  263012  0
# 31  96  0
# 32  120  0
（0 到 27 秒与首轮同型,Dirty 稳在 262148 到 262932,咱们略去;这轮 46 帧采样里,Writeback 没有一帧大于 0）
```

骤降落在了第 30 到 31 秒之间,Dirty 从 263012 一步掉到了 96。您再看 Writeback 这一栏:四十几帧采样里,Writeback 没有一帧是大于 0 的。256 MiB 的写回,连 1 秒的窗口都没给咱们留,在本机的这块盘上,写回本身快得让咱们抓不住,咱们能抓到的,只有写回前后的两个静态状态。换一块慢盘您再看,会看到另一幅画面:一段 Writeback 大于 0 的平台期,Dirty 一段一段地往下掉。

还有一个意外的发现,它值得咱们单独记一笔。调试脚本的时候咱们遇上了:writer 写完 256 MiB、Dirty 正高着,一条 rm 把文件删了,Dirty 跟着瞬间归了零。想明白以后一点也不神秘——脏页挂在文件的 inode 上(inode,内核里管文件元数据的那个节点,大小、时间戳、盘块的位置都记在它身上),文件没了,这批脏页跟着就没了,内核压根不会去写一个不存在的文件。所以 E1 的观察窗里有条纪律:别动那个文件。E3 的脚本把 `rm -f` 排在计时区间之外,防的就是它。

## E2:kill -9 杀得掉进程,杀不掉页缓存

咱们把镜头收回到 E1 的结尾:数据在页缓存里挂了三十秒,写它的进程一个都不在了。这自然引出那个流传很广的问题:进程死了,数据会丢吗?被 kill -9 呢?空口讨论没有意思,咱们把死亡方式摆成三个场景,一个一个地验过去。场景 A 的做法最直白,子进程 write 完了 44 字节、立刻 `_exit(0)`。场景 B 换的是分段写,子进程循环地写 4 KiB 编号块,写到一半的时候被 kill -9。场景 C 是拿来对照的,子进程拿 fprintf 写了 100 行,收尾的动作同样是 `_exit(0)`:

```cpp
// e2_exit.cpp(节选):场景 B 的子进程,块头 8 字节写编号,其余按块号填充
for (uint64_t i = 0;; ++i) {
    std::memcpy(blk.data(), &i, 8);
    unsigned char fill = static_cast<unsigned char>(0x41 + i % 26);
    std::memset(blk.data() + 8, fill, 4096 - 8);
    ssize_t n = write(fd, blk.data(), blk.size());
    if (n != (ssize_t)blk.size()) _exit(51);
    timespec ts{0, 200 * 1000};   // 每块睡 200µs 压速度,让 kill 落在写到一半的途中
    nanosleep(&ts, nullptr);
}

// 场景 C1:100 行 x 24 字节,小于 stdio 的 4096 缓冲,全攒在用户态
for (int i = 0; i < 100; ++i) fprintf(fp, "STDIO_BUFFERED_LINE_%03d\n", i);
_exit(0);   // 不 fclose、不 fflush;C2 只多一行 fflush(fp),其余一字不差
```

父进程负责的就是 kill、收尸、读回校验,咱们拿到的输出一共就这么几行:

```text
$ ./e2_exit
== 场景 A: write() 后立刻 _exit(0),不 fsync ==
  子进程 A 已退出 (waitpid status=0x0)。父进程现在 read 同一文件:
  read() 返回 44 字节,内容: "WRITTEN_VIA_WRITE_SYSCALL_NO_FSYNC_THEN_EXIT"
  判定: 可见 —— write() 返回时数据已在页缓存(内核),进程死不死它都在

== 场景 B: 写到一半 kill -9 ==
  子进程被 kill -9 (waitpid status=0x9, SIGKILL=9)。父进程读回校验:
  文件大小: 1728512 字节 (422 个完整 4KiB 块, 尾部零头 0 字节)
  校验: 422/422 个完整块编号与填充模式全部完好, 序号 0..421 连续
  判定: kill -9 杀掉的是进程;凡是 write() 已经返回的数据,一个字节都没丢

== 场景 C(对照): stdio 用户态缓冲,同样 _exit(0) ==
  C1 fprintf 后 _exit(0): 文件大小 0 字节 (100 行 x 24 字节本应 2400)
  C2 fprintf + fflush 后 _exit(0): 文件大小 2400 字节
  判定: 丢数据的另一层是 stdio 用户态缓冲;write() 返回后那一层已经不存在了
```

咱们一个场景一个场景地看。场景 A 的子进程连 close 都没做,父进程照样原样读回了那 44 个字节。场景 B 的现场更狠,被 kill -9 打断的时候,文件里躺着的还是 422 个完整块,编号 0 到 421 都是连续的,咱们把块内填充模式逐字节核对了一遍,没有一处是撕裂的。咱们把两个场景合起来,用机制直说就是:页缓存是内核的东西,不是进程的东西。而 write() 一返回,字节就住进了内核的页缓存,进程的生死从此与它无关,SIGKILL 能终结的只有进程自己。

可场景 C 的结果正好反过来。同样的一套子进程加 `_exit(0)`,fprintf 写了 100 行、每行 24 字节,读回来的却是 0 字节,加了一行 fflush,2400 字节就整整齐齐地在文件里了。丢数据的不是页缓存,是 stdio 的用户态缓冲:`FILE*` 里那块缓冲区活在您进程自己的内存里,fprintf 做的只是往里攒,攒够了、或者被 flush 了,才真正地调一次 write(2) 进内核。`_exit()` 是裸的系统调用,它不运行 atexit 注册的函数,也不替咱们刷任何 stdio 流,刷流的活是归 `exit(3)` 管的。攒在用户态的字节,是跟着进程一起蒸发的。

笔者在这件事上有发言权:E1 程序的初版,拿 printf 打了两行就直接 `_exit(0)`,跑完了您再看,输出里那两行无声无息地没了,实验代码自己走进了场景 C 的现场。修法您在 E1 的代码节选里已经见过,_exit 之前老老实实地 fflush。这也是为什么 E2 验证 write 语义的两个子进程,咱们一律不写 stdio,输出走的全是 write。

而在 write() 返回之后,到底什么样的死法才会丢数据?咱们把候选者过一遍:进程的正常退出、kill -9、还有 close,谁都动不了已经写下的字节(close 连自己的返回值都不值得看——RAII 篇讲过的,数据的事情它更管不着)。真正会丢的只有一种:内核还没来得及写回,机器没了——掉电、内核 panic,或者 WSL2 的场合,承载它的那层 Windows 整个死掉。这个窗口的典型长度,E1 已经替咱们量到了:三十秒上下。把它关到零的手段,就是 E3 的主角。

## E3:plain、fsync、fdatasync、O_SYNC,一个一个计时

把数据真正送到盘上的手段,咱们把三条路都请来,外加一条不持久的对照组。plain 的路子是 write 完就不管,fsync 的路子是循环跑完补一次 fsync(计时含它),fdatasync 是同型的做法,只是换成了 fdatasync,O_SYNC 的做法则是 open 时带上 flag,每次的 write 都同步写到盘上。一轮的总量是 256 MiB,单次写的是 1 MiB,每轮开跑前咱们把 Dirty 压回低位,咱们跑三轮,取各自的中位数:

```cpp
// e3_bench.cpp(节选):计时含 sync 调用本身
long long t0 = now_ns();
for (long long off = 0; off < total; off += chunk) {
    long long done = 0;
    while (done < chunk) {
        ssize_t n = write(fd, buf.data() + done, static_cast<size_t>(chunk - done));
        if (n < 0) { perror("write"); return 1; }
        done += n;
    }
}
if (do_fsync && fsync(fd) != 0) { perror("fsync"); return 1; }
if (do_fdatasync && fdatasync(fd) != 0) { perror("fdatasync"); return 1; }
long long t1 = now_ns();   // O_SYNC 模式则全程都在等盘
```

```text
mode              r1_ms        r2_ms        r3_ms      median_ms median_MiB_s
plain             129.9         40.4         38.8           40.4         6337
fsync              60.6         60.0         61.4           60.6         4224
fdatasync          60.3         59.3         59.6           59.6         4295
osync             301.6        284.4        318.6          301.6          849
osync_4k        22387.2      15162.0      20871.5        20871.5            3
```

osync_4k 是咱们附加的一组:O_SYNC 配 4 KiB 小块、总量 64 MiB,其余的口径相同。

头一个值得看的是 plain 的第 1 轮,它跑了 129.9 ms,后面的两轮只有 40 上下。这个冷启动价您已经见过同款了,E1 的 write_loop 是 129.1 ms,E6 里 dd 的第 1 轮也落在 2 GB/s 一档,三处的数字对上了,说的是同一件事:页缓存头一回分页、做各种登记,都是要额外花钱的。稳态的数,咱们看第 2、3 轮。

fsync 与 fdatasync 这一对的成绩,是 60.6 对 59.6 的差距,差别在毫秒的边缘上。顺序写一大份文件、收尾集中刷一次的场合,两者的价基本一样。那它们比 plain 慢吗?在本机上也就慢了 1.5 倍。这个数字请您务必带着机器的底细念:这块盘背后是 NVMe 上的 VHDX,一次 256 MiB 的集中刷盘,几十毫秒就被它写完了。换一块机械盘、或者关掉了盘上写缓存的 SSD 再跑,大出来的倍数就有好几圈,您手里的机器值多少,拿 e3_run.sh 跑一遍您就知道。真正的重头在表的最后一行。

O_SYNC 的字面意思,是每次的 write 都要等数据、等元数据,真正落到硬件了才返回。man 2 open 把它写成 write 之后跟一次 fsync 的语义,还有个只保数据的兄弟 O_DSYNC,它的对应物是 fdatasync,Linux 从 2.6.33 起才把这俩真正地分开。256 MiB、1 MiB 块的 O_SYNC 用了 301.6 ms,是 plain 稳态的七倍,可这还不是最惨的。咱们把块缩到 4 KiB、总量缩到 64 MiB:20871.5 ms,吞吐掉到了 3 MiB/s。咱们拿 64 MiB 除以 4 KiB,得到的是一万六千三百八十四次 write,每次都付一趟完整的写盘往返,摊下来的单次开销约 1.3 毫秒。掉到 3 MiB/s 的原因不在 O_SYNC 本身,在 O_SYNC 配小块:这正是数据库每个事务都要持久化的真实处境,也是为什么人家都要攒日志、成组刷盘。而小块加直写的代价不挑机器,3 MiB/s 这个深度则绑着本机约 1.3 毫秒的往返延迟,您复跑时按自家的盘重算就好。

所以持久化的开销,得按每次 sync 的延迟乘次数来算,跟总量的关系反而不那么大。E4 马上把每次的延迟量给您,这里咱们按总量再对一遍:同样是 256 MiB 要送到盘上,plain 是把写盘推迟到了三十秒之后,由内核替咱们代办,fsync 当场就付清了,多花的也就约 20 ms。O_SYNC 配小块的那一路,是把一次大传输切成了一万六千次同步小传输,每次都得等一趟完整的写盘往返,落成的对比是 849 MiB/s 对 3 MiB/s,差了两百多倍。

> 另一侧的名字咱们也挂个号:Windows 那边与 fsync 对应的,是叫 FlushFileBuffers 的那个调用,W02 讲文件映射的 msync 镜像时已经带过它,细节您到 [W02 的文件映射一篇](../../windows/file-io/02-file-mapping.md)里看。对应 O_SYNC 的写穿机制,Windows 侧也是有的,叫的就是 `FILE_FLAG_WRITE_THROUGH`,细节您到 [W01 的补课实验](../../windows/file-io/01-win32-file-io.md)里看。

## E4:fsync 与 fdatasync 差在哪——三种脏法

man 2 fsync 的说法,咱们捡要紧的两句:fsync 把文件的数据和元数据一起冲下去,一直阻塞到设备报告了传输完成。fdatasync 求的只是数据写到盘上,元数据里只有影响后续读取正确性的那部分必须跟着走:文件大小的变化必须刷,访问时间、修改时间的变化可以不刷。而在 ext4 上,元数据要过文件系统的日志(journal)才能写到盘上,所以省下的这一截,省的是一次日志提交的往返。

空对空地背这两句没有意思,咱们把文件弄脏的方式构造成三种,专门让两种 sync 的差别现形。fixed 的做法是提前扩到 1 MiB 并落稳,每轮往固定的偏移 pwrite 4 KiB,脏的只有数据页。append 的做法是每轮往文件尾 pwrite 4 KiB,数据页和大小就一起脏了。touch 的做法是一个字节都不写,每轮拿 utimensat 改的就是时间戳,脏的只有元数据。每种脏法咱们各配两种 sync,每轮固定地跑 200 次,咱们只给 sync 那一下计时:

```cpp
// e4_metadata.cpp(节选):三种脏法,只给 sync 计时
for (long long i = 0; i < iters; ++i) {
    if (!strcmp(scen, "fixed")) {
        pwrite(fd, blk.data(), blk.size(), 4096);        // 固定偏移,大小不变
    } else if (!strcmp(scen, "append")) {
        off_t end = lseek(fd, 0, SEEK_END);
        pwrite(fd, blk.data(), blk.size(), end);         // 文件尾,大小在涨
    } else {                                             // touch:只改时间戳,不写数据
        timespec now[2]{};
        clock_gettime(CLOCK_REALTIME, &now[0]);
        now[1] = now[0];
        utimensat(AT_FDCWD, path, now, 0);
    }
    long long t0 = now_ns();
    if (datasync ? fdatasync(fd) : fsync(fd)) { perror("sync"); return 1; }
    per_op_ns.push_back(now_ns() - t0);
}
```

| 场景 | sync | 三轮原始值(µs/次) | 中位数 |
|---|---|---|---|
| fixed(只脏数据) | fsync | 257.0 / 248.5 / 250.2 | 250.2 |
| fixed | fdatasync | 246.2 / 246.0 / 246.2 | 246.2 |
| append(数据加大小) | fsync | 1013.8 / 1302.3 / 1391.5 | 1302.3 |
| append | fdatasync | 1356.6 / 863.6 / 1023.9 | 1023.9 |
| touch(只脏元数据) | fsync | 746.2 / 738.4 / 844.0 | 746.2 |
| touch | fdatasync | 156.9 / 159.6 / 161.2 | 159.6 |

fixed 场景量出来的差别,只有 250.2 对 246.2 的差,4 µs 的差完全泡在噪声里。其实这不奇怪,咱们看脏的是什么:只有那 4 KiB 的数据页,时间戳那点元数据上的差别,在这次的测量里根本抬不起头,两种 sync 干的活几乎一模一样。

append 场景的中位比是 1302 对 1024,看着像 fdatasync 占了便宜,咱们把三轮原始值摆开再看:fsync 的三轮是 1013.8、1302.3、1391.5,fdatasync 的三轮是 1356.6、863.6、1023.9,两组的区间整个叠在一起,谁也没露出稳定的样子。这里咱们如实记:差异在噪声内,测不出稳定的差。倒是两边的绝对身价都涨到了 1 ms 一档——文件每长一截,大小的元数据就必须跟着写到盘上,这活是两种 sync 都躲不掉的。追加式的日志每条刷一次,付的就是这个价,append 场景真正贵的也正是这一项。

touch 场景才是把差别稳定拉开的那一组,fsync 的三轮是 746.2、738.4、844.0,fdatasync 的三轮是 156.9、159.6、161.2,逐轮的比值在 4.6 到 5.2 之间,中位数落在了 4.7。咱们三轮看下来,每一轮都稳稳地差着四五倍,不像同一台机器上量出来的抖动。机制也对得上:touch 只把 inode 的时间戳弄脏了,fsync 的代价,是要把这枚 inode 走日志刷下去的,fdatasync 一看没有会影响读取的元数据,查一圈就可以交差了,那 160 µs 基本是它查圈的功夫。

这组数字对写代码的咱们意味着什么?多数程序要的东西很朴素:数据不能丢,时间戳晚一点也就无所谓了。这类场合里 fdatasync 就是白捡的便宜,越写得频繁、每次的数据量越小,捡回来的也就越多。真要元数据也严格同步的场合(比如审计的要求),那就得请 fsync 来伺候了。还有一条配套的做法,新建文件的时候用得上:文件得作为一个条目存在于目录里,光对文件 fsync 是不够的,还得拿着目录的 fd 再 fsync 一次,这一步的讲究,L01 是讲过的,咱们不重讲。

## E5:崩溃窗口,咱们能演示到哪一步

咱们写到这里,最想做的实验其实是崩溃本身:写一份数据、不 sync,接着咱们把电源一拔,看活下来的文件缺多少。本机上咱们演不了,原因倒是只有两条,都说给您。一是 WSL2 里的拔电源,等价于让外面那层的 Windows 整个死机,笔者找不到安全的办法制造它,更不该拿咱们的工作机去赌。二是想做观察上的近似也得用 root,/proc/sys/vm/drop_caches 能把整个页缓存都扔掉、逼后续的读取走盘,它的门槛就是 root,而笔者的 sudo 是要密码的,实验脚本拿 `sudo -n true` 探了一下,就败下阵来了。E5 的输出里,把它如实记了下来:

```text
$ ./e5_run.sh
== 步骤 0: sudo / drop_caches 可用性 ==
# sudo -n true: 不可用 —— 提示: sudo: a password is required
（脚本接下来两行是它自己的文字说明,交代 drop_caches 需要 root、真崩溃无法安全演示,咱们略去）
== 步骤 1: sync 压基线 ==
# 基线 Dirty=108 kB

== 步骤 2: dd 写 64MiB,不 sync,立刻观察 ==
#   67108864 bytes (67 MB, 64 MiB) copied, 0.0379661 s, 1.8 GB/s
# dd 返回后: Dirty=65644 kB(≈64MiB 脏页滞留), 文件大小 67108864 字节, 前 8 字节 0000000000000000
#            —— 文件此刻"完全可读",但这 64MiB 只在内存里,盘上一个字节都还没有

== 步骤 3: sync 之后 ==
# sync 返回后: Dirty=72 kB, 文件大小 67108864 字节, 前 8 字节 0000000000000000
#            —— sync 返回 = 内核已把这批脏页写回存储;从这一刻起,断电才不丢
```

这几行连起来读:dd 花 0.038 秒就写完了 64 MiB,Dirty 涨到了 65644 kB,折出来正好是 64 MiB 的量。文件此刻是完全可读的,大小也是分毫不差的,但盘上的字节一个都还没有。而 sync 一返回,Dirty 就落回到了 72 kB,从返回的这一刻起,断电也就不丢了。窗口典型有多长?E1 量过了:三十秒。三十秒其实不是承诺,是默认旋钮的口径,真到了内存吃紧的时候,内核还有 dirty_ratio=20 的另一重防线,脏页堆到约 10 GiB 的量级(本机的内存量),写数据的进程会被内核暂停,一直等到写回追上来了才放行。您要是读到别处说“数据最多三十秒后一定在盘上”,请帮笔者在心里划掉那个“一定”。

咱们走到真实工程里,还剩最后一件事:fsync 自己的返回值。写回失败了,盘满了、硬件报错了都会走到这一步,fsync 拿回来的就是 -1,errno 里常见的是 EIO。man 2 fsync 的 ERRORS 写明,同步过程中出了错就报它,而且这个错可能来自写到同一文件上的任何一个 fd。数据是不是真的到了盘上,这一趟是您最后的提问机会,问完了才能安心。咱们系列的写法:

```cpp
sys_call("fsync", ::fsync, fd.get());   // 失败抛 system_error,what 带 "fsync:" 前缀
```

fd 咱们交给 unique_fd 管着,失败的路径也照样 close,这套写法的形态,是 [RAII 篇](../../thinking/01-raii-paradigm.md)和[错误处理篇](../../thinking/02-error-paradigm.md)都已经定义过的,咱们本篇只引用。man 2 close 的一句告诫也相关,RAII 篇完整讲过:close 的返回值不查,I/O 的错误可能无声滑过去。您真在乎数据,该问的是 fsync,而不是 close——这正是 RAII 篇的底气:析构里 close 的返回值敢丢弃,fsync 则留给了调用方去问。

## E6:dd 的旁证与工程上的取舍

最后咱们请 dd 出场,看它在三种模式下的开销分别落在哪儿:默认模式走页缓存,`oflag=direct` 绕过页缓存直写(L01 提过的那个 O_DIRECT 亲戚,它带着一串的对齐要求),`conv=fsync` 的做法是跑完补一次 fsync。一轮的总量是 256 MiB,咱们看三轮:

```text
$ ./e6_run.sh 256
# 口径: dd if=/dev/zero of=... bs=1M count=256,3 轮;吞吐取 dd 自报值

# round 1 buffered: pre_dirty=448kB post_dirty=262296kB dd_report=[1.8 GB/s]
# round 1 direct: pre_dirty=4kB post_dirty=4kB dd_report=[5.9 GB/s]
# round 1 fsync_only: pre_dirty=4kB post_dirty=248kB dd_report=[3.9 GB/s]
...（第 2、3 轮同型:buffered 吞吐 5.9、6.4 GB/s,跑完脏页 262408、262248 kB;
     direct 吞吐 5.1、5.7 GB/s,脏页 176、156 kB;fsync_only 吞吐 4.5、4.5 GB/s,
     脏页 104、100 kB;每轮另有一行 dd 原始报告,咱们略去）...
```

咱们把数字摆开,道理也就清楚了。默认模式的 dd 报 5.9 到 6.4 GB/s(第 1 轮的 1.8 GB/s 是冷启动价,咱们只看后两轮的稳态),跑完的瞬间 Dirty 里躺着 262 MB,dd 报的“盘速”其实是内存的速度,真正的写盘欠着,三十秒后就被内核补上了,根本轮不到 dd 操心了。direct 模式三轮落在 5.1 到 5.9 GB/s(第 1 轮的 5.9 就摆在摘录里),跑完的 Dirty 与基线齐平,每个字节真的走了盘。conv=fsync 的成绩居中,稳在 3.9 到 4.5 GB/s 的区间,fsync 那一趟的耗时,算进了 dd 自己报告的数字里。本机上 direct 没吃吞吐的亏,靠的还是 VHDX 的底子,慢盘上的 direct 常常明显更慢,但默认模式跑完留下的 262 MB 脏页,却是不挑机器的。您顺手也就看明白了:平时随手跑的 dd 测速、cp 大文件“瞬间完成”,咱们量到的多半是页缓存,而不是真正的盘。

那到了工程上,咱们什么时候该 sync、按什么粒度?咱们把本篇量到的东西,换成几条能落地的判断。要事务级持久化的,数据库的 WAL(write-ahead log、预写日志)、订单的落库都算,每笔事务跟一次 fdatasync、append 场景那 1 ms 一档就是单线程吞吐的天花板,所以数据库拼命做组提交,把很多笔事务攒成了一次刷盘。比如配置文件这类改完最好别丢的,改完了补一次 fsync,目录的那一次也补上,几百微秒的量级,您别犹豫。日志、缓存这类丢了能重建的,plain 走起:三十秒的窗口对着重建的成本,多数的场合里重建更便宜。最怕的是卡在中间的状态,每次 write 都想要同步、又用着小块的写法,E3 那个 3 MiB/s 就是这么来的。咱们要么把小块攒成大块再 sync,要么干脆连同步都省了,别让您的程序停在这档配置上。

最后停在一个真实的地方。您现在回头把 E2 的场景 A 再跑一遍:write 进去的 44 个字节,您立刻读是读得到的,等进程死了再读,它还好好地在那儿。咱们等上半分钟,内核的 flusher 会把它写下去,这件事 E1 已经量给您看了,从头到尾没有一个调过 sync 的进程。write() 返回之后发生了什么?本篇六个实验给出的现场是:数据进了页缓存,Dirty 涨了,进程的死活与它无关,内核按自己的钟点写回,您想知道确切的时点,fsync 给您一个能问出答案的地方。下一篇咱们回到库的这一层,把 std::filesystem 的目录与元数据接着讲。

<ReferenceCard title="参考资源">
  <ReferenceItem
    :id="1"
    title="fsync(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/fsync.2.html"
  />
  <ReferenceItem
    :id="2"
    title="open(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/open.2.html"
  />
  <ReferenceItem
    :id="3"
    title="write(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/write.2.html"
  />
  <ReferenceItem
    :id="4"
    title="_exit(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/_exit.2.html"
  />
  <ReferenceItem
    :id="5"
    title="exit(3)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man3/exit.3.html"
  />
  <ReferenceItem
    :id="6"
    title="proc_meminfo(5)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man5/proc_meminfo.5.html"
  />
  <ReferenceItem
    :id="7"
    title="Documentation/admin-guide/sysctl/vm.rst"
    publisher="The Linux kernel documentation"
    url="https://docs.kernel.org/admin-guide/sysctl/vm.html"
  />
  <ReferenceItem
    :id="8"
    author="Michael Kerrisk"
    title="The Linux Programming Interface"
    publisher="No Starch Press"
    :year="2010"
    url="https://man7.org/tlpi/"
  />
</ReferenceCard>
