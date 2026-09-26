---
title: Repository Slimming and Branch Cleanup
description: '2026-06-23 repository infrastructure cleanup — deployment migrates from the gh-pages branch to Actions artifact deployment, the legacy archive branch is removed, and a fresh clone drops from ~500MB to 19MiB'
chapter: 1
order: 2
reading_time_minutes: 6
tags:
  - 工程实践
translation:
  source: documents/community/dev/02-repo-slimdown.md
  source_hash: 397e582e75a6fe1d982f5d5e9d5776f42901f6421dbe824fd83c6261a9adb1d9
  translated_at: '2026-09-26T16:58:28+00:00'
  engine: anthropic
  token_count: 750
---

# Repository Slimming and Branch Cleanup

On 2026-06-23 we gave the repository infrastructure a cleanup that boiled down to two things: **migrating the deployment architecture** (gh-pages branch → GitHub Actions artifact deployment) and **retiring the legacy archive branch** (`archive/legacy_20260415`). Net effect: a fresh clone went from ~500MB down to 19MiB, the remote branches converged to just `main`, and the docs site kept its address and content exactly as they were. This log records the motivation, the changes, the results, and the traps we hit along the way.

## 1. Deployment Migration: gh-pages Branch → Actions Artifact Deployment

### Background: Why the Repository Ballooned to ~500MB

The old deployment used the third-party action `peaceiris/actions-gh-pages`, which committed **the entire build output into the `gh-pages` branch** on every deploy. The root cause of the bloat was VitePress's local search index chunk (`@localSearchIndexroot.*.js`): it is **content-addressed**, so every build gets a new hash, a single copy runs about 8–10MB, and every deploy stuffed one more copy into the `gh-pages` history. Let that accumulate for years and—

- the history had piled up **90 of these blobs, ~437MB in total**
- that was **86% of the total `.git` volume** (the whole repository was ~500MB)
- the 437MB was 100% quarantined in the `gh-pages` branch; `main` stayed clean the whole time (zero search-index blobs in `git rev-list --objects main`)

### What Changed

We switched to GitHub's official artifact deployment trio: the build output is deployed as a **one-shot artifact**, consumed and discarded, **never committed into any branch again**. From then on, each build's search index chunk was just an ordinary file inside the artifact, no longer polluting the history.

- `actions/configure-pages@v5` → `actions/upload-pages-artifact@v3` → `actions/deploy-pages@v4`
- split into two jobs, build / deploy, with `permissions: pages: write + id-token: write`
- kept the per-volume parallel build cache (`.build-cache`), `NODE_OPTIONS=--max-old-space-size=6144` to fend off OOM, and `fetch-depth: 0` (`lastUpdated` needs the full history)

The deployment configuration lives in `.github/workflows/deploy.yml` at the repository root.

### The Result

`git clone` fetches all branches by default, so before the migration it downloaded those 437MB by way of the `gh-pages` branch. After the branch deletion plus a server-side gc, a fresh clone measured:

| | Before migration | After migration |
|---|---|---|
| Clone transfer volume | ~500 MB | **19 MiB** |
| Local disk usage (worktree + `.git`) | ~500 MB | **47 MB** |

### Pitfalls Hit Along the Way (Kept on Record)

1. **Environment branch protection**: once we switched to GitHub Actions deployment, the `github-pages` environment's built-in `branch_policy` only admitted `gh-pages`, and the deploy job died on the spot (`Branch main is not allowed to deploy to github-pages due to environment protection rules`). Fix: add `main` to the environment's deployment-branch-policies.
2. **Leaving Pages Source unswitched keeps things "half working"**: with Source still set to `Deploy from a branch: gh-pages`, the deployments produced by `actions/deploy-pages` override what the branch source was serving — the site keeps running as usual, but it is a fragile state where the label and the reality do not match. You must flip Settings → Pages → Source to `GitHub Actions`, which is also the precondition for safely deleting the `gh-pages` branch (delete it earlier and the site goes down).
3. **The `gh api .../size` field lags badly**: long after the migration + gc finished, the field still read ~500MB and barely ever updated. **Take the fresh clone's transfer volume as the only trustworthy evidence**; do not trust the size field.

## 2. Branch Cleanup: Removing archive/legacy_20260415

### What It Was

`archive/legacy_20260415` was an archive branch from before the 2026-04-15 repository restructure, labeled "pre-restructure archive / Read-only" and kept around during the restructuring so the old state stayed reachable for tracing back.

### Why It Could Be Deleted Now

The restructure had long been finished and stable, with `main` as the one canonical history. Verified: `git rev-list --count archive/legacy_20260415 ^main` = **0** — the branch's tip (`993e8d0`) is an ancestor commit inside `main`'s history, every object is shared with `main`, and **deleting it frees 0 objects**. In other words, its contents were already fully preserved in `main`'s history; keeping it was pure noise in the branch list.

### Impact

A purely cosmetic cleanup: **no effect on repository size** (the size win all came from the gh-pages side) and no effect on any content.

## 3. Net Effect

- Remote branches: `main` + `gh-pages` + `archive/legacy_20260415` → **nothing left but `main`**
- Repository size: fresh clone **~500MB → 19MiB transferred / 47M on disk**
- Docs site: address, content, links, SEO **all unchanged**

## 4. Impact on Contributors

The site and the contribution flow upgraded without anyone noticing. If you carry an old local clone (`.git` still ~500MB), you can optionally slim it down:

```bash
git fetch --prune origin
git gc --prune=now --aggressive
```

Or just clone fresh (a mere 19MiB these days). This step is entirely optional — skipping it will not affect normal use.
