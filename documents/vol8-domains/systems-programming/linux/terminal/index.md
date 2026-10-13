---
title: "Linux 终端与控制台"
sidebar_order: 60
description: "termios 四组标志与 raw 模式、伪终端(PTY)与进程交互"
platform: host
tags:
  - cpp-modern
  - host
  - intermediate
  - 系统编程
  - POSIX
---

# Linux 终端与控制台

终端这一章咱们从键盘和 read 之间隔着一整层什么问起:第一篇把 termios 的四组标志逐位拨过,canonical 的行缓冲、^C 的信号翻译、VMIN/VTIME 的字符间计时器,全部用的是自建 pty 台架的实测。第二篇咱们把台架本身请到台前,从 posix_openpt 四步走到终端会话的录制与回放。全系列公共工具沿用[思维基石](../../thinking/)两篇的定义。

<ChapterNav variant="sub">
  <ChapterLink num="1" href="01-termios-raw" desc="行编辑、回显、把 ^C 翻译成信号,全是内核里 tty 这层替您的程序做的,termios 四组标志就是这台机构的开关面板,全部实验跑在自建 pty 台架上、主终端零接触:出厂普查(cfmakeraw 不清 ONLCR 的理由、手工最小 raw 与它差 ISIG/IXON/ICRNL/OPOST 四处、master 端也是 tty 且与 slave 共享行规程、glibc 2.42 起 B 常量就是真实速率值),canonical 干等换行 600.6ms 一笔交 5 字节对 raw 喂几块到几块,^C 的冲队与递信号可以只做一件(无前台组时 SIGINT=0,setsid+TIOCSCTTY+tcsetpgrp 三步后=1),^D 的 EOF 不粘,VMIN/VTIME 的 D+F 合判字符间计时器每收一字节重置(4@600.7ms 与 2@955.7ms 裁掉首字节起算的说法),ECHO/OPOST/ICRNL 逐位,RAII terminal_guard 与 TCSAFLUSH 冲滞留输入、TCSANOW 保留且免行结束符交货">termios 与 raw 模式</ChapterLink>
  <ChapterLink num="2" href="02-pty-recording" desc="上一篇的实验台走到台前:posix_openpt/grantpt/unlockpt/ptsname 四步手搓(/dev/ptmx 5:2 对 man 一字不差、节点生灭跟着 master、unlockpt 前 open slave 报 EIO 而非 EACCES、grantpt 在本机跑空由 devpts 代劳),forkpty 一条龙的孩子侧验收,同串 abc DEL z 六字节管道对 pty 的同字节对照(行编辑与回显归 tty 层的架构归属课),script(1) 最小复刻(138 字节 typescript 开头是 sh 5.3 的括号粘贴模式、exit 一次输入在三层各回显一次,由 timing 的逐层时刻对出来、收场 read(master)=EIO),按 timing 时刻表回放(1257.1ms 对 1255.8ms、cmp 逐字节一致、2 倍速 629.0ms),winsize 0x0 与 TIOCSWINSZ 的 SIGWINCH 计数 1 到 2,master 读写不对称的独家判据(EIO 要队列空加对端关两条件齐,write 成功证明不了对端在),与 daemon 篇 close(master) 后 SIGHUP 互为镜像">伪终端与进程交互</ChapterLink>
</ChapterNav>

终端章的两篇 Linux 侧都齐了,下一站 Windows 的控制台,路线与[总纲](../../00-overview.md)的学习路线对齐。
