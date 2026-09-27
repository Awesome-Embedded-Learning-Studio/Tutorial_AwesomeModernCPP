---
title: "A short history of C++: where the bad reputation came from"
description: "The opening piece used four firmwares to break the myths of bloated C++ and OOP-only C++; this one answers the remaining question: where the bad impressions came from. It runs from C with Classes in 1979 through the death of Cfront and the 1990s Embedded C++ feature-cutting affair, all the way to MISRA/AUTOSAR folding C++ into their safety standards — every historical claim carries a source, and the ending lays out the current coordinates of embedded C++ for you"
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
  source_hash: ecddb806928235c30867a1c6a02ff35fa22588675b5cc8dac0d75ed289025231
  translated_at: '2026-09-27T05:27:26+00:00'
  engine: anthropic
  token_count: 7400
---

# Ahem — you talked C++ up to the skies earlier, and now I'm curious where those "rumors" came from

> The author is not a programming-language historian; everything below is a conclusion reached by digging through sources. If anything is wrong, please go easy on me with your words — and by all means file an Issue~

At this point we can finally sentence the phrase "C++ can't run on a microcontroller" to death — the war is over, right now! But here is what I want to say: don't stop yet! We had better ask where a claim that an entire engineering community collectively believed for thirty years came from. The answer: it is not a fabricated rumor, it is history — and most likely a pile of real facts accumulated in the 1990s. What level were the C++ compilers of that era at, how much memory did the chips of that era have, and what famous "feature-cutting" incidents happened in that era. I decided to go do the digging on purpose, and this is also a chance for everyone to take a breather and just have a chat! The truth is, **from the very first day of C++'s existence, what it aimed at was our 64 KB Flash board, not the host PC**

## 1979: born a systems language

In 1979, Bjarne Stroustrup, the inventor of our C++, was working on his PhD topic at Bell Labs (sound familiar? That same lab also gave birth to two other remarkable things: one is Unix, and the other is the C language).

> Bjarne Stroustrup started fiddling with his "C with Classes" at Bell Labs, and the starting point was not "a better language for desktop programmers" — it was the PhD topic he had to do: simulation in distributed systems. SIMULA's classes were pleasant to use but just too slow; C was fast, crazy fast, ridiculously fast, but writing large-scale programs in it far too easily stirred the code into a tangle, and pinching your nose while writing it was miserable. Benjamin said to knead the two together<RefLink :id="1" preview="Mahmutbegović, C++ in Embedded Systems, Packt, 2025, Ch.1" />.

In 1983, Bjarne Stroustrup decided to replace the language's name with something more concise, and we have been calling it that for 43 years — C++.

On October 14, 1985, the first official compiler, Cfront 1.0, and the first edition of *The C++ Programming Language* were released on the very same day<RefLink :id="2" preview="Stroustrup, isocpp.org, Celebrating the 30th Anniversary of the first C++ compiler, 2015" />. From that day on, C++ slowly got onto the right track, and it accompanied us through C++98, C++03, C++11... arriving together with us at today's C++26.

Seen from here, the impression that "C++ is a desktop language that only looks bloated once you cram it into a microcontroller" fails at the very origin: for a language born to write operating-system-grade systems software, "manipulating hardware directly" and "paying no extra runtime overhead" were factory settings, not promises patched on later. Stroustrup himself later delivered a keynote specifically for embedded developers, on exactly what C++ can do for embedded folks in resource management, reliability, and zero-overhead abstraction<RefLink :id="3" preview="Stroustrup, Keynote: What can C++ do for embedded systems developers?, NDC Conferences" />. That was no nostalgic post-retirement speech — this set of demands has not changed since 1979.

Huh... so, where did this impression start to go wrong?

## Cfront kicked the bucket

The compiler! You betrayed me! Yes, unbelievably, it was the compiler. Look at Cfront's architecture: in my view it truly carried on the C with Classes way of thinking — this compiler was really more like... a translator? Because it was never a compiler that generated machine code directly; it translated C++ into C, and then borrowed each platform's C compiler to get the work out...

Don't laugh. Really, don't laugh. For its time, this design actually counted as quite clever. Back then everyone acknowledged C — hey, you say what comes out in the end is C? That way we could blanket almost every machine out there with a very, very small amount of code. While taking heavy hits from bugs, over a hot meal in the cafeteria, folks would say: hey guys, heard of C++? They say it's going to do that whole OOP thing.

