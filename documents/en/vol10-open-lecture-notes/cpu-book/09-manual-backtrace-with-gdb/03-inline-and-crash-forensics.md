---
title: "Inlining and Crash Forensics: Three Layers, Each Covering Its Own Stretch"
chapter: 9
order: 3
tags: [host, cpp-modern, intermediate]
difficulty: intermediate
platform: host
cpp_standard: [11, 17]
description: "Inlining costs bt one frame when no debug info is present, then the unwind table, the return addresses queued on the stack, and symbol-file come together for a complete forensic pass over a crashed -O2 build that has been stripped"
reading_time_minutes: 8
translation:
  source: documents/vol10-open-lecture-notes/cpu-book/09-manual-backtrace-with-gdb/03-inline-and-crash-forensics.md
  source_hash: 599e9fc29b195f3f770bd65db7b9179c4e904a9d617d5b1d60dd6ec4c4d5e44b
  translated_at: '2026-10-14'
  engine: manual
current_status:
  title: Being polished
  detail: Freshly written in this batch; still being read through and polished. Wording and measured claims may change.
  variant: polishing
---

# Inlining and Crash Forensics: Three Layers, Each Covering Its Own Stretch

In the previous piece we handed the omitted frame pointer over to the unwind table and the stripped symbols over to addr2line. The environment and the way we annotate the sessions are the same as in the [first piece](01-bt-baseline-and-rbp-chain.md); follow that piece if you want to rerun anything. Of the optimizations that reshape the call chain, one is still untested: inlining. In this piece we find out whether it costs bt a frame. Then we put the three things together and take on a crashed, optimized, stripped program for one complete piece of forensics.

## Does bt Lose a Frame After Inlining?

One more optimization reshapes the call chain: inlining. We read in 02 that the compiler expands a small function's body straight into the call site, and the call disappears. Intuition says that once the call is gone, that layer in bt should vanish with it. Let's measure it on the smallest example there is. Whether you carry debug info or not, the answer is different.

```cpp
int add_one(int x) { return x + 1; }
int main(int argc, char **) { return add_one(argc); }
```

```text
$ g++ -O0 -g -o inline_O0 inline.cpp
$ g++ -O2 -g -o inline_O2g inline.cpp
$ g++ -O2    -o inline_O2  inline.cpp
```

We start from the bt under -O0 -g. (The first session drops the opening Reading symbols, breakpoint, and run output and picks up from where the stop has settled; the middle session drops only the Reading symbols line; the last session keeps its opening untouched.)

```text
Breakpoint 1, add_one (x=1) at inline.cpp:1
1	int add_one(int x) { return x + 1; }
--- bt ---
(gdb) #0  add_one (x=1) at inline.cpp:1
#1  0x0000555555555141 in main (argc=1) at inline.cpp:2
```

bt gives two frames, one for add_one and one for main. That is our normal baseline. So what happens under -O2? Let's have the machine code speak:

```text
$ objdump -d --no-show-raw-insn inline_O2g | sed -n '/<main>:/,/ret/p'
0000000000001020 <main>:
    1020:	lea    0x1(%rdi),%eax
    1023:	ret

$ objdump -d --no-show-raw-insn inline_O2 | sed -n '/<main>:/,/ret/p'
0000000000001020 <main>:
    1020:	lea    0x1(%rdi),%eax
    1023:	ret

$ nm inline_O2 | grep 'add_one\|T main'
0000000000001020 T main
0000000000001130 T _Z7add_onei
```

main's body is down to a single `lea 0x1(%rdi),%eax`, and that is add_one's body; the call is gone. The machine code matches byte for byte with and without -g. Meanwhile add_one's standalone copy still sits at 0x1130, and even with nobody calling it, it still cannot be deleted. We read the reason in 02: it has external linkage, and another translation unit might take its address.

Now the bt from the two builds, the one with debug info first:

