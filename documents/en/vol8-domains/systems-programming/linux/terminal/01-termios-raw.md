---
title: "termios and Raw Mode: What the Terminal Layer Is Doing for You"
description: "The terminal is not an extension cord for your keyboard: line editing, echo, and translating ^C into a signal are all done for your program by the tty layer in the kernel; termios's four flag groups are the switch panel of that machinery, and every experiment runs on a self-built pty rig with zero contact with the main terminal. E1 is a factory-settings census (the ICRNL|IXON, OPOST|ONLCR, ISIG|ICANON|ECHO|IEXTEN whole family, B38400+CS8; cfmakeraw clears ICRNL/IXON/OPOST/ISIG/ICANON/ECHO/IEXTEN but not ONLCR, because once OPOST is off it is inert; the minimal raw that manually clears only ICANON|ECHO differs from cfmakeraw in four places — ISIG/IXON/ICRNL/OPOST — each later cashed out into behavior, plus three machine facts: the master end is also a tty sharing the same line discipline with the slave, since glibc 2.42 the B constants are the real rate values while the kernel's c_cflag still stores the old encoding with glibc translating at both ends, and the factory VMIN=1 VTIME=0). E2: canonical idles waiting for a newline and delivers 5 bytes in one stroke at 600.6ms, while raw delivers block for block as fed; DEL's in-line editing happens in the tty layer (the echo is the three bytes 08 20 08), and ^U wipes the whole line. E3: the two things ^C does can be done separately — with no foreground process group the queued input still gets flushed but SIGINT has no receiver; after the three steps setsid+TIOCSCTTY+tcsetpgrp the same ^C finds a receiver; ^D hands over the un-newlined line, a second one returns 0 and the EOF does not stick — the same fd stays readable afterwards; under raw, 0x03/0x04/0x1c are all ordinary bytes. E4: VMIN/VTIME in four combinations across six experiments; D+F jointly rule the interbyte timer resets on every byte received (feeding every 200ms yields 4 bytes @600.7ms, a 450ms gap in supply yields 2 bytes @955.7ms; the starts-only-from-the-first-byte account is cut down by measurement, consistent with termios(3)'s interbyte timer). E5 goes bit by bit: with ECHO on, the newline inside the echo also gets padded to CR LF by ONLCR; ECHO off leaves no trace at the master while the reading end still receives everything (the password-typing mechanism); OPOST differs by one CR inside versus outside the gate; with ICRNL off, CR is not a line terminator and needs a ^D to push it through. E6: the RAII terminal_guard — raw leftovers survive across processes (relay children inherit them with zero configuration), the destructor restores even on the throw path, TCSAFLUSH flushes leftover input while TCSANOW keeps it, and leftover bytes switched back to canonical get delivered in one stroke, no line terminator required"
chapter: 8
order: 1
platform: host
difficulty: intermediate
cpp_standard: [20]
reading_time_minutes: 31
prerequisites:
  - "POSIX 文件 I/O:open/read/write 与 fd 的一生"
  - "守护进程、会话与环境:setsid、双 fork、rlimit 与 environ"
related:
  - "守护进程、会话与环境:setsid、双 fork、rlimit 与 environ"
  - "时钟源与 POSIX 时间 API:机器里的九把钟"
  - "OS 资源的 RAII 范式:fd、HANDLE 与映射的同一副骨架"
  - "信号(上):sigaction 与异步信号安全"
tags:
  - host
  - cpp-modern
  - intermediate
  - 系统编程
  - POSIX
current_status:
  title: 正在打磨
  detail: 本批刚写完,正在通读打磨,表述与实测口径可能随时调整
  variant: polishing
translation:
  source: documents/vol8-domains/systems-programming/linux/terminal/01-termios-raw.md
  source_hash: 244e7a4dec6cfb1656f914aaf3ba3a1f82a7c5b371dd607632c7ea165ce773ff
  translated_at: '2026-11-22T15:40:21+00:00'
  engine: glm-5.3
  token_count: 18718
---
# termios and Raw Mode: What the Terminal Layer Is Doing for You

Let us start by asking about an action you perform dozens of times a day. You type the three letters abc in the terminal, the third one is wrong, so you press backspace once, type a d to fill in, and hit Enter as well. On screen the c disappears and the d slides into place — a routine you know by heart. So who handled that deletion? The foreground program in front of you is at this very moment blocked in read, having received not a single byte; it never even had a chance to process anything. Is it the shell? The shell is present, true, but all it waits for is the program's exit — it cannot step in here. The one really doing the work is the kernel's tty: between the moment a key leaves your finger and the moment read hands the bytes over lies an entire layer of machinery. Line editing — it did that. Echo — it did that. Turning ^C into SIGINT — also its doing. termios is the switch panel this machinery exposes, and in this article we flip every switch on the panel, watching the behavior change with our own eyes each time.

