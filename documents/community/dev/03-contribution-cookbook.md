---
title: 贡献速查手册
description: 新贡献者从 clone 到 PR 合并的快速通道——本地门禁、分支卫生、创建可编译的代码示例,以及 CI 红了怎么办
chapter: 1
order: 3
reading_time_minutes: 12
tags:
  - 工程实践
---

# 贡献速查手册

`CONTRIBUTING.md` 是一本很全的贡献百科,但全就意味着长,新人第一次打开往往读完不知道先做哪一步。这本手册是它的快速通道,只讲三件最容易翻车的事:

- 本地门禁没装全,改完一路绿,推上去 CI 才红。
- 分支没整理,PR 里夹了一堆无关 commit,review 和回退都费劲。
- 代码示例只在文章里贴了一份,没人编译过,合并后才发现跑不通。

三件事解决掉,大部分 PR 就能顺顺当当合进去。下面按这个顺序展开。

## 一、30 秒上手

最短路径,照着敲就能跑:

```bash
# 1. fork 后拉到本地
git clone <您的 fork 地址>
cd Tutorial_AwesomeModernCPP

# 2. 装依赖和提交前门禁(见第二节)
# pnpm 百度一下就好，安装需要提前安装 nodejs
# 检查办法：
# node -v
# npm -v
# 然后安装pnpm, 完事后新开一个终端pnpm -v检查
pnpm install
pnpm hooks:install

# 3. 基于最新 main 开分支
git switch main && git pull # 记得拉代码，要不然PR一长串，鼠标起飞了
git switch -c feat/your-feature

# 4. 改东西,提交
git add -A
git commit -m "feat: 一句话说清改了什么"

# 5. 推上去,去 GitHub 开 PR
git push -u origin feat/your-feature
```

装门禁那一步(`pnpm hooks:install`)是关键,第二节会讲为什么不能跳。

## 二、本地门禁 vs CI 门禁

仓库有两层检查:**本地 pre-commit**(提交前跑)和 **CI**(推到 GitHub 后跑)。新贡献者最常踩的坑,是以为装了 pre-commit 就万事大吉,结果 CI 又红一项。

### 先看清楚谁拦谁

| 检查项 | 本地 pre-commit 拦 | CI 兜底 | 本机想提前跑 |
|---|---|---|---|
| markdownlint | 是 | Lint | `pre-commit run --all-files` |
| clang-format(C/C++) | 是 | — | 同上 |
| frontmatter 校验 | 是 | Lint | pre-commit 已覆盖(手动跑 `python3 scripts/validate_frontmatter.py`) |
| 链接、tag、代码引用、图片 | 否 | Content Quality | `python3 scripts/check_quality.py documents/`(只查链接用 `pnpm check:links`) |
| `code/` 下示例编译 | 否 | Build Examples | `python3 scripts/build_examples.py --host` |
| VitePress 构建 | 否 | Build Check | `pnpm build` |

frontmatter 校验现在是本地和 CI 双重把关:pre-commit 接了 `validate_frontmatter.py`(改 `documents/` 下的 `.md` 时触发),CI 的 Lint workflow 会再跑一遍兜底。一个前提是 python3 要装了 PyYAML,否则脚本会自动降级,只查 frontmatter 边界、不校验字段内容(等于放行)。`setup_precommit.sh` 会检测 PyYAML 并提示安装。

::: tip
指望 CI 帮您发现问题是可行的,但每次往返一次 GitHub 要等几分钟。改文章时顺手在本地跑一次 `python3 scripts/check_quality.py documents/`,frontmatter、链接、tag、代码引用一次性查完,能省掉大多数 CI 往返。
:::

### 装本地门禁

```bash
pnpm install        # 装前端依赖(VitePress 等)
pnpm hooks:install  # 装 pre-commit 钩子,等价于 scripts/setup_precommit.sh
```

门禁依赖三个本机工具:`pre-commit`、`python3`、`clang-format`。缺哪个 `pnpm hooks:install` 都会提示。`pre-commit` 一般用 `pipx install pre-commit` 装。

装完之后,每次 `git commit` 前会自动跑:markdownlint、clang-format(对暂存的 C/C++ 文件)、翻译覆盖率更新、大文件和空白检查。如果钩子改了您的文件,pre-commit 会中断本次提交,提示您重新 `git add` 再提交一次。这是正常现象,不是出错。

确实需要临时跳过(比如改文档时只想先存档),可以 `git commit --no-verify`,但别在正经 PR 里长期用。

## 三、保持 PR 干净

仓库用 **squash 合并**。看一下提交历史就能确认:最近合并的每一条 commit 都是 `改动说明 (#PR号)` 的形式,这是 squash 合并的特征:PR 里的所有 commit 被压成一条,标题用的是 PR 标题。

由此推出两件要记牢的事，还请各位需要注意一下~:

1. **PR 标题就是最终进 main 的 commit 标题。** 别随手写 "update" 或 "fix",认真写一句话说清这次改了什么,带上 `feat:` / `fix:` / `docs:` 之类的前缀。
2. **分支里堆几个 WIP commit 不要紧。** squash 合并会把它们压平,最终历史只有一条。所以 review 时让您改,小改直接追加 commit 即可,不必为了整洁去 force push。

## 四、创建可编译的代码示例

