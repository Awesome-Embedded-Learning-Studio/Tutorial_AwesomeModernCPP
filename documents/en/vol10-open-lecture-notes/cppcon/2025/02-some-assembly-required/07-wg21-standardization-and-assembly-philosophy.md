---
chapter: 2
conference: cppcon
conference_year: 2025
cpp_standard:
- 17
- 20
description: 'CppCon 2025 Talk Notes —— C++: Some Assembly Required by Matt Godbolt'
difficulty: intermediate
order: 7
platform: host
reading_time_minutes: 30
speaker: Matt Godbolt
tags:
- cpp-modern
- host
- intermediate
talk_title: 'C++: Some Assembly Required'
title: WG21 Standardization and x86/RISC-V Assembly Philosophy
translation:
  engine: anthropic
  source: documents/vol10-open-lecture-notes/cppcon/2025/02-some-assembly-required/07-wg21-standardization-and-assembly-philosophy.md
  source_hash: c2b43f1d7eb03178b6a2f18ed355c6db1cbc0a8eda074ccb9592ff65884f4836
  token_count: 10526
  translated_at: '2026-06-14T00:16:05.966470+00:00'
video_bilibili: https://www.bilibili.com/video/BV1ptCCBKEwW?p=2
video_youtube: https://www.youtube.com/watch?v=zoYT7R94S3c
---
# WG21 and the Organizational Hierarchy of the C++ Standard

In various technical articles and videos, we frequently see the abbreviation "WG21," yet few people clearly explain this complete organizational chain from start to finish. Although there are many levels, the structure itself is not complex. Let's first sort out this chain so that later, when we look at proposals and standard documents, we at least know where these things come from and who manages them.

## Let's Start with a Counterintuitive Fact

ISO stands for **International Organization for Standardization** (note the American spelling "Organization," and the last word is "Standardization" not "Standards")<RefLink :id="10" preview="ISO, About Us" />. The abbreviation ISO does not come from the English name—the English abbreviation would be IOS, and in French, it would be OIN (*Organisation Internationale de Normalisation*). The founders felt that neither IOS nor OIN was good enough, so they chose the Greek word *isos* (meaning equal) as a unified abbreviation. This way, regardless of the language, it is called ISO. While this bit of trivia has no direct relationship to C++, it explains why the abbreviation doesn't match the English full name.

::: details Reference Text
The original text from the ISO "About us" page<RefLink :id="10" preview="ISO, About Us" />:

> "ISO, the **International Organization for Standardization**, brings global experts together to agree on the best ways of doing things."
>
> "Because 'International Organization for Standardization' would have different acronyms in different languages ('IOS' in English, 'OIN' in French for Organisation internationale de normalisation), our founders decided to give it the short form 'ISO'. ISO is derived from the Greek word isos (meaning 'equal')."

Readers can visit iso.org/about-us.html to verify this.
:::

## How Many Layers Separate ISO from C++?

ISO does not manage C++ directly. First, it formed a joint venture with another organization, the IEC (International Electrotechnical Commission), called JTC1. The full name is Joint Technical Committee 1. It manages information technology standards.

Then, under JTC1, there are subcommittees, such as SC22 (Subcommittee 22). The full name is "Programming languages, their environments and system software interfaces." Note this scope—it is not just programming languages, but also "environments" and "system software interfaces," so a whole bunch of things hang off SC22.

Below SC22 are the various Working Groups (WGs). Many WGs have been grayed out—they have completed their historical missions, and the corresponding language standards are finished. But those that are still active include: COBOL, Fortran, Ada, C, Prolog, Linux-related items, programming language vulnerability research, and the one we care about most: C++.

Inside this structure, C++ is WG21. Why number 21? This number is a historical allocation with no special meaning; it just happened to be the number assigned when it was its turn.

## A Notable Fact

