---
title: "A short history of C++: where the bad reputation came from"
description: "The opening piece used four firmwares to break the myths of bloated C++ and OOP-only C++; this one answers the remaining question: where the bad reputation came from. From C with Classes in 1979 to the death of Cfront, the 1990s Embedded C++ feature-cutting affair, and MISRA/AUTOSAR folding C++ into safety standards — every historical claim carries a source, and the ending lays out the current coordinates of embedded for you"
chapter: 0
order: 1
tags:
  - host
  - cpp-modern
  - beginner
  - 基础
difficulty: beginner
platform: host
reading_time_minutes: 12
related:
  - "Why C++, and by what right?"
translation:
  source: documents/vol8-domains/embedded/f103/00-env-setup/01-cpp-history.md
  source_hash: 5fff1d25fb44eb9953a257b16a3c669c90a549e2d84f52dda52b6ce3bd2cea9c
  translated_at: '2026-09-25T08:26:09+00:00'
  engine: anthropic
  token_count: 3600
---

# Ahem — you talked C++ up to the skies earlier; now I'm curious where all those "rumors" came from

> The author is not a programming-language historian — everything below was concluded from digging through sources. If anything is wrong, please be merciful with your words, and by all means file an Issue~

At this point we can finally sentence the phrase "C++ can't run on a microcontroller" to death — the war is over, right now! But here is what I want to say: don't stop yet! We ought to ask where a claim that an entire engineering community collectively believed for thirty years came from. The answer: it isn't a fabricated rumor, it's history — and most likely a pile of real facts accumulated in the 1990s. What level were C++ compilers at back then, how much memory did chips have back then, and what famous "feature-cutting" incidents happened back then. I decided to go do the searching on purpose, and this is also a chance for everyone to take a rest and just have a chat! The truth is, **from the very first day of C++'s existence, what it aimed at was our 64 KB Flash board, not the host PC**

## 1979: born a systems language

In 1979, Bjarne Stroustrup, the inventor of C++, was working on his PhD topic at Bell Labs (sound familiar? That same lab also gave birth to two other remarkable things: one is Unix, and the other is the C language).

> Bjarne Stroustrup started fiddling with his "C with Classes" at Bell Labs, and the starting point wasn't "a better language for desktop programmers" — it was the PhD topic he had to do: simulation in distributed systems. SIMULA's classes were nice to use but just too slow; C was fast, crazy fast, ridiculously fast, but writing large-scale programs in it far too easily stirred the code into a tangle, and writing it while holding your nose was miserable. Benjamin said to knead the two together<RefLink :id="1" preview="Mahmutbegović, C++ in Embedded Systems, Packt, 2025, Ch.1" />.

In 1983, Bjarne Stroustrup decided to replace the language's name with something more concise, and we have called it that for 43 years — C++.

On October 14, 1985, the first official compiler, Cfront 1.0, and the first edition of *The C++ Programming Language* were released on the same day<RefLink :id="2" preview="Stroustrup, isocpp.org, Celebrating the 30th Anniversary of the first C++ compiler, 2015" />. From that day on, C++ slowly got onto the right track, and accompanied us through C++98, C++03, C++11... arriving together with us at today's C++26.

Seen from here, the impression that "C++ is a desktop language that only looks bloated once you cram it into a microcontroller" fails at the very origin: for a language born to write operating-system-grade systems software, "manipulating hardware directly" and "paying no extra runtime overhead" were factory settings, not promises patched on later. Stroustrup himself later gave a keynote specifically for embedded developers, on exactly what C++ can do for embedded in resource management, reliability, and zero-overhead abstraction<RefLink :id="3" preview="Stroustrup, Keynote: What can C++ do for embedded systems developers?, NDC Conferences" />. That was no nostalgic post-retirement speech — this set of demands has not changed since 1979.

Hmm... so, where did this impression start to warp?

## Cfront kicked the bucket

The compiler! You betrayed me! Yes, unbelievably, it was the compiler. Look at Cfront's architecture: in my view it truly carried on the C with Classes way of thinking — this compiler was really more like... a translator? Because it was never a compiler that generated machine code directly; it translated C++ into C, and then borrowed each platform's C compiler to get the work out...

