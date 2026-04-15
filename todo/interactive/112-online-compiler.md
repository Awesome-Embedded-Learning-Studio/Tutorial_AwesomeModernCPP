---
id: "112"
title: "在线代码运行和汇编查看（Compiler Explorer 集成）"
category: interactive
priority: P3
status: pending
created: 2026-04-15
assignee: charliechen
depends_on: ["081"]
blocks: []
estimated_effort: large
---

# 在线代码运行和汇编查看（Compiler Explorer 集成）

## 目标

将 Compiler Explorer (Godbolt) 的嵌入式功能集成到 MkDocs 站点中，让读者可以直接在文档页面中查看 C++ 代码的汇编输出。这对于嵌入式开发教程尤为重要——理解代码如何被编译为机器指令是优化和学习的关键。

特别关注：
- 支持 ARM Cortex-M 目标（arm-none-eabi-gcc/armclang）
- 延迟加载，不影响页面性能
- 交互式：读者可以修改代码并重新编译

## 验收标准

- [ ] 评估了 Compiler Explorer 嵌入的可行性和性能影响
- [ ] 至少 3 篇教程嵌入了 Compiler Explorer 组件
- [ ] 支持 ARM Cortex-M 目标的汇编输出查看
- [ ] 嵌入组件支持延迟加载（不在初始页面加载时请求 Godbolt）
- [ ] `documents/javascripts/compiler-explorer.js` 自定义 JS 已创建
- [ ] 嵌入组件在亮色/暗色模式下视觉协调
- [ ] 移动端显示合理（可水平滚动）
- [ ] 备选方案已实现：对于不支持 iframe 的场景，使用静态汇编代码块

## 实施说明

### Compiler Explorer 嵌入方案

**方案 A：iframe 嵌入（推荐）**

Godbolt 支持通过 URL 参数直接配置编译器和代码：

```html
<iframe width="100%" height="400"
  src="https://godbolt.org/e#z:CgzlncAFGgpmAOgCwCoBUCGAHGAChALLAApQDaFALpQDaFOFALpQC2AFgJS7sB2FFFEgOYAKpgDuADwAmAPjPUBhVAG4QBbXvQBbaMgAt8FCuQAcfBK0H898Oo6aAMroBRWgFda0AAkAhJ/gA%3D%3D%3D">
</iframe>
```

URL 参数编码：
- `z:` 后面是 Base64 编码的配置（编译器、代码、选项）
- 可指定 ARM 目标编译器（如 `arm-none-eabi-gcc 12.2.0`）

**方案 B：API 调用**

通过 Compiler Explorer REST API 在客户端编译代码：

```javascript
// documents/javascripts/compiler-explorer.js

async function compileCode(source, compilerId, options) {
    const response = await fetch('https://godbolt.org/api/compiler/' + compilerId + '/compile', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
            source: source,
            options: {
                userArguments: options,
                compilerOptions: { executorRequest: false },
                filters: { binary: false, commentOnly: true, demangle: true, labels: true }
            }
        })
    });
    return await response.json();
}
```

### 延迟加载实现

```javascript
// documents/javascripts/compiler-explorer.js

class CompilerExplorerEmbed {
    constructor(element) {
        this.element = element;
        this.loaded = false;
        this.observer = new IntersectionObserver((entries) => {
            entries.forEach(entry => {
                if (entry.isIntersecting && !this.loaded) {
                    this.load();
                    this.observer.disconnect();
                }
            });
        }, { rootMargin: '200px' });
        this.observer.observe(element);
    }
    
    load() {
        const url = this.element.dataset.ceUrl;
        const iframe = document.createElement('iframe');
        iframe.src = url;
        iframe.width = '100%';
        iframe.height = this.element.dataset.ceHeight || '400';
        iframe.frameBorder = '0';
        iframe.loading = 'lazy';
        iframe.allowFullscreen = true;
        
        // 显示加载指示器
        this.element.innerHTML = '<div class="ce-loading">加载 Compiler Explorer...</div>';
        iframe.onload = () => {
            this.element.innerHTML = '';
            this.element.appendChild(iframe);
        };
        iframe.src = url;
        this.loaded = true;
    }
}

// 初始化所有 CE 嵌入
document.addEventListener('DOMContentLoaded', () => {
    document.querySelectorAll('[data-compiler-explorer]').forEach(el => {
        new CompilerExplorerEmbed(el);
    });
});
```

### 使用方式

**Markdown 中的使用**（通过 HTML 块）：

```html
<div data-compiler-explorer
     data-ce-url="https://godbolt.org/e#..."
     data-ce-height="500">
    <noscript>
        <pre>
// 静态备选：汇编输出示例
gpio_toggle():
  ldr r0, [pc, #8]    @ 加载 GPIO 寄存器地址
  ldr r1, [r0]        @ 读取 ODR
  eor r1, r1, #0x1    @ 翻转 bit 0
  str r1, [r0]        @ 写回 ODR
  bx lr               @ 返回
        </pre>
    </noscript>
</div>
```

### ARM 目标编译器

Godbolt 支持的 ARM 嵌入式编译器：

| 编译器 | Godbolt ID | 说明 |
|--------|------------|------|
| arm-none-eabi-gcc 12.2 | armgcc1220 | GNU ARM 嵌入式 |
| arm-none-eabi-gcc 13.2 | armgcc1320 | GNU ARM 嵌入式 |
| armclang 6.18 | armclang6180 | ARM 官方编译器 |

编译选项：
```
-O2 -mcpu=cortex-m3 -mthumb -ffreestanding -nostdlib
```

### 样式定制

```css
/* documents/stylesheets/extra.css */

/* Compiler Explorer 容器 */
[data-compiler-explorer] {
    border: 1px solid var(--md-default-fg-color--lightest);
    border-radius: 6px;
    margin: 1.5em 0;
    overflow: hidden;
    background: var(--md-code-bg-color);
}

/* 加载指示器 */
.ce-loading {
    padding: 2em;
    text-align: center;
    color: var(--md-default-fg-color--lighter);
    font-style: italic;
}

/* 备选静态汇编 */
.ce-static-output {
    font-family: var(--md-code-font-family);
    font-size: 0.85em;
    padding: 1em;
    overflow-x: auto;
}
```

### 适用文章

最适合嵌入 Compiler Explorer 的教程：

1. **RAII 和零开销抽象**：展示抽象不产生运行时开销
2. **模板和编译期计算**：展示编译器优化结果
3. **内联函数和 constexpr**：对比内联/非内联的汇编差异
4. **位操作技巧**：展示位运算的高效汇编
5. **中断处理函数**：展示 ISR 的汇编上下文保存

### 性能注意事项

- 每个 CE iframe 约增加 500KB-1MB 页面重量
- 使用 Intersection Observer 延迟加载
- 单个页面不超过 2 个 CE 嵌入
- 考虑使用 `loading="lazy"` iframe 属性
- 提供静态汇编备选方案（noscript）

## 涉及文件

- `documents/javascripts/compiler-explorer.js` — CE 嵌入自定义 JS
- `documents/stylesheets/extra.css` — CE 嵌入样式
- 相关教程 `.md` 文件 — 添加 CE 嵌入

## 参考资料

- [Compiler Explorer 嵌入文档](https://github.com/compiler-explorer/compiler-explorer/blob/main/docs/Embedding.md)
- [Compiler Explorer API](https://github.com/compiler-explorer/compiler-explorer/blob/main/docs/API.md)
- [Godbolt 嵌入 URL 生成器](https://godbolt.org/clientstate.html)
- [Intersection Observer API](https://developer.mozilla.org/en-US/docs/Web/API/Intersection_Observer_API)