Judging solely by the number of participants in standard setting, WG21 (C++) is the largest group within the entire SC22 (according to the speaker's observation, if you were to draw a proportional chart based on participation numbers, other language working groups might just be a few dots, while C++ would fill the entire chart). Of course, this doesn't mean other languages aren't important; Fortran, Ada, and others remain indispensable in their respective fields (scientific computing, aerospace). However, the high number of participants directly explains why the speed and complexity of C++ standardization are what they are—many proposals, many discussions, and many controversies.

## Summary of the Entire Chain

From top to bottom: ISO and IEC jointly established JTC1 (Joint Technical Committee 1, managing information technology). JTC1 set up SC22 (Subcommittee 22, managing programming languages and related items). SC22 set up WG21 (Working Group 21, specifically managing C++)<RefLink :id="2" preview="ISO/IEC JTC1/SC22/WG21, Official Page" />.

The complete formal designation is ISO/IEC JTC1/SC22/WG21.

## Why It's Meaningful to Understand This Chain

Once we understand this chain, when we see the WG21 identifier on proposal documents, we know these are things that have gone through the formal standard-setting process under the ISO framework, not something someone decided on a whim. The concept of the "C++ Standard" transforms from a vague idea into an entity backed by a specific organizational structure. Looking back, it's really just a few layers of nested committees—nothing mysterious, but without this knowledge, it feels like being in the fog.

---

# The Complete Journey of a Proposal from Idea to C++ Standard

Many people's understanding of "how the C++ standard is made" might stop at the stage of "a group of experts meeting and making decisions." In reality, the entire process is a rigorous funnel mechanism with quite a few levels, but each step has clear boundaries of responsibility.

## First, Let's Clarify What's Under WG21

When we usually say "The C++ Standards Committee," we are referring to WG21. WG21 is not a flat, large group; it has a bunch of sub-organizations attached to it. There are those for administration, those for core specifications, those for evolution directions, and a bunch of SGs (Study Groups) whose abbreviations we often see in proposal documents but might not be clear on their specific responsibilities. The status of these study groups is not static; some are active and open to new members, while others have completed their historical missions and are completely closed. However, watch out for a cognitive trap—seeing "closed" and assuming this direction will never be mentioned again. "Closed" just means the study group itself doesn't need to exist anymore; the conclusions it produced may have been taken over by other groups, or may be temporarily shelved. The most typical example is UB (Undefined Behavior); although the relevant study group is closed, proposals regarding UB still exist in various groups—after all, this is a pain that people writing C++ cannot bypass.

## How Far Does an Idea Have to Travel from Brain to Standard?

This part is the most interesting part of the whole process. An idea about how C++ should be changed has to go through a complete funnel mechanism to get from your brain into the standard.

The first step is to write the idea into a formal proposal document and send it to a mailing list called a reflector. "Reflector" sounds high-level, but it's actually just a mailing list with an old-fashioned name. After the proposal is sent out, it is routed to the corresponding Study Group (SG). Inside the SG, experts in that field will review it, provide feedback, and then the author goes back to revise it. After revising, send it again, discuss it again, and polish it back and forth. This stage is essentially about verifying, in a small scope, whether this idea is actually reliable.

When the discussion in the SG is basically mature, the proposal needs to "upgrade" and enter a broader scope to see how it integrates into the entire C++ ecosystem. At this point, it forks—if it's a library-level feature (like a new tool in a header file), it goes to LEWG (Library Evolution Working Group); if it's a language-level feature (like new syntax rules), it goes to EWG (Language Evolution Working Group). The difference between LEWG and LWG is: LEWG manages "evolution," discussing whether this feature is worth doing and how to do it more reasonably; whereas LWG is the "core" group that comes later, responsible for the specific standard wording.

In the evolution groups, it undergoes another round of polishing. When everyone feels the direction of the feature is right and the details are basically in place, it flows from the evolution group to the core group. Library features go to LWG, language features go to CWG. What the core groups do is very hardcore—they directly modify the C++ standard document, translating the proposal into normative text precise down to the punctuation marks.

Finally, assuming everyone in all stages is satisfied with this modification, the proposal enters the full vote stage. All members of WG21 vote together. After it passes, this feature will appear in the next version of the C++ standard. From idea to landing, it may undergo several years of iteration.

## The Core of the Process

After understanding this process, the abbreviations SGxx, EWG, and LWG on proposal documents are no longer so headache-inducing<RefLink :id="3" preview="ISO C++ Foundation, The Committee: WG21" />. Opening a proposal, we can consciously look at what stage it is currently at—if it's still in SG, it means it's in early exploration, and design changes are very large; if it has reached LWG/CWG, it basically means the general direction is set, and only wording-level polishing remains.

There is also an easily overlooked detail: the action of a proposal flowing from the evolution group (EWG/LEWG) to the core group (CWG/LWG) is called "forward" in committee terminology. If you read meeting minutes, you will often see sentences like "LEWG decided to forward Pxxxx to LWG." Here, "forward" means the proposal has moved one step down the process.

The entire process is essentially a layered peer review mechanism—first verify feasibility in a small circle, then look at the ecosystem impact in a large circle, and finally have the most rigorous people finalize the wording. Every step has clear boundaries of responsibility. Although slow, it is indeed steady.

---

# How Slow Is C++ Standardization Really?—A Horizontal Comparison with Other Languages

When talking about the timeline of C++ standardization, many people's intuition is that C++23 should have come out in 2023, and C++26 will be in 2026. But in reality, the technical work for C++23 was completed in early 2023, while ISO publication dragged on until **October 2024** (Standard number ISO/IEC 14882:2024)<RefLink :id="11" preview="ISO, ISO/IEC 14882:2024" />. The draft for C++26 still has a pile of things under discussion, and the final release will most likely be delayed further. The time span from initiation to publication for each version is much longer than most people imagine—this is also a side effect of the massive scale of the C++ standardization project.

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

So how do other languages do it? The paths each family takes are very different. A horizontal comparison helps us understand why C++ is so slow.

First, let's talk about Rust. Rust's philosophy is completely different from C++. C++'s model is to have an ISO standard document written in extreme detail, and then compiler teams like GCC, Clang, and MSVC each implement this document, everyone striving to align with the standard. Rust basically has only one implementation, which is rustc. More accurately, the test cases in the Rust source code repository are themselves the specification—the "standard" is executable. You write a piece of code, and if it passes that set of tests in the Rust source, it is legal Rust code. In the C++ world, we often encounter inconsistencies between "what the standard says" and "what the compiler actually does," while Rust directly eliminates this problem by using test cases.

The direct benefit of this model is speed. When the Rust team wants to add a feature, they modify the compiler code, write tests, submit a PR, someone reviews and passes it, it gets merged, and by the time the next version is released, everyone can use it. There is no problem of "three compilers implementing separately, progress varies, some support it, some don't." They also have a leadership committee and an RFC-like proposal process, but overall it is much lighter than C++. The "single implementation + test as specification" model is indeed a key reason why Rust can maintain a six-week release cycle.

Now let's look at Python. For a long time, Guido van Rossum (Python's father) played the role of "Benevolent Dictator For Life"—which direction the language goes, which features are added, which are not, and he made the final decision. For example, the controversial walrus operator `:=` was pushed during his tenure. But by 2018, Guido stepped down himself. Behind this reflects a realistic problem: when the language community grows to a certain size, it becomes increasingly difficult for one person to make the final decision, and internal divisions within the community will grow. Now Python uses a community governance model with a five-person Steering Council, and the proposal mechanism is called PEP (Python Enhancement Proposal), which is somewhat similar to C++'s proposal process but obviously much less formal. They strive to release a version every year and basically achieve it. In comparison, Python's process is quite a bit lighter than C++, but heavier than Rust, sitting in the middle.

