---
title: "Site Iteration Cadence"
description: "Content production, site maintenance, PR/Issue handling, and release cadence for Tutorial_AwesomeModernCPP"
chapter: 1
order: 1
tags: ["工程实践"]
---

# Site Iteration Cadence

Tutorial_AwesomeModernCPP is driven primarily by content production. Version numbers measure the size of content progress. Site maintenance, PRs, and Issues support the main content path instead of taking it over.

## Basic Rhythm

Maintainers usually run a lightweight iteration every 2 to 3 days. Each iteration should have one main goal:

- Finish a related group of content.
- Fix a batch of reading problems.
- Complete code, links, or translations for a chapter.
- Handle clearly actionable PRs or Issues.

One iteration does not need to cover every direction. Volume roadmaps, long-term candidates, and future themes remain in `todo/`. Temporary article-level ideas should not become new governance files.

## Per-Iteration Flow

Each maintenance iteration follows this order:

1. Review the current P0/P1 TODO goals and choose one main content target.
2. Quickly check Issues and PRs, handling only items that are actionable, release-relevant, or reader-blocking.
3. Complete the content, example code, indexes, and required English/Chinese sync for the iteration.
4. Run the quality checks that match the change scope.
5. If the change is reader-visible, update the changelog or prepare the next release entry.

PRs and Issues should be checked at least once per iteration. Urgent problems may interrupt the cycle, such as broken site builds, important 404 pages, misleading example code, or external contributions that need quick feedback.

## Version Cadence

Version numbers describe the size of change; they should not force the writing schedule.

- patch: typo fixes, links, site fixes, and low-risk text corrections.
- minor: one volume or topic has clearly moved forward, giving readers a new learning path or complete capability.
- major: TODO structure, site architecture, or the content system changes substantially.

Patch releases can ship as needed. Minor releases usually use a 2 to 4 week observation window and ship only when a topic forms a complete increment. Major releases should stay rare to avoid repeatedly changing reader and contributor entry points.

## Tags and Releases

Tags and GitHub Releases are used separately. Tags mark lightweight maintenance checkpoints so readers can see ongoing progress through the README badge. GitHub Releases are reserved for content versions that readers should explicitly notice.

- Patch-level fixes may be tagged without creating a GitHub Release.
- Minor topic increments should usually create a Release with a changelog.
- Major structural changes must create a Release and explain migration impact.

This keeps project activity visible without overwhelming readers with Release notifications.

## Definition of Done

A content iteration should usually satisfy these conditions:

- The article can be read independently, with terms and C++ standard versions clearly marked.
- Related volume pages, chapter indexes, or navigation entries are updated.
- Example code in the article can compile, or platform/toolchain limits are explicitly stated.
- Key Chinese and English pages stay in sync; community drafts and low-priority long-form notes may be translated later.
- Internal links pass checks, and the production build succeeds.

For local fixes, run only the relevant checks. Before a release, run the full pre-release checks.

## PR and Issue Handling

Issues are for actionable problems, Discussions are for open-ended learning conversations, and PRs are for concrete changes.

Handle items in this order:

1. Problems that block builds, deployment, or major reading paths.
2. Clear, low-risk fixes already submitted as PRs.
3. Content suggestions directly related to the current iteration theme.
4. Learning questions that can become QA entries, appendix material, or future TODO items.

Learning questions should not fill the Issue list directly. High-quality discussions can be summarized into FAQ entries, appendix pages, or links from the main content.

## Changelog Principles

The changelog should describe reader-visible changes, not just file counts.

Prefer recording:

- Which learning path was added or completed.
- Which examples can now run or be verified.
- Which site entries, search behavior, navigation, or community flows improved.
- Which contributors helped fix specific problems.

File counts, line counts, and commit counts can be supporting data, but they should not replace the explanation of what changed.

## Common Checks

Choose checks by change scope during daily maintenance:

```bash
pnpm check:links
python3 scripts/validate_frontmatter.py
python3 scripts/check_quality.py documents/
python3 scripts/build_examples.py --host
```

Before a release, run:

```bash
pnpm check:links
pnpm build
pnpm coverage:update
python3 scripts/validate_frontmatter.py
python3 scripts/check_quality.py documents/
python3 scripts/build_examples.py --host
```

If STM32 examples changed, also run:

```bash
python3 scripts/build_examples.py --stm32
```
