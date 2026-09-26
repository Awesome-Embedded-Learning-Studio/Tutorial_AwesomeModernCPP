---
chapter: 1
description: Content production, site maintenance, PR/Issue handling, and release
  cadence for Tutorial_AwesomeModernCPP
order: 1
reading_time_minutes: 4
tags:
- 工程实践
title: Website Iteration Cadence
translation:
  source: documents/community/dev/01-iteration-cadence.md
  source_hash: e8f634e26e0b458db58adeb419df7602fa84c46eb4f2f44a82f9c8bf560ca4e8
  translated_at: '2026-09-26T16:58:19+00:00'
  engine: anthropic
  token_count: 1500
---
# Website Iteration Cadence

Iteration in Tutorial_AwesomeModernCPP is content production first; version numbers exist to measure how far the content has advanced. Site maintenance, PRs, and Issue handling serve the main content line — they don't turn around and take over its rhythm.

## The Basic Cadence

We usually run a lightweight iteration every two to three days, and each round is bound to exactly one primary goal:

- Finish a related set of content.
- Fix a batch of problems that get in the way of reading.
- Fill in the code, links, or translations for a specific chapter.
- Handle PRs or Issues that are already clearly actionable.

A single iteration round doesn't try to cover every direction. Volume-level roadmaps, long-term candidates, and far-future topics stay in `todo/`; don't split one-off, single-article ideas off into new governance files.

## A Single Maintenance Round

Each maintenance round moves in this order:

1. Look at the current P0/P1 goals in the TODO and pick one primary content goal.
2. Do a quick pass over Issues and PRs, handling only what is clearly actionable, affects the current version, or blocks readers.
3. Finish this round's content, example code, index updates, and any necessary English synchronization.
4. Run the quality checks that match the scope of the changes.
5. If the changes are perceptible to readers, update the changelog or prepare the next version entry.

PRs and Issues get checked at least once per round. Urgent problems can jump the queue at any time — the site failing to build, an important page going 404, example code that seriously misleads readers, or an external contribution that needs quick feedback.

## Version Cadence

Version numbers describe the size of a change; they don't force the writing pace.

- patch: bug fixes, links, site fixes, low-risk text revisions.
- minor: a volume or topic has clearly advanced, and readers can perceive a new learning path or a complete capability.
- major: the TODO structure, site architecture, or content system has been reworked in a big way.

patch can ship on demand. minor usually runs on a two-to-four-week observation window and ships only when a topic has formed a complete increment. major should stay restrained — avoid frequently changing how readers and contributors perceive the project's entry points.

## Tags and Releases

tags and GitHub Releases are used for different things. A tag marks a lightweight maintenance checkpoint, letting readers see — through the README badge — that the project really is moving; a GitHub Release is reserved for content-bearing versions that readers would actually care to notice.

- patch-level fixes can get just a tag, without creating a GitHub Release.
- minor-level topic advances usually deserve a Release, with a changelog attached.
- major-level structural adjustments must have a Release that explains the migration impact.

This keeps the "project is alive" signal while avoiding Release spam.

## Definition of Done

When a content iteration is finished, it should meet these conditions as far as possible:

- The body text reads on its own, with terminology and standard-version annotations clearly marked.
- The relevant volume homepage, chapter index, or navigation entry has been updated.
- The article's example code compiles, or its platform and toolchain constraints are explicitly stated.
- Key Chinese and English pages stay in sync; community first-issue submissions and low-priority long-form articles can have their translation deferred.
- Internal links pass the checks, and the production build passes.

If the round only made local fixes, running just the relevant checks is fine; if a release is coming, run the full pre-release checks.

## Handling PRs and Issues

Issues are for actionable problems, Discussions are for open-ended learning conversation, and PRs are for concrete changes.

We handle them in this priority order:

1. Problems that block the build, deployment, or main reading paths.
2. Fixes in existing PRs that are clear, low-risk, and easy to merge.
3. Content suggestions directly tied to the current iteration's theme.
4. Learning questions that can be distilled into QA, the appendix, or future TODO items.

Learning questions don't go straight into the Issue list; high-quality discussions can be organized into an FAQ, appendix material, or links from the main text.

## Changelog Principles

A changelog should record changes readers can perceive, not a raw tally of files.

What we recommend recording:

- Which learning path was added or completed.
- Which examples now run or can be verified.
- Which site entry points, search, navigation, or community workflows improved.
- Which contributors helped fix concrete problems.

File counts, line counts, and commit counts can serve as supporting data, but they shouldn't replace a description of what changed.

## Common Checks

For everyday iterations, pick checks based on the scope of the change:

```bash
pnpm check:links
python3 scripts/validate_frontmatter.py
python3 scripts/check_quality.py documents/
python3 scripts/build_examples.py --host
```

Before a release, we recommend running:

```bash
pnpm check:links
pnpm build
pnpm coverage:update
python3 scripts/validate_frontmatter.py
python3 scripts/check_quality.py documents/
python3 scripts/build_examples.py --host
```

If the changes touch the STM32 examples, also run:

```bash
python3 scripts/build_examples.py --stm32
```
