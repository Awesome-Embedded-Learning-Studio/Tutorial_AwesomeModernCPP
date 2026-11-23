---
title: "Pseudoterminals and Process Interaction: Terminal Recording and Playback"
description: "What the pty (pseudoterminal) master and slave device pair is, and why script(1) and sshd both depend on it: the four hand-rolled steps posix_openpt/grantpt/unlockpt/ptsname measured live (/dev/ptmx 5:2 matching the man page to the letter, the slave node already present in /dev/pts with ownership settled at the very moment posix_openpt returns, grantpt being a formality on this machine, open on the slave before unlockpt returning EIO rather than EACCES, the node vanishing the instant close(master) with the slave reading EOF and write returning EIO), a forkpty one-stop child-side acceptance check (pid==sid, TIOCGSID confirming the controlling terminal, isatty 0/1/2 all 1, the foreground group being itself, and echo mixed with output in a single master stream with no direction to tell apart), the same six bytes abc DEL z fed to a pipe and to a pty for a byte-identical comparison (the pipe hands over all six bytes untouched, the pty's reading end receives the edited abz newline while the master separately collects the 08 20 08 DEL echo, and turning ECHO off keeps the editing while the echo vanishes — an architecture lesson assigning line editing and echo to the tty layer), a minimal script(1) re-implementation (forkpty plus exec sh -i, a 138-byte typescript opening with sh 5.3's bracketed-paste ESC[?2004h, the single exit input appearing three times in the stream and attributed layer by layer via the timing log, and read(master) returning EIO as the closing act), playback on the timing schedule (1257.1ms wall clock at 1.00x against a recorded 1255.8ms total with cmp byte-identical, 629.0ms at 2.00x), a fresh pty's winsize 0x0 and TIOCSWINSZ driving the SIGWINCH count from 1 to 2, plus the exclusive criterion for the master's read/write asymmetry (after the slave closes all fds, write(master) succeeds and the bytes get echoed by the line discipline for a follow-up read to receive; EIO requires both an empty queue and a closed peer — a re-check matrix of five consecutive 2048-byte writes at 200ms intervals reading 10/10 echoes versus 0ms intervals reading 852/254 bytes or EIO; write's success proves nothing), mirroring the daemon article's SIGHUP after close(master)"
chapter: 8
order: 2
platform: host
difficulty: intermediate
cpp_standard: [20]
reading_time_minutes: 22
prerequisites:
  - "termios 与 raw 模式:终端这层在替您做什么"
  - "守护进程、会话与环境:setsid、双 fork、rlimit 与 environ"
  - "时钟源与 POSIX 时间 API:机器里的九把钟"
related:
  - "termios 与 raw 模式:终端这层在替您做什么"
  - "守护进程、会话与环境:setsid、双 fork、rlimit 与 environ"
  - "时钟源与 POSIX 时间 API:机器里的九把钟"
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
translation:
  source: documents/vol8-domains/systems-programming/linux/terminal/02-pty-recording.md
  source_hash: 562181604de279edf709ca0b4f3e0b16c0164931ed047c0d7e3b5f536f118346
  translated_at: '2026-11-22T15:43:58+00:00'
  engine: glm-5.3
  token_count: 17529
---
# Pseudoterminals and Process Interaction: Terminal Recording and Playback

In [the previous article](./01-termios-raw.md) we flipped every switch of the line discipline, but the rig that stood through the whole show never got a formal introduction: what exactly is that pair of devices openpty produces at every stage? The [daemon article](../process/02-daemon.md) merely borrowed it as apparatus twice — once inside script's shell for daemonization, once opening its own rig with openpty to stage a terminal's death — with the mechanics left unfolded, packing up as soon as the run ended. This article moves it to center stage. Its name is the pseudoterminal (abbreviated pty), and it refers to a pair of devices bundled together: our side holds the master, the program holds the slave end, and the thing sandwiched in between you have already met — it is the previous article's protagonist: the line discipline.

