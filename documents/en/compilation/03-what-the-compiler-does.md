---
chapter: 13
difficulty: intermediate
order: 3
platform: host
reading_time_minutes: 20
tags:
- cpp-modern
- host
- intermediate
- 工具链
- 模板
title: "What the Compiler Does: From C++ to Assembly"
description: "Read the symbol table with nm and c++filt to see how name mangling encodes namespaces, overloads, and parameter types into symbol strings; then see why a template produces one copy of code per use, where the -O0 and -O2 assemblies differ, and finally how the assembler as turns a .s into a .o, recording the addresses it cannot fill in as relocations."
cpp_standard: [11, 14, 17, 20]
translation:
  source: documents/compilation/03-what-the-compiler-does.md
  source_hash: c6ea0bb10e093aea9bb047ec8193f2eb31d519bfd5031b31d8de06a6ab904ec5
  translated_at: '2026-10-13T04:03:01+00:00'
  engine: anthropic
  token_count: 4420
---

# What the Compiler Does: From C++ to Assembly

At the end of the previous piece we left hello.i sitting on the table: forty-four thousand nine hundred and seventy-one lines of plain text, with not a single `#` directive left in them. Today it's the compiler's turn to take over. In many people's mental picture, the compiler's job is error-checking: your syntax is wrong, it shouts; your types don't line up, it shouts again; then it translates our code into assembly, and the work is done. Error-checking and translating really are its main business. In the volume's opening piece we also left a line behind: every layer inside the compiler, from parsing through optimization to instruction selection, could fill a whole piece on its own. Today we won't burrow into the algorithms inside those layers; what we'll look at is the other end: your source goes in — where along the way did it lay hands on the assembly that comes out? It laid hands on three things, and we'll take them one at a time. Function names: it re-encoded them. The code you write from a template: it turned into one copy per type. And if you switch on optimization, it will happily swap out even the shape of your loops. That `_ZSt4cout` string from the volume's opening piece was the handiwork of the first of these; we agreed back then that the formal unpacking would wait for today.

The usual environment note: my experiments all ran in WSL2, with a toolchain of GCC 16.2.1 paired with binutils 2.47, and every piece of output in this piece came from a real run. If your versions differ, details like symbols and instructions may drift a little, but the mechanisms themselves haven't changed.

## Our Test Subject

What about hello.i? Its shift is over the moment the baton changes hands: of its forty-some-thousand lines the overwhelming majority are headers, and digging around in there for the symbol-naming patterns we're after today is far too much effort. The six-line hello.cpp can't carry the role either — it has too few players on stage. I've prepared a `mangle.cpp` that stocks every player we'll be looking at today: free functions, a namespace, a const member function, and a function template.

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

Only use_all and bigger come with function bodies; int_max, scale, and get are declarations only. Leaving out the implementations is deliberate: today we compile only as far as .o and never link, so missing implementations cost us nothing.

## Name Mangling: The Linker Only Knows Strings

The first rewrite lands on the names of functions. C++ lets us overload functions, lets us put functions inside namespaces, and lets our member functions share a name with ordinary functions. But the linker can't tell any of that apart: when it resolves symbols, all it holds in its hands are strings of characters. How does util::scale stay distinguishable from somebody else's scale? The compiler's move is to encode the namespace, the class name, and the parameter types all into the function's symbol name, and this move is called name mangling.

Let's compile mangle.cpp into a .o and ask nm to pull out its symbol table. nm is one of the binutils tools, and its job is reading the symbols inside object files. We'll use grep to pick out just two kinds of symbols: T-marked ones are defined in this very file, and U-marked ones reference somewhere else:

```text
$ g++ -c mangle.cpp && nm mangle.o | grep -E " (T|U) "
                 U _Z7int_maxii
0000000000000000 T _Z7use_allv
                 U _ZN4util5scaleEii
                 U _ZNK4util7Tracker3getEv
                 U __stack_chk_fail
```

The long strings of digits on the left are addresses; the U-marked lines don't even have an address, because their implementations aren't in this file at all. The right side is what the linker will have to recognize later. Scan it once from top to bottom and you'll notice that not a single name you know is left: perfectly respectable int_max has become `_Z7int_maxii`. Don't panic — let's decode them one at a time.

