---
title: "bt's Normal Output and a Manual Backtrace Along the rbp Chain"
chapter: 9
order: 1
tags: [host, cpp-modern, intermediate]
difficulty: intermediate
platform: host
cpp_standard: [11, 17]
description: "Read bt and info frame output in the -O0 baseline, pin down the two slots that hold rbp and the return address, then walk the rbp chain by hand with memory-reading commands and match bt level for level"
reading_time_minutes: 14
translation:
  source: documents/vol10-open-lecture-notes/cpu-book/09-manual-backtrace-with-gdb/01-bt-baseline-and-rbp-chain.md
  source_hash: c41c2e671f3e3faf2e92b050a0d4553e10c878981bf3ef8a43ae96b651736a4d
  translated_at: '2026-10-14'
  engine: manual
current_status:
  title: Being polished
  detail: Freshly written in this batch; still being read through and polished. Wording and measured claims may change.
  variant: polishing
---

# bt's Normal Output and a Manual Backtrace Along the rbp Chain

In [the opening piece of 02](../02-function-calls-and-stack-frames/01-call-and-stack-frame-walkthrough.md) we followed a single call and ret pair through the whole life of one function call: how the return address lands on the stack, and how the prologue stands the frame up. On the debugger side we have used bt as well; in 02's recursion-overflow section we ran bt 5 to look at the top 5 frames, and the stack's full depth of 260,000 frames was something we calculated. Otherwise bt has stayed a black box in our hands: you press Enter, and it lines up function names with line numbers as if it were born knowing who called whom. This piece opens that box and does its work with our own hands. As for the places where bt runs out of moves, when the frame pointer is optimized away, when the symbols are stripped, the later pieces of this topic pick those up.

Here is the route. In a build that keeps the frame pointer, the call chain sits on the stack as a ready-made linked list; we climb it once by hand with two memory-reading commands, then compare the climb against bt's output level by level. -O2 throws the frame pointer away, yet bt keeps working perfectly well, and we will find out what it lives on. Names get their own treatment: for the bare addresses strip leaves behind, addr2line buys the function names back. Finally we put the three together against a crashed, optimized, stripped program and do one complete piece of forensics. Across the first eight pieces the lead role mostly belonged to assembly; in this one it passes to gdb, and if you type along, every step reproduces on your own machine.

Every session and every line of output you see in this piece comes verbatim from real runs on my machine, and the examples and experiments are all original to this site.

## Our Test Environment

As usual the environment comes first, so that every block of output below is something you can reproduce on a machine with the same configuration:

```text
$ g++ --version
g++ (GCC) 16.2.1 20260810
Copyright (C) 2026 Free Software Foundation, Inc.
This is free software; see the source for copying conditions.  There is NO
warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

$ gdb --version | head -1
GNU gdb (GDB) 17.2

$ uname -a
Linux DESKTOP-65DBAA7 6.18.33.2-microsoft-standard-WSL2 #1 SMP PREEMPT_DYNAMIC Thu Jun 18 21:54:43 UTC 2026 x86_64 GNU/Linux
```

A few more facts that affect how you read the output, taken one at a time.

- The sessions all run non-interactively, with the commands fed to gdb through a heredoc, like the block below. The `(gdb)` prompt is printed by gdb itself and the commands are never echoed back, so in the session blocks I have added annotation lines of the form `--- command ---` marking which command each stretch of output came from. Every character outside those annotation lines is verbatim, and wherever output was trimmed, the trim is noted.

```text
DEBUGINFOD_URLS= gdb -q ./chain_O0 <<'EOF'
break f3
run
bt
EOF
```