Finally, let's talk about JavaScript. nominally, JavaScript has a standardization organization behind it called ECMA, and in some settings JavaScript is called ECMAScript—which is its technically formal name. But in actual experience, the evolution of JavaScript is mainly driven by the V8 engine (the engine behind Chrome) and the Node.js ecosystem. The ECMA standard is more of a post-facto recognition of "what everyone is already using." This is almost the reverse of the C++ path of "standard first, implement later."

Putting these together, a very interesting spectrum forms. On one end is Rust's "implementation is standard" model with extremely fast iteration; in the middle are Python and JavaScript, which have standardization processes but are relatively lightweight, and the actual driving force often comes from the implementation side; on the other end is C++, which first writes an extremely detailed specification document, and then multiple compilers implement it separately. The standard committee itself does not do any implementation. Each model has a price—Rust's price is basically no freedom of compiler choice; C++'s price is that a feature may take several years from proposal to actual use.

The reason C++ standardization is slow is not that the people in the committee aren't working hard, but that the framework of "specification first, multi-party implementation" itself determines that it can't be fast. As for whether this price is worth it, that is another topic.

---

# The Operation of the C++ Standards Committee and Community Participation

Regarding the C++ standardization process, there are many legends circulating—"C++ standards are controlled by big companies," "proposals are manipulated in the dark by vendors," etc. But actually understanding it, although there is indeed vendor participation (after all, implementing compilers requires a lot of engineering resources, and if you invest people, you naturally have a voice), there is a formal committee process behind it. Proposals have to go through multiple stages like drafts, voting, and reviews; it's not about who has the loudest voice. The process is relatively lightweight, unlike some languages with extremely strict governance structures, but lightweight doesn't mean no rules.

