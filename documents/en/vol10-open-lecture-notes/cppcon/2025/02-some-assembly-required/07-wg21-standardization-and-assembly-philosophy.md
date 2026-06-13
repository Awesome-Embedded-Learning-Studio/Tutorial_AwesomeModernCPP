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
  source_hash: 5abe719bd8f2b6ac1b756f05bcc928fa5ff7a5f66af625a55e57052592880a8c
  translated_at: '2026-06-13T11:49:20.532826+00:00'
  engine: anthropic
  token_count: 10527
---
# The Organizational Chain of WG21 and the C++ Standard

In various technical articles and videos, we often see the abbreviation "WG21," but few people clearly explain this complete organizational chain from start to finish. In reality, while there are many layers, the structure itself isn't complex—let's first sort out this chain. Later, when we look at proposals and standard documents, we will at least know where these things come from and who is in charge.

## Starting with a Counterintuitive Fact

ISO stands for **International Organization for Standardization** (note the American spelling "Organization," and the last word is "Standardization" rather than "Standards")<RefLink :id="10" preview="ISO, About Us" />. The abbreviation ISO does not come from the English name—the English abbreviation would be IOS, and in French, it's OIN (*Organisation Internationale de Normalisation*). The founders felt that IOS and OIN weren't good enough, so they chose the Greek word *isos* (equal) as a unified abbreviation. This way, it's called ISO in any language. This bit of trivia doesn't have much direct relationship to C++ itself, but it explains why the abbreviation doesn't match the English full name.

::: details Original Reference
The "About us" page on the official ISO website<RefLink :id="10" preview="ISO, About Us" /> states:

> "ISO, the **International Organization for Standardization**, brings global experts together to agree on the best ways of doing things."
>
> "Because 'International Organization for Standardization' would have different acronyms in different languages ('IOS' in English, 'OIN' in French for Organisation internationale de normalisation), our founders decided to give it the short form 'ISO'. ISO is derived from the Greek word isos (meaning 'equal')."

Readers can visit iso.org/about-us.html to verify this.
:::

## How Many Layers Separate ISO from C++?

ISO doesn't directly manage C++. First, it formed a joint partnership with another organization, the IEC (International Electrotechnical Commission), called JTC1. The full name is Joint Technical Committee 1. It manages information technology standards.

Then, under JTC1, there are subcommittees, such as SC22 (Subcommittee 22), whose full name is "Programming languages, their environments and system software interfaces." Note this scope—it's not just programming languages, but also "environments" and "system software interfaces," so a whole bunch of things hang under SC22.

Below SC22 are the various Working Groups (WGs). Many WGs have "grayed out"—they have completed their historical missions, and the corresponding language standards are finalized. But those that are still active, looking at the list: COBOL, Fortran, Ada, C, Prolog, Linux-related items, programming language vulnerability research, and the one we care most about, C++.

C++ is WG21 here. Why number 21? This number is historically assigned; there's no special meaning, just that when it was its turn, that was the number available.

## A Notable Fact