这是三件事里价值最高、也最容易被忽略的一件。文章里贴的代码,如果不放进 `code/` 目录走 CI 编译,就没人为它的正确性兜底。读者照着敲,编不过,信任就没了。

### 好消息:基建已经搭好了

C 教程的练习答案可编译版放在 `code/volumn_codes/vol1/c_tutorials/<章>/`。目前 2–8 章有(从文章参考答案抽的),其余章节等文章答案补齐后再抽。约定是:

```text
code/volumn_codes/vol1/c_tutorials/08A-multi-level/
├── CMakeLists.txt
└── ex1_dynamic_matrix.c
```

每个示例一个目录,放 `exN_*.c`(或 `.cpp`)+ `CMakeLists.txt`。`scripts/build_examples.py` 会自动发现 `code/` 下所有 CMake 工程,host 和 STM32 分开编译。也就是说,**只要您把示例按这个约定放好、配上 CMakeLists,CI 就会替您编译它**,不用额外登记。

### 最小 CMake 模板

纯 C 示例(照现行约定):

```cmake
cmake_minimum_required(VERSION 3.20)
project(lesson_08A_multi_level C)

add_executable(ex1_dynamic_matrix ex1_dynamic_matrix.c)
```

C++ 示例(参考 `code/volumn_codes/vol2/ch00-move-semantics/`):

```cmake
cmake_minimum_required(VERSION 3.20)
project(ch00_move_semantics VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(move_semantics_demo move_semantics_demo.cpp)
```

关键是 `project(... C)` 或 `LANGUAGES CXX`,编译器据此选 gcc 还是 g++,不用手动指定。

### 练习题参考答案:文章为主,code/ 抽取

练习题的参考答案走「文章为主、`code/` 抽取可编译版」的流程:

- **文章里的参考答案**由贡献者逐章写(教学版,允许加注释、吐槽),走 PR review。这是答案的「源头」。
- **`code/` 的可编译版**从文章答案**抽取**:`code/volumn_codes/vol1/c_tutorials/<章>/exN_*.c` 里函数实现逐字保留文章版,为能编译补上必要的 `#include`,只有函数实现的在末尾补一个 `main` 做演示。CI 自动编译,验证答案真能跑通。
- 两份**同源**(`code/` 从文章来),不是独立两套。所以**文章答案改了,`code/` 对应文件要同步重抽**,别让两边对不上。

新认领一章:在文章写参考答案(走 review),再把可编译版抽进 `code/`(逐字函数 + 补 `#include`/`main` + CMakeLists)。(教学正文里讲概念用的代码片段不受这个约束,该贴照贴。)

### 一个历史坑别踩

仓库里还有个老的 `code/examples/` 目录,里面是散装的 `.cpp` / `.c` 单文件,**没有 CMakeLists,不进 CI**。那是早期遗留,新示例一律走 `code/volumn_codes/<卷>/<章>/` 这套,别往 `code/examples/` 里加东西。（后面可能会整理和巩固）

### 本地先编一遍再推

```bash
# 没python3, 自己 python -V 瞄一眼是不是python 3.x版本
python3 scripts/build_examples.py --host    # 编所有 host 示例
python3 scripts/build_examples.py --stm32   # 编 STM32 示例(需 arm-none-eabi 工具链)
```

新增示例后,本地跑一次 `--host` 确认编得过,比等 CI 红了再改快得多。

## 五、CI 红了怎么办

CI 主要由这几个 workflow 组成,对应不同的检查:

| Workflow | 查什么 | 典型失败 |
|---|---|---|
| Lint | markdownlint + frontmatter | 格式不规范、frontmatter 缺字段或 tag 非法 |
| Content Quality | 链接、tag、代码引用、图片 | 内部链接指向不存在的文件、tag 不在白名单 |
| Build Examples | `code/` 下所有 CMake 工程 | 示例编不过、CMakeLists 写错 |
| Build Check | VitePress 构建 | 自定义组件语法错、标题末尾有花括号触发 markdown-it 属性语法 |

定位思路是"哪一项红,就用对应的本地命令复现":

- **frontmatter / tag 红**:本地 `python3 scripts/validate_frontmatter.py`,它会指出具体哪个文件、哪个字段。tag 必须来自 `scripts/tags.py` 的白名单(这是单一数据源,改 tag 只改这一处)。
- **链接红**:本地 `pnpm check:links`。不少断链能 `python3 scripts/check_links.py --fix` 自动修。
- **示例编译红**:本地 `python3 scripts/build_examples.py --host` 复现,看是哪个工程的编译错误。
- **VitePress 构建红**:本地 `pnpm build`(并行构建)或 `pnpm build:single`(单体构建)复现。本机内存吃紧时单体构建可能 OOM,用 `pnpm build` 更稳。

一个高频坑:文章标题或小标题末尾带了花括号(`{...}`),会被 markdown-it 当成属性语法处理,导致构建报错或渲染异常。标题里别留裸花括号。

## 六、参考资源

- `CONTRIBUTING.md` — 完整贡献指南,本手册是它的快速通道。
- [网站迭代节奏](01-iteration-cadence.md) — 项目维护和发版节奏。
- `.claude/style/writing-style.md` — 写作人格与代码风格,写正文内容时参考。
- `scripts/tags.py` — tag 白名单,唯一数据源。
- `AGENTS.md` — 用 AI agent 辅助开发时的通用入口。
