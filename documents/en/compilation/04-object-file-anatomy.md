---
chapter: 13
difficulty: intermediate
order: 4
platform: host
reading_time_minutes: 20
tags:
- cpp-modern
- host
- intermediate
- 工具链
title: 'Object File Anatomy: What''s Inside a .o'
description: "Read the ELF header and the section header table with readelf -h and -S, map the nine symbols back to demo.c one by one with nm, walk the readelf -r relocation records column by column down to the origin of the -4, then meet the DWARF debug sections that -g adds, the three-column totals from size, and the Windows-side counterpart of COFF and dumpbin."
cpp_standard: [11, 14, 17, 20]
translation:
  source: documents/compilation/04-object-file-anatomy.md
  source_hash: 2161618898666ea49367b0e144a971c48e072449f922d1ee02da41552919129f
  translated_at: '2026-10-13T03:55:58+00:00'
  engine: anthropic
  token_count: 4900
---

# Object File Anatomy: What's Inside a .o

At the end of part 03 we struck a deal: bring the tools along, and today we open the .o up. The questions on our list still number three: how the inside is laid out, what a relocation record actually looks like, and what .text and .data hold. The volume opener left an even earlier promise: that not stripped line next to hello.o was tremendous news, and the whole dissection of an object file leans on precisely the symbol table. Today we make good on those words, line by line.

The lead actor changes, though. The source of mangle.cpp does not hold a single global variable, so .data and .bss would have nothing to show. hello.cpp is too thin: six lines of code cannot carry a dissection of this size. So I have prepared a demo.c instead: global, static, initialized, uninitialized, declared-but-never-defined — it takes a share of every situation. It carries one more convenience: it is written in C, and as we saw in part 03, the C side skips the name mangling step entirely, so the symbol table holds nothing but plain names, and reading the output spares us a round of decoding.

The usual environment disclosure: my experiments all ran in WSL2, on a toolchain of GCC 16.2.1 with binutils 2.47, and every snippet of output in this piece is real, actually-run output. If your versions differ, details like the number of sections or the byte counts may not match, but the layout itself has not changed in years.

## Three Tools, Each Covering a Stretch

objdump, the one the end of part 03 told us to bring, now takes its seat at the table, and while we are at it we will get properly acquainted with its two stablemates. In the binutils family, the ones who work this trade come down to three:

- nm, who already did a job with us in part 03: it reads the symbol table — which names an object file carries and what standing each name holds — and reports the whole page to you.
- readelf, making its first appearance today: it reads nothing but ELF structure. File header, section header table, symbol table, relocation table — it lays any of them out for you exactly as they sit.
- objdump, the most senior of the three: its signature act is disassembly. Hand it -d, and it turns the machine code in .text back into assembly, one instruction at a time, for you to read.

The three overlap in their duties: the section table listing, for one, both readelf and objdump can print for you. What we are after today is structure and symbols, and the ones matching that description are readelf and nm. objdump's disassembly we save for the occasions that call for poring over machine code.

## The Dissection Subject: demo.c

Here is the code. Count along as you read: the global names written down come to nine in total.

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

Let us take the nine names in four groups. func and main are global functions with definitions. local_func is defined too, but it carries static, so its reach stays inside this translation unit. g_initialized_var and init_local_var are variables given initial values — one global, the other static. un_g_initialized_var is declared but never given a value, and un_init_local_var's initial value is equally blank. All that extern_var and extern_func hold is an extern declaration apiece: their definitions do not live in this file at all, and main goes and uses the two of them anyway.

Let us compile it. The product is today's dissection subject:

```bash
gcc -c demo.c -o demo.o
```

Where each of the nine names lands, we will now check with the tools, one by one.

## The ELF Header: REL, an Identity Left Pending

The output of readelf -h runs over a dozen lines; we take the four that matter most:

```text
  Class:                             ELF64
  Type:                              REL (Relocatable file)
  Machine:                           Advanced Micro Devices X86-64
  Entry point address:               0x0
```