Hm? So what was the price? Folks, generating C means the price is that the quality of its generated code is forever limited by whichever C compiler sits underneath. However good you are, you are still just the preprocessor stage of the compilation — what future is there in that? A whole lifetime of bowing low, at the mercy of the C compiler.

Cfront's death sentence was handed down in 1993: the development team tried and failed to add exception support to Cfront 4.0, and the whole compiler was retired on the spot<RefLink :id="4" preview="Wikipedia, Cfront" />. And with a clunk, everyone understood that the Cfront approach very likely could not keep going.

Let's savor this from an embedded engineer's angle: if you used C++ back then, what you used was most likely Cfront or a contemporary product, and **the most famous "expensive" C++ feature of that era was precisely exceptions** — unwind tables, runtime libraries, unpredictable stack unwinding. The compiler itself died on exceptions, so the industry's fear was not groundless. Throw in the real problems of those early years — GCC's template instantiation bloat, iostream dragging in libraries — and "C++ is big and slow" was a **reproducible observation** in the 1990s. C++ had contradicted itself there, on some levels!

## Embedded C++: a famous feature-cutting effort, and its cautionary tale

Yo! Fast-forward to the mid-1990s: the Embedded C++ technical committee, led by the Japanese chip giants Toshiba, Hitachi, Fujitsu, and NEC, decided that C++ was too big for embedded, and a "special embedded edition" had to be built. The draft came out in September 1996<RefLink :id="5" preview="Perforce, A Brief History of MISRA C++, 2021" />. This dialect called EC++ cut features without the slightest mercy: templates, exceptions, RTTI, multiple inheritance, namespaces, new-style casts — all cut; from the standard library, STL and locales were removed entirely, and iostream was swapped for a simplified version<RefLink :id="6" preview="Wikipedia, Embedded C++" />. Compiler vendors of the day put real money behind it too — Green Hills shipped a dedicated EC++ compiler<RefLink :id="7" preview="EE Times, Green Hills Unveils Compiler for Embedded C++" />.

And then? Let's just look at the ending — it is more convincing than any argument.

EC++ itself saw little actual use. What the market truly embraced was a variant of it, Extended EC++, **the version that put templates back in**<RefLink :id="5" preview="Perforce, A Brief History of MISRA C++, 2021" />. Those who cut templates bet on "templates bloat the code"; those who used templates discovered that "templates are the vehicle of zero-overhead abstraction": the "template version matches the HAL version instruction for instruction" we measured ourselves in the previous piece — people had already voted for it with Extended EC++ in the late 1990s.

The ISO standards committee's response is worth remembering for the rest of our lives: rather than endorsing EC++, they published a Performance Technical Report, giving per-feature models of time and space cost, along with efficient implementation techniques<RefLink :id="5" preview="Perforce, A Brief History of MISRA C++, 2021" />.

Huh, too dense? It boils down to this — you embedded folks keep saying "Oh, C++ costs too much", baffling; our committee's line was "Take what you really want", not "cut off your legs because you don't always need to walk". **A language subset drifts with the standard and becomes an orphan; a cost inventory stays valid forever** — EC++ code ultimately could not interoperate with standard C++, while per-project compiler switches like `-fno-exceptions` are still the engineering-correct answer today. The `-fno-exceptions -fno-rtti` you saw in the libestdx toolchain files in the previous piece is a direct descendant of that route.

## The standardization era: from C++98 to the safety industries voting with their feet

In 1998, the first ISO standard, C++98, landed, and the language entered a period of steady evolution. The real turning point was C++11 in 2011: `constexpr` pushed computation to compile time, and `<atomic>` gave multi-core bare-metal programming a standard memory model — we will taste the weight of these two features for embedded at every stop ahead. After that, 14, 17, 20, and 23 marched along in quick small steps, and C++20's concepts are exactly the underlying mechanism behind that "wrong configuration direction, compile-time error" from the previous piece<RefLink :id="8" preview="cppreference.com, History of C++" />.

And what explains things even better than the language's evolution is, in our view, **the attitude on the specification side**. Safety-critical industries are the pickiest customers when it comes to "language cost under control", and their timeline walked like this<RefLink :id="9" preview="Parasoft, Breaking Down the AUTOSAR C++14 Coding Guidelines" />: in 2008, MISRA C++:2008 was published, with C++03 as its baseline; in 2017, the automotive industry's AUTOSAR issued the AUTOSAR C++14 guidelines, whose full title is "Guidelines for the use of the C++14 language in critical and safety-related systems"; in 2023 the two sides merged, MISRA C++:2023 was published, the baseline jumped straight to C++17, and the AUTOSAR rules were absorbed wholesale<RefLink :id="10" preview="Perforce, What You Need to Know About the Next MISRA Standard" />.