We also easily feel that "the grass is greener on the other side." Seeing Rust's RFC process feels particularly standard and transparent, so we complain why C++ doesn't learn from it. But looking back, C++'s model has traded for long-term vitality—this language has walked from the 1980s to today, survived countless waves of technology, and is still alive, and living very well. Every governance model has its trade-offs.

## Those Investing Behind the Scenes

The dedication of standard committee members is often underestimated. Many people participating in proposals invest a huge amount of their personal time—not work time, but personal time—into making this language better. Writing proposals, responding to review comments, discussing details repeatedly on mailing lists, flying around the world to attend face-to-face meetings—most of these have no extra pay. CppCon was held in Hawaii, and someone came back saying they spent the whole time in the hotel room going over proposals. There are also companies that sponsor engineers to participate in standardization, and families who support members attending meetings—these support systems are invisible, but without them, the entire ecosystem wouldn't turn.

## The Value of Offline Meetings

According to the speaker, there are 11 major C++ international conferences in 2025, the most in history. There was a clear trough during COVID, but recovery was quite fast, and it is still rising—this shows the community is alive. Watching speeches online has its value, but sitting offline with a group of people, chatting during tea break about "how is range-v3 working in your project," "have you hit that pitfall with MSVC," this information density and sense of connection is something a screen can't give.

If you are still hesitating about whether to attend an offline C++ conference or meetup, I suggest you go try it, even if it's just a local half-hour sharing session.

---

## The Actual Form of Offline Gatherings

The number of global C++ meetups registered on isocpp.org has already exceeded one hundred and thirty, which means no matter where you live, there is a high probability of finding one within a hundred miles. Major cities in China basically have them, and second-tier cities are also slowly appearing. If you really can't find one, starting one yourself is completely fine—no formal process is needed. Someone just posted a message in a group saying "I'll bring my laptop and sit at a certain place on Friday night, chatting about C++, come if you want." The first time four people came, and later it stabilized at about a dozen people, once a month, looking at code together and discussing problems.

There are also more formal forms: big companies sponsor venues, invite external speakers to do technical sharing, with slides and Q&A; lightning talk form, where everyone speaks for five to ten minutes about a pitfall experience or a small trick, the rhythm is fast, and the information density is high. Some companies even have regular technical exchange time internally.

