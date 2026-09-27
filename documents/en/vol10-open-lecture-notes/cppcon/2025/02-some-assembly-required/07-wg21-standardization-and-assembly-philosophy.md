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
  translated_at: '2026-09-26T15:58:46+00:00'
  engine: anthropic
  token_count: 13000
  notes: '原文一处事实疑点按原文照译：「CppCon 在夏威夷办过」——CppCon 历届均在 Bellevue/Aurora 举办，从未在夏威夷；疑为演讲者实际说的是 WG21 会议（曾在夏威夷 Kona 举行）。'
---
# WG21 and the Organizational Chain Behind the C++ Standard

In technical articles and videos we constantly run into the abbreviation "WG21," but hardly anyone walks through the complete organizational chain from top to bottom. As it turns out, the hierarchy has many layers, yet the structure itself is not complicated — so let's straighten this chain out first. That way, when we later look at proposals and standards documents, we at least know where these things come from and who is in charge of them.

## Start with a Counterintuitive Fact

ISO's full name is **International Organization for Standardization** (note the American spelling "Organization," and note that the last word is "Standardization," not "Standards")<RefLink :id="10" preview="ISO, About Us" />. The abbreviation ISO does not come from the English name — in English the acronym would be IOS, and in French OIN (Organisation Internationale de Normalisation). The founders decided neither IOS nor OIN was good enough, so they picked the Greek word isos (equal) as the unified abbreviation, so that the name is ISO no matter the language. This bit of trivia has no direct bearing on C++ itself, but it explains why the abbreviation does not match the English full name.

::: details Reference Text
From the "About us" page of ISO's official website<RefLink :id="10" preview="ISO, About Us" />:

> "ISO, the **International Organization for Standardization**, brings global experts together to agree on the best ways of doing things."
>
> "Because 'International Organization for Standardization' would have different acronyms in different languages ('IOS' in English, 'OIN' in French for Organisation internationale de normalisation), our founders decided to give it the short form 'ISO'. ISO is derived from the Greek word isos (meaning 'equal')."

Readers can visit iso.org/about-us.html and verify this themselves.
:::

## How Many Layers Sit Between ISO and C++

ISO does not directly manage C++. It first set up a joint body with another organization, the IEC (International Electrotechnical Commission), called JTC1 — short for Joint Technical Committee 1, "joint technical committee number one," which oversees information technology standards.

Under JTC1 there are subcommittees, and ours is SC22 (Subcommittee 22), whose full scope is "programming languages, their environments and system software interfaces." Note the scope — not just programming languages, but also "environments" and "system software interfaces," which is why a whole pile of things hangs under SC22.

Under SC22 come the working groups — the WGs. Many WGs have already grayed out — they completed their historical mission once the corresponding language standard was finished. But the ones still active read like a roster: COBOL, Fortran, Ada, C, Prolog, the Linux-related one, programming language vulnerabilities research, and the one we care about most, C++.

C++ lives here as WG21. Why number 21? The number was assigned historically and carries no special meaning — it just happened to be the number when C++'s turn came up.

## A Fact Worth Noting

Purely by participation in standards development, C++'s WG21 is the largest group in all of SC22 (per the speaker's observation: plotted by participant count, the other languages' working groups would be a few dots, while C++ would fill the entire chart). That is not to say the other languages do not matter — Fortran and Ada remain irreplaceable in their own domains (scientific computing, aerospace). But the sheer headcount also directly explains why C++ standardization has the speed and complexity it has — many proposals, much discussion, and plenty of controversy.

## The Whole Chain, Summarized

From top to bottom: ISO and IEC jointly created JTC1 (Joint Technical Committee 1, in charge of information technology), JTC1 set up SC22 (Subcommittee 22, in charge of programming languages and related matters), and SC22 set up WG21 (Working Group 21, dedicated to C++)<RefLink :id="2" preview="ISO/IEC JTC1/SC22/WG21, Official Page" />.

The full formal designation is ISO/IEC JTC1/SC22/WG21.

## Why This Chain Is Worth Understanding

Once this chain is clear, when we see the WG21 mark on a proposal document we know it went through the formal standards-development process under the ISO framework — not something somebody decided on a whim. "The C++ standard" stops being a vague notion and becomes an entity backed by a concrete organizational structure. In hindsight it is just a few nested committees, nothing mystical — but if you do not know that, the whole thing feels like a fog.

---

# The Full Journey of a Proposal, from Idea to C++ Standard

Most people's understanding of "how the C++ standard gets made" probably stops at "a bunch of luminaries meet and vote." In reality the whole process is a very rigorous funnel mechanism: many tiers, but every step has a clearly bounded responsibility.

## First, What Actually Sits Under WG21

When we say "the C++ standards committee," we mean WG21. WG21 is not one flat, giant group; a pile of sub-organizations hangs under it — some handle administration, some the core specification, some the direction of evolution — plus a bunch of SGs (Study Groups) whose abbreviations we keep seeing on proposal documents without necessarily knowing what they do. These study groups are not frozen in place: some are active and open to new members, while others have completed their mission and are fully wound down. But watch out for one mental trap — seeing "wound down" and concluding that nobody will ever propose anything in that direction again. Winding down only means the study group itself no longer needs to exist; its conclusions may have been taken over by another group, or may be shelved for now. The classic example is UB (undefined behavior): the relevant study group is closed, yet proposals about UB are still everywhere across the other groups — after all, it is the pain nobody writing C++ gets around.

## How Far an Idea Travels from Someone's Head into the Standard

This part is the most interesting bit of the whole process. An idea about how C++ should change has to make it through a complete funnel mechanism to get from someone's head into the standard.

Step one: write the idea up as a formal proposal document and send it to a mailing list called the reflector. "Reflector" sounds exotic, but it is just a mailing list with an old-fashioned name. Once sent, the proposal gets routed to the corresponding study group (SG). Inside the SG, the domain experts review it and give feedback; the authors go back, revise, resend, discuss again — polish, over and over. What this stage is essentially doing is validating, in a small circle, whether the idea actually holds up.

Once the SG discussion has essentially matured, the proposal needs to "level up" and face a wider field of view: how does it fit into the whole C++ ecosystem? Here the path forks — if the feature is library-level (say, a new utility in a header), it goes to LEWG, the Library Evolution Working Group; if it is language-level (say, a new syntax rule), it goes to EWG, the Evolution Working Group. The difference between LEWG and LWG: LEWG handles "evolution," discussing whether the feature is worth doing and what shape makes the most sense; LWG is the "core" group whose turn comes later, responsible for the exact standard wording.

In the evolution groups the proposal goes through another round of polishing, and only when everyone agrees the direction is right and the details are mostly in place does it flow from the evolution group into the core group. Library features enter LWG, language features enter CWG. What the core groups do is hardcore in the literal sense — they edit the C++ standard document directly, translating the proposal into normative text precise down to the punctuation.

Finally, assuming everyone along the way is satisfied with the change, the proposal enters the plenary vote. The whole of WG21 votes together, and once it passes, the feature appears in the next version of the C++ standard. From idea to landing can take years of iteration.

## The Core of the Whole Process

With this process clear, the SGxx, EWG, and LWG abbreviations on proposal documents stop being such a headache<RefLink :id="3" preview="ISO C++ Foundation, The Committee: WG21" />. Opening a proposal, we can deliberately check which stage it is in — still in an SG means early exploration with a lot of design in flux; already in LWG/CWG basically means the direction is settled and only wording-level fine-tuning remains.

One easily missed detail: the move of a proposal flowing from an evolution group (EWG/LEWG) into a core group (CWG/LWG) is called "forward" in committee jargon. Reading meeting minutes, you constantly run into sentences like "LEWG decided to forward Pxxxx to LWG" — that "forward" is saying the proposal advanced one step down the pipeline.

The whole process is essentially a layered peer-review mechanism — validate feasibility in a small circle first, then examine ecosystem impact in a bigger circle, and finally have the most rigorous people finalize the wording. Every step has a clearly bounded responsibility. Slow, yes — but genuinely steady.

---

# Just How Slow Is C++ Standardization — A Cross-Language Comparison

When the C++ standardization timeline comes up, the instinct is that C++23 should have come out in 2023 and C++26 in 2026. In fact, C++23's technical work finished in early 2023, but ISO's official publication dragged on until **October 2024** (standard number ISO/IEC 14882:2024)<RefLink :id="11" preview="ISO, ISO/IEC 14882:2024" />, and the C++26 draft still has a pile of items under discussion, so the final version will most likely slip further. Each release's span from kickoff to publication is far longer than most people imagine — one more facet of just how enormous the C++ standardization project is.

