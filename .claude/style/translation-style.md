# 英文翻译规范（translation-style）

> 适用范围：`documents/` → `documents/en/` 的全部翻译工作。每个翻译 agent 与审查 agent 的**必读输入**。
> 术语翻译的单一权威参考：[documents/en/appendix/terminology.md](../../documents/en/appendix/terminology.md)。
> 中文写作风格见 [writing-style.md](writing-style.md)（人格与语气参照）；英文行文范例见下方「项目声音」。

## 1. 文件与路径

- **严格镜像**：`documents/<相对路径>` → `documents/en/<相同相对路径>`。文件名（含中文序号前缀）一字不改。
- 只写 en 侧文件；**绝不改动 `documents/` 中文侧任何文件**。

## 2. Frontmatter

| 字段 | 处理 |
|---|---|
| `title` / `description` | **译成英文**（title 精炼、description 一句话摘要，可意译但信息守恒） |
| `chapter` / `order` / `reading_time_minutes` / `cpp_standard` / `difficulty` / `platform` | **原样复制**，数值一律与中文源一致 |
| `tags` | **原样保留中文标签**（VALID_TAGS 即中文体系，合法） |
| `prerequisites` / `related` | 引用的是文章标题 → **译成对应英文文章的英文标题** |
| 键顺序 | 与中文源保持一致，不重排 |

正文若以 H1 开头，H1 随 title 一并译英。

## 3. `translation:` 追踪块（必写，置于 frontmatter 末尾）

```yaml
translation:
  source: documents/vol1-fundamentals/ch08/02-single-inheritance.md
  source_hash: d8e4b22bbeec9cdf72d737e2c76a590bcd0c06db569a722c07f5e331a63b5dda
  translated_at: '2026-09-25T08:00:00+00:00'
  engine: anthropic
  token_count: 3200
```

- `source`：中文源相对路径（仓库根起算）
- `source_hash`：**sha256(源文件内容去除所有 `\r` 后的字节)**。计算命令：
  `tr -d '\r' < <中文源路径> | sha256sum`
- `translated_at`：翻译完成时刻，UTC ISO 8601，**带引号**。命令：`date -u +%Y-%m-%dT%H:%M:%S+00:00`
- `engine`：固定 `anthropic`
- `token_count`：中文源 token 粗估（中文按 ~1.5 字符/token，代码与英文按词估），量级正确即可

## 4. 术语

- 一切术语以 [terminology.md](../../documents/en/appendix/terminology.md) 为准，未收录的按 C++ 社区惯例译，保持全篇一致。
- 英文站**不加中文括注**。

## 5. 正文规则

1. **标题一一对应**：H2/H3 数量与层级结构和中文源完全一致；措辞可意译，信息守恒。
2. **段落一一对应**：不合并、不拆分段落；列表项、表格行列结构与中文源一致。
3. **代码块逐字符保留**：包括 `// Standard: C++XX | Platform: host` 标注行、空行。分三类处理：
   - **注释**（`//` 与 `/* */`）：译成英文，保留代码语义与位置。
   - **字符串字面量与程序输出行**：**原样保留中文**——它们是代码真实打印的内容，译了就与实际运行不符。
   - **示意性图块**（```mermaid、```text 等非真实代码的示意图）：**图内中文标签/说明译成英文**，图语法与结构不动（en 侧既有先例：vol8 networking/02-epoll、vol5 ch05/02 均译 mermaid 标签）。
4. 行内代码：**标识符、API 名、标准库名原样**；但其中混入的中文描述性文字（如 `expected<void*, 错误码>`）译成英文（信息守恒优先）。
5. **链接**：`[链接文字](target)` —— 文字译英；`target` **保持原相对路径不变**（镜像结构下同一相对路径自然指向英文对应文件）。外链 URL 原样；锚点保留。
6. **图片**：`![alt](path)` —— alt 译英；path **原样保留**（构建时会把中文侧资产回拷进 en 卷，不要自己复制图片文件）。
7. VitePress 容器（`::: tip / warning / info / details`）类型保留，内容译英。
8. **文学性 H1**：若中文源正文 H1 与 frontmatter title 不同（文学性开头），**H1 保留为独立标题行并按文学语气译英**（标题结构对齐优先，不折叠进段落）；frontmatter title 用批次标题对照表的标准译名。

## 6. 项目声音（英文行文风格）

- **第一人称 "we"**，口语化但技术精确；参照 9 月手工批范例：
  - [documents/en/vol1-fundamentals/ch07/02-io-subscript.md](../../documents/en/vol1-fundamentals/ch07/02-io-subscript.md)
  - [documents/en/vol1-fundamentals/ch08/02-single-inheritance.md](../../documents/en/vol1-fundamentals/ch08/02-single-inheritance.md)
- **不逐字直译**：按英文习惯重组句式，但**信息量守恒**——数字、规格、性能断言、适用条件一个不丢、一个不添。
- 中文的玩笑/自嘲/语气词：转成自然的英文轻松语气，不硬翻、不删减。
- 讲解节奏（先直觉后标准、先例子后定义）保持与中文源一致。

## 7. 禁止项

- ❌ 加「译注」/「译者按」/任何译者评论
- ❌ 修改代码逻辑、示例输出、性能数字
- ❌ 「顺手修复」中文源的**技术内容**——技术断言、数据、代码逻辑一律照译，疑点在 `notes` 报告由主控决定
- ✅ 但**明显笔误**（错别字、产品名拼写如 STM32CudeIDE、乱码/残句）按正确拼写与文意译出，并在 `notes` 记录「原文疑为 X」
- ❌ 删除或新增 frontmatter 字段（`translation:` 块除外）
- ❌ 触碰中文侧文件

## 8. index.md 导航页

标题、简介、列表项文字译英；链接 target 原样保留；frontmatter 同上述规则。

## 9. 交付前自检（翻译 agent 逐项过）

1. 输出路径是 en 镜像路径，文件名一字不差
2. frontmatter 字段齐全，`title`/`description` 已译英，数值字段与中文源一致
3. `source_hash` 重算命令输出与写入值一致
4. H2/H3 数量与中文源一致
5. 代码块数量与中文源一致，内容逐字符相同（注释除外）
6. 所有相对链接 target、图片 path 与中文源相同