```text
--- break add_one ---
(gdb) Breakpoint 1 at 0x1020: add_one. (2 locations)
--- run ---
(gdb) Breakpoint 1.1, add_one (x=<optimized out>) at inline.cpp:1
1	int add_one(int x) { return x + 1; }
--- bt ---
(gdb) #0  add_one (x=<optimized out>) at inline.cpp:1
#1  main (argc=1) at inline.cpp:2
--- info frame ---
(gdb) Stack level 0, frame at 0x7fffffffd0f0:
 rip = 0x555555555020 in add_one (inline.cpp:1); saved rip = 0x7ffff7827781
 inlined into frame 1
 source language c++.
 Arglist at unknown address.
 Locals at unknown address, Previous frame's sp in rsp
```

bt still gives two frames; the add_one layer is still there. The shape has changed, though: neither #0 nor #1 carries the `0x... in` address prefix, because both levels share one real PC. `info frame` writes the verdict out directly: `inlined into frame 1`. This frame is a synthetic frame, inlined into frame 1. The breakpoint output tells the same story: `Breakpoint 1 at 0x1020: add_one. (2 locations)`. The period after the function name plus the two locations mean one location is the inlined instance inside main and the other is the standalone copy at 0x1130, and the one we stopped at is 1.1, the inlined instance in main. The synthetic frame stands on the inlining relationships recorded in the DWARF debug info; gdb reads them and puts the vanished layer back.

The build without debug info is blunter. Stop once more at the same PC and bt has a single frame left:

```text
Reading symbols from ./inline_O2...
(No debugging symbols found in ./inline_O2)
--- break by address ---
(gdb) Breakpoint 1 at 0x555555555020
--- run ---
(gdb) Breakpoint 1, 0x0000555555555020 in main ()
--- bt ---
(gdb) #0  0x0000555555555020 in main ()
```

So the claim has to be told per scenario. The real stack frame does lose one level: the machine code has no trace of a call anywhere, and that does not change with debug info. How many levels bt reports depends on what gdb has in hand. Without debug info it can only report physical frames, and two levels become one. With -g it reads the DWARF and synthesizes the inlined level back, so the count survives and the shape stays recognizable. When you next run into the line "inlining costs bt one frame", whoever said it was probably talking about the no-debug-info scenario.

## In Practice: A Stripped -O2 Program Crashes

When the three conditions line up, you are looking at a real crash scene: the program is built with -O2, the frame pointer is gone, and the symbols have been stripped on top of that. Let's build one on purpose. The trick for making a wild pointer is a left shift, which produces an illegal address whose value only exists at run time:

```cpp
__attribute__((noinline)) int f3(long *p) { *p = 42; return 0; }
__attribute__((noinline)) int f2(long *p) { return f3(p) + 1; }
__attribute__((noinline)) int f1(long *p) { return f2(p) + 1; }
int main(int argc, char **) {
  long *p = reinterpret_cast<long *>(static_cast<long>(argc) << 44);
  return f1(p) + 1;
}
```

We'll run it under gdb, where argc is 1, and 1 shifted left by 44 bits gives 0x100000000000. That value only shows itself at run time, so the compiler has no way to delete the store. We compile, copy, strip, then confirm by disassembly that the calls are all still there and the frame pointer is gone:

```text
$ g++ -O2 -g -o crash crash.cpp
$ cp crash crash.stripped
$ strip crash.stripped
$ objdump -d --no-show-raw-insn crash | sed -n '/^0000000000001130 <_Z2f3Pl>:/,/^$/p;/^0000000000001140 <_Z2f2Pl>:/,/^$/p;/^0000000000001150 <_Z2f1Pl>:/,/^$/p'
0000000000001130 <_Z2f3Pl>:
    1130:	movq   $0x2a,(%rdi)
    1137:	xor    %eax,%eax
    1139:	ret
    113a:	nopw 0x0(%rax,%rax,1)

0000000000001140 <_Z2f2Pl>:
    1140:	call   1130 <_Z2f3Pl>
    1145:	mov    $0x1,%eax
    114a:	ret
    114b:	nopl  0x0(%rax,%rax,1)

0000000000001150 <_Z2f1Pl>:
    1150:	call   1140 <_Z2f2Pl>
    1155:	mov    $0x2,%eax
    115a:	ret
```

