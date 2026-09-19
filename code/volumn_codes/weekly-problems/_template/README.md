# 每周一些题 · 出题模板包

一套您可以直接复制的骨架:一个周页面 + 一道题(含题解)。复制、改名、填空,上线。

## 复制三步

1. 您先定好周号(两位数字,如 `week-02`),把本目录的 `week-NN.md` 复制为 `documents/weekly-problems/week-02.md`;
2. 先 `mkdir code/volumn_codes/weekly-problems/week-02/`,再把 `01-your-problem/` 整个目录复制进去,改名为 `01-你的题目名/`(直接 `cp -r` 到不存在的路径会少套一层目录);
3. 照各文件里的注释填空,删掉注释;`solution/your-github-name/` 改成您的 GitHub 用户名(页面会拿它渲染成您的主页链接)。

您一道题不够就再复制一个题目目录,用 `02-`、`03-` 前缀控制展示顺序。

## 最容易翻车的两条

- 周页面文件名(去掉 `.md`)必须和 `code/volumn_codes/weekly-problems/` 下的题目目录**同名**。不同名不会报错,只是那一周安安静静零道题。
- 题解目录里 `answer.md` 必须存在。缺了它,构建直接失败。

## 填完之后去哪核对

- 六种题型怎么配、frontmatter 每个字段、构建期会拦什么:读站点手册 `documents/community/weekly-guide/`;
- 填完的本地自检与页面眼验步骤:手册第五篇;
- 让 AI 照着这份模板替您落地:读上级目录的 `AGENTS.md`。
