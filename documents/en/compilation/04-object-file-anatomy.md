---
chapter: 13
difficulty: intermediate
order: 4
platform: host
reading_time_minutes: 23
tags:
- cpp-modern
- host
- intermediate
title: "Object File Anatomy: What's Inside a .o"
description: "Read the ELF header and the section header table with readelf -h and -S, map the nine symbols back to demo.c one by one with nm, walk the readelf -r relocation records column by column down to the origin of the -4, then meet the DWARF debug sections that -g adds, the three-column totals from size, and the Windows-side COFF and dumpbin."
cpp_standard: [11, 14, 17, 20]
translation:
  source: documents/compilation/04-object-file-anatomy.md
  source_hash: ecc72d667225b70725fab4e999ea0aecec0cc347322eac77008968f8d304bebc
  translated_at: '2026-09-30T05:56:25+00:00'
  engine: glm
  token_count: 5647
---

# Object File Anatomy: What's Inside a .o

At the end of part 03 we made a deal: bring the tools, and today we open the .o up. The questions on our list still number three: how the inside is laid out, what a relocation record looks like, and what .text and .data actually hold. Part 01 also left an earlier promise: that not stripped line next to hello.o was tremendous news, and the thing a dissection of an object file leans on is precisely the symbol table. Today we make good on those words, item by item.

The lead actor changes, though. mangle.cpp has not a single global variable in its source, so .data and .bss would have nothing to show us. hello.cpp is too thin: six lines of code cannot carry a dissection of this size. So I prepared a demo.c instead: global, static, initialized, uninitialized, declared-but-never-defined — every situation holds a share in it. It has one more convenience going for it: it is written in C, and as we saw in part 03, the C side skips the name mangling step, so all that lies in the symbol table are plain names, and reading the output spares us a round of decoding.

As usual, the environment disclosure: all my experiments ran in WSL2, with GCC 16.2.1 plus binutils 2.47 as the toolchain, and every snippet of output in this piece was actually run. If your versions differ, details like the number of sections and the byte counts may not match, but the layout itself has not changed in years.

## Three Tools, Each with Its Own Job

objdump, the one the end of part 03 told us to bring along, now takes its seat at the table, and we will take the chance to get to know its two stablemates as well. Among the binutils family, the ones who work this line come down to three:

- nm, who already worked a job with us in part 03: it reads the symbol table — which names an object file holds, and what standing each name carries — and reports the whole page to you.
- readelf, on stage for the first time today: it reads ELF structure and nothing else. File header, section header table, symbol table, relocation table: it lays any of them out for you exactly as they sit.
- objdump, the longest-serving of the three: its signature act is disassembly. Add -d, and it translates the machine code in .text back into assembly, one instruction at a time, for you to read.

The three overlap in their work: the section list, for one, both readelf and objdump can print for you. What we are after today is structure and symbols, and the ones that answer to that are readelf and nm. objdump's disassembly we save for the occasions that call for poring over machine code.

## Our Dissection Subject: demo.c

Here is the code. Count as you go: the global names written down total nine.

```c
int un_g_initialized_var;
int g_initialized_var = 1;
extern int extern_var;
static int un_init_local_var;
static int init_local_var = 1;
static int local_func() { return 1; }
int func() { return 2; }
extern int extern_func();
int main() { return extern_var + extern_func(); }
```

Let us take the nine names in four groups. func and main are global functions with definitions. local_func is defined too, but it wears static, so its reach stays inside this translation unit. g_initialized_var and init_local_var are variables given initial values, one global, the other static. un_g_initialized_var is declared but given no value, and un_init_local_var's initial value is equally blank. All extern_var and extern_func hold in their hands is an extern declaration apiece: their definitions are not in this file at all, and main goes and uses the two of them anyway.

Let us compile. The product is today's dissection subject:

```bash
gcc -c demo.c -o demo.o
```

Where each of the nine names lands, we will walk through with the tools, one at a time.

## The ELF Header: REL, an Identity Still Pending

readelf -h prints a dozen-plus lines; we will pick out the four that matter most:

```text
  Class:                             ELF64
  Type:                              REL (Relocatable file)
  Machine:                           Advanced Micro Devices X86-64
  Entry point address:               0x0
```

