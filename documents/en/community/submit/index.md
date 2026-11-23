---
title: "Community Submission Guide"
description: "From a Markdown draft to first publication in the community — submission, credits and the review process"
translation:
  source: documents/community/submit/index.md
  source_hash: c3404c212a790dc0eb1c9b1e2ce1011de56c53ce8a7bf5566cd973a4d522586c
  translated_at: '2026-11-22T16:40:38+00:00'
  engine: glm-5.3-refresh
  token_count: 1802
---

# Community Submission Guide

Have a learning note, engineering experience, source reading or Q&A digest to share? Prepare a Markdown draft and you're ready to start; maintainers will help organize the site metadata and presentation.

[Submit a draft](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/issues/new?template=article-submission.yml) · [Browse community articles](../)

## Two Ways to Submit

1. **Send a draft**: paste the text or a draft link through the submission form above, explaining its sources and licensing. Maintainers help bring the draft into the repository.
2. **Open a PR** with a Markdown file in `documents/community/incoming/`, using the [article submission PR template](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/compare?expand=1&template=article-submission.md).

Include the title, target audience and sources for references, images and code. For code, state the compiler, language standard and verification performed. If submitting on someone's behalf or reposting, identify the original author and explain their permission.

## Credits and Submission Source

The information panel on the article page automatically shows the identity of the earliest traceable Git commit, linked to that commit. This is the submission-source record; submitting on someone's behalf, tidying up or migrating the article may all have been done by someone other than the original author, so a Git commit identity does not prove original authorship. Source archives or shallow clones may lack complete history, and the page shows only what can be traced.

When an original author should be shown, maintainers put the credit into frontmatter `contributors`; the article body doesn't need a duplicate author block. Without separate credits, the page shows only the submission source—maintainers are never marked as authors by default. Say so when submitting if you want anonymous or pseudonymous attribution. Standalone author cards currently require a GitHub account; text-only credits can stay in the draft while maintainers confirm how to display them.

```yaml
community_category: learning-notes
contributors:
  - github: Karl-Heinrich-Marx-520
    name: 游翰灵
    roles: [author]
```

Writing, revising, translating, reviewing, maintaining and proposing map to `author`, `revision`, `translation`, `review`, `maintain` and `idea` respectively. The [authors page](../authors/) shows each author's GitHub avatar and articles, counting only `author` credits; submission sources and change history are shown separately.

## Sections and Review Status

| Section | Metadata value | Content |
| --- | --- | --- |
| Learning notes | `learning-notes` | Concepts, library usage and experiments |
| Engineering | `engineering` | Builds, debugging, tooling and projects |
| Source reading | `source-reading` | Implementation analysis, design trade-offs and source walkthrough |
| Q&A | `qa` | Referenced questions and answers |

The section is decided by `community_category`, which serves its own purpose alongside technical tags. The [community home](../) shows recently published articles, and the [topics page](../topics/) gives each section its own entrance. Once maintainers complete the metadata, articles appear in their lists automatically—no hand-written index needed.

Review status comes from the directory: `incoming/` means first publication and open review; `articles/` means reviewed inclusion. Moving an article updates its directory status automatically.

## From Draft to Reviewed Article

1. Submit the draft with originality, licensing and source information.
2. Maintainers perform basic checks, complete metadata and publish it in the first-publication area.
3. Revise through community discussion and technical review.
4. Once reviewed, move the article into the long-term collection. Articles suited to the main tutorial can be discussed with their authors for further editing.

First publication means the article is publicly readable and open to discussion. Only after the technical review is complete is it marked as reviewed inclusion. To propose problems or submit solutions, use the [weekly problems handbook](../weekly-guide/).
