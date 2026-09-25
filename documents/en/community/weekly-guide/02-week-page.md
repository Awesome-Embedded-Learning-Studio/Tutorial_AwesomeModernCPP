---
title: Writing the Week Page: week-NN.md
description: A field-by-field walkthrough of the week page's frontmatter, the body skeleton, a directly copyable template, and source-level constraints such as single-line fields
chapter: 1
order: 2
reading_time_minutes: 4
tags:
  - 工程实践
translation:
  source: documents/community/weekly-guide/02-week-page.md
  source_hash: f9b8d5e3e489be361e4e5b5d3905be5c5ac1184609bff861206dce66b363c2e0
  translated_at: '2026-09-25T09:21:47+00:00'
  engine: anthropic
  token_count: 2700
---

# Writing the Week Page: week-NN.md

The week page is the storefront of an issue: the column list reads its title and dates, learners arriving read its introduction, and the attribution-and-thanks section you write at the end lives in this same file. The page itself is short — the hard parts are all in the frontmatter fields and a few inconspicuous writing constraints. The `week-NN.md` in the template pack already has all of them filled in with sample values; copying it over and editing beats handwriting from a blank file by a mile.

## Where the File Lives and What It's Called

The location is fixed: `documents/weekly-problems/week-NN.md`. The week number is two digits; you keep numbering onward from `week-01` — the previous article covered what happens when the number isn't zero-padded. Strip off the `.md`, and the file name is exactly the name this issue uses to find its problem directory under `code/volumn_codes/weekly-problems/`; the two sides must match.

## frontmatter, Field by Field

| Field | What to fill in | Notes |
|---|---|---|
| `title` | `"Week 2 · Topic Name"` | Shown in the column list and as the week page heading; the convention is the `Week N ·` prefix |
| `description` | One sentence | What this week practices and where the difficulty sits; shown in the list's description slot |
| `chapter` | `16` | Fixed value — the chapter number the weekly column holds in the frontmatter validator |
| `order` | Incrementing from `3` | week-01 took 1 and the solutions page took 2; each new issue keeps counting up — it only affects directory ordering |
| `dateRange` | `"2026-09-21 ~ 09-27"` | The dates this issue covers, shown in the list's time slot; there is no format validation, so follow convention |
| `difficulty` | Pick one of three | `beginner` / `intermediate` / `advanced`; fill in anything else and the validator errors out directly |
| `platform` | `host` | The column's problems all target the host platform for now — write it as is |
| `weeklyThanks` | The thanks list | An array; in each entry `github` holds a GitHub username and `role` a description of the contribution; validated at build time — see the previous article |
| `tags` | Within the whitelist | Pick only from `scripts/tags.json`; the convention is `host` + `cpp-modern` + the difficulty level |

`order` carries a small piece of historical baggage, so let's take a quick look: it exists only to pass validation and to order the directory tree. The ordering of the week list is decided entirely by sorting on the week number, so two issues clashing on `order` doesn't affect the column pages — but it does make a mess of the directory tree. Just keep incrementing.

## Three Writing Constraints

**`title`, `description`, and `dateRange` must each be written on a single line.** The manifest generator extracts these three fields from the source with line-oriented regexes; the moment a value wraps onto a second line, what gets extracted is the broken half. Titles that suddenly lose characters in the column list, descriptions that no longer match — investigations of all of these end up here. However long the description is, squeeze it onto one line and finish it there.

**You don't need to write `sidebar: false` or `aside: false`.** The builder injects both settings into week pages automatically (turning off the sidebar and the on-page outline); writing them by hand in the frontmatter is redundant. Their absence from the template is deliberate.

**Don't invent tags on the spot.** The validator treats `scripts/tags.json` as its whitelist — any tag not on the list fails validation. If you want a new tag, add it in that JSON; don't force it into the article.

## The Body Skeleton

The body has three parts, and we find looking at a real issue faster than reading a description — `week-01.md` is a ready-made reference:

The opening is the introduction: two or three paragraphs of prose covering why this theme was chosen for the week, the common thread running through the problems, and how the difficulty steps up. This is the one stretch of body text where you write freely; it is addressed to learners, and it's worth the effort.

The middle holds the problem cards, one component line per problem:

```md
<QuizProblem src="code/volumn_codes/weekly-problems/week-02/01-your-problem" />
```

`src` is the problem directory's path relative to the repository root, corresponding one to one with the problem directories the next article covers — check them entry by entry. Any number of problems on a page is fine; week-01 has three.

The ending is a horizontal rule followed by an attribution-and-thanks section: state clearly which course, book, or discussion the problems came from and who set them, and attach the GitHub links. When the material is someone else's, write this section as a rule of the column.

## Start from the Template

`code/volumn_codes/weekly-problems/_template/week-NN.md` is a complete week page with sample values filled in under the rules above. Copy it to `documents/weekly-problems/week-02.md` — its frontmatter carries comments explaining what to change in each field. With the copy in place as `documents/weekly-problems/week-02.md`, change the week number, title, dates, and thanks, swap the introduction and the problem-card paths for your own, delete the comments — and the week page is done.

We mentioned the three most common mistakes in the previous article; here they are again, pointed at the week page: a week number not padded to two digits; the week page's file name not matching the problem directory's name (that issue shows zero problems, with no warning); and `weeklyThanks`'s `github` casually filled in with a Chinese nickname (the build fails outright — it must be a GitHub username).
