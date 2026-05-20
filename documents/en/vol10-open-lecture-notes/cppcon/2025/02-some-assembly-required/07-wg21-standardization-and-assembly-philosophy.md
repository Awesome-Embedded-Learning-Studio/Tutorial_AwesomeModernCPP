---
title: WG21 Standardization and x86/RISC-V Assembly Philosophy
description: 'CppCon 2025 Talk Notes — C++: Some Assembly Required by Matt Godbolt'
conference: cppcon
conference_year: 2025
talk_title: 'C++: Some Assembly Required'
speaker: Matt Godbolt
video_bilibili: https://www.bilibili.com/video/BV1ptCCBKEwW?p=2
video_youtube: https://www.youtube.com/watch?v=zoYT7R94S3c
tags:
- cpp-modern
- host
- intermediate
difficulty: intermediate
platform: host
cpp_standard:
- 17
- 20
chapter: 2
order: 7
translation:
  source: documents/vol10-open-lecture-notes/cppcon/2025/02-some-assembly-required/07-wg21-standardization-and-assembly-philosophy.md
  source_hash: 20df7cbb4489115aa2f24094cb418ad71cf07458503e296ca30905ce3e831a90
  translated_at: '2026-05-20T04:40:09.204300+00:00'
  engine: anthropic
  token_count: 10364
---
# The WG21 Organizational Chain and the C++ Standard

In various technical articles and videos, we often see the abbreviation "WG21," but few people trace the full organizational chain from top to bottom. In reality, while there are many layers, the structure itself isn't complicated—let's walk through this chain first, so that later, when we look at proposals and standard documents, we at least know where these things come from and who manages them.

## Starting with a Counterintuitive Fact

ISO stands for **International Organization for Standardization** (note the American spelling "Organization," and the last word is "Standardization," not "Standards")<RefLink :id="10" preview="ISO, About Us" />. The abbreviation ISO does not come from the English name—the English acronym would be IOS, and in French it would be OIN (Organisation Internationale de Normalisation). The founders felt that neither IOS nor OIN was good enough, so they chose the Greek word *isos* (meaning "equal") as a universal abbreviation. This way, regardless of language, it's called ISO. This piece of trivia has no direct bearing on C++ itself, but it explains why the abbreviation doesn't match the English full name.

::: details Reference Text
The original text from the ISO "About us" page<RefLink :id="10" preview="ISO, About Us" />:

> "ISO, the **International Organization for Standardization**, brings global experts together to agree on the best ways of doing things."
>
> "Because 'International Organization for Standardization' would have different acronyms in different languages ('IOS' in English, 'OIN' in French for Organisation internationale de normalisation), our founders decided to give it the short form 'ISO'. ISO is derived from the Greek word isos (meaning 'equal')."

Readers can visit iso.org/about-us.html to verify this themselves.
:::

## How Many Layers Separate ISO from C++?

ISO doesn't directly manage C++. It first formed a joint body with another organization, the IEC (International Electrotechnical Commission), called JTC1, which stands for Joint Technical Committee 1, the first joint technical committee responsible for information technology standards.

Under JTC1, there is a subcommittee called SC22 (Subcommittee 22), whose full name is "Programming languages, their environments, and system software interfaces." Note the scope—this isn't just programming languages; it also includes "environments" and "system software interfaces," which is why a whole bunch of things fall under SC22.

Below SC22 are the various Working Groups (WG). Many WGs have already been grayed out—they completed their historical missions, and the corresponding language standards are finalized. But the ones that are still active, looking at the roster, include: COBOL, Fortran, Ada, C, Prolog, Linux-related work, programming language vulnerability research, and the one we care about most: C++.

C++ is WG21 within this structure. Why number 21? This number was historically assigned and carries no special meaning—it just happened to be the number when it was C++'s turn.

## A Noteworthy Fact

Looking purely at the number of people participating in standardization, C++'s WG21 is the largest body within SC22 (according to the speaker's observation, if you were to draw a proportional chart by participation, other language working groups might be just a few dots, while C++ would fill the entire chart). Of course, this doesn't mean other languages are unimportant; Fortran, Ada, and others remain irreplaceable in their respective domains (scientific computing, aerospace). But the large number of participants directly explains why the speed and complexity of C++ standardization are what they are—many proposals, many discussions, and many controversies.

## Summary of the Full Chain