Judging solely by the number of participants in standardization, WG21 (C++) is the largest in volume within the entire SC22 (according to the speaker's observation, if you were to draw a proportional chart based on participation numbers, other language working groups might just be a few dots, while C++ would fill the entire chart). Of course, this doesn't mean other languages aren't important; Fortran, Ada, and others remain indispensable in their respective fields (scientific computing, aerospace). However, the large number of participants directly explains why the speed and complexity of C++ standardization are what they are—many proposals, lots of discussion, and plenty of controversy.

## Summary of the Entire Chain

From top to bottom: ISO and IEC jointly established JTC1 (Joint Technical Committee 1, managing information technology), JTC1 set up SC22 (Subcommittee 22, managing programming languages and related items), and SC22 set up WG21 (Working Group 21, specifically managing C++)<RefLink :id="2" preview="ISO/IEC JTC1/SC22/WG21, Official Page" />.

The complete formal designation is ISO/IEC JTC1/SC22/WG21.

## Why Clarifying This Chain Matters

Once we clarify this chain, when we see the WG21 identifier on proposal documents, we know this is something that has gone through the formal standard-setting process under the ISO framework, not something someone decided on a whim. The "C++ Standard" transforms from a vague concept into an entity backed by a concrete organizational structure. Looking back, it's actually just a few layers of nested committees—nothing mysterious, but when you don't know it, it feels like being in the fog.

---

# The Complete Journey of a Proposal from Idea to C++ Standard

Many people's understanding of "how the C++ standard is made" might stop at the stage of "a group of big shots meeting and making the decision." In reality, the entire process is a very rigorous funnel mechanism. There are quite a few layers, but each step has clear boundaries of responsibility.

## First, Let's Clarify What's Under WG21

When we usually say "The C++ Standards Committee," we are referring to WG21. WG21 is not a flat, large group; it has a bunch of sub-organizations attached underneath. There are those for administration, those for core specifications, those for evolution directions, and a bunch of SGs (Study Groups) whose abbreviations we often see in proposal documents but might not be clear on their specific responsibilities. The status of these study groups is not static; some are active and open to new members, while others have completed their historical missions and are completely closed. However, be aware of a cognitive trap—seeing "closed" and assuming this direction will never be mentioned again. "Closed" just means the study group itself no longer needs to exist; the conclusions it produced may have been taken over by other groups, or may be temporarily shelved. The most typical example is UB (Undefined Behavior); although the relevant study group has closed, proposals regarding UB still exist in large numbers across various groups—after all, this is a pain that people writing C++ cannot bypass.

## How Far Does an Idea Have to Travel from Brain to Standard?

This part is the most interesting part of the whole process. An idea on how C++ should be changed has to go through a complete funnel mechanism to get from your brain into the standard.

The first step is to write the idea into a formal proposal document and send it to a mailing list called a reflector. "Reflector" sounds profound, but it's actually just a mailing list with a slightly old-fashioned name. After the proposal is sent out, it is routed to the corresponding Study Group (SG). In the SG, experts in that field will review it, provide feedback, the author will go back and revise it, send it again, discuss it, and polish it back and forth. This stage is essentially about verifying, on a small scale, whether this idea is actually reliable.

When the discussion in the SG is basically mature, the proposal needs to be "upgraded" to enter a broader view of how it fits into the entire C++ ecosystem. At this point, it forks—if it's a library-level feature (like a new tool in a header file), it goes to LEWG (Library Evolution Working Group); if it's a language-level feature (like new syntax rules), it goes to EWG (Language Evolution Working Group). The difference between LEWG and LWG is: LEWG manages "evolution," discussing whether this feature is worth doing and how to do it more reasonably; while LWG is the "core" group that comes later, responsible for the specific standard wording.

In the evolution groups, it will undergo another round of polishing. When everyone feels the direction of the feature is right and the details are basically in place, it flows from the evolution group to the core group. Library features go to LWG, language features go to CWG. What the core groups do is very hardcore—they directly modify the C++ standard document, translating the proposal into normative text precise down to the punctuation marks.

Finally, assuming everyone in all stages is satisfied with this modification, the proposal enters the full plenary voting stage. All members of WG21 vote together. Once passed, this feature will appear in the next version of the C++ standard. From idea to landing, it may undergo several years of iteration.

## The Core of the Entire Process

After understanding this process, those SGxx, EWG, and LWG abbreviations on proposal documents aren't so headache-inducing anymore<RefLink :id="3" preview="ISO C++ Foundation, The Committee: WG21" />. Opening a proposal, we can consciously look at what stage it is currently in—if it's still in SG, it means it's in early exploration, and design changes are large; if it's already in LWG/CWG, it basically means the general direction is set, and only wording-level polishing remains.

There is another easily overlooked detail: the action of a proposal flowing from the evolution group (EWG/LEWG) to the core group (CWG/LWG) is called "forward" in committee terminology. If you read meeting minutes, you will often see sentences like "LEWG decided to forward Pxxxx to LWG." Here, "forward" is saying the proposal has moved one step down the process.

The entire process is essentially a layered peer review mechanism—first verifying feasibility in a small circle, then looking at the ecosystem impact in a larger circle, and finally having the most rigorous people finalize the wording. Every step has clear boundaries of responsibility. Although slow, it is indeed steady.

---

# How Slow Is C++ Standardization Really—A Horizontal Comparison with Other Languages

Talking about the timeline of C++ standardization, many people's intuition is that C++23 should have come out in 2023, and C++26 will be in 2026. But actually, the technical work for C++23 was completed in early 2023, while ISO official publication dragged on until **October 2024** (Standard number ISO/IEC 14882:2024)<RefLink :id="11" preview="ISO, ISO/IEC 14882:2024" />. The draft for C++26 still has a pile of things under discussion, and the final release will likely be delayed further. The time span from initiation to publication for each version is much longer than most people imagine—this is also a side effect of the massive scale of the C++ standardization project.

::: details Original Reference
ISO official standard page<RefLink :id="11" preview="ISO, ISO/IEC 14882:2024" /> (iso.org/standard/83626.html):

> Status: Published
> Publication date: **2024-10**
> Edition: 7
> Number of pages: 2104

isocpp.org/std/the-Standard<RefLink :id="3" preview="ISO C++ Foundation, The Committee: WG21" />:

> "The current ISO C++ standard is C++23, formally known as ISO International Standard **ISO/IEC 14882:2024(E)** – Programming Language C++."

Readers can visit iso.org/standard/83626.html to verify the publication date themselves.
:::

So how do other languages do it? The paths each family takes differ greatly. A horizontal comparison helps us understand even better why C++ is so slow.

First, let's talk about Rust. Rust's philosophy is completely different from C++. C++'s model is to have an ISO standard document written in extreme detail, and then compiler teams like GCC, Clang, and MSVC each implement this document, striving to align with the standard. Rust basically has only one implementation: rustc. More accurately, the test cases in the Rust source code repository *are* the specification—the "standard" is executable. If you write a piece of code and it passes that set of tests in the Rust source, then it is legal Rust code. In the C++ world, we often encounter inconsistencies between "what the standard says" and "what the compiler actually does," while Rust directly eliminates this problem by using test cases.

The direct benefit of this model is speed. When the Rust team wants to add a feature, they modify the compiler code, write tests, submit a PR, someone reviews and passes it, it gets merged, and by the next release, everyone can use it. There's no problem of "three compilers implementing separately, progress varies, some support it, some don't." They also have a steering committee and an RFC-like proposal process, but overall it's much lighter than C++. The "single implementation + tests as specification" model is indeed a key reason why Rust can maintain a six-week release cycle.

Now let's look at Python. For a long time, Guido van Rossum (Python's father) played the role of "Benevolent Dictator For Life"—which direction the language goes, which features are added, which are not, he made the final call. For example, the controversial walrus operator was advanced during his tenure. But by 2018, Guido stepped down himself. Behind this lies a practical reality: when a language community grows to a certain size, it becomes increasingly difficult for one person to make final decisions, and internal divisions within the community grow larger. Now Python follows a community governance model with a five-person Steering Council, and the proposal mechanism is called PEP (Python Enhancement Proposal), which is somewhat similar to C++'s proposal process but obviously much less formal. They strive to release a version every year and basically achieve it. In comparison, Python's process is quite a bit lighter than C++, but heavier than Rust, sitting in the middle.