An actual benefit of offline chatting is that the technical solutions and pitfall experiences discussed are often things that can't be found online—those things aren't "systematic" enough to write a blog post, but it is precisely this fragmented, front-line experience from real projects that is often the most useful.

---

# Online Communities and Resources

Many people spend the early stages of learning C++ tinkering alone. When encountering a compilation error, they search for it themselves; if they can't find it, they change the writing method to bypass it. After continuing in this state for a while, one often finds that the bottleneck is not the degree of effort, but whether the right circle has been found.

## Online Communities

The atmosphere of online communities is often much better than imagined. The C++ Slack (operated by C++ Alliance) has very detailed channels, and you can join different channels according to your interests. There are even more choices on Discord, such as the Compiler Explorer Discord and servers dedicated to discussing C++ standard proposals, which are active discussion places. Novices and experts communicate in the same space—this is real in the C++ community. Someone who has studied for two months asks a pointer question in the `#cplusplus` channel on Slack, and several people patiently explain below; ISO committee members discuss proposal details with people on Discord.

The practical advice is: don't ask questions as soon as you enter. Spend a few days lurking first, see how others ask questions and answer them, and get familiar with the rhythm of the community. You will learn a lot during the lurking process.

## cppreference—Community-Driven Reference Documentation

cppreference<RefLink :id="4" preview="cppreference.com, C++ Reference" /> is a community-driven, community-operated reference website. Every page and every example code on it is maintained by actual people. It is not official documentation sponsored by some big company, but a group of volunteers working on it. Normally, it can be modified and supplemented by community members, which is also why it can maintain high quality—it's not one person writing, it's countless people maintaining it together. Every time you look up a standard library component, take a look at the comments and discussions at the bottom of the page, and you can often find some very valuable information, such as known issues of a function on a specific compiler.

## Code Sharing Platforms

Besides real-time chat communities, code sharing platforms like Compiler Explorer<RefLink :id="7" preview="Compiler Explorer, godbolt.org" /> are extremely important in technical communication. Put the code in, generate a link, and drop it anywhere—Discord, Slack, forums, or even send it directly to a colleague. Compared to pasting a large chunk of code text, a Compiler Explorer link lets others click to see directly, modify directly, and run directly. The efficiency is completely different.

When debugging problems, first put the minimal reproduction code on Compiler Explorer, confirm it can be reproduced on multiple compilers, and then go to the community to ask—the benefit of this is that when others help you troubleshoot, they don't need to set up the environment, they can just click the link to see what you see.

## The Community is the Core of the C++ Ecosystem

C++ is fascinating not only because the language itself is powerful, but because of the people behind it. Those who silently submit patches to open source projects, those who spend their own time maintaining cppreference, those who organize offline gatherings at their own expense, those who help novices debug code at 3 AM on Discord—it is these people who make up the C++ ecosystem. Soaking in the community, you see not only the answers to problems, but also how others think about problems, their ideas for solving them, and even their attitude towards technology.

---

# Participating in the C++ Community—Contributions Come in Many Forms

Regarding "participating in the open source community," many people have a narrow understanding—thinking it is something only qualified people can do, something only experts hanging their names in the committee or authors of famous libraries are worthy of talking about. But in reality, the ways to participate are far more diverse than imagined.

## "Contribution" is Broader Than We Imagine

Contributing to the C++ community doesn't necessarily mean writing a widely used library or submitting a proposal to the standards committee that gets adopted. The ways of participating mentioned in the speech are things that can be done right now: if there is no C++ gathering in your city, start one yourself—you don't need to be an expert, you just need to be someone willing to get people together to chat about C++; attend a conference, even if it's just to listen and meet a few other people using C++, this in itself is already participating in the community; write an article about the pits you stepped into so that people behind you have fewer detours, this is also a contribution.

## About Taking the Stage

There is a very real description in the speech—standing on the speaking stage, looking back at the countless faces staring at you, thinking "why am I doing this again." Doing technical sharing doesn't need to be perfect, you only need to talk about things you truly understand, talk about the pits you stepped into, and that is valuable enough. If you have the opportunity to share, even if you are nervous, it is worth trying once.

