# cppTemplateEscapePlugin 与 Vue 组件标签冲突修复

> 日期：2026-05-12
> 影响文件：`site/.vitepress/plugins/escape-cpp-templates.ts`

## 问题

在 Markdown 文件中使用 Vue 组件（如 `<ChapterNav>`、`<ChapterLink>`）时，`cppTemplateEscapePlugin` 会将其误判为 C++ 模板语法并转义为 `&lt;ChapterNav&gt;`，导致页面渲染为原始 HTML 文本。

### 根因

插件有两层防护：

1. **前处理**（`escapeCppTemplates`）：在 markdown-it 渲染前，将形如 `<xxx>` 的模式视为 C++ 模板并转义
2. **后处理**（`sanitizeHtml`）：渲染后再次清理残余的非标准 HTML 标签

两层均依赖 `HTML_TAGS` 白名单判断是否为合法标签。Vue 组件名（`ChapterNav`）不在白名单中，被误杀。

### 症状

- `<ChapterNav>` 被转义 → `&lt;ChapterNav&gt;`
- `</ChapterNav>` 未被转义（以 `/` 开头不匹配 C++ 模板正则）
- Vue 编译器遇到不匹配的闭合标签 → `Invalid end tag`

## 修复

### 前处理：`looksLikeCppTemplate`

```ts
// Vue 组件用 PascalCase（如 <ChapterNav>）；C++ 模板用小写（如 vector<int>）
if (/^[A-Z][a-zA-Z0-9]+$/.test(trimmed)) return false
```

### 后处理：`sanitizeHtml`

```ts
// 跳过大写开头的标签（Vue 组件），C++ 模板不会产生大写标签
if (tag[0] === tag[0].toUpperCase() && tag[0] !== tag[0].toLowerCase()) return match
```

## 判断依据

- C++ 模板参数在教程正文中为小写（`vector`、`shared_ptr`、`int`）
- Vue 组件遵循 PascalCase 命名（`ChapterNav`、`ChapterLink`）
- 代码块中的模板语法已由 `inFence` 检测跳过，不受影响