Finally, let's talk about JavaScript. nominally, JavaScript has a standardization organization behind it called ECMA, and in some settings JavaScript is called ECMAScript—which is its technically formal name. But in actual experience, the evolution of JavaScript is mainly driven by the V8 engine (behind Chrome) and the Node.js ecosystem. The ECMA standard is mostly a post-hoc ratification of "what everyone is already using." This is almost the reverse of the C++ path of "write standard first, then implement."

Putting these together, a very interesting spectrum forms. On one end is Rust's "implementation is standard" model with extremely fast iteration; in the middle are Python and JavaScript, which have standardization processes but are relatively lightweight, with actual driving force often coming from the implementation side; on the other end is C++, which first writes an extremely detailed specification document, and then multiple compilers implement it separately. The standard committee itself does not do any implementation. Each model has a price—Rust's price is basically no freedom of compiler choice; C++'s price is that a feature may take several years from proposal to actual use.

The reason C++ standardization is slow isn't that the committee members aren't working hard, but that the "define specification first, implement by multiple parties" framework itself determines it can't be fast. As for whether this price is worth it, that's another topic.

---

# The Operation of the C++ Standards Committee and Community Participation

Regarding the C++ standardization process, there are many rumors circulating—"C++ standards are controlled by big companies," "proposals are manipulated in the dark by vendors," etc. But actually understanding it, while there is indeed vendor participation (after all, implementing compilers requires massive engineering resources, and if you invest people, you naturally have a voice), there is a formal committee process behind it. Proposals must go through multiple stages: drafts, voting, review, etc. It's not about who shouts the loudest. The process is relatively lightweight, unlike some languages with extremely strict governance structures, but lightweight doesn't mean no rules.

