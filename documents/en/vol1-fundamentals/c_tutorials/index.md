---
translation:
  source: documents/vol1-fundamentals/c_tutorials/index.md
  source_hash: f83dad601ea72aa73f88dc6c42f2540247d267faded1192dc066e58741e5aebb
  translated_at: '2026-09-25T12:41:32+00:00'
  engine: anthropic
  token_count: 1600
---
# Comprehensive C Language Tutorial

PS: this part of the tutorial is not aimed at absolute beginners. It grew out of the C notes we took back in our embedded-development days, and C was already firmly in hand by the time those notes were written. So if you actually need to learn C from scratch, head over to this repo instead:

> [GitHub: The C Language Journey](https://github.com/Awesome-Embedded-Learning-Studio/C-Journey)
> [Website: C Journey](https://awesome-embedded-learning-studio.github.io/C-Journey/)

The C tutorials here are geared toward people who once learned C but have since forgotten what it looks like.

## Fundamentals

<ChapterNav variant="sub">
  <ChapterLink num="01" href="01-program-structure-and-compilation" desc="The basic structure of a C program, the four-stage compilation pipeline, the header file mechanism, and basic I/O">Program Structure and Compilation Basics</ChapterLink>
  <ChapterLink num="02A" href="02A-data-types-basics" desc="The integer family, signed vs. unsigned, fixed-width types, and sizeof">Data Type Basics: Integers and Memory</ChapterLink>
  <ChapterLink num="02B" href="02B-float-char-const-cast" desc="Floating-point precision, character encoding, the const qualifier, and implicit type conversion">Floating Point, Characters, const, and Type Conversion</ChapterLink>
  <ChapterLink num="03A" href="03A-operators-basics" desc="Arithmetic, relational, and logical operators, short-circuit evaluation, and assignment operators">Operator Basics: Making Data Move</ChapterLink>
  <ChapterLink num="03B" href="03B-bitwise-and-evaluation" desc="Bitwise operations, shifting caveats, precedence traps, and sequence points">Bitwise Operations and Evaluation Order</ChapterLink>
  <ChapterLink num="04" href="04-control-flow" desc="Conditional branches, loops, switch fall-through, and the state machine pattern">Control Flow: Teaching Programs to Choose and Repeat</ChapterLink>
  <ChapterLink num="05" href="05-function-basics" desc="Function declaration/definition/call, pass-by-value, pointer parameters, and recursion">Function Basics and Parameter Passing</ChapterLink>
  <ChapterLink num="06" href="06-scope-and-storage" desc="Scope rules, storage classes, linkage, and the three uses of static">Scope and Storage Classes</ChapterLink>
  <ChapterLink num="07A" href="07A-pointer-essentials" desc="The memory model, address-of and dereference, pointer arithmetic, and distance computation">Pointer Basics: The World of Addresses</ChapterLink>
  <ChapterLink num="07B" href="07B-pointers-arrays-const" desc="Array-to-pointer decay, combining const with pointers, NULL, and wild pointers">Pointers, Arrays, const, and Null Pointers</ChapterLink>
  <ChapterLink num="08A" href="08A-multi-level-pointers" desc="The memory model of multilevel pointers, array of pointers vs. pointer to array, and reading declarations with cdecl">Multilevel Pointers and Reading Declarations</ChapterLink>
  <ChapterLink num="08B" href="08B-restrict-incomplete-types" desc="restrict optimization, forward declarations, and the opaque pointer pattern">restrict, Incomplete Types, and Structure Pointers</ChapterLink>
  <ChapterLink num="09" href="09-function-pointers-and-callbacks" desc="Declaring and using function pointers, the callback pattern, and event-driven programming">Function Pointers and the Callback Pattern</ChapterLink>
  <ChapterLink num="10" href="10-arrays-deep-dive" desc="Memory layout, multidimensional arrays, variable-length arrays, and their relationship with pointers">A Deep Dive into Arrays</ChapterLink>
  <ChapterLink num="11" href="11-c-strings-and-buffer-safety" desc="The \0-terminated model, core string.h functions, and preventing buffer overflows">C Strings and Buffer Safety</ChapterLink>
  <ChapterLink num="12" href="12-struct-and-memory-alignment" desc="Struct definitions, alignment and padding rules, and flexible array members">Structures and Memory Alignment</ChapterLink>
  <ChapterLink num="13" href="13-union-enum-bitfield-typedef" desc="Type punning and hardware register mapping, compared with type-safe C++ alternatives">Unions, Enums, Bit Fields, and typedef</ChapterLink>
  <ChapterLink num="14" href="14-dynamic-memory" desc="malloc/calloc/realloc/free, common memory errors, and debugging">Dynamic Memory Management</ChapterLink>
  <ChapterLink num="15" href="15-preprocessor-and-multifile" desc="Macros, conditional compilation, header guards, and modular multi-file projects">The Preprocessor and Multi-File Projects</ChapterLink>
  <ChapterLink num="16" href="16-file-io-and-stdlib" desc="File reading and writing, formatted I/O, and command-line argument handling">File I/O and Standard Library Overview</ChapterLink>
</ChapterNav>

## Advanced Topics

The advanced topics live in the [advanced_feature/](advanced_feature/) subdirectory and cover more in-depth subjects:

<ChapterNav variant="sub">
  <ChapterLink num="01" href="advanced_feature/01-arm-architecture-fundamentals" desc="The ARM Cortex-M instruction set, registers, the exception vector table, and processor modes">ARM Architecture and Fundamentals</ChapterLink>
  <ChapterLink num="02" href="advanced_feature/02-cache-and-memory-hierarchy" desc="Cache lines, mapping strategies, the MESI protocol, and cache-friendly programming">Cache Mechanisms and Memory Hierarchy</ChapterLink>
  <ChapterLink num="03" href="advanced_feature/03-c-traps-and-pitfalls" desc="Syntax and semantic traps, compiler behavior, and analysis against the standard">C Traps and Common Pitfalls</ChapterLink>
  <ChapterLink num="04" href="advanced_feature/04-oop-in-c" desc="Simulating classes with structs plus function pointers, encapsulation, inheritance, and polymorphism">Object-Oriented Programming in C</ChapterLink>
  <ChapterLink num="05" href="advanced_feature/05-handmade-dynamic-array" desc="A type-safe dynamic array library, growing and shrinking memory, and API design">Building a Dynamic Array from Scratch</ChapterLink>
  <ChapterLink num="06" href="advanced_feature/06-handmade-linked-list" desc="Insertion, deletion, and search algorithms, plus sentinel node tricks">Building a Singly Linked List</ChapterLink>
  <ChapterLink num="07" href="advanced_feature/07-embedded-c-patterns" desc="Register access, volatile, interrupt safety, and peripheral abstraction layers">Embedded C Programming Patterns</ChapterLink>
  <ChapterLink num="08" href="advanced_feature/08-reusable-c-code" desc="Modular design, opaque pointers, and platform abstraction layers">Designing Reusable C Code</ChapterLink>
</ChapterNav>
