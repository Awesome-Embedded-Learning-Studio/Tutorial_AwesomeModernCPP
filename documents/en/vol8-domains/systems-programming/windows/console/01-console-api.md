---
title: "Windows Console: Character Grid, Input Events, and VT Sequences"
description: "The single article of the Windows-side terminal track, covering the console's input/output face — the event face belongs to the processes chapter. The methodology is inherited and upgraded: under interop, stdio is a pipe and GetConsoleMode gets gle=6 outright; CreateFile opens real handles on the windowless console via CONIN$/CONOUT$; acceptance checks are always ReadConsoleOutput reading characters and attribute bytes back against the table, no human eyes involved. The full mode-bit map: CONIN$ ships 0x1f7 (QUICK_EDIT/EXTENDED/AUTO_POSITION all on), CONOUT$ 0x3, the VT bit off by default, both input and output code pages 936. The screen is a 120x9001 CHAR_INFO grid against a 120x30 visible window; WriteConsoleOutput writes whole blocks without touching the cursor (60/60 read back all correct), WriteConsoleA follows the cursor, clipping past the right edge at 119, and scroll-to-fill measured. Input is a queue of INPUT_RECORDs: one press and one release as two records with all six fields logged; count, peek, take, and flush as the four tools; mouse and focus are records too (FOCUS is documented as system-internal and to be ignored); injected Ctrl+C is just a key record carrying LEFT_CTRL_PRESSED; WINDOW_BUFFER_SIZE_EVENT cannot trigger the system-side path on a windowless console (even resetting the buffer size to the same values reports 87; SetConsoleWindowInfo succeeds, srWindow really reads back changed, and the queue gets 0 records) — recorded as-is. VT sequences compared before and after the switch: with it off, ESC[31mRED lands as nine literal characters with attribute 07 everywhere; with it on, only RED's three cells remain with attribute 04; bright red reads back 0xc; after a positioning sequence the cursor sits at (9,4); 2J clears the screen but leaves the cursor alone (ANSI ED semantics — the zeroing behavior of DOS ANSI.SYS and older conhost needs a dated note; the document's examples for both classic API methods explicitly add the homing step). Mixing boundaries: VT positioning and WriteConsoleOutput's built-in coordinates leave each other alone; with WRAP_AT_EOL off, writing 8 characters from column 115 repeatedly overwrites (119,6) and only H survives; with DISABLE_NEWLINE_AUTO_RETURN unset, the line-end \n does the carriage return too (C lands at (0,10)), and with it set, it only moves down one line (C stops at (119,10)). The close compares with Linux: tty's line editing lives in the kernel's line discipline versus Windows's line editing in the conhost host process, and VT sequences are close kin on both sides"
chapter: 8
order: 1
platform: host
difficulty: intermediate
cpp_standard: [20]
reading_time_minutes: 18
prerequisites:
  - "Win32 文件 I/O:句柄、CreateFileW 与同步读写"
  - "控制台事件与 APC"
related:
  - "控制台事件与 APC"
  - "termios 与 raw 模式:终端这层在替您做什么"
  - "伪终端与进程交互:终端录制与回放"
tags:
  - host
  - cpp-modern
  - intermediate
  - 系统编程
  - Win32
current_status:
  title: 正在打磨
  detail: 本批刚写完,正在通读打磨,表述与实测口径可能随时调整
  variant: polishing
translation:
  source: documents/vol8-domains/systems-programming/windows/console/01-console-api.md
  source_hash: 193f55723d29b90f2f9c53c205181de53694774588790acd39f7b294efb3422c
  translated_at: '2026-11-22T15:40:56+00:00'
  engine: glm-5.3
  token_count: 18019
---

# Windows Console: Character Grid, Input Events, and VT Sequences

At the close of [Console Events and APC](../process/02-console-apc.md) we left behind a byte: we turned off `ENABLE_PROCESSED_INPUT`, and Ctrl+C demoted from a control event to the input byte 0x03, with the promise that the full unfolding of console modes belonged to this terminal chapter. The debt this article repays. That one covered the console's event face, answering how Ctrl+C earns its promotion to a function call; this one covers the input/output face, and the questions turn plainer: the characters you printf out — what kind of storage do they land on in the end? The key you press — what shape does it take before your program reads it? And those `\x1b[31m`-style VT (Virtual Terminal) escape sequences — on what grounds can they turn text red on Windows?