::: details Reference Text
ISO's official standards page<RefLink :id="11" preview="ISO, ISO/IEC 14882:2024" /> (iso.org/standard/83626.html):

> Status: Published
> Publication date: **2024-10**
> Edition: 7
> Number of pages: 2104

isocpp.org/std/the-Standard<RefLink :id="3" preview="ISO C++ Foundation, The Committee: WG21" />:

> "The current ISO C++ standard is C++23, formally known as ISO International Standard **ISO/IEC 14882:2024(E)** – Programming Language C++."

Readers can visit iso.org/standard/83626.html to verify the publication date.
:::

So how do other languages do it? Each one's path differs quite a bit, and the side-by-side comparison actually makes it easier to understand why C++ is this slow.

Rust first. Rust's philosophy is the opposite of C++'s. The C++ model is an ISO standard document, written in extreme detail, which the GCC, Clang, and MSVC teams each implement, all striving to converge on the standard. Rust has essentially one implementation, rustc. More precisely, the test cases in the Rust source repository are themselves the specification — the "standard" is executable. Write a piece of code: if it passes that test suite in the Rust source, it is legal Rust. In the C++ world we constantly hit cases where "what the standard says" and "what the compiler actually does" disagree; Rust dissolves that problem outright with test cases.

The direct benefit of this model is speed. The Rust team wants a feature: edit the compiler, write the tests, open a PR, get a review, merge — and when the next release ships, everyone has it. There is no "three compilers implement it at different paces, one supports it and another does not." They do have a leadership council and an RFC-like proposal process, but the whole thing is far lighter than C++'s. The "single implementation plus tests-as-specification" model is a key reason Rust can keep a six-week release cadence.