Class says this is a 64-bit ELF; Machine says the processor it aims at is x86-64. The Type line reads REL, whose full name is Relocatable file. That relocatable the file command reported back in part 01: this is its official spelling. What this identity means, let us confirm once more against the Entry line. The entry point address reads 0x0. An executable's entry points at the program's first instruction. The hello_app we built in part 01 is a pie program; run readelf -h on it and Type is DYN (a fixed-address executable would be EXEC), with Entry pointing at a location named _start. That is the runtime library's starting point, and everything before main is its business; we will unfold it in part 14. Over on the .o side, not one address has been settled, and even a question like "where does execution start" has nothing to stand on yet, so 0 is the only thing that can go here. This pending state is a product of the division of labor: settling addresses is nothing the compile stage ever intends to do, and every bit of it is left to the linker.

## The Section Header Table: Every Patch of Ground in the .o

ELF carves the file into blocks, each with its own name and purpose, and such a block is called a section. readelf -S lines all the sections up into a table. The full output also carries several more columns per section, Size and Flag among them; I trimmed those off, the ellipses are mine, and the columns kept are the ones we will use from here on:

```text
  [Nr] Name              Type             Address           Offset
  [ 0]                   NULL             0000000000000000  00000000
  [ 1] .text             PROGBITS         0000000000000000  0000000040
  [ 2] .rela.text        RELA             0000000000000000  000000002b0
  [ 3] .data             PROGBITS         0000000000000000  000000006c
  [ 4] .bss              NOBITS           0000000000000000  0000000074
  [ 5] .comment          PROGBITS         ...
  [ 6] .note.GNU-stack   PROGBITS         ...
  [ 7] .note.gnu.pr[...] NOTE             0000000000000000  000000000090
  [ 8] .eh_frame         PROGBITS         ...
  [ 9] .rela.eh_frame    RELA             ...
  [10] .symtab           SYMTAB           ...
  [11] .strtab           STRTAB           ...
  [12] .shstrtab         STRTAB           ...
```

Let us learn the columns once through: [Nr] is the index, Name is the section's name, Type is what this block of bytes is, Address is the section's place in the final address space, and Offset is its place in the file. Scan the Address column: zeros the whole way down. The pending identity in the ELF header lands on every section's head still pending; where they will live later, nobody yet knows. Offset, on the other hand, holds real values: .text's bytes start at file position 0x40, for one. Next we will go block by block and see who lives in each of these sections.

.text has type PROGBITS, which says this block holds real, honest bytes. All the machine code we wrote lands here: the translated bytes of local_func, func, and main, laid out one after another.

.data is PROGBITS as well, and what it holds are the initialized global and static variables. The initial value 1 we wrote in demo.c is at this moment lying right there in byte form.

.bss has type NOBITS, and this line deserves an extra look from you. NOBITS announces exactly one thing: this section takes up zero bytes in the file. For un_g_initialized_var and un_init_local_var, which carry no initial values, all that is kept is a size record; at program startup, this patch of memory gets marked out and cleared to zero, and the bytes only come into being at run time. What this saves becomes clear the moment you think it through: if we declared a 1MB uninitialized array, would we really lay down 1MB of zeros in the file? We can verify the matter from the Offset column, too. .data starts at 0x6c and runs 8 bytes, which puts its end at exactly 0x74. .bss's Offset also reads 0x74, but its type is NOBITS: the file holds none of its bytes, and that number is only the position assigned to it in the record.

.rela.text has type RELA and holds relocation records; a whole section of this piece is set aside to attend to it.

.symtab and .strtab are a pair of partners: SYMTAB means symbol table, STRTAB means string table. Symbol names are not embedded directly in the symbol table; they are collected in .strtab, and what the symbol table records is an offset pointing over there. The same mechanism has one more copy, .shstrtab. Guess whose strings it keeps? The section names' own. The dozen-plus section names before our eyes are housed precisely in .shstrtab.

The miscellaneous items we can count, too: three of them. .comment holds the compiler's version note, .note.GNU-stack is an annotation about the stack, and [7], that .note.gnu.pr[...], records the instruction-set property marks demo.c uses; none of the three enters our main story. The name at [7], though, deserves an extra sentence. The [...] inside its name is not the same thing as the ellipses I used for trimming columns: it is readelf's mark for a long name truncated at the default column width. Add -W and its full name comes into view: .note.gnu.property.

