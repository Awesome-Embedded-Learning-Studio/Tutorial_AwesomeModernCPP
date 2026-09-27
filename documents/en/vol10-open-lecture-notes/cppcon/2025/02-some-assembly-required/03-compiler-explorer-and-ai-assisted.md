---
chapter: 2
conference: cppcon
conference_year: 2025
cpp_standard:
- 17
- 20
description: 'CppCon 2025 talk notes — C++: Some Assembly Required by Matt Godbolt'
difficulty: intermediate
order: 3
platform: host
reading_time_minutes: 38
speaker: Matt Godbolt
tags:
- cpp-modern
- host
- intermediate
talk_title: 'C++: Some Assembly Required'
title: Deep Dive into Compiler Explorer and AI Assistance
video_bilibili: https://www.bilibili.com/video/BV1ptCCBKEwW?p=2
video_youtube: https://www.youtube.com/watch?v=zoYT7R94S3c
translation:
  source: documents/vol10-open-lecture-notes/cppcon/2025/02-some-assembly-required/03-compiler-explorer-and-ai-assisted.md
  source_hash: 9cc96901e133c073a1f098e54439c4fc9d6b9fa74d3a11add4511e894206c08b
  translated_at: '2026-09-26T15:33:34+00:00'
  engine: anthropic
  token_count: 7000
---
# Reading Assembly with Compiler Explorer: From "Gibberish" to "Getting It"

Many C++ developers have an instinctive aversion to reading assembly, feeling it is something only compiler-theory courses or low-level engineers ever need to touch. Yet when template error messages become unreadable, when performance tuning has nowhere to start, or when the `inline` keyword doesn't seem to do anything, learning to read assembly stops being optional and becomes a necessary skill. Among the many tools out there, Compiler Explorer<RefLink :id="1" preview="Matt Godbolt, Compiler Explorer, 2012–present" /> (usually just "godbolt") is one of the most practical ways in. This section introduces a method for reading assembly starting from zero, with the goal of taking you from "can't read a word of it" to "can start to see what's going on".

## Environment: Toolchain Setup

Before we start, here is the experiment environment used in this article, so you can reproduce it. Open `godbolt.org` in Chrome, pick GCC 16.1.1 as the compiler, leave optimization at the default `-O0` (for observing how code maps logically onto assembly), switch to `-O2` or `-O3` when you want to see what optimization does, and select C++20 as the language standard. Since godbolt uses a left/right split layout (C++ source on the left, assembly output on the right), a screen at 1920x1080 or higher is recommended, so the assembly pane doesn't get squeezed into something unreadable.

## The Core Idea: Mapping Between Source and Assembly

A common mistake when reading assembly is trying to go instruction by instruction from top to bottom, understanding every line the way you would read source code. In reality, the core purpose of looking at assembly is to build a correspondence—finding which machine instructions the compiler translated each line of C++ into. You don't need to understand what every single assembly instruction means; you only need to be able to locate where "those few lines of assembly that correspond to this line of C++" live.

Take a simple squaring function:

```cpp
int square(int x) {
    return x * x;
}
```

Drop this code into godbolt. If you are just starting to learn reading assembly, check Directives, Labels, and Comments in the Filter options—that way you get more complete information. Under `-O0` you will see output like this:

```asm
// GCC 16.1.1, -O0 -std=c++20 (AT&T syntax)
square(int):
        pushq   %rbp
        movq    %rsp, %rbp
        movl    %edi, -4(%rbp)
        movl    -4(%rbp), %eax
        imull   %eax, %eax
        popq    %rbp
        ret
```

At `-O0` the compiler's behavior is completely straightforward: first it stores the argument from `edi` (the first integer argument register on x86-64<RefLink :id="2" preview="System V ABI, AMD64 Architecture, §3.2.3" />) to the stack at `-4(%rbp)`, then reads it back from the stack, performs the multiplication, and finally leaves the result in `eax` (the return value register). The `pushq %rbp` / `movq %rsp, %rbp` pair is the function prologue and `popq %rbp` / `ret` is the epilogue—fixed patterns every function has, and once you recognize them you can skip past quickly. The only operations that really matter are the three lines in the middle: store the argument, load the argument, multiply.

Switch the optimization level to `-O2`, and GCC 16.1.1 generates `imull %edi, %edi; movl %edi, %eax; ret`—it multiplies `edi` by itself, moves the result into the return value register `eax`, done. Very tidy. It isn't strictly a single instruction (you need the `movl` to move the result from `edi` to `eax`), but the core arithmetic really is just one `imul`.

One reminder here: when reading assembly, always defer to the actual compiler output rather than inferring from memory. Different compiler versions and different optimization levels can produce significantly different output, and verifying by hand is the key step to avoid misjudgment.

## Hands-On: Analyzing a Real Function

Next, a slightly more involved example. The following function checks whether a `std::string_view` is a valid hexadecimal identifier: the identifier is exactly 16 characters long, and every character must be `0-9` or `A-F`:

