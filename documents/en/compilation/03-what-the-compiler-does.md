---
chapter: 13
difficulty: intermediate
order: 3
platform: host
reading_time_minutes: 18
tags:
- cpp-modern
- host
- intermediate
title: "What the Compiler Does: From C++ to Assembly"
description: "Read the symbol table side by side with nm and c++filt, see how name mangling encodes namespaces, overloads, and parameter types into symbol strings, why every use of a template spawns another copy of code, where the -O0 and -O2 assemblies differ, and finally how the assembler as turns .s into .o and records the addresses it cannot fill as relocations"
cpp_standard: [11, 14, 17, 20]
translation:
  source: documents/compilation/03-what-the-compiler-does.md
  source_hash: 4955e3bb8aac4567ad5feffc95efb420b4aed629116b028f304a647ea9e648b9
  translated_at: '2026-09-30T05:53:28+00:00'
  engine: glm
  token_count: 4007
---

# What the Compiler Does: From C++ to Assembly

At the end of the last piece we left hello.i sitting on the table: 44,971 lines of plain text, not a single `#` directive left in it. Today the compiler takes over. In many people's minds, the compiler's job is to catch errors: it yells when your syntax is wrong, yells again when the types don't line up, then translates our code into assembly and calls it a day. Catching errors and translating really are its main business. Back in part 01 we left a remark: inside the compiler, every layer from parsing through optimization to instruction selection is worth a whole piece of its own. Today we're not drilling into the algorithms in those layers; we're watching from the other end: your source goes in, and we look at where exactly the compiler laid hands on the assembly that comes out. There are three places it does. We'll take them one at a time. Function names: re-encoded. The one copy of code you wrote against a template: turned into one copy per type. And if you turn on optimization, it will even rewrite how your loops are written. That `_ZSt4cout` string from part 01 was the first item's handiwork, and we agreed back then that the formal walkthrough would wait until today.

As usual, the environment disclosure: all my experiments ran in WSL2, with GCC 16.2.1 plus binutils 2.47 as the toolchain at hand, and every snippet of output in this piece was actually run. If your versions differ, details like symbols and instructions may vary, but the mechanics themselves haven't changed.

## Our Test Subject

What about hello.i? Its job ended at the handoff: of those forty-some-thousand lines, headers take the overwhelming majority, and digging through it for the symbol naming patterns we want today would be too much pain. The six-line hello.cpp can't carry it either, because it simply doesn't have enough roles on hand. So I prepared a `mangle.cpp` that fields every role we want to look at today: a free function, a namespace, a const member function, and a function template.

```cpp
// mangle.cpp
int int_max(int a, int b);
namespace util {
    int scale(int v, int k);
    struct Tracker { int count; int get() const; };
}
template <typename T>
T bigger(T a, T b) { return a > b ? a : b; }
int use_all() {
    util::Tracker t{};
    return int_max(1, 2) + util::scale(3, 4) + t.get() + bigger(10, 20) + bigger(1.5, 2.5);
}
```

Only use_all and bigger come with bodies; int_max, scale, and get are declarations and nothing more. Leaving the implementations out is deliberate: today we compile only as far as the .o and never link, so missing implementations are no obstacle.

## Name Mangling: The Linker Only Knows Strings

The first rewrite lands on function names. C++ lets us overload functions, lets us tuck functions into namespaces, and lets our member functions share a name with ordinary functions. The linker recognizes none of these schemes, though. When it resolves symbols, all it holds in its hands are strings of characters. util::scale and somebody else's scale: how do the two stay distinguishable at the linker? The compiler's move is to encode the namespace, the class name, and the parameter types all into the function's symbol name, and this action goes by the name of name mangling.

Let's compile mangle.cpp into a .o, then ask nm to pull out its symbol table. nm is from the binutils family, and its whole job is reading the symbols inside object files. We'll use grep to keep only two classes of symbols in view: those starting with T are defined in this file, and those starting with U are referenced from elsewhere:

```text
$ g++ -c mangle.cpp && nm mangle.o | grep -E " (T|U) "
                 U _Z7int_maxii
0000000000000000 T _Z7use_allv
                 U _ZN4util5scaleEii
                 U _ZNK4util7Tracker3getEv
                 U __stack_chk_fail
```