Windows hands over three answers, each with its own shape. Take the screen: what the console holds is not a string of bytes but a color-annotated character grid whose rows and columns are directly addressable by coordinates. Take the keyboard: what comes in this side is a queue of well-formed event records, without a shadow of a character stream, each record's six fields filled in plainly. And there is a third path, the VT sequences — a compatibility layer laid into conhost (Windows' console host process, the service behind the traditional console window) only in 2015, letting cross-platform programs written around `\033[31m` run without changing a single line. We look at each in turn, using the same old routine: run experiments, read back evidence, let the table talk.

Environment and numbering conventions go up front as usual. This article's experiments are numbered e1 through e5, mapping to `e1_mode_bits` through `e5_mixing` under `code/volumn_codes/vol8/systems-programming/windows/console/01-console-api/` in the repository, each .cpp paired with a same-named .out, raw outputs all archived, reproduction commands written in the directory's README — independent of the e-series of W01 through W05, the memory chapter, the processes chapter, and the asynchronous I/O chapter; go by file names and you will not mistake anyone. The experiment programs are self-contained little things; `check_win32` from W01 and `unique_handle` from the two cornerstones articles were not invited this time — to copy any single experiment out for reproduction, you need not drag along any common headers. The machine is still the author's Win11 26200, the compiler is MSYS2 UCRT64 g++ 16.1.0, the compile command is uniformly `g++ -std=c++20 -Wall -Wextra`, the warning count is 0, and outputs were captured on 2026-10-05. The way of running follows the interop chain described in [Win32 File I/O](../file-io/01-win32-file-io.md) (W01 for short in this series): we run the `.exe` directly inside WSL, stdio hangs on pipes, and the process is attached to a console without a window. [Console Events and APC](../process/02-console-apc.md) already walked us through this situation once; this article upgrades it into a methodology, and the next section is devoted to it.

## Taking the Console Back from the Pipes

e1's first job is to measure our situation. We call `GetConsoleMode` once each on stdin and stdout; both returned 0, and `GetLastError` reported 6 (gle is our short name for GetLastError; number 6 is ERROR_INVALID_HANDLE — the handle does not fit):

```text
[1] GetConsoleMode(stdin)=0 GetConsoleMode(stdout)=0 GetLastError(后一次)=6
    stdio 挂的是管道;控制台要用 CONIN$/CONOUT$ 直开
```

Returning 0 means stdin and stdout are simply not console handles. The interop launch chain replaced stdin and stdout with pipes, so what `GetStdHandle` hands back are of course pipe handles, and taking them to console APIs is bound to fail. The process is, however, genuinely attached to a console, one with a buffer, an input queue, and mode bits — all four following experiments work on it. To get its real handles, the document gives the proper road; the Console Handles page says verbatim `The CreateFile function enables a process to get a handle to its console's input buffer and active screen buffer, even if STDIN and STDOUT have been redirected`. Redirection does not stop CreateFile: pass `CONIN$` and you hold the input buffer's handle; pass `CONOUT$` and you hold the active screen buffer's handle.

```cpp
// e1_mode_bits.cpp(节选):设备名当文件名开,访问权两边都要,共享位也得给足
HANDLE cin = CreateFileA("CONIN$", GENERIC_READ | GENERIC_WRITE,
                         FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
HANDLE co  = CreateFileA("CONOUT$", GENERIC_READ | GENERIC_WRITE,
                         FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
```

The excerpt uses `CreateFileA`, which seems to clash with W01's rule that modern code names the W version. The reason, stated here: the W-version rule governs paths, where Chinese characters must pass through code-page translation to survive. CONIN$ and CONOUT$ are pure-ASCII reserved device names that never go through translation at all, so the A version is safe here.

Once the handles are in hand, this article's methodology stands up. What is really on the screen — we do not look with human eyes; we uniformly use `ReadConsoleOutput` to read the grid back, characters and attributes together, with the assertion written into the program: the 60 cells read back must all 60 be correct; one cell off counts as failure. If you have written graphics programs, you will recognize this as screenshot comparison — except the console's screenshot is a ready-made API, and we do not even have to explain pixels. All five experiments in this article take acceptance through read-back-and-compare; what is read back may be the grid or a queue count.

