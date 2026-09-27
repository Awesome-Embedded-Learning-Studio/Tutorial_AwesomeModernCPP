---
title: "The Weekly Problems Handbook — Setting Problems and Writing Solutions"
description: "How the Weekly Problems column sets problems and takes solution submissions: column mechanics, the template pack, judge configuration, and pre-launch rehearsal — for maintainers and community contributors"
translation:
  source: documents/community/weekly-guide/index.md
  source_hash: 065655385270e2e1d477eb1cf009a78aa3f09d1f8a0475fa9e50e2374d5e65dc
  translated_at: '2026-09-25T09:21:59+00:00'
  engine: anthropic
  token_count: 800
---
# The Weekly Problems Handbook — Setting Problems and Writing Solutions

Weekly Problems is the site's online problem-solving column, and both its problems and their solutions come from the community. This handbook covers every way you can take part: where the content lives, how the build discovers it, how a problem gets its judge configuration, how to submit a solution write-up, and how to self-check before going live.

You don't need to read all five chapters before diving in — pick your entry point based on your situation:

If you just want to propose a problem idea, or all you have is a problem statement and a reference answer, simply open an issue or say the word in the chat group; how to hand in the materials and how authorship is credited is spelled out in [chapter one](01-how-it-works.md).

If you want to produce a full week of problems, you don't have to write from scratch: the repo ships a ready-made template pack at `code/volumn_codes/weekly-problems/_template/` — copy it, rename it, fill in the blanks following the comments, then cross-check the details against chapters two through four.

If you only want to submit a solution write-up — the lightest way to participate, just one folder's worth of work — see [chapter five](05-solutions-and-checklist.md).

If you want an AI collaborator to land the problems for you, `code/volumn_codes/weekly-problems/AGENTS.md` is the operating procedure written for AI agents — just feed it that file.

<ChapterNav variant="main">
  <ChapterLink num="1" href="01-how-it-works">How the Column Works — and How to Take Part</ChapterLink>
  <ChapterLink num="2" href="02-week-page">Writing the Week Page: week-NN.md</ChapterLink>
  <ChapterLink num="3" href="03-problem-directory">A Problem's Directory and Files</ChapterLink>
  <ChapterLink num="4" href="04-quiz-json">quiz.json and the Six Problem Types</ChapterLink>
  <ChapterLink num="5" href="05-solutions-and-checklist">Solutions, Self-Checks, and a Pre-Launch Rehearsal</ChapterLink>
</ChapterNav>
