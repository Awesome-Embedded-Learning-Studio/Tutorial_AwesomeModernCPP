---
chapter: 2
conference: cppcon
conference_year: 2025
cpp_standard:
- 17
- 20
description: 'CppCon 2025 Talk Notes — C++: Some Assembly Required by Matt Godbolt'
difficulty: intermediate
order: 7
platform: host
reading_time_minutes: 70
speaker: Matt Godbolt
tags:
- cpp-modern
- host
- intermediate
talk_title: 'C++: Some Assembly Required'
title: WG21 Standardization and x86/RISC-V Assembly Philosophy
video_bilibili: https://www.bilibili.com/video/BV1ptCCBKEwW?p=2
video_youtube: https://www.youtube.com/watch?v=zoYT7R94S3c
translation:
  source: documents/vol10-open-lecture-notes/cppcon/2025/02-some-assembly-required/07-wg21-standardization-and-assembly-philosophy.md
  source_hash: 5999582e915fb6ab7f63b74f58ebda7946c81b5a165ab46834c33a852a948b41
  translated_at: '2026-06-16T04:41:18.056852+00:00'
  engine: anthropic
  token_count: 10529
---
# WG21 and the Organizational Chain of the C++ Standard

In various technical articles and videos, we often see the abbreviation "WG21," but few people clearly explain this complete organizational chain from start to finish. Although there are many layers, the structure itself is not complex. Let's clarify this chain first, so that later when we look at proposals and standard documents, we at least know where these things come from and who manages them.

## Starting with a Counterintuitive Fact

ISO stands for **International Organization for Standardization** (note the American spelling "Organization," and the last word is "Standardization" rather than "Standards")<RefLink :id="10" preview="ISO, About Us" />. The abbreviation ISO does not come from the English name—the English abbreviation would be IOS, and in French, it would be OIN (*Organisation Internationale de Normalisation*). The founders felt that neither IOS nor OIN was good enough, so they chose the Greek word *isos* (meaning equal) as a unified abbreviation. This way, it is called ISO in any language. While this piece of trivia has no direct relationship with C++, it explains why the abbreviation doesn't match the English full name.

::: details Reference Text
From the ISO official website "About us" page<RefLink :id="10" preview="ISO, About Us" />:

> "ISO, the **International Organization for Standardization**, brings global experts together to agree on the best ways of doing things."
>
> "Because 'International Organization for Standardization' would have different acronyms in different languages ('IOS' in English, 'OIN' in French for Organisation internationale de normalisation), our founders decided to give it the short form 'ISO'. ISO is derived from the Greek word isos (meaning 'equal')."

Readers can visit iso.org/about-us.html to verify.
:::

## How Many Layers Separate ISO from C++?

ISO does not manage C++ directly. First, it formed a joint venture with another organization, the IEC (International Electrotechnical Commission), called JTC1. The full name is Joint Technical Committee 1. It manages information technology standards.

Then, under JTC1, there are subcommittees, such as SC22 (Subcommittee 22). The full name is "Programming languages, their environments and system software interfaces." Note the scope—it is not just programming languages, but also "environments" and "system software interfaces," so SC22 covers a bunch of things.

Under SC22, we finally have the Working Groups (WG). Many WGs have been grayed out—they have completed their historical missions, and the corresponding language standards are finalized. But the ones that are still active, looking at the list: COBOL, Fortran, Ada, C, Prolog, Linux-related items, programming language vulnerability research, and the one we care about most, C++.

Inside this structure, C++ is WG21. Why number 21? This number is a historical assignment with no special meaning; it just happened to be this number when it was its turn.

## A Notable Fact

Looking solely at the number of participants in standardization, C++'s WG21 is the largest in the entire SC22 (according to the speaker's observation, if you were to draw a proportional chart based on participation, other language working groups might be just a few dots, while C++ would fill the entire chart). Of course, this doesn't mean other languages aren't important; Fortran, Ada, and others remain indispensable in their respective fields (scientific computing, aerospace). However, the high number of participants directly explains why the speed and complexity of C++ standardization are what they are—many proposals, many discussions, and many controversies.

## Summary of the Chain

From top to bottom: ISO and IEC jointly established JTC1 (Joint Technical Committee 1, for Information Technology), JTC1 set up SC22 (Subcommittee 22, for Programming Languages and related items), and SC22 set up WG21 (Working Group 21, specifically for C++)<RefLink :id="2" preview="ISO/IEC JTC1/SC22/WG21, Official Page" />.

The complete formal title is ISO/IEC JTC1/SC22/WG21.

## Why Clarifying This Chain Matters

Once we understand this chain, when we see the WG21 identifier on proposal documents, we know these are things that have gone through the formal standard-setting process under the ISO framework, not something someone decided on a whim. "The C++ Standard" transforms from a vague concept into an entity backed by a specific organizational structure. Looking back, it's just a few nested committees—nothing mysterious, but it feels like fog when you don't know it.

---

# The Complete Journey of a Proposal from Idea to C++ Standard

Many people's understanding of "how the C++ standard is made" might stop at the stage of "a group of experts meeting and making decisions." In reality, the entire process is a rigorous funnel mechanism with many layers, but each step has clear boundaries of responsibility.

## Understanding What's Under WG21

When we talk about "The C++ Standards Committee," we are referring to WG21. WG21 is not a flat, large group; it has a bunch of sub-organizations attached. There are administrative ones, ones for core specifications, ones for evolution directions, and a bunch of SGs (Study Groups) whose abbreviations we often see in proposal documents but might not be clear on their specific responsibilities. The status of these study groups is not static; some are active and open to new members, while others have completed their historical missions and are fully closed. However, watch out for a cognitive trap—seeing "closed" and assuming this direction will never be mentioned again. "Closed" just means the study group itself doesn't need to exist anymore; the conclusions it produced might have been taken over by other groups, or temporarily shelved. The most typical example is UB (Undefined Behavior); the related study group is closed, but proposals regarding UB still exist in various groups—after all, this is a pain C++ users can't avoid.

## How Far Does an Idea Travel from Brain to Standard?

This part is the most interesting part of the process. An idea on how to change C++, from the brain to the standard, must go through a complete funnel mechanism.

The first step is to write the idea into a formal proposal document and send it to a mailing list called a reflector. "Reflector" sounds profound, but it's actually just a mailing list with a slightly old-fashioned name. After the proposal is sent out, it is routed to the corresponding Study Group (SG). Inside the SG, experts in that field will review it, provide feedback, and the author goes back to revise it. Then send it again, discuss again, and polish it back and forth. This stage is essentially about verifying whether the idea is reliable within a small scope.

When the discussion in the SG is basically mature, the proposal needs to be "upgraded" to see how it integrates into the wider C++ ecosystem. At this point, it splits—if it's a library-level feature (like a new tool in a header file), it goes to LEWG (Library Evolution Working Group); if it's a language-level feature (like new syntax rules), it goes to EWG (Language Evolution Working Group). The difference between LEWG and LWG is: LEWG manages "evolution," discussing whether the feature is worth doing and how to do it more reasonably; while LWG is the "core" group that comes later, responsible for the specific standard wording.

In the evolution groups, it undergoes another round of polishing. When everyone feels the feature direction is right and the details are basically in place, it flows from the evolution group to the core group. Library features go to LWG, language features go to CWG. What the core groups do is very hardcore—they directly modify the C++ standard document, translating the proposal into normative text precise down to the punctuation marks.

Finally, assuming everyone in all stages is satisfied with this modification, the proposal enters the full vote stage. All members of WG21 vote together. Once passed, this feature will appear in the next version of the C++ standard. From idea to landing, it may undergo several years of iteration.

## The Core of the Process

Once we understand this process, the abbreviations SGxx, EWG, and LWG on proposal documents aren't so headache-inducing<RefLink :id="3" preview="ISO C++ Foundation, The Committee: WG21" />. Opening a proposal, we can consciously look at what stage it is currently in—if it's still in SG, it means it's in early exploration with large design variables; if it's already in LWG/CWG, it basically means the general direction is set, and only wording-level polishing remains.

There is also an easily overlooked detail: the action of a proposal flowing from the evolution group (EWG/LEWG) to the core group (CWG/LWG) is called "forwarding" in committee terminology. If you read meeting minutes, you will often see sentences like "LEWG decided to forward Pxxxx to LWG." Here, forwarding means the proposal has moved one step down the process.

The entire process is essentially a layered peer review mechanism—first verify feasibility in a small circle, then look at the ecosystem impact in a larger circle, and finally have the most rigorous people finalize the wording. Each step has clear boundaries of responsibility. Although slow, it is indeed steady.

---

# How Slow is C++ Standardization—A Horizontal Comparison with Other Languages

When discussing the timeline of C++ standardization, many people's intuition is that C++23 should have come out in 2023, and C++26 in 2026. But in reality, the technical work for C++23 was completed in early 2023, while ISO publication dragged on until **October 2024** (Standard number ISO/IEC 14882:2024)<RefLink :id="11" preview="ISO, ISO/IEC 14882:2024" />. The draft for C++26 still has a pile of things under discussion, and the final release will likely be delayed further. The time span from initiation to publication for each version is much longer than most people imagine—this is also a side effect of the massive scale of the C++ standardization project.