> One more identification trick, also from the Console Handles page: unsure whether a handle is a console, call `GetFileType` — console handles report `FILE_TYPE_CHAR`, a character device. When you write cross-platform tools and want to detect whether you are running in front of a terminal, this is far more graceful than trying a console API and catching the failure.

stdio swapped for pipes is not a quirk unique to interop. Write `foo | more` in any shell and foo's stdout is a pipe just the same, equally unable to touch the console. CONIN$/CONOUT$ is therefore worth far more than coping with WSL: in any situation where the standard streams are redirected, a program that wants to keep painting the screen directly leans on it.

## The Full Mode-Bit Map

With the real handles in hand, e1's main course is served: we translate both sides' factory mode states, bit by bit, into plain speech. CONIN$ reads back 0x1f7, CONOUT$ reads back 0x3, and every bit registered in the document is laid out below:

| Input bit | Name | What it governs |
| --- | --- | --- |
| 0x1 | ENABLE_PROCESSED_INPUT | Special keys like Ctrl+C take the control-event road |
| 0x2 | ENABLE_LINE_INPUT | Line buffering; delivery happens on Enter |
| 0x4 | ENABLE_ECHO_INPUT | Input echo |
| 0x8 | ENABLE_WINDOW_INPUT | Buffer-size changes post a WINDOW_BUFFER_SIZE_EVENT |
| 0x10 | ENABLE_MOUSE_INPUT | Mouse events enter the queue |
| 0x20 | ENABLE_INSERT_MODE | Insert/overwrite in line editing (a sub-switch of EXTENDED) |
| 0x40 | ENABLE_QUICK_EDIT_MODE | Mouse selection and right-click paste (a sub-switch of EXTENDED) |
| 0x80 | ENABLE_EXTENDED_FLAGS | The carrier of the two above; touching them requires setting this too |
| 0x100 | ENABLE_AUTO_POSITION | The system decides where the window appears |
| 0x200 | ENABLE_VIRTUAL_TERMINAL_INPUT | Input becomes VT bytes; key events are no longer synthesized |

| Output bit | Name | What it governs |
| --- | --- | --- |
| 0x1 | ENABLE_PROCESSED_OUTPUT | Handles control characters like backspace, tab, and bell |
| 0x2 | ENABLE_WRAP_AT_EOL_OUTPUT | Wrap to the next line automatically at end of line |
| 0x4 | ENABLE_VIRTUAL_TERMINAL_PROCESSING | Interpret VT escape sequences in output as commands |
| 0x8 | DISABLE_NEWLINE_AUTO_RETURN | The line-end \n does not imply a carriage return |
| 0x10 | ENABLE_LVB_GRID_WORLDWIDE | Attribute bits for grid glyphs; only rare fonts use them |

Count 0x1f7 against the table: eight bits lit, two dark — one named WINDOW_INPUT, the other VIRTUAL_TERMINAL_INPUT. Line editing and echo live in the lit ones: LINE_INPUT paired with ECHO_INPUT is precisely the Windows-side counterpart of Linux's ICANON plus ECHO in termios (the switch interface Linux built for terminals). If you are writing a password prompt and want echo off, the thing to turn off is input-side 0x4, ENABLE_ECHO_INPUT. Output-side 0x3 lights only PROCESSED and WRAP; the bit worth an extra glance is the dark 0x4: VT interpretation is off by default. On this point the document's convention and our measurement agree, and a whole section of e4 deals with 0x4.

The input table also has a three-bit bundling worth a sentence of its own. INSERT and QUICK_EDIT serve as sub-switches, and their carrier is EXTENDED_FLAGS; the document's requirement is written into the bit descriptions: to touch them you must set EXTENDED_FLAGS along. In the factory state EXTENDED is lit, carrying precisely the INSERT and QUICK_EDIT above it, and QUICK_EDIT is on too — meaning mouse-box selection and right-click paste were alive throughout our experiments, the part of the console experience most easily taken for granted.

The dark VIRTUAL_TERMINAL_INPUT also deserves a word; it governs the input side: once set, keys that would produce escape sequences — arrow keys, say — are no longer synthesized into key-event records; the VT bytes go into the buffer raw and parsing is the program's business. It is the symmetric switch of output-side 0x4. This article's experiments did not enable it; e3 still watches the classic world of records. When you write full-screen programs needing fine-grained key control, it is the entrance at the other end.