- The `DEBUGINFOD_URLS=` at the very front of the command line is not a gdb argument; it empties that environment variable so that gdb 17.2 skips the debuginfod network query it makes at startup. It saves four lines of noise and has no other effect on backtrace behavior.
- gdb disables address space randomization by default, so the PIE program loads at 0x555555554000 on every run and libc lands somewhere in the 0x7ffff7xxxxxx range on every run. Addresses from different sessions can be read directly against each other, and we will make repeated use of that.
- At the end of every session gdb asks whether you really want to quit, and in non-interactive mode it answers Y on its own. Those closing prompts are uniformly removed from the later sessions, noted here once.
- The discipline for crash forensics follows [02's precedent](../02-function-calls-and-stack-frames/03-kept-frames-and-stack-exhaustion.md): evidence comes from gdb's `Program received signal` line, and at the non-interactive shell level only the exit code is visible.

## What bt's Normal Output Looks Like

We start from the cleanest baseline there is: four functions strung into a chain, the calls nesting one level at a time from main down to f3. Here is the source:

```cpp
int f3(int x) { return x * 3; }
int f2(int x) { return f3(x + 1) + 1; }
int f1(int x) { return f2(x + 1) + 1; }
int main() { return f1(10); }
```

We compile with -O0 plus -g, drop the breakpoint on f3, run, and watch what bt prints:

```text
$ g++ -O0 -g -o chain_O0 chain.cpp
```

```text
Reading symbols from ./chain_O0...
--- break f3 ---
(gdb) Breakpoint 1 at 0x1120: file chain.cpp, line 1.
--- run ---
(gdb) Starting program: /tmp/cpu-book-09/chain_O0
[Thread debugging using libthread_db enabled]
Using host libthread_db library "/usr/lib/libthread_db.so.1".

Breakpoint 1, f3 (x=12) at chain.cpp:1
1	int f3(int x) { return x * 3; }
--- bt ---
(gdb) #0  f3 (x=12) at chain.cpp:1
#1  0x0000555555555143 in f2 (x=11) at chain.cpp:2
#2  0x0000555555555160 in f1 (x=10) at chain.cpp:3
#3  0x0000555555555173 in main () at chain.cpp:4
```

That is what normal looks like: bt hands over four levels, each carrying a function name, the argument values, and a file and line number. Let's state the rules precisely. [The Frames entry of the gdb manual](https://sourceware.org/gdb/current/onlinedocs/gdb.html/Frames.html) (retrieved 2026-10-12) defines a frame as "each frame is the data associated with one call to one function". Turned back into plain words: a frame is exactly the data bound to one call of one function.

When the program has just started, the stack holds only main's frame. Every call adds one more layer, and every return takes one away. bt's numbering rule puts frame zero at the innermost level, the one execution is currently stopped in; frame 1 is the level that called it, and the numbers grow as we count outward. [The Backtrace entry of the manual](https://sourceware.org/gdb/current/onlinedocs/gdb.html/Backtrace.html) (retrieved 2026-10-12) opens with "A backtrace is a summary of how your program got where it is", and that is precisely the question it answers: how the program arrived at this very step.

If you were paying attention, you noticed bt stops once it reaches main. libc's startup code is obviously sitting above main calling into it, yet this output never climbs up there. We will park that question here and come back to it once we have covered the symbols section of the next piece.

Next, how much does gdb know about a single frame? `frame n` switches the current frame to level n, and `info frame` reports the full particulars of that frame in one dump. The manual describes it as printing a verbose description of the selected stack frame; [the Frame Info entry](https://sourceware.org/gdb/current/onlinedocs/gdb.html/Frame-Info.html) (retrieved 2026-10-12) reads "This command prints a verbose description of the selected stack frame". Let's put it to work. We queried frames 0, 1, and 3 in turn:

```text
--- frame 0 ---
(gdb) #0  f3 (x=12) at chain.cpp:1
1	int f3(int x) { return x * 3; }
--- info frame ---
(gdb) Stack level 0, frame at 0x7fffffffd0b0:
 rip = 0x555555555120 in f3 (chain.cpp:1); saved rip = 0x555555555143
 called by frame at 0x7fffffffd0c8
 source language c++.
 Arglist at 0x7fffffffd0a0, args: x=12
 Locals at 0x7fffffffd0a0, Previous frame's sp is 0x7fffffffd0b0
 Saved registers:
  rbp at 0x7fffffffd0a0, rip at 0x7fffffffd0a8
--- frame 1 ---
(gdb) #1  0x0000555555555143 in f2 (x=11) at chain.cpp:2
2	int f2(int x) { return f3(x + 1) + 1; }
--- info frame ---
(gdb) Stack level 1, frame at 0x7fffffffd0c8:
 rip = 0x555555555143 in f2 (chain.cpp:2); saved rip = 0x555555555160
 called by frame at 0x7fffffffd0e0, caller of frame at 0x7fffffffd0b0
 source language c++.
 Arglist at 0x7fffffffd0b8, args: x=11
 Locals at 0x7fffffffd0b8, Previous frame's sp is 0x7fffffffd0c8
 Saved registers:
  rbp at 0x7fffffffd0b8, rip at 0x7fffffffd0c0
--- frame 3 ---
(gdb) #3  0x0000555555555173 in main () at chain.cpp:4
4	int main() { return f1(10); }
--- info frame ---
(gdb) Stack level 3, frame at 0x7fffffffd0f0:
 rip = 0x555555555173 in main (chain.cpp:4); saved rip = 0x7ffff7827781
 caller of frame at 0x7fffffffd0e0
 source language c++.
 Arglist at 0x7fffffffd0e0, args:
 Locals at 0x7fffffffd0e0, Previous frame's sp is 0x7fffffffd0f0
 Saved registers:
  rbp at 0x7fffffffd0e0, rip at 0x7fffffffd0e8
```

In info frame's output, the two lines under `Saved registers:` carry the most weight. `rbp at 0x7fffffffd0a0` tells you the value $rbp held while this frame was executing, and `rip at 0x7fffffffd0a8` says where the return address is stored, sitting exactly 8 past the previous number, in the slot at $rbp+8. We met this layout back in 02's prologue discussion: `push rbp` stores the caller's rbp onto the stack, `mov rbp,rsp` sets the new stack top as the baseline, and so [rbp] holds the previous level's old rbp while [rbp+8] holds the return address.

Better still, the three outputs interlock. Look at frame 0's reported `saved rip = 0x555555555143`: it lands exactly on the address in bt's #1 line. frame 1's `saved rip = 0x555555555160` lines up with the address of bt #2. Following it through, the address bt displays at each level is the return address the level below it stored on its behalf. Even the fourth hop, which bt never shows, matches: frame 3's `saved rip = 0x7ffff7827781` is the return address main hands back to libc, it lives on the stack, and bt by default does not climb up there. All three tools are reading the same record on the stack, and this interlock is the footing for the hands-on work in the next section.

## Manual Backtrace: Leaving bt Out and Reading Each Level Ourselves

The last section established the two slots; this one leaves bt out entirely and reads the whole chain with nothing but two memory-reading moves. The recipe is simple: `p/x $rbp` shows rbp's own value, `x/gx $rbp` reads out the previous level's rbp, and `x/gx $rbp+8` reads out the return address. Once a level is climbed, the `set` command catches the previous rbp into a new gdb variable, and the two slot reads repeat against that variable. `set` produces no output, so the prompt shows up twice in a row in the session; the annotations point that out.

We are still on the same chain_O0, with the breakpoint stopped in f3 as before. The session is pasted below exactly as it ran (the break and run output at the top matches the last section, and the closing quit prompt has been removed):

```text
--- bt (for comparison) ---
(gdb) #0  f3 (x=12) at chain.cpp:1
#1  0x0000555555555143 in f2 (x=11) at chain.cpp:2
#2  0x0000555555555160 in f1 (x=10) at chain.cpp:3
#3  0x0000555555555173 in main () at chain.cpp:4
--- p/x $rbp ---
(gdb) $1 = 0x7fffffffd0a0
--- x/gx $rbp ---
(gdb) 0x7fffffffd0a0:	0x00007fffffffd0b8
--- x/gx $rbp+8 ---
(gdb) 0x7fffffffd0a8:	0x0000555555555143
--- set $rbp_f2 = *(unsigned long *)$rbp (no output), then p/x the new variable ---
(gdb) (gdb) $2 = 0x7fffffffd0b8
--- x/gx on the new variable, reading the +8 slot as well ---
(gdb) 0x7fffffffd0b8:	0x00007fffffffd0d0
(gdb) 0x7fffffffd0c0:	0x0000555555555160
--- set catching the rbp of f1's level (no output), then p/x the new variable ---
(gdb) (gdb) $3 = 0x7fffffffd0d0
--- reading its two slots ---
(gdb) 0x7fffffffd0d0:	0x00007fffffffd0e0
(gdb) 0x7fffffffd0d8:	0x0000555555555173
--- set catching main's level rbp (no output), then p/x the new variable ---
(gdb) (gdb) $4 = 0x7fffffffd0e0
--- reading its two slots ---
(gdb) 0x7fffffffd0e0:	0x00007fffffffd190
(gdb) 0x7fffffffd0e8:	0x00007ffff7827781
```

What the climb brought up, organized into a table and set beside bt's results:

| Level | $rbp of that level | [$rbp], the previous level's rbp | [$rbp+8], the return address | bt counterpart |
|---|---|---|---|---|
| f3 | 0x7fffffffd0a0 | 0x7fffffffd0b8 | 0x0000555555555143 | #1 f2 |
| f2 | 0x7fffffffd0b8 | 0x7fffffffd0d0 | 0x0000555555555160 | #2 f1 |
| f1 | 0x7fffffffd0d0 | 0x7fffffffd0e0 | 0x0000555555555173 | #3 main |
| main | 0x7fffffffd0e0 | 0x7fffffffd190 | 0x00007ffff7827781 | not shown by bt by default |

The three user-space return addresses 0x143, 0x160, and 0x173 (tail digits only here; the full values are in the table above) mesh with bt's #1 through #3 one for one. The fourth hop, 0x00007ffff7827781, is the return address main hands back to libc. bt never displayed it, but frame 3's `saved rip` in the last section reported exactly that number, so the two sources agree. One more pattern in the table deserves a second look: each level's [$rbp] turns out to be the $rbp of the level on the next row down. 0x7fffffffd0b8, which we read out as the caller's rbp on f3's row, is f2's own $rbp on the next row. That is precisely what `push rbp; mov rbp,rsp` leave behind once they execute. We read those two instructions in 02's assembly; once they land in memory they form exactly this singly linked list, each node holding the address of the next node along with the return address back into its own caller.

In a build with the frame pointer, the chain is intact at every level, and when gdb reconstructs a call chain, following it is a path already laid out. Our hand-climbed result agrees with bt word for word, and that agreement is the proof the walk works. You may have run across scripts elsewhere that decode a call stack by walking the rbp chain; what they do is exactly this string of moves. This climbing method has exactly one precondition: every function on the chain must have honestly executed push rbp.