We also easily feel like "the grass is greener on the other side." Seeing Rust's RFC process, we think it's particularly standardized and transparent, so we complain about why C++ doesn't learn from it. But looking back, C++'s model has exchanged for long-term vitality—this language has walked from the 1980s to today, survived countless waves of technology, and is still alive, and living well. Every governance model has its trade-offs.

## Those Investing Behind the Scenes

The commitment of standard committee members is often underestimated. Many people participating in proposals invest a massive amount of their personal time—not work time, but personal time—into making this language better. Writing proposals, responding to review comments, discussing details repeatedly in mailing lists, flying around the world to attend face-to-face meetings—most of these have no extra pay. CppCon was held in Hawaii; someone came back and said they spent the whole time in the hotel room working on proposals. Then there are the companies sponsoring engineers to participate in standardization, and the families supporting members to attend meetings—these support systems are invisible, but without them, the entire ecosystem wouldn't turn.

## The Value of Offline Meetings

According to the speaker, there are 11 major C++ international conferences in 2025, the most in history. There was a clear trough during COVID, but recovery was quite fast, and it continues to rise—this shows the community is alive. Watching talks online has its value, but sitting offline with a group of people, chatting during the tea break about "how is range-v3 working in your project," "have you hit that MSVC pitfall," this information density and sense of connection is something a screen can't give.

If you are still hesitating about whether to attend an offline C++ conference or meetup, I suggest you go try it, even if it's just a local half-hour sharing session.

---

## The Actual Form of Offline Gatherings

The number of global C++ meetups registered on isocpp.org has already exceeded one hundred and thirty. This means no matter where you live, there is a high probability of finding one within a hundred miles. Major cities in China basically have them, and second-tier cities are slowly seeing them appear. If you really can't find one, starting one yourself is completely fine—no formal process is needed. Someone just sent a message in a group saying "I'm bringing my laptop to sit at [place] on Friday night, chatting about C++, come if you want." The first time four people came, later it stabilized at about ten people, once a month, looking at code together and discussing problems.

There are also more formal forms: big companies sponsor venues, invite external speakers for technical sharing, with slides and Q&A; lightning talk form, where everyone speaks for five to ten minutes about a pitfall experience or a small trick, very fast pace and high information density. Some companies even have regular internal technical exchange times.

A practical benefit of offline chatting is that the technical solutions discussed and pitfall experiences are often things you can't find online—those things aren't "systematic" enough to justify writing a blog post, but it is precisely this fragmented, frontline experience from real projects that is often the most useful.

---

