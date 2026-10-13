# 01-termios-raw 配套实验

《termios 与 raw 模式》(`documents/vol8-domains/systems-programming/linux/terminal/`,文章待写)的实验代码与原始输出存档。`.out` 是 2026-10-05 当轮机器的原始捕获。

**安全口径:全部实验跑在自建的 pty 里(父进程 openpty 开台、孩子在 slave 上读),主终端一个字节都不动。** termios 是内核里 tty 的属性,不随进程走,真把主终端改坏就得靠 `stty sane` 救场——本篇从装置上杜绝了这种事故,这套"自建 pty 当实验台"的架子本身就是文章要教的东西。

## 环境

| 项 | 值 |
|---|---|
| 系统 | WSL2,内核 6.18.33.2-microsoft-standard-WSL2 |
| 编译器 | g++ (GCC) 16.2.1,glibc 2.44 |
| 编译 | `g++ -std=c++20 -Wall -Wextra -Wpedantic -O2 xxx.cpp -o xxx`(全部 0 warning) |
| 台架 | 实验进程自身无 tty(harness 的 stdio 是管道),凡要终端处一律自建 pty |

## 目录与实验对照

| 文件 | 实验 | 一句话结论 |
|---|---|---|
| `e1_flags.cpp` | E1 | 新开 pty 出厂=canonical 全家(ISIG/ICANON/ECHO/IEXTEN,ICRNL+IXON,OPOST+ONLCR,B38400+CS8+CREAD);cfmakeraw 清 ICRNL/IXON/OPOST/ISIG/ICANON/ECHO/IEXTEN 但留着 ONLCR(OPOST 一关它就失效);只清 ICANON|ECHO 的"最小 raw"与 cfmakeraw 差 ISIG/IXON/ICRNL/OPOST 四处,后续实验逐个兑现成行为 |
| `e2_canon_raw.cpp` | E2 | canonical 下 read 干等到换行才一次交货(喂到 600ms 才读到 5 字节);DEL 的行内编辑发生在 tty 层,读端只拿到编辑后的行;^U 整行抹掉;raw(VMIN=1)喂几块到几块 |
| `e3_special.cpp` | E3 | ^C 无前台组时照样冲掉排队输入但信号没有收件人(SIGINT=0),setsid+TIOCSCTTY+tcsetpgrp 三步之后 SIGINT=1——冲队与递信号是两件事;^D 交出未换行的行、再一个 ^D 返回 0,且 tty 的 EOF 不粘,后面还能接着读;raw 下 0x03/0x04/0x1c 全是普通字节 |
| `e4_vmin_vtime.cpp` | E4 | MIN=1 逐字节;MIN=4 凑够才交;MIN=0+TIME=5 空读 0.5s 返回 0、有货立刻拿;MIN=4+TIME=5 的字符间计时器**每收一字节重置**(200ms 间隔连喂 4 字节拿到 4 字节@a~600ms;450ms 间隔喂 2 字节断供拿到 2 字节@a~950ms——若只从第一字节起算应是 3@500ms/1@500ms,folklore 两种说法被实测裁掉一种) |
| `e5_echo_opost.cpp` | E5 | ECHO 开:喂 "hi\n" master 收到回显 "hi\r\n"(回显也过 ONLCR);ECHO 关:master 无痕、读端照收(密码不回显的机制);OPOST:孩子写 "A\nB" master 收 "A\r\nB",清掉后原样 "A\nB";ICRNL:进门方向 CR→NL,关掉则原样(此时 CR 不是行结束符,要 ^D 补一刀才交货) |
| `e6_guard.cpp` | E6 | 无 guard:孩子改 raw 后 `_exit`,lflag 原样滞留,接力的孩子什么都没设却继承 raw——设置挂在内核 tty 上;guard 正常返回/中途 throw 都在析构里还原;TCSAFLUSH 收回时冲掉滞留输入("junk" 没了),TCSANOW 保留("junk" 混进下一行,而且不用行结束符就交了货——raw 期间收到的字节切回 canonical 后一次 read 直接拿走) |

## E1 顺带的三个本机事实(文章引用时注明口径)

- **master 端也是 tty**:`isatty(master)=1`,tcgetattr/tcsetattr 经 master 都能用,且与 slave 同步(两端共享同一份行规程)。惯常说"master 不是 tty"的说法在本内核不成立(部分 BSD 实现确实不是,写文章时按"Linux 实测如此"表述)。
- **波特率的两种记法**:本机 glibc 2.44 的 B 常量是真实速率值(B9600 宏==9600,glibc 2.42 起的新口径),内核 c_cflag 里存的仍是老编码(0o15/0o17),cfget*/cfset* 两头翻译,round-trip 实测一致;初始 c_cflag=0x000f00bf,低 4 位 0xf 是输出波特率 B38400,0xf0000 是输入波特率的镜像(老编码<<16)。pty 上这些位无物理意义。
- **出厂 VMIN=1 VTIME=0**:新开 pty 的 c_cc 默认,不是 4/0。

## 复现

```sh
cd code/volumn_codes/vol8/systems-programming/linux/terminal/01-termios-raw
for e in e1_flags e2_canon_raw e3_special e4_vmin_vtime e5_echo_opost e6_guard; do
  g++ -std=c++20 -Wall -Wextra -Wpedantic -O2 $e.cpp -o $e && ./$e
done
```

e2/e3/e4 每阶段 0.2-0.75 秒的节奏是刻意的(时刻表就长在代码里),全程约 10 秒。
