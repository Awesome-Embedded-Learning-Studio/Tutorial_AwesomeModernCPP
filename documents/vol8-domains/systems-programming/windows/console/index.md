---
title: "Windows 控制台"
sidebar_order: 50
description: "Windows 控制台的输入输出面:CONIN$/CONOUT$ 直开真句柄与模式位全图,120x9001 的字符网格,输入事件队列,以及 VT 序列开关前后的读回对照"
platform: host
tags:
  - cpp-modern
  - host
  - intermediate
  - 系统编程
  - Win32
---

# Windows 控制台

进程章的[控制台事件与 APC](../process/02-console-apc.md)管的是事件面,Ctrl+C 凭什么变成函数调用。这一章咱们管输入输出面:屏幕是一块带属性字节的字符网格,键盘这边进来的是一队事件记录,VT 序列是 2015 年才铺进 conhost 的第三条路。方法论继承 interop 篇的经验:stdio 挂管道的处境下,咱们用 CreateFile 直开 CONIN$/CONOUT$ 真句柄,验收咱们一律拿 ReadConsoleOutput 把字符与属性读回来对表,不靠人眼的参与。

<ChapterNav variant="sub">
  <ChapterLink num="1" href="01-console-api" desc="Windows 侧终端章唯一一篇:CONIN$/CONOUT$ 从管道手里拿回控制台(GetConsoleMode 对 stdio 直接 gle=6),模式位全图逐位翻译(CONIN$ 出厂 0x1f7、CONOUT$ 0x3、VT 位默认关、CP 双 936),屏幕是 120x9001 的 CHAR_INFO 网格对 120x30 可见窗,WriteConsoleOutput 整块直写不碰光标(60/60 读回)、WriteConsoleA 跟光标走、越右缘裁到 119、上滚填补,输入是 INPUT_RECORD 的队列(键一按一松两条六字段全录、点数/窥视/取走/清空四件工具、注入 Ctrl+C 只是键记录,WINDOW_BUFFER_SIZE_EVENT 本机触发不了系统侧路径如实入册:缓冲区尺寸连原样重设都报 87,改窗成功 srWindow 读回真变而队列 0 条),VT 序列开关前后读回对照(关时九个字面字符属性 07,开时 RED 属性 04,2J 只清屏不动光标,ANSI ED 语义对老 conhost 归零口径注时间),混用边界(VT 定位对直写坐标互不干扰、WRAP_AT_EOL 关掉反复覆盖行尾、DISABLE_NEWLINE_AUTO_RETURN 的两种 \n 解释),收尾与 Linux 侧 tty 行规程、conhost 宿主进程、VT 近亲的对照">Windows 控制台:字符网格、输入事件与 VT 序列</ChapterLink>
</ChapterNav>
