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
  translated_at: '2026-05-26T11:23:17.243193+00:00'
  engine: anthropic
  token_count: 10364
---
# The WG21 Organizational Chain and the C++ Standard

In various technical articles and videos, we often see the abbreviation "WG21," but few people trace the complete organizational chain from top to bottom. Although there are many layers, the structure itself is not complicated. Let's walk through this chain first, so that later, when we look at proposals and standard documents, we at least know where these things come from and who is in charge.

## Starting with a Counterintuitive Fact

ISO stands for **International Organization for Standardization** (note the American spelling "Organization," and that the last word is "Standardization," not "Standards")<RefLink :id="10" preview="ISO, About Us" />. The abbreviation ISO does not come from the English name—the English abbreviation would be IOS, and in French it would be OIN (Organisation Internationale de Normalisation). The founders felt that neither IOS nor OIN was good enough, so they chose the Greek word *isos* (meaning "equal") as a universal abbreviation. This way, regardless of the language, it is called ISO. This piece of trivia has no direct bearing on C++ itself, but it explains why the abbreviation does not match the English full name.

::: details Original Reference Material
The ISO official website "About us" page<RefLink :id="10" preview="ISO, About Us" /> states:

> "ISO, the **International Organization for Standardization**, brings global experts together to agree on the best ways of doing things."
>
> "Because 'International Organization for Standardization' would have different acronyms in different languages ('IOS' in English, 'OIN' in French for Organisation internationale de normalisation), our founders decided to give it the short form 'ISO'. ISO is derived from the Greek word isos (meaning 'equal')."

Readers can visit iso.org/about-us.html to verify this themselves.
:::

## How Many Layers Separate ISO from C++?

ISO does not directly manage C++. It first formed a joint body with another organization, the IEC (International Electrotechnical Commission), called JTC1, which stands for Joint Technical Committee 1. It is responsible for information technology standards.

Under JTC1, there is a subcommittee called SC22 (Subcommittee 22), whose full name is "Programming languages, their environments, and system software interfaces." Note this scope—it is not just programming languages, but also "environments" and "system software interfaces," which is why a whole bunch of things fall under SC22.

Below SC22 are the various Working Groups (WG). Many WGs have already been grayed out—they completed their historical missions, and the corresponding language standards are finalized. But looking at the list of those still active: COBOL, Fortran, Ada, C, Prolog, Linux-related work, programming language vulnerability research, and the one we care about most, C++.

C++ is WG21 in this context. Why number 21? This number was historically assigned and has no special meaning—it just happened to be the number when it was C++'s turn.

## A Noteworthy Fact