## About Participating in the C++ Committee

The C++ committee is recruiting. The work of the committee requires the participation of people at all levels—not just experts in language design, but also feedback from actual users, people to test proposals, write use cases, and report problems. You don't need to be Bjarne Stroustrup to get in, you just need passion and willingness to invest time.

## A Final Small Interlude

There is a very real detail in the Q&A session: the speaker referred to Barry Revzin as the person responsible for Ranges, only to be corrected on the spot—Barry Revzin has recently done a lot of work on the application layer of C++26 Reflection (he gave a speech "Practical Reflection With C++26" at CppCon), while the main author of Ranges is Eric Niebler (the speaker misspoke it as Eric Kneedler). However, strictly speaking, the main drivers of the Reflection proposal are Daveed Vandevoorde and Herb Sutter, etc., while Revzin is more at the application and teaching level. This kind of "mixing up people's names and responsible areas" is very common; the C++ Standards Committee involves too many people and sub-working groups, and even frequent participants may not be able to figure it all out. The speaker self-deprecatingly said "I am really terrible," this sense of reality actually makes people feel that this community is very down-to-earth.

## The Threshold for Participating in the Community

The C++ community is not a closed circle; it is composed of every person currently using C++. The simplest contribution might just be sharing what you learned today with a colleague next to you, or answering a novice's question in the community. You don't have to wait until you are "strong enough" to participate—because by then you may have forgotten the confusion of the novice stage, and it is precisely those confusions that are the most valuable sharing content.

---

# The "Never Execute" Instruction in ARM32 Condition Codes—Orthogonal Design and Its Demise

This Q&A session involves an interesting architectural design question. In the ARM32 instruction set, every instruction has a four-bit condition code field in front. You can write `ADDNE` (add if not equal) or `MOVEQ` (move if equal) without writing a separate branch instruction, resulting in very high code density. Among the condition codes, there is an `AL` (Always, always execute), corresponding to `0b1110`; but there is also a condition code where all four bits are 1, i.e., `0b1111`, called `NV` (Never), meaning "Never." An instruction that "never executes"—writing it is just taking up space, right?

::: warning Important Correction
The NV condition code only exists in **ARMv4 and earlier versions**. Starting from ARMv5, NV was officially deprecated, and the `0b1111` encoding was reassigned for unconditional instruction extension. On ARMv7-A, using the condition code `NV` results in **UNPREDICTABLE** behavior, no longer guaranteeing "never execute." The verification experiments later in this article need to target the ARMv4 architecture to get the expected results. ARM official documentation text:

> "Every conditional instruction contains a 4-bit condition code field, the cond field, in bits 31 to 28. This field contains one of the values **0b0000 – 0b1110**."
>
> — ARM Architecture Reference Manual ARMv7-A/R, Section "The condition code field"<RefLink :id="5" preview="Arm Developer, Condition Codes: Conditional Execution" />

Actual verification results (arm-none-linux-gnueabihf-gcc 15.2 + qemu-arm-static):

```text
$ arm-none-linux-gnueabihf-gcc -march=armv4 -std=c17 -O2 -static test.c -o test
$ qemu-arm-static ./test
Result: 0
```

Verification code in repository: [05-01-arm32-nv-condition.c](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/blob/main/code/volumn_codes/vol10/cppcon/2025/02-some-assembly-required/05-01-arm32-nv-condition.c).
:::

## Orthogonality—The Design Philosophy of ARM32

The key lies in the design philosophy of ARM32: **extreme orthogonality**<RefLink :id="5" preview="Arm Developer, Condition Codes: Conditional Execution" />. Simply put, orthogonality means "every dimension of choice is independent and can be freely combined." In ARM32, the dimension of condition codes is designed very thoroughly—every condition has its logical opposite. Equal (EQ) is the opposite of Not Equal (NE), Greater or Equal (GE) is the opposite of Less Than (LT), Unsigned Higher (HI) is the opposite of Unsigned Lower or Same (LS)... and so on.