# Online Communities and Resources

Many people spend the early stages of learning C++ tinkering alone. When encountering compilation errors, they search themselves; if they can't find it, they change the writing method to bypass it. After continuing in this state for a while, one often discovers the bottleneck isn't the effort, but whether one has found the right circle.

## Online Communities

The atmosphere of online communities is often much better than imagined. The C++ Slack (operated by C++ Alliance) has very detailed channels; you can join different channels according to your interests. There are even more choices on Discord, such as the Compiler Explorer Discord and servers dedicated to discussing C++ standard proposals, which are active discussion venues. Novices and experts communicate in the same space—this is real in the C++ community. Someone who has studied for two months asks a pointer question in the `#C++-basic` channel on Slack, and several people patiently explain below; ISO committee members discuss proposal details with people on Discord.

The practical advice is: don't ask questions as soon as you enter. Spend a few days lurking first, see how others ask and answer questions, and get familiar with the rhythm of the community. You will learn a lot during the lurking process.

## cppreference—Community-Driven Reference Documentation

cppreference<RefLink :id="4" preview="cppreference.com, C++ Reference" /> is a community-driven, community-operated reference website. Every page and every example code on it is actually maintained by someone. It is not official documentation sponsored by some big company, but a group of volunteers working on it. Normally, it can be modified and supplemented by community members, which is also why it can maintain high quality—it's not one person writing, it's countless people maintaining it together. Every time you look up a standard library component, casually look at the comments and discussions at the bottom of the page, and you can often find some very valuable information, such as known issues with a function on a specific compiler.

## Code Sharing Platforms

Besides real-time chat communities, code sharing platforms like Compiler Explorer<RefLink :id="7" preview="Compiler Explorer, godbolt.org" /> are extremely important in technical exchange. Put code in, generate a link, and drop it anywhere—Discord, Slack, forums, or even send it directly to a colleague. Compared to pasting a large block of code text, a Compiler Explorer link lets others click to see directly, modify directly, and run directly. The efficiency is completely different.

When debugging problems, first put the minimal reproduction code onto Compiler Explorer, confirm it can be reproduced on multiple compilers, and then go to the community to ask—the benefit of this is that when others help you troubleshoot, they don't need to set up an environment; they can directly click the link to see what you see.

## The Community Is the Core of the C++ Ecosystem

The reason C++ is fascinating is not just because the language itself is powerful, but because of the people behind it. Those who silently submit patches in open source projects, those who spend their own time maintaining cppreference, those who organize offline gatherings at their own expense, those who help novices debug code at 3 AM on Discord—it is these people who make up the C++ ecosystem. Soaking in the community, you see not only the answers to problems, but also how others think about problems, their ideas for solving them, and even their attitude towards technology.

---

# Participating in the C++ Community—Contributions Come in More Than One Form

Regarding "participating in the open source community," many people have a narrow understanding—thinking it's something only qualified people can do, something only big shots with their names on the committee or authors of famous libraries can talk about. But in reality, the ways to participate are far more diverse than imagined.

## "Contribution" Is Broader Than We Think

Contributing to the C++ community doesn't necessarily mean writing a widely used library or submitting a proposal to the standard committee that gets adopted. The participation methods mentioned in the talk are things you can do right now: if your city doesn't have a C++ meetup, just start one yourself—you don't need to be an expert, you just need to be someone willing to get people together to chat about C++; attending a conference, even just to listen and meet a few other people using C++, is itself already participating in the community; writing an article about a pitfall you stepped in so that people behind you have fewer detours is also a contribution.

## About Getting on Stage

There is a very real description in the talk—standing on the speaking stage, looking back at countless faces staring at you, thinking "Why am I doing this again?" Doing technical sharing doesn't require perfection; you only need to speak about what you have truly understood and the pits you have stepped in. This is valuable enough. If you have the opportunity to share, even if you are nervous, it's worth trying once.

## About Participating in the C++ Committee

