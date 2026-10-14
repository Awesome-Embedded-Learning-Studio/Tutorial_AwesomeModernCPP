---
title: "Community Submission Guide"
description: "Submit Markdown drafts and learn how credits and review status are displayed"
translation:
  source: documents/community/submit/index.md
  source_hash: ce9754df4f0131c330683e6b0ea87025f01459b1dd5e7c054e0e90def650dd69
---

# Community Submission Guide

Share learning notes, engineering experience, source reading or a Q&A digest. Start with a Markdown draft; maintainers can help prepare the metadata and publish the article.

[Submit a draft](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/issues/new?template=article-submission.yml) · [Browse community articles](../)

## Two Ways to Submit

1. **Send a draft** through the submission form. Paste Markdown or link to your draft, and explain its sources and licensing.
2. **Open a PR** with a Markdown file in `documents/community/incoming/`, using the [article submission template](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/compare?expand=1&template=article-submission.md).

Include the title, target audience and sources for references, images and code. For code, state the compiler, language standard and verification performed. If submitting someone else's work, identify the original author and explain their permission.

## Credits and Submission Source

An independent information panel displays the identity from the earliest traceable Git commit, with a link to that commit. This records the submission source; editing, migration or submission on behalf of someone else can be performed by another person. Git identity does not establish original authorship. Archives or shallow clones may lack complete history, so only traceable records are shown.

Optional author credits belong in frontmatter `contributors`; the article body does not need a duplicate author block. Without explicit credits, only the submission source is displayed. Maintainers are never assigned as default authors of community submissions. Request anonymous or pseudonymous attribution when submitting. Author cards currently require a GitHub account; text-only credits may remain in the draft until maintainers confirm how to display them.

```yaml
community_category: learning-notes
contributors:
  - github: Karl-Heinrich-Marx-520
    name: 游翰灵
    roles: [author]
```

Available roles are `author`, `revision`, `translation`, `review` and `maintain`. The [authors page](../authors/) displays GitHub avatars and articles for people credited as `author`. Submission sources and revision history are shown separately.

## Sections and Review Status

| Section | Metadata value | Content |
| --- | --- | --- |
| Learning notes | `learning-notes` | Concepts, library usage and experiments |
| Engineering | `engineering` | Builds, debugging, tooling and projects |
| Source reading | `source-reading` | Implementation and design analysis |
| Q&A | `qa` | Referenced questions and answers |

Maintainers set `community_category` and complete the article metadata. The [community home](../) displays recent articles, while the [topics page](../topics/) provides a separate entrance to each section. Articles appear in their lists automatically. Technical tags remain available for topic indexing.

Review status comes from the directory: `incoming/` means first publication and open review; `articles/` means reviewed inclusion. Moving an article updates its directory status automatically.

## From Draft to Reviewed Article

1. Submit the draft with originality, licensing and source information.
2. Maintainers perform basic checks, complete metadata and publish it in the first-publication area.
3. Revise through community discussion and technical review.
4. Once reviewed, move the article into the long-term collection. Articles suited to the main tutorial can be discussed with their authors for further editing.

First publication allows public reading and discussion. Reviewed status follows technical review. To contribute problems or solutions, use the [weekly problems handbook](../weekly-guide/).
