---
chapter: 1
description: Content production, site maintenance, PR/Issue handling, and release
  schedule for Tutorial_AwesomeModernCPP
order: 1
reading_time_minutes: 4
tags:
- 工程实践
title: Website Iteration Cadence
translation:
  source: documents/community/dev/01-iteration-cadence.md
  source_hash: e8f634e26e0b458db58adeb419df7602fa84c46eb4f2f44a82f9c8bf560ca4e8
  translated_at: '2026-06-16T03:26:27.240503+00:00'
  engine: anthropic
  token_count: 556
---
# Site Iteration Rhythm

The iteration of Tutorial_AwesomeModernCPP focuses primarily on content output, while version numbers measure the magnitude of content progress. Site maintenance, PR, and Issue handling serve the main content, rather than dictating the main rhythm.

## Basic Rhythm

Maintainers typically perform a lightweight iteration every two to three days. Each round focuses on a single primary objective:

- Complete a set of related content.
- Fix a batch of issues affecting readability.
- Fill in code, links, or translations for a specific chapter.
- Address actionable PRs or Issues.

A single iteration does not aim to cover all directions. Volume-level roadmaps, long-term candidates, and future topics remain in ``todo/``; do not split temporary, article-level ideas into new governance files.

## Single Round Maintenance Process

Each maintenance round proceeds in the following order:

1. Review current P0/P1 goals in TODO and select a primary content objective.
2. Quickly check Issues and PRs, addressing only those that are actionable, affect the current version, or block readers.
3. Complete content, example code, indices, and necessary English synchronization for the round.
4. Run quality checks matching the scope of changes.
5. If changes are user-perceivable, update the changelog or prepare the next version entry.

PRs and Issues are checked at least once per round. Urgent issues may be queued at any time, such as site build failures, major page 404s, seriously misleading example code, or external contributions requiring rapid feedback.

## Version Rhythm

Version numbers describe the magnitude of changes, rather than forcefully driving the writing rhythm.

- **patch**: Error fixes, links, site fixes, low-risk text revisions.
- **minor**: Significant progress in a volume or topic where readers can perceive new learning paths or complete capabilities.
- **major**: Major adjustments to TODO structure, site architecture, or content system.

patch releases can be made on demand. minor releases usually have an observation window of two to four weeks, and are published only when a topic forms a complete increment. major releases should be restrained to avoid frequently changing the entry perception for readers and contributors.

## Tags and Releases

Tags and GitHub Releases are used separately. Tags mark lightweight maintenance nodes, allowing readers to perceive continuous project activity via README badges; GitHub Releases are reserved for content versions worthy of specific reader attention.

- **patch** level fixes may only be tagged, without creating a GitHub Release.
- **minor** level topic progress should usually create a Release, accompanied by a changelog.
- **major** level structural adjustments must create a Release and explain migration impacts.

This preserves project activity signals while avoiding Release spam.

## Definition of Done

When a content iteration is complete, the following conditions should be met as much as possible:

- The main text can be read independently, with terminology and standard versions clearly marked.
- Relevant volume homepages, chapter indices, or navigation entries are updated.
- Example code in the article compiles, or platform and toolchain limitations are explicitly stated.
- Key pages in Chinese and English are synchronized; community initial publications and low-priority long articles may have translation deferred.
- Internal links pass checks, and production builds succeed.

If the round involves only local fixes, only relevant checks need to be run; if preparing for a release, full pre-release checks should be run.

## PR and Issue Handling

Issues handle actionable problems, Discussions handle open learning exchanges, and PRs handle specific modifications.

Processing priority is as follows:

1. Issues blocking builds, deployment, or major reading paths.
2. Clear, low-risk, easy-to-merge fixes in existing PRs.
3. Content suggestions directly related to the current iteration theme.
4. Learning questions that can be consolidated into QA, appendices, or future TODOs.

Learning questions should not be stuffed directly into the Issue list; high-quality discussions can be organized into FAQs, appendices, or main text links.

## Changelog Principles

Changelogs should describe reader-perceivable changes, rather than simply listing file counts.

Recommended records:

- Which learning paths were added or completed.
- Which examples can now run or be verified.
- Which site entry points, search, navigation, or community processes were improved.
- Which contributors helped fix specific issues.

File counts, line counts, and commit counts can serve as auxiliary data but should not replace change descriptions.

## Common Checks

For daily iterations, select checks based on the scope of changes:

````bash
pnpm check:links
python3 scripts/validate_frontmatter.py
python3 scripts/check_quality.py documents/
python3 scripts/build_examples.py --host
````

Before release, it is recommended to run:

````bash
pnpm check:links
pnpm build
pnpm coverage:update
python3 scripts/validate_frontmatter.py
python3 scripts/check_quality.py documents/
python3 scripts/build_examples.py --host
````

If STM32 examples are changed, also run:

````bash
python3 scripts/build_examples.py --stm32
````