It solves a very concrete class of problems. You want script to record a session, ssh to log into a remote machine, xterm to paint a window — every party in these scenes needs a terminal, yet a real one is nowhere at hand: script must make the shell inside believe it is on a terminal, and ssh must make the remote shell treat your local window as its terminal. Pipes can carry bytes, yes, but they cannot counterfeit a terminal: line discipline, echo, line editing, window size — pipes provide none of these, and one question from isatty gives the game away. The pty fills exactly this gap, and man 4 pts states the promise plainly: while both master and slave are open, the interface the slave offers the process is indistinguishable from a real terminal.

We start from the four hand-rolled steps at the very bottom and walk all the way to recording a session on script(1)'s skeleton and playing it back on schedule. The experiments are numbered E1 through E6, mapping one-to-one onto the six sources e1 through e6 under `code/volumn_codes/vol8/systems-programming/linux/terminal/02-pty-recording/`; the code together with all raw output is archived, and the session artifacts session.typescript and session.timing are on file too: session.typescript records bytes, session.timing records moments; they were recorded live in E4 and serve as E5's input during playback. Output blocks in the text are excerpts; deletions inside a block are marked with ..., while trims at a block's head and tail are not individually annotated — the archive is authoritative when cross-checking. The E numbers in this article belong to this article alone; the previous article's E numbers share numerals with ours but have nothing to do with us — just match the directory when browsing the archive.

As usual, the environment is stated up front, because the numbers later must be checked against it: the experiments come from my WSL2, the kernel is build 6.18.33.2-microsoft-standard-WSL2, g++ is 16.2.1, glibc is version 2.44, compilation is uniformly `-std=c++20 -Wall -Wextra -Wpedantic -O2`, the warning count is zero, and all `.out` files plus the two session artifacts come from the same run on 2026-10-05. The experiment processes have no tty of their own — the rig running them hands us stdio as three pipes — so wherever a terminal is needed we build our own pty as in the previous article, touching not one byte of the main terminal. devpts, the kernel filesystem supplying /dev/pts, is a single-instance mount on this machine; the parameters gid=5, mode=620, ptmxmode=000 are visible in E1's output, and the /dev/pts/ptmx entrance is sealed by ptmxmode, so the proper road to a master is /dev/ptmx. /bin/sh is sh-5.3, which is really bash running under the name sh — it is E4's protagonist. Recording and playback timing all use CLOCK_MONOTONIC; the reasons for that choice were covered in the [clock sources article](../time/01-clock-sources.md), so we will not reopen the class.

## E1: Opening a Pair in Four Steps: posix_openpt, grantpt, unlockpt, and ptsname

The opening procedure man 4 pts gives amounts to a few sentences: open /dev/ptmx to get the master; the slave's node will appear in /dev/pts, but before that door opens you must clear the two formalities of grantpt and unlockpt. We build it accordingly, photographing /dev/pts's contents and node permissions after every step; the skeleton below is excerpted from e1_four_steps.cpp:

```cpp
int mfd = posix_openpt(O_RDWR | O_NOCTTY);  // 拿 master,节点此刻已出现在 /dev/pts
char* sn = ptsname(mfd);                    // 问出 slave 的路径
grantpt(mfd);                               // 名义上管节点的属主与权限
unlockpt(mfd);                              // 解锁
int sfd = open(sn, O_RDWR | O_NOCTTY);      // 现在才开得了门
```

The first block's output states the environmental facts; we lift it up verbatim:

```text
[0] 环境:/dev/ptmx 主次设备号 5:2(mode 0666);
    devpts 挂载:devpts /dev/pts devpts rw,nosuid,noexec,noatime,gid=5,mode=620,ptmxmode=000 0 0
  /dev/pts 内容(开新 pty 前): 7 5 4 9 3 2 0 1 ptmx
[1] posix_openpt -> master fd=3
    ptsname -> /dev/pts/6(isatty(master)=1)
  /dev/pts 内容(posix_openpt 之后、grantpt 之前): 6 7 5 4 9 3 2 0 1 ptmx
```

