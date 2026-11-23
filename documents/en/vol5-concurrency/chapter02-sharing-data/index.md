---
title: "Unit 2: How Multiple Tasks Use Data Together"
description: "Know data races and race conditions from programs that compute wrong, then see how mutexes protect shared state, where the boundary of one operation lies, and how to reduce sharing and avoid deadlock."
translation:
  source: documents/vol5-concurrency/chapter02-sharing-data/index.md
  source_hash: c013eb8ed30553fc3b353a5588241ce93b7f6f40bc23508ff6b9e90b94c1dceb
  translated_at: '2026-11-22T13:28:59+00:00'
  engine: glm-5.3
  token_count: 912
---
# Unit 2: How Multiple Tasks Use Data Together

Unit 1 settled "how threads start and end"; now we change the question: when several flows of execution meet the same data, what happens, and how should the access be organized?

We fix the order as: see the errors first, learn the tools after. Starting from broken programs, we get to know the full shapes of data races, race conditions, deadlock, and starvation; only then do we touch mutexes and locks, and finally we discuss the various plans for reducing sharing and avoiding deadlock. Judgment walks ahead of hands-on — a principle for the whole volume. The unit's content is split into articles by section, filled in gradually as migration proceeds.

## In This Unit

<ChapterNav variant="sub">
  <ChapterLink href="01-from-wrong-programs">Same Counter, Two Threads — Why Does It Compute Wrong?</ChapterLink>
  <ChapterLink href="02-mutex-and-locks">How Do We Get the Lost Counts Back? — std::mutex and the Three Scoped Locks</ChapterLink>
  <ChapterLink href="03-lock-scope">How Much Should a Lock Cover: Protection Scope, Granularity, and Less Sharing</ChapterLink>
  <ChapterLink href="04-check-then-act">Where Is the Boundary of One Operation</ChapterLink>
  <ChapterLink href="05-interface-leaks">The Boundary Handed to the Caller: Four Ways Interfaces Leak</ChapterLink>
  <ChapterLink href="06-deadlock-defenses">What If Nobody Lets Go: The Deadlock Scene and Its Defenses</ChapterLink>
  <ChapterLink href="07-three-arrangements">Don't Share If You Don't Have To: Choosing Among Three Arrangements</ChapterLink>
</ChapterNav>