Don't laugh. Really, don't laugh. For its time this design actually counted as quite clever. Back then everyone acknowledged C — hey, you say what comes out in the end is C? That way we could blanket almost every machine out there with a very, very small amount of code. While taking heavy hits from bugs, over a hot meal in the cafeteria, folks would say: hey guys, heard of C++? They say it's going to do that whole OOP thing.

Hm? So what was the price? Brothers, generating C means the price is that the quality of its generated code is forever limited by whichever C compiler sits underneath. However good you are, you're just the preprocessor stage of the compilation — what future is there in that? A whole lifetime of bowing low, at the mercy of the C compiler.

Cfront's death sentence was handed down in 1993: the development team's attempt to add exception support to Cfront 4.0 failed, and the entire compiler was retired on the spot<RefLink :id="4" preview="Wikipedia, Cfront" />. With one loud clatter, everyone knew that the Cfront approach very likely couldn't go on.

Let's savor this from an embedded engineer's perspective: if you used C++ in that era, what you used was very likely Cfront-derived or a same-generation product, and **the most famous "expensive" C++ feature of those years was precisely exceptions** — unwind tables, runtime library, unpredictable stack unwinding. When the compiler itself dies on exceptions, the industry's fear was not groundless. Add in genuine problems of the day such as early GCC's template-instantiation bloat and iostream dragging libraries along, and "C++ is big and slow" was, in the 1990s, a **reproducible observation**. In some respects, C++ had denied itself!

## Embedded C++: a famous feature-cutting, and its cautionary tale

Yo! We fast-forward to the mid-1990s. The Embedded C++ technical committee, led by the Japanese chip giants Toshiba, Hitachi, Fujitsu, and NEC, felt that C++ was too big for embedded and a "dedicated embedded edition" had to be built. In September 1996, the draft came out<RefLink :id="5" preview="Perforce, A Brief History of MISRA C++, 2021" />. This dialect called EC++ cut features without the slightest mercy: templates, exceptions, RTTI, multiple inheritance, namespaces, new-style casts — all cut. From the standard library, STL and locales were removed entirely, and iostream was swapped for a simplified version<RefLink :id="6" preview="Wikipedia, Embedded C++" />. Compiler vendors of the day put real money behind it too — Green Hills shipped a dedicated EC++ compiler<RefLink :id="7" preview="EE Times, Green Hills Unveils Compiler for Embedded C++" />.

And then? Let's look straight at the ending — it is more convincing than any argument.

Actual usage of EC++ itself was scant. What the market really lifted up was a variant of it, Extended EC++, **the version that added templates back**<RefLink :id="5" preview="Perforce, A Brief History of MISRA C++, 2021" />. The people who cut templates bet that "templates bloat the code"; the people who used templates discovered that "templates are the carrier of zero-overhead abstraction": that line we measured ourselves in the previous piece — "the template version matches the HAL version instruction for instruction" — had already been voted on with Extended EC++ in the late 1990s.

The ISO standards committee's response is even more worth remembering for the rest of our lives: they did not endorse EC++, but instead published a Performance Technical Report, giving feature by feature models of time and space costs, along with efficient implementation techniques<RefLink :id="5" preview="Perforce, A Brief History of MISRA C++, 2021" />.

Ah, can't parse it? In truth it's this — you embedded folks keep saying "Oh C++ costs too much", puzzling; our committee's line was — "Take what you really want", not cutting off your legs because this particular errand doesn't require walking. **A subset of a language will drift along with the standard until it becomes an orphan; a cost inventory stays valid forever** — EC++ code ultimately could not interoperate with standard C++, while per-project compiler switches like `-fno-exceptions` survive to this day as the engineering-correct answer. The `-fno-exceptions -fno-rtti` you saw in the libestdx toolchain file in the previous piece is a direct descendant of this route.

## The standardization era: from C++98 to the safety industry voting with its feet

In 1998, the first ISO standard, C++98, landed, and the language entered a period of steady evolution. The real great turning point was C++11 in 2011: `constexpr` pushed computation to compile time, and `<atomic>` established a standard memory model for multicore bare-metal programming — the weight these two features carry for embedded, we will taste again at every station ahead. After that, 14, 17, 20, and 23 marched on in quick small steps, and C++20's concepts are exactly the underlying mechanism behind that "wrong direction configured, caught at compile time" moment in the previous piece<RefLink :id="8" preview="cppreference.com, History of C++" />.