We'll warm up on the shortest one, `_Z7use_allv`; the reading scheme takes just three rules:

- The leading `_Z` is the fixed prefix on every mangled symbol.
- The 7 that follows means the name takes up 7 characters, and the use_all after it is that name.
- Once the name is read off, the remaining v is the parameter list; v corresponds to void — this function of ours takes no parameters.

So the whole string reads use_all(). Decoding is nothing we need to do by hand: the dedicated tool for the job is c++filt, which already made an appearance in the volume's opening piece. Feed it the four `_Z`-prefixed symbols one at a time, and what it hands back is:

```text
int_max(int, int)
use_all()
util::scale(int, int)
util::Tracker::get() const
```

For the remaining three symbols, the only structurally new markers are N, E, and K; let's pick up the type-code letters along the way too: i corresponds to int, d corresponds to double — same scheme as v. You can already read `_Z7int_maxii`: 7 characters of int_max, with the ii after it being two int parameters. Next, `_ZN4util5scaleEii`: read the N and E inside it as a pair of brackets dedicated to nested names — 4util is 4 characters of util, 5scale is 5 characters of scale, and together they read util::scale, with the ii at the end still being two ints. `_ZNK4util7Tracker3getEv` adds one more, K: it records const. Here it's tagged onto a member function, but const elsewhere in types uses the same marker, so the decoded form is util::Tracker::get() const. This whole encoding scheme comes from the Itanium C++ ABI, a specification for C++ binary interfaces, and GCC on Linux is one of its users. Whether other compilers and other platforms share the same encoding is a question we'll leave for the piece on ABIs; recognizing and decoding these strings is all we need today.

Having read this far, the overload story falls into place. Suppose we add another overload of int_max that takes two doubles: the parameter types are encoded into the symbol too, so the int version's symbol is `_Z7int_maxii`, and the double version becomes `_Z7int_maxdd`. What the linker receives is two entirely unrelated symbols — that is how overloads survive. Think about it: without this layer of encoding, both overloads would have to be named int_max, and what would the linker have to tell them apart by?

As for `__stack_chk_fail` on that list — none of us ever wrote it, yet there it is. It's a product of the stack protector: GCC in our setup turns on stack protection by default, adding a check to each function's stack frame, and when the check finds trouble it calls for this. It wears an underscore on each end and carries no _Z prefix, because it is a C interface provided by the runtime library to begin with, and C doesn't go through C++ mangling.

## extern "C": Turning Mangling Off

Some situations call for turning mangling off instead, and the most common one is calling a C library. The C compiler on our Linux toolchain doesn't mangle names — the exported symbol is simply the function's own name. If our C++ code wants to use somebody else's library, our symbols have to grow into the shapes they recognize; otherwise the names won't line up, and at link time neither side can find the other. There's a very direct way to do this: hang `extern "C"` in front of the declaration.

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

Still the same int_max; the only change is `extern "C"` in front of the declaration, and the symbol comes out clean — nothing left but the bare name int_max. Put the two outputs side by side: with mangling on, its symbol is `_Z7int_maxii`; with mangling off, all that remains is the bare name int_max. This mechanism's formal name is language linkage — look up linkage in the standard, and that is what you'll find.

How do C headers make use of it? In the previous piece, when we printed the predefined macros with -dM, we had already met the `__cplusplus` macro: the C++ compiler defines it, so the C side never sees it. So a certain pairing shows up all over C library headers: use `#ifdef __cplusplus` to ask whether the visitor is C++, and if it is C++, wrap a whole batch of declarations in an `extern "C"` block. When the C++ side includes the header, the declarations pick up C linkage; when the C side includes it, that stretch in the middle might as well not exist. From now on, when you open a C library header and run into these lines again, you'll know what they're up to.

The price of turning mangling off is that overloads can no longer survive. Think it through: if both int_max overloads hung `extern "C"` on themselves, both symbols would become the bare name int_max — one name carrying two different functions. So a set of overloads sharing one name cannot all be handed over to C linkage; try it for real and GCC will stop you on the spot, and the error it reports is conflicting declaration. When I wrote this section I ran it once more: what popped out was one error plus a note pointing back at the earlier declaration.