Class says this is a 64-bit ELF, and Machine says the processor it targets is x86-64. The Type line reads REL, whose full name is Relocatable file. The relocatable that the file command reported back in the volume's first piece — this is its official wording. What does this identity mean? The Entry line confirms it a second time: the entry address reads 0x0. An executable's entry points at the program's first instruction. The hello_app built in the opener was a pie program — point readelf -h at it and Type reads DYN (an executable fixed at a definite address reads EXEC instead), and Entry points at a spot called `_start`. That is where the runtime library starts; everything before main falls to it, and we unfold that story in the program startup piece. The .o, by contrast, has not a single address settled, so a question like "where does execution start" cannot even be asked — hence the 0 here. This pending state is a product of the division of labor: settling addresses was never on the compilation stage's agenda; the whole job was left to the linker.

## The Section Header Table: Every Patch of Ground in the .o

ELF carves the file into pieces, each with its own name and purpose, and such a piece is called a section. readelf -S lays all the sections out as a table — the full output carries several more columns per section, Size and Flag among them, which I trimmed; the ellipses are mine. These are the columns we are about to use:

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

Let us learn what the columns mean: [Nr] is the index, Name is the section's name, Type is the identity of this patch of bytes, Address is the section's place in the final address space, and Offset is its place in the file. Sweep your eye down the Address column: nothing but 0s. The pending identity in the ELF header lands on every single section as pending in its own right — nobody knows where any of them will live. Offset, though, carries real values: .text's bytes, for instance, start at 0x40 in the file. Now let us visit these sections one at a time and see who lives in each.

.text's type is PROGBITS, meaning this patch holds genuine bytes. All the machine code we wrote lands here: the compiled bytes of local_func, func, and main lie packed one after another.

.data's type is also PROGBITS, and it holds initialized global and static variables. The initial value 1 we gave in demo.c is lying here right now, in byte form.

.bss's type is NOBITS, and this line deserves a second look: NOBITS declares exactly one thing — this section occupies zero bytes in the file. For un_g_initialized_var and un_init_local_var, which carry no initial value, what is stored is merely a size record; when the program starts, memory is carved out for this region and cleared to zero — the bytes only exist at runtime. What it saves, you can work out with a moment's thought: if we declare a 1MB uninitialized array, surely we are not going to lay down 1MB of zeros in the file? We can even verify this from the Offset column: .data starts at 0x6c and spans 8 bytes, which adds up to an endpoint of exactly 0x74. .bss's Offset also reads 0x74, but its type is NOBITS — no bytes of it exist in the file; that number is just the position assigned to it in the record-keeping.

.rela.text's type is RELA, and it holds relocation records; we have devoted a whole section to serving it further down.

.symtab and .strtab are a pair of partners: SYMTAB means symbol table and STRTAB means string table. Symbol names are not embedded directly in the symbol table; they are collected centrally in .strtab, and what the symbol table records is an offset pointing over there. The same mechanism has one more copy, .shstrtab — and whose strings do you suppose it holds? The section names' own. The dozen-odd section names before our eyes are housed by .shstrtab precisely.

We can count three items among the miscellany too. .comment holds the compiler's version notes, .note.GNU-stack is a stack-related note, and that [7] entry, .note.gnu.pr[...], records the instruction-set attribute flags demo.c uses — none of the three enters our main story. The name of [7] does deserve a word more: the [...] in its name is not the same thing as the ellipses I used to trim columns; that is readelf's mark for truncating long names at the default column width. Add -W and you see its full name: .note.gnu.property.

.eh_frame stores the tables used for stack unwinding — exception handling and stack backtraces both depend on it. We did not write a single exception in demo.c, yet the section still stayed in the file, along with the .rela.eh_frame that accompanies it. Today we merely learn its face; we will not spread out the details.

## The Symbol Table: Nine Names, Mapped Back to the Source One by One

Now is nm's turn on stage. Read its output against demo.c, and let us not let a single line slip past:

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

nm has sorted by name; the left column gives the address, the middle column is the letter, and the right column gives the symbol name — all nine names present, not one missing. Let us match them back group by group.

