# Comprehensive C Language Tutorial

PS: This part of the tutorial is not aimed at complete beginners. It grew out of a set of C notes taken back when working in embedded development, by which point C was already well in hand. So if you actually need to learn C from scratch, head over to this repo instead:

> [GitHub: C Journey](https://github.com/Awesome-Embedded-Learning-Studio/C-Journey)
> [Website: C Journey](https://awesome-embedded-learning-studio.github.io/C-Journey/)

The C tutorials here are better suited for people who once learned C but have forgotten what it looks like.

## Fundamentals

| # | Article | Description |
|---|---------|-------------|
| 01 | [Program Structure and Compilation Basics](01-program-structure-and-compilation.md) | Basic structure of C programs, the four-stage compilation process, header file mechanism, and basic I/O |
| 02A | [Data Type Basics: Integers and Memory](02A-data-types-basics.md) | Integer family, signed vs. unsigned, fixed-width types, and sizeof |
| 02B | [Floating-Point, Characters, const, and Type Conversion](02B-float-char-const-cast.md) | Floating-point precision, character encoding, const qualifier, and implicit type conversion |
| 03A | [Operator Basics: Making Data Move](03A-operators-basics.md) | Arithmetic, relational, and logical operators, short-circuit evaluation, and assignment operators |
| 03B | [Bitwise Operations and Evaluation Order](03B-bitwise-and-evaluation.md) | Bitwise operations, shift caveats, precedence traps, and sequence points |
| 04 | [Control Flow: Teaching Programs to Choose and Repeat](04-control-flow.md) | Conditional branching, loops, switch fall-through, and the state machine pattern |
| 05 | [Function Basics and Parameter Passing](05-function-basics.md) | Function declaration/definition/calling, pass-by-value, pointer parameters, and recursion |
| 06 | [Scope and Storage Class](06-scope-and-storage.md) | Scope rules, storage classes, linkage, and the three uses of static |
| 07A | [Pointer Essentials: The World of Addresses](07A-pointer-essentials.md) | Memory model, address-of and dereference operators, pointer arithmetic, and distance calculation |
| 07B | [Pointers, Arrays, const, and Null Pointers](07B-pointers-arrays-const.md) | Array-to-pointer decay, const and pointer combinations, NULL, and wild pointers |
| 08A | [Multi-Level Pointers and Declaration Reading](08A-multi-level-pointers.md) | Multi-level pointer memory model, array of pointers vs. pointer to array, and cdecl reading |
| 08B | [restrict, Incomplete Types, and Struct Pointers](08B-restrict-incomplete-types.md) | restrict optimization, forward declarations, and the opaque pointer pattern |
| 09 | [Function Pointers and the Callback Pattern](09-function-pointers-and-callbacks.md) | Function pointer declaration and usage, the callback pattern, and event-driven programming |
| 10 | [Arrays Deep Dive](10-arrays-deep-dive.md) | Memory layout, multi-dimensional arrays, variable-length arrays, and their relationship with pointers |
| 11 | [C Strings and Buffer Safety](11-c-strings-and-buffer-safety.md) | `\0` null-terminated model, core string.h functions, and buffer overflow prevention |
| 12 | [Structs and Memory Alignment](12-struct-and-memory-alignment.md) | Struct definition, alignment padding rules, and flexible array members |
| 13 | [Unions, Enums, Bit Fields, and typedef](13-union-enum-bitfield-typedef.md) | Type punning, hardware register mapping, and comparison with C++ type-safe alternatives |
| 14 | [Dynamic Memory Management](14-dynamic-memory.md) | malloc/calloc/realloc/free, common memory errors, and debugging |
| 15 | [Preprocessor and Multi-File Projects](15-preprocessor-and-multifile.md) | Macros, conditional compilation, header guards, and modular multi-file projects |
| 16 | [File I/O and Standard Library Overview](16-file-io-and-stdlib.md) | File reading and writing, formatted I/O, and command-line argument processing |

## Advanced Topics

Advanced topics are located in the [advanced_feature/](advanced_feature/) subdirectory, covering more in-depth subjects:

| # | Article | Description |
|---|---------|-------------|
| 01 | [ARM Architecture and Fundamentals](advanced_feature/01-arm-architecture-fundamentals.md) | ARM Cortex-M instruction set, registers, exception vector table, and processor modes |
| 02 | [Cache Mechanisms and Memory Hierarchy](advanced_feature/02-cache-and-memory-hierarchy.md) | Cache lines, mapping strategies, the MESI protocol, and cache-friendly programming |
| 03 | [C Traps and Common Pitfalls](advanced_feature/03-c-traps-and-pitfalls.md) | Syntax and semantic traps, compiler behavior, and standard specification analysis |
| 04 | [Implementing OOP in C](advanced_feature/04-oop-in-c.md) | Simulating classes with structs and function pointers, encapsulation, inheritance, and polymorphism |
| 05 | [Building a Dynamic Array from Scratch](advanced_feature/05-handmade-dynamic-array.md) | Type-safe dynamic array library, memory resizing, and API design |
| 06 | [Building a Singly Linked List from Scratch](advanced_feature/06-handmade-linked-list.md) | Insertion, deletion, and search algorithms, along with sentinel node techniques |
| 07 | [Embedded C Programming Patterns](advanced_feature/07-embedded-c-patterns.md) | Register access, volatile, interrupt safety, and peripheral abstraction layers |
| 08 | [Building Reusable C Code](advanced_feature/08-reusable-c-code.md) | Modular design, opaque pointers, and platform abstraction layers |
