---
title: A Problem's Directory and Files
description: What goes into each of a problem's four pieces, the two shapes of
  judge-type starter code, code.cpp for find-bug problems, and which files each
  of the six problem types needs
chapter: 1
order: 3
reading_time_minutes: 4
tags:
  - 工程实践
translation:
  source: documents/community/weekly-guide/03-problem-directory.md
  source_hash: 1ae0cfe9561c77eb2c8c6ca9e96095679c85b3bde6602b477de138287613cad8
  translated_at: '2026-09-25T09:23:51+00:00'
  engine: anthropic
  token_count: 1000
---

# A Problem's Directory and Files

One problem is one directory, and we put everything it owns inside: the `problem.md` statement, the `quiz.json` judging configuration, the `starter.cpp` initial code, plus a `solution/` folder holding the solutions. There is no registry and no index file; the manifest trusts a single marker — if the directory contains a `quiz.json`, it is a problem; if not, the whole directory is ignored.

## Where the Directory Lives, and How to Name It

Problems live under `code/volumn_codes/weekly-problems/<week-number>/`. The directory name has two parts, like `01-count-coins` or `02-digit-distance`: a numeric prefix plus a short English name for the problem. The prefix decides the order of the problems on that page (directory names sort lexicographically); the short name has low standards — kebab-case, and good enough if the name you pick lets people tell which problem it is.

The `examples/` directory beside them holds six demo problems, one per problem type — whatever a file should look like, they are the authority. When you are unsure while writing a new problem, open the same-type directory under `code/volumn_codes/weekly-problems/examples/` and check against it.

## problem.md: The Statement

Plain markdown: no frontmatter, no heading line — the first line goes straight into the statement. The site takes it and renders it in the browser, so inline code, code blocks, and bold all work. On writing style there is exactly one requirement: we stand in the shoes of a learner who has never seen this problem and state the task in full — what the input or parameters are, what is to be returned, and what constraints must hold (hard requirements like "must use recursion" go in, bolded). For judge types, the statement should also carry one or two concrete examples; before touching the keyboard, learners calibrate their understanding entirely against the examples.

The anti-pattern is writing the statement as a riddle. Every sentence you save in the statement turns into a hint you have to patch into hints later and a debt you have to repay in the solutions — a bad trade.

## starter.cpp: Initial Code for the Judge Types

Only the judge types (`judge-assert` and `judge-io`) need this file; it is simply the initial content of the editor when a learner opens the problem card. The two shapes follow the type: `judge-assert` gets a function skeleton awaiting implementation, whose signature is exactly the one the judge will call, with a one-sentence description in the body; `judge-io` gets a program skeleton containing `main`, with the input-reading part left blank. Compare the starters in `examples/01-count-coins` and `examples/02-sum` respectively and it will click.

In the skeleton, put only the "blanks the learner is supposed to keep writing" — no implementation ideas, and no judging-related markers of any kind. One term deserves an explicit callout: the judging protocol has a `##JUDGE##` marker, an internal token that the site's judge generates and recognizes by itself; having it appear in learner code or in the starter would actually interfere with judging. As a problem setter, you never need to know it exists.

The verbal-answer types (fill, choice, reveal) and the find-bug type (find-bug) have no starter; from start to finish the learner faces only the statement, so all the effort we can put in goes into the statement.

## code.cpp: The Code for find-bug Problems

Exclusive to the `find-bug` type: it holds a complete piece of code that carries a bug, and the learner's task is to mark the offending lines inside it. `bugLines` in `quiz.json` takes the line numbers where the bug lives — 1-based, counted along the lines of `code.cpp`. See `examples/05-dangling-view`.

## Which Types Need Which Files

How to pick the type and how to fill in every `quiz.json` field is [the next article's](04-quiz-json.md) home turf; here's a quick-reference table for us, to check the files against when copying the template pack.

| Type | starter.cpp | code.cpp | Notes |
|---|---|---|---|
| `judge-assert` | Yes | No | Function skeleton; tests are assertions |
| `judge-io` | Yes | No | Program skeleton; tests are input/output pairs |
| `fill` / `choice` / `reveal` | No | No | Verbal-answer interaction; the statement is everything |
| `find-bug` | No | Yes | Mark the buggy lines; `bugLines` counts lines of code.cpp |

Whatever the type, your `solution/` must exist. How to write solutions and the submission rules are [the fifth article's](05-solutions-and-checklist.md) business; here we only cover structure: one subfolder per person, named after the GitHub username, always containing an `answer.md`, and for judge types usually a `solution.cpp` reference implementation alongside. The template pack `code/volumn_codes/weekly-problems/_template/01-your-problem/` lays all these files out for you — copy the directory, rename the files, fill in the content per the comments; three steps and done.

Those are all the files we need to create — no configuration entries to register, no checklist to reconcile. Get the directory right, and the problem is there; get it wrong, and either the whole problem vanishes or the build errors out — both outcomes were covered in the previous article.
