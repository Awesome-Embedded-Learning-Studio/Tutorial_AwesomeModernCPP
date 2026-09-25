---
title: Contribution Cookbook
description: The fast lane for new contributors, from clone to a merged PR — local
  gates, branch hygiene, creating compilable code examples, and what to do when CI
  goes red
chapter: 1
order: 3
reading_time_minutes: 12
tags:
  - 工程实践
translation:
  source: documents/community/dev/03-contribution-cookbook.md
  source_hash: a98ea8c7e5a0baa68344f7fd645ebcb154f3f210e6722fc9d58f65aaf6cf20f1
  translated_at: '2026-09-25T09:22:23+00:00'
  engine: anthropic
  token_count: 4500
---

# Contribution Cookbook

`CONTRIBUTING.md` is a fully stocked encyclopedia of contributing, but fully stocked also means long — a newcomer opening it for the first time often finishes reading without knowing which step to do first. This handbook is its fast lane, covering only the three things most likely to go wrong:

- The local gates aren't fully installed: everything stays green after your edits, and only the push to CI turns red.
- The branch wasn't tidied up: the PR drags in a pile of unrelated commits, making both review and rollback painful.
- The code example exists only as a listing pasted into the article: nobody ever compiled it, and it turns out to be broken only after the merge.

With those three out of the way, most PRs merge smoothly. The rest of this handbook follows that order.

## 1. Get Started in 30 Seconds

The shortest path — type it verbatim and it runs:

```bash
# 1. Clone your fork to your machine
git clone <your fork URL>
cd Tutorial_AwesomeModernCPP

# 2. Install dependencies and the pre-commit gates (see Section 2)
# For pnpm, just look it up online; installing it requires nodejs first
# How to check:
# node -v
# npm -v
# Then install pnpm; afterwards open a new terminal and verify with pnpm -v
pnpm install
pnpm hooks:install

# 3. Branch off the latest main
git switch main && git pull # remember to pull, or the PR comes with a mile-long commit list and your mouse wheel takes off
git switch -c feat/your-feature

# 4. Make your changes, commit
git add -A
git commit -m "feat: describe what you changed in one line"

# 5. Push and open a PR on GitHub
git push -u origin feat/your-feature
```

The gate-installation step (`pnpm hooks:install`) is the critical one; Section 2 explains why you can't skip it.

## 2. Local Gates vs CI Gates

The repo has two layers of checks: **local pre-commit** (runs before each commit) and **CI** (runs after you push to GitHub). The trap new contributors step into most often is assuming that installing pre-commit means all is well — and then CI flags one more failure.

### First, See Clearly Who Blocks What

| Check | Caught by local pre-commit | CI backstop | Run locally in advance |
|---|---|---|---|
| markdownlint | Yes | Lint | `pre-commit run --all-files` |
| clang-format (C/C++) | Yes | — | Same as above |
| Frontmatter validation | Yes | Lint | Already covered by pre-commit (run manually with `python3 scripts/validate_frontmatter.py`) |
| Links, tags, code references, images | No | Content Quality | `python3 scripts/check_quality.py documents/` (for links only, use `pnpm check:links`) |
| Compiling examples under `code/` | No | Build Examples | `python3 scripts/build_examples.py --host` |
| VitePress build | No | Build Check | `pnpm build` |

Frontmatter validation is now double-guarded, locally and in CI: pre-commit wires in `validate_frontmatter.py` (triggered when `.md` files under `documents/` change), and CI's Lint workflow runs it again as a backstop. One precondition: your python3 must have PyYAML installed; otherwise the script automatically degrades to checking only the frontmatter boundaries without validating field contents (which amounts to letting everything through). `setup_precommit.sh` detects PyYAML and prompts you to install it.

::: tip
Counting on CI to catch your problems is workable, but every round trip to GitHub costs a few minutes of waiting. When editing an article, run `python3 scripts/check_quality.py documents/` locally while you're at it — frontmatter, links, tags, and code references get checked in one pass, sparing you most CI round trips.
:::

### Installing the Local Gates

```bash
pnpm install        # install frontend dependencies (VitePress etc.)
pnpm hooks:install  # install the pre-commit hooks, equivalent to scripts/setup_precommit.sh
```

The gates depend on three local tools: `pre-commit`, `python3`, and `clang-format`. Whichever one is missing, `pnpm hooks:install` will tell you. `pre-commit` is usually installed with `pipx install pre-commit`.

Once installed, every `git commit` automatically runs: markdownlint, clang-format (on staged C/C++ files), translation-coverage updates, and large-file and whitespace checks. If a hook modifies your files, pre-commit aborts that commit and asks you to `git add` again and commit once more. That's normal behavior, not an error.

If you genuinely need to skip it temporarily (say, while editing docs you just want a checkpoint), `git commit --no-verify` works — but don't make a habit of it in a serious PR.

## 3. Keeping Your PR Clean

The repo uses **squash merges**. A glance at the commit history confirms it: every recently merged commit takes the form `change description (#PR number)` — the signature of squash merging: all commits in the PR get squeezed into one, titled with the PR title.

From this follow two things worth committing to memory — and we really do mean you:

1. **The PR title becomes the commit title that lands in main.** Don't dash off "update" or "fix"; write a proper one-liner saying what this change does, with a prefix like `feat:` / `fix:` / `docs:`.
2. **A few WIP commits piled up in your branch don't matter.** Squash merging flattens them; the final history has exactly one entry. So when review asks for changes, just append a commit for small fixes — no need to force-push for the sake of tidiness.