/dev/ptmx reports 5:2 and mode 0666 — exactly the two numbers man 4 pts writes, and this machine matches them to the letter. The fd obtained is 3, because 0/1/2 are all taken by the rig's pipes. What truly deserves a second look is the pair of directory listings around it: the slave's node /dev/pts/6 was already there the moment posix_openpt returned, before grantpt's scene began. The node's birth and death track the master — a sentence that gets cashed again at the closing. The isatty(master)=1 in the output is also worth noting; we inventoried this piece of trivia in E1 of the previous article: Linux's master end is also a tty, and E3 is about to rely on it.

We deliberately force the slave open before unlockpt:

```text
[2] unlockpt 之前 open(/dev/pts/6) -> -1 errno=5(Input/output error)
[3] grantpt 之后  stat /dev/pts/6: uid=1000 gid=5 mode=0620
    unlockpt 之后  stat /dev/pts/6: uid=1000 gid=5 mode=0620
[4] open(/dev/pts/6) -> 4 isatty=1 ttyname=/dev/pts/6
```

It reports EIO (errno 5), not EACCES. This errno has its own logic: what is locked is not the permission bits — the node and its permissions are perfectly fine — yet open simply cannot get in; what actually stops you is the lock on the device itself. That kernel lock has a name, TIOCSPTLCK, listed among the ioctls of ioctl_tty(2), and unlocking it is exactly what unlockpt does. The two stat snapshots around grantpt and unlockpt are identical; what they touch simply does not live at the node-attribute layer.

So what does grantpt actually do? man 3 grantpt states its duty as changing the slave node's owner and permissions, but the archive's snapshots only photographed the after picture, so the credit cannot be assigned. While writing this article I planted an extra probe in /tmp, compiled with the same flags, stat-ing immediately after posix_openpt:

```text
posix_openpt 后:  uid=1000 gid=5 mode=0620
grantpt 后:      uid=1000 gid=5 mode=0620
```

The node was already uid=1000, gid=5, mode=0620 at the moment of posix_openpt — grantpt ran on empty. Owner 1000 is us ourselves; devpts settles the ownership when the master is opened. gid=5 and mode 0620 correspond exactly to the mount parameters gid=5 and mode=620; in that 0620, the tty group gets only write — which is what write(1)-style programs rely on to hand messages onto your terminal — while read permission is reserved for the owner alone, so other people's sessions stay out of your reach. The chore the man page assigns is done by devpts on this machine; glibc's grantpt is a formality on Linux, older systems may behave differently, and recording by this machine's account is enough.

With the door open we verify the channel: the master writes five bytes in, and the slave's side receives them verbatim in one read — the bytes have passed through the line discipline on the way. We save the best act for the closing, for the moment the master goes away:

```text
[5] master 写 "ping\n",slave read -> n=5 (ping
):字节过了一遍行规程
[6] close(master) 之后  stat /dev/pts/6 失败:No such file or directory
    slave 再 read -> n=0 (EOF)  <- master 没了,slave 读到 EOF(read 返回 0)
    slave 再 write -> n=-1 Input/output error  <- 写也一样,对端没人了
  /dev/pts 内容(master 关掉后): 7 5 4 9 3 2 0 1 ptmx
```

The node vanished on the spot: the 6 that appeared in the listing after posix_openpt is gone now — the node's birth and death track the master, cashed as promised. Our end of the slave still holds an fd: read gets 0 (EOF), write eats EIO. pts(4) commits not a single written word to this half of the show; we record by measurement: master gone — the reading end sees EOF, the writing end sees EIO. In E6 we stand on the master's side to settle the closing act, where the measurements differ again.

For the closing there is also a pre-packaged choice: openpty wraps all four steps plus open(slave) into one call. One more posture is worth recognizing now: the posix_openpt family comes from UNIX 98 — man 3 posix_openpt demands `_XOPEN_SOURCE` no less than 600, while the grantpt kin asks only for 500. In C's strict mode, without that macro line the compile fails; we tried a small file with only the necessary headers: the no-macro and 500 tiers both got implicit declaration, and only the 600 tier compiled clean. But g++ on Linux defines `_GNU_SOURCE` by default, so all declarations are lit; we deleted the `#define _XOPEN_SOURCE 600` at the top of e1's source and compiled by this article's flags — the zero-warning result passed just the same, so the retained define counts as a belt-and-suspenders.