```cpp
#include <string_view>

bool is_valid_hex_id(std::string_view sv) {
    if (sv.size() != 16)
        return false;
    for (char c : sv) {
        if (c >= '0' && c <= '9') continue;
        if (c >= 'A' && c <= 'F') continue;
        return false;
    }
    return true;
}
```

This implementation is obviously not optimal—you could improve it with `std::all_of`, a lookup table, or `find_first_not_of`. But it deliberately uses the most plain-spoken version, so we can watch how the compiler translates logic that contains branches and loops.

Put this code into godbolt. The `-O0` assembly is fairly long, so we won't list it all here. The key trick: hover the mouse over a line of C++ (say `if (sv.size() != 16)`), and the corresponding instructions on the right light up; hover over a line of assembly, and the corresponding C++ on the left lights up too. This hover highlighting is one of godbolt's most practical features—it directly solves the core problem of "finding the correspondence between C++ code and assembly instructions".

At `-O0`, the call to `sv.size()` gets expanded into a group of instructions (since `string_view`'s `size()` is inline—it is essentially just reading a member variable), which is then compared against 16, with a jump to the return-`false` spot if they differ. The two `if`s in the loop body work similarly: each conditional maps to a set of compare-and-jump instructions. `-O0` assembly is "faithful to the point of clumsiness": every C++ operation is translated literally—variables go to the stack when they should, and come back off the stack when they should.

## Switching to -O2 to Watch the Compiler Optimize

After switching the optimization level to `-O2`, the assembly shrinks markedly. The compiler has done work on several fronts: prologues and epilogues may be simplified, loops may be unrolled or otherwise optimized, branches may be rearranged. Concretely, for this example, the compiler inlines the `size()` call, compares the length directly, and treats the loop body in a way that looks nothing like `-O0`.

Try it yourself in godbolt, because outputs vary across compiler versions and optimization levels. One important principle when reading assembly: defer to the actual compiler output, don't assert conclusions you aren't sure of, and let the compiler's output speak for itself.

## Common Pitfalls and Things to Watch

While reading assembly, a few common issues deserve attention. First, godbolt by default filters out some assembly instructions through the Filter options; at the beginner stage, turn all filtering off and look at the full output, and only re-enable filters once you know which information counts as "noise". Second, you need some familiarity with the x86-64 calling convention<RefLink :id="2" preview="System V ABI, AMD64 Architecture, §3.2.3" />—at minimum, that integer arguments go in `rdi`, `rsi`, `rdx`, `rcx`, `r8`, and `r9` in that order, and the return value goes in `rax`. You don't need to memorize these deliberately; after reading enough assembly they stick on their own. Third, in simple functions you can mostly infer where parameters are, but once the function's logic is complicated and registers get reused repeatedly, guessing is off the table—you have to honestly trace the data flow.

Once you have the correspondence between C++ and assembly down, godbolt's hover highlighting lowers the learning barrier to nearly nothing. From there, try using the same method on harder scenarios—what template-instantiated code looks like, how aggressively a `constexpr` function gets optimized, how `std::string` differs across standard library implementations. These are the scenarios where reading assembly really pays off.

---

# What the Assembly Reveals About string_view

Faced with a wall of assembly output, many developers' instinct is to close the window. But once you understand what the compiler is "doing", assembly isn't that intimidating. This section walks through one very concrete scenario: what actually happens underneath when a `std::string_view` is passed to a function by value.

First, the experiment environment: GCC 16.1.1, running on x86-64 Linux, libstdc++ as the standard library, optimization level O1. Why not O0? Because O0's output is overly literal—write `int x = 0; return x;`, and the compiler really will first write 0 to memory, then read it back from memory into the return value register. That's friendly for debugging, but if the goal is to follow the logic of the code, O0's output is noise instead: a screen full of meaningless stack traffic—"can't see the forest for the trees" describes exactly this situation. O1 is much better: the redundancy is gone, but it hasn't reached O2's level of aggressive inlining and transformation, which makes it just right for reading at the learning stage.

Here's a simple piece of test code:

```cpp
#include <string_view>

bool check_length(std::string_view sv) {
    if (sv.size() == 16) {
        // do something more complicated
        return true;
    }
    return false;
}
```

The function itself is simple. Analyze the assembly produced by `g++ -O1 -S -o - test.cpp`. A common question: isn't `std::string_view` just "a read-only view of a string"? What's the difference from `const std::string&`? After looking at the assembly, this question becomes very concrete.

Under the hood, a `string_view` has only two members: a pointer (to the character data) and a `size_t` (the length)<RefLink :id="3" preview="cppreference, std::basic_string_view, C++17" />. It is essentially a struct with just two members. A common misconception is that when you pass a struct to a function, no matter how small, it gets put on the stack, or that the compiler implicitly converts it to pass-by-reference. Neither is true. The x86-64 System V ABI<RefLink :id="2" preview="System V ABI, AMD64 Architecture, §3.2.3" /> (the convention for C/C++ function calls on Linux) says that if a struct's total size fits in two registers, and every member is a "simple type" (pointer, integer, and so on), then it can be passed directly through registers—exactly like passing two ordinary variables.

Note that the member layout of `string_view` can differ across standard library implementations. GCC's libstdc++ puts the `size_t` first (`{size_t _M_len; const char* _M_str;}`), so on function entry **the length part is in `RDI` and the pointer part is in `RSI`**. That is the exact opposite of the "pointer first" intuition many documents give. Clang's libc++ uses `{const char* __data; size_t __size;}`—pointer first. The assembly output in this article is based on GCC/libstdc++; if you use Clang/libc++, the register assignment is reversed.

The corresponding assembly output (GCC 16.1.1, `-O1 -std=c++20`, with `.cfi_*` directives and irrelevant labels removed):

```asm
// GCC 16.1.1, -O1 -std=c++20
check_length(std::string_view):
    cmpq    $16, %rdi          ; compare size (in RDI) against 16
    sete    %al                ; AL=1 if equal, AL=0 otherwise
    ret
```

GCC already has this logic optimized very cleanly at O1: `cmpq $16, %rdi` compares the immediate 16 against the value in the `RDI` register. Since the first member of libstdc++'s `string_view` is `size_t _M_len` (placed in the first integer argument register `RDI` per the System V ABI), `RDI` holds exactly `sv.size()`. Next, `sete %al` is a clever instruction—if the previous comparison came out "equal", it sets `%al` to 1, otherwise to 0. That directly produces the `bool` return value (0 for `false`, 1 for `true`), with no branch jump at all.

It's worth noting that GCC chose the branchless `sete` approach rather than the more intuitive branch pattern of "compare → jump if unequal → set the return value separately in each arm". This shows that even at O1 (not an aggressive optimization level), the compiler prefers branch-eliminating strategies—a mispredicted branch usually costs far more than a few straight-line instructions.

Another detail worth attention: when analyzing a more complicated function, if you scroll down through the assembly, you may notice the highlight color suddenly disappears—the correspondence between source and assembly has broken. This is not a browser rendering problem; it's because the function calls STL helper functions internally (member functions of `string_view`, for example), and under O1 optimization the compiler has inlined them. Once inlined, that code no longer corresponds to any line of user-written source, so the highlight mapping breaks off.

This is a great learning point: inlining does not always require writing the `inline` keyword by hand. Already at O1 the compiler uses its own judgment to expand small functions (especially ones defined in STL headers) directly at the call site. After the expansion the assembly gets longer, but the function-call overhead is eliminated, and the compiler also gains more context for further optimization. From now on, when reading assembly, if you find the highlight correspondence suddenly cut off, your first reaction should be: inlining has most likely happened here.

To summarize this section's analysis: `string_view` is a two-member struct, passed through registers when passed by value (under GCC/libstdc++, `RDI` is the length and `RSI` is the pointer), the `size()` check corresponds to a single `cmp` instruction, and GCC returns the result branchlessly with `sete` at O1. The key is tying two things together—the "ABI convention" and the "standard library's member layout". Different STL implementations can assign registers completely differently, so always defer to the actual compiler output.

---

# Dissecting find_first_not_of in Compiler Explorer, One Optimization Level at a Time

Many C++ developers use `std::string::find_first_not_of` as a black box—pass the arguments, take the return value, never care what the compiler expanded it into. But after stepping the optimization level from O0 all the way to O3 on Compiler Explorer, you can see that the compiler's handling of this function differs noticeably at different optimization levels.

## Experiment Environment

The experiment uses Compiler Explorer (godbolt.org), GCC 16.1.1 as the compiler, x86-64 as the target architecture, libstdc++ as the standard library. The test code is simple: given a hexadecimal string, find the first position that does not belong to the character set "0123456789ABCDEF".

```cpp
#include <string>

int find_non_hex(const std::string& s) {
    // Find the first position that is not a hexadecimal character
    // If all characters are valid hexadecimal, returns std::string::npos
    return static_cast<int>(s.find_first_not_of("0123456789ABCDEF"));
}
```

The function looks unremarkable, but the compiler treats it very differently at different optimization levels.

## At O1: The Appearance of memchr Calls

Open the assembly view under O1, and the first phenomenon worth noting is that Compiler Explorer by default does not show the inline expansion of STL source, so everything inside the standard library renders in white (no source-highlight correspondence)—you see only bare assembly instructions.

More surprising is that calls to `memchr` show up in the middle of the assembly. The source clearly calls `find_first_not_of`—"find the first character not in the set"—so what does that have to do with `memchr` ("find the first occurrence of a specific byte")?

Think it through, and the logic is actually quite reasonable: to determine that a character is "not in" a set, the most direct approach is to call `memchr` once for each element of the set; if none of them finds it, then the character truly isn't in the set. The argument string "0123456789ABCDEF" happens to be exactly 16 characters, so the compiler's implementation becomes, for each candidate character, separately querying "is this character in the input string?".

## At O2: Looking for Loop Structure and Vectorization

After switching to O2, the amount of assembly code shrinks a bit, but the overall structure stays basically the same as O1. There is some bounds checking and preprocessing at the beginning, and the core logic still revolves around `memchr`.

When analyzing compiler output, an effective strategy is to locate the loop structure first. The concrete method is to look for the pattern of a label plus a backward jump—for example, after a `.L4:` label, the end of the loop body has a `jne .L4`, and that makes a complete loop. This method matters most when judging vectorization optimization (whether SIMD instructions are being used): watch how many bytes the pointer advances per iteration and how many elements get processed at once, and you can tell whether the compiler has transformed it into SIMD instructions.

But in this example's O2 output, there is no such loop structure. The compiler did not "use one loop to walk every character of the input string"; instead it calls `memchr` repeatedly. Intuitively, `find_first_not_of` should iterate over the input string and then check each character against the set; but the assembly presents exactly the opposite logic—for each character in the set, go search the input string. These two directions differ greatly in algorithmic complexity, but in this particular scenario (a set of only 16 elements), the compiler chose the latter.

## At O3: The Loop Vanishes, Fully Unrolled

After switching to O3, the loop structure disappears completely, replaced by massively duplicated `memchr` calls—sixteen nearly identical `memchr` call sequences laid out flat in the assembly.

The underlying logic is already clear once you combine the earlier analysis. For every character in the input string (the compiler already knows at this point that the string is 16 characters long, because of the length check up front), it separately queries: is this character in the "0" to "9" range? Is it in the "A" to "F" range? If all of these checks answer "not found", then this character definitely isn't in the valid hexadecimal character set—and it is the target position.

In other words, O3 fully unrolls the logic of "call memchr once for each of the 16 candidate characters". No loop overhead, no indirect jumps from function calls—just 16 copies of the `memchr` call lined up in a row.

## A Cognitive Bias Worth Noting

Before reading this assembly, many people would probably assume `find_first_not_of` is implemented like this: iterate over the input string, and for each character use some efficient method (a lookup table, say) to decide whether it is in the character set. That intuition may be right when "the set is large", but when the set is small, libstdc++'s implementation takes another road—it flips the problem around and searches the input for each character of the set.

This discovery illustrates an important fact: the standard library's actual implementation logic can be completely different from intuition, and the only way to verify it is to look at the assembly output directly.

To summarize `find_first_not_of`'s behavior across optimization levels: O1 produces the initial `memchr` calls, O2 keeps the same structure but trims the redundancy, O3 does a brute-force unroll. At every level the compiler performs whatever transformation it considers "most worth it"—it's just that its criteria for "worth it" don't necessarily match human intuition.

---

# Watching Clang's Different Loop-Handling Strategies on Compiler Explorer

Compiler optimization is often treated as a black box—turn on O2 or O3, the generated code is faster anyway, and exactly where it got faster is nobody's concern. But after comparing outputs across optimization levels and compiler versions on Compiler Explorer, you can find that the same loop code looks very different in assembly under different conditions.

## Test Environment

The experiment uses Compiler Explorer (godbolt.org), Clang as the compiler, target architecture set to x86-64, CPU model set to skylake (a typical modern desktop architecture). The test code is a plain loop that calls `memchr` internally to scan a 16-byte buffer segment by segment, returning an error immediately upon finding an invalid character. The logic itself is not complicated, but the compiler's handling of this code is worth a close look.

## Understanding Loop Unrolling Correctly

A common misconception: loop unrolling means blindly copying the loop body N times, the more unrolling the better, and that is exactly where O3's advantage over O2 lies. Reality is not that simple.

This loop has only 16 iterations, and the loop body contains a `memchr` call. If the compiler unrolled all 16, that would mean generating 16 consecutive blocks of `memchr` call plus conditional jump. Once all that code enters the instruction cache, the cache pressure may actually drag performance down. The compiler has to strike a balance between "unrolling reduces branch overhead" and "don't blow up the instruction cache", and that balance point is not easy to find.

## Comparing on Compiler Explorer

Paste the code onto Compiler Explorer and compile with Clang trunk (the latest development build) first, comparing O2 and O3 separately. One phenomenon worth noting: trunk Clang's behavior may not match expectations. Aggressive unrolling behavior previously observed on some pinned version may have become more "restrained" on trunk.

Doing experiments with trunk easily runs into irreproducibility problems, because a new commit can change the optimization strategy at any time. To reproduce experiment results, pin a specific version number—Clang 21, say—instead of using trunk.

## Analysis Results After Pinning the Version

Switch the compiler to Clang 21, keep the target architecture at skylake, turn on O2. This time the assembly output is well worth studying.

First, the `memchr` call disappears—not deleted, but inlined. The compiler embeds `memchr`'s core logic directly into the loop body, eliminating the function-call overhead (pushing, jumping, returning). Then you see some more involved instructions—not a simple `cmp` plus `je`, but AVX2-related vector compare instructions—the compiler recognized that this code is doing a byte scan and went straight to SIMD instructions to accelerate it, comparing multiple bytes at once.

This discovery shows that Clang has special built-in knowledge about standard library functions: it understands `memchr`'s semantics, does not treat it as an ordinary external function call, and can perform further transformations after inlining—including automatic vectorization.

## A Detail Still to Be Confirmed

In the assembly output, a strange immediate was noticed showing up in an offset calculation or a masking operation. Exactly where this number comes from still needs further confirmation—possibly some alignment-related mask, because when `memchr` starts at an unaligned address, it must first handle the unaligned head and then process the aligned body with vector instructions. Verifying how exactly that constant is computed would require checking against glibc's `memchr` implementation.

None of this affects this section's core conclusion: what Clang does to this code at O2 goes far beyond "unroll the loop a few times". It combines `memchr` inlining, vectorization, and possibly loop strength adjustment; the generated code already looks nothing like the original C++ code, but the semantics are equivalent.

## Caveats

When switching compiler versions, note that Compiler Explorer's UI sometimes has caching problems, and after a switch it may still be using the old version. After every switch, check the full compiler version string shown in the upper-left corner to confirm it really has changed. Also, specifying `-march=skylake` matters—without it, the default is `-march=x86-64`, the compiler won't use AVX2 instructions, the generated assembly will be far plainer, and none of the transformations above can be observed.

With this experiment, the compiler's process of optimizing loops is no longer a complete black box—at the very least, you can observe what decisions it is making. Next we move on to more complex cases.

---

---

# Reading Assembly with LLM Assistance in Compiler Explorer

The traditional way of reading assembly is usually counting instruction by instruction—tensing up at the sight of a loop, skipping any instruction you don't recognize. This "half-understanding" state is common among developers. Compiler Explorer recently added a feature: submit the assembly output to an LLM and let it assist with the explanation. This section covers what using this feature is like, and also discusses how to read assembly systematically without AI assistance.

## Experiment Environment

The experiment uses the Chrome browser to open Compiler Explorer (godbolt.org), GCC 16.1.1 as the compiler, optimization level -O2, language standard C++20. The assembly generated under different compilers and optimization levels differs a lot, so what you see may not match this article exactly, but the overall approach carries over.

## Starting from an Unfamiliar Instruction

While analyzing some bit-manipulation-related code, an uncommon instruction appeared in the compiler output. Hovering the mouse over it, Compiler Explorer's tooltip was extremely vague—it only said this "looks very much like a bitmask", with no explanation at all of what it is actually doing.

Compiler Explorer's hover hints are very useful for common instructions (`mov`, `add`, `cmp`, and the like)—click through and you can see the corresponding source line. But for the instruction encountered this time, the hint was almost empty, or just a very generic description, of no help for understanding the actual logic.

Faced with this, you can try repeatedly adjusting the compiler's optimization level—switch from -O0 to -O1 and then to -O2, and watch whether this instruction turns into an easier-to-understand form at a different optimization level. In this case, at -O0 it turned into a pile of wordier but more straightforward instruction sequences, and at -O2 it folded back into that single incomprehensible instruction. This provides an important clue: this instruction is very likely the compiler, at a higher optimization level, "compressing" some stretch of logic into a single bit-manipulation instruction the processor natively supports.

## How to Read Assembly Without AI Help

Without AI assistance, you can build an overall picture of the assembly output through the following steps.

First, turn off the display items that clutter your view. Compiler Explorer shows a lot by default—instruction addresses, opcode byte representations, source line annotations, and so on. These are useful when debugging, but if the goal is "understand what this code is doing", they just make the screen messy. In the settings, disable "Show instruction addresses" and "Show machine code", keeping only the instruction mnemonics and the source-line highlight correspondence.

Then, count loops. This is the fastest way to build assembly intuition. See a `jmp` jumping backward, and you know there's a loop here; see a `call`, and you mark a call to an external function; see a `ret`, and you know this is the end of a function. This way, even without recognizing every single instruction, you can make a rough judgment about the code's structure: any unexpected loops? Any calls to functions you don't know? Roughly how big is the function's stack frame?

Back to that incomprehensible instruction. One effective strategy is switching to another compiler—say from GCC to Clang 18, keeping the same source and optimization level. In Clang's generated assembly, the same logic may use a different instruction sequence; it's still not readable at a glance, but at least each instruction's hover hint may be more detailed. When you're stuck on some instruction, comparing across compilers often opens things up—different compilers have different "translation styles" for the same C++ code; when compiler A's instruction is opaque, compiler B may express the same logic more plainly.

## Confirming What the BT Instruction Means

Back in GCC's output, hovering over that instruction again, the tooltip showed this is the `BT` instruction—short for "Bit Test"—whose role is to select a bit in a bit string and test it.

With that explanation understood, the logic of the whole stretch of assembly clicked. The C++ source really does contain a bit test like `(1ULL << n) & mask`, and at -O2 the compiler mapped it directly onto x86's `BT` instruction instead of literally doing a shift and then an AND. This is a classic compiler optimization: recognize the bit-operation pattern in the source, then replace it with a natively supported processor instruction—reducing the instruction count and speeding up execution at the same time.

This illustrates an important lesson: reading assembly does not require recognizing every single instruction. Just grab the few key ones, figure out which operation in the source each corresponds to, and the remaining filler instructions (stack frame setup and teardown, argument passing, for instance) need only a glance.

## Compiler Explorer's LLM Explanation Feature

Compiler Explorer recently added an option in the UI that submits the source and the corresponding assembly output together to an LLM and has it explain "what is happening here".

The LLM's way of explaining is not per-instruction translation—doing that would be no different in essence from reading it yourself. It does something more valuable: splits the assembly into several logical blocks, then describes each block's function. For example, it might point out "this is the initialization before the loop", "this is a loop body that checks one bit per iteration", "this is where the result gets collected". Exactly this kind of high-level summarization is what human assembly reading tends to miss—developers sink into per-instruction details and forget to step back and look at the overall structure.

## Caveats on Using the LLM Feature

Although the LLM-assisted explanation experience is good, a few key points deserve special attention.

First, this feature is currently in beta. The speaker also said explicitly that if it proves too costly or misleading, it may be taken offline. So don't over-rely on it—treat it as an auxiliary tool.

Second, the LLM's explanations are not necessarily correct. After testing with assembly containing SIMD instructions (`xmm`-register-related instructions), the LLM was found to make clear errors in its explanation of some instructions—describing floating-point operation instructions as integer operations. Without verifying on your own, you might accept an incorrect explanation. Treat the LLM's explanation as a "lead" rather than an "answer": it provides a rough direction, but the final right-or-wrong still needs human confirmation.

Third, for scenarios involving sensitive code, do not use this feature. The source and the assembly get sent to an external service.

## A Recommended Assembly-Reading Workflow

Putting all of this experience together, the recommended assembly-reading workflow is: first do a quick pass yourself—count loops, find `call`s, look at function boundaries, and build an overall impression; when you hit an unfamiliar instruction, hover for the hint first, and compare across compilers; if it still doesn't click, then consider using the LLM-assisted explanation—but always cross-verify its conclusions.

Reading assembly doesn't require memorizing an instruction manual, nor understanding the meaning of every byte. The key is building a kind of "pattern recognition" ability—see a certain pattern and know roughly what it is doing. Compiler Explorer's tools (source-highlight correspondence, instruction hover hints, LLM explanation) all help you build that intuition faster.

---

# When AI Points You Down a "Clever" Path

Compiler Explorer's Claude Explain feature can directly explain tricks in the assembly—for example, "the compiler used a clever bit operation here, packing character validity into a 64-bit value and then consulting bits via shifts". That level of explanation is genuinely helpful. But confident delivery and correctness are two different things, as we will discuss in detail shortly.

First, let's look at the bit-operation trick itself. The principle is no mystery—similar techniques can be found in the source of many string-parsing libraries. Below is a hand-written simplified version you can use to verify your understanding.

## How the Bit-Lookup-Table Trick Works

The core idea: to determine whether an ASCII character belongs to some valid character set (say "digits 0-9"), the most intuitive way to write it is `if (c >= '0' && c <= '9')`. But sometimes the compiler will not generate two comparisons plus an AND; instead it uses a 64-bit lookup table, representing each ASCII character's "validity" with one bit, and then queries it through a shift.

```cpp
// bit_lookup_demo.cpp
#include <cstdint>
#include <cstdio>

// Build the lookup table by hand: only the bits for '0'-'9' are set
// '0' has ASCII value 48, and '9' is 57
// so we fill bits 48 through 57 with 1 and leave the rest 0
constexpr uint64_t make_digit_table() {
    uint64_t table = 0;
    for (int i = '0'; i <= '9'; ++i) {
        table |= (uint64_t{1} << i);
    }
    return table;
}

constexpr uint64_t kDigitTable = make_digit_table();

// Test whether a character is a digit: use the character value as the shift amount and check whether the bit is 1
bool is_digit_bitlookup(char c) {
    // Note that c is a char, which may be signed — convert to unsigned first
    unsigned char uc = static_cast<unsigned char>(c);
    // A shift amount >= 64 is undefined behavior (C++ standard [expr.shift])
    // x86 hardware masks the shift count to 6 bits, so uc=112('p') actually shifts by 48
    // landing exactly on bit 48 ('0') — a false positive: 'p'~'y' misjudged as digits
    if (uc >= 64) return false;
    return (kDigitTable >> uc) & 1;
}

// The traditional way to write it, for comparison
bool is_digit_naive(char c) {
    return c >= '0' && c <= '9';
}

int main() {
    // Test all printable ASCII characters
    bool all_match = true;
    for (int i = 32; i < 127; ++i) {
        char c = static_cast<char>(i);
        if (is_digit_bitlookup(c) != is_digit_naive(c)) {
            printf("Mismatch at '%c' (ASCII %d): bitlookup=%d, naive=%d\n",
                   c, i, is_digit_bitlookup(c), is_digit_naive(c));
            all_match = false;
        }
    }
    if (all_match) {
        printf("All printable ASCII chars match!\n");
    }

    // Test a few edge cases as well
    printf("'5' is digit: %d\n", is_digit_bitlookup('5'));
    printf("'a' is digit: %d\n", is_digit_bitlookup('a'));
    printf("NUL is digit: %d\n", is_digit_bitlookup('\0'));
    return 0;
}
```

Compiled and run, the output matches expectations exactly: every printable character gets the same verdict as the naive version. This conclusion has one precondition: the original version, for `uc >= 64`, relied on x86 hardware's masking of the shift amount (truncating the shift amount to `shift & 63`), which is undefined behavior under the C++ standard—in fact 'p' through 'y' (ASCII 112-121) would be misjudged as digits because the shift amount wraps around into bits 48-57. Adding the `uc >= 64` range guard fixed the problem. The advantage of this trick is that it turns a "range check" into "one shift plus one AND", which on some architectures eases branch-prediction pressure. And the trick extends—if you want to check "letters plus digits", you only need to set a few more bits in the table; one 64-bit integer covers ASCII 0-63, and two cover up to 127.

One caution: if you shift directly with `char c`, negative ASCII values (values in certain extended character sets, for instance) cause problems, because the behavior of signed right shift is implementation-defined. Always convert to `unsigned char` first—a point the C++ Core Guidelines also make. Likewise, a shift amount exceeding the width (`uc >= 64`) is undefined behavior; don't rely on x86's masking behavior.

## Environment Notes

The experiment environment is Arch Linux WSL LTS (WSL2), compiler GCC 16.1.1, compile command:

```bash
g++ -std=c++20 -O2 -Wall -Wextra bit_lookup_demo.cpp -o bit_lookup_demo && ./bit_lookup_demo
```

`-O2` is used to observe whether the compiler optimizes the hand-written bit lookup any further. Interested readers can add `-S -o -` to see the assembly output, then use Compiler Explorer's Claude Explain feature to analyze it.

## Never Trust AI Explanations Blindly

That was the part about understanding the bit-operation trick; next comes the cautionary side of AI assistance.

The speaker shared a navigation incident he personally experienced: in the neighborhood where he has lived for 15 years—six or seven of which he also spent delivering newspapers door to door—the main road was blocked by a truck that had dropped its cargo, so he decided to detour to the next village, turn left, and loop back around. The core lesson of this story is clear: **your domain knowledge of the problem may be more reliable than the "optimal solution" any intelligent system gives you—provided you actually have that domain knowledge.**

Mapped onto programming, AI tools—code completion, assembly explanation, or direct code generation—really are getting more powerful, and Claude Explain being able to parse the bit-packing trick is itself proof. But if you don't understand what that bit operation is doing yourself, you cannot judge whether the AI is right. If it confidently claims "this is doing a CRC check" and the developer takes it as truth, things go off track.

In a real case, a developer asked an AI to explain the implementation details of a `std::variant`, and the AI spoke with an air of authority—"this uses small-object optimization, embedding the discriminator into the alignment padding"—which sounded very reasonable, but a later line-by-line check against the source showed it had the offsets completely wrong: the discriminator was not where it said at all. Writing code directly from that explanation would most likely have introduced bugs.

So the conclusion is: AI is a great learning companion, especially when the developer already has some foundation and can ask good questions. Claude Explain can help quickly build an intuitive understanding of a stretch of assembly, but you still need to verify it yourself. Never treat the AI as an authority—it may sound far more confident than most people, but confidence is not correctness.

Back to the bit-lookup-table example: if the AI tells you "the compiler generated a bit lookup table here to do character validation", you can now at least write one yourself to check whether that claim is plausible, instead of just nodding along. That ability to "verify it yourself" is what really matters.

---

# From Navigation Incidents to Toolchain Traps: Don't Trust a Technical Solution Blindly

The speaker shared a memorable satellite-navigation incident: he followed the navigation down a "private road", and his car got hopelessly stuck in a bridleway for four or five hours, during which dog walkers passing by even comforted him with "don't worry, delivery trucks get stuck here all the time". Having finally gotten out, he went to OpenStreetMap afterwards and fixed the place, marking it "not passable, blocked at the far end".

This story has a strong resemblance to C++ developers' everyday experience. When configuring a CMake cross-compilation toolchain, many developers have been through something similar: some tutorial online (playing the role of the "satellite navigation") swears that all you need is to set `CMAKE_SYSTEM_NAME` to Linux and then specify `CMAKE_C_COMPILER`. Every step seems to make sense, and the path seems clear, but the compiled binary simply won't run on the target board—because what got linked was the host's glibc rather than the sysroot that ships with the cross-compilation toolchain. Checking each step over and over, everything feels "fine"—just like that car stuck in the bridleway: the road looks passable, but the far end is sealed.

The cause is usually that the tutorial's author used a very particular toolchain layout, and the unstated preconditions account for half of it. This is exactly the same as the navigation not telling you "this road can be entered, but the exit is sealed".

So it is important to build a habit: when a technical solution looks perfect and every step "checks out", stop first and ask yourself—**does this solution have unstated preconditions?** The compiler not reporting errors doesn't mean it did what you thought, just as the navigation not complaining doesn't mean that road actually goes through. After falling into the pit, you should also go improve the documentation or file an issue on the open-source project, so the next person doesn't get stuck there again.

---

# The Broader Meaning of "Assembly" in C++

The navigation stories above discussed the risk of blindly trusting technical solutions; now we return to the speaker's main line of thought. The "assembly" discussed here is not assembly in the assembly-language sense, but the broader concept of "a set of cooperating parts fitted together".

The speaker posed an interactive question: in the world of C++, what fits the description of "a set of cooperating parts"? He mentioned a few directions himself—programs, production builds, and assembly itself. Then he said the key sentence: **"When I think of those cooperating components, I think of all the libraries we use, and how they get cobbled together, or how they fit together perfectly on their own."**

This view deserves deep thought. Many developers understand "assembly" in C++ as the compile-and-link pipeline: `.cpp` compiles into `.o`, and the linker stitches a pile of `.o` into an executable. That understanding isn't wrong, exactly, but it only sees the bottom-most layer. Looking at a C++ project from a higher vantage point, what is really "cooperating"? The libraries.

Take a typical modern C++ project: fmt for formatted output, nlohmann/json for parsing configuration, spdlog for logging, plus a third-party linear algebra library. Each of these is itself "a set of cooperating parts"—inside fmt, the formatting core, type parsing, error handling, and other components cooperate; inside spdlog, sinks, formatters, and the logger hierarchy cooperate. And then they have to cooperate with each other: spdlog can use fmt underneath for formatting, while business code calls spdlog and nlohmann/json at the same time.

The way these components get fitted together is the real "assembly" of C++. And this "fitting" process is far more fragile than you would imagine.

A real example: when upgrading fmt from v9 to v10, compilation simply failed. The business code itself had no problem, but a certain version of spdlog at the time depended internally on an implementation detail of fmt v9. Look at spdlog on its own, and it "cooperates" with itself just fine; look at fmt v10 on its own, and there's nothing wrong with it either. But fit them together—the assembly failed. This is cut from the same cloth as the navigation story: every segment of road is passable on its own, but drive the combined route and you're stuck.

From this it is clear that the speaker's lifting of the "assembly" concept up a level from the underlying compile-and-link is well founded. As C++ programmers, the "assembly" problems we face daily happen more between library and library, module and module. Which libraries you choose, whether their versions are compatible, whether their ABIs agree, whether their build systems can live in peace—these are the real "assembly" challenges.

Modularity in C++ is not just "how to write headers and source files", but even more "how to reliably fit a pile of independently developed libraries together and make them cooperate properly". The latter is the truly hard part.

Thinking further along this direction: what components does the C++ ecosystem actually offer to fit together? The STL is the most basic, but what beyond it? Where does Boost sit? What is the Beman project that has been showing up frequently on the mailing lists lately doing? And the package-management tools in daily use—vcpkg, conan—what problems are they actually solving? Many developers assume these are all "advanced topics" with little bearing on writing a small project; in reality, the moment you use even one third-party library, you are already facing toolchain dependency-management problems.

The content that follows will set assembly aside for now and step up a level from the "component assembly" angle, to see what ready-made components exist in the C++ ecosystem, where they come from, how to choose among them, and how to manage them. The next article starts from the origins of the STL.

<ReferenceCard title="References">
  <ReferenceItem
    :id="1"
    author="Matt Godbolt"
    title="Compiler Explorer"
    publisher="godbolt.org"
    :year="2012"
    chapter="interactive compiler output explorer"
    url="https://godbolt.org/"
  />
  <ReferenceItem
    :id="2"
    author="AMD / System V"
    title="System V Application Binary Interface, AMD64 Architecture"
    publisher="x86-64 psABI"
    :year="2018"
    chapter="calling convention: RDI, RSI, RDX, RCX, R8, R9 for integer args"
    url="https://gitlab.com/x86-psABIs/x86-64-ABI"
  />
  <ReferenceItem
    :id="3"
    author="cppreference.com"
    title="std::basic_string_view"
    publisher="cppreference.com"
    :year="2024"
    chapter="C++17; stores a pointer and a size; passed by value through registers"
    url="https://en.cppreference.com/w/cpp/string/basic_string_view"
  />
</ReferenceCard>

---

## Further Reading

- Compiler Explorer is the best window onto compiler behavior; for a systematic tour of what each GCC/Clang option does, see [Guide to Common Compiler Options](../../../../vol7-engineering/02-compiler-options.md).
- To see how auto-vectorization enables AVX/AVX2 in CE, see [SIMD and vectorization](../../../../vol6-performance/ch04-tuning-by-bottleneck/04-05-simd.md).
