---
title: "Frame Pointer Omitted, Symbols Stripped: Unwind Tables and addr2line"
chapter: 9
order: 2
tags: [host, cpp-modern, intermediate]
difficulty: intermediate
platform: host
cpp_standard: [11, 17]
description: "After -O2 drops the frame pointer, rbp still holds libc's leftover value, bt stays alive on the unwind tables in .eh_frame, and once strip removes the symbols, addr2line trades bare addresses back for function names by subtracting the load base"
reading_time_minutes: 22
translation:
  source: documents/vol10-open-lecture-notes/cpu-book/09-manual-backtrace-with-gdb/02-no-frame-pointer-no-symbols.md
  source_hash: 6ba524a74ed31cd3cae3013969e26cdcbf0523b73741cc7dfd46566ebc0cafec
  translated_at: '2026-10-14'
  engine: manual
current_status:
  title: Being polished
  detail: Freshly written in this batch; still being read through and polished. Wording and measured claims may change.
  variant: polishing
---

# Frame Pointer Omitted, Symbols Stripped: Unwind Tables and addr2line

Last time, on an -O0 build that still carried the frame pointer, we climbed the rbp chain by hand: read the two slots at each level, and the whole chain meshed with bt's output level for level. The environment and the session annotation conventions are the same as in [the previous piece](01-bt-baseline-and-rbp-chain.md), so follow that one if you want to rerun anything. This time we pull the frame pointer out and look at what the rbp chain actually climbs into and what bt lives on without it. Then we strip the symbols too, and see how to trade bare addresses back for names with addr2line.

## After -O2: The Frame Pointer Is Gone, bt Still Works

We read in the stack-frames piece that `-fomit-frame-pointer`, the switch governing this, is on by default from -O1 onward, and that -O2 functions no longer carry the whole `push rbp` routine. The frame-pointer chain is broken, yet bt still hands back a result, and this section is about finding what it depends on. Before the demo, though, one prerequisite fact has to be stated, or you will try it yourself and decide we're making things up.

Compile the very same chain.cpp straight at -O2, and the entire call chain disappears, without a shadow of even one level left:

```text
$ g++ -O2 -g -o chain_O2_plain chain.cpp
$ objdump -d --no-show-raw-insn chain_O2_plain | sed -n '/<main>:/,/^$/p'
0000000000001020 <main>:
    1020:	mov    $0x26,%eax
    1025:	ret
    1026:	cs nopw 0x0(%rax,%rax,1)
```

main's entire machine code is down to one instruction, `mov $0x26,%eax`. 0x26 is 38 in decimal, and working f1(10) through f2 and f3 lands on exactly 38. The value was folded away at compile time, and not one call was left standing. With no calls there is nothing for a backtrace to walk. So to see a build that omits the frame pointer while the call chain survives, we need a controlled variant that keeps all four functions making real calls under -O2:

```cpp
__attribute__((noinline)) int f3(int x) { return x * 3; }
__attribute__((noinline)) int f2(int x) { return f3(x + 1) + 1; }
__attribute__((noinline)) int f1(int x) { return f2(x + 1) + 1; }
int main(int argc, char **) { return f1(argc) + 1; }
```

The three `noinline`s block inlining, and swapping the argument for argc, which nobody knows until runtime, blocks constant folding. The `+ 1` in main is necessary too. I left it out of my first version and got burned: without the +1, main's call to f1 becomes a tail call, the -O2 call turns into a jmp, f1's return address points straight at main's caller, main's own frame never appears at all, and bt counts no main. With the +1 in place, main still has work of its own after the call, and the frame survives.

Let's build both, one at -O2 and one at -O0, then confirm from the disassembly that the calls are still there and the frame pointer is gone:

```text
$ g++ -O2 -g -o chain_ni_O2 chain_ni.cpp
$ g++ -O0 -g -o chain_ni_O0 chain_ni.cpp
$ objdump -d --no-show-raw-insn chain_ni_O2 | grep -A6 '<_Z2f3i>:\|<_Z2f2i>:\|<main>:'
0000000000001020 <main>:
    1020:	call   1150 <_Z2f1i>
    1025:	add    $0x1,%eax
    1028:	ret
    1029:	nopl   0x0(%rax)

0000000000001130 <_Z2f3i>:
    1130:	lea    (%rdi,%rdi,2),%eax
    1133:	ret
    1134:	nop

0000000000001140 <_Z2f2i>:
    1140:	add    $0x1,%edi
    1143:	call   1130 <_Z2f3i>
    1148:	add    $0x1,%eax
    114b:	ret

$ objdump -d --no-show-raw-insn chain_ni_O2 | sed -n '/^0000000000001150 <_Z2f1i>:/,/^$/p'
0000000000001150 <_Z2f1i>:
    1150:	add    $0x1,%edi
    1153:	call   1140 <_Z2f2i>
    1158:	add    $0x1,%eax
    115b:	ret
```