But before touching the switches, we should know who cares about them. Full-screen programs like editors, htop, and ssh want the tty's default services withdrawn wholesale: key presses must arrive one by one, immediately, and the interface's backspaces and cursor movements are all drawn by the programs themselves. ^C must become an ordinary byte they read themselves, not something translated into a signal halfway through. Every byte the program outputs must hit the wire as-is, with nobody casually processing even one byte along the way. This fully-withdrawn state is what people call raw mode. Meanwhile, the in-line editing you enjoy while typing commands, and the echo-free password input, are another combination on that very same panel. One terminal can hold two opposite expectations, and the reconciliation is entirely in termios's hands.

The experiments are numbered E1 through E6, corresponding to the six programs e1 through e6 under the archive at `code/volumn_codes/vol8/systems-programming/linux/terminal/01-termios-raw/` in the repository; the code and all raw output are on file. The E numbers in this article belong to this article alone: the text always writes uppercase E, archive filenames always start with lowercase e, and this holds identically for this article, the next one, and the Windows console article — same numbers are different things in each, so just match the directory when browsing the archive; case is not something to match. Most output blocks in the text are excerpts; deleted lines are marked with `...`, and when checking against the archive, the archive is authoritative.

As usual, the environment is stated up front, because the numbers later must be checked against it: the experiments come from my WSL2, with kernel 6.18.33.2-microsoft-standard-WSL2, g++ 16.2.1, and glibc 2.44; compilation is uniformly `-std=c++20 -O2 -Wall -Wextra -Wpedantic`, the result zero warnings, and all `.out` files were captured on 2026-10-05. Timing uses CLOCK_MONOTONIC throughout; the criteria for choosing that clock were already measured in the [clock sources article](../time/01-clock-sources.md), so we will not re-teach it.

