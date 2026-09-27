---
title: quiz.json and the Six Problem Types
description: The common fields of quiz.json and the required fields of each of the six problem types, the contract for writing tests, a reference table of verbatim build-time error messages, and how to self-check
chapter: 1
order: 4
reading_time_minutes: 5
tags:
  - 工程实践
translation:
  source: documents/community/weekly-guide/04-quiz-json.md
  source_hash: 2c34218610d3dd6bbb0afaaf67381679304a0452670b9bc760416afb9f121823
  translated_at: '2026-09-25T09:36:09+00:00'
  engine: anthropic
  token_count: 3500
---

# quiz.json and the Six Problem Types

`quiz.json` is a problem's configuration hub: type, title, star rating, hints, judging data — everything lives in this one file. It is also the file the build inspects most strictly: get the configuration wrong and the build fails outright. This article covers the required fields of each of the six problem types and the spots where they are easy to get wrong, with the verbatim error messages appended at the end, so you can debug against them instead of digging through the source.

## First, All Six Problem Types

| type | Card label | What the learner does | Extra required fields |
|---|---|---|---|
| `judge-assert` | Implement · function | Write a function online, submit it for judging | `tests` assertion array |
| `judge-io` | Implement · program | Write a complete program online, judged on input/output | `tests` input/output pairs |
| `fill` | Verbal · fill-in | Type in the expected text | `answerText` |
| `choice` | Verbal · choice | Check the correct options | `options` + `answer` |
| `find-bug` | Find the bug · open thinking | Mark the buggy lines in the code | `bugLines` |
| `reveal` | Reflect · reveal | Think first, then expand to see the answer | none |

Our intuition for choosing among them: `judge-assert` suits "implement a function", `judge-io` suits "write a complete program", the two verbal types suit testing concept discrimination, `find-bug` suits testing sharp eyes with a genuinely error-prone piece of code, and `reveal` suits open-ended discussion where no unique answer exists. The `src` directory, judging behavior, and sample configuration for every problem type live under `code/volumn_codes/weekly-problems/examples/`, one directory per type — open the one matching your type and pattern yours after it before you write; that is the least error-prone route.

## Common Fields

`type` picks exactly one of the six from the table above — write it wrong, and this is the first thing the build flags. `title` must be non-empty and shows on the problem card; keep it short, around six characters, because long titles get truncated on the card. `stars` takes 1 to 3 for difficulty, and there is a behavior here worth remembering: when `stars` is not a number, or falls outside 1 to 3, **no error is raised — it is silently treated as 3**. You think you flagged an easy problem; what the learner sees is three stars. Give it a glance after filling in.

`hints` is an array of strings; the learner clicks to unlock one hint at a time, and inline markdown is supported. On calibration: make the first hint a nudge in the right direction, and let the last one go as far as a strong hint that comes close to the answer — but never place the answer itself there. Every type except `reveal` should carry hints; one or two will do.

## judge-assert: How to Write the Assertions

`tests` is an array of strings, and we write each entry as one line of "expression == expected value":

```json
"tests": [
  "count_coins(0) == 1",
  "count_coins(100) == 242"
]
```

It hides an implicit contract; let's take it apart. First, each line is split into "the call" and "the expected value" at the **last** `==`, with the split point searched from right to left, so an expression that itself contains `==` still splits correctly. Empty lines are ignored, but the array must carry at least one entry, otherwise you get `tests 至少要有一条断言`.

Second, every function called in the expression must have a signature exactly identical to the skeleton given in `starter.cpp`. The judge compiles the learner's code together with the generated calling code; if a function name or a parameter does not match, it blows up at compile time, and the learner sees a compile error instead of a verdict — a miserable experience. After writing your tests, check them one by one against the starter's function names.

## judge-io: How to Write the Input/Output Pairs

`tests` becomes an array of objects instead, and we write each group like this: `in` is the complete content of standard input, `out` is the expected standard output, and both must be strings:

```json
"tests": [
  { "in": "2 3", "out": "5" },
  { "in": "-7 4", "out": "-3" }
]
```

Comparison ignores trailing whitespace on each line and blank lines at the end, so whether or not the expected output ends with a newline counts as correct — you can rest easy on that point. To simulate multi-line input, just embed `\n` directly in the `in` string. At least one group is required here as well; forgetting the quotes on `in` or `out` (writing a bare number) reports `tests 第 N 组的 in/out 都必须是字符串`.

## The Three Verbal Types and Find-the-Bug

`fill` takes `answerText`: the text the learner submits is compared against it, so the expected value must be written without ambiguity — several synonymous phrasings with different word orders cannot all be counted correct. When we set such a problem, we pin down the answer's exact shape in the statement, for example "output two numbers, separated by a space".

`choice` takes `options` (at least two) and `answer`. `answer` is a 1-based array of line numbers: `[1, 3]` means the first and the third options are correct, and multiple selections are supported. A line number beyond the number of options errors out, and the message carries how many options there currently are — handy for us to locate the mistake.

`find-bug` takes `bugLines`, a 1-based array of line numbers counting the lines of `code.cpp`. Our recommended order for authoring this type: finish the buggy `code.cpp` first, then go back and count the line numbers; any change to the code forces a recount, because once the line numbers are off, the learner can never mark them correctly.

`reveal` has no extra required fields; the answer simply lives in the solution at `solution/*/answer.md`. Until the learner clicks to expand, the statement is all the guidance we provide.

## Error Message Reference Table

The build stage and the page runtime use the same set of validations, and we list the verbatim error messages below: search your build log or a problem card's error banner for the text in the left column, and you have located the cause:

| Verbatim error message | Fix |
|---|---|
| `type 必须是 judge-assert / judge-io / fill / choice / find-bug / reveal 之一,现在的是:…` | the `type` field is misspelled; pick one of the six from the table above |
| `title 必须是非空字符串` | `title` was forgotten, or written as an empty string |
| `【题名】判题类必须有 tests 数组` | a judge-type problem is missing `tests`, or it is not an array |
| `tests 断言语法应为"表达式 == 期望值",这行不合法:…` | the `==` in that assertion line is misplaced: the line starts with `==`, or ends without an expected value |
| `tests 至少要有一条断言` / `tests 至少要有一组 {"in": …, "out": …}` | `tests` is an empty array |
| `tests 第 N 组的 in/out 都必须是字符串` | some judge-io group is missing `in` or `out`, or a value is not a string |
| `【题名】choice 必须有 options(至少两项)` | `options` is missing, or has fewer than two entries |
| `【题名】answer 应是 options 的 1-based 行号数组,当前 options 共 N 项` | `answer` is missing, or is not an integer between 1 and N |
| `【题名】fill 必须有 answerText(期望文本)` | `fill` is missing its expected text |
| `【题名】find-bug 必须有 bugLines(1-based 行号数组,行数由 code.cpp 决定)` | `bugLines` is missing, or is not an array of positive integers |

## How to Self-Check After Filling It In

Your fastest self-check is `pnpm dev`: bring the dev server up and refresh the page. If the configuration parses, the problem cards render normally; if something is wrong, the broken-card notice or a page error shows up immediately, and changes take effect on the spot — no restart needed. For the full build validation, run `pnpm build`: the manifest generation stage runs every `quiz.json` through the checks above, and not a single one escapes. The complete pre-submission self-check list is in [the fifth article](05-solutions-and-checklist.md).
