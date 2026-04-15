---
id: "082"
title: "富媒体内容支持：图表、视频与交互组件"
category: mkdocs
priority: P2
status: pending
created: 2026-04-15
assignee: charliechen
depends_on: ["003"]
blocks: []
estimated_effort: medium
---

# 富媒体内容支持：图表、视频与交互组件

## 目标

为 MkDocs 站点添加丰富的富媒体内容支持，提升教程的可视化效果和交互体验：

1. **Mermaid 图表渲染优化**：优化已有 Mermaid 支持的样式和配置
2. **嵌入式视频**：支持 YouTube 和 Bilibili 视频嵌入
3. **PDF 嵌入**：支持在页面中嵌入 PDF 文件查看
4. **交互式组件**：折叠/展开代码块、可切换标签页
5. **自定义 Admonition 样式**：为不同类型提示框设计统一视觉风格

## 验收标准

- [ ] Mermaid 图表在亮色/暗色模式下都有良好的渲染效果
- [ ] 支持 Bilibili 视频嵌入（响应式 iframe），至少测试一个示例
- [ ] 支持 YouTube 视频嵌入（响应式 iframe），至少测试一个示例
- [ ] 支持通过 markdown 语法嵌入视频（自定义扩展或 shortcode）
- [ ] PDF 文件可通过链接或嵌入方式查看
- [ ] 代码块折叠/展开功能正常工作（使用 `pymdownx.details` 或 `tabbed` 扩展）
- [ ] 自定义 Admonition 类型已定义至少 5 种（tip/warning/danger/hardware/practice）
- [ ] `documents/stylesheets/extra.css` 中包含所有富媒体相关的样式定制
- [ ] `documents/javascripts/` 目录下包含必要的自定义 JS（如视频嵌入脚本）
- [ ] 所有富媒体内容在移动端正常显示

## 实施说明

### Mermaid 图表优化

```yaml
# mkdocs.yml
markdown_extensions:
  - pymdownx.superfences:
      custom_fences:
        - name: mermaid
          class: mermaid
          format: !!python/name:pymdownx.superfences.fence_code_format
```

样式优化：

```css
/* documents/stylesheets/extra.css */

/* Mermaid 图表响应式 */
.mermaid {
  text-align: center;
  overflow-x: auto;
  margin: 1.5em 0;
}

.mermaid svg {
  max-width: 100%;
  height: auto;
}

/* 暗色模式 Mermaid 配色 */
[data-md-color-scheme="slate"] .mermaid {
  --mermaid-font-family: var(--md-text-font);
}
```

MkDocs Material 内置 Mermaid 支持（9.x 版本），需要确认版本并在 `mkdocs.yml` 中启用：

```yaml
markdown_extensions:
  - pymdownx.superfences

extra_javascript:
  - https://unpkg.com/mermaid@10/dist/mermaid.min.js
```

### 视频嵌入方案

**YouTube 嵌入**：

```html
<div class="video-container">
  <iframe src="https://www.youtube.com/embed/VIDEO_ID"
          frameborder="0"
          allowfullscreen>
  </iframe>
</div>
```

**Bilibili 嵌入**：

```html
<div class="video-container">
  <iframe src="//player.bilibili.com/player.html?bvid=BV_ID&page=1"
          frameborder="0"
          allowfullscreen>
  </iframe>
</div>
```

**响应式样式**：

```css
.video-container {
  position: relative;
  padding-bottom: 56.25%; /* 16:9 比例 */
  height: 0;
  overflow: hidden;
  margin: 1.5em 0;
  border-radius: 8px;
}

.video-container iframe {
  position: absolute;
  top: 0;
  left: 0;
  width: 100%;
  height: 100%;
}
```

**Markdown 语法扩展**（可选自定义扩展）：

考虑创建简单的 JS 脚本，将特殊语法自动转换为 iframe：

```javascript
// documents/javascripts/video-embed.js
// 将 <!-- video: youtube VIDEO_ID --> 和 <!-- video: bilibili BV_ID -->
// 转换为响应式 iframe
document.addEventListener('DOMContentLoaded', function() {
  // 处理视频嵌入标记
});
```

### 交互式组件

**折叠代码块**：

```markdown
??? example "点击展开完整代码"
    ``` cpp
    // 完整代码
    ```
```

**标签页切换**：

```markdown
=== "STM32F1"

    ``` cpp
    // STM32F1 代码
    ```

=== "ESP32"

    ``` cpp
    // ESP32 代码
    ```

=== "RP2040"

    ``` cpp
    // RP2040 代码
    ```
```

### 自定义 Admonition 样式

```yaml
# mkdocs.yml
markdown_extensions:
  - admonition
  - pymdownx.details
```

```css
/* 自定义 admonition 类型 */

/* 硬件相关提示 */
.admonition.hardware {
  border-left-color: #FF6F00;
}
.admonition.hardware > .admonition-title {
  background-color: rgba(255, 111, 0, 0.1);
}
.admonition.hardware > .admonition-title::before {
  content: "🔌 ";
}

/* 实践练习 */
.admonition.practice {
  border-left-color: #00BFA5;
}
.admonition.practice > .admonition-title {
  background-color: rgba(0, 191, 165, 0.1);
}
.admonition.practice > .admonition-title::before {
  content: "🔧 ";
}

/* 安全警告 */
.admonition.danger {
  border-left-color: #D32F2F;
}
.admonition.danger > .admonition-title {
  background-color: rgba(211, 47, 47, 0.1);
}
```

使用方式：

```markdown
!!! hardware "硬件连接"
    将 PA0 连接到 LED 的正极，串联 330Ω 电阻。

!!! practice "动手练习"
    修改代码，让 LED 以 1Hz 频率闪烁。

!!! danger "安全警告"
    确保 3.3V 和 5V 引脚不要短接，否则可能损坏开发板。
```

### PDF 嵌入

使用 `<object>` 标签或链接方式：

```html
<object data="../datasheet.pdf" type="application/pdf" width="100%" height="600px">
  <p>无法显示 PDF，<a href="../datasheet.pdf">点击下载</a></p>
</object>
```

## 涉及文件

- `mkdocs.yml` — Mermaid、Admonition 等扩展配置
- `documents/stylesheets/extra.css` — 富媒体样式定制
- `documents/javascripts/` — 自定义 JavaScript 脚本（如视频嵌入）

## 参考资料

- [MkDocs Material Admonitions](https://squidfunk.github.io/mkdocs-material/reference/admonitions/)
- [MkDocs Material Content Tabs](https://squidfunk.github.io/mkdocs-material/reference/content-tabs/)
- [Mermaid 文档](https://mermaid.js.org/)
- [Bilibili 播放器嵌入文档](https://player.bilibili.com/)