The C++ committee is recruiting. The committee's work requires participation from people at all levels—not just experts in language design, but also feedback from actual users, people to test proposals, write use cases, and report problems. You don't need to be Bjarne Stroustrup to get in; you just need passion and willingness to invest time.

## A Final Small Interlude

There is a very real detail in the Q&A session: the speaker referred to Barry Revzin as the person in charge of Ranges, only to be corrected on the spot—Barry Revzin has recently done a lot of work on the application layer of C++26 Reflection (he gave a talk "Practical Reflection With C++26" at CppCon), while the main author of Ranges is Eric Niebler (the speaker misspoke it as Eric Kneedler). However, strictly speaking, the main drivers of the Reflection proposal are Daveed Vandevoorde and Herb Sutter, etc., while Revzin is more on the application and teaching side. This kind of "mixing up people and their responsible areas" is common; the C++ standard committee involves too many people and sub-working groups, and even frequent participants may not be able to figure it all out clearly. The speaker mocked himself, saying "I am truly terrible," and this sense of realism actually makes people feel that this community is very down-to-earth.

## The Threshold for Participating in the Community

The C++ community is not some closed circle; it is composed of every person currently using C++. The simplest contribution might just be sharing what you learned today with a colleague next to you, or answering a novice's question in the community. You don't have to wait until you are "strong enough" to participate—because by then you may have forgotten the confusion of the novice stage, and it is precisely those confusions that are the most valuable sharing content.

---

# The "Never Execute" Instruction in ARM32 Condition Codes—Orthogonal Design and Its Demise

This Q&A segment involves an interesting architectural design question. In the ARM32 instruction set, every instruction has a four-bit condition code field at the front. You can write `ADDNE` to mean "add if not equal," `MOV EQ` to mean "move if equal," without writing separate branch instructions, resulting in very high code density. Among the condition codes, there is `AL` (Always, always execute), corresponding to `0b1110`; but there is another condition code where all four bits are 1, i.e., `0b1111`, called `NV` (Never), meaning "Never." A "never execute" instruction—writing it in is just taking up space, right?

::: warning Important Correction
The NV condition code only exists in **ARMv4 and earlier versions**. Starting from ARMv5, NV was officially deprecated, and the `0b1111` encoding was reassigned for unconditional instruction extensions. On ARMv7-A, using the condition code `NV` results in **UNPREDICTABLE** behavior; it no longer guarantees "never execute." The verification experiments later in this article need to target the ARMv4 architecture to get the expected results. The official ARM documentation states:

> "Every conditional instruction contains a 4-bit condition code field, the cond field, in bits 31 to 28. This field contains one of the values **0b0000 – 0b1110**."
>
> — ARM Architecture Reference Manual ARMv7-A/R, Section "The condition code field"<RefLink :id="5" preview="Arm Developer, Condition Codes: Conditional Execution" />

Actual verification results (arm-none-linux-gnueabihf-gcc 15.2 + qemu-arm-static):

```text
$ ./a.out
Before: 0
After: 0
```

Verification code is in the repository: [05-01-arm32-nv-condition.c](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/blob/main/code/volumn_codes/vol10/cppcon/2025/02-some-assembly-required/05-01-arm32-nv-condition.c).
:::

## Orthogonality—The Design Philosophy of ARM32

The key lies in the design philosophy of ARM32: **extreme orthogonality**<RefLink :id="5" preview="Arm Developer, Condition Codes: Conditional Execution" />. Simply put, orthogonality means "the choice of each dimension is independent and can be freely combined." In ARM32, the dimension of condition codes is designed very thoroughly—every condition has its logical opposite. Equal (EQ) is the opposite of Not Equal (NE), Greater or Equal (GE) is the opposite of Less Than (LT), Unsigned Higher (HI) is the opposite of Unsigned Lower or Same (LS)... and so on.

So what is the logical opposite of "Always Execute" (AL)? Naturally, it is "Never Execute" (NV).

