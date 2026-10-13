# 02-pty-recording 配套实验

《伪终端(PTY)与进程交互》(`documents/vol8-domains/systems-programming/linux/terminal/`,文章待写)的实验代码与原始输出存档。`.out` 是 2026-10-05 当轮机器的原始捕获,`e5_player.out` 里 `$` 开头的行是当时敲的命令。

与前篇(01-termios-raw)的分工:termios 的标志位怎么改、read 什么时候交货,是上一篇的事;本篇管 pty 这对设备本身——怎么开、谁在上面干活、怎么拿它录一场会话再放回去。

与 daemon 篇(`linux/process/02-daemon` E3)的分工:那篇已实测过 `close(master)` 之后前台组各收一条 SIGHUP、nohup 的 strace 四件事、TIOCGSID 的判据,本篇**不重跑**,只在 E6 的读端错误码处与它互为镜像。

## 环境

| 项 | 值 |
|---|---|
| 系统 | WSL2,内核 6.18.33.2-microsoft-standard-WSL2 |
| devpts | `rw,nosuid,noexec,noatime,gid=5,mode=620,ptmxmode=000` |
| 编译器 | g++ (GCC) 16.2.1 |
| 编译 | `g++ -std=c++20 -Wall -Wextra -Wpedantic -O2 xxx.cpp -o xxx`(全部 0 warning) |
| 附加 | `/dev/ptmx` 主次设备号 5:2 mode 0666,`/bin/sh` 是 sh-5.3 |

e1 源码顶上的 `#define _XOPEN_SOURCE 600` 是给 C 严格模式留的保险:man 3 posix_openpt 的特性宏要求是 ≥600(grantpt/unlockpt/ptsname 一家是 500),严格 C 下无宏与 500 都报 implicit declaration、600 才干净(2026-10-05 最小文件三档复核)。本篇成稿口径是 g++,g++ 在 Linux 上默认预定义 `_GNU_SOURCE`,删掉这句 define 按同口径编译零警告、机制输出不变(同日复核),故保留原捕获源码不重录。

## 目录与实验对照

| 文件 | 实验 | 一句话结论 |
|---|---|---|
| `e1_four_steps.cpp` | E1 | 四步手搓:posix_openpt 拿 master(节点此刻已在 /dev/pts 出现);unlockpt 之前 open slave 报 EIO;grantpt 后节点 uid=1000 gid=5 mode=0620;open+isatty+ttyname 通;close(master) 后节点即刻消失,slave 读到 EOF(0)、写报 EIO |
| `e2_forkpty.cpp` | E2 | forkpty 一个调用=开对+fork+孩子 setsid+TIOCSCTTY(控制终端)+0/1/2 接管:孩子 pid==sid、TIOCGSID 成功、前台组=自己;父写 "ping\n" 孩子读到手,master 流里 "ping\r\npong\r\n" 混在一起分不出方向 |
| `e3_pipe_vs_pty.cpp` | E3 | 同一读程序同 6 字节 "abc DEL z \n":管道里原样 6 字节到手;pty(canonical+ECHO)里到手编辑后的 "abz\n",master 另收到回显(DEL 回显为 `^H space ^H`);关 ECHO 后编辑照做、回显消失——行编辑与回显都在 tty 层,不在读程序里 |
| `e4_recorder.cpp` | E4 | script(1) 最小复刻:forkpty+exec `sh -i`,父进程当终端仿真器,按时刻表喂键、全量录 master 读到的字节。产物 `session.typescript`(138 字节,开头是 `ESC[?2004h` 括号粘贴模式+提示符+回显+输出)与 `session.timing`(每笔一行的时刻表);会话收场=exit 后 read(master) 报 EIO |
| `e5_player.cpp` | E5 | 回放:按 timing 逐笔睡逐笔写,1.00 倍速全程 1257.1ms 对录制 1255.8ms(差 1.3ms),`cmp` 回放产物与 typescript 逐字节一致;2.00 倍速 629.0ms |
| `e6_winsize_eio.cpp` | E6 | 新开 pty 的 winsize 是 0x0(终端仿真器必须主动 TIOCSWINSZ);改一次尺寸前台组收一次 SIGWINCH(计数 1→2 对两次 ioctl);孩子退场(slave 全关)后 read(master)=EIO,但 write(master) 照样成功,而且刚写的字节被行规程回显回来、再 read 能收到——**EIO 的判据是"队列空+对端关",不是"对端关";写路径不查这个状态** |

## E6 的读端/写端不对称(下结论前用来复核的数据)

单笔 2048 字节连写 5 次、间隔 200ms 再读:10/10 读到回显;间隔 0ms 立即读:读到的是正在回显途中的部分(852/254 字节),赶在回显进队之前的 read 报 EIO。所以"master 端发现对端没了"的可靠信号是 read 的 EIO;write 的成功什么都证明不了。

## 复现

```sh
cd code/volumn_codes/vol8/systems-programming/linux/terminal/02-pty-recording
for e in e1_four_steps e2_forkpty e3_pipe_vs_pty e4_recorder e5_player e6_winsize_eio; do
  g++ -std=c++20 -Wall -Wextra -Wpedantic -O2 $e.cpp -o $e
done
./e1_four_steps && ./e2_forkpty && ./e3_pipe_vs_pty
./e4_recorder            # 生成 session.typescript / session.timing
./e5_player session.timing session.typescript > replay.out && cmp replay.out session.typescript
./e6_winsize_eio
```

e3/e4/e5 都在自建 pty 上跑,不碰当前终端;e4 会 exec 交互式 sh,输入日程写在代码里,无人值守。