::: details Reference Text
ISO official standard page<RefLink :id="11" preview="ISO, ISO/IEC 14882:2024" /> (iso.org/standard/83626.html):

> Status: Published
> Publication date: **2024-10**
> Edition: 7
> Number of pages: 2104

isocpp.org/std/the-Standard<RefLink :id="3" preview="ISO C++ Foundation, The Committee: WG21" />:

> "The current ISO C++ standard is C++23, formally known as ISO International Standard **ISO/IEC 14882:2024(E)** – Programming Language C++."

Readers can visit iso.org/standard/83626.html to verify the publication date.
:::

So how do other languages do it? Each path is quite different. A horizontal comparison helps us understand why C++ is so slow.

First, let's talk about Rust. Rust's philosophy is completely different from C++. C++'s model is to have an ISO standard document written in extreme detail, and then compiler teams like GCC, Clang, and MSVC each implement this document, striving to align with the standard. Rust basically has only one implementation: rustc. More accurately, the test cases in the Rust source code repository are themselves the specification—the "standard" is executable. If you write a piece of code and it passes that set of tests in the Rust source, it is legal Rust code. In the C++ world, we often encounter inconsistencies between "what the standard says" and "what the compiler actually does," while Rust eliminates this problem directly by using test cases.

The direct benefit of this model is speed. The Rust team wants to add a feature, modifies the compiler code, writes tests, submits a PR, someone reviews and passes it, merges it, and everyone can use it in the next release. There is no "three compilers implementing separately, different progress, some support it, some don't" problem. They also have a steering committee and an RFC-like proposal process, but overall it is much lighter than C++. The "single implementation + test as spec" model is indeed the key reason Rust can maintain a six-week release cycle.