From top to bottom: ISO and IEC jointly established JTC1 (Joint Technical Committee 1, for information technology), JTC1 set up SC22 (Subcommittee 22, for programming languages and related things), and SC22 set up WG21 (Working Group 21, exclusively for C++)<RefLink :id="2" preview="ISO/IEC JTC1/SC22/WG21, Official Page" />.

The full formal designation is ISO/IEC JTC1/SC22/WG21.

## Why Clarifying This Chain Matters

Once we understand this chain, when we see the WG21 identifier on a proposal document, we know it has gone through a formal standardization process under the ISO framework—it wasn't just someone's arbitrary decision. "The C++ Standard" transforms from a vague concept into an entity backed by a concrete organizational structure. Looking back, it's really just a few layers of nested committees—nothing mysterious, but when you don't know, it feels clouded in fog.

---

# The Complete Journey of a Proposal from Idea to C++ Standard

Many people's understanding of "how the C++ standard is made" might stop at "a bunch of experts meet and make decisions." In reality, the entire process is a very rigorous funnel mechanism with quite a few layers, but each step has clear boundaries of responsibility.

## Understanding What's Under WG21

When we casually say "the C++ Standards Committee," we're referring to WG21. WG21 is not a flat, monolithic group; it has a bunch of sub-organizations under it—some handle administration, some handle core specifications, some handle the direction of evolution, and then there are the SGs (Study Groups) whose abbreviations we often see in proposal documents but might not be entirely clear on their specific responsibilities. The status of these study groups isn't static—some are active and open to new members, while others have completed their historical missions and been officially closed. But we need to be careful of a cognitive trap—seeing "closed" and assuming that direction will never be brought up again. "Closed" just means the study group itself no longer needs to exist; its conclusions may have been taken over by other groups, or they may be temporarily shelved. The most typical example is UB (undefined behavior); although the related study group has been closed, proposals about UB still exist in abundance across various groups—after all, it's a pain point that anyone writing C++ can't avoid.

## How Far Does an Idea Have to Travel from Brain to Standard?

This part is the most interesting of the entire process. An idea about how C++ should be changed, from inside someone's head into the standard, must go through a complete funnel mechanism.

The first step is to write the idea up as a formal proposal document and send it to a mailing list called the reflector. "Reflector" sounds very sophisticated, but it's really just a mailing list with a somewhat archaic name. Once the proposal is sent out, it gets routed to the corresponding Study Group (SG). Inside the SG, experts in that domain will review it, provide feedback, and then the author goes back to revise it, sends it again, it gets discussed again, and this cycle of polishing repeats. This stage is essentially about validating the idea's viability in a small circle.

When the discussion in the SG has basically matured, the proposal needs to "level up" to be viewed in the broader context of how it fits into the entire C++ ecosystem. At this point, it forks—if it's proposing a library-level feature (like adding a utility in a header file), it goes to LEWG, the Library Evolution Working Group; if it's proposing a language-level feature (like a new syntax rule), it goes to EWG, the Language Evolution Working Group. The difference between LEWG and LWG is this: LEWG handles "evolution," discussing whether the feature is worth doing and how to do it more reasonably; LWG is the "core" group that comes later, responsible for the specific standard wording.

In the evolution groups, there's another round of polishing. When everyone feels the feature's direction is right and the details are mostly in place, it flows from the evolution group into the core group. Library features go to LWG, and language features go to CWG. What the core groups do is very hardcore—they directly modify the C++ standard document, translating the proposal into normative text precise down to the punctuation.

Finally, assuming everyone at every stage is satisfied with the modifications, the proposal enters a plenary vote. All members of WG21 vote together, and once it passes, this feature will appear in the next version of the C++ standard. From idea to landing, it can take several years of iteration.

## The Core of the Entire Process

Once we understand this process, those abbreviations like SGxx, EWG, and LWG on proposal documents become much less daunting<RefLink :id="3" preview="ISO C++ Foundation, The Committee: WG21" />. When we open a proposal, we can consciously check what stage it's at—if it's still in an SG, it means it's in early exploration and the design is highly variable; if it's already reached LWG/CWG, it basically means the general direction is set and only wording-level refinements remain.

There's also an easily overlooked detail: the action of a proposal flowing from an evolution group (EWG/LEWG) to a core group (CWG/LWG) is called "forward" in committee terminology. If you read the meeting minutes, you'll frequently see sentences like "LEWG decided to forward Pxxxx to LWG"—here, "forward" means the proposal has moved one step down the process.

