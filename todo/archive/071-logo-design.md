---
id: "071"
title: "设计专业 Logo 和统一视觉风格"
category: branding
priority: P2
status: done
created: 2026-04-15
assignee: charliechen
depends_on: []
blocks: []
estimated_effort: small
---

# 设计专业 Logo 和统一视觉风格

## 目标

为 Tutorial_AwesomeModernCPP 项目设计专业的视觉标识系统，包括：

1. **项目 Logo**：用于网站、README、社交媒体的核心标识
2. **Favicon**：网站标签页图标
3. **GitHub Social Preview**：仓库社交预览图（1280x640）
4. **配色方案**：与 MkDocs Material 主题协调的统一配色

视觉风格应传达以下品牌特质：现代、技术感、教育友好、嵌入式/硬件关联。

## 验收标准

- [ ] `documents/images/logo.png` — 项目主 Logo（至少 512x512，支持透明背景）
- [ ] `documents/images/favicon.ico` — 网站 favicon（包含 16x16、32x32、48x48 尺寸）
- [ ] `documents/images/social-preview.png` — GitHub social preview（1280x640）
- [ ] Logo 在深色和浅色背景下都有良好表现
- [ ] 配色方案文档已确定，包含主色、辅色、强调色及对应 HEX 值
- [ ] Logo 设计融合以下元素中至少两个：C++ 标识、芯片/电路、教科书/学习
- [ ] 配色与 MkDocs Material 主题（indigo 色调）协调
- [ ] Logo 可缩放（提供 SVG 源文件优先）
- [ ] `mkdocs.yml` 中引用 favicon 配置正确

## 实施说明

### 设计方向

**方案 A：技术教育融合**
- 主体：芯片轮廓内嵌入 C++ logo 或 `{ }` 代码符号
- 风格：线条简约、几何化
- 寓意：C++ 代码运行在芯片上

**方案 B：现代渐变**
- 主体：几何化的 CPU/芯片图形，搭配现代渐变色
- 风格：Material Design 风格，圆角
- 寓意：现代、友好

**方案 C：极简文字**
- 主体：项目缩写 "AMC" 或 "mCPP" 的文字 Logo
- 风格：等宽字体 + 技术装饰线
- 寓意：代码/编程

### 配色方案

推荐基于 MkDocs Material indigo 主题衍生：

| 角色 | 颜色 | HEX | 用途 |
|------|------|-----|------|
| 主色 | Indigo | `#4051B5` | 主标题、重要元素 |
| 辅色 | Teal | `#009688` | 代码块、技术元素 |
| 强调色 | Amber | `#FFC107` | 高亮、CTA 按钮 |
| 深色 | Dark Grey | `#212121` | 正文文本 |
| 浅色 | Light Grey | `#F5F5F5` | 背景 |

### 制作途径

优先级排序：
1. **AI 生成**：使用 DALL-E / Midjourney 生成初版，手动精修
2. **设计师合作**：在 Fiverr/淘宝找设计师，预算 200-500 元
3. **自行设计**：使用 Figma/Inkscape，基于开源图标素材

### 技术规格

- **Logo**: SVG 源文件 + PNG 导出（512x512, 256x256, 128x128）
- **Favicon**: ICO 格式（多尺寸打包）+ PNG 备份（32x32）
- **Social Preview**: PNG 1280x640，文件大小 < 1MB
- **背景**: Logo 提供 transparent 和 white 两个版本

### Favicon 配置

```yaml
# mkdocs.yml
theme:
  name: material
  favicon: images/favicon.ico
  logo: images/logo.png
```

## 涉及文件

- `documents/images/logo.png` — 主 Logo（及 SVG 源文件）
- `documents/images/favicon.ico` — Favicon
- `documents/images/social-preview.png` — GitHub 社交预览图
- `mkdocs.yml` — Favicon 和 Logo 配置引用

## 参考资料

- [MkDocs Material 主题定制](https://squidfunk.github.io/mkdocs-material/setup/changing-the-logo-and-icons/)
- [GitHub Social Preview 规格](https://docs.github.com/en/repositories/managing-your-repositorys-settings-and-features/customizing-your-repository/customizing-your-repositorys-social-media-preview)
- [Favicon 生成器](https://realfavicongenerator.net/)