Since four bits can represent 16 states, the designers of the condition codes filled all 16 states, and each has a corresponding meaning. This isn't "deliberately leaving a useless one," but the inevitable result of pushing orthogonality to the extreme—it's impossible to keep only 15 and leave one empty, that wouldn't be orthogonal. The price is: in the entire instruction encoding space of ARM32, a full sixteenth of the encodings correspond to instructions that "do nothing." This is a design trade-off—using a little wasted space in exchange for conceptual perfect symmetry of the instruction set.

This design was indeed the case in the original ARM (ARMv1 to ARMv4). But subsequent versions of ARM prove that "orthogonal to the extreme" also has a price.

## Hands-on Verification: Writing a "Never Execute" Instruction (ARMv4)

We can verify this ourselves<RefLink :id="6" preview="Arm Developer, Condition Codes: Condition Flags and Codes" />. Because the NV condition code is only valid in ARMv4 and earlier, we need to explicitly specify the architecture version.

::: details Why can't we use ARMv7?
The valid condition code range for ARMv7-A is only `0b0000`–`0b1110`. The encoding `0b1111` has been reassigned in ARMv5+—it is either interpreted as a completely different instruction (using condition code bits to extend opcode space) or produces UNPREDICTABLE behavior. Using `NV` on ARMv7 **does not guarantee** the result is "never execute." The verification code is in the repository ([05-01-arm32-nv-condition.c](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/blob/main/code/volumn_codes/vol10/cppcon/2025/02-some-assembly-required/05-01-arm32-nv-condition.c)), and readers can compare tests on ARMv4 and ARMv7 targets themselves.
:::

The environment is Arch Linux WSL, using the cross-compilation toolchain `arm-none-linux-gnueabihf-gcc` (Arm GNU Toolchain 15.2). Note that when compiling, you need to use `-march=armv4` to ensure the semantics of the NV condition code:

First, write a simple C file:

```cpp
// 05-01-arm32-nv-condition.c
#include <stdio.h>

int main(void) {
    int result = 0;
    printf("Before: %d\n", result);

    // Inline assembly: MOV R0, #5 (Always)
    // We will manually modify the machine code later to change AL to NV
    asm volatile(
        "mov r0, #5 \n\t"
        "str r0, %0"
        : "=m"(result)
        :
        : "r0"
    );

    printf("After: %d\n", result);
    return 0;
}
```

Compile it to assembly to see what a normal `MOV` looks like (note we use `-march=armv4` here):

```bash
arm-none-linux-gnueabihf-gcc -S -march=armv4 -masm=intel 05-01-arm32-nv-condition.c -o 05-01.s
```

Now, let's manually construct a "never execute" `MOV`. In the ARM32 `MOV` instruction encoding format, the high four bits are the condition code. The machine code for a normal `MOV R0, #5` can be checked with `objdump`:

```bash
cat 05-01.s | grep -A 5 "mov r0, #5"
# Output example: mov r0, #5  @ machine code: 0xe3a00005
```

See the `e3`? The high four bits are `e`, which is binary `1110`, corresponding to the condition code `AL` (Always). Now, change the high four bits from `e` to `f`, i.e., from `1110` to `1111`. On ARMv4, this is a "never execute" `MOV`—it is decoded, the CPU recognizes it as a MOV instruction, but because the condition code is NV, it never actually executes.

::: warning Reminder again
This instruction only behaves as "never execute" on ARMv4 and earlier. If executing `0xf3a00005` on ARMv5+ (including ARMv7-A), the behavior is UNPREDICTABLE.
:::

Use `.inst` to directly stuff the machine code in for verification:

```cpp
// 05-01-arm32-nv-condition.c (modified)
#include <stdio.h>

int main(void) {
    int result = 0;
    printf("Before: %d\n", result);

    // 0xf3a00005 = MOV NV R0, #5
    asm volatile(
        ".inst 0xf3a00005 \n\t"
        "str r0, %0"
        : "=m"(result)
        :
        : "r0"
    );

    printf("After: %d\n", result);
    return 0;
}
```