The entire process is essentially a layered peer-review mechanism—first validating feasibility in a small circle, then looking at ecosystem impact in a larger circle, and finally having the most rigorous people finalize the wording. Each step has clear boundaries of responsibility. It's slow, but it's solid.

---

# Just How Slow Is C++ Standardization? A Cross-Language Comparison

When it comes to the C++ standardization timeline, many people's intuition is that C++23 should have come out in 2023, and C++26 will be in 2026. But in reality, the technical work for C++23 was completed in early 2023, while ISO's official publication was delayed until **October 2024** (standard number ISO/IEC 14882:2024)<RefLink :id="11" preview="ISO, ISO/IEC 14882:2024" />, and the C++26 draft still has a bunch of things under discussion, with the finalization highly likely to be pushed back further. The time span from initiation to publication for each version is much longer than most people imagine—this is another side of the massive scale of C++ standardization engineering.

::: details Reference Text
ISO official standard page<RefLink :id="11" preview="ISO, ISO/IEC 14882:2024" /> (iso.org/standard/83626.html):

> Status: Published
> Publication date: **2024-10**
> Edition: 7
> Number of pages: 2104

isocpp.org/std/the-Standard<RefLink :id="3" preview="ISO C++ Foundation, The Committee: WG21" />:

> "The current ISO C++ standard is C++23, formally known as ISO International Standard **ISO/IEC 14882:2024(E)** – Programming Language C++."

Readers can visit iso.org/standard/83626.html to verify the publication date themselves.
:::

So how do other languages do it? Each one takes a very different approach, and a cross-language comparison actually makes it easier to understand why C++ is so slow.

Let's start with Rust. Rust's philosophy is completely different from C++'s. C++'s model is to have an ISO standard document written in extreme detail, and then compiler teams like GCC, Clang, and MSVC each implement this document, with everyone striving to align with the standard. Rust essentially has only one implementation: rustc. More accurately, the test cases in the Rust source code repository *are* the specification—the "standard" is executable. If you write a piece of code and it passes that set of tests in the Rust source, then it's valid Rust code. In the C++ world, we often encounter inconsistencies between "what the standard says" and "what the compiler actually does," but Rust eliminates this problem directly by using test cases.

The direct benefit of this model is speed. When the Rust team wants to add a feature, they modify the compiler code, write the tests, submit a PR, someone reviews and approves it, it gets merged, and by the next release, everyone can use it. There's no problem of "three compilers implementing it separately, with different progress, some supporting it and some not." They also have a leadership committee and an RFC-like proposal process, but overall it's much lighter weight than C++'s. The "single implementation + tests as specification" model is indeed the key reason Rust can maintain a six-week release cycle.

