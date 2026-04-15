---
id: "080"
title: "重新设计 MkDocs 导航结构"
category: mkdocs
priority: P1
status: pending
created: 2026-04-15
assignee: charliechen
depends_on: ["003"]
blocks: ["084"]
estimated_effort: large
---

# 重新设计 MkDocs 导航结构

## 目标

全面重新设计 MkDocs 站点的导航结构，以支持数百页级别的大型文档站点。当前导航结构需要在以下方面进行优化：

1. **多标签导航**：使用 MkDocs Material 的 `tabs` 特性，将内容分为顶层标签页（Theory / Platforms / RTOS / Reference / General）
2. **侧边栏层级优化**：每个标签页内使用 `.pages` 文件精确控制章节排序和标题
3. **面包屑导航**：完善面包屑，让读者始终知道当前位置
4. **可扩展性**：导航结构应能方便地新增平台（如 ESP32、RP2040）而不破坏现有结构

## 验收标准

- [ ] `mkdocs.yml` 中 `nav` 配置已重新设计，使用多级结构
- [ ] 启用 `theme.features: navigation.tabs`，至少 5 个顶层标签页
- [ ] 每个标签页内的侧边栏层级不超过 3 级（标签 > 章节 > 子章节）
- [ ] `documents/` 下所有相关目录创建 `.pages` 文件，控制标题和排序
- [ ] `.pages` 文件使用 `nav` 指令自定义排序（非默认字母序）
- [ ] 面包屑导航启用 (`navigation.path`)
- [ ] 导航折叠行为合理 (`navigation.sections` 或 `navigation.expand`)
- [ ] 所有现有文章在新导航中都有正确位置
- [ ] 移动端导航体验良好（响应式测试通过）
- [ ] 导航加载性能不受影响（侧边栏不超过 50 个即时展开项）

## 实施说明

### 顶层标签页设计

```yaml
# mkdocs.yml
theme:
  name: material
  features:
    - navigation.tabs      # 顶层标签页
    - navigation.tabs.sticky  # 滚动时保持可见
    - navigation.sections   # 侧边栏章节折叠
    - navigation.path       # 面包屑导航
    - navigation.indexes    # 章节索引页
    - navigation.top        # 返回顶部按钮
    - navigation.tracking   # URL hash 追踪

nav:
  - 理论基础:
    - theory/index.md
    - C++ 基础:
      - theory/cpp-basics/index.md
      - theory/cpp-basics/modern-features.md
      - theory/cpp-basics/memory-management.md
    - 嵌入式 C++:
      - theory/embedded-cpp/index.md
      - theory/embedded-cpp/hardware-abstraction.md
      - theory/embedded-cpp/no-std-lib.md
  - 平台教程:
    - platforms/index.md
    - STM32F1:
      - platforms/stm32f1/index.md
      - platforms/stm32f1/gpio.md
      - platforms/stm32f1/uart.md
    - ESP32:
      - platforms/esp32/index.md
    - RP2040:
      - platforms/rp2040/index.md
  - RTOS:
    - rtos/index.md
    - FreeRTOS:
      - rtos/freertos/index.md
  - 参考资料:
    - reference/index.md
  - 其他:
    - general/index.md
```

### .pages 文件配置

每个目录下的 `.pages` 文件控制标题和排序：

```yaml
# documents/theory/.pages
title: 理论基础
nav:
  - cpp-basics
  - embedded-cpp
  - design-patterns
```

```yaml
# documents/platforms/stm32f1/.pages
title: STM32F1
nav:
  - index.md
  - gpio.md
  - uart.md
  - spi.md
  - i2c.md
  - timer.md
  - adc.md
  - dma.md
```

### 导航结构原则

1. **广度优先**：每个层级不超过 8 个项目，超过则考虑合并或分组
2. **深度限制**：最多 3 级可展开层级（标签 > 章节 > 子章节 > 文章）
3. **一致性**：每个平台教程的结构保持一致（overview → gpio → communication → timer → adc → dma）
4. **索引页**：每个章节目录都有 `index.md` 作为章节概述
5. **相关内容**：使用 `navigation.footer` 在文章底部提供"上一篇/下一篇"导航

### 迁移策略

1. 先在本地设计新的 `nav` 配置和 `.pages` 文件
2. 不移动文件（保持现有目录结构），仅调整导航配置
3. 如果需要重新组织目录，先创建映射表，确认无遗漏
4. 测试所有链接在新导航下是否正常工作
5. 部署后检查 Google Search Console 中的 404 错误

### 性能考虑

对于 200+ 页面的站点：
- 启用 `navigation.prune`（仅渲染当前章节的子页面）减少 HTML 体积
- 考虑将 Reference 部分使用独立的 section 或 API 文档方式
- 监控构建时间，若超过 60 秒需要优化

## 涉及文件

- `mkdocs.yml` — 主配置文件（nav 部分）
- `documents/**/.pages` — 各目录的页面配置文件
- `documents/**/index.md` — 各章节的索引页面（可能需要新建）

## 参考资料

- [MkDocs Material 导航定制](https://squidfunk.github.io/mkdocs-material/setup/setting-up-navigation/)
- [MkDocs awesome-pages 插件](https://lukasgeiter.github.io/mkdocs-awesome-pages-plugin/)
- [MkDocs nav 配置](https://www.mkdocs.org/user-guide/configuration/#nav)