Now Python. For a long time, Guido van Rossum (Python's father) played "benevolent dictator for life" — which direction the language went, which features went in or stayed out, was ultimately his call. The controversial walrus operator `:=`, for instance, was pushed through during his tenure. Then in 2018 Guido himself stepped back, and behind that lies a very real problem: once a language community grows past a certain size, having one person make the final calls gets harder and harder, and the rifts inside the community get wider. Today Python runs on a community-governance model with a five-person steering council; the proposal mechanism is the PEP (Python Enhancement Proposal) — somewhat similar to C++'s proposal process but noticeably less formal. They aim for a release a year and have basically delivered. By comparison, Python's process is quite a bit lighter than C++'s but heavier than Rust's — it sits in the middle.

Finally, JavaScript. Nominally there is a standards body behind JavaScript called ECMA, and in some contexts JavaScript is called ECMAScript — that is its technically formal name. But in actual experience, JavaScript's evolution is mostly pushed along by the V8 engine (the engine behind Chrome) and the Node.js ecosystem; the ECMA standard is more of an after-the-fact ratification of "what everyone is already using." That is nearly the reverse of C++'s "write the standard first, then implement it" path.

Put together, these form a rather interesting spectrum. On one end, Rust: "the implementation is the standard," iterating extremely fast. In the middle, Python and JavaScript: they have standardization processes but relatively lightweight ones, with the real driving force usually coming from the implementation side. On the other end, C++: first write an extremely detailed specification, then have multiple compilers implement it separately — the standards committee itself does no implementation at all. Every model has its cost — Rust's is essentially no freedom to choose a compiler; C++'s is that a feature can take years from proposal to actually being usable.

C++ standardization is slow not because the committee's people are not trying, but because the "specification first, multiple implementations" framework itself makes speed impossible. Whether that cost is worth paying is a topic for another day.

---

# How the C++ Standards Committee Works, and How to Take Part in the Community

Around the C++ standardization process, plenty of stories circulate — "the C++ standard is controlled by big corporations," "the proposals are secretly steered by vendors," and so on. From what one actually finds, while vendors do participate (implementing a compiler takes enormous engineering resources, and whoever invests the people naturally gets a voice), there is a formal committee process behind it all: proposals go through drafts, votes, reviews, and more — it is not a shouting match where the loudest voice wins. The process is relatively lightweight; it lacks the extremely rigid governance structures some languages have — but lightweight does not mean lawless.

It is also easy to feel that the grass is greener next door: watching Rust's RFC process look so orderly and transparent, we grumble about why C++ will not learn from it. But with hindsight, what C++'s model has bought is longevity — this language has walked from the 1980s to today, surviving countless technology waves, alive and doing well. Every governance model has its trade-offs.

## The People Investing Behind the Scenes

How much the standards committee's members invest is often underestimated. Many people working on proposals pour enormous amounts of their personal time — not work hours, personal time — into making this language better. Writing proposals, responding to review comments, hammering out details on mailing lists, flying around the world to attend meetings in person: most of it comes with no extra pay. CppCon has even been held in Hawaii, and someone came back saying they spent the entire time in their hotel room working through proposals. Then there are the companies sponsoring engineers to participate in standardization, and the families supporting a member's travel — these support structures are invisible, but without them the whole ecosystem would grind to a halt.

## The Value of In-Person Conferences

Per the speaker, 2025 has eleven large international C++ conferences — the most in history. There was an obvious dip during COVID, but the recovery was fast and the curve keeps rising — a sign the community is alive. Watching talks online has its value, but sitting in a room with a group of people, and at the coffee break asking "how's range-v3 working out in your project" or "did you hit that MSVC trap too" — that information density and sense of connection is something a screen cannot deliver.

If you are still hesitating about attending an in-person C++ conference or a meetup, go try one — even just a local half-hour talk.

---

## What Local Meetups Actually Look Like

By isocpp.org's count, the number of C++ meetups registered worldwide has already passed one hundred thirty, which means that wherever you live, odds are there is one within a hundred miles. China's first-tier cities basically all have them, and second-tier cities are slowly appearing too. If you truly cannot find one, starting your own is perfectly fine — no formal process required. Someone literally posted in a group chat, "I'll be sitting somewhere Friday night with my laptop, talking C++; come if you like." Four people showed up the first time; it later settled at around ten, once a month, reading each other's code and working through problems together.

More formal formats exist too: a large company sponsors the venue and brings in an outside speaker for a technical talk, with slides and Q&A; lightning-talk format, where everyone gets five to ten minutes on a war story or a small trick — fast-paced and information-dense. Some companies even hold regular internal tech-sharing hours.

One practical benefit of in-person conversation: the technical solutions and pitfalls under discussion are largely unsearchable online — the material is not "systematic" enough to merit a blog post, yet precisely this fragmented, from-the-trenches experience tends to be the most useful.

---

# Online Communities and Resources

Many people spend their early days with C++ grinding away alone. Hit a compile error, search for it; nothing found, rewrite around it. After a while in that state, you often realize the bottleneck is not how hard you are trying, but whether you have found the right circle.

## Online Communities

The vibe of the online communities is far better than you would expect. The C++ Slack (run by the C++ Alliance) has finely divided channels, and you can join whichever match your interests. Discord has even more choice: the Compiler Explorer Discord and servers dedicated to C++ standard proposals are both active discussion venues. Beginners and experts sharing the same space genuinely exists in the C++ community — someone two months into learning asks about pointers in the Slack `#beginners` channel and several people patiently explain; an ISO committee member discusses proposal details with people on Discord.

The practical advice: do not barge in asking questions — lurk for a few days first, watch how others ask and answer, get a feel for the community's rhythm. You will learn a lot just lurking.

## cppreference — A Community-Driven Reference

cppreference<RefLink :id="4" preview="cppreference.com, C++ Reference" /> is a community-driven, community-run reference site; every page and every code sample on it is maintained by actual people. It is not official documentation sponsored by some big company — it is volunteers doing the work. Under normal circumstances community members can edit and extend it, which is exactly why it stays high quality — not one person writing, but countless people maintaining it together. Whenever you look up a standard library component, take a moment to read the notes and discussion at the bottom of the page; there is often genuinely valuable information there, like a known issue with a function on a particular compiler.

## Code-Sharing Platforms

Beyond real-time chat communities, code-sharing platforms like Compiler Explorer<RefLink :id="7" preview="Compiler Explorer, godbolt.org" /> are crucial to technical exchange. Drop your code in, generate a link, throw it anywhere — Discord, Slack, forums, or straight to a colleague. Compared with pasting a wall of code as text, a Compiler Explorer link lets the other person view, edit, and run it in one click — a completely different level of efficiency.

When debugging a problem, first put the minimal reproducer on Compiler Explorer, confirm it reproduces across several compilers, and then go ask the community — the benefit is that nobody helping you has to set up an environment; one click on the link shows them exactly what you see.

## The Community Is the Heart of the C++ Ecosystem

What makes C++ fascinating is not just that the language is powerful, but the people behind it. The ones quietly submitting patches to open-source projects, the ones spending their own time maintaining cppreference, the ones paying out of pocket to organize local meetups, the ones still helping a beginner debug at three in the morning on Discord — these people are what make up the C++ ecosystem. Soaking in the community, what you see is not just answers to questions, but how other people think about problems, how they go about solving them, even their attitude toward the craft.

---

# Getting Involved in the C++ Community — More Than One Way to Contribute

About "participating in open-source communities," many people hold a narrow picture — that it is something only qualified people get to do, something for the big names listed in the committee or the authors of famous libraries. In reality, the ways to participate are far more diverse than that.

## "Contribution" Is Broader Than We Think

Contributing to the C++ community does not require writing a widely used library, or submitting a proposal to the standards committee and getting it adopted. The participation modes mentioned in the talk are things you can do right now: if your city has no C++ meetup, just start one — you do not need to be an expert, only someone willing to get people together to talk C++. Attend a conference, even if only to listen and to meet a few other people using C++ — that alone already counts as participating in the community. Write up a pit you fell into and publish it so the people after you take fewer wrong turns — that is a contribution too.

## On Taking the Stage

There is a very honest description in the talk — standing on the stage, looking back at a sea of faces staring at you, thinking "why do I keep doing this to myself." Giving a technical talk does not require being perfect; just talk about what you genuinely figured out and the pits you fell into — that alone is valuable. If a chance to speak comes up, nerves and all, it is worth trying once.

## On Joining the C++ Committee

The C++ committee is recruiting. The work needs people at every level — not only language-design experts, but feedback from real users, people to test proposals, write use cases, and report problems. You do not need to be Bjarne Stroustrup to get in; enthusiasm plus a willingness to put in the time is enough.

## One Final Anecdote

A detail from the Q&A is delightfully human: the speaker called Barry Revzin "the person in charge of Ranges" and got corrected on the spot — Barry Revzin has recently done a lot of work on the application side of C++26 Reflection (he gave a "Practical Reflection With C++26" talk at CppCon), while Ranges' primary author is Eric Niebler (the speaker misspoke it as "Eric Kneedler"). Strictly speaking, though, the main drivers of the Reflection proposal are Daveed Vandevoorde, Herb Sutter, and others; Revzin's work is more at the application and teaching level. Mixing up names and areas like this is common — the C++ standards committee involves so many people and sub-working-groups that even the regulars cannot keep it all straight. The speaker's self-deprecating "I'm terrible" — that kind of honesty is exactly what makes this community feel down-to-earth.

## The Barrier to Joining the Community

The C++ community is not some closed circle; it is made up of everyone currently using C++. The simplest contribution might be sharing what you learned today with a colleague, or answering a beginner's question in the community. Do not wait until you are "strong enough" to participate — by then you may have forgotten the confusions of the beginner stage, and those confusions are precisely the most valuable content you could share.

---

# The "Never Execute" Instruction in ARM32 Condition Codes — Orthogonal Design and Its Demise

This Q&A segment touches on a fascinating architecture design question. In the ARM32 instruction set, every instruction carries a four-bit condition code field up front, so you can write `ADDNE` for "add if not equal" or `MOVEQ` for "move if equal" — no separate branch instruction needed, and code density is high. Among the condition codes is `AL` (Always), encoded 0b1110; but there is also a condition code with all four bits set — 0b1111 — called `NV`, for "Never." An instruction that never executes — isn't writing one just wasted space?

::: warning Important Correction
The NV condition code exists only in **ARMv4 and earlier**. Starting with ARMv5, NV is formally deprecated, and the `0b1111` encoding was reallocated for unconditional-instruction extensions. On ARMv7-A, the behavior of condition code `0b1111` is **UNPREDICTABLE** — "never executes" is no longer guaranteed. The verification experiments later in this article must target ARMv4 to get the expected results. The official ARM documentation reads:

> "Every conditional instruction contains a 4-bit condition code field, the cond field, in bits 31 to 28. This field contains one of the values **0b0000 – 0b1110**."
>
> — ARM Architecture Reference Manual ARMv7-A/R, Section "The condition code field"<RefLink :id="5" preview="Arm Developer, Condition Codes: Conditional Execution" />

Actual verification results (arm-none-linux-gnueabihf-gcc 15.2 + qemu-arm-static):

```bash
# ARMv4: NV works as documented
$ arm-none-linux-gnueabihf-gcc -static -march=armv4 test.c && qemu-arm-static ./a.out
AL (always): result = 42
NV (never):  result = 0         # ← as expected: NV skipped the MOV

# ARMv7: raises SIGILL right away (illegal instruction exception)
$ arm-none-linux-gnueabihf-gcc -static -march=armv7-a test.c && qemu-arm-static ./a.out
qemu: uncaught target signal 4 (Illegal instruction) - core dumped
```

Verification code in the repository: [05-01-arm32-nv-condition.c](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/blob/main/code/volumn_codes/vol10/cppcon/2025/02-some-assembly-required/05-01-arm32-nv-condition.c).
:::

## Orthogonality — The ARM32 Design Philosophy

The key is ARM32's design philosophy: **radical orthogonality**<RefLink :id="5" preview="Arm Developer, Condition Codes: Conditional Execution" />. Orthogonality, loosely put, means "the choices along each dimension are independent and freely combinable." In ARM32, the condition-code dimension was taken all the way to completion — every condition has its logical opposite. The opposite of equal (EQ) is not-equal (NE); of greater-or-equal (GE), less-than (LT); of unsigned higher (HI), unsigned lower-or-same (LS)… and so on.

Then what is the logical opposite of "always" (AL)? "Never" (NV), of course.

Because four bits can encode sixteen states, the condition-code designers filled all sixteen, each with its own semantics. This is not "deliberately keeping something useless" — it is the inevitable result of pushing orthogonality to its limit. You cannot keep fifteen of the states and leave the sixteenth unassigned; that would break the orthogonality. The cost: across ARM32's entire instruction encoding space, a full one-sixteenth of the encodings all correspond to instructions that "do nothing." It is a design trade-off — a little wasted space bought perfect conceptual symmetry in the instruction set.

That design held in the original ARM (ARMv1 through ARMv4). But ARM's later generations proved that "orthogonality pushed to the extreme" has a price of its own.

## Try It Yourself: Writing a "Never Execute" Instruction (ARMv4)

We can verify this with our own hands<RefLink :id="6" preview="Arm Developer, Condition Codes: Condition Flags and Codes" />. Because the NV condition code is only valid on ARMv4 and earlier, we need to pin the architecture version explicitly.

::: details Why can't we use ARMv7?
ARMv7-A's valid condition codes only span `0b0000`–`0b1110`. The encoding `0b1111` was reallocated in ARMv5+ — it is either interpreted as an entirely different instruction (using the condition-code bits to extend the opcode space) or produces UNPREDICTABLE behavior. Using `.word 0xf3a0002a` on ARMv7 **does not guarantee** a "never executes" result. The verification code is in the repository ([05-01-arm32-nv-condition.c](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/blob/main/code/volumn_codes/vol10/cppcon/2025/02-some-assembly-required/05-01-arm32-nv-condition.c)); readers can run the comparison on ARMv4 and ARMv7 targets themselves.
:::

The environment is Arch Linux WSL, with the cross toolchain `arm-none-linux-gnueabihf-gcc` (Arm GNU Toolchain 15.2). Note that compiling needs `-march=armv4` to preserve the NV condition-code semantics:

First, a minimal C file:

```c
// test_nv.c
void foo(void) {
    __asm__ volatile("mov r0, #42");
}
```

Compile it to assembly and see what a normal `MOV` looks like (note we pass `-march=armv4` here):

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

Now we hand-craft a "never execute" `MOV`. In the ARM32 `MOV` instruction encoding format, the top four bits are the condition code. We can look at the machine code of a normal `MOV R0, #42` with `objdump`:

```bash
$ arm-none-linux-gnueabihf-gcc -c -march=armv4 test_nv.c -o test_nv.o
$ arm-none-linux-gnueabihf-objdump -d test_nv.o

test_nv.o:     file format elf32-littlearm

Disassembly of section .text:

00000000 <foo>:
   0:   e52db004        push    {r7}
   4:   e24db000        sub     r7, sp, #0
   8:   e3a0002a        mov     r0, #42     ; note this: 0xe3a0002a
   c:   e320f000        nop
  10:   e49db004        pop     {r7}
  14:   e12fff1e        bx      lr
```

See `0xe3a0002a`? The top four bits are `0xe` — binary `1110`, condition code `AL` (Always). Now change the top four bits from `1110` to `1111`, i.e. from `0xe3a0002a` to `0xf3a0002a`. On ARMv4 this is a "never execute" `MOV R0, #42` — it gets decoded, the CPU recognizes it as a MOV instruction, but because the condition code is NV, it never actually executes.

::: warning One More Reminder
This instruction behaves as "never executes" only on ARMv4 and earlier. Executing `0xf3a0002a` on ARMv5+ (including ARMv7-A) is UNPREDICTABLE.
:::

Stuff the machine code in directly with `.word` to verify:

```c
// test_nv2.c
#include <stdio.h>

void foo(void) {
    int result = 0;
    // A normal MOV R0, #42 with condition code AL (0xe)
    __asm__ volatile("mov r0, #42" : "=r"(result));
    printf("AL (always): result = %d\n", result);

    result = 0;
    // The same instruction, hand-patched with condition code NV (0xf)
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

`result` is still 0 — that `MOV R0, #42` was fully decoded, but the CPU glanced at the condition code, saw `NV`, and skipped it, doing nothing. `result` kept its previous value, 0.

There is a pitfall here that is easy to step in: without the `=r`(result) output constraint, the compiler may optimize `result` away entirely, so it stays 0 no matter what — very easy to mistake for "the machine code must be wrong."

## By the Way: The TEQ Instruction

The Q&A also mentioned an instruction called `TEQP`. `TEQ` stands for "Test Equivalence"; it performs an XOR and sets the flags, and is used to compare two values for equality (it does not change register values, only the flags). `TEQP` with the `P` suffix is an old-ARM (pre-ARMv4) instruction for manipulating the processor status register (PSR) directly — replaced by the `MSR`/`MRS` instructions in modern ARM.

## Summary

That one-sixteenth of "do nothing" instruction encodings in ARM32 (ARMv4 and earlier) is not a bug, not legacy baggage, but the inevitable by-product of a radically orthogonal design. The designers chose perfect conceptual symmetry, paying for it with some wasted encoding space.

But ARM's own later evolution tells the rest of the story: ARMv5 deprecated the NV condition code and reclaimed the `0b1111` encoding space; ARM64 (AArch64) removed the condition field entirely. "Orthogonality to the extreme" is beautiful conceptually, but ARM's practice shows that in real-world evolution, encoding space and instruction-set simplicity eventually beat conceptual symmetry. Once you know this piece of design history, reading an assembly manual feels completely different.

---

# Learning Assembly: Should You Read x86 or RISC-V

When playing on Compiler Explorer, we often agonize over one question: x86 assembly looks like hieroglyphics — `mov rax, qword ptr [rdi + 8]`, register names long and patternless; switch to RISC-V and things look far more approachable — registers are just `x0` through `x31`, and the instruction formats are much more regular. But how big is the gap between reading RISC-V assembly and the x86 code that actually runs at work? Could hours of reading turn out to be wasted?

## The Answer: Which Architecture to Read Depends on the Optimization Level

There is no one-size-fits-all answer here; the key is the optimization level selected in Compiler Explorer. At `-O0` (no optimization), it barely matters whether you read x86 or RISC-V. What the compiler does at `-O0` is very "generic" — faithfully translate C++ statements into machine instructions one by one, push to the stack where told, store to memory where told; every architecture gets the same treatment. What you learn at this level about "what the compiler turned the code into" is genuinely transferable knowledge across architectures.

Verify with a simple function:

```cpp
int add_and_double(int a, int b) {
    int sum = a + b;
    return sum * 2;
}
```

At `-O0`, x86 and RISC-V outputs differ in their instructions but are identical in "flavor" — both first spill the arguments to the stack, load them back to do the addition, store the result to the stack, then load it out again to multiply. The compiler is honest at no optimization and does nothing clever; that insight is architecture-independent.

## At -O2 and Above, Things Change

Once the optimization level goes to `-O2` or even `-O3`, differences between architectures start showing up systematically. The assembly you see is no longer purely "the compiler's generic optimization strategy" — it is mixed with a large dose of "targeted optimization for this particular architecture's instruction set."

A classic example — popcount, counting the 1 bits in an integer:

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

Throw this code at x86 on Compiler Explorer under `-O2`, and the compiler replaces it outright with a single `popcnt` instruction. The entire loop is gone; the function body is one instruction. Switch to RISC-V — the loop is still there. The RISC-V base instruction set has no `popcnt` instruction (some extensions have it), so the compiler cannot make that substitution and has to fall back honestly on a loop or a table-lookup optimization. Same C++ code, same `-O2`, and the two architectures produce completely different assembly.

Learn assembly on RISC-V and you might conclude "the compiler does not auto-recognize popcount patterns"; learn it on x86 and you conclude the exact opposite. Who is right? Both, and neither — because this is not a difference in compiler capability, but a difference in the target architectures' instruction sets.

## A Practical Strategy

To sum up the strategy: if the goal of learning assembly is to understand "the compiler's high-level optimization decisions" — how inlining happens, how constant propagation happens, how dead-code elimination happens — then either architecture works, because those genuinely do transfer across architectures. When the compiler decides "should I inline this function," it weighs function size, call frequency, presence of side effects — high-level concerns with little to do with which CPU is running underneath.

But if the goal is to understand "what the instructions the compiler finally emits actually look like," then read the architecture you actually use at work. At `-O2` and above, every instruction you see may be an "architecture-specific shortcut" that simply has no counterpart on another architecture.

## Compiler Explorer's AI Features

Compiler Explorer has shipped an AI-assisted assembly-explanation feature, with mixed results. For simple instruction sequences — basic calling conventions, stack frame layout, that kind of thing — the AI explains things quite clearly. But faced with architecture-specific optimization tricks, like using `cmov` on x86 for conditional moves to avoid branch mispredictions, it sometimes gives a fairly generic explanation without pointing out "what architectural property this optimization is actually targeting." Use it as a beginner's crutch, not as an authoritative answer.

## Summary

People used to say "learn assembly on the cleanest architecture you can find," but clean to the point of disconnecting from real work breeds misconceptions instead. Better to face real x86 assembly from the start: the learning curve is steeper, but every single thing you learn is directly usable. RISC-V is excellent for "validating generic optimization logic" — run the same code on both architectures; if an optimization shows up on both, it is probably a generic compiler strategy; if it shows up on only one, it is probably an architecture-specific instruction substitution. That comparative method is far clearer than staring at one architecture's output alone.

---

# Rethinking Hand-Written Assembly — When to Touch It and When Not To

About assembly, two extreme attitudes are common: one says "the compiler handles it for you; ignore assembly," the other says "without hand-written inline assembly on the critical path, you don't really know C++." Both are wrong. The speaker put it well: he writes assembly nowadays mostly for the old computers he loves, because those architectures' assembly is still manageable — you can fit the whole thing in your head. The value of assembly is not "being smarter than the compiler," but "fully understanding what this machine is doing."

## Why Modern x86-64 Assembly Is Hard to "Fit in Your Head"

Compare instruction sets across eras and it becomes obvious. Today's x86-64 has dozens of encoding variants for `mov` alone — `mov rax, imm32` sign-extended to 64 bits, `mov r/m64, imm32` also sign-extended, `movzx`, `movsx`, `cmovcc` conditional moves… plus the whole AVX-512 apparatus of EVEX encoding prefixes, mask registers, and broadcast mechanisms. For an ordinary human, fitting the complete x86-64 instruction set "in their head" is basically an impossible mission.

The speaker brought up the Hitachi SH4 instruction set, saying that may be the limit of what one person can hold. The SH4 is a late-1990s RISC processor with 16-bit fixed-length instruction encoding and very clean addressing modes. The comparison makes it clear why assembly on old hardware feels so different — that is an instruction set "a human brain can fully master," while x86-64, after forty-some years of accumulated backward compatibility, has become a beast no one can fully comprehend. It is not that "assembly" itself is hard; it is that this particular platform's assembly is hard.

## When a Modern C++ Developer Should Touch Assembly

The speaker told a real story: at a company he visited, the compiler kept spilling registers in an absolutely hot loop, and no amount of option-tuning fixed it — in the end the team hand-wrote the entire loop in assembly, then maintained a C++ version and an assembly version to cross-check each other. It sounds exhausting, but it was a genuinely pragmatic engineering decision. Inline-assembly syntax differs between GCC and Clang, and precisely controlling the compiler's register allocation around it is hard — sometimes what you want is exactly "register usage in this code is entirely up to me," and a standalone assembly file is the cleanest answer.

But hand-written assembly carries a high maintenance cost. In an agile environment, one requirement change can force a complete rewrite of carefully hand-written assembly. Anyone who has hand-written SIMD intrinsics knows this pain too — you carefully design a loop around four `__m256i` registers, then a requirement changes, the data structure gains a field, and the register allocation collapses on the spot.

So the criteria are fairly clear: only when you have hit an extreme hotspot the compiler genuinely cannot handle, and profiling has confirmed the bottleneck is register spilling or the instruction sequence, and the hotspot is stable enough not to churn — only when all three conditions hold at the same time is hand-written assembly worth it. Otherwise, write honest C++ and let the compiler do the work.

## The Real Value of Learning Assembly — Reading Compiler Output

The greatest value of learning assembly is not being able to write it, but being able to read what the compiler produced. A concrete example:

```cpp
// Count occurrences of the character ch in buf (length known)
size_t count_char(const char* buf, size_t len, char ch) {
    size_t count = 0;
    for (size_t i = 0; i < len; i++) {
        if (buf[i] == ch) count++;
    }
    return count;
}
```

This function is as simple as they come. But drop it onto godbolt and compile with `-O3 -march=x86-64-v2`, and the compiler (GCC 16) auto-vectorizes it, using SSE instructions to compare 16 bytes at a time. If you cannot read assembly, you have no idea the compiler did this — and you might go hand-write SIMD optimizations yourself.

::: warning Correction to the Original Text
The original version of the example used a `while (*str)` null-terminated loop. In reality, **neither GCC nor Clang auto-vectorizes that pattern at `-O2` or `-O3`** — the position of the null character is a runtime property, so the compiler cannot safely read 16 bytes at once (it might read past the null character into unmapped memory). Only the known-length version (`for (i < len)`) gets vectorized.

Readers can verify with the following commands (environment: GCC 16.1.1, x86-64):

```bash
# while(*str) version — not vectorized
cat > /tmp/test.cpp << 'EOF'
#include <cstddef>
__attribute__((noinline))
size_t f(const char* s, char c) {
    size_t n = 0; while (*s) { if (*s == c) ++n; ++s; } return n;
}
EOF
g++ -O3 -march=x86-64-v2 -S /tmp/test.cpp -o /tmp/test.s
grep pcmpeqb /tmp/test.s   # no output = not vectorized
```

Verification code in the repository: [05-04-count-char-vec.cpp](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/blob/main/code/volumn_codes/vol10/cppcon/2025/02-some-assembly-required/05-04-count-char-vec.cpp).
:::

The real GCC output (core loop, simplified):

```asm
# Core loop emitted by GCC 16 -O3 -march=x86-64-v2 (simplified)
count_char:
    movd    %r8d, %xmm4            # lowest byte of ch into XMM4
    pxor    %xmm2, %xmm2           # XMM2 = all zeros (pshufb broadcast mask)
    pshufb  %xmm2, %xmm4           # broadcast ch to 16 bytes
                                    # pshufb zero mask: each byte takes src[0], i.e. broadcast the lowest byte
    pxor    %xmm2, %xmm2           # zero the count accumulator
.L4:
    movdqu  (%rax), %xmm0          # load 16 bytes
    pcmpeqb %xmm4, %xmm0           # byte-wise compare; matches 0xFF, misses 0x00
    pmovsxbw %xmm0, %xmm6          # sign-extend low 8 bytes → 8 words
    pmovsxwd %xmm6, %xmm5          # sign-extend → 8 dwords
    pmovsxdq %xmm5, %xmm5          # sign-extend → 4 qwords (each 0 or -1)
    # ... convert -1 to +1 and accumulate into xmm2 ...
    paddq   %xmm1, %xmm2           # accumulate into the counter
    cmpq    %rax, %rcx              # loop done?
    jne     .L4
```

::: details Why Is the Original Assembly Wrong?
The original text claimed GCC uses `punpcklbw` to broadcast the byte — that is wrong. What `punpcklbw` does is **interleave** the low bytes of two registers (byte → word); it does not broadcast. What GCC actually uses is `pshufb` (PSHUFB with a zero mask) to broadcast: when the mask is all zeros, every position takes `src[0]`, which copies the lowest byte to all 16 positions.

Also, the original text claimed counting via `pmovmskb` + `popcnt` — what GCC actually uses is a sign-extension chain (`pmovsxbw` → `pmovsxwd` → `pmovsxdq`), turning the 0xFF/-1 match results into qword values via sign extension, then accumulating. That strategy can beat `pmovmskb`+`popcnt` in some situations (especially when the count feeds further SIMD processing).
:::

Seeing `pcmpeqb` and the sign-extension chain tells you: in this case the compiler already did well, and no manual optimization is needed; but in another, more complicated scenario where the compiler failed to auto-vectorize, reading the assembly output lets you locate "where it got stuck." The real value of learning assembly — it gives you the ability to audit the compiler. Not to replace the compiler, but to be able to read its output.

## In Practice: Reading Assembly Far More Than Writing It

Every time you finish a function that might have performance issues, read the compiler's output first — drop it onto godbolt, turn on `-O2` or `-O3`, and check a few key indicators: are there unnecessary memory accesses (a variable you expected to live in a register that the compiler keeps reloading from the stack — maybe `volatile` is involved, maybe an aliasing problem); has the loop been vectorized (if the loop body is simple but the compiler did not vectorize, check for data dependencies or branches); have function calls been inlined (if not, is the function too big, or is something blocking inlining).

Every one of those judgments rests on "being able to read assembly." You do not need to be able to hand-write a stretch of perfect assembly from scratch; being able to read the basic instructions — `mov`, `load`, `store`, `cmp`, `jmp`, `call`, `ret` — and follow the data flow is enough.

## The Value of Writing Assembly on a Simple Architecture

The speaker said he does not miss the toil of hand-writing assembly, but he does miss that intellectual challenge. Having genuinely written assembly from scratch on some simple architecture builds a kind of "machine thinking" — while writing C++, a model runs in the back of your head: roughly what instructions will this line generate? How is this object laid out in memory? How many levels of indirection does this virtual call go through? That intuition pays off enormously in performance work.

## Summary

Learning assembly is not a tool for replacing the compiler, nor unattainable black magic. It is the ability to see what the machine is doing, and the best way to acquire that ability is probably not grinding away on x86-64, but picking a simple architecture your brain can hold and actually writing some. The day-to-day rule is simple: read assembly — read lots of it; write assembly — write it sparingly. Unless you have truly hit something the compiler cannot handle, and are confident hand-writing brings a significant win, and that stretch of code is stable enough not to churn. Otherwise, let the compiler do the work, and you audit it.

---

# How to Bring Newcomers into C++

CppCon is seriously chewing on this question too: when a CS graduate has never written C++, how do we pull them in? The speaker offered an observation: as a kid, the devices in his home, once turned on, let you do exactly one thing — type something in and, by immersion, figure out how it all worked. With not many choices, you ended up digging deep. Today the choices are endless: a student can get through four years of computer science, submit the homework in Python, build the capstone in React, and never once need to know what a stack is, what a heap is, or what undefined behavior is.

But what the speaker said next deserves more attention: at Google he has met new graduates who grew up entirely in "high-level" language environments yet dig into low-level hardware on their own. Curiosity about the low level is not particular to any one era — it is always there; only the trigger changes.

Bringing newcomers into C++ probably should not start with the sermon "you should learn C++ because it matters," but with finding the trigger inside each person — the day they suddenly hit a performance problem Python cannot solve, or suddenly want to figure out "how does a program actually reach the hardware" — that is the best moment. And those of us who came to C++ "the long way around" actually have one advantage: we know exactly where it hurts most when falling from a high-level language down to C++, and that "from pain to clarity" experience is precisely what can be shared with the next newcomer.

---

# The Preprocessor's Gradual Exit — C++'s Path of Incremental Replacement

In the Q&A, Matt Godbolt was asked "if you could remove one feature, what would you kill," and his answer was the preprocessor. That is not a spur-of-the-moment take — since C++11, this language has been doing the same thing over and over: reimplementing the machinery of the "preprocessor era" in "real C++."

## The Preprocessor's Typical Problems

In early C++ projects, screens full of `#define`, `#ifdef`, and nested conditional compilation were the norm. Take the logging macro:

```cpp
// How I wrote it in 2022 — I want to slap my past self looking at it now
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

The problem with this style: macros are text substitution and understand nothing of C++'s type system. Pass in an expression containing a comma, like `LOG_DEBUG(func(a, b))`, and the preprocessor reads it as two arguments — a compile error on the spot. Worse, the error messages are absurd, because they point at the expanded code, which bears no resemblance to the macro you wrote.

## The Modern Alternative: Real C++ Instead of Text Substitution

Replacing macros with `constexpr`, `inline` functions, and templates changes the game completely:

```cpp
// log.hpp
#pragma once
#include <iostream>
#include <source_location>

enum class LogLevel { Debug = 0, Info = 1, Error = 2 };

inline LogLevel g_log_level = LogLevel::Info;

// A constexpr function instead of a macro — type-safe, accepts arbitrary arguments
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

// inline constexpr variables instead of macro constants
inline constexpr LogLevel log_debug = LogLevel::Debug;
inline constexpr LogLevel log_info  = LogLevel::Info;
inline constexpr LogLevel log_error = LogLevel::Error;
```

```cpp
// main.cpp
#include "log.hpp"

int main() {
    g_log_level = LogLevel::Debug;

    // Called like this — expressions with commas are no problem at all
    log(log_debug, "value is {}", std::max(1, 2));
    log(log_info, "program started");
    log(log_error, "something went wrong: code={}", 404);
}
```

You might ask: what about `__FILE__` and `__LINE__`? That is exactly the problem C++20's `std::source_location` solves — it is a "real C++ feature," not preprocessor black magic; the compiler understands it properly, and you can get accurate information when debugging.

## Replacing `#include`: Modules

The preprocessor's most deep-rooted presence comes from `#include`. C++20 introduced modules<RefLink :id="8" preview="cppreference.com, Modules (since C++20)" />, shaking the preprocessor's position at the root. A minimal example:

```cpp
// math_utils.cppm — this is a module interface file
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

When compiling, note that module support is at different stages on different compilers. I used GCC 14, and the command looks roughly like this:

```bash
g++-14 -std=c++20 -fmodules-ts math_utils.cppm main.cpp -o demo
```

Run it and see:

```text
5^2 = 25
pi = 3.14159
```

The key difference: the `math_utils` module gets compiled once, no matter how many times it is `import`ed. Traditional `#include` copies and pastes the header's content into every translation unit — which is why large projects compile slowly: the same `<vector>` gets processed hundreds of times.

That said, modules still have plenty of potholes to step in, mainly the interoperability between modules and traditional headers. If you `import` a module that internally `#include`s a traditional header, and somewhere else also `#include` the same header, some compilers throw very strange errors. So the advice is: go all-modules or no modules — do not mix them, at least until the toolchains mature.

## What About Conditional Compilation

There is no perfect replacement for `#ifdef` yet. C++20's `consteval` and `if constexpr` cover part of the problem, on the condition that the condition can be settled at compile time.

::: warning Correction to the Original Text
The original version of the example used `reinterpret_cast` to detect endianness, but `reinterpret_cast` is **not allowed in constant-expression evaluation** by the C++ standard ([expr.const]<RefLink :id="12" preview="cppreference.com, Constant expressions" />), so it cannot be used inside a `consteval` function. GCC 16.1.1's actual error output:

```text
/tmp/test.cpp:4:12: warning: 'reinterpret_cast' is not a constant expression [-Winvalid-constexpr]
    4 |     return reinterpret_cast<const char*>(&test)[0] == 1;
      |            ^~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/tmp/test.cpp:7:34: error: call to consteval function 'is_little_endian()' is not a constant expression
```

Verification code in the repository: [05-02-consteval-endian-broken.cpp](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/blob/main/code/volumn_codes/vol10/cppcon/2025/02-some-assembly-required/05-02-consteval-endian-broken.cpp) (fails to compile) and [05-03-consteval-endian-fixed.cpp](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/blob/main/code/volumn_codes/vol10/cppcon/2025/02-some-assembly-required/05-03-consteval-endian-fixed.cpp) (fixed version, compiles). Readers can run `g++ -std=c++20 05-02-consteval-endian-broken.cpp` and see the compilation failure for themselves.
:::

With the fix in place, there are two compile-time ways to detect endianness:

```cpp
// Method 1: compiler built-in macros (recommended — simple and reliable)
consteval bool is_little_endian() {
    return __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__;
}

// Method 2: std::bit_cast (C++20, usable in constexpr/consteval)
#include <array>
#include <bit>
consteval bool is_little_endian_bitcast() {
    // std::bit_cast works in constant expressions; reinterpret_cast does not
    constexpr auto bytes = std::bit_cast<std::array<unsigned char, sizeof(int)>>(1);
    return bytes[0] == 1;
}

void write_bytes(int value) {
    if constexpr (is_little_endian()) {
        // little-endian handling path
        std::cout << "little endian path\n";
    } else {
        // big-endian handling path
        std::cout << "big endian path\n";
    }
}
```

But for genuine platform detection (Windows vs Linux), preprocessor-defined macros are still what you have to reach for. Which is also why Matt said "make the preprocessor less and less important" rather than "delete it tomorrow" — this is a gradual process.

## The Overall Trend

Since C++11, this language has been doing the same thing — reimplementing the machinery of the "preprocessor era" in "real C++." `constexpr` replaces macro constants, `inline` functions replace macro functions, `template` replaces type-agnostic macros, `source_location` replaces `__FILE__`/`__LINE__`, `modules` replace `#include`, `if constexpr` replaces part of `#ifdef`… Each step is unremarkable on its own, but added together they draw one clear direction. The preprocessor does not understand C++ — all it knows is scissors and glue, which is why it produces so many absurd errors. Templates, `constexpr`, `if constexpr` — these are part of C++ proper, and the compiler genuinely understands what you are doing.

The preprocessor will not disappear tomorrow, but as people writing C++, we can actively cut down our dependence on it. Every time you are about to write `#define`, stop and think: is there a type-safe C++ replacement for this? Most of the time, the answer is yes.

---

# Telling Wild Assembly Output Apart — Optimization or UB

Looking at the assembly for your own C++ code on Compiler Explorer, you often run into instruction sequences you simply cannot read — is the compiler being smarter than you can follow, or did you write some UB and the compiler is legally "going mad"? For a long time this was a very hard question to answer.

## The Most Direct Signal: Trap Instructions

There is one particularly blatant red flag: seeing the `UD2` instruction. Its full name is Undefined Instruction, and executing it does exactly one thing — the CPU raises an illegal-instruction exception and the program dies on the spot. When the compiler emits this instruction, it is saying: "Under normal circumstances, control cannot reach here; if it truly does, let the program die."

The classic scenario is a switch statement:

```cpp
#include <cstdint>

int32_t classify(int32_t value) {
    switch (value) {
        case 0:  return 1;
        case 1:  return 2;
        case 10: return 3;
        case 11: return 4;
    }
    // My thinking at the time: if it's none of these four values, just return 0
    return 0;
}
```

Logically this code looks complete — every branch returns, plus a fallback `return 0` at the end. But crank optimization to `-O2` and look at the assembly GCC or Clang produce, and you may find a `UD2` after the switch's jump table. During value-range analysis, if the compiler determines that callers have already constrained the incoming value to a finite set, it may infer that the final `return 0` can never execute. It then generates no normal return code for it, instead placing a `UD2` there as a "road closed" marker. So if you see `UD2` in the assembly and are confident that a legitimate path in your logic can reach that point, you can pretty much conclude: you and the compiler disagree about the program's behavior — and that usually means UB.

## More Often, It Is Not That Obvious

Not every UB shows up as `UD2`. Much of the time, after encountering UB, the compiler simply charges ahead with aggressive optimization on the assumption that "this case cannot happen," and the result is an instruction sequence that looks completely baffling.

```cpp
#include <cstddef>

int sum_array(const int* arr, size_t n) {
    int sum = 0;
    for (size_t i = 0; i <= n; ++i) {  // note the <= here
        sum += arr[i];
    }
    return sum;
}
```

The `i <= n` in this loop means `arr[n]` gets accessed — one element out of bounds. With optimization off, this code may "appear to work," because the out-of-bounds memory location happens to hold some value and the program does not crash right away. But once `-O2` is on, the compiler may rework the entire loop's logic beyond recognition on the assumption that "out-of-bounds array access is UB." What you see in the assembly then is not `UD2`, but a stretch of instructions that "look busy but the result is surely wrong." Nothing in the assembly itself tells you "this is because of UB" — you can only suspect it from experience.

## A Debugging Approach

Step one: check for trap instructions. If you see `UD2`, or the equivalent trap on the target architecture (`UDF` on ARM, say), you can lock it in: the compiler is saying there is an unreachable path here — go find out why it considers it unreachable.

Step two: no trap instructions, but the assembly looks off — the loop count is visibly short, some variables have vanished entirely, or computation logic you never wrote has appeared — then start suspecting UB. Recompile once with `-fsanitize=undefined`<RefLink :id="9" preview="GCC Documentation, Program Instrumentation Options" /> and see whether anything fires at runtime. This tool is extremely effective at catching signed-integer overflow, null-pointer dereference, and out-of-bounds array access — that class of UB.

Step three: if the sanitizer reports nothing either, then it may genuinely be an unexpected-but-legal compiler optimization. Pull up the compiler's control-flow graph — Compiler Explorer can open that view — and check whether the jumps between basic blocks match your expectations.

Finally, if none of that settles it, drop the instruction you cannot read into a search engine and see whether anyone else has hit something similar.

## No Silver Bullet

There is no one neat trick that lets you glance at assembly and know "optimization or UB." It is much more a process of accumulating experience — the more UB patterns you have seen, the sharper your intuition reading assembly. `-fsanitize=undefined` and trap instructions are the two most reliable anchors; the rest is research, control-flow graphs, and reasoning by comparing outputs across optimization levels. Reading assembly is really a debugging skill: you do not need to recognize every instruction, but you do need the ability to notice "something is off here," and then a systematic method to narrow it down.

---

# The Blurry Line Between Compiler "Cleverness" and UB

Between UB and non-UB there is not always a clean line. Once you push optimization to -O2 or even -O3, it is often hard to tell whether the compiler is "cleverly optimizing for you" or "legally wrecking your code."

## "It Runs Right, So There Is No UB" — A Common Misconception

A common folk model of UB: as long as the program produces the correct result, everything is fine. The principle — "UB is UB; whether it runs right today or not, the compiler is entitled to do anything" — is easy to state. But what actually makes it click is rarely being lectured; it is being burned once.

A typical scene: allocate a block of memory with `new`, walk it with an `unsigned char*` pointer to initialize it, then read and write it through a `float*` pointer. Runs perfectly fine in Debug mode; switch to Release and the output turns to garbage on the spot. That is the strict aliasing rule — at -O2 the compiler sees the `float*` reading that memory, concludes "this pointer has nothing to do with the earlier `unsigned char*`," and optimizes away the previously written values. Did the compiler do anything wrong? No — it acted completely legally. That is where the blurry line comes from.

## Why the Line Keeps Getting Harder to Draw

Modern compiler optimization is not the crude "delete unused variables" level; it rests on deep analysis of program semantics — dead-store elimination, pointer analysis built on strict-aliasing assumptions, loop optimizations built on "signed overflow is UB"… Every one of these optimizations stands on the premise "this program has no UB." The moment your code trips UB, those premises collapse — but the compiler will not tell you; it just keeps reasoning down its own logic, and the conclusions it reaches may happen to be right or be complete nonsense. More maddening still: switch the compiler version, switch the optimization level, even change the compilation order, and the results may all differ.

The C++ standard defines so many things as UB essentially to give the compiler room to optimize. Enjoy the optimization dividends, carry the UB risk — this is not the compiler working against you; it is the "contract" you signed when you chose C++.

## The Strategy: Don't Guess, Use Tools

Since eyeballing the code can hardly tell UB from non-UB, stop guessing and use tools.

```cmake
# The warning set standard in my projects, works for GCC/Clang
add_compile_options(
    -Wall -Wextra -Wpedantic
    -Werror          # warnings as errors — force yourself to deal with them
    -Wconversion     # implicit conversion warnings; this one has caught me several times
    -Wsign-conversion # signed/unsigned mixing warnings
)
```

The first habit: turn compiler warnings up to maximum strictness. `-Wconversion` comes strongly recommended — it gives an early truncation warning for the case where an `int` loop index accesses a `std::vector` and the container size exceeds `INT_MAX`.

The second habit: Sanitizers. Turn on UBSan and ASan when running tests during development:

```cmake
# Options in development mode
add_compile_options(
    -fsanitize=undefined,address
    -fno-sanitize-recover=all  # abort immediately on UB instead of carrying on
    -g -O1                     # note: Sanitizers work best at -O0,
                               # but -O1 is closer to real scenarios; I pick -O1 as the compromise
)
add_link_options(-fsanitize=undefined,address)
```

What UBSan can detect includes signed-integer overflow, null-pointer dereference, misaligned memory access, invalid casts (including strict-aliasing violations), out-of-range shift amounts, and so on. Covering all of that by reading code alone is nearly impossible.

The third habit is a bit "dumb" but very effective: cross-validation across compilers. GCC locally, Clang in CI, the occasional toss onto MSVC. Different compilers "exploit" UB in different ways — the same UB may happen to run correctly under GCC and blow up under Clang. If three compilers produce inconsistent results, there is almost certainly UB.

## The LLM Feature on Compiler Explorer

The Q&A also brought up the LLM feature on Compiler Explorer — mixed experiences. Using it to "explain" existing assembly works quite well: drop in a stretch of -O2-generated assembly, ask "how was this loop unrolled," and it mostly gives a close-enough answer. But having it "generate" a stretch of assembly from scratch is far riskier, because an instruction set has too many details.

The conservative usage: only let the LLM help "read" assembly, never "write" it. And after every one of its explanations, verify against the instruction-set manual or against what actually runs on Compiler Explorer. The strategy the speaker mentioned is interesting too — emphasizing "if unsure, stay silent" in the system prompt; it really does cut down the overconfident wrong answers, at the cost of the model going more "silent."

## Accept the Ambiguity, Without Giving Up Precision

In the C++ world, "the compiler did the right thing" and "the code has UB but happened not to blow up" can look exactly the same from the outside — no amount of watching the output will distinguish them. Rather than agonizing over "does this count as UB," put the energy into prevention: strict warnings, sanitizers, multi-compiler verification. These three moves block the vast majority of UB problems. As for the genuinely gray-zone cases — if you cannot tell whether it is UB, rewrite it into a shape that definitely is not. A few extra lines of code beat tracking down a baffling optimization problem at midnight.

---

# The Value of Hand-Written Assembly — Instruction Sets Have Not Abandoned Humans

## Instruction Sets Have Not Abandoned Humans

A common misconception goes: the early x86 instruction set was written for humans — regular instruction formats, clear semantics — while today's instruction set, with AVX-512, all the masked operations, all the prefix combinations, is purely machine code meant for compilers to generate; no human could read it. But spend real time in Intel's instruction manuals and you find this picture does not hold.

The talk offered a precise example: an instruction called `PMAXUB`. Going just by the name and description — "parallel compare for maximum of unsigned bytes," comparing 16 bytes against another 16 bytes one by one and keeping the larger — your first reaction may be "what on earth is this instruction for." But open the motion JPEG specification and you find that motion compensation needs exactly this operation — one instruction, done — and the compiler has no idea in which context it should emit this instruction.

The logic behind new instructions' births has never changed — "some specific domain needs a high-frequency operation, so a dedicated instruction gets added." It is not "designed so compilers can generate it easily," but "designed so that domain's programmers can write it easily." It is just that the "programmer" in question may be writing video codecs, cryptography, or numerical computing. It is not that the instruction set has rejected humans; it is that the "humans" it serves have become ever more specialized.

## Try It Yourself: Hand-Written Assembly vs Compiler Output

Let's write a real stretch of hand-written assembly and get a feel for how it differs from compiler output. The environment is Arch Linux WSL, GCC 16.1.1, x86-64. Note that GCC inline-assembly syntax and standalone-assembler syntax are two different things — more on that below.

Start with the simplest scenario: take the absolute value of every element in an array. Write it in pure C++, then in hand-written SIMD assembly, and compare:

```cpp
// abs_array.cpp
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <chrono>

constexpr int N = 1024 * 1024;  // 1M int32s

void abs_c(int32_t* dst, const int32_t* src, int n) {
    for (int i = 0; i < n; i++) {
        dst[i] = std::abs(src[i]);
    }
}

// Hand-written SSE assembly version, 4 int32s at a time
void abs_asm(int32_t* dst, const int32_t* src, int n) {
    // GCC extended inline asm
    // Core idea: use PSIGND, which can negate based on the sign mask;
    // but the simpler route is the PXOR + PSUBD trick:
    // abs(x) = (x ^ mask) - mask, where mask = x >> 31 (sign bit splat)
    __asm__ volatile (
        "xor %%eax, %%eax\n\t"         // i = 0
        "1:\n\t"
        "cmp %2, %%eax\n\t"            // compare i with n
        "jge 2f\n\t"                   // if i >= n, jump to the end
        "movdqu (%1, %%eax, 4), %%xmm0\n\t"  // load 4 int32s
        "movdqa %%xmm0, %%xmm1\n\t"   // keep a copy
        "psrad $31, %%xmm1\n\t"       // arithmetic right shift by 31 → sign mask
        "pxor %%xmm1, %%xmm0\n\t"     // x ^ mask
        "psubd %%xmm1, %%xmm0\n\t"    // (x ^ mask) - mask = abs(x)
        "movdqu %%xmm0, (%0, %%eax, 4)\n\t"  // store
        "add $4, %%eax\n\t"           // i += 4 (4 per iteration)
        "jmp 1b\n\t"                  // continue the loop
        "2:\n\t"
        : // output operands, none needed here
        : "r"(dst), "r"(src), "r"(n)   // input operands
        : "eax", "xmm0", "xmm1", "memory", "cc"  // clobber list
    );
}

int main() {
    // allocate aligned memory
    int32_t* src = (int32_t*)std::aligned_alloc(16, N * sizeof(int32_t));
    int32_t* dst_c = (int32_t*)std::aligned_alloc(16, N * sizeof(int32_t));
    int32_t* dst_asm = (int32_t*)std::aligned_alloc(16, N * sizeof(int32_t));

    // fill with random data (negatives included)
    srand(42);
    for (int i = 0; i < N; i++) {
        src[i] = (int32_t)(rand() - RAND_MAX / 2);
    }

    // warm-up
    abs_c(dst_c, src, N);
    abs_asm(dst_asm, src, N);

    // correctness check — never skip this step; I once got excited for nothing over an unchecked version
    bool correct = true;
    for (int i = 0; i < N; i++) {
        if (dst_c[i] != dst_asm[i]) {
            printf("MISMATCH at %d: c=%d, asm=%d\n", i, dst_c[i], dst_asm[i]);
            correct = false;
            break;
        }
    }
    printf("Correctness: %s\n", correct ? "PASS" : "FAIL");

    // benchmark
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

The result is roughly that the ASM version comes out 3 to 4 times faster. But hold off on conclusions — swap `-O2` for `-O3` and GCC will auto-vectorize this loop, and the gap narrows a lot. The point of hand-written assembly: when the compiler's auto-vectorization "misreads your intent," you can take precise control — the data has a special alignment, the loop has special unrolling needs, you need to insert a specific instruction inside the loop that the compiler knows nothing about — in those situations, hand-written assembly is the last resort.

## The Big Assembler Trap: AT&T vs Intel Syntax

The `as` shipped with GCC (the GNU Assembler) uses AT&T syntax — operand order is reversed (source operand first, destination second), registers take a `%` prefix, immediates take a `$` prefix. For instance, "store the value of eax to the address [rbx + 8]": in AT&T syntax that is written `movl %eax, 8(%rbx)`, while the NASM way is `mov [rbx + 8], eax` — the latter is far more intuitive. If you genuinely plan to hand-write assembly, use NASM or YASM; Intel syntax reads much better:

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
    movdqu xmm0, [rsi + rax*4]   ; load 4 int32s
    movdqa xmm1, xmm0            ; copy
    psrad xmm1, 31               ; sign mask
    pxor xmm0, xmm1              ; x ^ mask
    psubd xmm0, xmm1             ; abs(x)
    movdqu [rdi + rax*4], xmm0   ; store
    add eax, 4                   ; i += 4
    jmp .loop
.done:
    ret
```

The same logic, but the NASM version reads far more clearly. When building, note that NASM produces an object file, which you then link together with the C++ object files. The calling convention is on you to uphold — on Linux x86-64 it is the System V AMD64 ABI: the first six integer arguments go in rdi, rsi, rdx, rcx, r8, r9 respectively, and the return value comes back in rax.

## Where Instruction Sets Are Heading

The direction of instruction-set development is not "from designed-for-humans to designed-for-compilers," but "from general-purpose design to domain-specific design." Behind every weird-looking instruction there is a concrete application scenario. Outside that domain, it looks silly; inside that domain, it is a lifeline.

For C++ programmers, this means two things. First, when you hit a performance bottleneck and compiler optimization has run its course, you know you can open the compiler's assembly output (the `-S` flag, or Compiler Explorer) and see what is actually happening. Second, when you find that a domain has a dedicated instruction you can use, you have the ability to invoke it through inline assembly or a standalone assembly file, instead of just waiting for the compiler to "learn it someday."

Inline assembly does have a learning cost, but it is not insurmountable — no need to memorize the instruction manual; you only need to know "where to look it up" and "how to write a minimal runnable example." The rest is documentation reading and trial and error.

---

# Human-Oriented Assemblers and LLM-Generated Assembly

## The Idea of a "Human-Oriented Assembler"

The speaker mentioned that many existing assemblers are no longer actively maintained, and asked whether there is still room to build a "human-oriented" assembler. The heart of the question: the design philosophy of existing tools is stuck in the era of "an assembler is a translator of assembly instructions," and has never moved in the direction of "making the person writing assembly more comfortable."

For example, in NASM, to express "load the second field of this struct into rax," you have to compute the offset yourself and write `mov rax, [rcx + 8]` — that 8 is mental arithmetic. If the struct changes one field's type, you have to hunt down every hardcoded offset and fix them all. FASM (Flat Assembler) has a genuinely practical feature — it supports defining "virtual structs" right in the assembly, then referencing offsets by field name: `mov rax, [rcx + MyStruct.second_field]`. Underneath it is still computing offsets, but at least the assembler is computing them for you.

But FASM's macro system is miserable to debug: the error messages often point at some line inside the macro expansion, and you have no idea which part of the original macro went wrong. Modern C++ compilers are bending over backwards to improve error messages and the debugging experience, while over in assembler land, time seems to have stopped.

The ideal assembler would give beautiful error messages, build in struct and union support (not via macro hacks), and support some form of modularity (rather than relying on recursive include chains). "Niche" does not mean "without value."

## LLM-Generated Assembly — Never Trust It Blindly

In the Q&A, one audience member pointed out that LLM-generated assembly code had treated RSI as a length, when in reality it might not be. The speaker's response: "skeptical" and "nondeterministic." As someone who has actually been burned using LLM-generated assembly: **never use LLM-generated assembly code directly when you do not fully understand it.**

A concrete example. Ask an LLM to write an assembly function that "takes three integer arguments and returns their sum":

```asm
; LLM-generated code — looks reasonable, but carries a hidden hazard
section .text
global add_three

add_three:
    ; first argument in rdi, second in rsi, third in rdx
    lea rax, [rdi + rsi + rdx]
    ret
```

Looks fine at a glance — under the System V AMD64 ABI the first six integer arguments are indeed rdi, rsi, rdx, rcx, r8, r9, and adding three values with one lea is more elegant than a chain of adds. Compile, link, run — and the result is even correct. But the problem comes afterward — ask it to generate a version that "takes six arguments and returns their sum":

```asm
; LLM-generated code — wrong this time
section .text
global add_six

add_six:
    ; arguments: rdi, rsi, rdx, rcx, r8, r9
    lea rax, [rdi + rsi]        ; add the first two
    add rax, rdx
    add rax, rcx
    add rax, r8
    add rax, r9
    ret
```

This code runs correctly most of the time, but there is one subtle problem: `lea rax, [rdi + rsi]` performs unsigned addition, and if the values of rdi and rsi are large enough that their sum exceeds the 64-bit unsigned integer range, it silently overflows. Using `add rax, rdi` then `add rax, rsi` also overflows, but the overflow flag (OF) is set in line with arithmetic-addition semantics. If the caller relies on the OF flag to detect overflow, that lea quietly digs a pit.

Even more absurd: ask the LLM to generate the same functionality again, and the second result may put the sixth argument's register down as r10 — flatly wrong; r10 is not an argument-passing register. That is "nondeterminism": ask twice, get two different answers — one maybe right, the other maybe wrong.

## The Practical Workflow

After all those pits, the way to use an LLM for assembly writing should change completely: stop asking it to "write me a function that does X," and treat it as "a chat partner that has the instruction manual memorized." Ask it "does x86-64 have one instruction that can do addition and multiplication at the same time," and it will tell you that `imul` has a with-addition variant (for example the three-operand form `imul rax, rbx, 42`); then you go confirm that instruction's exact behavior in the Intel manual, and write the code yourself. The LLM's role demotes from "code generator" to "indexing tool" — an unreliable one, but faster than flipping through the PDF manual yourself.

## How the Two Questions Connect

Put the two together and they point in the same direction: **there is still enormous room to improve the assembly programming experience**. A human-oriented assembler improves the experience at the tool layer; reliable LLM assistance (if it can ever be achieved) improves it at the learning-curve layer. But the premise for both is the same — you have to understand what is happening underneath. The best tools cannot replace thinking, and the strongest LLM cannot replace verification.

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