Now look at Python. For a long time, Guido van Rossum (Python's creator) played the role of "Benevolent Dictator for Life"—which direction the language should go, which features to add and which not to, he had the final say. For example, the highly controversial walrus operator `:=` was pushed through during his tenure. But by 2018, Guido stepped down himself, which reflected a practical problem: when a language community grows to a certain size, having one person make the final decisions becomes increasingly exhausting, and internal divisions within the community grow larger. Python now uses a community governance model with a five-person Steering Council, and its proposal mechanism is called PEP (Python Enhancement Proposal), which is somewhat similar to C++'s proposal process but noticeably less formal. They strive to release a version every year, and they've mostly achieved that. By comparison, Python's process is quite a bit lighter than C++'s, but heavier than Rust's—sitting somewhere in the middle.

Finally, let's talk about JavaScript. Nominally, JavaScript has a standardization body behind it called ECMA, and in some contexts JavaScript is called ECMAScript—which is its technically formal name. But in practice, JavaScript's evolution is primarily driven by the V8 engine (behind Chrome) and the Node.js ecosystem. The ECMA standard mostly retroactively endorses "what everyone is already using." This is almost the exact reverse of C++'s "write the standard first, then implement" path.

Putting these together, we get a very interesting spectrum. On one end is Rust's "implementation as standard" model with extremely fast iteration; in the middle are Python and JavaScript, which have standardization processes but relatively light ones, where the actual driving force often comes from the implementation side; on the other end is C++, which first writes an extremely detailed specification document, and then multiple compilers each implement it separately, with the standards committee itself not doing any implementation. Each model has its costs—Rust's cost is essentially no freedom of compiler choice; C++'s cost is that a feature might take several years from proposal to actual usability.

The reason C++ standardization is slow isn't that the committee members aren't working hard, but rather that the "specify first, implement by many" framework inherently dictates that it can't be fast. Whether this cost is worth it is another topic entirely.

---

# How the C++ Standards Committee Operates and Community Participation

Regarding the C++ standardization process, quite a few claims circulate—"the C++ standard is controlled by big tech companies," "proposals are secretly manipulated by vendors," and so on. But when you actually look into it, while there is indeed vendor participation (after all, implementing compilers requires massive engineering resources, and those who invest people naturally have a voice), there is a formal committee process behind it. Proposals must go through multiple stages—drafts, votes, reviews—and it's not about whoever is loudest getting their way. The process is relatively lightweight, unlike some languages that have extremely strict governance structures, but lightweight doesn't mean rule-free.

We also easily fall into the "grass is greener on the other side" trap—seeing Rust's RFC process and thinking it's particularly well-structured and transparent, then complaining about why C++ doesn't learn from it. But looking back, C++'s model has traded for long-term vitality—this language has gone from the 1980s to today, weathered countless waves of technological change, and is not only still alive but thriving. Every governance model has its trade-offs.

## The People Investing Behind the Scenes

The level of commitment from standards committee members is often underestimated. Many people involved in proposals invest massive amounts of their personal time—not work time, personal time—into making this language better. Writing proposals, responding to review comments, repeatedly discussing details on mailing lists, flying around the world to attend in-person meetings—most of this comes with no extra compensation. CppCon was held in Hawaii once, and someone came back saying they spent the entire time in their hotel room working on proposals. Then there are the companies that sponsor engineers to participate in standardization, and the families who support their members attending meetings—these support structures are invisible, but without them, the entire ecosystem wouldn't turn.

## The Value of In-Person Meetings

According to the speaker, there are 11 major international C++ conferences in 2025, the most in history. There was a noticeable dip during COVID, but recovery was quite fast, and it's still climbing—this shows the community is alive. Watching talks online has its value, but sitting in a room with a group of people, chatting during coffee breaks about "how's range-v3 working out in your project" or "have you hit that MSVC pitfall"—that information density and sense of connection is something a screen can't give.

If you're still hesitating about whether to attend an in-person C++ conference or meetup, I'd suggest giving it a try, even if it's just a local half-hour sharing session.

---

## The Practical Form of In-Person Meetups

The number of global C++ meetups registered on isocpp.org alone exceeds one hundred and thirty, which means no matter where you live, there's a high probability of finding one within a hundred miles. Major cities in China basically all have them, and they're slowly appearing in second-tier cities too. If you really can't find one, starting one yourself is completely fine—no formal process needed. Someone literally posted a message in a group saying "I'll be sitting at [some place] with my laptop on Friday night, chatting about C++, come if you want"—four people showed up the first time, and it stabilized at around ten, meeting once a month to look at each other's code and discuss problems.

There are more formal formats too: big companies sponsoring venues, inviting external speakers for technical talks, with slides and Q&A; lightning talk formats where everyone gets five to ten minutes to share a pitfall or a tip, with a fast pace and high information density. Some companies even have regular internal technical exchange time.

A practical benefit of in-person chatting is that the technical solutions and pitfall experiences discussed are often things you can't find online—they're not "systematic" enough to warrant a blog post, but it's precisely this fragmented, frontline experience from real projects that tends to be the most useful.

---

# Online Communities and Resources

Many people spend their early days learning C++ struggling alone. When they hit a compilation error, they search for it themselves, and if they can't find it, they find a different way to write it and work around it. After this state persists for a while, they often discover that the bottleneck isn't effort, but whether they've found the right circles.

## Online Communities

The atmosphere in online communities is often much better than imagined. The C++ Slack (run by the C++ Alliance) has very finely divided channels, and you can join different channels based on your interests. On the Discord side, there are even more choices—the Compiler Explorer Discord and servers specifically for discussing C++ standard proposals are active discussion venues. Beginners and experts interacting in the same space is a real thing in the C++ community—someone who's been learning for two months asks a pointer question in the Slack `#beginners` channel, and several people patiently explain below; ISO committee members discuss proposal details with people on Discord.

A practical suggestion: don't go in and immediately ask questions. Spend a few days lurking first—see how others ask and answer questions, and get a feel for the community's rhythm. You'll learn a lot during the lurking process.

## cppreference—Community-Driven Reference Documentation

cppreference<RefLink :id="4" preview="cppreference.com, C++ Reference" /> is a community-driven, community-operated reference website where every page and every example code has someone actually maintaining it. It's not an official document sponsored by some big company; it's a group of volunteers working on it. Under normal circumstances, it can be modified and supplemented by community members, which is also why it maintains high quality—it's not one person writing it, but countless people maintaining it together. Every time you look up a standard library component, take a moment to check the notes and discussions at the bottom of the page—you can often find valuable information, like known issues with a particular function on a specific compiler.

## Code Sharing Platforms

Beyond real-time chat communities, code sharing platforms like Compiler Explorer<RefLink :id="7" preview="Compiler Explorer, godbolt.org" /> are extremely important for technical communication. Put your code in, generate a link, and drop it anywhere—Discord, Slack, forums, or even send it directly to a colleague. Compared to pasting a big block of code text, a Compiler Explorer link lets others click through, see it directly, modify it directly, and run it directly. The efficiency is completely different.

When debugging a problem, first put the minimal reproduction code on Compiler Explorer, confirm it can be reproduced across multiple compilers, and then go ask the community—the benefit is that when others help you troubleshoot, they don't need to set up an environment; they can click the link and see exactly what you see.

## The Community Is the Core of the C++ Ecosystem

The reason C++ is fascinating isn't just because the language itself is powerful, but because of the people behind it. Those who silently submit patches to open-source projects, those who spend their own time maintaining cppreference, those who pay out of pocket to organize in-person meetups, those who are still helping beginners debug code at 3 AM on Discord—it's these people who constitute the C++ ecosystem. By immersing yourself in the community, you see not just answers to problems, but also how others think about problems, their approaches to solving them, and even their attitudes toward technology.

---

# Participating in the C++ Community—Contributions Come in More Than One Form

Regarding "participating in open-source communities," many people have a narrow understanding—they think it's something only qualified people can do, something only the experts whose names are listed in the committee or authors of well-known libraries are worthy of. But in reality, the ways to participate are far more diverse than imagined.

## "Contribution" Is Broader Than We Think

Contributing to the C++ community doesn't necessarily mean writing a widely used library, or submitting a proposal to the standards committee that gets adopted. Many of the participation methods mentioned in the talk are things you can do right now: if your city doesn't have a C++ meetup, just start one yourself—you don't need to be an expert, you just need to be someone willing to bring people together to chat about C++; attending a conference, even if just to listen and meet a few other people who use C++, is itself already participating in the community; writing up a pitfall you encountered into an article and publishing it so others can avoid the same detour is also a contribution.

## About Taking the Stage

There's a very real description in the talk—standing on the speaking stage, looking back at all those faces watching you, thinking "why am I doing this to myself again." Doing technical sharing doesn't require perfection; you just need to talk about something you've truly understood, talk about the pitfalls you've hit—that's valuable enough. If you have the opportunity to share, even if you're nervous, it's worth trying once.

## About Participating in the C++ Committee

The C++ committee is recruiting. The committee's work needs people at all levels to participate—not just experts in language design, but also feedback from actual users, people to test proposals, write test cases, and report issues. You don't need to be Bjarne Stroustrup to get in; you just need passion and a willingness to invest time.

## One Final Aside

There's a very real detail in the Q&A session: the speaker referred to Barry Revzin as the person responsible for Ranges, only to be corrected on the spot—Barry Revzin has recently done a lot of work on the application side of C++26 Reflection (he gave a "Practical Reflection With C++26" talk at CppCon), while the primary author of Ranges is Eric Niebler (the speaker misspoke it as Eric Kneedler). Though strictly speaking, the main drivers of the Reflection proposal are Daveed Vandevoorde and Herb Sutter, among others, and Revzin is more on the application and teaching side. This kind of "mixing up people's names and their areas of responsibility" is very common—there are so many people and sub-working groups involved in the C++ standards committee that even frequent participants can't necessarily keep them all straight. The speaker's self-deprecating "I'm so terrible" actually makes the community feel very down-to-earth.

## The Threshold for Community Participation

The C++ community isn't some closed circle; it's made up of everyone currently using C++. The simplest contribution might just be sharing something you learned today with a colleague nearby, or answering a beginner's question in the community. You don't need to wait until you're "good enough" to participate—because by then you might have forgotten the confusion of the beginner stage, and it's precisely that confusion that makes for the most valuable sharing content.

---

# The "Never Execute" Instruction in ARM32 Condition Codes—Orthogonal Design and Its Demise

This Q&A segment touches on an interesting architectural design question. In the ARM32 instruction set, every instruction has a four-bit condition code field at the front—you can write `ADDNE` to mean "add if not equal," `MOVEQ` to mean "move if equal," without needing a separate branch instruction, resulting in very high code density. Among the condition codes, there's `AL` (Always), corresponding to 0b1110; but there's also a condition code where all four bits are 1, that is, 0b1111, called `NV`, meaning "Never." A "never execute" instruction—writing it in would just waste space, right?

::: warning Important Correction
The NV condition code only exists in **ARMv4 and earlier versions**. Starting from ARMv5, NV was officially deprecated, and the `0b1111` encoding was reassigned for unconditional instruction extensions. On ARMv7-A, using the condition code `0b1111` results in **UNPREDICTABLE** behavior; it no longer guarantees "never execute." The verification experiment later in this article needs to target the ARMv4 architecture to get the expected results. The original ARM documentation states:

> "Every conditional instruction contains a 4-bit condition code field, the cond field, in bits 31 to 28. This field contains one of the values **0b0000 – 0b1110**."
>
> — ARM Architecture Reference Manual ARMv7-A/R, Section "The condition code field"<RefLink :id="5" preview="Arm Developer, Condition Codes: Conditional Execution" />

Actual verification results (arm-none-linux-gnueabihf-gcc 15.2 + qemu-arm-static):

```bash
# ARMv4：NV 正常工作
$ arm-none-linux-gnueabihf-gcc -static -march=armv4 test.c && qemu-arm-static ./a.out
AL (always): result = 42
NV (never):  result = 0         # ← 符合预期，NV 跳过了 MOV

# ARMv7：直接触发 SIGILL（非法指令异常）
$ arm-none-linux-gnueabihf-gcc -static -march=armv7-a test.c && qemu-arm-static ./a.out
qemu: uncaught target signal 4 (Illegal instruction) - core dumped
```

Verification code is in the repository: `code/volumn_codes/vol10/cppcon/2025/02-some-assembly-required/05-01-arm32-nv-condition.c`.
:::

## Orthogonality—The Design Philosophy of ARM32

The key lies in ARM32's design philosophy: **extreme orthogonality**<RefLink :id="5" preview="Arm Developer, Condition Codes: Conditional Execution" />. Simply put, orthogonality means "choices in each dimension are independent and can be freely combined." In ARM32, the condition code dimension was designed very thoroughly—every condition has its logical opposite. Equal (EQ) has Not Equal (NE), Greater or Equal (GE) has Less Than (LT), Unsigned Higher (HI) has Unsigned Lower or Same (LS)... and so on.

So what's the logical opposite of "Always" (AL)? Naturally, it's "Never" (NV).

Since four bits can represent 16 states, the condition code designers filled all 16 states, each with a corresponding semantic meaning. This wasn't "deliberately leaving a useless one," but the inevitable result of pushing orthogonality to its extreme—it's impossible to keep only 15 and leave one state unassigned, because that wouldn't be orthogonal. The cost is this: in the entire ARM32 instruction encoding space, a full one-sixteenth of the encodings correspond to "do nothing" instructions. This is a design trade-off—trading a bit of space waste for conceptual perfection in the instruction set's symmetry.

This design was indeed the case in the original ARM (ARMv1 through ARMv4). But subsequent ARM versions proved that "extreme orthogonality" itself has costs.

## Hands-on Verification: Writing a "Never Execute" Instruction (ARMv4)

We can verify this ourselves<RefLink :id="6" preview="Arm Developer, Condition Codes: Condition Flags and Codes" />. Because the NV condition code is only valid in ARMv4 and earlier, we need to explicitly specify the architecture version.

::: details Why can't we use ARMv7?
The valid condition code range for ARMv7-A is only `0b0000`–`0b1110`. The encoding `0b1111` was reassigned in ARMv5+—it's either interpreted as a completely different instruction (using the condition code bits to extend the opcode space), or it produces UNPREDICTABLE behavior. Using `.word 0xf3a0002a` on ARMv7 **does not guarantee** the result will be "never execute." The verification code has been placed in the repository (`code/volumn_codes/vol10/cppcon/2025/02-some-assembly-required/05-01-arm32-nv-condition.c`), and readers can compare and test it on ARMv4 and ARMv7 targets themselves.
:::

The environment is Arch Linux WSL, using the `arm-none-linux-gnueabihf-gcc` cross-compilation toolchain (Arm GNU Toolchain 15.2). Note that when compiling, you need to use `-march=armv4` to ensure the NV condition code's semantics:

First, write a simplest C file:

```c
// test_nv.c
void foo(void) {
    __asm__ volatile("mov r0, #42");
}
```

Compile it to assembly to see what a normal `MOV` looks like (note that here we use `-march=armv4`):

```bash
$ arm-none-linux-gnueabihf-gcc -S -O0 -march=armv4 test_nv.c -o test_nv.s
$ cat test_nv.s
    .arch armv4
    .file   "test_nv.c"
    .text
    .align  2
    .global foo
    .arch armv4
    .type   foo, %function
foo:
    push    {r7}
    sub     r7, sp, #0
    mov     r0, #42
    nop
    pop     {r7}
    bx      lr
    .size   foo, .-foo
    .ident  "GCC: (Ubuntu 12.3.0-1ubuntu1~22.04) 12.3.0"
```

Now let's manually construct a "never execute" `MOV`. In the ARM32 `MOV` instruction encoding format, the high four bits are the condition code. We can check the machine code of a normal `MOV R0, #42` using `objdump`:

```bash
$ arm-none-linux-gnueabihf-gcc -c -march=armv4 test_nv.c -o test_nv.o
$ arm-none-linux-gnueabihf-objdump -d test_nv.o

test_nv.o:     file format elf32-littlearm

Disassembly of section .text:

00000000 <foo>:
   0:   e52db004        push    {r7}
   4:   e24db000        sub     r7, sp, #0
   8:   e3a0002a        mov     r0, #42     ; 注意这里：0xe3a0002a
   c:   e320f000        nop
  10:   e49db004        pop     {r7}
  14:   e12fff1e        bx      lr
```

See the `0xe3a0002a`? The high four bits are `0xe`, which is binary `1110`, corresponding to the condition code `AL` (Always). Now change the high four bits from `1110` to `1111`, that is, from `0xe3a0002a` to `0xf3a0002a`. On ARMv4, this is a "never execute" `MOV R0, #42`—it gets decoded, the CPU recognizes it as a MOV instruction, but because the condition code is NV, it never actually executes.

::: warning Reminder
This instruction only behaves as "never execute" on ARMv4 and earlier. If you execute `0xf3a0002a` on ARMv5+ (including ARMv7-A), the behavior is UNPREDICTABLE.
:::

Use `.word` to directly inject the machine code and verify:

```c
// test_nv2.c
#include <stdio.h>

void foo(void) {
    int result = 0;
    // 正常的 MOV R0, #42，条件码 AL (0xe)
    __asm__ volatile("mov r0, #42" : "=r"(result));
    printf("AL (always): result = %d\n", result);

    result = 0;
    // 手动塞入条件码 NV (0xf) 的同一条指令
    // 0xf3a0002a = MOVNV R0, #42  (ARMv4 only!)
    __asm__ volatile(".word 0xf3a0002a" : "=r"(result));
    printf("NV (never):  result = %d\n", result);
}

int main(void) {
    foo();
    return 0;
}
```

Compile and run (note `-march=armv4`):

```bash
$ arm-none-linux-gnueabihf-gcc -march=armv4 test_nv2.c -o test_nv2 -static
$ qemu-arm-static ./test_nv2
AL (always): result = 42
NV (never):  result = 0
```

`result` is still 0—that `MOV R0, #42` was fully decoded, but the CPU took one look at the condition code being `NV`, skipped it directly, and did nothing. `result` kept its previous value of 0.

There's an easy pitfall here: if you didn't add the output constraint for `=r`(result), the compiler might optimize away the `result` entirely, and no matter how you run it, it's always 0, making it easy to mistakenly think you wrote the machine code wrong.

## By the Way: The TEQ Instruction

The Q&A also mentioned an instruction called `TEQP`. `TEQ` itself stands for "Test Equivalence," performing an XOR operation and setting flags, used to compare whether two values are equal (without changing register values, only changing flags). The `P`-suffixed `TEQP` is an instruction in older ARM (pre-ARMv4) for directly manipulating the Processor Status Register (PSR)—in modern ARM, it has been replaced by `MSR`/`MRS` instructions.

## Summary

That one-sixteenth of "no-op" instruction encodings in ARM32 (ARMv4 and earlier) isn't a bug, isn't a legacy issue, but an inevitable byproduct of pushing orthogonal design to the extreme. The designers chose conceptual perfect symmetry, and the cost was wasting some encoding space.

But ARM's own subsequent evolution tells the whole story: ARMv5 deprecated the NV condition code and reclaimed the `0b1111` encoding space; ARM64 (AArch64) completely eliminated the condition code field. "Extreme orthogonality" is conceptually beautiful, but ARM's practice proves that in actual evolution, encoding space and instruction set simplicity ultimately triumphed over conceptual perfect symmetry. After understanding this design history, the experience of reading an assembly manual will be completely different.

---

# Learning Assembly—Should You Look at x86 or RISC-V?

When tinkering on Compiler Explorer, you often wrestle with a question: x86 assembly looks like gibberish—`mov rax, qword ptr [rdi + 8]`, and the register names are long and irregular; switching to RISC-V looks much more understandable, with registers just being `x0` through `x31`, and the instruction format is much more regular. But how big is the gap between looking at RISC-V assembly and the x86 code that actually runs in your work? Would you be wasting your time looking at it?

## The Conclusion: Which Architecture to Look At Depends on the Optimization Level

There's no one-size-fits-all answer to this; the key is the optimization level you choose in Compiler Explorer. If you're using `-O0` (no optimization), it doesn't make much difference whether you look at x86 or RISC-V. What the compiler does under `-O0` is very "generic"—it faithfully translates C++ statements into machine instructions one by one, pushing to the stack when it should, storing to memory when it should, and regardless of architecture, it follows this same pattern. At this level, the knowledge gained about "what the compiler turned the code into" is genuinely interchangeable across architectures.

Let's verify with a simple function:

```cpp
int add_and_double(int a, int b) {
    int sum = a + b;
    return sum * 2;
}
```

Under `-O0`, the x86 and RISC-V outputs use different instructions, but the "flavor" is exactly the same—both first store the parameters to the stack, then load them back from the stack to do addition, store the result back to the stack, and finally load it out again to do multiplication. The compiler is very honest at no optimization; it doesn't do anything clever, and this understanding is architecture-independent.

## Once You Hit -O2 and Above, Things Are Different

When the optimization level is cranked up to `-O2` or even `-O3`, the differences between architectures start to systematically appear. The assembly you see is no longer purely "the compiler's generic optimization strategies"; it's mixed with a lot of "specialized optimizations for this architecture's specific instruction set."

A typical example—counting the number of 1s in an integer with popcount:

```cpp
int count_ones(unsigned int x) {
    int count = 0;
    while (x) {
        count += x & 1u;
        x >>= 1;
    }
    return count;
}
```

Drop this code into x86's Compiler Explorer under `-O2`, and the compiler directly replaces it with a single `popcnt` instruction. The entire loop is gone; the function body is just one instruction. But switch to RISC-V—the loop is still there. The base RISC-V instruction set doesn't have a `popcnt` instruction (although some extensions do), so the compiler can't make this replacement and can only honestly optimize using a loop or a lookup table. The same C++ code, the same `-O2`, and the two architectures produce completely different assembly.

If you learn assembly on RISC-V, you might conclude "the compiler can't automatically recognize the popcount pattern"; if you learn on x86, you'll reach the exact opposite conclusion. Which is right? Both are, and neither are—because this isn't a difference in compiler capability, but a difference in the target architecture's instruction set.

## Practical Strategy

To summarize the strategy: if your goal in learning assembly is to understand "the compiler's high-level optimization decisions"—how inlining is done, how constant propagation is done, how dead code elimination is done—then it doesn't matter which architecture you look at, because these are genuinely cross-architecture universal concepts. When the compiler decides "whether to inline this function," it's considering high-level things like function size, call frequency, and side effects, which have little to do with what CPU is running underneath.

But if your goal is to understand "what the compiler's final generated instructions actually look like," then you're best off looking at the architecture you actually use in your work. At `-O2` and above, every instruction you see could be an "architecture-specific shortcut" that might not even have a corresponding instruction on another architecture.

## Compiler Explorer's AI Feature