The long numbers on the left are addresses; the U lines don't even have one, because their implementations are not in this file at all. On the right are the symbols the linker will have to recognize. Scan down the list from the top and you'll find that not a single name you know survives: plain old int_max has become `_Z7int_maxii`. Don't panic. We'll decode them one at a time.

Let's warm up on the shortest one, `_Z7use_allv`. The reading comes down to three rules:

- The leading `_Z` is the fixed prefix every mangled symbol carries.
- The 7 that follows means the name takes 7 characters, and the use_all after it is exactly that.
- With the name read off, the leftover v is the parameter list. v corresponds to void, and this function of ours takes no arguments.

So the whole string reads back as use_all(). Decoding isn't something we have to do by hand: the dedicated tool for it is c++filt, which already made its appearance in part 01. Feed it the four `_Z`-prefixed symbols one at a time, and what it hands back is:

```text
int_max(int, int)
use_all()
util::scale(int, int)
util::Tracker::get() const
```

Of the remaining three symbols, the only new structural marks to learn are N, E, and K, and we'll take in the type-code letters while we're at it: i corresponds to int, d corresponds to double, same routine as v. You can already read `_Z7int_maxii`: int_max with 7 characters, then ii for two int parameters. Next up is `_ZN4util5scaleEii`. Read the N and E inside it as a pair of brackets that governs nested names: 4util is util with 4 characters, 5scale is scale with 5 characters, so together they read util::scale, and the ii at the end is still two ints. `_ZNK4util7Tracker3getEv` adds one more letter: K records const. Here it is attached to a member function; a const elsewhere in a type uses the same mark, so it decodes back to util::Tracker::get() const. This set of encoding rules comes out of the Itanium C++ ABI, a specification for C++ binary interfaces, and it is what GCC uses on Linux. Whether cross-compiler and cross-platform setups share the same encoding is a question we leave to part 12 (ABI Design: Cross-Toolchain Interfaces); being able to recognize and decode is enough for us.

With this in hand, the overloading business falls into place. Suppose we add another int_max overload that eats two doubles: the parameter types get encoded into the symbol too, so the int version's symbol is `_Z7int_maxii` and the double version becomes `_Z7int_maxdd`. What the linker receives is two unrelated symbols, and that is how overloading survives. Think about it: without this layer of encoding, both overloads would have to be named int_max, and what would the linker use to tell them apart?

Then there is `__stack_chk_fail` on the list. None of us wrote it, yet there it is. It is a product of the stack protector: GCC turns the stack protector on by default in this environment of ours, adding a check to each function's stack frame, and when the check finds a problem, this is who gets called. It wears an underscore on each end and carries no _Z prefix, because it is a C interface provided by the runtime library in the first place and never goes through C++ mangling.

## extern "C": Turning the Mangling Off

In some situations it is the reverse: we need the mangling turned off, and the most common case is calling C libraries. The C compiler on this Linux toolchain of ours does no name mangling, and the symbols it exports are the function's plain name. If our C++ code wants to use their libraries, our symbols have to look the way they recognize; otherwise the names don't match up, and at link time nobody finds anybody. We have a very direct method for this: hang `extern "C"` in front of the declaration.

```cpp
// externc.cpp
extern "C" int int_max(int a, int b);
int use() { return int_max(1, 2); }
```

```text
$ g++ -c externc.cpp && nm externc.o | grep -E " (T|U) "
0000000000000000 T _Z3usev
                 U int_max
```

It is still the same int_max; the only difference is the `extern "C"` in front of the declaration, and the symbol comes out clean, with nothing left but the bare name int_max. Put the two outputs side by side: with mangling on, its symbol is `_Z7int_maxii`. With mangling off, only a bare int_max remains. The formal name of this mechanism is language linkage, and when you look up linkage in the standard, this is what you find.

How do C headers put it to use? Back in part 02, when we printed the predefined macros with -dM, we already met the `__cplusplus` macro: it is defined by the C++ compiler, so the C side cannot see it. C library headers commonly run a combination built on that. `#ifdef __cplusplus` asks whether the include is coming from C++, and if C++ has come, an entire batch of declarations gets wrapped in an `extern "C"` block. When the C++ side includes the header, the declarations get C linkage. When the C side includes it, that middle stretch might as well not exist. Next time you open a C library header and see these lines again, you'll know what they are doing.