Now look at Python. For a long time, Guido van Rossum (Python's father) played the role of "Benevolent Dictator For Life"—the direction of the language, which features to add, which not to add, was his final decision. For example, the controversial walrus operator was pushed during his tenure. But by 2018, Guido stepped down, reflecting a practical problem: when a language community grows to a certain size, it becomes increasingly difficult for one person to make final decisions, and internal divisions in the community grow. Now Python uses a community governance model with a five-person steering committee, and the proposal mechanism is called PEP (Python Enhancement Proposal), which is somewhat similar to C++'s proposal process but obviously less formal. They strive to release a version every year and basically achieve it. In comparison, Python's process is lighter than C++'s, but heavier than Rust's, sitting in the middle.

Finally, let's talk about JavaScript.名义上, JavaScript has a standardization organization behind it called ECMA, and JavaScript is sometimes called ECMAScript in certain contexts—which is its technically formal name. But in actual experience, the evolution of JavaScript is mainly driven by the V8 engine (the engine behind Chrome) and the Node.js ecosystem. The ECMA standard is more of a post-facto recognition of "what everyone is already using." This is almost the reverse of the C++ path of "standard first, implement later."

Putting these together, an interesting spectrum forms. On one end is Rust's "implementation is standard" model with extremely fast iteration; in the middle are Python and JavaScript, which have standardization processes but are relatively lightweight, with actual driving force often coming from the implementation side; on the other end is C++, which writes an extremely detailed specification document first, and then multiple compilers implement it separately. The standard committee itself does not do any implementation. Each model has a price—Rust's price is basically no freedom of compiler choice; C++'s price is that a feature from proposal to actual use may take several years.

The reason C++ standardization is slow is not that the people in the committee aren't working hard, but that the "standard first, multi-party implementation" framework itself determines it can't be fast. As for whether this price is worth it, that is another topic.

---

# Operation of the C++ Standards Committee and Community Participation

Regarding the C++ standardization process, there are many rumors—"C++ standards are controlled by big factories," "proposals are manipulated in the dark by vendors," etc. But actually understanding it, although there is indeed vendor participation (after all, implementing compilers requires a lot of engineering resources, and investing people naturally gives you a voice), there is a formal committee process behind it. Proposals must go through multiple stages such as drafts, voting, and review. It's not about who has the loudest voice. The process is relatively lightweight, unlike some languages with extremely strict governance structures, but lightweight doesn't mean no rules.

We also easily feel "the grass is greener on the other side," seeing Rust's RFC process and thinking it's particularly standardized and transparent, and complaining why C++ doesn't learn from it. But looking back, C++'s model exchanges for long-term vitality—this language has walked from the 1980s to today, survived countless waves of technology, and is still alive, and living well. Every governance model has its trade-offs.

## Those Investing Behind the Scenes

The commitment of standards committee members is often underestimated. Many people participating in proposals invest a huge amount of their personal time—not work time, but personal time—into making this language better. Writing proposals, responding to review comments, discussing details repeatedly in mailing lists, flying around the world to attend face-to-face meetings—most of these have no extra pay. CppCon was held in Hawaii, and someone came back saying they spent the whole time in the hotel room going over proposals. There are also companies that sponsor engineers to participate in standardization, and families who support their members to attend meetings—these support systems are invisible, but without them, the entire ecosystem wouldn't turn.

## The Value of Offline Meetings

According to the speaker, there are 11 major C++ international conferences in 2025, the most in history. There was a obvious trough during COVID, but recovery was quite fast, and it is still rising—this shows the community is alive. Watching speeches online has its value, but sitting offline with a group of people, chatting during tea break about "how is range-v3 working in your project," "have you hit that MSVC pit," this information density and connection is something screens can't give.

If you are still hesitating whether to attend an offline C++ conference or meetup, I suggest you try it, even if it's just a local half-hour sharing session.

---

## Actual Forms of Offline Gatherings

The number of global C++ meetups registered on isocpp.org already exceeds one hundred and thirty, which means no matter where you live, there is a high probability of finding one within a hundred miles. Major cities in China basically have them, and second-tier cities are also slowly appearing. If you really can't find one, starting one yourself is completely fine—no formal process is needed. Someone just posted a message in a group saying "I'm bringing my laptop to sit somewhere on Friday night, chatting about C++, come if you want." The first time four people came, later it stabilized at ten or so, once a month, looking at code together, discussing problems.

More formal forms also exist: big companies sponsor venues, invite external speakers to do technical sharing, with slides and Q&A; lightning talk form, where everyone speaks for five to ten minutes about a pitfall experience or a small trick, fast rhythm, high information density. Some companies even have regular technical exchange time internally.

An actual benefit of offline chatting is that the technical solutions discussed and pitfall experiences are often things you can't find online—those things aren't "systematic" enough to write a blog post, but it is precisely this fragmented, frontline experience from real projects that is often most useful.

---

# Online Communities and Resources

Many people spend the early stages of learning C++ tinkering alone. When encountering compilation errors, they search themselves; if they can't find it, they change the writing method to bypass it. After continuing this state for a while, they often find the bottleneck is not in the level of effort, but in whether they have found the right circle.

## Online Communities

The atmosphere of online communities is often much better than imagined. The C++ Slack (operated by C++ Alliance) has very subdivided channels, and you can join different channels according to your interests. There are more choices on Discord, the Compiler Explorer Discord and servers specifically discussing C++ standard proposals are active discussion places. Newbies and experts communicate in the same space—this is real in the C++ community. Someone who has learned for two months asks a pointer question in the Slack `C++-general` channel, and several people explain patiently below; ISO committee members discuss proposal details with people in Discord.

The actual advice is: don't ask questions as soon as you enter. Spend a few days lurking first, see how others ask and answer questions, and familiarize yourself with the rhythm of the community. You will learn a lot during the lurking process.

## cppreference—Community-Driven Reference Documentation

cppreference<RefLink :id="4" preview="cppreference.com, C++ Reference" /> is a community-driven, community-operated reference website. Every page and every example code on it is actually maintained by someone. It is not official documentation sponsored by some big company, but a group of volunteers doing it. Normally it can be modified and supplemented by community members, which is also the reason it can maintain high quality—it is not one person writing, but countless people maintaining it together. Every time you look up a standard library component, take a look at the comments and discussions at the bottom of the page, and you can often find some very valuable information, such as known issues of a function on a specific compiler.

## Code Sharing Platforms

Besides real-time chat communities, code sharing platforms like Compiler Explorer<RefLink :id="7" preview="Compiler Explorer, godbolt.org" /> are extremely important in technical exchange. Put the code in, generate a link, and drop it anywhere—Discord, Slack, forums, or even directly to colleagues. Compared to directly pasting a large piece of code text, a Compiler Explorer link lets others click to see directly, modify directly, and run directly. The efficiency is completely different.

When debugging problems, first put the minimal reproduction code on Compiler Explorer, confirm it can be reproduced on multiple compilers, and then go to the community to ask—the benefit of this is that others don't need to set up an environment to help you troubleshoot, they can directly click the link to see what you see.

## Community is the Core of the C++ Ecosystem

The reason C++ is fascinating is not only because the language itself is powerful, but also because of the people behind it. Those who silently submit patches in open source projects, those who spend their own time maintaining cppreference, those who self-fund to organize offline gatherings, those who help newbies debug code at 3 AM in Discord—it is these people who constitute the C++ ecosystem. Soaking in the community, you see not only the answers to questions, but also how others think about problems, ideas for solving problems, and even attitudes towards technology.

---

# Participating in the C++ Community—Contributions Are Not Just One Form

Regarding "participating in the open source community," many people have a narrow understanding—thinking it is something only qualified people can do, something only experts with names in the committee or authors of famous libraries are worthy of talking about. But in reality, the ways to participate are far more diverse than imagined.

## "Contribution" Is Broader Than We Imagine

Contributing to the C++ community doesn't necessarily mean writing a widely used library or submitting a proposal to the standards committee that gets adopted. The participation methods mentioned in the speech are many things you can do now: if your city doesn't have a C++ gathering, start one yourself—you don't need to be an expert, just someone willing to bring people together to chat about C++; attend a conference, even if just to listen and meet a few other people using C++, this itself is already participating in the community; write an article about the pits you stepped into so that later people take fewer detours, this is also a contribution.

## About Standing on Stage

There is a very real description in the speech—standing on the speaking stage, looking back at countless faces staring at you, thinking "why am I doing this again." Doing technical sharing doesn't need to be perfect, just speak about what you truly understand, speak about the pits you stepped in, this is already valuable enough. If you have the opportunity to share, even if you are nervous, it's worth trying once.

## About Participating in the C++ Committee

The C++ committee is hiring. The work of the committee requires people at all levels to participate—not just experts in language design, but also feedback from actual users, people to test proposals, write use cases, and report problems. You don't need to be Bjarne Stroustrup to get in, you just need passion and willingness to invest time.

## A Final Small Interlude

There is a very real detail in the Q&A session: the speaker described Barry Revzin as the person responsible for Ranges, but was corrected on the spot—Barry Revzin has done a lot of work recently on the application layer of C++26 Reflection (he gave a speech "Practical Reflection With C++26" at CppCon), while the main author of Ranges is Eric Niebler (the speaker misspoke it as Eric Kneedler). However, strictly speaking, the main drivers of the Reflection proposal are Daveed Vandevoorde and Herb Sutter, etc., and Revzin is more on the application and teaching level. This kind of "mixing up people and responsible areas" is very common. The C++ standards committee involves too many people and sub-working groups, and even frequent participants may not be able to figure it all out. The speaker self-deprecatingly said "I am just too terrible," and this sense of reality actually makes people feel this community is very down-to-earth.

## The Threshold for Participating in the Community

The C++ community is not some closed circle; it is composed of everyone currently using C++. The simplest contribution might just be sharing what you learned today with a colleague next to you, or answering a newbie's question in the community. Don't wait until you are "strong enough" to participate—because by then you may have forgotten the confusion of the newbie stage, and it is precisely those confusions that are the most valuable sharing content.

---

# The "Never Execute" Instruction in ARM32 Condition Codes—Orthogonal Design and Its Demise

This Q&A session involves an interesting architectural design question. In the ARM32 instruction set, every instruction has a four-bit condition code field in front. You can write `ADDNE` to mean "add if not equal," `MOVEQ` to mean "move if equal," without writing separate branch instructions, so code density is very high. Among the condition codes, there is one called `AL` (Always, always execute), corresponding to `0b1110`; but there is another condition code, where all four bits are 1, that is `0b1111`, called `NV` (Never), meaning "Never." An instruction that "never executes"—writing it in is just taking up space for nothing, right?

::: warning Important Correction
The NV condition code only exists in **ARMv4 and earlier versions**. Starting from ARMv5, NV was officially deprecated, and the `0b1111` encoding was reassigned for unconditional instruction extension. On ARMv7-A, using the condition code `NV` results in **UNPREDICTABLE** behavior, no longer guaranteeing "never execute." The verification experiments later in this article need to target the ARMv4 architecture to get the expected results. ARM official documentation text:

> "Every conditional instruction contains a 4-bit condition code field, the cond field, in bits 31 to 28. This field contains one of the values **0b0000 – 0b1110**."
>
> — ARM Architecture Reference Manual ARMv7-A/R, Section "The condition code field"<RefLink :id="5" preview="Arm Developer, Condition Codes: Conditional Execution" />

Actual verification result (arm-none-linux-gnueabihf-gcc 15.2 + qemu-arm-static):

```text
$ ./a.out
result is 0
```

Verification code in repository: [05-01-arm32-nv-condition.c](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/blob/main/code/volumn_codes/vol10/cppcon/2025/02-some-assembly-required/05-01-arm32-nv-condition.c).
:::

## Orthogonality—The Design Philosophy of ARM32

The key lies in the design philosophy of ARM32: **extreme orthogonality**<RefLink :id="5" preview="Arm Developer, Condition Codes: Conditional Execution" />. Simply put, orthogonality means "choices in each dimension are independent and can be freely combined." In ARM32, the dimension of condition codes is designed very thoroughly—every condition has its logical opposite. Equal (EQ) is opposite to Not Equal (NE), Greater or Equal (GE) is opposite to Less Than (LT), Unsigned Higher (HI) is opposite to Unsigned Lower or Same (LS)... and so on.

So what is the logical opposite of "Always Execute" (AL)? Naturally, it is "Never Execute" (NV).

Because four bits can represent 16 states, the designers of the condition codes filled all 16 states, each with corresponding semantics. This isn't "intentionally leaving a useless one," but the inevitable result of pushing orthogonality to the extreme—it's impossible to keep only 15 and leave one empty, that wouldn't be orthogonal. The price is: in the entire instruction encoding space of ARM32, a full sixteenth of the encodings correspond to instructions that "do nothing." This is a design trade-off—using a little space waste in exchange for conceptual perfect symmetry of the instruction set.

This design was indeed the case in the original ARM (ARMv1 to ARMv4). But subsequent versions of ARM proved that "orthogonal to the extreme" also has a price.

## Hands-on Verification: Writing a "Never Execute" Instruction (ARMv4)

We can verify this thing ourselves<RefLink :id="6" preview="Arm Developer, Condition Codes: Condition Flags and Codes" />. Because the NV condition code is only valid in ARMv4 and earlier, we need to explicitly specify the architecture version.

::: details Why can't we use ARMv7?
The valid condition code range for ARMv7-A is only `0b0000`–`0b1110`. The encoding `0b1111` was reassigned in ARMv5+—it is either interpreted as a completely different instruction (using condition code bits to extend opcode space) or produces UNPREDICTABLE behavior. Using `NV` on ARMv7 **does not guarantee** the result is "never execute." Verification code is in the repository ([05-01-arm32-nv-condition.c](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/blob/main/code/volumn_codes/vol10/cppcon/2025/02-some-assembly-required/05-01-arm32-nv-condition.c)), readers can compare tests on ARMv4 and ARMv7 targets themselves.
:::

The environment is Arch Linux WSL, using the `arm-none-linux-gnueabihf` cross-compilation toolchain (Arm GNU Toolchain 15.2). Note that when compiling, you need to use `-march=armv4` to ensure the semantics of the NV condition code:

First write a simple C file:

```c
// test.c
int result = 0;
int main() {
    result = 1;
    return 0;
}
```

Compile it to assembly to see what a normal `MOV` looks like (note here we use `-O2` to prevent optimization from removing the assignment):

```bash
arm-none-linux-gnueabihf-gcc -O2 -march=armv4 -S test.c -o test.s
```

Now we manually construct a "never execute" `MOV`. In the ARM32 `MOV` instruction encoding format, the high four bits are the condition code. The machine code for a normal `MOV R0, #1` can be checked with `objdump`:

```bash
$ arm-none-linux-gnueabihf-objdump -d test.s
...
    e3a00001:    mov   r0, #1
...
```

See `e3a00001`? The high four bits are `e`, which is binary `1110`, corresponding to the condition code `AL` (Always). Now change the high four bits from `e` to `f`, that is, from `1110` to `1111`. On ARMv4, this is a "never execute" `MOV`—it is decoded, the CPU recognizes it as a MOV instruction, but because the condition code is NV, it never actually executes.

::: warning Reminder again
This instruction only behaves as "never execute" on ARMv4 and earlier. If executing `0xf3a00001` on ARMv5+ (including ARMv7-A), the behavior is UNPREDICTABLE.
:::

Use `asm` to directly stuff the machine code in to verify:

```c
// nv_test.c
#include <stdio.h>

int result = 0;

int main() {
    // Normal MOV
    // asm volatile("mov r0, #1" : "=r"(result));

    // NV MOV (0xe3a00001 -> 0xf3a00001)
    asm volatile(".inst 0xf3a00001" : "=r"(result));

    printf("result is %d\n", result);
    return 0;
}
```

Compile and run (note `-march=armv4`):

```bash
$ arm-none-linux-gnueabihf-gcc -march=armv4 nv_test.c -o nv_test
$ qemu-arm-static ./nv_test
result is 0
```

`result` is still 0—that `MOV` was fully decoded, but the CPU looked at the condition code `NV` and skipped it directly, doing nothing. `result` maintained its previous value 0.

There is a pitfall here: if the output constraint `"=r"(result)` was not added, the compiler might optimize `result` away directly, and no matter how you run it, it's 0, easily mistaking it for writing the wrong machine code.

## By the Way: The TEQ Instruction

The Q&A also mentioned an instruction called `TEQ`. `TEQ` itself is an abbreviation for "Test Equivalence," performing an exclusive-OR operation and setting flags, used to compare whether two values are equal (without changing register values, only changing flags). `TEQP` with the `P` suffix is an instruction in old ARM (before ARMv4) used to directly operate the Processor Status Register (PSR)—in modern ARM it has been replaced by `MSR`/`MRS` instructions.

## Summary

That sixteenth of "no operation" instruction encoding in ARM32 (ARMv4 and earlier) is not a bug, not a legacy issue, but an inevitable byproduct of extreme orthogonal design. The designers chose conceptual perfect symmetry, and the price was wasting some encoding space.

But ARM's own subsequent evolution also explains everything: ARMv5 deprecated the NV condition code and reclaimed the `0b1111` encoding space; ARM64 (AArch64) completely cut the condition code field. "Orthogonal to the extreme" is beautiful conceptually, but ARM's practice proves that in actual evolution, encoding space and instruction set simplicity ultimately defeat conceptual perfect symmetry. After understanding this design history, the experience of reading assembly manuals will be completely different.

---

# Should I Learn x86 or RISC-V Assembly

When tinkering on Compiler Explorer, I often struggle with a question: x86 assembly looks like heavenly script—`mov eax, dword ptr [rbx + 0x10]`, register names are long and irregular; switching to RISC-V looks much more understandable, registers are just `x0` to `x31`, and the instruction format is much more regular. But how big is the gap between looking at RISC-V assembly and the x86 code actually running at work? Will reading it for a long time be a waste?

## Conclusion: Which Architecture Depends on the Optimization Level

There is no one-size-fits-all answer here; the key factor is the optimization level selected in Compiler Explorer. If you use `-O0` (no optimization), it makes little difference whether you look at x86 or RISC-V. What the compiler does at `-O0` is very "generic"—it faithfully translates C++ statements line-by-line into machine instructions, pushing to the stack when needed, storing to memory when required. Regardless of the architecture, the routine is the same. The knowledge gained at this level about "how the compiler transforms code" is effectively interchangeable across architectures.

Let's verify this with a simple function:

```cpp
int add_and_double(int a, int b) {
    int sum = a + b;
    return sum * 2;
}
```

Under `-O0`, although the x86 and RISC-V outputs use different instructions, the "flavor" is identical—both store parameters to the stack, load them back for addition, store the result back to the stack, and finally load it again for multiplication. The compiler is very "honest" without optimizations; it doesn't do anything clever. This understanding holds true regardless of the architecture.

## Things Change at -O2 and Above

When the optimization level is cranked up to `-O2` or even `-O3`, systematic differences between architectures begin to emerge. The assembly you see is no longer purely a reflection of "generic compiler optimization strategies"; it is heavily mixed with "specialized optimizations for that specific architecture's instruction set."

A classic example is `popcount`, which counts the number of 1 bits in an integer:

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

With this code at `-O3` on x86 in Compiler Explorer, the compiler replaces the whole function with a single `popcnt` instruction. The loop vanishes; the function body is just one instruction. Switch to RISC-V, however, and the loop remains. The base RISC-V instruction set lacks the `popcnt` instruction (though some extensions include it), so the compiler cannot make this substitution. It must rely on loops or lookup tables. The same C++ code, same `-O3`, yields completely different assembly on the two architectures.

If you learn assembly on RISC-V, you might conclude "compilers can't auto-vectorize popcount patterns"; on x86, you'd reach the exact opposite conclusion. Who is right? Both, and neither—because this isn't a difference in compiler capability, but a difference in the target instruction set.

## Practical Strategy

To summarize the strategy: if your goal is to understand "high-level compiler optimization decisions"—how inlining works, constant propagation, dead code elimination—then any architecture will do. These are indeed cross-architecture concepts. When a compiler decides "should I inline this function?", it considers high-level factors like function size, call frequency, and side effects, which have little to do with the underlying CPU.

However, if your goal is to understand "what the final generated instructions actually look like," it is best to look at the architecture you use in real work. At `-O2` and above, every instruction you see might be an "architectural shortcut" that simply doesn't exist on another platform.

## Compiler Explorer's AI Features

Compiler Explorer has launched AI-assisted assembly explanation, with mixed results. For simple instruction sequences—basic calling conventions, stack frame layouts—the AI explains things quite clearly. But when it encounters architecture-specific optimizations, like using `cmov` on x86 to avoid branch misprediction, the AI sometimes gives generic explanations without highlighting "which architectural feature is being optimized." You can use it as a beginner's crutch, but don't treat it as an authoritative answer.

## Summary

People often say "to learn assembly, pick the cleanest architecture," but if it's so clean that it detaches from reality, it creates misconceptions. You might as well face real x86 assembly from the start. Although the learning curve is steeper, everything you learn is directly applicable. RISC-V is excellent for "verifying generic optimization logic"—run the same code on both architectures; if an optimization appears on both, it's likely a generic compiler strategy; if it appears on only one, it's likely an architectural instruction substitution. This comparative method is much clearer than looking at output from a single architecture in isolation.

---

# Rethinking "Hand-Written Assembly"—When to Touch It and When Not To

There are two common extreme attitudes regarding assembly: one is "the compiler handles it, don't worry about assembly," and the other is "if you don't write inline assembly on critical paths, you don't truly know C++." Both are wrong. The speaker put it well: he now writes assembly mainly for vintage computers he likes, because those architectures are manageable enough to fit entirely in his brain. The value of assembly isn't about being "smarter than the compiler," but about "fully understanding what the machine is doing."

## Why Modern x86-64 Assembly is Hard to "Fit in Your Brain"

Compare instruction sets from different eras. Today, x86-64 has dozens of encoding variants just for the `mov` instruction—`movsx` sign-extending to 64 bits, `movsxd` also sign-extending, `movzx`, `movabs`, `cmov` conditional moves... Add to that AVX-512's EVEX encoding prefixes, mask registers, and broadcast mechanisms. For a normal person to "fit" the complete x86-64 instruction set into their brain is an impossible task.

The speaker mentioned the Hitachi SH4 instruction set, saying that might be the limit of what a normal person can handle. SH4 is a late 1990s RISC processor with 16-bit fixed-length instruction encoding and very clean addressing modes. The comparison explains why the assembly experience on old hardware is so different—it's a "human-comprehensible" instruction set, whereas x86-64, after forty years of backward compatibility accumulation, has become a beast no one fully understands. It's not that "assembly" itself is hard, but that assembly for the specific x86-64 platform is hard.

## When Modern C++ Developers Should Touch Assembly

The speaker shared a real case: a company he visited had a compiler constantly spilling registers in an absolute hot loop. No amount of tuning optimization options fixed it. Finally, the team hand-wrote the entire loop in assembly, maintaining a C++ version and an assembly version for cross-validation. This sounds painful, but it's a very pragmatic engineering decision. Inline assembly syntax isn't unified between GCC and Clang, and it's hard to precisely control register allocation around the compiler—sometimes you just want "I decide register usage here," and a standalone assembly file is the cleanest way.

However, hand-written assembly has high maintenance costs. In an agile development environment, changing requirements might mean a complete rewrite of the hand-written assembly. This pain also occurs when writing SIMD intrinsics—you carefully design a loop using 4 `__m256` registers, requirements change, a field is added to the data structure, and the register allocation collapses.

So the judgment criteria are clear: unless you encounter an extreme hot spot the compiler can't handle, profiling confirms the bottleneck is register spilling or instruction sequencing, and the hot spot is stable and won't change frequently—only when all three conditions are met is hand-written assembly worth it. Otherwise, write C++ honestly and let the compiler do the work.

## The Real Value of Learning Assembly—Understanding Compiler Output

The biggest value of learning assembly isn't writing it yourself, but being able to understand what the compiler output. Here is a concrete example:

```cpp
// 统计 buf 中字符 ch 出现的次数（已知长度）
size_t count_char(const char* buf, size_t len, char ch) {
    size_t count = 0;
    for (size_t i = 0; i < len; i++) {
        if (buf[i] == ch) count++;
    }
    return count;
}
```

This function is as simple as it gets. But throw it into Godbolt with `-O3`, and the compiler (GCC 16) auto-vectorizes it, using SSE instructions to compare 16 bytes at a time. If you don't understand assembly, you won't know the compiler did this, and you might try to hand-write SIMD optimizations yourself.

::: warning Correction from Original Text
The original example used a `strlen`-style null-terminated loop. In reality, **GCC and Clang will not auto-vectorize this pattern at `-O2` or `-O3`**—because the null character's position is determined at runtime, and the compiler cannot safely read 16 bytes at once (it might read past the null into unmapped memory). Only the known-length version (`std::char_traits<char>::length`) gets vectorized.

Readers can verify this themselves (Environment: GCC 16.1.1, x86-64):

```bash
# while(*str) 版本 — 不会被向量化
cat > /tmp/test.cpp << 'EOF'
#include <cstddef>
__attribute__((noinline))
size_t f(const char* s, char c) {
    size_t n = 0; while (*s) { if (*s == c) ++n; ++s; } return n;
}
EOF
g++ -O3 -march=x86-64-v2 -S /tmp/test.cpp -o /tmp/test.s
grep pcmpeqb /tmp/test.s   # 无输出 = 没有向量化
```

Verification code in repo: [05-04-count-char-vec.cpp](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/blob/main/code/volumn_codes/vol10/cppcon/2025/02-some-assembly-required/05-04-count-char-vec.cpp).
:::

The actual GCC output (simplified core loop):

```asm
# GCC 16 -O3 -march=x86-64-v2 输出的核心循环（简化）
count_char:
    movd    %r8d, %xmm4            # ch 放入 XMM4 最低字节
    pxor    %xmm2, %xmm2           # XMM2 = 全零（用于 pshufb 广播掩码）
    pshufb  %xmm2, %xmm4           # 广播 ch 到 16 个字节
                                    # pshufb 零掩码：每个字节取 src[0]，即广播最低字节
    pxor    %xmm2, %xmm2           # count 累加器清零
.L4:
    movdqu  (%rax), %xmm0          # 加载 16 字节
    pcmpeqb %xmm4, %xmm0           # 逐字节比较，匹配的位置 0xFF，不匹配 0x00
    pmovsxbw %xmm0, %xmm6          # 符号扩展低 8 字节 → 8 个 word
    pmovsxwd %xmm6, %xmm5          # 符号扩展 → 8 个 dword
    pmovsxdq %xmm5, %xmm5          # 符号扩展 → 4 个 qword（每个值为 0 或 -1）
    # ... 将 -1 转为 +1 并累加到 xmm2 ...
    paddq   %xmm1, %xmm2           # 累加到计数器
    cmpq    %rax, %rcx              # 循环是否结束
    jne     .L4
```

::: details Why was the original assembly wrong?
The original text claimed GCC used `punpcklbw` to broadcast a byte—this is wrong. `punpcklbw`'s function is **interleaving/merging** low bytes from two registers (byte → word), not broadcasting. GCC actually uses `vpshufb` (PSHUFB with a zero mask) to broadcast: when the mask is all zeros, every position takes element 0, effectively copying the lowest byte to all 16 positions.

Additionally, the original text claimed counting used `pcmpgtb` + `pand`—GCC actually uses a sign-extension chain (`pmovmskb` → `popcnt` or similar logic), turning match results (0xFF/-1) into qword values via sign extension, then accumulating. This strategy is better in some scenarios than `pcmpgtb`+`pand` (especially when further SIMD processing of the count is needed).
:::

Seeing `vpcmpeqb` and the sign-extension chain allows you to judge: the compiler did a great job here, no manual optimization needed. But if in another more complex scenario you find the compiler didn't auto-vectorize, you can locate "where it got stuck" by looking at the assembly output. The real value of learning assembly is gaining the ability to "audit the compiler." Not to replace the compiler, but to understand its output.

## Practice Method: Read More Assembly Than You Write

Every time you write a function that might have performance issues, look at the compiler output first—throw it on Godbolt, turn on `-O2` or `-O3`, and check a few key indicators: are there unnecessary memory accesses (e.g., a variable expected to be in a register is repeatedly loaded from the stack, perhaps because of `volatile` or aliasing issues); was the loop vectorized (if the loop body is simple but wasn't vectorized, check for data dependencies or branches); was the function inlined (if not, is it too big or does it use something that blocks inlining).

All these judgments rely on the premise of "understanding assembly." You don't need to hand-write perfect assembly from scratch, just understand basic instructions like `mov`, `cmp`, `jmp`, `call`, `ret`, `test`, `lea`, `imul`, and see the data flow.

## The Value of Writing Assembly on Simple Architectures

The speaker said he doesn't miss the toil of hand-writing assembly, but he does miss the intellectual challenge. Truly writing assembly from scratch on a simple architecture helps build a "machine mindset"—when writing C++, you unconsciously have a model in your head: what instructions does this line generate? How is this object laid out in memory? How many levels of indirection does this virtual function call involve? This intuition plays a huge role in performance optimization.

## Summary

Learning assembly isn't a tool to replace the compiler, nor is it an insurmountable black magic. It's an ability to understand what the machine is doing. The best way to gain this ability might not be grinding on x86-64, but finding a simple, human-comprehensible architecture and actually writing some. The principle in daily work is simple: read assembly often; write assembly rarely. Unless you really encounter a scenario the compiler can't handle, and you're sure hand-writing brings significant gains, and the code is stable enough not to change frequently. Otherwise, let the compiler work, and you audit it.

---

# How to Attract Newcomers to C++

At CppCon, they are seriously thinking about a question: when a CS graduate has never written C++, how do we bring them in? The speaker mentioned an observation: the devices in his home when he was young, the only thing you could do after opening them was type something in, and then figuring out what was going on by osmosis. When there weren't many choices, you dug deeper. Now there are too many choices; a college student can complete four years of Computer Science, submit homework in Python, do their capstone in React, and never need to know what a stack is, what a heap is, or what undefined behavior is.

But what the speaker said later is worth noting: he met some new graduates at Google who, despite growing up in "high-level" language environments, were exploring low-level hardware on their own. This shows that curiosity about the low level isn't specific to a certain era; it's always there, just triggered differently.

Bringing newcomers into C++ probably shouldn't start with preaching like "you should learn C++ because it's important," but rather finding that trigger point in everyone—maybe one day they hit a performance problem Python can't solve, or suddenly want to understand "how does the program actually run on the hardware." That is the best moment. We "latecomers" actually have an advantage: we know exactly where it hurts most when falling from high-level languages to C++. This experience of "from pain to gain" is exactly what can be shared with the next newcomer.

---

# The Preprocessor's Gradual Exit—C++'s Path of Progressive Replacement

In the Q&A, Matt Godbolt was asked "if you could remove one feature, what would you kill," and his answer was the preprocessor. This isn't a whim—since C++11, the language has been doing the same thing: reimplementing "preprocessor-era" stuff with "real C++."

## Typical Problems with the Preprocessor

In early C++ projects, screens full of `#define`, `#ifdef`, and nested conditional compilation were common. Take logging macros as an example:

```cpp
// 我 2022 年的写法，现在看着想打自己
#define LOG(level, msg) \
    do { \
        if (level >= g_log_level) { \
            printf("[%s:%d] %s\n", __FILE__, __LINE__, msg); \
        } \
    } while(0)

#define LOG_DEBUG(msg) LOG(0, msg)
#define LOG_INFO(msg)  LOG(1, msg)
#define LOG_ERROR(msg) LOG(2, msg)
```

The problem with this is: macros are text replacement; they don't understand C++'s type system. Pass in an expression with a comma, like `add(1, 2)`, and the preprocessor treats it as two arguments, causing a compilation error. And the error message is ridiculous because the location reported is in the expanded code, completely misaligned with the macro you wrote.

## Modern Alternatives: Replacing Text Replacement with C++

Using `constexpr`, `inline` functions, and templates to replace macros yields a completely different result:

```cpp
// log.hpp
#pragma once
#include <iostream>
#include <source_location>

enum class LogLevel { Debug = 0, Info = 1, Error = 2 };

inline LogLevel g_log_level = LogLevel::Info;

// 用 constexpr 函数替代宏，类型安全，支持任意参数
template <typename... Args>
void log(LogLevel level, const std::format_string<Args...> fmt, Args&&... args,
         const std::source_location& loc = std::source_location::current())
{
    if (static_cast<int>(level) >= static_cast<int>(g_log_level)) {
        std::cout << std::format("[{}:{}] {}\n",
                                 loc.file_name(), loc.line(),
                                 std::format(fmt, std::forward<Args>(args)...));
    }
}

// 用 inline constexpr 变量替代宏常量
inline constexpr LogLevel log_debug = LogLevel::Debug;
inline constexpr LogLevel log_info  = LogLevel::Info;
inline constexpr LogLevel log_error = LogLevel::Error;
```

```cpp
// main.cpp
#include "log.hpp"

int main() {
    g_log_level = LogLevel::Debug;

    // 这样调用，带逗号的表达式完全没问题
    log(log_debug, "value is {}", std::max(1, 2));
    log(log_info, "program started");
    log(log_error, "something went wrong: code={}", 404);
}
```

You might ask, what about `__FILE__` and `__LINE__`? This is exactly what C++20's `std::source_location` is for—it's a "real C++ feature," not preprocessor black magic. The compiler understands it correctly, and you can get accurate information when debugging.

## Replacing `#include`: Modules

The preprocessor's most deep-rooted presence comes from `#include`. C++20 introduced modules<RefLink :id="8" preview="cppreference.com, Modules (since C++20)" />, shaking the preprocessor's status from its roots. Look at a simple example:

```cpp
// math_utils.cppm —— 这是一个模块接口文件
export module math_utils;

export int square(int x) {
    return x * x;
}

export double pi() {
    return 3.14159265358979;
}
```

```cpp
// main.cpp
import math_utils;
#include <iostream>

int main() {
    std::cout << "5^2 = " << square(5) << "\n";
    std::cout << "pi = " << pi() << "\n";
}
```

When compiling, note that module support varies by compiler. I used GCC 14; the compile command looks like this:

```bash
g++-14 -std=c++20 -fmodules-ts math_utils.cppm main.cpp -o demo
```

Let's run it:

```text
5^2 = 25
pi = 3.14159
```

The key difference is: the `math` module is compiled only once, no matter how many times it's `import`ed. Traditional `#include` copies and pastes the header content into every translation unit, which is why big projects compile slowly—the same `iostream` is processed hundreds of times.

However, modules still have plenty of pitfalls, mainly interoperability between modules and traditional headers. If you `import` a module, but that module internally `#include`s a traditional header, and another place `#include`s the same header, some compilers throw weird errors. So the advice is: either go all-in on modules or don't use them; don't mix them, at least until the toolchain matures.

## What About Conditional Compilation?

There is no perfect replacement for `#if` yet. C++20's `if consteval` and `requires` can solve part of the problem, provided the condition is determinable at compile time.

::: warning Correction from Original Text
The original example used `std::endian` to judge byte order, but `std::endian::native` **is not allowed in constant expression evaluation** in the C++ standard ([expr.const]<RefLink :id="12" preview="cppreference.com, Constant expressions" />), so it cannot be used in a `consteval` function. The actual error message from GCC 16.1.1 is as follows:

```text
/tmp/test.cpp:4:12: warning: 'reinterpret_cast' is not a constant expression [-Winvalid-constexpr]
    4 |     return reinterpret_cast<const char*>(&test)[0] == 1;
      |            ^~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/tmp/test.cpp:7:34: error: call to consteval function 'is_little_endian()' is not a constant expression
```

Verification code in repo: [05-02-consteval-endian-broken.cpp](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/blob/main/code/volumn_codes/vol10/cppcon/2025/02-some-assembly-required/05-02-consteval-endian-broken.cpp) (Compilation failure) and [05-03-consteval-endian-fixed.cpp](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/blob/main/code/volumn_codes/vol10/cppcon/2025/02-some-assembly-required/05-03-consteval-endian-fixed.cpp) (Fixed version, compiles). Readers can verify the compilation failure themselves with `-std=c++23 -Wall -Werror`.
:::

After correction, there are two ways to judge byte order at compile time:

```cpp
// 方法 1：编译器内置宏（推荐，简洁可靠）
consteval bool is_little_endian() {
    return __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__;
}

// 方法 2：使用 std::bit_cast（C++20，可在 constexpr/consteval 中使用）
#include <array>
#include <bit>
consteval bool is_little_endian_bitcast() {
    // std::bit_cast 可以在常量表达式中使用，而 reinterpret_cast 不行
    constexpr auto bytes = std::bit_cast<std::array<unsigned char, sizeof(int)>>(1);
    return bytes[0] == 1;
}

void write_bytes(int value) {
    if constexpr (is_little_endian()) {
        // 小端序的处理逻辑
        std::cout << "little endian path\n";
    } else {
        // 大端序的处理逻辑
        std::cout << "big endian path\n";
    }
}
```

But if it's true platform detection (Windows vs Linux), we still have to rely on preprocessor-defined macros. This is also why Matt said "make the preprocessor increasingly unimportant" rather than "delete it tomorrow"—it's a gradual process.

## Overall Trend

Since C++11, the language has been doing the same thing—reimplementing "preprocessor-era" stuff with "real C++." `constexpr` replaces macro constants, `inline` functions replace macro functions, templates replace type-agnostic macros, `span` replaces pointer/length pairs, `std::array` replaces C arrays, `string_view` replaces `char*`, `std::optional` replaces some `#ifdef`s... Each step is subtle, but together they form a clear direction. The preprocessor doesn't understand C++; it only knows the clipboard, which leads to so many ridiculous errors. Templates, `constexpr`, and concepts are part of C++; the compiler can truly understand what you are doing.

The preprocessor won't disappear tomorrow, but as C++ developers, we can actively reduce our dependence on it. Every time you want to write a `#define`, stop and think: is there a type-safe C++ alternative? Most of the time, the answer is yes.

---

# How to Judge if Bizarre Assembly is Optimization or UB

When looking at the assembly for your C++ code on Compiler Explorer, you often see instruction sequences you can't understand at all—is the compiler so smart it's beyond you, or did you write some UB causing the compiler to legally "go crazy"? For a long time, this was a hard question to answer.

## The Most Direct Signal: Trap Instructions

One particularly obvious red flag is seeing the `ud2` instruction. Its full name is Undefined Instruction; the result of execution is only one: the CPU throws an illegal instruction exception, and the program crashes on the spot. The compiler puts this instruction here to mean: "under normal circumstances, execution cannot reach here. If it does, let the program die."

The most typical scenario is a switch statement:

```cpp
#include <cstdint>

int32_t classify(int32_t value) {
    switch (value) {
        case 0:  return 1;
        case 1:  return 2;
        case 10: return 3;
        case 11: return 4;
    }
    // 我当时觉得：如果不是上面这四个值，就返回 0 吧
    return 0;
}
```

This code looks logically complete; every branch has a return, and there's even a fallback `return 0`. But turn optimization up to `-O2` and look at the assembly generated by GCC or Clang, and you might see a `ud2` after the switch jump table. When doing value range analysis, if the compiler discovers the caller's passed value is already constrained to a finite set, it can infer that the final `return 0` is never executed. At this point, it won't generate normal return code for `return 0`, but instead puts a `ud2` as a "dead end" marker. So if you see `ud2` in the assembly and are sure a logical path exists in your code to get there, you can basically conclude: you and the compiler have a disagreement on the program's behavior, and this often means UB.

## Most of the Time It's Less Obvious

Not all UB appears in the form of `ud2`. Often, when the compiler encounters UB, it proceeds with aggressive optimizations based on the assumption "this situation won't happen," resulting in a generated instruction sequence that looks completely nonsensical.

```cpp
#include <cstddef>

int sum_array(const int* arr, size_t n) {
    int sum = 0;
    for (size_t i = 0; i <= n; ++i) {  // 注意这里是 <=
        sum += arr[i];
    }
    return sum;
}
```

In this loop, `data[16]` means accessing `data[16]`, which is one element out of bounds. Without optimizations, this code might "seem to work" because the out-of-bounds memory location happens to have some value, and the program doesn't crash immediately. But once `-O2` is enabled, the compiler might reshape the entire loop logic based on the assumption "array out-of-bounds is UB." Looking at the assembly then, you won't see `ud2`, but a sequence of instructions that "seem to be working but the result is definitely wrong." You can't determine "this is because of UB" from the assembly itself; you can only rely on experience to suspect it.

## Troubleshooting Strategy

First, check for trap instructions. If you see `ud2` or an equivalent trap instruction on the target architecture (like `brk` on ARM), lock it in immediately: the compiler is saying there is an unreachable path here. Investigate why the compiler thinks it's unreachable.

Second, if there are no trap instructions, but the assembly looks wrong—e.g., the loop count is obviously lower, certain variables have completely disappeared, or calculation logic you never wrote appears—start suspecting UB. Recompile with `-fsanitize=undefined`<RefLink :id="9" preview="GCC Documentation, Program Instrumentation Options" /> and see if it reports errors at runtime. This tool is very effective in catching UB like signed integer overflow, null pointer dereference, and array out-of-bounds.

Third, if the sanitizer doesn't report an error, it might really be a legal optimization the compiler made that you didn't expect. Check the compiler's control flow graph; you can open this view in Compiler Explorer to see if the jumps between basic blocks match your expectations.

Finally, if all else fails, throw that unintelligible instruction into a search engine and see if anyone else has encountered a similar situation.

## No Silver Bullet

There is no silver bullet that lets you look at assembly and know "is this optimization or UB." It's more a process of accumulating experience; the more UB patterns you've seen, the more accurate your intuition becomes when reading assembly. Sanitizers and trap instructions are the two most reliable anchors; the rest relies on checking, looking at control flow graphs, and repeatedly comparing outputs at different optimization levels to reason through it. Reading assembly is more of a debugging method; you don't need to know every instruction, but you need the ability to identify "something is wrong here," and then have a systematic way to narrow it down.

---

# The Blurry Boundary Between Compiler "Smartness" and UB

There isn't always a clear line between UB and non-UB. When you crank the optimization level to `-O2` or `-O3`, it's often hard to tell if the compiler is "smartly optimizing for you" or "legimately breaking your code."

## "Runs Right Means No UB"—A Common Misconception

A common naive understanding of UB is: as long as the program produces the correct result, it's fine. Logically, "UB is UB; regardless of whether it runs right now, the compiler has the right to do anything"—but what truly "enlightens" people isn't usually hearing reasoning, but getting burned once.

A typical scenario: allocate a block of memory with `malloc`, initialize it with an `int*` pointer, then read/write with a `char*` pointer. It runs fine in Debug mode, but output gets garbled in Release mode. This is a strict aliasing issue—the compiler at `-O2` sees `char*` reading that memory and thinks "this pointer has no relation to the previous `int*`," so it optimizes away the previously written values. Did the compiler do wrong? No, it's completely legal. This is where the blurry line comes from.

## Why This Line is Harder to Draw

Modern compiler optimization isn't just "deleting unused variables"; it's based on deep semantic analysis of the program—dead store elimination, pointer analysis based on strict aliasing assumptions, loop optimizations based on signed integer overflow being UB... Every one of these optimizations relies on the premise "the program has no UB." Once code triggers UB, these premise assumptions collapse for the compiler, but it won't tell you; it just keeps pushing forward with its own logic. The result might be exactly right, or completely absurd. Even more torturous is that changing a compiler version, optimization level, or even compilation order might yield different results.

The C++ standard defines many things as UB, essentially to make room for compiler optimization. To enjoy the benefits of optimization, you must bear the risk of UB—this isn't the compiler fighting you; it's the "contract" you signed when choosing C++.

## Coping Strategy: Don't Guess, Use Tools

Since it's hard to judge if it's UB by just looking at code, don't guess. Use tools.

```cmake
# 我的项目里标配的警告选项，GCC/Clang 通用
add_compile_options(
    -Wall -Wextra -Wpedantic
    -Werror          # 警告当错误，强迫自己处理
    -Wconversion     # 隐式类型转换警告，这个抓过我好几次坑
    -Wsign-conversion # 有符号无符号混用警告
)
```

The first habit is to turn compiler warnings to the strictest. `-Werror` is highly recommended; it can turn a warning into an error, forcing you to fix issues like using an `int` to index a `std::vector` when the container size exceeds `int` range.

The second is using Sanitizers. When running tests during development, turn on UBSan and ASan:

```cmake
# 开发模式下的选项
add_compile_options(
    -fsanitize=undefined,address
    -fno-sanitize-recover=all  # 遇到 UB 直接 abort，别继续跑
    -g -O1                     # 注意：Sanitizer 在 -O0 下效果最好，
                               # 但 -O1 更接近真实场景，我选 -O1 做折中
)
add_link_options(-fsanitize=undefined,address)
```

UBSan can detect things like signed integer overflow, null pointer dereference, unaligned memory access, invalid type casts (including strict alias violations), shift amounts out of range, etc. It's hard to cover all these just by looking at code.

The third habit is "dumb" but effective: cross-verification with multiple compilers. Use GCC locally, run Clang in CI, and occasionally compile with MSVC. Different compilers "exploit" UB differently; the same UB might run correctly under GCC but explode under Clang. If the results from three compilers are inconsistent, you can almost be sure there's UB.

## LLM Features on Compiler Explorer

The Q&A also mentioned the LLM features on Compiler Explorer, with mixed experiences. It works well to "explain" existing assembly code—throw in `-O2` generated assembly, ask "how was this loop unrolled," and it gives a pretty accurate answer. But asking it to "generate" assembly from scratch is much riskier, because there are too many details in instruction sets.

A conservative usage: only let the LLM help "read" assembly, not "write" it. And every time you read its explanation, verify it against the instruction manual or actual results on Compiler Explorer. The strategy mentioned by the speaker is also interesting—emphasize in the system prompt "don't say it if you're not sure." This does reduce overconfident wrong outputs, but the cost is it might become more "silent."

## Accept Ambiguity, But Don't Give Up Precision

In the world of C++, "the compiler did it right" and "the code has UB but didn't explode" can look exactly the same on the surface; you can't distinguish them by observing output. Instead of agonizing over "does this count as UB," focus energy on prevention—turn on strict warnings, run Sanitizers, multi-compiler verification. These three axes can block the vast majority of UB issues. As for those truly gray-area situations, if you aren't sure if it's UB, rewrite it to be definitely not UB. Writing a few more lines of code is better than troubleshooting inexplicable optimization problems in the middle of the night.

---

# The Value of Hand-Written Assembly—Instruction Sets Haven't Abandoned Humans

## Instruction Sets Haven't Abandoned Humans

There is a common misconception: early x86 instruction sets were for humans, with regular formats and clear semantics; modern instruction sets, AVX-512, various mask operations, various prefix combinations, are purely prepared for compiler-generated machine code, and humans can't read them at all. But looking closely at Intel's instruction manuals reveals a problem with this perception.

The lecture gave a precise example: there is an instruction called `pmaxub`. Looking at the name and description—"parallel compare for unsigned byte maximum values"—comparing 16 bytes with another 16 bytes and taking the larger one. The first reaction might be "what the hell is this instruction." But look at the motion JPEG specification, and you find motion compensation needs exactly this operation, done in one instruction. The compiler has no idea in what context to issue this instruction.

The logic behind new instructions hasn't changed—"a specific domain needs a high-frequency operation, so add a dedicated instruction." It's not "designed for easy compiler generation," but "designed for easy writing by programmers in that domain." It's just that the "programmer" might be writing video codecs, cryptography, or numerical computing. It's not that instruction sets have rejected humans, but that the "humans" they serve have become more specialized.

## Hands-on Verification: Hand-Written Assembly vs Compiler Output

Let's write a piece of actual hand-written assembly to feel the difference between it and compiler output. Environment is Arch Linux WSL, GCC 16.1.1, x86-64 architecture. Note that GCC inline assembly syntax and standalone assembler syntax are two different things, as mentioned later.

First, a simple scenario: take the absolute value of all elements in an array. Write in pure C++, then hand-written SIMD assembly, and compare.

```cpp
// abs_array.cpp
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <chrono>

constexpr int N = 1024 * 1024;  // 1M 个 int32

void abs_c(int32_t* dst, const int32_t* src, int n) {
    for (int i = 0; i < n; i++) {
        dst[i] = std::abs(src[i]);
    }
}

// 手写 SSE 汇编版本，每次处理 4 个 int32
void abs_asm(int32_t* dst, const int32_t* src, int n) {
    // 这里用 GCC 扩展内联汇编
    // 核心思路：用 PSIGND 指令，它可以根据符号掩码取反
    // 但更简单的方式是用 PXOR + PSUBD 的技巧：
    // abs(x) = (x ^ mask) - mask，其中 mask = x >> 31（符号位扩展）
    __asm__ volatile (
        "xor %%eax, %%eax\n\t"         // i = 0
        "1:\n\t"
        "cmp %2, %%eax\n\t"            // 比较 i 和 n
        "jge 2f\n\t"                   // 如果 i >= n，跳到结束
        "movdqu (%1, %%eax, 4), %%xmm0\n\t"  // 加载 4 个 int32
        "movdqa %%xmm0, %%xmm1\n\t"   // 复制一份
        "psrad $31, %%xmm1\n\t"       // 算术右移 31 位，得到符号掩码
        "pxor %%xmm1, %%xmm0\n\t"     // x ^ mask
        "psubd %%xmm1, %%xmm0\n\t"    // (x ^ mask) - mask = abs(x)
        "movdqu %%xmm0, (%0, %%eax, 4)\n\t"  // 存储
        "add $4, %%eax\n\t"           // i += 4（一次处理 4 个）
        "jmp 1b\n\t"                  // 继续循环
        "2:\n\t"
        : // 输出操作数，这里不需要
        : "r"(dst), "r"(src), "r"(n)   // 输入操作数
        : "eax", "xmm0", "xmm1", "memory", "cc"  // clobber 列表
    );
}

int main() {
    // 分配对齐的内存
    int32_t* src = (int32_t*)std::aligned_alloc(16, N * sizeof(int32_t));
    int32_t* dst_c = (int32_t*)std::aligned_alloc(16, N * sizeof(int32_t));
    int32_t* dst_asm = (int32_t*)std::aligned_alloc(16, N * sizeof(int32_t));

    // 填充随机数据（包含负数）
    srand(42);
    for (int i = 0; i < N; i++) {
        src[i] = (int32_t)(rand() - RAND_MAX / 2);
    }

    // 预热
    abs_c(dst_c, src, N);
    abs_asm(dst_asm, src, N);

    // 正确性验证——这一步千万别省，我之前就因为没验证白高兴半天
    bool correct = true;
    for (int i = 0; i < N; i++) {
        if (dst_c[i] != dst_asm[i]) {
            printf("MISMATCH at %d: c=%d, asm=%d\n", i, dst_c[i], dst_asm[i]);
            correct = false;
            break;
        }
    }
    printf("Correctness: %s\n", correct ? "PASS" : "FAIL");

    // 性能测试
    constexpr int ITER = 1000;
    auto t0 = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < ITER; i++) abs_c(dst_c, src, N);
    auto t1 = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < ITER; i++) abs_asm(dst_asm, src, N);
    auto t2 = std::chrono::high_resolution_clock::now();

    double ms_c = std::chrono::duration<double, std::milli>(t1 - t0).count();
    double ms_asm = std::chrono::duration<double, std::milli>(t2 - t1).count();
    printf("C version:   %.2f ms\n", ms_c);
    printf("ASM version: %.2f ms\n", ms_asm);
    printf("Speedup:     %.2fx\n", ms_c / ms_asm);

    std::free(src);
    std::free(dst_c);
    std::free(dst_asm);
    return 0;
}
```

Compile and run:

```bash
g++ -O2 -march=native abs_array.cpp -o abs_array && ./abs_array
```

The result is roughly 3 to 4 times faster for the ASM version. But don't jump to conclusions—if you change `std::vector` to `std::array`, GCC will actually auto-vectorize this loop, and the speed gap will narrow significantly. The value of hand-written assembly lies in: when the compiler's auto-vectorization "doesn't guess your intent," you can control precisely—data has special alignment, the loop has special unrolling needs, you need to insert specific instructions the compiler doesn't know about in the loop—in these scenarios, hand-written assembly is the last resort.

## The Assembler's Big Pit: AT&T vs Intel Syntax

The `as` (GNU Assembler) that comes with GCC uses AT&T syntax—operand order is reversed (source before destination), registers need a `%` prefix, immediates need a `$` prefix. For example, "store the value of eax to address [rbx + 8]", AT&T syntax is `movl %eax, 8(%rbx)`, while NASM syntax is `mov [rbx + 8], eax`—the latter is much more intuitive. If you really plan to write hand-written assembly, I recommend using NASM or YASM; Intel syntax is much more readable:

```asm
; abs_asm.nasm
section .text
global abs_asm_nasm

; void abs_asm_nasm(int32_t* dst, const int32_t* src, int n)
; rdi = dst, rsi = src, rdx = n
abs_asm_nasm:
    xor eax, eax          ; i = 0
.loop:
    cmp eax, edx          ; i < n?
    jge .done
    movdqu xmm0, [rsi + rax*4]   ; 加载 4 个 int32
    movdqa xmm1, xmm0            ; 复制
    psrad xmm1, 31               ; 符号掩码
    pxor xmm0, xmm1              ; x ^ mask
    psubd xmm0, xmm1             ; abs(x)
    movdqu [rdi + rax*4], xmm0   ; 存储
    add eax, 4                   ; i += 4
    jmp .loop
.done:
    ret
```

Same logic, the NASM version reads much clearer. When compiling, note that NASM generates an object file that needs to be linked with your C++ object file. You have to guarantee the calling convention yourself—on Linux x86-64 it's System V AMD64 ABI: the first six integer arguments go in rdi, rsi, rdx, rcx, r8, r9, and the return value is in rax.

## The Direction of Instruction Sets

The direction of instruction sets isn't "from designed for humans to designed for compilers," but "from general design to domain-specific design." Every weird-looking instruction背后 has a specific application scenario. Outside that domain, it looks stupid; inside that domain, it's a lifesaver.

This means two things for C++ programmers: first, when encountering a performance bottleneck and compiler optimization has hit the wall, know you can open the compiler's assembly output (`-S` parameter or Compiler Explorer) to see what's actually happening; second, when discovering a domain has dedicated instructions available, have the ability to use them via inline assembly or standalone assembly files, instead of waiting for the compiler to "learn it someday."

Inline assembly does have a learning cost, but it's not insurmountable—you don't need to memorize the instruction manual, just know "where to look" and "how to write a minimal runnable example," and the rest is the process of checking docs and trial and error.

---

# Human-Oriented Assemblers and LLM-Generated Assembly

## The Concept of a "Human-Friendly Assembler"

The speaker mentioned that many existing assemblers are no longer actively maintained, and then asked if there is still room for a "human-oriented" assembler. The core of this issue lies in the fact that the design philosophy of current tools remains stuck in the era where "an assembler is just a translator for assembly instructions," without moving towards the goal of "making the experience of writing assembly more comfortable."

For example, in NASM, if you want to express "load the second field of this structure into `rax`," you have to calculate the offset yourself and write `mov rax, [rdi + 8]`. That `8` is a result of mental arithmetic. If the structure's field type changes, you have to find all the hardcoded offsets and update them. FASM (Flat Assembler) has a very practical feature—it supports defining "virtual structures" directly in assembly and referencing offsets by field name: `mov rax, [rdi + MyStruct.second]`. Although it is still calculating offsets under the hood, at least the assembler does the math for you.

However, debugging FASM's macro system is painful. Error messages often point to a line within the expanded macro, making it impossible to know where the original macro went wrong. Modern C++ compilers strive to improve error messages and the debugging experience, but it seems time has stood still for assemblers.

An ideal assembler should provide beautiful error messages, have built-in support for structures and unions (not via macro hacks), and support some form of modularity (rather than relying on recursive `include` directives). "Niche" does not mean "worthless."

## LLM-Generated Assembly—Never Trust It Blindly

During the Q&A, an audience member pointed out that LLM-generated assembly code treated `RSI` as a length, which might not actually be the case. The speaker's response was "skeptical" and highlighted the "non-deterministic" nature of these tools. Based on my experience using LLMs to generate assembly and getting burned, **never use LLM-generated assembly code directly unless you fully understand it.**

Here is a real example. I asked an LLM to write an assembly function that "takes three integer arguments and returns their sum":

```nasm
add_three:
    lea rax, [rdi + rsi + rdx]
    ret
```

At first glance, this looks fine—under the System V AMD64 ABI, the first six integer arguments are indeed in `rdi`, `rsi`, `rdx`, `rcx`, `r8`, and `r9`. Adding three arguments using `lea` is more elegant than a chain of `add` instructions. Compiling, linking, and running it yields the correct result. But the problem arises later—ask it to generate a version that "takes six arguments and returns their sum":

```nasm
add_six:
    lea rax, [rdi + rsi]
    lea rax, [rax + rdx]
    lea rax, [rax + rcx]
    lea rax, [rax + r8]
    lea rax, [rax + r9]
    ret
```

This code works correctly in most cases, but there is a subtle problem: `lea` performs unsigned addition. If the values of `rdi` and `rsi` are large and their sum exceeds the range of a 64-bit unsigned integer, it will silently overflow. Using `add` followed by `add` would also overflow, but the setting of the Overflow Flag (OF) conforms to the semantics of arithmetic addition. If the caller relies on the OF flag to judge overflow, this `lea` instruction quietly digs a pit for you.

Even more ridiculously, if you ask the LLM to generate the same functionality again, the second result might write the sixth argument's register as `r10`—which is completely wrong, as `r10` is not an argument-passing register. This is "non-determinism": asking twice yields two different answers; one might be right, and the other might be wrong.

## The Actual Workflow

After falling into these traps, the way we use LLMs to assist with assembly writing should completely change: we should no longer ask it to "write a function that does XXX," but rather treat it as a "chatbot that has memorized the instruction manual." Ask it, "Does x86-64 have an instruction that can perform addition and multiplication simultaneously?" It will tell you that `imul` has an addition variant (like the three-operand form `imul rax, rbx, rcx`), and then you can verify the specific behavior of that instruction in the Intel manual yourself before writing your own code. The value of the LLM devolves from a "code generator" to an "indexing tool"—a tool that is somewhat unreliable but faster than flipping through a PDF manual yourself.

## The Connection Between the Two Points

Viewing these two points together, they point in the same direction: **there is still significant room for improvement in the assembly programming experience**. A human-oriented assembler improves the experience at the tool level, while reliable LLM assistance (if it can ever be achieved) improves the experience at the learning curve level. But the prerequisite for both is—you must understand what is happening at the bottom. No matter how good the tool is, it cannot replace thinking; no matter how strong the LLM is, it cannot replace verification.

---

<ReferenceCard title="References">
  <ReferenceItem
    :id="1"
    author="Matt Godbolt"
    title="C++: Some Assembly Required"
    publisher="CppCon 2025"
    :year="2025"
    url="https://www.youtube.com/watch?v=zoYT7R94S3c"
  />
  <ReferenceItem
    :id="2"
    author="ISO/IEC JTC1/SC22/WG21"
    title="The C++ Standards Committee — Official Page"
    publisher="Open Standards"
    url="https://www.open-std.org/jtc1/sc22/wg21/"
  />
  <ReferenceItem
    :id="3"
    author="ISO C++ Foundation"
    title="The Committee: WG21"
    publisher="isocpp.org"
    url="https://isocpp.org/std/the-committee"
  />
  <ReferenceItem
    :id="4"
    author="cppreference.com"
    title="C++ Reference"
    url="https://en.cppreference.com/"
  />
  <ReferenceItem
    :id="5"
    author="Arm Developer"
    title="Condition Codes 2: Conditional Execution"
    publisher="Arm Community Blogs"
    url="https://developer.arm.com/community/arm-community-blogs/b/architectures-and-processors-blog/posts/condition-codes-2-conditional-execution"
  />
  <ReferenceItem
    :id="6"
    author="Arm Developer"
    title="Condition Codes 1: Condition Flags and Codes"
    publisher="Arm Community Blogs"
    url="https://developer.arm.com/community/arm-community-blogs/b/architectures-and-processors-blog/posts/condition-codes-1-condition-flags-and-codes"
  />
  <ReferenceItem
    :id="7"
    author="Matt Godbolt"
    title="Compiler Explorer"
    url="https://godbolt.org/"
  />
  <ReferenceItem
    :id="8"
    author="cppreference.com"
    title="Modules (since C++20)"
    url="https://en.cppreference.com/cpp/language/modules"
  />
  <ReferenceItem
    :id="9"
    author="Free Software Foundation"
    title="GCC Manual: Program Instrumentation Options"
    publisher="GCC Online Documentation"
    url="https://gcc.gnu.org/onlinedocs/gcc/Instrumentation-Options.html"
  />
  <ReferenceItem
    :id="10"
    author="ISO"
    title="About Us — International Organization for Standardization"
    publisher="iso.org"
    url="https://www.iso.org/about-us.html"
  />
  <ReferenceItem
    :id="11"
    author="ISO"
    title="ISO/IEC 14882:2024 — Programming languages — C++"
    publisher="iso.org"
    :year="2024"
    url="https://www.iso.org/standard/83626.html"
  />
  <ReferenceItem
    :id="12"
    author="cppreference.com"
    title="Constant expressions (C++20/23 [expr.const])"
    url="https://en.cppreference.com/w/cpp/language/constant_expression"
  />
</ReferenceCard>
