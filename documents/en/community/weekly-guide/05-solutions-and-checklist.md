---
title: Solutions, Self-Checks, and a Pre-Launch Rehearsal
description: The submission rules for the solution directory and how to write answer.md, the local self-check commands to run before submitting, and the steps of a rehearsal that walks the whole pipeline with the week-99 trial week
chapter: 1
order: 5
reading_time_minutes: 4
tags:
  - 工程实践
translation:
  source: documents/community/weekly-guide/05-solutions-and-checklist.md
  source_hash: 6813f8d6c6895a87215293a1dc765f0c170a479d7cc31853d65b82ad4a24f7f9
  translated_at: '2026-09-25T09:22:42+00:00'
  engine: anthropic
  token_count: 2400
---

# Solutions, Self-Checks, and a Pre-Launch Rehearsal

Solutions are the contribution this column welcomes most: the entry bar is low, and one solution is one folder's worth of work. This article covers how to submit a solution, how to self-check the whole PR before submitting it, and finally walks you through the entire pipeline with a trial week — once that passes, setting a real week is just repeating the same moves.

## Ground Rules for the Solution Directory

It lives under `solution/` inside the problem's directory: one subfolder per contributor, and the folder name is your **GitHub username**. This is not an arbitrary convention: the solutions page renders the folder name as a link to `github.com/<username>`, so if you fill in a Chinese nickname, the link leads straight out to a 404.

Inside your folder, `answer.md` is mandatory — it is the body of the solution — and judge types usually add a `solution.cpp` reference implementation alongside. The build checks both facts: a subfolder missing its `answer.md` fails the build; `solution.cpp` is optional — the verbal-answer and find-bug types usually don't carry one, and problems 03, 04, and 06 in the examples directory `code/volumn_codes/weekly-problems/examples/` have none, yet they are live all the same. When several people submit solutions for the same problem, that just means a few more folders, displayed side by side, none disturbing the others.

## What answer.md Should Look Like

`answer.md` is written for learners who have finished the problem (or gotten stuck on it), and we hold it to exactly one standard: the learner genuinely understands after reading. The fastest way to learn what a good solution looks like is to study a finished one — `code/volumn_codes/weekly-problems/week-01/01-count-coins/solution/charliechen114514/answer.md` is the column's first solution, and its structure is worth borrowing: an opening paragraph that makes the cracking idea clear — how the cases are partitioned, why the boundaries fall where they do; then a section-by-section explanation against `solution.cpp`, code and prose alternating; and a dedicated "debugging" section at the end that takes the wrong version a learner is most likely to write and explains where it goes wrong and what it actually outputs when run.

Plain markdown, no frontmatter. As you write, put yourself back in the position of "the person stuck on this problem": explain at length wherever that person gets stuck; anything that reads fine at a glance in the code does not need to be restated in prose. In the template pack, `solution/your-github-name/answer.md` provides the skeleton of this structure with comments — start from it and modify.

## The Submission Flow and Local Self-Checks

The repo's general contribution flow (branch hygiene, squash merges, what to do when CI goes red) is covered in full in the [Contribution Cookbook](../dev/03-contribution-cookbook.md); we won't repeat it here. The self-checks specific to a problem-setting PR are just the few commands below — run them once before submitting, and you save yourself most of the CI round-trips:

| What to check | Command |
|---|---|
| frontmatter (week-page fields) | `.venv/bin/python scripts/validate_frontmatter.py` |
| document links | `pnpm check:links` |
| content quality (links, tags, images) | `.venv/bin/python scripts/check_quality.py documents/` |
| regression tests for the weekly mechanism | `pnpm test:weekly` |
| visual check of the pages | `pnpm dev` |

For Python commands we always go through the repo root's `.venv/bin/python`: the system `python3` lacks PyYAML, so its validation results cannot be trusted — the cookbook stresses this too. During the visual check, remember that once `pnpm dev` is up, changes to the directories take effect on a browser refresh — no dev restart needed.

## Pre-Launch Rehearsal: Walk the Whole Pipeline with week-99

Real week numbers are precious — a shame to burn them on trial and error. The full-pipeline rehearsal uses a `week-99` that is destined to be deleted; work through the five steps and you'll have the mechanism down:

1. Copy the template: copy `code/volumn_codes/weekly-problems/_template/week-NN.md` to `documents/weekly-problems/week-99.md`; on the `code/` side, first `mkdir week-99/`, then copy the `01-your-problem/` directory into it and rename it to `01-hello/` (a bare `cp -r` aimed at a path that doesn't exist yet leaves the problem one directory level short — a pitfall we genuinely fell into during rehearsals); finally, rename `solution/your-github-name/` to your own username.
2. Fill in the blanks: write the simplest possible problem statement (say, read two numbers and print their sum), fill in `quiz.json` following the template, and put a few runnable lines into each of `starter.cpp`, `answer.md`, and `solution.cpp`; in the week page, change the `src` of `QuizProblem` to `week-99/01-hello`, and delete all the frontmatter comments.
3. Visual check: bring the server up with `pnpm dev` and open the column home page — `week-99` should sit at the very front of the list (the largest week number). Enter the week page and you will see the problem card render properly; paste the reference answer into the editor and submit it for judging, and everything should pass. Then open the solutions page, and your username should appear under that problem.
4. While you're at it, verify once that the build-time validation is real: temporarily rename `answer.md`, refresh the page, and you should see a manifest error; rename it back and everything returns to normal.
5. Delete everything: once the visual check is done, delete both `documents/weekly-problems/week-99.md` and `code/volumn_codes/weekly-problems/week-99/`, refresh, and confirm the column is back to its original state. `git status` should show no trace of either path.

Passing the rehearsal means you have walked every link of the "template → directory → judging → solutions → validation" chain. From then on, setting a real week is: change the week number from 99 to the next official number, swap in real problems, open the PR, wait for CI to go green and merge — and the next issue goes live.