`SetConsoleMode` changes read back: in e1 we zeroed CONIN$ entirely and read back 0x0000, then lit the VT bit alone on CONOUT$ and got back 0x0007 — the rewrite road runs both directions. We also checked the code pages (Code Page, Windows' number for character encodings): both sides report 936, meaning GBK. Once characters enter the grid they are stored as `UnicodeChar`, and double-byte Chinese still occupies one cell each — one less thing to worry about under CP 936.

The PROCESSED_INPUT row we only point at. The whole drama of turning it off and demoting Ctrl+C to byte 0x03 was already staged in e7 of the processes chapter's [Console Events and APC](../process/02-console-apc.md) — how the signal is delivered and how the ignore flag is inherited are that article's topic, and we will not re-stage them.

## The Screen: A 120x9001 Grid

Now we answer the first question: where do printf's characters land. e2 opens with `GetConsoleScreenBufferInfo`, carrying out this storage's dimensions:

```text
[1] 缓冲区 dwSize=120x9001(带滚动历史) 可见窗 srWindow=120x30 光标=(0,0) 默认属性=0x7
```

The two numbers do not speak of the same thing. The buffer is one whole block of 120 columns by 9001 rows; the visible window is merely a 30-row observation window somewhere inside it — scroll up and you are seeing earlier content of the buffer. The 9001 height is just this machine's factory configuration; we do not take it as a universal constant, but it proves at least one thing: scroll history is not the terminal window remembering for you; it is remembered in the buffer itself. This grid's smallest unit is `CHAR_INFO`, with two fields: `Char.UnicodeChar` holds the character, `Attributes` holds a 16-bit attribute word, of which the low 8 bits are the everyday half — foreground blue, green, red at 0x1, 0x2, 0x4, the intensity bit at 0x8, the three background ones moved up to 0x10, 0x20, 0x40, and the background intensity bit at 0x80. Split a byte in two: the low half governs text color, the high half governs background; the factory default is 0x7 — precisely gray-white text on black. Coordinates in this article are uniformly written `(x,y)`, x for the column, y for the row, both counted from 0.

The classic APIs for writing onto this grid come in two postures with completely different tempers. `WriteConsoleOutput` is the whole-block direct write: you prepare a CHAR_INFO array, hand it a target rectangle, and it lays the whole block down by coordinates. e2 laid down a 6-row by 10-column colored matrix — the first 3 rows in bright text, and in the last 3 rows the odd rows padded with blue backgrounds — then read the same block back with `ReadConsoleOutput` to compare:

```text
[2] WriteConsoleOutput(6x10 @ (2,0)) -> 1;光标 (0,0)->(0,0):整块直写不碰光标
    ReadConsoleOutput 读回 60 格,字符+属性全对:60/60
    第 0 行属性=RGB|---(亮白字)
    第 1 行属性=RGB|--B(亮字/蓝底)
```

All 60 cells read back correct, colors unsmudged — red is red, blue is blue. And hidden in this output is a more important contrast: the cursor stood at `(0,0)` from head to tail, never having moved nest — the whole-block direct write does not touch the cursor at all. `WriteConsoleA` is entirely different: it treats the cursor as its own pen tip, writes where the cursor is, and advances when done. Segment [4] of e2 placed the cursor at `(40,8)` and wrote hi:

```text
[4] SetConsoleCursorPosition(40,8)+WriteConsoleA("hi"):写 2 字节,光标推进到 (42,8)
```

It wrote 2 bytes and the cursor advanced to `(42,8)`; every step of the pen tip left its trace. This difference gets put to work in e5 later — remember: one side acknowledges coordinates, the other acknowledges the pen tip.

One temper of the direct write we also verified: the part of a rectangle that exceeds the buffer is simply clipped; what fits is written. Segment [3] of e2 writes a 10-column block starting at x=115, with the buffer only 120 columns wide:

```text
[3] 写 10 列宽 @x=115,实际落下的矩形 Right=119(裁到缓冲区右缘 119,只写进 5 列),读回首格字符='A'
```

The landed rectangle's right edge stopped at 119 — only 5 columns went in. When you draw full-screen interfaces, the edge alignment is ours to compute; it warns you by quietly rewriting the target rectangle rather than reporting an error.

The same-family `ScrollConsoleScreenBuffer` we verified along the way: scroll rows 0 through 5 up by 2 rows, with the two rows freed at the bottom filled by the character and attributes we chose:

```text
[5] 上滚 2 行后第 0 行读回:"CCCC"(原第 2 行顶上来;空出的底部两行由红字 '.' 填补)
```

We read back row 0: exactly the CCCC left by the original row 2, and the fill characters carried the red we specified. Scrolling is a grid operation too — whole rows of CHAR_INFO are moved; there is no such thing as inserting or deleting in a byte stream.

## The Keyboard: A Queue of Event Records

The output side's worldview is the grid; the input side's counterpart is a queue of formed records whose element is the `INPUT_RECORD`. e3 walked this world's residents around the block. No live human sits at this machine's console, so input records are uniformly injected via `WriteConsoleInput`. The convention must be stated clearly: injection is transport — a filled record is placed directly at the queue's tail; it does not re-travel the keyboard ingestion pipeline. That difference is about to become interesting.

Start with what a keyboard resident looks like. One press and one release of the 'A' key are two independent KEY_EVENT_RECORDs, all six fields recorded:

```text
[3] ReadConsoleInput 逐条读:
    KEY_EVENT  按下 vk=0x41(A) scan=0x1e char=A repeat=1 修饰:(无)
    KEY_EVENT  松开 vk=0x41(A) scan=0x1e char=A repeat=1 修饰:(无)
    KEY_EVENT  按下 vk=0x43(C) scan=0x2e char=c repeat=1 修饰:L-Ctrl
    MOUSE_EVENT 位置=(5,3) 按钮=0x1 事件=0x0 修饰:(无)
    FOCUS_EVENT  bSetFocus=1
    取完后队列余量=0
```

The six items, identified one by one: whether it records a press or a release, the repeat count, the virtual-key code, the scan code, plus the translated character and the modifier-key state. The virtual-key code is the logical key number in Windows' keyboard layout; the scan code is the physical key number reported by the keyboard hardware — the same press of 'A' yields 0x41 and 0x1e respectively: one governs abstract identity, the other physical position, and a key-remapping program needs both ends. The modifier state is a bitmap; in the Ctrl+C record, `LEFT_CTRL_PRESSED` is the lit one. Mouse clicks travel as MOUSE_EVENT records, with coordinates, button, and event kind all aboard. Focus changes have their own record too, but the document's position on FOCUS_EVENT brooks no discussion — verbatim, `These events are used internally and should be ignored`, meaning system-internal use, and applications reading one should ignore it. We injected it only to show its face, not to invite you to build logic on it.

The queue itself comes with four tools — count, peek, take, flush — each governing its own stretch. `GetNumberOfConsoleInputEvents` counts, `PeekConsoleInput` peeks without taking; the first two segments of e3 played their personalities for us:

```text
[1] 注入 5 条后,GetNumberOfConsoleInputEvents=5
[2] PeekConsoleInput 取 1 条(不取走),EventType=KEY_EVENT:1,队列余量仍 5
```

Five records injected, the count reports 5; Peek glanced at one and the remaining count still reports 5 — the queue lost not one hair. `ReadConsoleInput` is the real taker, dequeuing record by record; after all five were read, the count hit zero. `FlushConsoleInputBuffer` is the one-keystroke clean sweep. Compare with the Linux side and it clicks: a tty (short for teletype, the Unix world's name for terminal devices) feeds you a byte stream; here it is a structured record queue, and an act like Peek — look first, decide later — simply has no byte-stream equivalent.

This stretch also ran into one limitation of this machine, honestly reported. Among the input bits is ENABLE_WINDOW_INPUT: with it on, any change in the buffer's size should post a WINDOW_BUFFER_SIZE_EVENT. But on this windowless console, `SetConsoleScreenBufferSize` is rejected across the board; segments `[4]` and `[5]` of e3 lay out the readings:

```text
[4] 开 ENABLE_WINDOW_INPUT 后 SetConsoleScreenBufferSize(120x3000) -> 0 GetLastError=87
    于是这条事件的形态用注入演示:
    WINDOW_BUFFER_SIZE_EVENT  新尺寸=100x30
[5] SetConsoleScreenBufferSize(原样 120x9001) -> 0 GetLastError=87
    SetConsoleWindowInfo(同尺寸 120x30 原样重设) -> 1 GetLastError=0 队列=0
    SetConsoleWindowInfo(异尺寸 120x29) -> 1 GetLastError=0 队列=0
    读回 srWindow=120x29(窗口真改没改,以读回为准)
```

Every buffer-size change attempt was refused: the different 120x3000 was rejected, and so was even a same-values reset against 120x9001, both reporting gle=87. `SetConsoleWindowInfo` cuts another temper entirely: same-size and different-size calls both returned success, and srWindow really read back changed from 120x30 to 120x29 — the change genuinely landed. Yet the window shrank by one row and the queue count stayed 0: not one WINDOW_BUFFER_SIZE_EVENT was posted. The system-side trigger path does not run on our rig; we can only inject a record to demonstrate its shape — the fields it carries are the new size's x and y. The drag-the-edge path in a real window cannot be tested on this machine; if yours has a real window, you can fill this part in.

Finally, back to our injected Ctrl+C record. It entered the queue and was read out, and from start to finish it was only a key record with the L-CTRL modifier — never converted into a control event, and the handler never ran once. The reason hides in the convention above: by the two articles' combined reading, signal conversion should grow on the keyboard ingestion pipeline (the real-keyboard path — untested on this machine), while injection writes straight into the queue, bypassing the pipeline. The full journey of Ctrl+C from key record to function call, together with how the PROCESSED_INPUT bit acts as the gate, is the proper topic of [Console Events and APC](../process/02-console-apc.md); that article's e7 takes it to the end.

> One more relationship to point at: the input mode bits like LINE_INPUT and ECHO_INPUT govern the byte view that `ReadFile` and `ReadConsole` see — Enter-time delivery and input echo both happen at that layer. `ReadConsoleInput` reads the record view and skips line editing outright. The two readings live on separate layers; choose the record view and you simultaneously give up line editing's conveniences — the rest is yours to wire.

## The Third Path: VT Sequences

The worldview of grids and records is Windows' native invention, but most command-line programs we meet emit byte streams, and the Linux world grew up speaking only the `\033[31m` dialect. The Windows 10 1511 update (build 10586, November 2015) laid the VT interpretation layer into conhost, and escape sequences in a byte stream could henceforth be translated as commands — the switch being that default-dark 0x4 bit on the output side. We use e4's switch for a before-and-after comparison, with acceptance still on our old method: read the buffer back and let it speak for itself:

```text
[1] VT 关:WriteConsoleA 写了 12 字节,序列没被吃——
    第 0 行读回:"ESC[31mREDESC[0m"
    前 9 格属性: 07 07 07 07 07 07 07 07 07
[2] VT 开:同一串 12 字节,只剩 R E D 三个字符进了缓冲,属性=红色——
    第 0 行读回:"RED         "
    前 9 格属性: 04 04 04 07 07 07 07 07 07
```

The same 12 bytes of `\x1b[31mRED\x1b[0m` met two completely different fates. With the switch off, the 12 bytes are 12 cells of ordinary characters: ESC, `[`, `3`, `1`, `m` all laid into the grid as body text (0x1b displays as the three letters ESC when printed back), attributes a uniform 07. With the switch on, the sequences were consumed and only the body text remained, the first 3 cells' attributes becoming 0x4 — exactly the value of FOREGROUND_RED. When you see ANSI colors fail in a Windows terminal and source code scroll by raw, the first thing to check is output-side 0x4, ENABLE_VIRTUAL_TERMINAL_PROCESSING — especially after a program's output is redirected: the VT bit does not follow the handle, and you must re-enable it on the real console's handle.

We verified the combined parameters too; bright red `\x1b[1;31m` and the cursor-positioning readings are these two lines:

```text
[3] \x1b[1;31mX 读回:字符='X' 属性=0xc(RED=0x4|INTENSITY=0x8 -> 0xc)
[4] \x1b[5;10H 后光标=(9,4)(VT 是 1 起算,缓冲区坐标是 0 起算,9,4 对上了)
```

The 0x8 intensity plus the 0x4 red line up exactly in bitwise arithmetic. Here we connect e2 and e4: the red that SGR (Select Graphic Rendition, the VT family governing color and intensity) 31 asks for lands in the buffer as attribute byte 0x4 — the same bit as the FOREGROUND_RED that `WriteConsoleOutput` fills directly. At the end of its translation, a VT sequence still edits the CHAR_INFO attributes; it is merely another way of writing onto the same grid. So classic and VT can never fight over color — there is only one household ledger.

Cursor-class sequences also land on the grid's coordinate system, with a single counting difference: `\x1b[5;10H` uses VT's 1-based counting, which lands at 0-based buffer coordinates `(9,4)` — the row goes from 5 to 4, the column from 10 to 9. Many a cross-platform program has tripped here: to carry Linux's 0-based coordinates into a VT sequence, remember to add one to both row and column.

Screen clearing also deserves its own moment. The author placed the cursor at `(30,3)`, wrote `\x1b[2J`, and then read the cursor:

```text
[5] 光标先放 (30,3),\x1b[2J 之后光标=(30,3) —— 2J 只清屏不动光标,与 ANSI 的 ED 定义一致(老 conhost 曾顺手归零,本机 26200 不再如此);缓冲区读回 "    "(清空成空格)
```

2J wiped the whole visible area into spaces while the cursor stayed put in place. This matches ANSI's definition of ED (Erase in Display): it governs erasing, not the cursor. But the matter carries historical baggage: the DOS-era ANSI.SYS used the old convention of homing the cursor along with the clear, and in microsoft/terminal issue #60 someone worked over this divide between old and new standards. The document's attitude, in truth, hides in its examples: the Clearing the Screen page gives three methods; the VT example ends right after writing `\x1b[2J`, while the two classic-API examples both explicitly add a homing line, whose comment says verbatim `Move the cursor to the top left corner too` — the second example's comment even notes that this method matches the behavior of cls in cmd, again ending with the two steps of scroll plus home. Our measurement stands with ANSI. But if you count on 2J to home the cursor as well, add `\x1b[H` yourself, or simply call `SetConsoleCursorPosition(0,0)` to put it back — do not gamble on conhost's version temperament.

## Two Roads Walked Together, Two Readings of the Line End

In real programs the classic APIs and VT sequences often coexist; they write the same buffer with two sets of brushwork, and e5 asks whether they quarrel. Three small experiments pin the answer down one by one.

First, the ownership of coordinates. Segment [1] of e5 put both writings on the same stage: we direct-wrote a # at `(40,2)` via `WriteConsoleOutput`, then positioned with VT `\x1b[2;5H` and wrote AB on top.

```text
[1] A 落在 (4,1),B 落在 (5,1)(跟光标走);'#' 落在 (40,2)(WriteConsoleOutput 自带坐标)
    此刻光标=(6,1) —— 两套写法互不干扰,但光标只认 VT/WriteConsole 一系
```

The hash landed at its self-carried `(40,2)`; A and B followed the VT positioning's pen tip to `(4,1)` and `(5,1)`, and the cursor stopped at `(6,1)`. The two coordinate systems do not interfere, for the reason sits in what e2 verified: the whole-block direct write never touches the cursor, so it cannot disturb the pen tip, and the pen tip does not know it either. Mixing is fine — just keep clear in your head which brush you are holding at this moment.

The second experiment concerns the line end. `ENABLE_WRAP_AT_EOL_OUTPUT` is on by default: writing past the line end wraps automatically to the next line's start. And off? e5 writes the 8 characters ABCDEFGH starting from column 115, with the buffer 120 wide — overrun is destiny:

```text
[2] WRAP_AT_EOL 开:光标起 (115,6) 写 "ABCDEFGH",A=(115,6) H=(2,7) —— 越过行尾回绕到下一行,E..H 在第二行
[2] WRAP_AT_EOL 关:光标起 (115,6) 写 "ABCDEFGH",A=(115,6) H=(119,6) —— 越过行尾不回绕,E..H 反复覆盖 (119,6),只剩 H
```

With it on, A through E fill columns 115 to 119 and F, G, H wrap to the next line, H landing at `(2,7)` — all as usual. With it off, things turn strange: H sits at `(119,6)`, but where did E, F, G go? The answer: they all stayed there in turn, each pressed away by the next arrival. Without wrapping, the pen tip that passes the line end is held on the last cell; every subsequent character lands at `(119,6)`, the later overwriting the earlier, and of 8 characters only one H remains in place. (The E..H annotation on those two archive lines is loose: the first wrapped character is F; E stayed in column 119 at the line end the whole time — we speak by the read-back coordinates.) If you ever walk into a scene where a long string's output leaves only its last character, this bit is what you check.

Third, the two readings of `\n`. With `DISABLE_NEWLINE_AUTO_RETURN` unset, a `\n` at the line end does the carriage return and line feed together — the cursor lands at the next line's start. With it set, `\n` changes only the row; the column stays where it is:

```text
[3] DISABLE_NEWLINE_AUTO_RETURN 不设:B=(118,9) C=(0,10) —— 行尾 \n 连回车一起做,C 到下一行行首
[3] DISABLE_NEWLINE_AUTO_RETURN 设:B=(118,9) C=(119,10) —— \n 只下移一行不回零,C 落在 (119,10)
```

The same run of AB plus newline plus C, written from column 117: flip the switch and C's landing point moves from `(0,10)` to `(119,10)`. This bit is prepared for VT programs: VT sequences manage cursor positioning themselves, and if the system presumptuously homes you, it would wreck the arithmetic of relative positioning like `\x1b[<n>E`. Set it, and the semantics of the newline are handed back to your sequences.

## How the Other Side Sees It

The Linux side of this chapter has two articles — one on termios and raw mode, one on pseudoterminals and process interaction — and before we close, we lay both sides' machinery side by side.

Where line editing and echo live, the two sides answer differently, yet equally outside your program. Linux puts them in the kernel's tty line discipline; termios's ICANON and ECHO are the switches built onto that layer. Windows puts them in the console subsystem; the mode bits LINE_INPUT and ECHO_INPUT are the same kind of gate. This article's e3 injections and the Linux side's pipe-versus-pty experiments verified the same thing: the reading program has no say, and the shell has no say either — the one in charge is the middle layer; only the address differs — Linux's lives in the kernel, Windows's lives in the conhost host process.

VT sequences are close kin on both sides — so close that the `\x1b[31m` you write needs not one byte changed. But the direction is reversed: in the Linux world, programs have always emitted byte streams with the terminal emulator doing the interpreting; VT is its native tongue. Windows's native tongue is grids and records; VT is a translation layer added only in 2015, translating byte streams into grid operations. So on this side you mind one extra thing — confirming the translation layer is on, the 0x4 bit mentioned above — while on the Linux side the extra thing minded is the terminal emulator's dialect differences. When it truly comes time to write cross-platform terminal code, both sides read their own mode bits and turn off their own line editing, by different means toward the same end; this correspondence was already walked once from the termios direction in the two Linux-side articles. For the other side's full story, see [termios and raw mode](../../linux/terminal/01-termios-raw.md) and [pseudoterminals and process interaction](../../linux/terminal/02-pty-recording.md).

Before the experiment programs packed up, they cleaned the scene: mode bits restored to factory, the grid areas they played on filled back with spaces, the cursor homed. The console is a machine-wide shared surface; our experiments left writing on it, and the next program to run in would see it first thing. That tidiness is worth carrying into your own terminal programs.

<ReferenceCard title="References">
  <ReferenceItem
    :id="1"
    title="Console Handles (the proper road to CONIN$ and CONOUT$)"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/console/console-handles"
  />
  <ReferenceItem
    :id="2"
    title="SetConsoleMode function (definitions of every mode bit)"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/console/setconsolemode"
  />
  <ReferenceItem
    :id="3"
    title="WriteConsoleOutput function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/console/writeconsoleoutput"
  />
  <ReferenceItem
    :id="4"
    title="ReadConsoleOutput function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/console/readconsoleoutput"
  />
  <ReferenceItem
    :id="5"
    title="INPUT_RECORD structure (the event record's six fields)"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/console/input-record-str"
  />
  <ReferenceItem
    :id="6"
    title="ReadConsoleInput function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/console/readconsoleinput"
  />
  <ReferenceItem
    :id="7"
    title="WriteConsoleInput function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/console/writeconsoleinput"
  />
  <ReferenceItem
    :id="8"
    title="FOCUS_EVENT_RECORD structure (the system-internal, should-be-ignored convention)"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/console/focus-event-record-str"
  />
  <ReferenceItem
    :id="9"
    title="Console Virtual Terminal Sequences"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/console/console-virtual-terminal-sequences"
  />
  <ReferenceItem
    :id="10"
    title="Clearing the Screen (three clear methods and cursor homing)"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/console/clearing-the-screen"
  />
</ReferenceCard>