None of the four functions has a prologue anymore, and f3 is down to a bare lea plus ret. Now the -O0 build for contrast. The opening of the same f3 at -O0 is the standard sight we got used to in the stack-frames piece:

```text
$ objdump -d --no-show-raw-insn chain_ni_O0 | grep -A8 '<_Z2f3i>:'
0000000000001119 <_Z2f3i>:
    1119:	push   %rbp
    111a:	mov    %rsp,%rbp
    111d:	mov    %edi,-0x4(%rbp)
    1120:	mov    -0x4(%rbp),%edx
    1123:	mov    %edx,%eax
    1125:	add    %eax,%eax
    1127:	add    %edx,%eax
    1129:	pop    %rbp
```

Next we break on f3 in the -O2 build, look at bt's output, then climb the rbp chain the old way:

```text
Reading symbols from ./chain_ni_O2...
--- break f3 / run ---
(gdb) Breakpoint 1 at 0x1130: file chain_ni.cpp, line 1.
(gdb) Starting program: /tmp/cpu-book-09/chain_ni_O2
[Thread debugging using libthread_db enabled]
Using host libthread_db library "/usr/lib/libthread_db.so.1".

Breakpoint 1, f3 (x=x@entry=3) at chain_ni.cpp:1
1	__attribute__((noinline)) int f3(int x) { return x * 3; }
--- bt ---
(gdb) #0  f3 (x=x@entry=3) at chain_ni.cpp:1
#1  0x0000555555555148 in f2 (x=2) at chain_ni.cpp:2
#2  0x0000555555555158 in f1 (x=<optimized out>) at chain_ni.cpp:3
#3  0x0000555555555025 in main (argc=<optimized out>) at chain_ni.cpp:4
--- p/x $rbp ---
(gdb) $1 = 0x7fffffffd190
--- x/gx $rbp ---
(gdb) 0x7fffffffd190:	0x00007fffffffd1f0
--- x/gx $rbp+8 ---
(gdb) 0x7fffffffd198:	0x00007ffff78278b9
```

bt shows the arguments as `x=x@entry=3` and `<optimized out>`, and one remark belongs here: this is gdb's normal shape under -O2. An argument is readable only in the instant the function is entered; after that the register is put to other uses, and we don't treat it as anything out of the ordinary. bt itself comes back with all four levels intact, and every address it reports matches the disassembly. f2's call sits at 0x1143, the instruction after the call is at 0x1148, and adding the load base 0x555555554000 gives exactly the 0x555555555148 of bt #1. f1's call returns to 0x1158, which is bt #2. main's call returns to 0x1025, which is bt #3. Counted level by level, every one of them lines up.

Now look at the three numbers from our manual climb and something smells wrong. $rbp holds 0x7fffffffd190; its two slots hold 0x7fffffffd1f0 and 0x00007ffff78278b9. Not one of the three numbers matches any of bt's four levels. Yet the climb ran through without a hitch: no errors anywhere, no garbage in sight, and every value read back was a perfectly honest stack address. This is the most important observation in the piece, so let me put it strongly: the frame-pointer chain fails in a peculiar way. It doesn't hand you junk values. Climb it, and the path you walk is a chain that really exists and is entirely wrong.

Whose chain did we climb? Let's ask `info symbol`, whose whole job is to say which symbol an address falls inside. Besides the 0x7ffff78278b9 we just read, I also looked up the fourth hop 0x7ffff7827781 from the -O0 session's table, to see what identity the symbol table gives it:

```text
Reading symbols from ./chain_ni_O2...
--- break f3 / run (same output as the previous session, omitted) ---
--- info symbol 0x7ffff78278b9 ---
(gdb) __libc_start_main + 137 in section .text of /usr/lib/libc.so.6
--- info symbol 0x7ffff7827781 ---
(gdb) No symbol matches 0x7ffff7827781.
--- p/x $rbp ---
(gdb) $1 = 0x7fffffffd190
--- set $r = *(unsigned long *)$rbp (no output), then p/x $r ---
(gdb) (gdb) $2 = 0x7fffffffd1f0
--- x/gx $r+8 ---
(gdb) 0x7fffffffd1f8:	0x0000555555555055
--- info symbol *(unsigned long *)($r+8) ---
(gdb) _start + 37 in section .text of /tmp/cpu-book-09/chain_ni_O2
```