So what is the logical opposite of "Always Execute" (AL)? Naturally, it is "Never Execute" (NV).

Since four bits can represent 16 states, the designers of the condition codes filled all 16 states, and each has a corresponding meaning. This isn't "deliberately leaving a useless one," but the inevitable result of pushing orthogonality to the extreme—it's impossible to keep just 15 and leave one empty, that wouldn't be orthogonal. The price is: in the entire instruction encoding space of ARM32, a full sixteenth (1/16) of the encodings correspond to instructions that "do nothing at all." This is a design trade-off—using a little space waste in exchange for conceptual perfect symmetry of the instruction set.

This design was indeed the case in the original ARM (ARMv1 to ARMv4). But subsequent versions of ARM prove that "orthogonal to the extreme" also has a price.

## Hands-on Verification: Writing a "Never Execute" Instruction (ARMv4)

We can verify this thing ourselves<RefLink :id="6" preview="Arm Developer, Condition Codes: Condition Flags and Codes" />. Since the NV condition code is only valid in ARMv4 and earlier, we need to specify the architecture version explicitly.

::: details Why can't we use ARMv7?
The valid condition code range for ARMv7-A is only `0b0000`–`0b1110`. The encoding `0b1111` was reassigned in ARMv5+—it is either interpreted as a completely different instruction (using condition code bits to extend opcode space) or produces UNPREDICTABLE behavior. Using `NV` on ARMv7 **does not guarantee** the result is "never execute." The verification code has been placed in the repository ([05-01-arm32-nv-condition.c](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/blob/main/code/volumn_codes/vol10/cppcon/2025/02-some-assembly-required/05-01-arm32-nv-condition.c)), and readers can compare and test on ARMv4 and ARMv7 targets themselves.
:::

The environment is Arch Linux WSL, using the cross-compilation toolchain `arm-none-linux-gnueabihf-gcc` (Arm GNU Toolchain 15.2). Note that when compiling, you need to use `-march=armv4` to ensure the semantics of the NV condition code:

First, write a simplest C file:

```c
// test.c
#include <stdio.h>

int main(void) {
    int result = 0;
    printf("Result: %d\n", result);
    return 0;
}
```

Compile it to assembly to see what a normal `MOV` looks like (note here we use `-march=armv4`):

```bash
arm-none-linux-gnueabihf-gcc -march=armv4 -S -O2 test.c -o test.s
```

Now we manually construct a "Never Execute" `MOV`. In the ARM32 `MOV` instruction encoding format, the high four bits are the condition code. The machine code for a normal `MOV R0, #5` can be seen using `objdump`:

```bash
$ arm-none-linux-gnueabihf-objdump -d test.s
...
e3a00005: mov r0, #5
```

See `e3a00005`? The high four bits are `e`, which is binary `1110`, corresponding to the condition code `AL` (Always). Now change the high four bits from `e` to `f`, i.e., from `1110` to `1111`. On ARMv4, this is a "Never Execute" `MOV`—it is decoded, the CPU recognizes it as a MOV instruction, but because the condition code is NV, it never actually executes.

::: warning Reminder again
This instruction only behaves as "never execute" on ARMv4 and earlier. If `MOVNV` is executed on ARMv5+ (including ARMv7-A), the behavior is UNPREDICTABLE.
:::

Use inline assembly to stuff the machine code directly to verify:

```c
// test_nv.c
#include <stdio.h>

int main(void) {
    int result = 0;
    // MOVNV R0, #5 -> Machine code: f3a00005
    // High 4 bits 'f' (1111) is NV (Never)
    asm volatile (
        ".inst 0xf3a00005 \n\t"
        : "=r"(result)
    );
    printf("Result: %d\n", result);
    return 0;
}
```