One more ground rule to state: termios is an attribute of the kernel's tty object, not attached to any process. If we ran experiments on the main terminal you are actively using, any setting we broke would silence your terminal on the spot, and the only rescue would be `stty sane`. So all experiments run inside a pty we build ourselves: the parent process calls `openpty` to open a master and slave pair, the forked child wires the slave onto its fd0, and it thereby owns a terminal at our full disposal. The parent writes bytes into the master on its own schedule, playing the person at the keyboard, and each time the child completes a read, it reports the start time, completion time, byte count, and contents back to us through a reporting pipe. Not one byte of the main terminal is touched. The pty's own story (`posix_openpt`'s four steps, `forkpty`, the read/write semantics of the master) is the next article's main topic; here we use it as apparatus. This article's experimental code goes with bare POSIX calls — the unique_fd of the previous few articles does not get a role this time.

## E1: A Census of the Four Flag Groups: Factory Settings, Bit by Bit, in Plain Words

A freshly opened pty has had nothing configured, so we photograph the factory settings with `tcgetattr`. What actually governs inside the termios struct is a combination of four flag groups plus a row of special characters, each with its own direction: c_iflag governs the way in — what gets rewritten in incoming bytes. c_oflag governs the way out — what gets processed in outgoing bytes. c_cflag governs the line — physical parameters like bit width and baud rate are registered under its name. c_lflag governs behavior — terminal-specific services like line editing, echo, and signal translation. What lives in c_cc is a row of settings: the byte corresponding to each special key — ^C, ^D, and all the rest — is registered right here.

```text
[B1. 新开 pty slave 的出厂设置(内核默认)]
  c_iflag  = 0x00000500 :  ICRNL IXON
  c_oflag  = 0x00000005 :  OPOST ONLCR
  c_cflag  = 0x000f00bf : CS8 CREAD
  c_lflag  = 0x00008a3b :  ISIG ICANON ECHO ECHOE ECHOK ECHOCTL ECHOKE IEXTEN
  c_cc: VINTR=^C VQUIT=^\ VERASE=^? VKILL=^U VEOF=^D VSTART=^Q VSTOP=^S VSUSP=^Z VMIN=1 VTIME=0
```

Let us identify them one by one. On the way in, ICRNL translates CR into NL — this is the real reason the Enter key can act as a line terminator; the 0x0d you press is swapped for 0x0a at the door, and in E5 we will watch it move with our own eyes. IXON lets ^S and ^Q be swallowed as flow control — ^S governs pausing output, ^Q governs resuming it — and the two of them never reach the reading program's hands. On the way out, OPOST is the master switch of output processing, and ONLCR is one item under it, padding the \n a program writes into \r\n; when you write one newline character and the cursor obediently returns to the start of the next line, the credit goes to it. That long string in c_lflag is this article's main cast: ISIG governs translating special characters into signals, ICANON governs line mode, ECHO governs echo, and ECHOE, ECHOK, ECHOCTL, ECHOKE are four sub-switches hanging under ECHO: echoing the erase of one character belongs to ECHOE, echoing the kill of a line to ECHOK, printing control characters in ^X guise to ECHOCTL, and the on-screen erasure when a whole line is wiped to ECHOKE. The settings in c_cc line up with keyboard common sense: backspace is ^? (0x7f, not the 0x08 some people assume), ^U wipes the whole line, ^D delivers EOF, ^Z handles suspend. The factory VMIN=1 and VTIME=0 — in non-canonical mode they decide read's return conditions, and the section devoted to them is E4.

The baud-rate part deserves a second look on its own, because it sits between two notations. The archive's e1 measurement: on this machine's glibc 2.44, the B constants are already the real rate values — the `B9600` macro's value is literally 9600, the new convention since glibc 2.42; in the old convention it was an inscrutable encoding. The kernel's c_cflag still stores the old encoding. We set B9600 once and read it back:

```text
    c_cflag=0xf00bf 的记录:低 4 位 0xf 是输出波特率的老编码(B38400=0o17),0xf0000 那几位是输入波特率的镜像(老编码<<16);本机 glibc 的 B 常量已是真实速率值(B9600 宏==9600,glibc 2.42 起的新口径),内核里存的仍是老编码,glibc 两头翻译,round-trip 实测:
    设 B9600 后 cfgetospeed=9600 c_cflag=0xd00bd(低 4 位变 0xd=0o15 老编码,pty 上无物理意义,stty 同样报 9600)
```

Look: we set 9600, `cfgetospeed` reads back 9600, yet c_cflag's low 4 bits changed from 0xf to 0xd — that is B9600 in the old encoding. The translation is done by glibc at both ends, and the round-trip agrees. The factory 0xf is B38400 in the old encoding, and those 0xf0000 bits are a mirror of the input baud rate. On a pty these bits carry no physical meaning, but a census will see them, and matching them up is enough to set the mind at ease. One more note in passing: the archive's original annotation wrote the changeover point as 2.44, mistaking this machine's version for the starting version; it was fixed when the .out was re-recorded, and the starting version counts as 2.42 per the official announcement.

## Flipping to raw: What cfmakeraw Clears, and What It Leaves

We flip the panel to raw; the standard library's one-button operation is called `cfmakeraw`, and we show its before-and-after difference as a diff:

```text
[cfmakeraw 相对原始的差异]
  c_iflag : -ICRNL -IXON
  c_oflag : -OPOST
  c_cflag : (无变化)
  c_lflag : -ISIG -ICANON -ECHO -IEXTEN
    注意:cfmakeraw 只清 OPOST、不清 ONLCR(OPOST 一关,ONLCR 失去作用,留着无害);ECHOE/ECHOK/ECHOCTL/ECHOKE 是 ECHO 的子开关,母开关 ECHO 一清它们全部失效;
    本例 CSIZE 已是 CS8,所以 c_cflag 看不出变化。
```

Four lines of differences plus two lines of notes — let us walk them line by line. ICRNL and IXON cleared: nothing gets rewritten on the way in anymore, and the flow-control bytes are handed over like any others. OPOST cleared: the way out is now as-is too. ISIG, ICANON, ECHO, IEXTEN cleared: special characters are no longer translated into signals, read no longer waits for a line, and key presses are no longer echoed. The extended input handling governed by IEXTEN stops as well — escapes like ^V taking the next character literally fall under it. The four sub-switches of ECHO need no individual clearing; turn off the master switch and they all go inert together, which is why cfmakeraw does not touch them. c_cflag looks unchanged, but the truth is that CSIZE was already CS8 — cfmakeraw sets the width to CS8, VMIN to 1, and VTIME to 0, and the factory settings happen to be exactly that combination, so the diff shows no movement.

What truly deserves a pause is that ONLCR stays. With OPOST, the master switch of output processing, turned off, ONLCR as one item under that master switch naturally goes inert, so cfmakeraw cannot be bothered to touch it — leaving it is harmless. When you clear bits by hand you may likewise skip this one, but you should know why, rather than copying a list off the internet and filling it in blindly. By the same token, cfmakeraw's full list also clears IGNBRK (ignore the break condition), ISTRIP (strip bytes to 7 bits), and other bits this machine never set — only bits that were set show up in the diff.

The more common approach online clears only the two bits ICANON and ECHO by hand — people call it minimal raw — and we made a diff of that too:

```cpp
manual.c_lflag &= ~(tcflag_t)(ICANON | ECHO);   // 网上流传的最小 raw
```

```text
[手工 raw vs cfmakeraw 相对原始的差异]
  c_iflag : -ICRNL -IXON
  c_oflag : -OPOST
  c_cflag : (无变化)
  c_lflag : -ISIG -IEXTEN
    含义:手工版下 ISIG 仍在(^C 还是信号)、IXON 仍在(^S/^Q 会被吃掉)、ICRNL 仍在(CR 会被改写成 NL)、
             OPOST 仍在(程序写的 \n 出门变 \r\n)。这些差异正是 E2/E3/E5 要逐个看到的行为。
```

The gap table above we will cash out into behaviors one by one: with ISIG still in its place, ^C remains a signal rather than a byte — E3 demonstrates that for you. With ICRNL still in place, the Enter key gets rewritten the moment it walks in — we see that in E5. OPOST also stays put, so a program's \n still leaves the door as \r\n — likewise E5's show. IXON's position unchanged means ^S and ^Q still never reach your hand; we did not schedule a dedicated experiment for it — the termios(3) man page's account agrees with its mechanism, so we note it and move on.

E1 also carried home a machine fact that differs from the customary claim, and we put it on the table. The traditional saying about the master end that openpty opens is that the master is not a tty; this kernel's measurement says otherwise:

```text
[A] master=3 slave=4:isatty(master)=1 isatty(slave)=1
    tcgetattr(master)=0 tcgetattr(slave)=0 两端看到的 iflag/lflag 相同:1
    经 master 清 ECHO:tcsetattr(master)=0,slave 侧 ECHO=0(0=同步清掉,两端共享同一份行规程)
```

isatty reports 1 for the master, and when we call tcsetattr through the master to clear ECHO, the slave side clears it in sync. The reason is not mysterious: the two ends share one and the same line discipline (the layer of logic in the kernel's tty responsible for line editing, echo, and signal translation), and master and slave are merely two outward names for that same logic. The master-is-not-a-tty claim holds on other implementations — POSIX simply does not specify isatty for the master end, and Solaris's master is indeed not a tty. We record it per Linux's measured account: both ends count as ttys, and changes propagate between them.

## E2: canonical versus raw: When read Is Willing to Deliver

With the panel covered, next comes behavior. In E2 we feed two modes with the same schedule and see how much read's patience differs. In the canonical (line mode) round, we feed one block every 300ms — ab, cd, newline:

```text
[阶段1 canonical:行没写完,read 一直干等]
  喂 t=   0ms 61 62 (ab)
  喂 t= 300ms 63 64 (cd)
  喂 t= 600ms 0a (\n)
  读 t=[    0.2..  600.6]ms n=5   61 62 63 64 0a (abcd\n)
  master 收到的回显:61 62 63 64 0d 0a (abcd\r\n)
```

In truth the child had been blocked in read from 0.2ms, idling until the newline walked in at 600.6ms, then scooping up 5 bytes in one stroke. The bytes fed in the first two blocks were not lost — they lay accumulating in the tty's line buffer; that is canonical's delivery condition: what read waits for is a whole line. Please also note the echo line: we fed \n (0a), yet the echo the master received was 0d 0a — echo travels the output road too, and it passed through ONLCR's gate as well; we return to this in E5.

In the second round we schedule the backspace key (0x7f, DEL) into the timetable:

```text
[阶段2 canonical:DEL(0x7f) 的行内编辑发生在 tty 层,读端拿不到]
  喂 t=   0ms 61 62 63 (abc)
  喂 t= 200ms 7f (DEL)
  喂 t= 400ms 64 0a (d\n)
  读 t=[    0.3..  400.8]ms n=4   61 62 64 0a (abd\n)
  master 收到的回显:61 62 63 08 20 08 64 0d 0a (abc^H ^Hd\r\n)
```

Look at what the reading end got: abd\n — the c was crossed out, and DEL itself left not a trace. The opening question now has its measured answer: backspace's in-line editing happens in the tty layer, finished before read ever delivers, and the reading program only ever sees the edited result. The echo string is interesting too: the three bytes between abc and d are 08 20 08 — back one, space overwrites the c, back one again — three moves acting out one deletion, and that is exactly the show the ECHOE sub-switch performs for us.

In the third round we bring on ^U (0x15, VKILL), whose role is wiping the whole line:

```text
[阶段3 canonical:NAK(^U) 整行抹掉,读端只看到新行]
  喂 t=   0ms 61 62 63 (abc)
  喂 t= 200ms 15 (NAK)
  喂 t= 400ms 78 79 0a (xy\n)
  读 t=[    0.3..  400.6]ms n=3   78 79 0a (xy\n)
  master 收到的回显:61 62 63 08 20 08 08 20 08 08 20 08 78 79 0d 0a (abc^H ^H^H ^H^H ^Hxy\r\n)
```

The whole abc line vanished, and the echo shows three 08 20 08 sequences — one erase routine per character — exactly the scene ECHOKE performs. Back at the reading end: it saw only the newcomer xy\n, as if the earlier line had never existed.

In the fourth round we switch termios to cfmakeraw's raw, with the schedule kept verbatim:

```text
[阶段4 raw(cfmakeraw,VMIN=1):喂几块到几块,没有行概念]
  喂 t=   0ms 61 62 (ab)
  喂 t= 300ms 63 64 (cd)
  喂 t= 600ms 0a (\n)
  读 t=[    0.2..    0.3]ms n=2   61 62 (ab)
  读 t=[    0.3..  300.5]ms n=2   63 64 (cd)
  读 t=[  300.5..  600.6]ms n=1   0a (\n)
  master 收到的回显:(无)
```

The result: however many blocks we feed, that many it delivers — three deliveries of 2, 2, and 1; bytes are handed over the moment they enter the door, with no notion of a line at all. The echo column is empty, ECHO having been cleared by cfmakeraw. The services canonical gives you are two layers in total — assembling bytes into lines, and echoing for you — and raw withdraws both together. That is exactly what editors want: the raw bytes of backspace, arrow keys, and chord combinations, with everything on the interface handled by themselves.

## E3: Special Characters and ISIG: The Two Things ^C Does Can Be Done One at a Time

While ISIG is on, bytes like VINTR (^C), VQUIT (^\), and VSUSP (^Z) are intercepted right in the tty layer and translated into signals for delivery. Books usually tell the two things as one; we lay them on the head of a single ^C: one side flushes the queued input and output, the other delivers SIGINT to the foreground group. The interesting part is that we can make the tty do only the first — on the condition that no foreground process group is registered under the tty's name. The experiment runs two scripts; in the first, the child never establishes a session of its own, and the pty has no controlling session either:

```text
[阶段1 canonical 无会话:^C 冲掉排队的 ab,但信号没有收件人(SIGINT=0)]
  喂 t=   0ms 61 62 (ab)
  喂 t= 200ms 03 (^C)
  喂 t= 400ms 63 64 0a (cd\n)
  读 t=[    0.2..  400.6]ms n=3   63 64 0a (cd\n) 计数:SIGINT=0 SIGQUIT=0
  master 收到的回显:61 62 5e 43 63 64 0d 0a (ab^Ccd^M\n)   孩子退出码=0
```

Look at what the reading end got: only cd\n; the ab queued ahead was flushed away by ^C. Yet the child's SIGINT count is 0 — the signal found no recipient. The queue got flushed but the signal was not delivered, because no foreground group under the tty's name existed to receive it. Now the second script: before read, the child takes three extra steps to put on the foreground-group identity:

```cpp
setsid();                   // 脱离原会话,自立门户
ioctl(0, TIOCSCTTY, 0);     // 认 slave 为自己的控制终端
tcsetpgrp(0, getpgrp());    // 把自己这一组设为这只 tty 的前台组
```

```text
[阶段1b canonical 前台组:setsid+TIOCSCTTY+tcsetpgrp 三步之后,同一场 ^C 有人接了]
  喂 t=   0ms 61 62 (ab)
  喂 t= 200ms 03 (^C)
  喂 t= 400ms 63 64 0a (cd\n)
  读 t=[    0.4..  400.5]ms n=3   63 64 0a (cd\n) 计数:SIGINT=1 SIGQUIT=0
...
```

Same schedule, same single ^C — and the SIGINT count reaches 1. Flushing the queue and delivering the signal are two independent actions: flushing is the tty's fixed reaction to a special character (it flushes whenever NOFLSH is unset), while delivering the signal must identify the foreground group, and with nobody to claim it, there is no way to receive. How sessions, controlling terminals, and foreground groups hang together was covered from the opposite direction with six experiment groups in the [daemon article](../process/02-daemon.md) — there the show was who gets executed when the terminal dies; here we borrow the same machinery, so we will not reopen the class. Also, the 5e 43 in the echo is precisely the literal ^C you see on screen: ECHOCTL prints control characters in the two-character caret notation. The ^C look you see every day is given by the echo layer, not by the keyboard.

^\ travels the same road. The third round's identical schedule swaps ^C for ^\ (0x1c), the SIGQUIT count goes from 0 to 1, and the queued input is flushed just the same. We will not re-paste the output wholesale — check it against round three of archive e3.

^D performs a different identity for us: it leaves queued bytes untouched, its dedicated job being to hurry the delivery:

```text
[阶段2 canonical:^D 交出行、再 ^D 是 EOF,EOF 之后还能接着读]
  喂 t=   0ms 61 62 (ab)
  喂 t= 200ms 04 (^D)
  喂 t= 400ms 04 (^D)
  喂 t= 600ms 78 0a (x\n)
  读 t=[    0.3..  200.5]ms n=2   61 62 (ab) 计数:SIGINT=0 SIGQUIT=0
  读 t=[  200.5..  400.6]ms n=0   () 计数:SIGINT=0 SIGQUIT=0
  读 t=[  400.6..  600.7]ms n=2   78 0a (x\n) 计数:SIGINT=0 SIGQUIT=0
...
```

The first ^D handed over the unfinished ab immediately, without even a newline attached — n reports 2. The second ^D caught an empty line, so read returned 0. This 0 looks like EOF but behaves nothing like it: for pipes and files, EOF means closed — every later read returns 0 forever — whereas the tty's 0 means 'no line this time'; it names an event, not a state. The reading end's third delivery is the evidence: later reads on the same fd still receive the subsequently fed x\n. 'EOF does not stick' refers to exactly this 0. When you press ^D in a shell and log out, that is the shell's own read deciding to stop after getting 0 — nothing is wrong with the fd.

In the raw round, we feed the three special bytes together as ordinary fodder:

```text
[阶段4 raw:0x03/0x04/0x1c 全是普通字节,一次 read 原样全收]
  喂 t=   0ms 61 62 03 63 64 04 1c 0a (ab^Ccd^D^\\n)
  读 t=[    0.3..    0.3]ms n=8   61 62 03 63 64 04 1c 0a (ab^Ccd^D^\\n) 计数:SIGINT=0 SIGQUIT=0
  master 收到的回显:(无)   孩子退出码=0
```

Count with us: one read received 8 bytes; 0x03, 0x04, and 0x1c all arrived in full count, while the two signal counters did not budge. With ISIG cleared, the special-character translation stopped, and the bytes were delivered as bytes.

## E4: VMIN and VTIME: Setting read's Return Conditions Yourself

With ICANON off, the notion of a line is gone, and the question of when read returns needs a new answer. The answer is written in the two c_cc slots VMIN and VTIME: VMIN is the minimum number of bytes each read must receive, and VTIME is a timer in units of 0.1 seconds. The setup code is just two lines:

```cpp
raw.c_cc[VMIN]  = vmin;    // read 至少收几个字节才肯返回
raw.c_cc[VTIME] = vtime;   // 计时器,单位 0.1 秒
```

We run one round for each of the three basic tiers; the output is excerpted throughout:

```text
[A: MIN=1 TIME=0 —— 来一个给一个]
  喂 t=   0ms 78 (x)
  喂 t= 300ms 79 (y)
  喂 t= 600ms 7a (z)
  读 t=[    0.2..    0.3]ms n=1  78 (x)
  读 t=[    0.3..  300.5]ms n=1  79 (y)
  读 t=[  300.5..  600.7]ms n=1  7a (z)
...
[B: MIN=4 TIME=0 —— 凑够 4 才交货,散喂也一次拿 4(两轮各 4 字节)]
  喂 t=   0ms 61 (a)
...
  喂 t= 750ms 64 (d)
  读 t=[    0.2..  750.7]ms n=4  61 62 63 64 (abcd)
...
[C: MIN=0 TIME=5 —— 没数据 0.5s 返回 0,有数据立刻拿走]
  喂 t=   0ms ()
  喂 t= 600ms 61 62 (ab)
  喂 t=1200ms ()
  读 t=[    0.2..  530.8]ms n=0  (计时到点,EOF 长相)
  读 t=[  530.8..  600.4]ms n=2  61 62 (ab)
  读 t=[  600.4.. 1102.8]ms n=0  (计时到点,EOF 长相)
```

Tier A's temperament is one-in-one-out — exactly the look of round four in E2. Tier B delivers only when 4 have accumulated: we fed one letter every 250ms, and it stubbornly held on until the 4th arrived, then delivered all at once — both rounds delivering exactly 4 bytes. Tier C zeroes MIN, and read becomes a poll with a timeout: when empty it returns 0 after 0.5 seconds — another EOF look-alike — and when data exists it takes it immediately. None of these three is hard to read; the hard one is the fourth combination: the tier where both MIN and TIME are greater than 0. Two versions of this timer's semantics circulate among the folk. Version one says the timer starts from the first byte, and delivery happens when it expires. Version two says the timer resets on every byte received — what it actually measures is the gap between bytes. The two versions say different things, so we stage two experiments and make them confront each other face to face.

The first round is arranged like this: we feed 1 byte every 200ms, four in a row, each gap under 0.5 seconds:

```text
[D: MIN=4 TIME=5 —— 每 200ms 喂 1 字节,计时器重不重置,看返回]
  喂 t=   0ms 61 (a)
  喂 t= 200ms 62 (b)
  喂 t= 400ms 63 (c)
  喂 t= 600ms 64 (d)
  读 t=[    0.2..  600.7]ms n=4  61 62 63 64 (abcd)
```

Version one predicts the timer expires at 500ms, when only 3 bytes are in hand, so the delivery should be 3. Version two predicts the timer resets on every received byte; 200ms gaps never exhaust the 0.5-second patience, life is renewed all the way to 600ms when the 4th byte enters, MIN=4 is satisfied, and the delivery is 4. Our measured n=4 at 600.7ms — version two wins.

In between sits round E: we feed exactly 1 byte and then cut supply; at 521.2ms the timer expires and hands over the 1 byte in hand. This round shows that read will not wait forever for MIN to fill — when the timer expires, whatever is there gets delivered, protecting precisely the supply-cut end.

Now the second round, with a different feeding pattern that blocks version one's retreat one more time:

```text
[F: MIN=4 TIME=5 —— a@0ms、b@450ms 后断供:计时器从第 2 字节重起则 ~950ms 交 2 字节;只从第 1 字节起算则 ~500ms 交 1 字节]
  喂 t=   0ms 61 (a)
  喂 t= 450ms 62 (b)
  读 t=[    0.2..  955.7]ms n=2  61 62 (ab)
```

a entered the door at 0ms, b at 450ms, and then supply was cut. Version one predicts a delivery of 1 byte at 500ms, because the timer only honors a's starting point. Version two predicts the timer resets the moment b enters, expires at 950ms, and the delivery would be 2. The measurement: n=2 at 955.7ms — version two wins another round. The man 3 termios wording for this tier is interbyte timer — a timer between bytes — which says plainly that every received byte resets it; it is only in retellings that this keeps turning into 'counts from the first byte'. For you, writer of interactive programs, this tier matters because: when the user types key by key at a leisurely pace, as long as the gap between keys does not exceed VTIME, read will quietly wait for MIN to fill and never snatch away a half-finished input mid-way.

## E5: ECHO, OPOST, and ICRNL: Each Direction Gets Its Own Rewrites

The tty is a bidirectional filter: the incoming direction is gatekept by c_iflag, the outgoing direction is processed by c_oflag, and echo happens to sit right between the two. In E5 we flip three switches one at a time, changing only one and watching its movement alone.

First, ECHO. We have the rig feed hi\n into the master; then we turn ECHO off and switch the feed to secret\n:

```text
[阶段1 ECHO:回显是 tty 干的,关掉它,读端照收、屏幕无痕]
  喂(ECHO 开) t=0ms hi\n
    master 侧(回显):68 69 0d 0a (hi\r\n)
  喂(ECHO 关) t=150ms secret\n
    master 侧(回显):(无)
    读端收到 n=3 68 69 0a (hi\n)
    读端收到 n=7 73 65 63 72 65 74 0a (secret\n)
```

With ECHO on, we feed hi\n and the master receives the echo hi plus 0d 0a — ONLCR at work again, because echo travels the output road and passes through the outgoing processing too. With ECHO off, the master stays clean, yet the reading end's harvest shrinks by nothing. The password you type at login showing nothing is exactly this act of turning ECHO off: the program switches it off, reads, then switches it back on. As for who owns the echo and line-editing services — E2 already showed the half that does not belong to the reading program; the half that cleared the shell in our opening scene gets a head-on look in the next article, which feeds the same byte string through a pipe and through a pty for comparison.

Next, OPOST — the direction reverses; it governs the bytes the program writes out. The child writes the same three bytes A\nB twice on the slave, and in between we clear OPOST:

```text
[阶段2 OPOST:孩子两次写同样的 A\nB,门里门外差一个 \r]
    master 侧(OPOST 开,第一轮):41 0d 0a 42 (A\r\nB)
    (t=200ms 清 OPOST)
    master 侧(OPOST 关,第二轮):41 0a 42 (A\nB)
```

The same three bytes: with the gate open, \n leaves as \r\n; with the gate closed, the three bytes pass through untouched. Your program wrote a single \n, yet the terminal advanced a line and returned to column zero — the \r added in is ONLCR's doing under OPOST. Once we clear OPOST, \n keeps only its line-advance semantics and the cursor's column stays put, which is why a raw-mode program that does not send \r\n itself will paint a staircase — a picture instantly familiar to anyone who has debugged over a serial line.

Finally, ICRNL, governing the rewrite on the incoming direction:

```text
[阶段3 ICRNL:进门方向,CR 要不要被翻成 NL]
  喂(ICRNL 关) y\r ^D
  喂(ICRNL 开) x\r
    读端收到 n=2 79 0d (y\r)
    读端收到 n=2 78 0a (x\n)
```

With ICRNL off, we feed y\r and the reading end gets nothing — CR has no standing as a line terminator, the line waits in the buffer for an ending that never comes, so we must add a ^D to push it through, and what arrives is still the verbatim y\r. With ICRNL on, we feed x\r: the 0x0d is translated into 0x0a at the door, becomes its own line terminator, and delivers immediately — the reading end receives x\n. So the Enter key's power to end a line rests on the translation ICRNL performs for it; the Enter key holds no inborn qualification. The minimal raw checklist in E1 did not clear ICRNL, so a program written from that checklist reading the keyboard will receive 0x0a, not 0x0d, when the user presses Enter — if your protocol counts keys by the byte, here is one place the books will not balance.

## E6: terminal_guard: Settings Live on the tty — Whoever Changed Them Takes Them Back

That termios does not attach to any process — E1 showed it once through master and slave sharing one line discipline; E6 takes a more striking angle: the process dies, and the settings stay. In phase A the child sets the tty to raw and immediately `_exit`s; we arrange no cleanup for it whatsoever:

```text
[阶段A 无 guard:改完就跑,raw 留在 tty 上]
  初始 lflag=0x8a3b ICANON=1 ECHO=1 ISIG=1
  孩子A(_exit 无善后)之后 lflag=0xa30 ICANON=0 ECHO=0 ISIG=0
  孩子B(没设过任何东西)读到 n=1 (a) —— 继承了 raw,没换行也到货
  孩子B(没设过任何东西)读到 n=1 (b) —— 继承了 raw,没换行也到货
```

Child A exited, lflag went from 0x8a3b to 0xa30, and raw stayed behind as-is. The relay child B, which configured nothing, received the fed a and b each as its own delivery, handed over without waiting for any newline — it inherited a raw terminal. You have most likely seen this scene on a real terminal: a program crashes in raw mode, the terminal stops echoing, ^C stops working, and the only hope of rescue is `stty sane`. On our rig, rescuing it takes a single restore — but relying on human hands for the restore is not dependable; the right owner is a destructor. The formulation in archive e6 is this article's C++ landing point:

```cpp
class terminal_guard {
public:
    explicit terminal_guard(int fd, int restore_actions = TCSAFLUSH)
        : fd_(fd), restore_actions_(restore_actions) {
        if (tcgetattr(fd_, &saved_) != 0)
            throw std::runtime_error(std::string("tcgetattr: ") + std::strerror(errno));
    }
    void apply_raw() {
        termios t = saved_;
        cfmakeraw(&t);
        if (tcsetattr(fd_, TCSANOW, &t) != 0)
            throw std::runtime_error(std::string("tcsetattr: ") + std::strerror(errno));
    }
    ~terminal_guard() { tcsetattr(fd_, restore_actions_, &saved_); }
    terminal_guard(const terminal_guard&) = delete;
    terminal_guard& operator=(const terminal_guard&) = delete;

private:
    int fd_;
    int restore_actions_;
    termios saved_{};
};
```

At construction, `tcgetattr` photographs the scene and keeps it on file; `apply_raw` performs cfmakeraw on top of that baseline and issues it; the destructor restores using the action chosen at construction; and copying is simply deleted. You have seen this skeleton before in [the RAII article of Thinking Foundations](../../thinking/01-raii-paradigm.md): the resource is the terminal's termios state, acquisition is tcgetattr, release is tcsetattr, and exception safety again rests on the destructor running even during stack unwinding. The experiment schedules two paths for it — normal return and exception:

```text
[阶段B 有 guard,正常返回:析构还原]
  初始 lflag=0x8a3b ICANON=1 ECHO=1 ISIG=1
  孩子正常返回后 lflag=0x8a3b ICANON=1 ECHO=1 ISIG=1

[阶段C 有 guard,中途 throw:栈展开照样还原]
  初始 lflag=0x8a3b ICANON=1 ECHO=1 ISIG=1
  孩子抛异常被接住后 lflag=0x8a3b ICANON=1 ECHO=1 ISIG=1
  孩子报告的异常:模拟业务炸了
```

Look at lflag after both paths complete: back to 0x8a3b in both cases, bit-for-bit identical to the initial value. The business logic blew up; the terminal lives on just fine.

One more choice hides in the constructor parameter: tcsetattr's third argument has three values in total. Besides the TCSAFLUSH and TCSANOW we used, the third is called TCSADRAIN — it waits for already-written output to drain before taking effect, which a terminal restore has no use for. The two we did use differ in whether the restore flushes unread input: TCSAFLUSH, when flushing, also waits for output to drain, but this article's output volumes are small, so the scene cannot show it. Phase D stages the comparison deliberately: the child holds the raw terminal and sleeps 200ms; meanwhile we stuff 4 junk bytes into the queue with nobody reading; when the child throws and the guard takes the state back, the relay reading end waits for lines in the restored canonical mode:

```text
[阶段D1 guard 用 TCSAFLUSH:还原时冲掉没读的输入]
  接力读端读到 n=2 (z
) —— junk 被 TCSAFLUSH 冲了

[阶段D2 guard 用 TCSANOW:还原时保留没读的输入]
  接力读端读到 n=4 (junk) —— junk 滞留,混进了这一行
```

We pasted D1's two lines exactly as they originally looked; the line broken in the middle is the \n that was read, nothing else.

D1 flushed the junk, and the relay reading end received only the z\n we fed afterwards. In D2 the junk was kept — and please note the delivery style: 4 bytes of junk, then we appended only z\n as the line terminator, yet the relay's single read got n=4 of junk, and z\n never entered that read at all. Canonical's wait-for-line condition only constrains bytes received after the switch; inventory already lying in the queue before the switch becomes, at the moment of switching back to canonical, a delivery ready on the spot — line terminator waived. That is why the guard's restore defaults to TCSAFLUSH: it guards against bytes stranded during raw. Those bytes originate from the program's own key-press protocol, and they will mix verbatim into the first read after the switch — and the one taking over afterwards is a program like the shell that trusts line contents.

## Next Stop: pty, and the Console on the Other Side

This article's rig used openpty as apparatus for six rounds; the next article turns to the apparatus itself: why the master can feed keys, why the slave counts as a terminal, what each of the four steps from `posix_openpt` through `grantpt`, `unlockpt`, and `ptsname` does, how one line of `forkpty` replaces a dozen of ours, and how a session gets recorded into a typescript plus timing log and played back one-to-one — those are the next article's main topics. The final ownership of echo and line editing from E2 and E5 — the question of services belonging to the tty layer with neither the reading program nor the shell involved — will be examined there by feeding the same byte string through a pipe and through a pty side by side, which is sturdier than our pronouncing judgment here.

The Windows side has no termios; the same kind of panel is called console mode, `SetConsoleMode` sets it per handle, with line mode, echo, and processed input each occupying one switch bit, and the Windows-side chapter of this volume is also on the books as ch06 in the [master outline](../../00-overview.md). How ^C becomes an event delivered to a process on that side was covered in the Windows process article [Console Events and APC](../../windows/process/02-console-apc.md); the measured behavior of the console mode bits we leave to this volume's Windows-side console article. As for the termios panel — today we have personally flipped every one of its switches.