There's the answer. The 0x7ffff78278b9 we looked up is `__libc_start_main + 137`; 0x555555555055 reports as the program's own `_start + 37`; and the middle one, 0x7ffff7827781, which matched no symbol, falls inside some function within libc that has no exported name. Climbing the chain outward, what we reached was libc's own startup frames. f3, f2, f1, main: not one frame of user code is on the chain. There's a cross-check that makes this even clearer: the value of $rbp, 0x7fffffffd190, is exactly the [$rbp] of main's row in the earlier -O0 session's table. libc's startup function set rbp to 0x7fffffffd190, and not one of f3, f2, f1, main ever touched rbp, so what we read when the breakpoint landed in f3 was still the old value they left behind. It's valid. It points at a real frame. And it is not your call chain.

Take a moment to appreciate how dangerous this failure mode is. If rbp held garbage, two reads would turn up an absurd address and you would grow suspicious on your own. But the climb runs clean the whole way, every hop lands on a legitimate address, and `info symbol` even produces a respectable name. An unsuspecting you would most likely take it for the answer. Garbage raises the alarm for you; a wrong chain doesn't.

So what does bt live on? Unwind information. When the stack-frames piece covered frame pointer omission, it had one line on this: to recover the call chain from the current instruction position, you compute it level by level from unwind data that records each function's stack layout, and the `.cfi` directives are where that data comes from. Now let's dig up the finished product. It lives in the `.eh_frame` section of the ELF file:

```text
$ readelf -S chain_ni_O2 | grep -i 'eh_frame'
  [13] .eh_frame_hdr     PROGBITS         0000000000002004  00002004
  [14] .eh_frame         PROGBITS         0000000000002038  00002038
```

`.eh_frame` is the runtime backtrace table the compiler emits, and `.eh_frame_hdr` is an index sitting in front of it that carries a lookup table. The table's contents are organized as CIE and FDE records, one of each per group. Here's the inventory:

```text
$ readelf -wf chain_ni_O2 | grep -n 'FDE\|CIE'
18:00000000 0000000000000014 00000000 CIE
30:00000018 0000000000000014 0000001c FDE cie=00000000 pc=0000000000001030..0000000000001056
38:00000030 0000000000000010 00000034 FDE cie=00000000 pc=0000000000001130..0000000000001134
43:00000044 0000000000000010 00000048 FDE cie=00000000 pc=0000000000001140..000000000000114c
48:00000058 0000000000000010 0000005c FDE cie=00000000 pc=0000000000001150..000000000000115c
53:0000006c 0000000000000010 00000070 FDE cie=00000000 pc=0000000000001020..0000000000001029
```

Each FDE covers a stretch of code with its pc range: the one starting at 0x1130 covers f3, the one at 0x1140 covers f2, the one at 0x1150 covers f1, and the one at 0x1020 covers main. Every function has its own record, and not one function is missing. The CIE holds the default rules they all share, and it happens to be short, so let's read the whole thing:

```text
$ readelf -wf chain_ni_O2 | sed -n '/^00000000 .* CIE/,/^$/p'
00000000 0000000000000014 00000000 CIE
  Version:               1
  Augmentation:          "zR"
  Code alignment factor: 1
  Data alignment factor: -8
  Return address column: 16
  Augmentation data:     1b
  DW_CFA_def_cfa: r7 (rsp) ofs 8
  DW_CFA_offset: r16 (rip) at cfa-8
  DW_CFA_nop
  DW_CFA_nop
```