Compile and run (note `-march=armv4`):

```bash
$ arm-none-linux-gnueabihf-gcc -march=armv4 -std=c17 -O2 -static test_nv.c -o test_nv
$ qemu-arm-static ./test_nv
Result: 0
```

`result` is still 0—that `MOV` instruction was fully decoded, but the CPU looked at the condition code, saw it was `NV`, and skipped it directly, doing nothing. `result` maintained its previous value of 0.

Here is a pitfall: if the output constraint `"+r"(result)` wasn't added, the compiler might optimize `result` away directly, and no matter how you run it, it's 0, easily mistaking it for a wrong machine code.

## By the Way: The TEQ Instruction

The Q&A also mentioned an instruction called `TEQ`. `TEQ` itself stands for "Test Equivalence," performing an XOR operation and setting flags, used to compare whether two values are equal (without changing register values, only changing flags). `TEQP` with the `P` suffix is an instruction in old ARM (pre-ARMv4) used to directly operate the Processor Status Register (PSR)—in modern ARM it has been replaced by `MSR`/`MRS` instructions.

## Summary

The "no-op" instruction encoding, one-sixteenth of the space in ARM32 (ARMv4 and earlier), is not a bug, not a legacy issue, but an inevitable by-product of extreme orthogonal design. The designers chose conceptual perfect symmetry, and the price was wasting some encoding space.

But ARM's own subsequent evolution explains everything: ARMv5 deprecated the NV condition code and reclaimed the `0b1111` encoding space; ARM64 (AArch64) completely cut the condition code field. "Orthogonal to the extreme" is conceptually beautiful, but ARM's practice proves that in actual evolution, encoding space and instruction set simplicity ultimately triumph over conceptual perfect symmetry. After understanding this design history, the experience of reading assembly manuals will be completely different.

---

# Should I Learn x86 or RISC-V Assembly?

When tinkering on Compiler Explorer, we often struggle with a question: x86 assembly looks like gibberish—`%r15`, `rbx`, register names are long and irregular; switching to RISC-V looks much more understandable, registers are just `x0` to `x31`, and the instruction format is much more regular. But how much of a gap is there between looking at RISC-V assembly and the x86 code actually running at work? Will I have watched it for nothing?

## Conclusion: It Depends on the Optimization Level

There is no one-size-fits-all answer to this; the key lies in the optimization level selected in Compiler Explorer. If it is `-O0` (no optimization), there isn't much difference between looking at x86 or RISC-V. What the compiler does under `-O0` is very "generic"—it honestly translates C++ statements into machine instructions one by one, pushing the stack when it should, storing to memory when it should, regardless of the architecture, this is the routine. At this level, what you learn—"what the compiler turned the code into"—is indeed interchangeable knowledge between architectures.

Verify with a simple function:

```cpp
int add_mul(int a, int b, int c) {
    int x = a + b;
    return x * c;
}
```

Under `-O0`, although the instructions of x86 and RISC-V are different, the "flavor" is exactly the same—both first store parameters on the stack, then load them back from the stack to do addition, store the result back to the stack, and finally load it out to do multiplication. The compiler is very honest without optimization, and it doesn't do any smart things. This cognition has nothing to do with the architecture.

## When it Reaches -O2 and Above, Things Change

When the optimization level is pulled to `-O2` or even `-O3`, the differences between architectures begin to appear systematically. The assembly you see is no longer purely "compiler's general optimization strategy," but mixed with a lot of "specialized optimizations for this architecture's specific instruction set."

Take a typical example—counting the number of 1s in an integer, popcount:

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

This code, thrown into x86's Compiler Explorer under `-O3`, the compiler directly replaces it with a `popcnt` instruction. The entire loop is gone, and the function body is just one instruction. But switch to RISC-V—the loop is still there. The base RISC-V instruction set doesn't have a `popcnt` instruction (although some extensions do), so the compiler can't do this replacement, and can only honestly use a loop or a lookup table to