And what explains things even better than language evolution, in our view, is **the attitude on the specification side**. Safety-critical industries are the pickiest customers of "language costs must be controllable", and their timeline walked like this<RefLink :id="9" preview="Parasoft, Breaking Down the AUTOSAR C++14 Coding Guidelines" />: in 2008, MISRA C++:2008 was published, with C++03 as its baseline; in 2017, the automotive world's AUTOSAR issued the AUTOSAR C++14 guidelines, whose full name is literally "Guidelines for the use of the C++14 language in critical and safety-related systems"; in 2023 the two sides merged, MISRA C++:2023 was published, the baseline jumped straight to C++17, and the AUTOSAR rules were absorbed wholesale<RefLink :id="10" preview="Perforce, What You Need to Know About the Next MISRA Standard" />.

Let's savor what this timeline means: core autonomous-driving software is being written in C++17. This industry, the one for which "every instruction's cost must be accounted for", gave — twenty-odd years after EC++ was born — an answer that was not cutting features off C++, but **writing guidelines to constrain usage, then moving forward with the whole language**. History took a big loop right here, returning to the route of that ISO performance report. In other words — and this especially deserves emphasis — modern C++ really is catching on, even though it truly does carry an entire truckload of historical debt.

## What about 2026?

I'm no professional on this — this is just-for-fun talk, everyone, enjoy it for the sound.

