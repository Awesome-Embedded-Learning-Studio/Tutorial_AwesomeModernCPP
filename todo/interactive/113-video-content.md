---
id: "113"
title: "视频和动图讲解内容"
category: interactive
priority: P3
status: pending
created: 2026-04-15
assignee: charliechen
depends_on: ["002"]
blocks: []
estimated_effort: epic
---

# 视频和动图讲解内容

## 目标

为关键教程制作简短视频讲解或动图演示（GIF/WebM），通过动态可视化帮助读者理解难以用文字表达的概念。视频内容重点覆盖：

1. **硬件连接演示**：开发板接线的实际操作过程
2. **示波器/逻辑分析仪波形展示**：通信协议的实际信号
3. **调试过程录屏**：使用 GDB/OpenOCD 调试的完整过程

## 验收标准

- [ ] 至少 5 个关键教程配备了视频或动图内容
- [ ] 视频以嵌入式方式在文档页面中展示（Bilibili/YouTube）
- [ ] 动图（GIF/WebM）文件大小合理（单个 < 5MB）
- [ ] `documents/images/videos/` 目录已创建，存放视频相关资源
- [ ] 所有视频/动图有文字替代说明（可访问性）
- [ ] 视频内容风格统一（片头/片尾/字幕格式）
- [ ] 视频分辨率不低于 1080p
- [ ] 动图使用 WebM 格式（优先于 GIF，更小的文件大小）

## 实施说明

### 内容优先级

按教学价值排序，优先制作以下内容：

**第一优先级（硬件可视化）**：
1. **GPIO LED 闪烁**：实际接线 + LED 闪烁效果（动图）
2. **按钮输入**：按钮按下/释放的硬件效果 + 代码响应（动图）
3. **SPI 通信波形**：逻辑分析仪捕获的 SPI 信号（动图/截图）
4. **I2C 通信波形**：逻辑分析仪捕获的 I2C 信号（动图/截图）
5. **UART 串口输出**：实际串口终端输出效果（动图）

**第二优先级（调试过程）**：
6. **GDB 调试入门**：完整的调试会话录屏（视频）
7. **RTT 日志输出**：Segger RTT 实时日志效果（动图）
8. **FreeRTOS 任务切换**：任务状态可视化的调试过程（视频）

**第三优先级（深入理解）**：
9. **DMA 传输对比**：CPU 传输 vs DMA 传输的效率对比（动图）
10. **中断优先级**：不同优先级中断的嵌套执行（动画）

### 视频规格

| 参数 | 要求 |
|------|------|
| 分辨率 | 1920x1080 (1080p) |
| 帧率 | 30fps |
| 格式 | MP4 (H.264) 上传到 Bilibili/YouTube |
| 字幕 | 中文字幕（SRT 格式） |
| 时长 | 短视频 2-5 分钟，录屏 5-15 分钟 |
| 片头 | 统一片头（项目 Logo + 标题，< 3 秒） |
| 片尾 | 项目链接 + "阅读更多"指引 |

### 动图规格

| 参数 | 要求 |
|------|------|
| 分辨率 | 800x600 或 1280x720 |
| 帧率 | 15-24fps |
| 格式 | WebM (VP9) 优先，GIF 备选 |
| 时长 | 3-15 秒 |
| 文件大小 | WebM < 2MB, GIF < 5MB |
| 循环 | 无限循环播放 |

### 录制工具

| 用途 | 工具 | 平台 |
|------|------|------|
| 屏幕录制 | OBS Studio | 全平台 |
| 终端录制 | asciinema / terminalizer | 全平台 |
| 动图录制 | Peek / LICEcap | Linux/macOS |
| 视频编辑 | DaVinci Resolve / Kdenlive | 全平台 |
| GIF 压缩 | gifsicle / ffmpeg | 全平台 |
| WebM 转换 | ffmpeg | 全平台 |

### 录制流程

```
1. 准备脚本（30-60 秒讲稿）
2. 准备硬件/软件环境
3. 试录 1-2 遍
4. 正式录制
5. 后期处理：
   - 剪辑（去除多余部分）
   - 添加片头/片尾
   - 添加字幕
   - 导出
6. 上传到 Bilibili/YouTube
7. 嵌入到文档中
```

### 文档中嵌入方式

**视频嵌入**：

```markdown
!!! info "视频讲解：GPIO LED 闪烁"
    <div class="video-container">
      <iframe src="//player.bilibili.com/player.html?bvid=BVXXXXXXXXX"
              frameborder="0" allowfullscreen>
      </iframe>
    </div>
    
    在这个视频中，我们演示了：
    - STM32 开发板的接线方式
    - LED 闪烁的实际效果
    - 使用示波器观察 GPIO 输出波形
```

**动图嵌入**：

```markdown
![SPI 通信时序 - 逻辑分析仪捕获](../images/videos/spi-logic-analyzer.webm)

*SPI 模式 0 通信时序：CPOL=0, CPHA=0。黄色为 SCK，蓝色为 MOSI，绿色为 CS。*
```

### 存储策略

- 视频文件上传到 Bilibili/YouTube（不存储在 Git 仓库中）
- 动图文件（WebM/GIF）存储在 `documents/images/videos/` 中
- 大文件（> 10MB）使用 Git LFS 追踪

```bash
# .gitattributes
documents/images/videos/*.webm filter=lfs diff=lfs merge=lfs -text
documents/images/videos/*.gif filter=lfs diff=lfs merge=lfs -text
```

### 统一片头模板

使用 DaVinci Resolve 或类似工具创建统一的片头模板：

- 背景色：项目主色 (#4051B5)
- Logo 动画：淡入 + 缩放
- 标题文字：教程标题
- 时长：< 3 秒

### 成本估算

| 项目 | 预计成本 |
|------|----------|
| 硬件（逻辑分析仪等） | 可能已有 |
| 录制工具（OBS等） | 免费 |
| 视频编辑（DaVinci Resolve） | 免费 |
| 存储（Git LFS） | GitHub 免费额度 |
| Bilibili/YouTube | 免费 |
| **总计** | 约 0 元（如果已有硬件） |

## 涉及文件

- `documents/images/videos/` — 动图和视频缩略图存放目录
- 相关教程 `.md` 文件 — 嵌入视频/动图
- `.gitattributes` — Git LFS 配置

## 参考资料

- [OBS Studio](https://obsproject.com/)
- [asciinema 终端录制](https://asciinema.org/)
- [Peek 动图录制](https://github.com/phw/peek)
- [ffmpeg WebM 转换](https://ffmpeg.org/ffmpeg-formats.html#webm)
- [Bilibili 创作中心](https://member.bilibili.com/)