## E2: forkpty, One Stop: A Single Call Covers setsid, Controlling Terminal, and 0/1/2

The four steps only produce a device pair; to make the child work as if on a real terminal, a string of chores remains. We have in fact seen them all: setsid in the daemon article's E2, `ioctl(sfd, TIOCSCTTY, 0)` in its E3, tcsetpgrp in the previous article's E3, plus the dup2 that wires the slave onto 0/1/2. forkpty folds openpty, fork, and the child-side chores into one call; man 3 openpty says the child's work belongs to login_tty: create a new session, claim the controlling terminal, and let dup2 take over the three standard streams. We check every item:

```text
孩子报告: pid=1089214 sid=1089214(pid==sid:1,会话长) pgid=1089214 isatty0/1/2=1/1/1 ttyname(0)=/dev/pts/6 TIOCGSID=0(sid=1089214,控制终端到手) TIOCGPGRP=0(前台组=1089214,自己)
父进程 t=0.1ms 收齐 master 流:(ping\r\npong\r\n) —— 里面混着孩子的回显与输出,一条流分不出方向
孩子退出:0
```

Let us walk the checklist item by item. The reported pid equals the sid — the child is itself the leader of the new session. isatty(0/1/2) all report 1, ttyname gives /dev/pts/6, and all three standard streams landed on the slave. TIOCGSID also returned successfully — the controlling terminal is in hand. This binding we speak of is precisely what got cut away after setsid in the daemon article's E2, beyond what redirection can restore. TIOCGPGRP reports the foreground group as the child itself — ^C's recipient is in place.

The second line's output is the stage for this article's second half. The parent wrote `ping\n` into the master; the child read it and answered `pong\n`; and what the parent read back from the master was the whole string `ping\r\npong\r\n`: what was typed and what was printed mix in one stream, with even the \n padded into \r\n by output processing — the previous article's E5 ONLCR at work. The master's stream carries no direction bit and no delimiter; E4's recording will magnify that for you. One small design point deserves a mention: forkpty's last two parameters can set termios and winsize in passing; we pass nullptr for both, so what opens is the factory state, and the winsize vacancy we leave for E6 to fill.

There is one more small move on the child's side worth explaining: the moment forkpty returns, the child closes the master's fd. The child does this not for tidiness — whether the master's fd gets closed cleanly decides the life and death of a terminal: the kernel tracks references for the pty, and as long as anyone still holds the master's fd, the device pair is not yet dead. The scene in the daemon article's E3 where close(master) triggers SIGHUP had as its precondition that the parent had become the last holder of the master; had our child kept clutching the master too, no amount of closing by the parent would work, and the terminal would never quite die. The reporting pipe through which the child writes its checklist back to us also had to be established before forkpty — fds are inherited through fork — and the write end in the child's hand and the read end in the parent's are simply the two ends of one and the same pipe.

## E3: One Byte String, Two Fates: Pipe versus pty

Back to the opening question: why must it be a pty — where has the pipe wronged you? The apparatus is the same reader program: the same executable run again with a reader argument, playing the reading end itself, reading stdin, doing no processing on what arrives, and reporting every read's harvest back through the reporting pipe. The byte string fed to it is fixed at six bytes: abc, one DEL (0x7f), z, newline. We have it hang on a pipe, on a factory-state pty (canonical plus ECHO), and on a pty with ECHO off. First, the pipe round:

```text
[场景1 管道]喂 61 62 63 7f 7a 0a (abcDELz\n)
  READ n=6 61 62 63 7f 7a 0a (abcDELz\n)
READ n=0
```

The pipe hands over all six bytes, not one fewer; nobody touched it anywhere in sight, and DEL is nothing more than an ordinary 0x7f byte. Neither end of a pipe knows anything about line editing — if you want a pipe, you must write the editing yourself. Now we feed the same byte string to a pty instead:

```text
[场景2 pty 出厂态(canonical+ECHO)]
  喂 61 62 63 7f 7a 0a (abcDELz\n)
  master 收到的回显:61 62 63 08 20 08 7a 0d 0a (abc^H ^Hz\r\n)
  READ n=4 61 62 7a 0a (abz\n)
```