C, to this day, still rules embedded — nothing to argue about there, facts are facts. Which is why, back when I wanted to try CFBox, C++ for embedded (oh, it's a C++23 stand-in for busybox), my WeChat official account was positively a field of denunciation. The argument "you're doing open source and not using C" shook me for months. I even joked with a friend that I had quit my job to study that sentence, haha.

C is a splendid language. Just like when you say to me "hey, help me look at the performance problem in this code" — if it's C, I'm the happiest: one glance down and I can guess most of the assembly underneath; it is, after all, closer to the hardware.

But when it comes to the embedded cake, the new-generation languages represented by Rust and C++ really are rapidly carving up this enormous cake. The Stack Overflow Developer Survey added a dedicated embedded section for the first time in 2024, and in 2025 it kept expanding with new embedded-related questions<RefLink :id="12" preview="Stack Overflow Developer Survey 2024/2025, Embedded technologies" />. The diversification of embedded languages is present progressive, not future tense.

So fire the question: and us?

First, even repaying the bad reputation with interest requires an accurate ledger: in the 1990s C++ really was "big and slow", but that is the bill of an era when Cfront reigned and 64 KB counted as a lot of memory — today's compilers have already changed three generations: g++, clang++, msvc... these de-facto compiler giants produce binaries that will not disappoint you or your CPU in the slightest (most of the time, hm)

Second, the lesson of EC++ is not "C++ doesn't suit embedded", but that **"cutting a language subset" is a road that goes nowhere in itself**. That is exactly why we choose to configure compiler options and understand language features rather than run off and build a different wheel.

Third, the ecosystem is already in place: for zero-heap containers there are libraries made specifically for bare metal, like the ETL (Embedded Template Library) and Google's Pigweed, and C++26 will further take the fixed-length, on-stack `inplace_vector` into the standard library. The libestdx you saw in the previous piece is precisely this craft put into practice on our line.

> A quick PS: I will also try to share my own approach to analyzing binaries with the corresponding binutils.
>
> The most distinctive engineering move in embedded C++ is "trust no claim about an abstraction — look at the artifact directly": `arm-none-eabi-size` to check size, `objdump -d` to check instructions — we already used them all in the previous piece; later we'll add linker map files and size-attribution tools like bloaty, chasing "which function ate my Flash" all the way to the responsible head. So that when we're genuinely confused about what the hell just happened, we can finally say: haha, kid, it was you who did it.
>
> This toolkit carries exceptional weight here with C++: **it is an important acceptance test for the promise of "zero-overhead abstraction"**.

## Where this line stands

To wrap up. Over fifty years, C++ set out from Bell Labs' systems-language ambitions and walked to today carrying the old debts of 1990s compilers and the institutional memory of EC++, and the safety industry has issued it a pass to critical systems with MISRA C++:2023. It is still not perfect: **the language is complex, the historical baggage is heavy, the learning curve is steep — all of these are true. And they can equally stand as a major reason for you not to use C++ for embedded programming!**

But on a 64 KB Flash board, stopping accidents with types, computing the configuration at compile time, and accepting every single cost with objdump — this path is genuinely walkable in 2026. I'd say we have no reason not to stand up and try a more modern development experience. This is also why I do embedded C++, and a major starting point for why I opened TAMCPP.

In the next piece, we return to the tools themselves: get the environment at hand set up, and bring in Renode, the old veteran. Congratulations — you really can now get ready to try embedded without owning a board!

<ReferenceCard title="References">
  <ReferenceItem
    :id="1"
    author="Amar Mahmutbegovic"
    title="C++ in Embedded Systems: A practical transition from C to modern C++"
    :year="2025"
    url="https://www.packtpub.com/en-us/product/c-in-embedded-systems-9781835881149"
    chapter="Packt Publishing, Ch.1 account of the origins of C with Classes"
  />
  <ReferenceItem
    :id="2"
    author="Bjarne Stroustrup"
    title="Celebrating the 30th Anniversary of the first C++ compiler"
    :year="2015"
    url="https://isocpp.org/blog/2015/10/cpp-30"
    chapter="Firsthand recollection of Cfront 1.0 and the first edition of TC++PL released on the same day"
  />
  <ReferenceItem
    :id="3"
    author="Bjarne Stroustrup"
    title="Keynote: What can C++ do for embedded systems developers?"
    url="https://www.youtube.com/watch?v=VoHOLDdfDhk"
    chapter="NDC Conferences keynote"
  />
  <ReferenceItem
    :id="4"
    author="Wikipedia"
    title="Cfront"
    :year="2026"
    url="https://en.wikipedia.org/wiki/Cfront"
    chapter="Abandoned after the failed attempt to add exceptions to Cfront 4.0 in 1993"
  />
  <ReferenceItem
    :id="5"
    author="Perforce"
    title="A Brief History of MISRA C++"
    url="https://www.perforce.com/blog/qac/misra-cpp-history"
    chapter="The EC++ committee, the draft timeline, Extended EC++, and the Performance TR"
  />
  <ReferenceItem
    :id="6"
    author="Wikipedia"
    title="Embedded C++"
    :year="2026"
    url="https://en.wikipedia.org/wiki/Embedded_C%2B%2B"
    chapter="The list of cut features and the composition of the committee"
  />
  <ReferenceItem
    :id="7"
    author="EE Times"
    title="Green Hills Unveils Compiler for Embedded C++"
    :year="1997"
    url="https://www.eetimes.com/green-hills-unveils-compiler-for-embedded-c/"
    chapter="Record of compiler vendors following suit on EC++"
  />
  <ReferenceItem
    :id="8"
    author="cplusplus.com"
    title="History of C++"
    :year="2026"
    url="https://cplusplus.com/info/history/"
    chapter="Timeline of the standards, 1979 through the 2020s"
  />
  <ReferenceItem
    :id="9"
    author="Parasoft"
    title="Breaking Down the AUTOSAR C++14 Coding Guidelines"
    :year="2023"
    url="https://www.parasoft.com/blog/breaking-down-the-autosar-c14-coding-guidelines-for-adaptive-autosar/"
    chapter="Baseline comparison of AUTOSAR C++14 and MISRA C++ 2023"
  />
  <ReferenceItem
    :id="10"
    author="Perforce"
    title="What You Need to Know About the Next MISRA Standard"
    :year="2023"
    url="https://www.perforce.com/blog/qac/misra-cpp-2023-intro"
    chapter="MISRA C++:2023 absorbing the AUTOSAR guidelines"
  />
  <ReferenceItem
    :id="11"
    author="Jacob Beningo"
    title="The Best Embedded Programming Languages for Engineers Now"
    :year="2024"
    url="https://www.beningo.com/the-best-embedded-programming-languages-for-engineers-now/"
    chapter="Industry-survey figure: C drives more than 60% of embedded projects worldwide"
  />
  <ReferenceItem
    :id="12"
    author="Stack Overflow"
    title="Developer Survey 2024/2025 — Embedded technologies"
    :year="2025"
    url="https://survey.stackoverflow.co/2025/technology"
    chapter="Embedded section added for the first time in 2024, expanded into a full section in 2025"
  />
</ReferenceCard>