The first two lines have no address at all, and the letters are uniformly U. U stands for undefined: extern_var and extern_func hold only a declaration in this file, and their definitions live in other files. As we worked out in the volume opener, the undefined reference error comes from the linking leg of the journey. But which names are still hanging — that much was already written down at the .o stage, and nm shows it to us plain as day.

Now look at the letter case: func and main carry an uppercase T, local_func a lowercase t. Capital T reports three things at once: defined in this file, content in the code section, externally visible — other object files may reference it when linking. Lowercase t is the private edition of the same story: a function wearing static belongs to this translation unit alone. Match the three addresses as well: local_func sits at 0x0, func at 0xb, main at 0x16 — that is the three of them ranked inside .text, and they are all section-relative offsets, the very same pending state as the uniform 0s in the section table's Address column.

On to the variables: g_initialized_var's letter is D, init_local_var's is d. D corresponds to .data: initialized variables whose bytes genuinely sit in the file. Pair up the addresses too: one records 0x0 and the other 0x4 — two ints lined up one behind the other, dividing .data's 8 bytes exactly between them.

un_g_initialized_var's letter is B and un_init_local_var's is b; they correspond to .bss. The addresses are the same old 0x0-and-0x4 pairing, but here the addresses are mere numbering — the bytes will not exist until runtime, and we have already seen the inside story back in the section table discussion.

Have you spotted the pattern? This alphabet is really answering two questions. The first asks where the symbol lives: T is code, D is initialized data, B is uninitialized data — the letters are just the leading characters of the respective section names. A symbol without even a place to live gets a U. The second asks public or private: uppercase is public, lowercase is static-private. Look back at demo.c with these two rules and every static has landed as a lowercase letter. Let us press the rules into a table you can consult at will from now on:

| Letter | Meaning | Examples in demo.c |
|------|------|-----------------|
| U | Referenced only; the definition is not in this file | extern_var, extern_func |
| T / t | Code, public / static-private | func, main / local_func |
| D / d | Initialized data, public / static-private | g_initialized_var / init_local_var |
| B / b | Uninitialized data, public / static-private | un_g_initialized_var / un_init_local_var |

> The alphabet has other members too — the weak symbols W and V, for instance. They did not show up in this run's output; we will make their acquaintance when we run into them later on.

For the rest of this volume, every piece will put this table to work: whenever we want to see whether a library exports everything it should, or chase a symbol whose definition cannot be found, this is the first thing to open in the tool's output.

## The Relocation Table: Two Empty Slots and Their Filling Recipe

.rela.text, which we set aside when discussing the section table, now has its turn. Back in part 03, on the assembler as, we left one sentence hanging: when as meets a definition this file does not have, it leaves the address in the instruction empty and appends a relocation record, whose meaning is: the address here is not settled yet — we wait for the linker to fill it. The place where the records collect is the section before us:

```text
$ readelf -r demo.o
Relocation section '.rela.text' at offset 0x2b0 contains 2 entries:
  Offset          Info           Type           Sym. Value    Sym. Name + Addend
00000000001b  000a00000004 R_X86_64_PLT32    0000000000000000 extern_func - 4
000000000021  000b00000002 R_X86_64_PC32     0000000000000000 extern_var - 4
```

The 0x2b0 on the first line matches the Offset we saw for .rela.text in the section table — same section, no doubt about it. Further down the output there is actually one more short stretch: three records of .rela.eh_frame, serving the tables inside .eh_frame. We only learned that section's face, and here we likewise skip it rather than paste it. Let us think through where the two records before us come from: main's return calls extern_func, so the compiler emits a call. When it comes time to read extern_var, the compiler emits another rip-relative data reference. Each of the two instructions carries a 4-byte offset field, and the target address is something the compiler cannot supply either — the finishing work falls to as: it registers the unfillable fields in .rela.text, recording which position gets rewritten, which symbol is being sought, and which formula applies. main starts at 0x16; the two records' Offsets, 0x1b and 0x21, both fall inside main's machine code. The symbol names at the end of each line write the correspondence out plainly — no guessing required.