Look at the reading end: what arrives is four bytes, `abz\n` — the c in the middle was eaten by DEL. Where did the editing happen? The echo line answers most directly: the master separately received nine bytes — abc is the echo of what was typed, 08 20 08 is DEL's echo posture (backspace, space, backspace), whose dedicated job is wiping the c off the screen, and the trailing 0d 0a is the newline after passing through output processing. Both the copy the reading end received and the copy the master saw come from the line discipline's hand; our reader program contains not one line of editing code and does not even know editing happened. Now we turn ECHO off and feed the same string:

```text
[场景3 pty 关 ECHO(编辑仍在)]
  喂 61 62 63 7f 7a 0a (abcDELz\n)
  master 收到的回显:(无)
  READ n=4 61 62 7a 0a (abz\n)
```

The editing happened as before — the `abz\n` that arrived is unchanged to the byte — but the echo is gone. The manner of turning it off also deserves a look: the tcgetattr and tcsetattr in the code both go through the master's fd, and both work — relying on exactly the fact we recorded in E1: on Linux the master is also a tty, and both ends share the same line discipline. Editing and echo thereby separate into two things: one governs what what-you-typed finally looks like, the other governs what the screen shows you. Under a password prompt, the screen stays dead still while you type, thanks to exactly this switch — your line still gets edited, still gets delivered to the reading end.

Line up the three scenes side by side and the ownership settles into place: line editing and echo live in the tty layer — nothing to write in the reading program's code, and nothing in the feeder's either. The other class of customers man 4 pts names makes the point even better: su and passwd flatly refuse input from pipes — the proper road to feed them is a pty. script(1)'s manual even provides an -E switch governing the slave's ECHO bit; in never mode the recording lacks the input's echo — exactly what our scene 3 performed. As for the shell — do not rush to thank the line discipline on its behalf: interactive shells carry their own separate line editing, and in E4 it will show you in person.

## E4: A Minimal script(1): Recording an sh Session

util-linux's script(1) does exactly the job of recording a terminal session verbatim into a file — it has done so since the 3.0BSD era, its default output filename is literally typescript, and the archive's two session artifacts are named after it. Its skeleton we have already assembled in full: forkpty opens the rig, the child execs an interactive sh, the parent plays keyboard-plus-screen — what it writes into the master is the keys pressed, what it reads from the master is the bytes that belong on screen, and every read is recorded verbatim into the file. We re-implement on this skeleton: the child is `sh -i`, no human types, and our schedule is written in code: echo hello, then `sleep 1; echo done` for a one-second pause, with exit closing the show.

Right after opening the rig, the parent's first chore is setting the size to 24x80 via `ioctl(mfd, TIOCSWINSZ)`. Why must it be set? E6 holds the measurement; here we tell only half: a fresh pty has no notion of a window, and if we do not set one proactively, programs that lay out their interface by size will simply collapse. Then we enter the loop: poll waits for the master to be readable, the bytes read and the gaps between them go into two files, and key feeding follows the schedule. The loop's skeleton looks like this (excerpt from e4_recorder.cpp):

```cpp
struct pollfd pfd{mfd, POLLIN, 0};
char buf[4096];
for (;;) {
    // ...按日程决定这一轮发不发输入:第0条等 shell 起来,第1条等 hello,第2条等 done
    int r = poll(&pfd, 1, fed[2] ? 500 : 50);
    if (pfd.revents & (POLLIN | POLLHUP | POLLERR)) {
        ssize_t n = read(mfd, buf, sizeof buf);
        if (n <= 0) break;                    // EIO:会话收场
        double t = now_ms();
        fprintf(timing, "%.1f %zd\n", t - t_last, (size_t)n);   // 时刻表:间隔+字节数
        t_last = t;
        fwrite(buf, 1, (size_t)n, script);    // 字节流:原样追加
        seen.append(buf, (size_t)n);
    }
    // ...(poll 出错即收场,键的 write 与 exit 后的 waitpid 收尸也排在这里)exit 晚了半秒的来历就在这
}
```