The price of turning the mangling off is that overloading cannot survive. Think it through: if two int_max overloads both wore `extern "C"`, both symbols would be the bare name int_max, one name holding two different functions. So a set of overloads sharing one name cannot all be handed over to C linkage. Try it for real, and GCC will stop you on the spot with an error that says conflicting declaration. I ran it once more while writing this, and out came one error, plus a note pointing back at the earlier declaration.

## Templates: Only the Types You Use Get Code

The second rewrite falls to templates. In mangle.cpp we called bigger exactly twice: once with integers, which landed as bigger<int>, and once with floating-point values, which landed as bigger<double>. So what does bigger look like in the assembly? One copy of code that runs for any type, or one copy per type? Let's just look. Inlining, in one sentence: skip the function call and lay the function body down right at the call site. bigger is too small for its own good; once optimization is on, odds are it gets inlined and the standalone body may no longer exist. So I added -fno-inline to the command, turning off ordinary inlining with only functions marked with the always_inline attribute excepted, so that we can count the code bigger spawned:

```text
$ g++ -S -fno-inline mangle.cpp -o mangle_noInline.s && grep "^_Z6bigger" mangle_noInline.s
_Z6biggerIiET_S0_S0_:
_Z6biggerIdET_S0_S0_:
```

Hand the two labels grep caught to c++filt, and what they decode to is:

```text
int bigger<int>(int, int)
double bigger<double>(double, double)
```

There is the answer, sitting right in front of us: bigger exists twice in the assembly, matching our two calls exactly. We call the concrete code generated from a template an instance, and the generating process instantiation. A template itself never turns into any code, and the compiler works on demand: you use one combination of types, and it generates one copy for us. Nobody used bigger<char>, and bigger<char> is nowhere to be found in the entire assembly. There is one more thing the symbol tells us: the template arguments get encoded too, and the Ii in `_Z6biggerIiET_S0_S0_` corresponds to <int>. The T_ and S0_ after it are shorthand for types; we don't need to know how to write them, just hand them over for decoding.

This settles half of an old question: why do template definitions usually have to live in header files? For the compiler to generate bigger<int>, the precondition is that it can see bigger's complete function body within this translation unit; with only a declaration to work from, there is nothing it can compile. So the template definition has to travel with the header, and whoever uses it needs the body in their own translation unit. And the next question follows right along: if ten translation units all use bigger<int>, don't we end up with ten identical copies of code? Why don't they fight at link time? That is the mess for part 07 (templates, inline, and the ODR) to clean up, so we'll bank the question there. ODR stands for the one definition rule, and we'll unfold it properly when we get to part 07.

## -O0 vs -O2: Two Assemblies from the Same Source

For the third rewrite, the compiler's latitude is far greater. We'll take the most ordinary summing function as our sample:

```cpp
// opt_demo.cpp
int sum_to(int n) {
    int s = 0;
    for (int i = 1; i <= n; ++i) s += i;
    return s;
}
```

Today's experiments have not written a single -O, which means everything has been running at the default level, -O0. A few lines excerpted from its assembly:

```asm
	movl	%edi, -20(%rbp)
	movl	$0, -8(%rbp)
	movl	$1, -4(%rbp)
	movl	-4(%rbp), %eax
	addl	%eax, -8(%rbp)
	addl	$1, -4(%rbp)
```

Let's walk these lines one by one. %edi is the register parameter n arrives in, and the first instruction stores it at -20(%rbp) on the stack. Next come s's 0 and i's 1, landing at -8(%rbp) and -4(%rbp) respectively. The three loop-body instructions are the same story: we haul i off the stack into %eax, add %eax into s on the stack, and do the increment on i, still on the stack. You see it: every variable from the source has its own spot on the stack, and every step dutifully loads, computes, and stores back. Not one wasted motion, and not one shortcut either.

Switch the level to -O2, and the excerpt takes on another face:

```asm
	leal	1(%rdi), %ecx
	movl	$1, %eax
	movl	$2, %eax
	movl	$1, %edx
	leal	1(%rdx,%rax,2), %edx
```