For main's disassembly we only mention the opening: `shl $0x2c,%rdi` followed by one call; we won't paste the whole listing.

The forensics start now. Our first move is to get the stripped build running under gdb:

```text
Reading symbols from ./crash.stripped...
(No debugging symbols found in ./crash.stripped)
--- run ---
(gdb) Starting program: /tmp/cpu-book-09/crash.stripped
[Thread debugging using libthread_db enabled]
Using host libthread_db library "/usr/lib/libthread_db.so.1".

Program received signal SIGSEGV, Segmentation fault.
0x0000555555555130 in ?? ()
--- bt ---
(gdb) #0  0x0000555555555130 in ?? ()
#1  0x0000555555555145 in ?? ()
#2  0x0000555555555155 in ?? ()
#3  0x0000555555555029 in ?? ()
#4  0x00007ffff7827781 in ?? () from /usr/lib/libc.so.6
#5  0x00007ffff78278b9 in __libc_start_main () from /usr/lib/libc.so.6
#6  0x0000555555555055 in ?? ()
--- x/i $pc ---
(gdb) => 0x555555555130:	movq   $0x2a,(%rdi)
--- p/x $rdi ---
(gdb) $1 = 0x100000000000
--- x/12gx $rsp ---
(gdb) 0x7fffffffd0c0:	0x0000555555555145	0x0000555555555155
0x7fffffffd0d0:	0x0000555555555029	0x00007ffff7827781
0x7fffffffd0e0:	0x00007ffff7ecf248	0x00007fffffffd208
0x7fffffffd0f0:	0x0000000100000008	0x0000555555555020
0x7fffffffd100:	0x0000000000000000	0x0000000000000009
0x7fffffffd110:	0x0000000000000000	0x921a7316ea03ae4c
```

Five moves bought us five pieces of evidence. `Program received signal SIGSEGV` pins the crashing instruction at 0x555555555130. `x/i $pc` lays out the instruction that went wrong: `movq $0x2a,(%rdi)`, a store of 42 into [rdi], which lines up with `*p = 42` in the source exactly. `p/x $rdi` reports 0x100000000000, precisely the result of argc shifted left by 44 bits. bt comes back with seven bare addresses, and the shape follows the rule from the symbols section of 02: the name at #5 is being held up by libc's `.dynsym`, and the extra tail entries are the knock-on effect of main going unrecognized.

The fifth piece of evidence is a technique that belongs to this exercise alone. Look at the first four 8-byte words of `x/12gx $rsp`: 0x0000555555555145, 0x0000555555555155, 0x0000555555555029, 0x00007ffff7827781. They match bt's #1 through #4 without a single digit off. In the world without a frame pointer, the return addresses never disappeared; they have only moved somewhere else. On this chain, f3 writes memory the moment it enters, the bodies of f2 and f1 are nothing but a call plus one mov, and the pasted fragment of main holds only a shl plus a call. Nobody pushes anything else onto the stack, so the return addresses pressed in by the four calls sit on the stack side by side, laid out in a row starting right at $rsp. Telling them apart is simple too: any value landing in the program text segment range around 0x555555555000, or in the loaded-library range at 0x7ffff7xxxxxx, is a return address candidate, and we verify them one by one. Even if bt fails completely, the first four words are enough for us to piece the call chain together by hand.

Step two is restoring the names with addr2line. As before, let's lay out the arithmetic:

```text
0x555555555130 - 0x555555554000 = 0x1130
0x555555555145 - 0x555555554000 = 0x1145
0x555555555155 - 0x555555554000 = 0x1155
0x555555555029 - 0x555555554000 = 0x1029
```

We look the four link-time addresses up against crash, the build that still carries debug info:

```text
$ addr2line -e crash -f -C 0x1130 0x1145 0x1155 0x1029
f3(long*)
/tmp/cpu-book-09/crash.cpp:1
f2(long*)
/tmp/cpu-book-09/crash.cpp:2
f1(long*)
/tmp/cpu-book-09/crash.cpp:3
main
/tmp/cpu-book-09/crash.cpp:7
```

Frames #0 through #3 translate back: f3, f2, f1, main. Step three: inside the gdb session there is a faster road. We use `symbol-file` to swap the unstripped build's symbols into the process that is still stopped at the crash point, in place:

```text
--- symbol-file ./crash ---
(gdb) Reading symbols from ./crash...
--- bt ---
(gdb) #0  f3 (p=p@entry=0x100000000000) at crash.cpp:1
#1  0x0000555555555145 in f2 (p=p@entry=0x100000000000) at crash.cpp:2
#2  0x0000555555555155 in f1 (p=p@entry=0x100000000000) at crash.cpp:3
#3  0x0000555555555029 in main (argc=<optimized out>) at crash.cpp:6
```

We never rerun the process, and bt immediately recovers names, line numbers, and arguments, carrying p=0x100000000000 back up every single level. The precondition for this to work is that the two files share the same BuildID, in other words they are two artifacts of the same build. Our crash.stripped was copied out of crash and then stripped, so the condition happens to be met exactly.

One small discrepancy needs owning, and it settles a rule for citing evidence. Take the address 0x555555555029. After gdb restores the symbols it files the address under crash.cpp:6, the line where `return f1(p) + 1;` lives, while addr2line reports line 7, the line carrying the closing brace. The two tools read the same line table and pick different lines, so neither one is wrong. When you cite line numbers in a report, write the two sources out separately, and don't merge them into double evidence for one line.

Step four is corroboration at the shell layer. We leave gdb and run it once in an ordinary shell:

```text
$ ./crash.stripped; echo "exit=$?"
exit=139
```

139 minus 128 is 11, which is exactly SIGSEGV's number. Under the discipline set down in 02, a non-interactive shell does not print the crash text, so this one exit-code line is the entire shell-layer evidence, and the details of the incident live only in gdb's `Program received signal` line.

## Three Layers, Each Covering Its Own Stretch

Pull the three pieces together and bt depends on three layers, each independent of the others. The frame pointer chain on the stack is a singly linked list that comes into being once `push rbp; mov rbp,rsp` have executed; in the -O0 world it is what we climbed to read off each level, and the two slots of every level read out reliably each time. Once the frame pointer is omitted, the unwind table in `.eh_frame` takes over: the two lines in the CIE mark down for every function where its return address lives, strip cannot touch the table, and C++ exception unwinding leans on it too. The name layer rests on the symbol table, and the symbol table is exactly what strip deletes. Two roads lead back: take the offline addr2line and you must remember to subtract the load base; take the in-session symbol-file and the stopped process recovers where it stands. Each of the three layers has its failure scenarios and its own fallback. When the frame pointer chain snaps, the unwind table steps in; when the symbols are gone, addr2line supplies the names; and when nothing at all can be counted on, you can still do what the exercise did, pick out the return addresses queued on the stack, and verify every single one you accept.

The threads back into earlier material connect here too. The foundations of stack frames and the prologue are in [02](../02-function-calls-and-stack-frames/01-call-and-stack-frame-walkthrough.md); the full craft of crash forensics lives in [crash-lab's null pointer scene](../../../crash-lab/a-memory-safety/01-null-deref.md), where this line of work was applied to lessons learned the hard way. If you want to grab a call stack yourself while the program is running, without gdb doing it for you, C++23's std::stacktrace wraps the job into a library, and the details of the linker arguments are in [the stacktrace reference card in cpp-reference](../../../cpp-reference/core-language/19-stacktrace.md).

To finish, let's put that one bt line from the exercise session, the one after symbol-file, back on the table as the place this piece stops: `#0 f3 (p=p@entry=0x100000000000) at crash.cpp:1`. From one movq that crashes mid-store to a complete call chain carrying the guilty pointer value, everything in between is a single symbol-swapping command.