How does the session end? After exit, sh is gone, not one fd remains on the slave side, and our read(master) gets EIO — the loop stops right there:

```text
read(master) -> n=-1 Input/output error —— 会话收场
录制完成:时长 1256ms,12 笔共 138 字节
```

The recorded session totals 138 bytes, and we bring it up in full. ESC is our name for 0x1b; the gaps around prompts are for readability — at the byte level, the archive's session.typescript is authoritative:

```text
ESC[?2004h sh-5.3$ echo hello\r\n
ESC[?2004l \r hello\r\n
ESC[?2004h sh-5.3$ sleep 1; echo done\r\n
ESC[?2004l \r exit\r\n
done\r\n
ESC[?2004h sh-5.3$ exit\r\n
ESC[?2004l \r exit\r\n
```

The first eight bytes, `1b 5b 3f 32 30 30 34 68`, spell ESC[?2004h. It is sh's line-editing library readline greeting the terminal emulator (the emulator being whoever holds the master and paints the screen — xterm and Windows Terminal both qualify; in this experiment that job belongs to the parent process): please enable bracketed paste mode, so that pasting a large block of text wraps it in special markers and newlines inside the paste are no longer executed one by one as a series of Enters. This picks up exactly the thread E3 left: the interactive shell finds the line discipline's built-in editing too plain, retunes the terminal to its own modes, and hands editing, history, and completion entirely over to readline. Line editing thus acquires two layers: the line discipline as the fallback for ordinary reading programs, and fancy programs building their own layer on top — and in this session we will see both layers take the stage.

Now the three appearances of exit in the stream. Our schedule fed it once, yet it shows up three times in the stream, and only with the timing log can we tell who is who. The trouble lies in our own schedule logic: the code's wait condition is the word `done` appearing in the stream, and the moment it appears, exit is fed — but the second command's echo itself contains echo done, so the keyword hit early and exit entered the queue ahead of plan. The actual feeding moment then dragged by half a second, because the key-feeding write in the code is scheduled after poll, and that poll round held no data, sitting out a full 500ms timeout before returning — the 500.6 entry in the timing file records exactly that delay.

Aligning the byte sequence with the timing log, the three exits peel apart layer by layer. During the half-second delay, sh was sleeping, readline was off stage, and the terminal had been handed back by readline to the factory state — canonical plus ECHO again. The line discipline echoed the queued exit once — that is the first one we identified. When the second ticked to full, done printed, the new prompt lit up, readline returned to duty, picked up the queued line and echoed it again — the second. Finally sh received exit and left the stage; an interactive bash prints a farewell exit of its own before quitting — an old habit, a well-known behavior, and we did not run a dedicated experiment to isolate that layer — but sh-5.3 is bash's sh mode, so the attribution line connects. From the second command's echo to done's appearance, the timing log records 500.6 plus 501.0 = 1001.6ms — exactly sleep 1's duration plus a little scheduling fuzz. Which layer was on stage at which moment is a duty roster we inferred from bytes plus timing; the file itself records only gaps and byte counts.

The schedule our playback will use is session.timing: one line per delivery, two columns — milliseconds since the previous delivery, and that delivery's byte count:

```text
2.8 8
0.2 8
250.7 21
0.1 7
0.1 16
0.1 29
500.6 6
501.0 6
0.0 8
0.1 12
0.0 11
0.1 6
```

The twelve gaps sum to 1255.8ms. Our two-column format follows script's timing in spirit; its two columns record seconds and character counts, ours record milliseconds and byte counts — mind the units when cross-referencing. One last thing to make clear: what lies in the typescript is a single stream with no direction information — which delivery is the echo of something typed, which is a program's output, the file does not say one word about. script(1)'s own BUGS section confesses it: newlines and backspaces all end up in the recording, unlike what a newcomer expects — that is the master's nature, nobody being lazy.

## E5: Playback: Re-enacting the Session on Schedule

Having recorded it, we must get it to play back before it counts. The player's logic amounts to a few lines: read the timing line by line, sleep the gap since the previous delivery (we can also divide the gap by a speed factor), then write the corresponding bytes from the typescript to stdout. The core loop is just this (excerpt from e5_player.cpp):

```cpp
while (fgets(line, sizeof line, timing)) {
    double delta = 0; size_t n = 0;
    if (std::sscanf(line, "%lf %zu", &delta, &n) != 2) continue;
    sleep_ms(delta / speed);              // 倍速就除在间隔上
    if (fread(buf, 1, n, script) != n) { std::fprintf(stderr, "typescript 短了一截\n"); break; }
    write(1, buf, n);                     // 这笔字节交给 stdout
}
```

We play it once at 1.00x and once at 2.00x:

```text
$ ./e5_player session.timing session.typescript > replay.out
回放完成:12 笔 138 字节,计划睡眠合计 1255.8ms,实际睡眠合计 1256.9ms,全程墙钟 1257.1ms(含写 stdout 的开销),倍速=1.00
$ cmp replay.out session.typescript && echo 一致
一致
$ ./e5_player session.timing session.typescript 2.0 > /dev/null
回放完成:12 笔 138 字节,计划睡眠合计 627.9ms,实际睡眠合计 628.9ms,全程墙钟 629.0ms(含写 stdout 的开销),倍速=2.00
```

The 1.00x run's full wall clock is 1257.1ms against the recording side's timing total of 1255.8ms — a 1.3ms difference, owed to nanosleep's granularity plus the cost of writing to stdout; at this magnitude we accept it. The 2.00x round comes to 629.0ms — gaps halved, delivery count and bytes untouched. The byte-level gate we hand to cmp: replay.out versus session.typescript passes silently, exit code 0, and the echo that follows prints the match marker — all 138 bytes agree bit for bit. The util-linux side has a ready-made pair: script's --log-timing records the schedule, scriptreplay's -d/--divisor plays it back. --timing is script's older spelling, now marked obsolete in the current manual — we record by the current name. divisor means exactly 'divisor': dividing the gaps by it speeds things up, the same idea as our `sleep_ms(delta / speed)` dividing the gap by speed.

One point we must volunteer. During acceptance, the player's stdout was redirected into a file, dodging a layer of processing. If you play it straight onto your own terminal, the \r\n in the recording passes through the line discipline's output processing once more, ONLCR pads another \r onto the factory \n, and the screen shows \r\r\n. A real playback program must first set its own tty to raw, the previous article's way, before writing. I did not run a measurement for this step; it follows directly from the previous article's OPOST measurement — take it as a starting point when you write your own player.

## E6: Size and Closing: winsize, SIGWINCH, and the Master's EIO

The thread E4 buried, we now gather. We open a fresh pty pair, and the first question asked is the size:

```text
[1] 新开 pty 的 winsize: rows=0 cols=0 —— 0x0,没人替你设
```

It reports 0x0. The pty has no screen, no window, and its size must be told to it — the only one who can tell it is the terminal emulator holding the master. forkpty's fourth parameter could have set it at opening; e4 patching it via ioctl is the same task in a different posture. We resize twice, while the child reports TIOCGWINSZ and the SIGWINCH count from the slave side every 100ms:

```text
[2] t=200ms TIOCSWINSZ(30x100)
[2] t=500ms TIOCSWINSZ(40x120)
    孩子报告 t=  100ms TIOCGWINSZ=0x0 SIGWINCH 计数=0
    孩子报告 t=  200ms TIOCGWINSZ=30x100 SIGWINCH 计数=1
    ...
    孩子报告 t=  501ms TIOCGWINSZ=40x120 SIGWINCH 计数=2
    ...
[3] 孩子退场(slave 全关),父进程这边:
    read(master)  -> n=-1 errno=5(Input/output error)  <- 队列空+对端关,才报 EIO
    write(master) -> n=1 errno=0(Success)  <- 写不报错
    再 read(master) -> n=1 (回显)  <- 刚写的 x 被行规程回显回来了(ECHO 还开着)
```

The size side verified two mechanisms. The moment TIOCSWINSZ lands, TIOCGWINSZ on the slave side is the new value — the t=200ms entry matches airtight. Every time we change the size, the foreground process group attached to the terminal receives one SIGWINCH; after two ioctls the count went from 0 to 2. The instant you drag your terminal window, the signal sender is the emulator making this very call on the master; our experiment has no human dragging windows, so we pressed the button twice via ioctl — the mechanism travels the same road. Full-screen programs of the ncurses kind redraw their interfaces on SIGWINCH; with winsize stuck at 0x0 they cannot even lay out the first screen — and E4's preset of 24x80 closes its loop here.

What truly deserves to stand on its own is the [3] trio. The child has left the stage, not one fd remains on the slave side, and the parent's read(master) gets EIO — that half was within expectation; the daemon article's E3 performed the other half of the same affair from the other end. The surprise comes after: write(master) reports success (n=1), not even an errno for us. We read once more and actually get back the x we just wrote — the line discipline delivered its echo: this tty's ECHO is still on, and with the slave gone it emits all the same, into the master's read queue, and we receive it.

Unwilling to rule on a single experiment, we laid a re-check matrix into the archive's README: five consecutive single writes of 2048 bytes followed by a read, spaced 200ms apart — the whole routine rerun ten times, and all ten rounds read the echo back. Squeezing the gap to 0ms and reading immediately, the results mixed: sometimes half of an echo still in transit (852 or 254 bytes), sometimes read winning the race before the echo entered the queue and reporting EIO outright. Putting it all together, the criterion surfaces: for read to report EIO, the queue must be empty and the peer closed — both conditions met together. The write path carries no such check at all, so write's success proves nothing. For us coders the rule is one sentence: to know whether the pty's far side still exists, only read's EIO is trustworthy — which is exactly what our e4 recorder's closing detection relies on.

The mirror relation with the daemon article's E3 is now complete. There we `close(master)`, and from the slave side saw: the foreground group receiving one SIGHUP each in the same millisecond, while the session leader was executed by the default action. Here we let the slave leave the stage, and from the master side saw: read reporting EIO while write still succeeds. The two ends of one device pair, with a different end exiting, see different signals — and when we write programs, we must be able to read both ends.

## The Other Side: Windows' ConPTY

The POSIX master and slave long had no rightful owner on the Windows side; to give terminal-ness to programs on pipes, the winpty era worked entirely by emulation. Windows 10 1809 in 2018 delivered ConPTY (the Windows pseudo console) into the system: the API family led by CreatePseudoConsole likewise opens a pair of channels, Windows Terminal and Windows OpenSSH both rely on it, and E6's resize corresponds to ResizePseudoConsole over there. ConPTY is not on [the Windows console article](../../windows/console/01-console-api.md)'s experiment bench — the console's character buffer, input events, and VT sequences are that article's main course; here we merely pay our respects.

<ReferenceCard title="Reference Resources">
  <ReferenceItem
    :id="1"
    title="pts(4)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man4/pts.4.html"
  />
  <ReferenceItem
    :id="2"
    title="pty(7)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man7/pty.7.html"
  />
  <ReferenceItem
    :id="3"
    title="posix_openpt(3) with grantpt(3), unlockpt(3), and ptsname(3)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man3/posix_openpt.3.html"
  />
  <ReferenceItem
    :id="4"
    title="openpty(3), login_tty(3), and forkpty(3)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man3/openpty.3.html"
  />
  <ReferenceItem
    :id="5"
    title="ioctl_tty(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/ioctl_tty.2.html"
  />
  <ReferenceItem
    :id="6"
    title="script(1)"
    publisher="util-linux (man7.org mirror)"
    url="https://man7.org/linux/man-pages/man1/script.1.html"
  />
  <ReferenceItem
    :id="7"
    title="scriptreplay(1)"
    publisher="util-linux (man7.org mirror)"
    url="https://man7.org/linux/man-pages/man1/scriptreplay.1.html"
  />
  <ReferenceItem
    :id="8"
    title="Creating a Pseudoconsole Session (ConPTY)"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/console/creating-a-pseudoconsole-session"
  />
</ReferenceCard>