## Templates: Only the Types You Use Get Code

The second rewrite is the template's turn. In mangle.cpp we called bigger only twice: once with integers, which settled into bigger<int>, and once with decimals, which settled into bigger<double>. So what does bigger look like in the assembly? One copy of code that runs for every type, or one copy per type? Let's look directly. Inline takes one sentence to explain: no more function calls — the function body gets laid down right at the call site. bigger is so small that once optimization comes on, it's the likely candidate for inlining, and a standalone function body may no longer be there. So I added -fno-inline to the command, switching off ordinary inlining (functions marked with the always_inline attribute being the exception), so that we can count the code bigger produced:

```text
$ g++ -S -fno-inline mangle.cpp -o mangle_noInline.s && grep "^_Z6bigger" mangle_noInline.s
_Z6biggerIiET_S0_S0_:
_Z6biggerIdET_S0_S0_:
```

Hand the two labels grep caught over to c++filt, and what comes back is:

```text
int bigger<int>(int, int)
double bigger<double>(double, double)
```

There's the answer, laid out in the open: the assembly holds two copies of bigger, matching our two calls exactly. We call each concrete piece of code generated from a template an instance, and the generating process instantiation. A template on its own never turns into any code; the compiler works on demand: you use a type combination, and it generates a copy for us. Nobody ever used bigger<char>, and nowhere in the whole assembly will you find a trace of bigger<char>. There's one more thing we can read out of the symbols: the template arguments get encoded in too — the Ii in `_Z6biggerIiET_S0_S0_` corresponds to <int>. The `T_` and `S0_` after it are shorthands for types; no need to learn to write them — let the tool do the decoding.

This answers half of an old question: why do template definitions usually live in headers? For the compiler to generate the code of bigger<int>, it has to see bigger's complete function body in this translation unit; with a single declaration in hand, it can't produce anything. So a template definition has to travel with the header — whoever uses it must have the function body in their translation unit. And that raises the next question right along with it: if ten translation units all use bigger<int>, won't ten identical copies of code appear? How do they avoid fighting at link time? That is the business the piece on templates and ODR will come and sort out; let's deposit the question there. ODR's full name is the One Definition Rule — the one-definition rule in plain words; we'll unpack it properly in that piece.

## -O0 versus -O2: Two Assemblies from One Source

In the third rewrite, the compiler's freedom grows considerably. Let's take the most ordinary summing function as our specimen:

```cpp
// opt_demo.cpp
int sum_to(int n) {
    int s = 0;
    for (int i = 1; i <= n; ++i) s += i;
    return s;
}
```

Not one experiment today wrote a single -O, so what we've been using is the default tier, -O0. A few excerpted lines of its assembly:

```asm
	movl	%edi, -20(%rbp)
	movl	$0, -8(%rbp)
	movl	$1, -4(%rbp)
	movl	-4(%rbp), %eax
	addl	%eax, -8(%rbp)
	addl	$1, -4(%rbp)
```

Let's go through these lines one at a time. %edi is the register the parameter n arrives in, and the first instruction stores it into the stack slot -20(%rbp). Then come the 0 for s and the 1 for i, landing in -8(%rbp) and -4(%rbp) respectively. The three instructions of the loop body follow the same pattern: we move i from the stack into %eax, add %eax into s on the stack, and add one to i, still on the stack. Notice: every variable in the source has its own spot on the stack, and every step dutifully fetches, computes, and puts back — not one superfluous move, and not one time-saving move either.

Switch the tier to -O2, and the excerpt takes on a whole different shape:

```asm
	leal	1(%rdi), %ecx
	movl	$1, %eax
	movl	$2, %eax
	movl	$1, %edx
	leal	1(%rdx,%rax,2), %edx
```

