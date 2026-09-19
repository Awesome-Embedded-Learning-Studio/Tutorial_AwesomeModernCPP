# AGENTS.md — 「每周一些题」落题规程

给任何 AI coding agent 的目录级入口:在本目录(或其子目录)落新一期题目、投稿题解时,您照本文件执行。人读的机制详解在站点手册 `documents/community/weekly-guide/`(五篇,含全部字段说明与报错对照表),模板在 `_template/`。本文件只放执行规程,两者冲突时以实际代码行为为准(`site/.vitepress/config/weekly-manifest.ts`、`site/.vitepress/theme/utils/quiz-data.ts`)。

## 两类任务

**落一期新周**:输入通常是出题人给的原始材料(题面文本、参考答案、题目出处、大致难度),也可能只有半成品目录。这份材料交到您手上,就由您把它变成完整的一期。

**投稿题解**:您往已有题目目录的 `solution/` 下加一个投稿人文件夹,不动周页面。

## 落题步骤

1. **定周号**:您 `ls documents/weekly-problems/` 找现有最大的 `week-NN`,取下一个。周号永远两位数字(`week-02`,不是 `week-2`)。
2. **复制模板**:
   - `_template/week-NN.md` → `documents/weekly-problems/week-NN.md`
   - `_template/01-your-problem/` → `code/volumn_codes/weekly-problems/week-NN/01-<题名slug>/`
   - `solution/your-github-name/` 改名为出题人或投稿人的 GitHub 用户名。
   多道题就多复制目录,`02-`、`03-` 前缀即展示顺序。
3. **填空**(删光模板里的注释和占位):
   - `problem.md`:纯题面,无 frontmatter,不写标题头;
   - `quiz.json`:按材料选题型(六选一,契约见下);
   - `starter.cpp`:仅判题类(judge-assert 给函数骨架,judge-io 给含 main 的程序骨架);
   - `solution/<用户名>/answer.md`(必有)+ `solution.cpp`(判题类要有);
   - 周页面:frontmatter 全字段 + 引言 + `<QuizProblem src="…"/>` 每题一条 + 出处致谢段。
4. **自检**(见下),`pnpm dev` 眼验,然后按 PR 规范提交。

## 硬规则(违反会静默失败或构建失败)

- 周页面文件名去掉 `.md` 必须与本目录下的题目目录**同名**。不同名不报错,那一期零道题。
- 周页面 frontmatter 的 `title` / `description` / `dateRange` 必须各自**单行**——栏目数据是按行正则提取的,折行即残缺。
- `weeklyThanks[].github` 必须是合法 GitHub 用户名(字母开头结尾,1–39 位,字母数字连字符),`role` 非空;不合法构建直接失败。
- `solution/` 下投稿人子目录**必须含 `answer.md`**,缺失构建失败。
- `quiz.json` 契约:判题类(`judge-assert`/`judge-io`)必须有非空 `tests`;assert 按**最后一个 `==`** 切分「表达式 == 期望值」,函数名须与 starter 一致;io 的 `in`/`out` 必须是字符串;`choice` 要 `options`(≥2)+ `answer`(1-based 行号数组);`fill` 要 `answerText`;`find-bug` 要 `bugLines`(1-based,数 `code.cpp` 的行)。
- `stars` 非法时**静默取 3**,不报错——填完核对一眼。
- 任何出题文件里**不得出现 `##JUDGE##`**:那是站点判题器生成的内部协议标记,写了会干扰判题。
- 周页面 `difficulty` 只能 beginner / intermediate / advanced;`tags` 只能从 `scripts/tags.json` 白名单取。
- `examples/` 与 `_template/` 不进周列表,别把正式题放进去;也别动这两个目录的内容(除非模板本身要改)。

## 自检命令

在仓库根执行,Python 咱们一律用 `.venv/bin/python`(系统 python 缺 PyYAML,结果不可信):

```bash
.venv/bin/python scripts/validate_frontmatter.py   # 周页面 frontmatter
pnpm check:links                                    # 文档链接
.venv/bin/python scripts/check_quality.py documents/
pnpm test:weekly                                    # weekly 机制回归
pnpm build                                          # 完整构建(manifest 校验在这步爆)
```

`pnpm dev` 眼验清单,您逐项确认:栏目首页新周出现在最前;周页题卡渲染;参考答案提交判题通过;题解页出现投稿人条目。dev 下改目录刷新即生效,无需重启。

## 正文写作约束

周页面引言、出处致谢段、`answer.md` 都是站内中文正文,不是代码注释。动笔前读 `.claude/style/writing-style.md`(已提交,写作人格与去 AI 味规则);维护者环境另有更全的 `.claude/tools/content_forge/writing_style.md` 与 `tools/humanizer_lint.py`,能跑则跑。要点:自称笔者;拉着人一起走用咱们;称呼对方用您。口语可以留,比喻和框架词不要;`answer.md` 讲思路带排错段,参考 `week-01/01-count-coins/solution/charliechen114514/answer.md` 的结构。

## PR 规范

分支、squash、CI 排错您看 `CONTRIBUTING.md` 与 `documents/community/dev/03-contribution-cookbook.md`,此处咱们不重复。PR 标题按仓库惯例写清改了什么(如 `feat(weekly-problems): Week 2 · xxx 三题`)。
