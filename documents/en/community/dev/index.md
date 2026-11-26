---
title: "Project Development"
description: "Project maintenance cadence, development logs, release governance, and site evolution records under community collaboration"
translation:
  source: documents/community/dev/index.md
  source_hash: f4a2bf9a41d7fd89cb747d06180dc147fcdae569f2ee4cdd8abb97867a3c8f31
  translated_at: '2026-09-26T16:58:36+00:00'
  engine: anthropic
  token_count: 500
---

# Project Development

This section tracks the development, maintenance, and release cadence of Tutorial_AwesomeModernCPP itself. It lives under the community section because these processes directly serve content collaboration, PR/Issue handling, and the site's long-term maintenance.

This section does not replace `todo/`, `changelogs/`, or `CONTRIBUTING.md`:

- `todo/` records the content roadmap and volume-level planning.
- `changelogs/` records the changes of already released versions.
- `CONTRIBUTING.md` records submission, review, and quality rules.
- `community/dev/` records how maintainers drive the site and its content forward.

## Version History

Wondering what the project has already released? The changelogs are hosted at the root of the GitHub repository:

- 📦 **[Version Changelogs](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/tree/main/changelogs)** — New content and significant changes for every released version.

## Work in Progress

The board below reads the repository's open issues directly, most recently updated first. Labels, sub-issue progress, and comment counts come from GitHub; a card title links back to the original issue, where you can pick it up or follow along.

The board pulls fresh data every five minutes and reuses the session cache on repeated visits. The anonymous API is rate-limited; when the quota runs out the board shows the reset time, and the refresh button still works.

<IssueBoard repo="Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP" en />

## Development Records

<ChapterNav variant="main">
  <ChapterLink num="1" href="01-iteration-cadence">Website Iteration Cadence</ChapterLink>
  <ChapterLink num="2" href="02-repo-slimdown">Repository Slimming and Branch Cleanup</ChapterLink>
  <ChapterLink num="3" href="03-contribution-cookbook">Contribution Cookbook</ChapterLink>
</ChapterNav>

## Usage Principles

The project development section favors maintenance practices that stay valid over the long run over ad-hoc task lists. Short-term tasks still go into the corresponding volume-level TODO, and version changes still go into the changelog.