Compile and run (note `-march=armv4`):

```bash
arm-none-linux-gnueabihf-gcc 05-01-arm32-nv-condition.c -march=armv4 -o a.out
qemu-arm-static ./a.out
```

`result` is still 0—that `MOV` was fully decoded, but the CPU looked at the condition code, saw it was `NV`, skipped it directly, and did nothing. `result` kept its previous value of 0.

There is an easy pitfall here: if you didn't add the output constraint `=m"(result)`, the compiler might optimize away `result` entirely, and no matter how you run it, it's 0, easily leading you to think you wrote the machine code wrong.

## By the Way: The TEQ Instruction

The Q&A also mentioned an instruction called `TEQ`. `TEQ` stands for "Test Equivalence," performing an XOR operation and setting flags, used to compare whether two values are equal (without changing register values, only changing flags). `TEQP` with the `P` suffix is an instruction in older ARM (pre-ARMv4) used to directly operate on the Processor Status Register (PSR)—in modern ARM it has been replaced by `MSR`/`MRS` instructions.

## Summary

The "no-op" instruction encoding, one-sixteenth of the space in ARM32 (ARMv4 and earlier), is not a bug, not a legacy issue, but an inevitable byproduct of extreme orthogonal design. The designers chose conceptual perfect symmetry, and the price was wasting some encoding space.

But ARM's own subsequent evolution explains everything: ARMv5 deprecated the NV condition code and reclaimed the `0b1111` encoding space; ARM64 (AArch64) completely removed the condition code field. "Orthogonal to the extreme" is conceptually beautiful, but ARM's practice proves that in actual evolution, encoding space and instruction set simplicity ultimately triumph over conceptual perfect symmetry. After understanding this design history, the experience of reading assembly manuals will be completely different.

---

# Should I Learn x86 or RISC-V Assembly?

When tinkering on Compiler Explorer, we often struggle with one question: x86 assembly looks like gibberish—`mov eax, dword ptr [rbx + 8]`, register names are long and irregular; switching to RISC-V looks much more understandable, registers are just `x0` to `x31`, and the instruction format is much more regular. But how big is the gap between reading RISC-V assembly and the actual x86 code running at work? Will reading it be a waste of time?

## Conclusion: It Depends on the Optimization Level

There is no one-size-fits-all answer to this; the key lies in the optimization level selected in Compiler Explorer. If you are using `-O0` (no optimization), there isn't much difference between looking at x86 or RISC-V. What the compiler does under `-O0` is very "generic"—it honestly translates C++ statements into machine instructions one by one, pushing to the stack when needed, storing to memory when needed. Regardless of the architecture, this is the routine. At this level, what you learn—"what the compiler turned the code into"—is indeed interchangeable knowledge across architectures.

Let's verify with a simple function:

```cpp
int add_mul(int a, int b, int c) {
    int x = a + b;
    return x * c;
}
```

Under `-O0`, although the instructions differ between x86 and RISC-V, the "flavor" is exactly the same—both first store parameters to the stack, then load them back from the stack to do addition, store the result back to the stack, and finally load it out to do multiplication. The compiler is very honest without optimization; this understanding has nothing to do with the architecture.

## When You Hit -O2 and Above, Things Change

When the optimization level is pulled to `-O2` or even `-O3`, the differences between architectures start to appear systematically. The assembly you see is no longer purely "compiler's generic optimization strategy"; it's mixed with a large amount of "specialized optimization for this architecture's specific instruction set."

A typical example—counting the number of 1s in an integer, popcount:

```cpp
int count_ones(int x) {
    int count = 0;
    while (x) {
        count += x & 1;
        x >>= 1;
    }
    return count;
}
```

Under `-O3`, if you throw this code into x86 on Compiler Explorer, the compiler directly replaces it with a single `popcnt`