Let's savor what this timeline means: the core software of autonomous driving is being written in C++17. This industry, which must "account for the cost of every single instruction", answered — more than twenty years after EC++ was born — not by deleting features from C++, but by **writing guidelines to constrain usage, then moving forward with the whole language**. History looped a big circle here and returned to the route of that ISO performance report. In other words, and this deserves emphasis — modern C++ really is on the rise, even though it truly does carry a huge pile of historical debt on its back.

## What about 2026

The author is no professional; take this as fun-talk and just enjoy the noise.

C, to this day, still rules embedded — there is nothing to argue about there, facts are facts. Which is also why, when I tried writing CFBox for embedded in C++ back then (oh, that's a C++23 stand-in for busybox), my WeChat official account was positively a chorus of condemnation. The "you're doing open source and not using C" argument shook me for months. I even joked with friends that I had resigned to study that sentence, haha.

C is a splendid language — like when you come to me saying "hey, help me look at the performance problem in this code": if it's C, I'm the happiest; one glance down to the assembly and I can guess seven parts out of ten — after all, it sits closer to the hardware.

But when it comes to the embedded cake, the new-generation languages represented by Rust and C++ really are rapidly carving up this enormous cake. The Stack Overflow Developer Survey added a dedicated embedded section for the first time in 2024, and in 2025 it kept expanding with new embedded questions<RefLink :id="12" preview="Stack Overflow Developer Survey 2024/2025, Embedded technologies" />. The diversification of embedded languages is in progress, not in the future tense.

So the question arises: what about us?

First, repaying the bad reputation with interest still means repaying the right account: C++ in the 1990s really was "big and slow", but that is the account of the era when Cfront ruled and 64 KB counted as a lot of memory — today's compilers have changed three generations since: g++, clang++, msvc... these de facto compiler giants produce binaries that will not disappoint you or your CPU in the slightest (most of the time, hmm)

Second, the lesson of EC++ is not "C++ is unsuitable for embedded", but that **the road of "cutting a language subset" itself does not work**. That is exactly why we choose to configure compiler options and understand compilation features, rather than run off and reinvent the wheel.

Third, the ecosystem is ready: for heap-less containers there are libraries built specifically for bare metal, like ETL (Embedded Template Library) and Google's Pigweed, and C++26 will also bring the fixed-length, on-stack `inplace_vector` into the standard library. The libestdx you saw in the previous piece is exactly this craft put into practice on our track.

> A PS: the author will also try to share his own approach to analyzing binaries with the corresponding binutils.
>
> The most distinctive engineering move in embedded C++ is "trust no abstraction claim, look at the artifact directly": `arm-none-eabi-size` for size, `objdump -d` for instructions — we already used both in full in the previous piece; later we will add the linker map file and size-attribution tools like bloaty, chasing "which function ate my Flash" all the way to the culprit. So that when we are genuinely confused about what the hell just happened, we can finally say: haha, kid, it was you all along.
>
> This set of techniques carries extra weight here in C++: **it is an important acceptance test for the promise of "zero-overhead abstraction"**.

## Where this track stands

Let's wrap up. Over fifty years, C++ set out from Bell Labs' systems-language ambition, carried the old debts of 1990s compilers and the institutional memory of EC++ all the way to today, and the safety industry issued it a pass to critical systems with MISRA C++:2023. It is still not perfect: **the language is complex, the historical baggage is heavy, and the learning curve is steep — all of that is true. And it can equally stand as a major reason for you not to use C++ for embedded programming!**

But on a 64 KB Flash board, using types to block accidents, using compile time to work out the configuration, and being able to accept every single cost with objdump — this path is genuinely walkable in 2026. I think we have no reason not to stand up and try a more modern development experience. This is why the author does embedded C++, and it is also an important starting point for the author to run TAMCPP.

In the next piece, we return to the tools themselves: set up the environment at hand, and bring in Renode, the old workhorse. Congratulations — you really can get ready to try embedded without a board!

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
    chapter="Firsthand recollection of Cfront 1.0 and the first edition of TC++PL shipping on the same day"
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
    chapter="Cfront 4.0 abandoned after the failed exception support attempt in 1993"
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
    chapter="The list of cut features and the committee's composition"
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
    chapter="Timeline of standards from 1979 through the 2020s"
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
    chapter="Survey figure: C drives more than 60% of embedded projects worldwide"
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