The two instructions at the end together are the body of the recipe, so let's translate them into plain words. `DW_CFA_def_cfa: r7 (rsp) ofs 8` says: when we unwind a frame, the reference address, the CFA, equals $rsp plus 8. `DW_CFA_offset: r16 (rip) at cfa-8` says: the return address is stored at CFA minus 8. Put the two together and the return address lives at [$rsp]. Now f3's FDE for comparison (the command takes lines 30 through 48; the output also contains FDEs for 0x1030, 0x1140 and others, and I've kept only the 0x1130 segment for f3, cutting the rest):

```text
$ readelf -wf chain_ni_O2 | sed -n '30,48p'
00000030 0000000000000010 00000034 FDE cie=00000000 pc=0000000000001130..0000000000001134
  DW_CFA_nop
  DW_CFA_nop
  DW_CFA_nop
```

f3's FDE is nothing but three nops, meaning it rides the CIE's default rules the whole way. f3 is a leaf function: not one push in its body, the return address is what sits on top of the stack, and that matches the CFA recipe's [$rsp] exactly. When gdb stops in f3, one read of $rsp hands over the return address back into f2, and rbp was never touched. The FDEs of f2, f1 and main are just as short; their nop lines are left out below, and that is where I cut readelf's raw output. Whenever the stack layout around a call changes inside a function body, the FDE adds a correction at the matching position; the unwinder looks up the table by instruction address, applies the matching formula level by level, and computes the call chain out of a build with no frame pointer. Those `.cfi_*` directives we dropped from the listings in the stack-frames piece are assembled into the ELF, and the table in front of us is what they become.

One more relationship is worth writing down: `.eh_frame` is an ELF runtime backtrace table, and it does not count as debug info. When C++ throws an exception and hunts for the catch, this is the table it walks; when stack unwinding tears down objects level by level, this is the table it walks too. We'll need that identity in the next section, right away.

## After strip: bt Turns into Bare Addresses

bt's output carries two things: the shape of the call chain, and the names hung on each level. The first comes from the records on the stack and the unwind tables; the second comes from the symbol table. Let's strip the symbol table and watch what each of the two turns into. We take the chain_O0 from the previous piece's baseline section, copy it, and strip the copy:

```text
$ cp chain_O0 chain_O0s
$ strip chain_O0s
$ file chain_O0 chain_O0s
chain_O0:  ELF 64-bit LSB pie executable, x86-64, version 1 (SYSV), dynamically linked, interpreter /lib64/ld-linux-x86-64.so.2, BuildID[sha1]=c35dfbc2d99926338b8d56a9fc1fdb63c82f4333, for GNU/Linux 4.4.0, with debug_info, not stripped
chain_O0s: ELF 64-bit LSB pie executable, x86-64, version 1 (SYSV), dynamically linked, interpreter /lib64/ld-linux-x86-64.so.2, BuildID[sha1]=c35dfbc2d99926338b8d56a9fc1fdb63c82f4333, for GNU/Linux 4.4.0, stripped

$ nm chain_O0 | grep ' T '
0000000000001178 T _fini
0000000000001000 T _init
0000000000001165 T main
0000000000001020 T _start
0000000000001148 T _Z2f1i
000000000000112b T _Z2f2i
0000000000001119 T _Z2f3i

$ nm chain_O0s 2>&1 | head -3
nm: chain_O0s: no symbols
```

The two files share a BuildID; strip deleted content without moving the layout. f3's link-time address is 0x1119, so base 0x555555554000 plus 0x1119 gives the runtime address 0x555555555119, and a breakpoint can now only be placed at that address: with the symbols gone, breaking by function name gets refused, and gdb answers No symbol table is loaded, which is part of what strip costs you. bt comes out looking like this:

```text
Reading symbols from ./chain_O0s...
(No debugging symbols found in ./chain_O0s)
--- break *0x555555555119 ---
(gdb) Breakpoint 1 at 0x555555555119
--- run ---
(gdb) Starting program: /tmp/cpu-book-09/chain_O0s
[Thread debugging using libthread_db enabled]
Using host libthread_db library "/usr/lib/libthread_db.so.1".

Breakpoint 1, 0x0000555555555119 in ?? ()
--- bt ---
(gdb) #0  0x0000555555555119 in ?? ()
#1  0x0000555555555143 in ?? ()
#2  0x0000555555555160 in ?? ()
#3  0x0000555555555173 in ?? ()
#4  0x00007ffff7827781 in ?? () from /usr/lib/libc.so.6
#5  0x00007ffff78278b9 in __libc_start_main () from /usr/lib/libc.so.6
#6  0x0000555555555045 in ?? ()
```

Checked over, every name has turned into `??`, but the shape of the chain is intact: the three return addresses at #1 through #3 are identical, one by one, to the matching levels of the unstripped bt in the previous piece's baseline section, and the #0 stop was placed differently on the two sides, so I leave it out of the comparison. The backtrace itself came through essentially unaffected, which confirms again what the last section concluded: bt finds the call chain through `.eh_frame`, and what strip removed was `.symtab` and the debug info, not that table. Only one thing was lost: the lookup that turns an address back into a name.

Two details deserve a stop. The first is that #5 still has a name. libc is a shared library, and its exported symbols live in `.dynsym`, the table dynamic linking needs at runtime; strip removed only the main program's own `.symtab` and has no say over libc. We did check this program's `.dynsym`: not one user function in it:

```text
$ nm -D chain_O0s
                 w __cxa_finalize@GLIBC_2.2.5
                 w __gmon_start__
                 w _ITM_deregisterTMCloneTable
                 w _ITM_registerTMCloneTable
                 U __libc_start_main@GLIBC_2.34
```

The second detail is more counterintuitive: after strip, bt actually gets longer, gaining levels #4 through #6 and reaching all the way down to `_start`. Delete something and the output grows. That is not the backtrace getting stronger. This is the moment to collect the question the previous piece's baseline section left open, and say why bt stops at main. gdb has a setting called `backtrace past-main`, off by default; the [Backtrace manual entry, settings section](https://sourceware.org/gdb/current/onlinedocs/gdb.html/Backtrace.html) (retrieved 2026-10-12) reads "Backtraces will stop when they encounter the user entry point", and the very next sentence, This is the default, is about that default. Stop when it reaches the user entry point: that's the default behavior. But the sentence hides a premise: recognizing where main sits takes symbols. With the symbols gone, gdb can no longer recognize the user entry point, and that is how the walk ran on to `_start`. We verified this by turning the setting on for the unstripped build (the session stops at the same f3 breakpoint; the break and run output is the same as the previous piece's baseline section, so I won't paste it again):

```text
--- set backtrace past-main on (no output) ---
(gdb) (gdb) #0  f3 (x=12) at chain.cpp:1
#1  0x0000555555555143 in f2 (x=11) at chain.cpp:2
#2  0x0000555555555160 in f1 (x=10) at chain.cpp:3
#3  0x0000555555555173 in main () at chain.cpp:4
#4  0x00007ffff7827781 in ?? () from /usr/lib/libc.so.6
#5  0x00007ffff78278b9 in __libc_start_main () from /usr/lib/libc.so.6
#6  0x0000555555555045 in _start ()
```

bt gave seven frames, and every level's address matches the stripped version exactly. So the three extra levels in the stripped run are not new information; more of the same call chain has come into view. The episode also happens to tell us a less conspicuous fact: where bt stops turns out to be decided by symbols too.

The names are gone, so how do we get them back? There are two roads to recovery, and we start with addr2line. It translates addresses back into function names and line numbers, working from a file that carries debug info, and two prerequisites have to be stated.

The first thing to deal with is PIE. That pie in file's output means position-independent executable: it has no code addresses fixed at link time, and the operating system picks the load base at runtime. gdb turns randomization off, so the base settles at 0x555555554000, but addr2line looks up link-time addresses in the file and knows nothing about any base. The addresses we feed it have to have the base subtracted by hand. Here are the four subtractions, one per line:

```text
$ for a in 0x555555555119 0x555555555143 0x555555555160 0x555555555173; do printf '%s - 0x555555554000 = 0x%x\n' $a $((a - 0x555555554000)); done
0x555555555119 - 0x555555554000 = 0x1119
0x555555555143 - 0x555555554000 = 0x1143
0x555555555160 - 0x555555554000 = 0x1160
0x555555555173 - 0x555555554000 = 0x1173
```

Writing the subtraction as an explicit step is a habit I bought with one failure. I reduced 0x555555555143 to 0x143, dropping one digit, and addr2line returned `??` for the whole batch without a single word of complaint. A `??` has two possible origins here: an address subtracted wrong, or a file with no debug info in it. It hands you the same `??` for both and won't tell you which you've got. So we keep a control experiment, and queried the same batch of addresses against the stripped build:

```text
$ addr2line -e chain_O0 -f -C 0x1119 0x1143 0x1160 0x1173
f3(int)
/tmp/cpu-book-09/chain.cpp:1
f2(int)
/tmp/cpu-book-09/chain.cpp:2 (discriminator 1)
f1(int)
/tmp/cpu-book-09/chain.cpp:3 (discriminator 1)
main
/tmp/cpu-book-09/chain.cpp:4 (discriminator 1)

$ addr2line -e chain_O0s -f -C 0x1119 0x1143 0x1160 0x1173
??
??:0
??
??:0
??
??:0
??
??:0
```

Queried against the unstripped chain_O0, the four addresses turn back into four function names with line numbers, matching what bt reported level for level. Queried against the stripped build, the whole batch comes back as `??`. The discriminator after a line number is an extra tag in the line table; it doesn't get in the way of reading the line number. Which brings us to the second prerequisite: the file `-e` points at must contain debug info. Each prerequisite fails in its own way, and both failures look identical: subtract the base wrong and you get `??`, point at the wrong file and you get `??` as well. With the arithmetic laid out and the control run done, neither failure can hide anymore. The trouble at both ends, the frame pointer and the symbols, is now dealt with.