## 4. Creating Compilable Code Examples

Of the three, this one carries the most value and gets ignored the most easily. Code pasted into an article, if it never lands in the `code/` directory to be compiled by CI, has nobody underwriting its correctness. Readers type it out, it fails to compile, and the trust is gone.

### Good News: The Infrastructure Is Already There

Compilable versions of the C tutorial exercise answers live under `code/volumn_codes/vol1/c_tutorials/<chapter>/`. Chapters 2–8 exist so far (extracted from the article reference answers); the remaining chapters will be extracted once the article answers are complete. The convention is:

```text
code/volumn_codes/vol1/c_tutorials/08A-multi-level/
├── CMakeLists.txt
└── ex1_dynamic_matrix.c
```

One directory per example, containing `exN_*.c` (or `.cpp`) plus a `CMakeLists.txt`. `scripts/build_examples.py` automatically discovers every CMake project under `code/` and compiles host and STM32 separately. In other words, **as long as you place the example following this convention and give it a CMakeLists, CI compiles it for you** — no extra registration needed.

### A Minimal CMake Template

A pure C example (following the current convention):

```cmake
cmake_minimum_required(VERSION 3.20)
project(lesson_08A_multi_level C)

add_executable(ex1_dynamic_matrix ex1_dynamic_matrix.c)
```

A C++ example (see `code/volumn_codes/vol2/ch00-move-semantics/`):

```cmake
cmake_minimum_required(VERSION 3.20)
project(ch00_move_semantics VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(move_semantics_demo move_semantics_demo.cpp)
```

The key part is `project(... C)` or `LANGUAGES CXX` — that's how the build picks gcc versus g++; you never specify it manually.

### Exercise Reference Answers: Articles First, Extracted into code/

Reference answers for exercises follow an "articles as the source, `code/` holds the extracted compilable version" workflow:

- **The reference answers in the articles** are written chapter by chapter by contributors (teaching edition — comments and grumbling allowed) and go through PR review. They are the "source" of the answers.
- **The compilable versions in `code/`** are **extracted** from the article answers: in `code/volumn_codes/vol1/c_tutorials/<chapter>/exN_*.c`, the function implementations are kept verbatim from the article version, the `#include`s needed to compile are added, and entries that contain only a function implementation get a `main` appended at the end as a demo. CI compiles them automatically, verifying that the answers truly run.
- The two copies **share one source** (`code/` derives from the articles); they are not two independent sets. So **when an article's answer changes, the corresponding `code/` file must be re-extracted in sync** — don't let the two sides drift apart.

Claiming a new chapter: write the reference answers in the article (through review), then extract the compilable version into `code/` (verbatim functions + added `#include`/`main` + CMakeLists). (Code snippets used to explain concepts in the teaching prose are exempt from this constraint — paste those as the article needs.)

### One Historical Trap to Avoid

The repo still has an old `code/examples/` directory holding loose single `.cpp` / `.c` files — **no CMakeLists, never touched by CI**. It's an early-days leftover; new examples always go through the `code/volumn_codes/<volume>/<chapter>/` scheme — don't add anything to `code/examples/`. (It may get cleaned up and consolidated later.)

### Compile Locally Once Before Pushing

```bash
# No python3? Run python -V yourself and check it's a 3.x version
python3 scripts/build_examples.py --host    # build all host examples
python3 scripts/build_examples.py --stm32   # build the STM32 examples (needs the arm-none-eabi toolchain)
```

After adding an example, run `--host` locally once to confirm it compiles — far faster than waiting for CI to go red and fixing it after the fact.

## 5. What to Do When CI Goes Red

CI consists mainly of these workflows, each covering different checks:

| Workflow | What it checks | Typical failures |
|---|---|---|
| Lint | markdownlint + frontmatter | Non-conforming formatting, missing frontmatter fields, or an illegal tag |
| Content Quality | Links, tags, code references, images | An internal link pointing to a nonexistent file, a tag not on the whitelist |
| Build Examples | Every CMake project under `code/` | An example that fails to compile, a miswritten CMakeLists |
| Build Check | The VitePress build | A custom component with a syntax error, curly braces at the end of a heading triggering markdown-it's attribute syntax |

The debugging principle: whichever item goes red, reproduce it locally with the matching command:

- **frontmatter / tag failures**: run `python3 scripts/validate_frontmatter.py` locally; it points out the exact file and field. Tags must come from the whitelist in `scripts/tags.py` (the single source of truth — change tags only there).
- **Link failures**: run `pnpm check:links` locally. Quite a few broken links can be auto-fixed with `python3 scripts/check_links.py --fix`.
- **Example-compilation failures**: reproduce locally with `python3 scripts/build_examples.py --host` and see which project's build errors out.
- **VitePress build failures**: reproduce locally with `pnpm build` (parallel build) or `pnpm build:single` (single-process build). On a memory-tight machine the single-process build can OOM; `pnpm build` is the steadier choice.

One high-frequency trap: a heading ending in curly braces (`{...}`) gets parsed by markdown-it as attribute syntax, breaking the build or garbling the rendering. Don't leave bare curly braces in headings.

## 6. Reference Resources

- `CONTRIBUTING.md` — the complete contributing guide; this handbook is its fast lane.
- [Website Iteration Cadence](01-iteration-cadence.md) — the project's maintenance and release rhythm.
- `.claude/style/writing-style.md` — the writing persona and code style; consult it when writing prose.
- `scripts/tags.py` — the tag whitelist, the single source of truth.
- `AGENTS.md` — the general entry point when developing with AI agents.
