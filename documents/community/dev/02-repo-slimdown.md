---
title: 仓库瘦身与分支清理
description: 2026-06-23 仓库基础设施清理——部署从 gh-pages 分支迁移到 Actions artifact 部署,移除遗留存档分支,fresh clone 从 ~500MB 降到 19MiB
chapter: 1
order: 2
reading_time_minutes: 6
tags:
  - 工程实践
---

# 仓库瘦身与分支清理

2026-06-23 做了一次仓库基础设施清理,两件事:**部署架构迁移**(gh-pages 分支 → GitHub Actions artifact 部署)和**遗留存档分支下线**(`archive/legacy_20260415`)。净效果:fresh clone 从 ~500MB 降到 19MiB,远程分支收敛到只剩 `main`,文档站地址与内容零变化。这篇日志记录动机、改动、效果和过程中踩到的坑。

## 一、部署迁移:gh-pages 分支 → Actions artifact 部署

### 背景:仓库为什么膨胀到 ~500MB

旧部署用第三方 action `peaceiris/actions-gh-pages`,每次部署都把**整个构建产物 commit 进 `gh-pages` 分支**。根因在 VitePress 的本地搜索索引 chunk(`@localSearchIndexroot.*.js`):它是**内容寻址**的,每次构建换一个 hash,单个约 8–10MB,每次部署往 `gh-pages` 历史里塞一个新副本。经年累月——

- 历史里堆了 **90 个这样的 blob,合计 ~437MB**
- 占 `.git` 总体积的 **86%**(整个仓库 ~500MB)
- 这 437MB 100% 隔离在 `gh-pages` 分支,`main` 分支始终干净(`git rev-list --objects main` 里 0 个搜索索引 blob)

### 改动

换成 GitHub 官方的 artifact 部署三件套:构建产物作为**一次性 artifact** 部署,用完即弃,**不再提交进任何分支**。以后每次构建的搜索索引 chunk 只是 artifact 里一个普通文件,不再污染历史。

- `actions/configure-pages@v5` → `actions/upload-pages-artifact@v3` → `actions/deploy-pages@v4`
- 拆 build / deploy 双 job,`permissions: pages: write + id-token: write`
- 保留分卷并行构建缓存(`.build-cache`)、`NODE_OPTIONS=--max-old-space-size=6144` 防 OOM、`fetch-depth: 0`(`lastUpdated` 需要完整历史)

部署配置在仓库根的 `.github/workflows/deploy.yml`。

### 效果

`git clone` 默认拉所有分支,迁移前会通过 `gh-pages` 分支把那 437MB 一并下载;分支删除 + 服务端 gc 后,fresh clone 实测:

| | 迁移前 | 迁移后 |
|---|---|---|
| clone 传输量 | ~500 MB | **19 MiB** |
| 本地占盘(工作区 + `.git`) | ~500 MB | **47 MB** |

### 过程中踩到的坑(留档)

1. **environment 分支保护**:切到 GitHub Actions 部署后,`github-pages` environment 自带的 `branch_policy` 只放行了 `gh-pages`,deploy job 秒挂(`Branch main is not allowed to deploy to github-pages due to environment protection rules`)。修复:把 `main` 加进 environment 的 deployment-branch-policies。
2. **Pages Source 没切也能「半工作」**:Source 仍是 `Deploy from a branch: gh-pages` 时,`actions/deploy-pages` 产生的部署会盖过分支源在服务,站点照常跑,但这是名实不符的脆弱态。必须把 Settings → Pages → Source 切成 `GitHub Actions`——这也是安全删除 `gh-pages` 分支的前提(否则删了会断站)。
3. **`gh api .../size` 字段严重滞后**:迁移 + gc 完成后该字段仍长期显示 ~500MB,几乎不更新。**以 fresh clone 的传输量为唯一可信证据**,别信 size 字段。

## 二、分支清理:移除 archive/legacy_20260415

### 它是什么

`archive/legacy_20260415` 是 2026-04-15 仓库架构重构前的存档分支,标注为「重构前存档 / Read-only」,重构期间用于保留旧状态以备回溯。

### 为什么现在能删

重构早已完成并稳定,`main` 是唯一正典历史。核实:`git rev-list --count archive/legacy_20260415 ^main` = **0**——该分支 tip(`993e8d0`)是 `main` 历史中的祖先 commit,所有对象都与 `main` 共享,**删除它释放 0 个对象**。也就是说,它的内容早已完整保存在 `main` 历史里,留着只是分支列表的噪音。

### 影响

纯整洁性清理,**不影响仓库体积**(体积收益全在 gh-pages 那条),也不影响任何内容。

## 三、净效果

- 远程分支:`main` + `gh-pages` + `archive/legacy_20260415` → **只剩 `main`**
- 仓库体积:fresh clone **~500MB → 19MiB 传输 / 47M 占盘**
- 文档站:地址、内容、链接、SEO **全部不变**

## 四、对贡献者的影响

站点与贡献流程无感升级。本地若有旧 clone(`.git` 仍 ~500MB),可选瘦身:

```bash
git fetch --prune origin
git gc --prune=now --aggressive
```

或直接重新 clone(现在只要 19MiB)。此步完全可选,不做也不影响正常使用。