.eh_frame stores the tables used for stack unwinding; exception handling and stack backtraces both lean on it. We did not write a single exception in demo.c, and the section stayed in the file all the same, together with its companion .rela.eh_frame. Today we only learn their faces; the details wait until a volume that actually puts them to work.

## The Symbol Table: Nine Names Mapped Back to the Source

Now it is nm's turn on stage. Read it side by side with demo.c, and let us not let one line slip past:

```text
$ nm demo.o
                 U extern_func
                 U extern_var
000000000000000b T func
0000000000000000 D g_initialized_var
0000000000000004 d init_local_var
0000000000000000 t local_func
0000000000000016 T main
0000000000000000 B un_g_initialized_var
0000000000000004 b un_init_local_var
```

nm sorted by name. The left column gives the address, the middle column is the letter, the right column gives the symbol name, and all nine names are up there without one missing. Let us map them back, group by group.

The first two lines do not even have an address, and the letter is U across the board. U means undefined: extern_var and extern_func hold nothing in this file but a declaration; their definitions live in other files. Back in part 01 we reasoned out that the undefined reference error comes from the linking leg of the trip. And which names are still hanging open is already written down at the .o leg, and nm shows it to us plain as day.

Now the letter's case: func and main get the uppercase T, local_func gets the lowercase t. A capital T reports three things at once: defined in this file, contents in the code section, visible from the outside, so any other object file can reference it when linking. A lowercase t is the private edition of the same story: a function wearing static belongs to this translation unit alone. Pair up the three addresses while we are at it. local_func sits at 0x0, func at 0xb, main at 0x16: that is the three brothers' seating in .text, and they are all offsets within the section, the same pending state as the Address column's uniform zeros in the section header table.

On to the variables: g_initialized_var's letter is D, init_local_var's letter is d. D pairs with .data: initialized variables, bytes genuinely present in the file. Pair up the addresses, too. One records 0x0, the other 0x4; two ints queued one behind the other, splitting .data's 8 bytes exactly between them.

un_g_initialized_var's letter is B, un_init_local_var's letter is b, pairing with .bss. The addresses are the same old 0x0 and 0x4 combination; only, here the address is nothing but a number, and the bytes will not exist until run time — a story we already saw through while covering the section header table.

Have you spotted the pattern? This alphabet really answers just two questions. The first asks where the symbol lives: T is code, D is initialized, B is uninitialized, and the letter is the head of the section's name. A symbol without so much as a place to live gets the U. The second asks public or private: uppercase is public, lowercase is static-private. Run the two rules back over demo.c, and every static lands as a lowercase letter. Let us fold the rules into a table, yours to consult whenever you need it:

| Letter | Meaning | Examples in demo.c |
|--------|---------|--------------------|
| U | Referenced only; definition not in this file | extern_var, extern_func |
| T / t | Code; public / static-private | func, main / local_func |
| D / d | Initialized data; public / static-private | g_initialized_var / init_local_var |
| B / b | Uninitialized data; public / static-private | un_g_initialized_var / un_init_local_var |

> The alphabet has further members — W and V for weak symbols among them. They did not show their faces in this output of ours; we will make their acquaintance when we meet them later on.

From here on in this volume, every piece will be putting it to work: checking whether everything a library should export is accounted for, or chasing down a symbol whose definition cannot be found — the first tool we open for either job is this one.

## The Relocation Table: Two Blanks and How to Fill Them

.rela.text, which we held back while covering the section header table, now has its turn. The end of part 03 left us a sentence: when the compiler meets a definition this file does not hold, what it leaves the assembler is a relocation record. And the place those records gather is the section in front of us:

```text
$ readelf -r demo.o
Relocation section '.rela.text' at offset 0x2b0 contains 2 entries:
  Offset          Info           Type           Sym. Value    Sym. Name + Addend
00000000001b  000a00000004 R_X86_64_PLT32    0000000000000000 extern_func - 4
000000000021  000b00000002 R_X86_64_PC32     0000000000000000 extern_var - 4
```

The 0x2b0 in the first line matches the Offset we saw for .rela.text in the section header table; it is the same section being named. The output actually runs a bit further: three records under .rela.eh_frame, tending to the tables inside .eh_frame. That is a section we only learned the face of, so we will skip past it here without pasting it. Now let us think through where these two records come from. main's return needs to call extern_func, and the compiler emitted a call for it. To read extern_var, the compiler emitted another one, a rip-relative data reference. Each of the two instructions carries a 4-byte offset field, and the target address is something the compiler cannot supply either. What it can do is file a record in .rela.text: which position gets rewritten, which symbol is being sought, by what formula. main starts at 0x16, and the two records' Offsets are 0x1b and 0x21, both landing inside main's machine code. The symbol name at the end of each line writes the correspondence out plainly; there is nothing for us to guess.

