---
title: "Linux Terminals and the Console"
sidebar_order: 60
description: "termios's four flag groups and raw mode; pseudoterminals (PTY) and process interaction"
platform: host
tags:
  - cpp-modern
  - host
  - intermediate
  - 系统编程
  - POSIX
translation:
  source: documents/vol8-domains/systems-programming/linux/terminal/index.md
  source_hash: f72f0587f45fe6e98d77ef3d4d73378e4d71a07e41a423d01854550387c07dce
  translated_at: '2026-11-22T15:44:37+00:00'
  engine: glm-5.3
  token_count: 1675
---
# Linux Terminals and the Console

This terminal chapter starts from the question of what entire layer sits between the keyboard and read: the first article flips termios's four flag groups bit by bit — canonical's line buffering, ^C's signal translation, the VMIN/VTIME interbyte timer — all backed by measurements from a self-built pty rig. The second article moves the rig itself onto center stage, walking from the four posix_openpt steps to recording and replaying a terminal session. The series-wide common utilities follow the definitions in the two [Thinking Foundations](../../thinking/) articles.

<ChapterNav variant="sub">
  <ChapterLink num="1" href="01-termios-raw" desc="Line editing, echo, and translating ^C into a signal are all done for your program by the tty layer in the kernel; termios's four flag groups are the switch panel of that machinery, and every experiment runs on a self-built pty rig with zero contact with the main terminal: a factory-settings census (why cfmakeraw leaves ONLCR alone, the minimal hand-rolled raw differing from it in four places — ISIG/IXON/ICRNL/OPOST — the master end also being a tty sharing the line discipline with the slave, and since glibc 2.42 the B constants being the real rate values), canonical idling until the newline to deliver 5 bytes in one stroke at 600.6ms versus raw delivering block for block as fed, ^C's flush and signal delivery doable one without the other (SIGINT=0 with no foreground group, =1 after the three steps setsid+TIOCSCTTY+tcsetpgrp), ^D's non-sticking EOF, VMIN/VTIME's D+F joint verdict that the interbyte timer resets on every byte received (4@600.7ms and 2@955.7ms cutting down the counts-from-first-byte account), ECHO/OPOST/ICRNL bit by bit, and the RAII terminal_guard with TCSAFLUSH flushing leftover input while TCSANOW keeps it and delivers without a line terminator">termios and Raw Mode</ChapterLink>
  <ChapterLink num="2" href="02-pty-recording" desc="The previous article's test rig walks to center stage: the four hand-rolled steps posix_openpt/grantpt/unlockpt/ptsname (/dev/ptmx 5:2 matching the man page to the letter, node birth and death tracking the master, open on the slave before unlockpt returning EIO rather than EACCES, grantpt running on empty with devpts doing the chore on this machine), a forkpty one-stop child-side acceptance check, the same six bytes abc DEL z fed to pipe and pty for a byte-identical comparison (an architecture lesson assigning line editing and echo to the tty layer), a minimal script(1) re-implementation (a 138-byte typescript opening with sh 5.3's bracketed-paste mode, the single exit input echoed once at each of three layers and attributed via the timing log's layer-by-layer timestamps, closing with read(master)=EIO), playback on the timing schedule (1257.1ms against 1255.8ms, cmp byte-identical, 629.0ms at double speed), winsize 0x0 and TIOCSWINSZ driving the SIGWINCH count from 1 to 2, the exclusive criterion for the master's read/write asymmetry (EIO requires an empty queue plus a closed peer — write succeeding proves nothing about the far side), mirroring the daemon article's SIGHUP after close(master)">Pseudoterminals and Process Interaction</ChapterLink>
</ChapterNav>

Both Linux-side articles of the terminal chapter are in place; the next stop is the Windows console, aligned with the learning route in the [master outline](../../00-overview.md).