The stack is gone; these lines never even graze %rbp. The arithmetic has all moved into registers, and the constants 1 and 2 are baked straight into the instructions. The loop itself got rewritten too: GCC gave it a partial unrolling, so one round does different work than before. But there is one direction you will most likely guess wrong: the number of instructions in the file did not shrink. For our sum_to, the -O0 body comes to 15 instructions, while -O2 comes to 23. We count this by mnemonics: objdump's disassembly prints 24 address lines, one of which is a continuation byte of the 10-byte alignment instruction and does not qualify as an independent instruction, so counting mechanically by lines would overcount by one. What optimization saves is the instructions actually executed when the program runs: each round computes several more numbers, the loading and storing on the stack is waived entirely, and the number of instructions actually executed drops by a large chunk. Yet the semantics of the source have not moved at all: feed sum_to(100) in, and 5050 still comes back out. Your program cannot observe any difference.

These two levels are not our only choices: GCC also has -O1, -O3, and -Os. -O3 rewrites more aggressively than -O2, and -Os puts the size of the binary first in line. Whichever level you pick changes how hard the compiler comes down. One more thing worth stating: every rewrite we just looked at happens entirely inside the compiler, and what the linker receives is already the finished product. LTO (link-time optimization) in part 15 pushes the optimization boundary over toward linking, but that is a story for later.

## The Assembler as: Translate Line by Line, Make No Decisions

We have arrived at the assembly leg of the trip, and the one taking over is as, whom we met in part 01. Its line in the g++ -v output looks like this:

```text
 as -v --64 -o xxx.o xxx.s
```

The job it receives is simple: translate the instructions in the .s one by one into machine-code bytes, then pack the result into a .o following the ELF format. Labels become addresses, symbols get registered in the symbol table, and the translation is done. Don't expect it to understand C++ or do optimization. Every decision that needed making was already made when the compiler wrote the .s; all that is left for as is the mechanical word-for-word translation.

There is one class of things, though, that neither the compiler nor as can settle, and we need to look at it separately. Recall that batch of U symbols in mangle.o: int_max's implementation is not in this file, so for the call instruction that calls it, what should the target address be filled with? Nobody knows at compile time, and it is the same story when the translation reaches as. What as does is leave the address in the instruction blank, then append a relocation record, whose message reads: this address is not decided yet, and we'll wait for the linker to fill it in. So the .o in our hands holds more than code and symbols. It also carries a batch of pending addresses.

## The CMake Perspective: Who Owns the Optimization Level

In your day-to-day builds you mostly don't write -O yourself; that is CMake's CMAKE_BUILD_TYPE's business. Let's lay out the mapping for the GNU compiler family: Debug gives you -g, and we get not a shred of optimization. Release switches to -O3 and brings -DNDEBUG along, so assertions like assert are turned off. RelWithDebInfo gives -O2 plus -g, with optimization and debug information both in full, and it is the one we reach for most in everyday releases. It carries -DNDEBUG all the same, though, so assert is off at this level too. MinSizeRel uses -Os plus -DNDEBUG.

If you never set CMAKE_BUILD_TYPE at all, its value is empty, CMake will not add a single -O on your behalf, and the compiler falls back to the default -O0. This is an easy trap to fall into. The program builds, runs inexplicably slowly, we chase it all the way down, and the cause we finally dig up is: the build type was never set.

To attach compiler options beyond -O, we don't go touching anything global. The idea is still the one from part 02: options follow targets, declared by the target that uses them. The spelling CMake gives us is target_compile_options:

```cmake
target_compile_options(app PRIVATE -Wall -Wextra)
```

The -Wall and -Wextra in there are two sets of warning switches that get stricter as you stack them; with them on, the compiler checks one extra layer of trouble for us. The PRIVATE at the front we also saw in part 02's target_include_directories: it locks the options to app itself, and no other target gets to bask in them.

## The .o Handed to the Linker

Everything we met today is already gathered in mangle.o, except for one batch of addresses still sitting empty, waiting for the linker to fill them.

How it is laid out inside, what a relocation record looks like, and what .text and .data actually hold: bring objdump along, and we'll settle these questions one by one in part 04 (Object File Anatomy: What's Inside a .o). By the time we leave that piece, we will have seen every material that linking needs.