Let us read it column by column. Offset points at where, in .text, the 4 bytes to be rewritten sit. Info packs the type and the symbol index into one number, and readelf has already undone it for us, laying the results into the Type and Sym. Name that follow, so we read those two directly. One more glance down the Sym. Value column: zeros all the way, because these symbols' values are equally unsettled. The columns with real meat on them are Type and the final Addend, and we will take them one at a time.

Where the -4 in the Addend comes from — for that we call RIP-relative addressing to the stand. Instructions of this kind on x86-64 do not write absolute addresses; they write an offset, and the base is the start of the next instruction, which is the value RIP holds when the instruction executes. Both instructions' offset fields happen to be the last 4 bytes of the entire instruction: the field's end is the instruction's end, and RIP sits precisely there. So between the base the CPU measures distance from (RIP, the field's end) and the base the linker fills the value from (P, the field's start) stand the field's own 4 bytes, and -4 is the correction that levels that gap. Laid out as formulas it looks like this: PLT32 goes by L + A - P, where L is the address of the PLT entry. PC32 goes by S + A - P, where S is the symbol's own address. The two formulas share a shape: the routine is target plus A minus P, and the result is the distance from this instruction to the target. Both records carry A = -4, and its origin is exactly those 4 bytes described above.

The two Type names we have to keep apart, because a widely circulated misreading lives here. Start from the surface: the call carries PLT32, the data reference carries PC32, one apiece as it happens. That division of labor is real: per the x86-64 psABI (processor-specific Application Binary Interface) and the convention in force since binutils 2.36, the assembler issues PLT32 for operands of branch instructions like call and jmp, and PC32 for rip-relative data references like lea and mov; it goes by the class of instruction. But read PLT32 as "this call is guaranteed to go through the PLT at run time" and you have read it sideways. PLT32's meaning is far milder: it marks a call site for which the linker may, when needed, build a PLT entry. We glimpsed the PLT from afar in part 01, the trampoline that function calls pass through; its dedicated walkthrough lives in part 09 (position independence and GOT/PLT). If the linker can establish that this symbol will not be shouldered aside by some other definition, it skips the trampoline and wires the call straight to the function body, and what it computes is indistinguishable from PC32. So at run time the two types compute exactly alike; the only difference is whether the linker was left room to build a trampoline. I checked this passage against the x86-64 psABI and maskray's research articles. maskray is a linker maintainer on the LLVM side (the other major open-source toolchain family besides GCC); if you want to dig deeper, R_X86_64_PLT32 is a ready-made keyword, and one search puts the articles in your hands.

And so the two blanks stand entered on the record, with the filling formulas written out as well. As for who does this work, we meet them at the end of this piece.

## Segment Totals: size and the -g Comparison

Sections are the .o's internal carving, and we have one higher vantage point besides: size, which reports the numbers to us summed by segment. Part 01 introduced the three names text, data, and bss: text holds code, data holds initialized data, bss holds uninitialized data, and dec and hex total the three columns:

```text
$ size demo.o
   text	   data	    bss	    dec	    hex	filename
    209	      8	      8	    225	     e1	demo.o
```

Now you can check it against nm's results: the 8 under data is exactly the D/d pair of ints summed. The 8 under bss records the B/b pair. The 209 under text we can count out as well, using precisely the Size column I trimmed off the section table: .text's machine code takes 41 bytes, .note.gnu.property takes 48, .eh_frame takes 120. All three numbers are booked under the text column, and together they come to exactly 209.

Let us compile one with debug information and see where the .o put on the weight:

```text
$ gcc -g -c demo.c -o demo_g.o && readelf -S demo_g.o | grep debug
  [ 5] .debug_info       PROGBITS ...
  [ 6] .rela.debug_info  RELA ...
  [ 7] .debug_abbrev     PROGBITS ...
  [ 8] .debug_aranges    PROGBITS ...
  ...plus .debug_line etc.
```

The section table has grown a row of sections headed by .debug_, and what is inside them is DWARF. DWARF is the format standard for debug information in the ELF world; gdb's breakpoints, stepping, and variable inspection all read these sections. The names are plain about their meaning, too: .debug_line records line numbers, .debug_info records types and variables. So did size gain weight? Put the two side by side:

```text
$ size demo.o demo_g.o
    text data bss dec hex filename
     209     8   8 225  e1 demo.o
     209     8   8 225  e1 demo_g.o
```

The three columns are identical. Debug information lives in its own .debug_* sections; the segments that get loaded into memory when the program runs have no place set for them, so size did not budge a byte. And demo.c is still the same file: with -g or without it, the portion that has to be loaded into memory at run time does not change. When we want to peel these off, there is a tool with that single job, strip, one more member of the binutils family: it shows the symbol table and the debug sections out the door in one go, and the file slims down on the spot. That not stripped line file reported back in part 01 — at the time we only called it good news; now we can say the whole of it: what it was holding on to is exactly this batch. Running strip before shipping a finished build is a common closing touch, too.

## Over on Windows: COFF and dumpbin

The tools in our hands are all Linux-side, and Windows has your counterparts too. The main compiler on the Windows side goes by MSVC, its full name Microsoft Visual C++; the object files it compiles carry the .obj suffix and use a format called COFF, its full name Common Object File Format, the Windows family's object file format. We do not look at it with nm but with dumpbin, which comes with Visual Studio; the subcommand for symbols is dumpbin /symbols, and what it prints is the COFF SYMBOL TABLE. Every concept maps across: our uppercase T, D, and B are written SECTn over there, meaning the symbol's definition lands in the nth section. And our U corresponds to UNDEF External. Sections, the symbol table, relocations: this set of things is not an ELF patent either. Windows changed the format names and the tool names, but the reading method we practice today works just the same.

## The CMake Perspective

We have been typing commands by hand in the terminal the whole way, but everything here also has a place kept for it in everyday CMake builds. Let us compare notes.

First, the object files themselves. Leave the type off add_library and the default is STATIC: the .o files get packed by ar into a .a, and the packing details are part 06's business (Static Libraries: Packaged with ar, Linked with -l/-L). If you would rather the .o files go to the linker exactly as they are, CMake keeps a type for precisely that: OBJECT

```cmake
add_library(demo_core OBJECT demo.c)
```

All it compiles out is object files; it does none of the packing, none of the library-producing. To let the app we have been building up since parts 02 and 03 use it, a single target_link_libraries(app PRIVATE demo_core) hooks it on, and these .o files go straight into app's link. When we have a batch of .o files that several targets share and we would rather not stand up a static library just for them, it fits the bill exactly.

Second, inspecting the products with the tools. If you want today's three tools to run automatically after the build, the path is nothing you need to copy out by hand: CMake stocks generator expressions. $<TARGET_FILE:app> is still a string of literal characters at configure time; at build time it expands into the full path of the app product. You can use it to attach a finishing command to app:

```cmake
add_custom_command(TARGET app POST_BUILD COMMAND nm $<TARGET_FILE:app>)
```

When the build finishes, it runs nm over the product for us on its own; the output lands in the build log, and we flip through the log to see the symbols. Where you hang it matters, too: it has to hang off a real binary target like app. An OBJECT library like demo_core will not take it — CMake rejects it with an error right at configure time.

Third, -g. For a product like demo_g.o, you do not need to stuff -g into the compile command yourself: part 03 laid out the CMAKE_BUILD_TYPE mapping, where the Debug setting carries -g by default and RelWithDebInfo gives -O2 plus -g. The caution from part 03 applies here as well: set no build type at all, and not one -g makes it into the command; what reaches your hand is still a plain object file.

## The Linker Takes Over

Every material we needed has now been met: the ELF header gave demo.o its pending identity, the section header table accounted for every block of bytes, the symbol table listed nine names, the relocation table entered the two blanks, and even the filling formulas are written. Next, the one who does the work steps on stage: in what order the linker digests these records; where it goes for the definitions behind the two U's hanging among our nine names; how the undefined reference it reports when they cannot be found gets produced; and how it rules when two files lay claim to the same name. The answers to all of these live in part 05, whose title reads The Linker: Symbol Resolution and undefined reference. Keep demo.o and do not delete it: the next piece still uses it as raw material.