The stack is gone — these lines never so much as touch %rbp. The computation has all moved into registers, and the constants 1 and 2 are encoded straight into the instructions. The loop itself got rewritten too: GCC partially unrolled it, so one round does different work than before. But there's one direction you'd probably guess wrong: the instruction count in the file did not go down. For this sum_to of ours, the -O0 build produced a function body of 15 instructions, while the -O2 build produced 23. We counted by mnemonic: objdump's disassembly prints 24 address lines, one of which is the continuation bytes of a 10-byte-aligned instruction and doesn't qualify as an independent instruction, so counting rows mechanically would overcount by one. What optimization saves is the instructions actually executed when the program runs: each round computes several numbers at once, and the loading and unloading on the stack is done away with entirely, so the number of instructions actually executed drops by a wide margin. Yet the source's semantics haven't budged an inch: feed in sum_to(100), and 5050 still comes back — your program can observe no difference whatsoever.

These two aren't the only tiers on offer: GCC also has -O1, -O3, and -Os. -O3 rewrites more aggressively than -O2, and -Os puts binary size at the very front of its priorities. Which tier you pick changes how hard the compiler is willing to swing. One more thing to make clear: all the rewriting we just looked at happens inside the compiler, and what the linker receives is already the finished, rewritten product. LTO (link-time optimization) moves the boundary of optimization toward the link step — that is the business of the piece on LTO and the linker.

## The Assembler as: Translating Line by Line, Deciding Nothing

At the assembly leg of the journey, the one taking over is as, whom we already met in the volume's opening piece. Its line in the g++ -v output looks like this:

```text
 as -v --64 -o xxx.o xxx.s
```

The job it receives is a simple one: translate the instructions in the .s into machine-code bytes one for one, then package the result into a .o in ELF format. Labels become addresses, the symbols get entered into the symbol table, and the translation is done. Don't expect it to understand C++ or to optimize; every decision worth making, the compiler finished making when it wrote the .s, and what's left for as is purely mechanical cross-referencing.

There is one class of things, though, that the compiler and as together cannot pin down, and it deserves a look of its own. Cast your mind back to that batch of U symbols in mangle.o: int_max's implementation isn't in this file, so what target address should the call instruction that invokes it be filled with? Nobody knows at compile time, and it's no different once the .s reaches as's hands. What as does is leave the address in the instruction empty and append a relocation record, whose meaning is: the address here isn't settled yet — we wait for the linker to come fill it. So the .o in our hands carries more than code and symbols: it also carries a batch of pending addresses.

## The CMake View: Who Owns the Optimization Level

In day-to-day builds you probably don't write -O yourself; that matter belongs to CMake's CMAKE_BUILD_TYPE. Let's list the correspondence for the GNU compilers: Debug gives you -g, and not a shred of optimization. Release switches to -O3, and brings -DNDEBUG along with it, so asserts and their kin get shut off. RelWithDebInfo gives -O2 plus -g — optimization and debug info both present in full — and it's the one we reach for most in everyday releases. But it carries -DNDEBUG all the same, so asserts are off in this tier too. MinSizeRel uses -Os plus -DNDEBUG.

If you never set CMAKE_BUILD_TYPE at all, its value is empty, CMake won't add a single -O for you, and the compiler falls back to the default -O0. This trap is an easy one to fall into. The program builds, runs inexplicably slowly, we chase it all the way down, and the cause we finally dig up is: the build type was never set in the first place.

To attach compiler options beyond -O, don't touch anything global; the idea is the one from the previous piece: options travel with targets, declared by whichever target uses them. CMake's spelling for this is target_compile_options:

```cmake
target_compile_options(app PRIVATE -Wall -Wextra)
```

The -Wall and -Wextra in that block are two groups of warning switches that tighten the checks the more of them you turn on; with them attached, the compiler can dig one layer deeper for problems on our behalf. The PRIVATE at the front we also saw in the previous piece's target_include_directories: it locks the options onto app itself, and no other target gets to benefit from them.

## The .o Handed to the Linker

Everything we've met today has piled up inside mangle.o — only one batch of addresses is still sitting empty, waiting for the linker to fill.

How its interior is actually laid out, what a relocation record looks like, what .text and .data hold — we'll bring objdump along in the next piece and settle these questions one by one. By then, we'll have met every material the link step needs.