Let us read it column by column. Offset points at the position in .text of the 4 bytes to be rewritten. The Info column packs the type and the symbol index together, and readelf has already untangled them for us into the Type and Sym. Name columns that follow — we simply read those two. Glance at the Sym. Value column: uniform 0s, because these symbols' values are equally unsettled. Where the real subtlety lives is Type and the final Addend; let us take them in turn.

For the origin of that -4 in the Addend, let us bring in RIP-relative addressing. Instructions of this kind on x86-64 do not write an absolute address; they write an offset, and the baseline is the start of the next instruction — which is exactly the value RIP holds while the instruction executes. In both instructions, the offset field is precisely the last 4 bytes of the whole instruction: the field's end coincides with the instruction's end, and that is exactly where RIP lands. So between the baseline from which the CPU measures distance (RIP, the field's end) and the baseline from which the linker fills the value (P, the field's start) there sits the field's own 4 bytes — and -4 is the correction term that flattens out this gap. On the formulas it looks like this: PLT32 computes L + A - P, where L is the address of the PLT entry; PC32 computes S + A - P, where S is the address of the symbol itself. The two expressions share the same shape: both compute target plus A minus P, and both results are the distance from the target back to this instruction. Both records carry A = -4, and the origin of that is the 4 bytes described above.

The two Type names we must keep straight, because a widespread misreading circulates here. Start from the surface: the call carries PLT32, the data reference carries PC32 — one apiece in our output. That division of labor itself is real: per the x86-64 psABI (processor-specific Application Binary Interface, the processor-specific binary interface specification) and the convention adopted as of binutils 2.36, the assembler issues PLT32 for operands of branch instructions like call and jmp, and PC32 for rip-relative data references like lea and mov — it looks at the instruction's category. But if you read PLT32 as "this call will definitely go through the PLT at runtime," you have twisted it. PLT32's meaning is far milder: it marks a call site for which the linker may build a PLT entry if it needs one. We glimpsed the PLT from afar in the volume opener — the trampoline that function calls walk across; its dedicated treatment lives in the position-independence piece. If the linker can determine that this symbol will not be overridden by a definition elsewhere, it skips the trampoline and connects the call straight to the function body, and the computed result is no different from PC32's. So the two types are fully isomorphic in their runtime computation; the only difference is whether room is left for the linker to build a trampoline. I have checked this account against the x86-64 psABI and maskray's research write-ups. maskray is a linker maintainer on the LLVM side (LLVM being the other mainstream open-source toolchain besides GCC); if you want to dig deeper, R_X86_64_PLT32 is a ready-made keyword — one search and the articles are in hand.

The two empty slots now stand registered on record, with the filling formulas written out. Who the worker is that does this job — we shall meet them at the end of the piece.

## Segment Totals: size Against -g

Sections are the .o's internal division; we also have a higher vantage point: size, which reports the numbers totaled up by segment. In the volume's first piece we made the acquaintance of the names text, data, and bss: text holds code, data holds initialized data, bss holds uninitialized data, and dec and hex are the totals of the three columns:

```text
$ size demo.o
   text	   data	    bss	    dec	    hex	filename
    209	      8	      8	    225	     e1	demo.o
```

Now you can cross-check against nm's results: data's 8 is exactly the total of the D/d pair of ints. bss's 8 records the B/b pair. text's 209 we can count out too, using the very Size column I trimmed from the section table: .text's machine code takes 41 bytes, .note.gnu.property takes 48, and .eh_frame takes 120 — all three numbers are booked under the text column, and their total is indeed 209.

Now let us build a second copy with debug information and see where the .o put on weight:

```text
$ gcc -g -c demo.c -o demo_g.o && readelf -S demo_g.o | grep debug
  [ 5] .debug_info       PROGBITS ...
  [ 6] .rela.debug_info  RELA ...
  [ 7] .debug_abbrev     PROGBITS ...
  [ 8] .debug_aranges    PROGBITS ...
  ... plus .debug_line and the rest
```

The section table has grown a row of sections starting with `.debug_`, and what they hold is DWARF. DWARF is the debug information format standard of the ELF world; gdb's breakpoints, single-stepping, and variable inspection all read these sections. The names speak plainly too: .debug_line records line numbers, .debug_info records types and variables. So did size put on weight? Let us put the two side by side:

```text
$ size demo.o demo_g.o
    text data bss dec hex filename
     209     8   8 225  e1 demo.o
     209     8   8 225  e1 demo_g.o
```

The three columns are identical. The debug information lives in its own .debug_* sections, which have no place in the segments loaded into memory at runtime, so size has not moved a byte. And demo.c is still the same file — with or without -g, the part that gets loaded into memory at runtime has not changed. When we want to shed them, a dedicated tool exists: strip, yet another member of the binutils family, which ushers out the symbol table and the debug sections together, and the file slims down on the spot. That not stripped line file reported in the volume opener — we offered our congratulations at the time, and now we can state it in full: what it preserves is precisely this batch of things. Running strip once before shipping a finished product is a common closing step too.

## The Windows Side: COFF and dumpbin

The tools in our hands are all Linux-side, but on Windows you have counterparts. The main compiler over there is called MSVC, short for Microsoft Visual C++; the object files it produces carry the .obj suffix and use a format called COFF, short for Common Object File Format — the Windows family's object file format. We do not inspect it with nm but with dumpbin, which ships with Visual Studio; the subcommand for looking up symbols is dumpbin /symbols, and what it prints is a COFF SYMBOL TABLE. Every concept maps straight across: our uppercase T, D, B are written there as SECTn, meaning the symbol's definition falls in section number n. Our U corresponds to UNDEF External. Sections, symbol tables, relocations — none of this apparatus is ELF's patent; Windows swapped in different format and tool names, but the reading we practiced today works just the same.

## The CMake View

We have been typing commands by hand in the terminal the whole way through, but in day-to-day builds CMake has a place reserved for all of this too. Let us walk the correspondence.

First, the object file itself. When add_library is written without a type, the default is STATIC: the compiled .o files get packed into a .a by ar, and the packing details belong to the static library piece. If you want your .o files to stay as they are and go straight to the linker, CMake keeps a type on the shelf for exactly this: OBJECT.

```cmake
add_library(demo_core OBJECT demo.c)
```

What it builds is object files and nothing else — packing and library-emitting work are not its jobs. The projects we accumulated in the preprocessing and compiler pieces all used that app. To have the app take it on, one line of target_link_libraries(app PRIVATE demo_core) attaches it, and these .o files go straight into the app's link. When we have a batch of .o files shared by several targets and would rather not stand up a separate static library for them, this is exactly our fit.

Second, let us point the tools at the build product. If you want today's three tools to run automatically after the build, you need not copy paths by hand — CMake keeps a generator expression in reserve: $<TARGET_FILE:app> is still a string literal at configure time and only expands, at build time, into the full path of the app product. You can use it to attach a closing command to the app:

```cmake
add_custom_command(TARGET app POST_BUILD COMMAND nm $<TARGET_FILE:app>)
```

Once the build finishes, it runs nm over the product for us automatically, the output lands in the build log, and we can read the symbols straight from the log. Where you hang it matters too: it has to hang on a genuine binary target like app — an OBJECT library like demo_core cannot take it, and CMake refuses it outright with an error at configure time.

Third, -g. If you want a product like demo_g.o, there is no need to stuff -g into the compile command yourself: the CMAKE_BUILD_TYPE correspondence table appeared in part 03 — the Debug tier carries -g by default, and RelWithDebInfo gives -O2 plus -g. The warning from that piece applies here just the same: if you never set a build type at all, not a single -g makes it into the command, and what you get is still a bare object file.

## The Linker Takes Over

All the material we need has now been seen: the ELF header issued demo.o a pending identity, the section table accounted for every patch of bytes, the symbol table listed nine names, and the relocation table recorded two empty slots — with the filling recipe written out, no less. Now the worker takes the stage: in what order does the linker digest these records, where does it go hunting for the definitions behind the two Us hanging among the nine names, how is the undefined reference error it raises when it finds nothing actually generated, and how does it adjudicate when two files carry the same name. The answers to all of these live in the next piece, titled "The Linker: Symbol Resolution and undefined reference". Keep demo.o and do not delete it — that piece uses it as raw material too.