Judging solely by the number of participants in standardization, WG21 is the largest body within SC22 (according to the speaker's observation, if you were to draw a proportional chart by participation, other language working groups might just be a few dots, while C++ would fill the entire chart). Of course, this does not mean other languages are unimportant; Fortran and Ada remain irreplaceable in their respective domains (scientific computing, aerospace). However, the large number of participants directly explains why the speed and complexity of C++ standardization are what they are—many proposals, much discussion, and plenty of controversy.

## Summary of the Entire Chain

From top to bottom: ISO and IEC jointly established JTC1 (Joint Technical Committee 1, for information technology), JTC1 set up SC22 (Subcommittee 22, for programming languages and related things), and SC22 set up WG21 (Working Group 21, exclusively for C++)<RefLink :id="2" preview="ISO/IEC JTC1/SC22/WG21, Official Page" />.

The full formal designation is ISO/IEC JTC1/SC22/WG21.

## Why Clarifying This Chain Matters

Once we understand this chain, when we see the WG21 identifier on a proposal document, we know it has gone through a formal standardization process under the ISO framework—it was not decided by someone off the top of their head. "The C++ standard" transforms from a vague concept into an entity backed by a concrete organizational structure. Looking back, it is really just a few layers of nested committees—nothing mysterious, but when you do not know, it feels clouded in fog.

---

# The Complete Journey of a Proposal from Idea to C++ Standard

Many people's understanding of "how the C++ standard is made" might stop at "a bunch of experts meet and make the call." In reality, the entire process is a very rigorous funnel mechanism with quite a few layers, but each step has clear boundaries of responsibility.

## First, Understand What Is Actually Under WG21

When we casually say "the C++ Standards Committee," we are referring to WG21. WG21 is not a flat, monolithic group; it has a bunch of sub-organizations under it—some handle administration, some handle core specifications, some handle the direction of evolution, and there are a bunch of SGs (Study Groups) whose abbreviations we often see in proposal documents but might not be entirely clear on their specific responsibilities. The status of these study groups is not static; some are active and open to new members, while others have completed their historical missions and been officially closed. However, we need to be careful of a cognitive trap—seeing "closed" and assuming this direction will never be brought up again. "Closed" just means the study group itself no longer needs to exist. Its conclusions might have been taken over by other groups, or they might be temporarily shelved. The most typical example is UB (undefined behavior). The related study group has been closed, but proposals about UB still exist in abundance across various groups—after all, it is a pain point that anyone writing C++ cannot avoid.

## How Far Does an Idea Have to Travel from Brain to Standard?

This part is the most interesting of the entire process. For an idea about how C++ should be changed, getting from your brain into the standard requires going through a complete funnel mechanism.

The first step is to write the idea into a formal proposal document and send it to a mailing list called a reflector. "Reflector" sounds very profound, but it is really just a mailing list with a somewhat archaic name. After the proposal is sent out, it gets routed to the corresponding Study Group (SG). Within the SG, experts in that field will review it, provide feedback, and then the author goes back to revise it. They send it again, discuss it again, and iteratively polish it. This phase is essentially about validating the idea's viability in a small circle.

When the discussion in the SG is mostly mature, the proposal needs to be "upgraded" to be viewed in a broader context of how it fits into the entire C++ ecosystem. At this point, it forks—if it is a library-level feature (like adding a utility in a header file), it goes to LEWG, the Library Evolution Working Group; if it is a language-level feature (like a new syntax rule), it goes to EWG, the Language Evolution Working Group. The difference between LEWG and LWG is this: LEWG handles "evolution," discussing whether the feature is worth doing and how to do it more reasonably; LWG is the "core" group that comes later, responsible for the specific standard wording.

In the evolution groups, there is another round of polishing. When everyone feels the feature's direction is right and the details are mostly in place, it flows from the evolution group into the core group. Library features go to LWG, and language features go to CWG. What the core groups do is very hardcore—they directly modify the C++ standard document, translating the proposal into normative text precise down to the punctuation mark.

Finally, assuming everyone at all stages is satisfied with the modification, the proposal enters a plenary vote. All members of WG21 vote together, and once it passes, this feature will appear in the next version of the C++ standard. From idea to landing, it can take several years of iteration.

## The Core of the Entire Process

Once we understand this process, those abbreviations like SGxx, EWG, and LWG on proposal documents are no longer so headache-inducing<RefLink :id="3" preview="ISO C++ Foundation, The Committee: WG21" />. When we open a proposal, we can consciously look at what stage it is currently in—if it is still in an SG, it means it is in early exploration and the design is highly variable; if it has already reached LWG/CWG, it basically means the general direction is set, and only wording-level refinements remain.

There is also an easily overlooked detail: the action of a proposal flowing from the evolution groups (EWG/LEWG) to the core groups (CWG/LWG) is called "forward" in committee terminology. If you read the meeting minutes, you will often see sentences like "LEWG decided to forward Pxxxx to LWG." Here, "forward" means the proposal has moved one step down the process.

The entire process is essentially a layered peer-review mechanism—first validating feasibility in a small circle, then looking at the ecosystem impact in a larger circle, and finally having the most rigorous people finalize the wording. Each step has clear boundaries of responsibility. It is slow, but it is indeed steady.

---

# Just How Slow Is C++ Standardization? A Horizontal Comparison with Other Languages

When it comes to the C++ standardization timeline, many people's intuition is that C++23 should have come out in 2023, and C++26 will be in 2026. But in reality, the technical work for C++23 was completed in early 2023, and ISO's official publication was delayed until **October 2024** (standard number ISO/IEC 14882:2024)<RefLink :id="11" preview="ISO, ISO/IEC 14882:2024" />. The C++26 draft still has a bunch of things under discussion, and the final publication will very likely be delayed further. The time span from initiation to publication for each version is much longer than most people imagine—this is another side effect of the sheer scale of the C++ standardization effort.

::: details Original Reference Material
ISO official standard page<RefLink :id="11" preview="ISO, ISO/IEC 14882:2024" /> (iso.org/standard/83626.html):

> Status: Published
> Publication date: **2024-10**
> Edition: 7
> Number of pages: 2104

isocpp.org/std/the-Standard<RefLink :id="3" preview="ISO C++ Foundation, The Committee: WG21" />:

> "The current ISO C++ standard is C++23, formally known as ISO International Standard **ISO/IEC 14882:2024(E)** – Programming Language C++."

Readers can visit iso.org/standard/83626.html to verify the publication date.
:::

So how do other languages do it? Each takes a very different path, and a horizontal comparison makes it much easier to understand why C++ is so slow.

Let's start with Rust. Rust's philosophy is completely different from C++'s. C++'s model is to have an extremely detailed ISO standard document, and then compiler teams like GCC, Clang, and MSVC each implement this document, with everyone striving to align with the standard. Rust essentially has only one implementation: rustc. More accurately, the test cases in the Rust source code repository are themselves the specification—the "standard" is executable. If you write a piece of code and it passes that set of tests in the Rust source, then it is valid Rust code. In the C++ world, we often encounter inconsistencies between "what the standard says" and "what the compiler actually does," but Rust directly eliminates this problem by using test cases.

The direct benefit of this model is speed. When the Rust team wants to add a feature, they modify the compiler code, write the tests, submit a PR, someone reviews and approves it, it gets merged, and by the next release, everyone can use it. There is no problem of "three compilers implementing it separately, with different progress, where some support it and some do not." They also have a leadership committee and an RFC-like proposal process, but overall it is much lighter weight than C++'s. The "single implementation + tests as specification" model is indeed the key reason Rust can maintain a six-week release cycle.

Now look at Python. For a long time, Guido van Rossum (Python's creator) played the role of "Benevolent Dictator for Life"—which direction the language should go, which features to add and which not to, he had the final say. For example, the highly controversial walrus operator `:=` was pushed through during his tenure. But in 2018, Guido stepped down himself. Behind this lies a practical problem: when a language community grows to a certain size, having one person make the final decisions becomes increasingly exhausting, and internal divisions within the community grow larger. Python now uses a community governance model with a five-person Steering Council. Its proposal mechanism is called PEP (Python Enhancement Proposal), which is somewhat similar to C++'s proposal process but noticeably less formal. They strive to release a version every year, and they have largely achieved that. By comparison, Python's process is quite a bit lighter than C++'s, but heavier than Rust's, sitting somewhere in the middle.

Finally, let's talk about JavaScript. Nominally, JavaScript has a standardization body behind it called ECMA, and in some contexts, JavaScript is called ECMAScript—which is its technically formal name. But in practice, JavaScript's evolution is primarily driven by the V8 engine (behind Chrome) and the Node.js ecosystem. The ECMA standard mostly retroactively endorses "what everyone is already using." This is almost the exact reverse of C++'s "write the standard first, then implement" path.

Putting these together forms a very interesting spectrum. On one end is Rust's "implementation is the standard" model with extremely fast iteration; in the middle are Python and JavaScript, which have standardization processes but relatively light ones, where the actual driving force often comes from the implementation side; on the other end is C++, which first writes an extremely detailed specification document, and then multiple compilers separately implement it, with the standards committee itself not doing any implementation. Each model has its costs—Rust's cost is essentially no freedom of compiler choice; C++'s cost is that a feature might take several years from proposal to actual usability.

The reason C++ standardization is slow is not that the committee members are not working hard, but rather that the "specify first, implement by many" framework inherently dictates that it cannot be fast. Whether this cost is worth it is another topic entirely.

---

# How the C++ Standards Committee Operates and Community Participation

Regarding the C++ standardization process, quite a few claims circulate in the community—"the C++ standard is controlled by big tech companies," "proposals are secretly manipulated by vendors," and similar views pop up from time to time. But in reality, although there is indeed vendor participation (after all, implementing compilers requires massive engineering resources, and those who invest people naturally have a voice), there is a formal committee process behind it. Proposals must go through multiple stages—drafts, votes, reviews—and it is not a matter of whoever is loudest gets the final say. The process is relatively lightweight, unlike some languages that have extremely strict governance structures, but lightweight does not mean rule-free.

We also easily fall into the "grass is greener on the other side" trap—seeing Rust's RFC process and thinking it is especially well-regulated and transparent, then complaining about why C++ does not learn from it. But looking back, C++'s model has traded for long-term vitality—this language has gone from the 1980s to today, weathered countless waves of technological change, and is not only still alive but thriving. Every governance model has its trade-offs.

## The People Investing Behind the Scenes

The level of commitment from standards committee members is often underestimated. Many people involved in proposals invest a massive amount of their personal time—not work time, personal time—into making this language better. Writing proposals, responding to review comments, repeatedly discussing details on mailing lists, flying around the world to attend face-to-face meetings—most of this comes with no extra compensation. CppCon was held in Hawaii once, and someone came back saying they spent the entire time in their hotel room going over proposals. Then there are the companies that sponsor engineers to participate in standardization, and the families who support their members attending meetings—these support structures are invisible, but without them, the entire ecosystem would stop turning.

## The Value of In-Person Meetings

According to the speaker, there are 11 major international C++ conferences in 2025, the most in history. There was a noticeable dip during COVID, but recovery was quite fast, and it is still climbing—this shows the community is alive. Watching talks online has its value, but sitting in a room with a group of people and chatting during coffee breaks about "how is range-v3 working out in your project" or "have you hit that MSVC pitfall"—that information density and sense of connection is something a screen cannot give.

If you are still hesitating about whether to attend an in-person C++ conference or meetup, I suggest giving it a try, even if it is just a local half-hour sharing session.

---

## The Actual Form of In-Person Meetups

The number of global C++ meetups registered on isocpp.org alone exceeds one hundred and thirty, which means no matter where you live, there is a high probability of finding one within a hundred miles. Major cities in China basically all have them, and they are slowly appearing in second-tier cities too. If you really cannot find one, starting one yourself is completely fine—no formal process is needed. Someone literally posted a message in a group saying, "I'm bringing my laptop and sitting at [place] on Friday night to chat about C++, come if you want." The first time, four people showed up; later it stabilized at around ten, meeting once a month to look at each other's code and discuss problems.

There are more formal formats too: big companies sponsoring venues, inviting external speakers for technical sharing, with slides and Q&A; lightning talk formats, where each person gets five to ten minutes to share a pitfall or a tip—the pace is fast, and the information density is high. Some companies even have regular internal technical exchange time.

A practical benefit of in-person chatting is that the technical solutions and pitfall experiences discussed are often things you cannot find online—they are not "systematic" enough to warrant a blog post, but it is precisely this fragmented, frontline experience from real projects that is often the most useful.

---

# Online Communities and Resources

Many people spend their early days learning C++ struggling alone. When they hit a compilation error, they search for it themselves; if they cannot find an answer, they find a different way to write it and work around it. After staying in this state for a while, they often discover that the bottleneck is not their level of effort, but whether they have found the right circle.

## Online Communities

The atmosphere in online communities is often much better than imagined. The C++ Slack (run by the C++ Alliance) has very finely divided channels, and you can join different channels based on your interests. On Discord, there are even more choices—the Compiler Explorer Discord and servers specifically for discussing C++ standard proposals are active discussion venues. Beginners and experts interacting in the same space is a real thing in the C++ community—someone who has been learning for two months asks a pointer question in the `#beginners` channel on Slack, and several people patiently explain below; ISO committee members discuss proposal details with people on Discord.

A practical piece of advice: do not ask questions the moment you join. Spend a few days lurking first—see how others ask and answer questions, and get a feel for the community's rhythm. You will learn a lot during the lurking process.

## cppreference—Community-Driven Reference Documentation

cppreference<RefLink :id="4" preview="cppreference.com, C++ Reference" /> is a community-driven, community-operated reference website. Every single page and every example code on it is actively maintained by real people. It is not an official document sponsored by some big company, but rather a group of volunteers working on it. Under normal circumstances, it can be modified and supplemented by community members, which is also why it maintains high quality—it is not just one person writing, but countless people maintaining it together. Every time you look up a standard library component, take a moment to glance at the notes and discussions at the bottom of the page. You can often find very valuable information there, such as known issues with a particular function on a specific compiler.

## Code Sharing Platforms

Besides real-time chat communities, code sharing platforms like Compiler Explorer<RefLink :id="7" preview="Compiler Explorer, godbolt.org" /> are incredibly important for technical exchange. Put your code in, generate a link, and drop it anywhere—Discord, Slack, forums, or even send it directly to a colleague. Compared to pasting a huge block of code text, a Compiler Explorer link lets others click, read, modify, and run it directly. The efficiency is completely different.

When debugging a problem, first put the minimal reproduction code on Compiler Explorer, confirm it can be reproduced on multiple compilers, and then go ask the community—the benefit of doing this is that when others help you troubleshoot, they do not need to set up an environment; they can just click the link and see exactly what you see.

## The Community Is the Core of the C++ Ecosystem

The reason C++ is fascinating is not just because the language itself is powerful, but even more so because of the people behind it. The people who silently submit patches to open-source projects, the people who spend their own time maintaining cppreference, the people who pay out of pocket to organize in-person meetups, the people who are still helping beginners debug code at 3 AM on Discord—it is these people who make up the C++ ecosystem. By immersing yourself in the community, you see not just answers to problems, but also how others think about problems, their approaches to solving them, and even their attitudes toward technology.

---

# Participating in the C++ Community—Contributions Come in More Than One Form

Regarding "participating in the open-source community," many people have a narrow understanding—they feel it is something only qualified people can do, something only the big shots whose names are listed in the committee or authors of well-known libraries are worthy of doing. But in reality, the ways to participate are far more diverse than imagined.

## "Contribution" Is Broader Than We Think

Contributing to the C++ community does not mean you have to write a widely used library, or submit a proposal to the standards committee that gets adopted. Many of the participation methods mentioned in the talk are things you can do right now: if your city does not have a C++ meetup, just start one yourself—you do not need to be an expert, you just need to be someone willing to bring people together to chat about C++; attend a conference, even if just to listen and meet a few other people who are also using C++, which in itself is already participating in the community; write an article about a pitfall you hit and publish it, so the people coming after you can avoid the same detour—this is also a contribution.

## About Taking the Stage

There is a very real description in the talk—standing on the speaking stage, looking back at the countless faces staring at you, thinking, "Why am I doing this to myself again." Doing technical sharing does not require you to present perfectly; you only need to talk about something you truly understand, about a pitfall you have hit—that alone is valuable enough. If you have the opportunity to share, even if you are nervous, it is worth trying once.

## About Participating in the C++ Committee

The C++ committee is recruiting. The committee's work needs people at all levels to participate—not just experts in language design, but also feedback from actual users, people to test proposals, write use cases, and report issues. You do not need to be Bjarne Stroustrup to get in; you just need passion and a willingness to invest time.

## One Final Aside

There is a very real detail in the Q&A session: the speaker referred to Barry Revzin as the person responsible for Ranges, only to be corrected on the spot—Barry Revzin has recently done a lot of work on the application side of C++26 Reflection (he gave a "Practical Reflection With C++26" talk at CppCon), while the primary author of Ranges is Eric Niebler (the speaker misspoke it as Eric Kneedler). However, strictly speaking, the main drivers of the Reflection proposal are Daveed Vandevoorde and Herb Sutter, among others, and Revzin is more on the application and teaching side. This kind of "mixing up people's names and their areas of responsibility" is very common. The C++ standards committee involves so many people and sub-working groups that even regular participants cannot necessarily keep them all straight. The speaker's self-deprecating "I'm so terrible at this" actually makes the community feel very down-to-earth.

## The Threshold for Community Participation

The C++ community is not some closed circle; it is made up of every person currently using C++. The simplest contribution might just be sharing what you learned today with a colleague next to you, or answering a beginner's question in the community. You do not need to wait until you are "good enough" to participate—because by then you might have forgotten the confusions of the beginner stage, and it is precisely those confusions that make for the most valuable sharing content.

---

# The "Never Execute" Instruction in ARM32 Condition Codes—Orthogonal Design and Its Demise

This Q&A segment touches on an interesting architectural design question. In the ARM32 instruction set, every instruction has a four-bit condition code field at the front. You can write `ADDNE` to mean "add if not equal," or `MOVEQ` to mean "move if equal," without needing a separate branch instruction, resulting in very high code density. Among the condition codes, there is `AL` (Always), corresponding to 0b1110; but there is another condition code where all four bits are 1, that is, 0b1111, called `NV`, meaning "Never." A "never execute" instruction—writing it in would just be wasting space, right?

::: warning Important Correction
The NV condition code only exists in **ARMv4 and earlier versions**. Starting from ARMv5, NV was officially deprecated, and the `0b1111` encoding was reassigned for unconditional instruction extensions. On ARMv7-A, using the condition code `0b1111` results in **UNPREDICTABLE** behavior; it no longer guarantees "never execute." The verification experiment later in this article needs to target the ARMv4 architecture to get the expected results. The official ARM documentation states:

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

The key lies in ARM32's design philosophy: **extreme orthogonality**<RefLink :id="5" preview="Arm Developer, Condition Codes: Conditional Execution" />. Simply put, orthogonality means "choices in each dimension are independent and can be freely combined." In ARM32, the condition code dimension was designed very thoroughly—every condition has its logical opposite. Equal (EQ) has Not Equal (NE), Greater Than or Equal (GE) has Less Than (LT), Unsigned Higher (HI) has Unsigned Lower or Same (LS)... and so on.

So what is the logical opposite of "Always" (AL)? Naturally, it is "Never" (NV).

Because four bits can represent 16 states, the condition code designers filled all 16 states, each with a corresponding semantic meaning. This was not "deliberately leaving a useless one," but the inevitable result of pushing orthogonality to its extreme—it is impossible to keep only 15 and leave one empty, because that would not be orthogonal. The cost is this: in the entire ARM32 instruction encoding space, a full one-sixteenth of the encodings correspond to "do nothing" instructions. This is a design trade-off—trading a little space waste for conceptual perfection in the instruction set's symmetry.

This design was indeed the case in the original ARM (ARMv1 through ARMv4). But subsequent versions of ARM proved that "extreme orthogonality" itself has a cost.

## Hands-on Verification: Writing a "Never Execute" Instruction (ARMv4)

We can verify this ourselves<RefLink :id="6" preview="Arm Developer, Condition Codes: Condition Flags and Codes" />. Because the NV condition code is only valid in ARMv4 and earlier, we need to explicitly specify the architecture version.

::: details Why Can't We Use ARMv7?
The valid condition code range for ARMv7-A is only `0b0000`–`0b1110`. The encoding `0b1111` was reassigned in ARMv5+—it is either interpreted as a completely different instruction (using the condition code bits to extend the opcode space), or it produces UNPREDICTABLE behavior. Using `.word 0xf3a0002a` on ARMv7 **does not guarantee** the result will be "never execute." The verification code has been placed in the repository (`code/volumn_codes/vol10/cppcon/2025/02-some-assembly-required/05-01-arm32-nv-condition.c`), and readers can compare and test it on ARMv4 and ARMv7 targets themselves.
:::

The environment is Arch Linux WSL, using the `arm-none-linux-gnueabihf-gcc` cross-compilation toolchain (Arm GNU Toolchain 15.2). Note that when compiling, you need to use `-march=armv4` to ensure the semantics of the NV condition code:

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

Now let's manually construct a "never execute" `MOV`. In the ARM32 `MOV` instruction encoding format, the high four bits are the condition code. We can look at the machine code of a normal `MOV R0, #42` using `objdump`:

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

See the `0xe3a0002a`? The high four bits are `0xe`, which is binary `1110`, corresponding to the condition code `AL` (Always). Now change the high four bits from `1110` to `1111`, that is, from `0xe3a0002a` to `0xf3a0002a`. On ARMv4, this is a "never execute" `MOV R0, #42`—it gets decoded, the CPU recognizes it as a MOV instruction, but because the condition code is NV, it will never actually execute.

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

There is an easy pitfall here: if you did not add the output constraint for `=r`(result), the compiler might optimize away the `result` entirely. No matter how you run it, it would be 0, and you could easily mistake it for having written the machine code incorrectly.

## By the Way: The TEQ Instruction

The Q&A also mentioned an instruction called `TEQP`. `TEQ` itself stands for "Test Equivalence"; it performs an XOR operation and sets the flags, used to compare whether two values are equal (without changing the register values, only changing the flags). The `P`-suffixed `TEQP` is an instruction in older ARM (pre-ARMv4) used to directly manipulate the Processor Status Register (PSR)—in modern ARM, it has been replaced by `MSR`/`MRS` instructions.

## Summary

That one-sixteenth of "no-op" instruction encodings in ARM32 (ARMv4 and earlier) is not a bug, not a legacy issue, but an inevitable byproduct of pushing orthogonal design to the extreme. The designers chose conceptual perfect symmetry, and the cost was wasting some encoding space.

But ARM's own subsequent evolution tells the whole story: ARMv5 deprecated the NV condition code and reclaimed the `0b1111` encoding space; ARM64 (AArch64) completely removed the condition code field. "Extreme orthogonality" is conceptually beautiful, but ARM's practice proves that in actual evolution, encoding space and instruction set simplicity ultimately triumphed over conceptual perfect symmetry. After understanding this design history, the experience of reading an assembly manual will be completely different.

---

# Learning Assembly—Should You Look at x86 or RISC-V?

When tinkering on Compiler Explorer, we often wrestle with a question: x86 assembly looks like gibberish—`mov rax, qword ptr [rdi + 8]`, and the register names are long and irregular; switching to RISC-V looks much more understandable, with registers simply being `x0` through `x31`, and the instruction format is much more regular. But how big is the gap between looking at RISC-V assembly and the x86 code actually running in your work? Will you have wasted your time looking at it?

## Conclusion: Which Architecture to Look At Depends on the Optimization Level

There is no one-size-fits-all answer to this; the key is the optimization level you choose in Compiler Explorer. If you are using `-O0` (no optimization), it does not make much difference whether you look at x86 or RISC-V. What the compiler does under `-O0` is very "generic"—it faithfully translates C++ statements into machine instructions one by one, pushing to the stack when it should, storing to memory when it should, and this routine is the same regardless of architecture. At this level, the knowledge gained about "what the compiler turned the code into" is indeed interchangeable across architectures.

Let's verify this with a simple function:

```cpp
int add_and_double(int a, int b) {
    int sum = a + b;
    return sum * 2;
}
```

Under `-O0`, the x86 and RISC-V outputs use different instructions, but the "flavor" is exactly the same—they both first store the parameters to the stack, then load them back from the stack to do the addition, store the result back to the stack, and finally load it out again to do the multiplication. The compiler is very honest at no optimization and will not do anything clever; this understanding is architecture-independent.

## Once You Hit -O2 and Above, Things Are Different

When the optimization level is cranked up to `-O2` or even `-O3`, the differences between architectures start to manifest systematically. The assembly you see is no longer purely "the compiler's generic optimization strategies"; it is mixed with a large amount of "specialized optimizations for this architecture's specific instruction set."

A typical example—counting the number of 1s in an integer, popcount:

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

Drop this code into x86's Compiler Explorer under `-O2`, and the compiler directly replaces it with a single `popcnt` instruction. The entire loop is gone, and the function body is just one instruction. But switch to RISC-V—the loop is still there. The base RISC-V instruction set does not have a `popcnt` instruction (although some extensions do), so the compiler cannot make this replacement and can only honestly optimize using a loop or a lookup table. The exact same C++ code, the exact same `-O2`, and the two architectures produce completely different assembly.

If you learn assembly on RISC-V, you might conclude "the compiler cannot automatically recognize the popcount pattern"; if you learn on x86, you will reach the exact opposite conclusion. Who is right? Both are, and neither are—because this is not a difference in compiler capability, but a difference in the target architecture's instruction set.

## Practical Strategy

To summarize the strategy: if your goal in learning assembly is to understand "the compiler's high-level optimization decisions"—how inlining is done, how constant propagation is done, how dead code elimination is done—then it does not matter which architecture you look at, because these are indeed cross-architecture universal concepts. When the compiler decides "whether to inline this function," it considers high-level things like function size, call frequency, and side effects, which have little to do with what CPU is running underneath.

But if your goal is to understand "what the compiler's final generated instructions actually look like,"
