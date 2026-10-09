---
title: "跨平台"
sidebar_order: 50
description: "多路复用章的收拢处,兼全卷收卷章的住处:两侧五篇的实测汇进一张八维差异矩阵,AsyncBackend concept 把完成式的后端形状定下来,ch07 再把检测、分发、句柄承载这些更宽的差异收进同一套 C++ 接口"
platform: host
tags:
  - cpp-modern
  - host
  - advanced
  - 系统编程
---

# 跨平台

Linux 与 Windows 两侧各讲各的 API,讲到收尾的地方,有一层活是两侧都躲不开的:把差异收进一套 C++ 的抽象里。咱们在本目录收的就是这样的活。多路复用收尾的时候,咱们把 epoll、kqueue、IOCP 与 io_uring 四个后端放进同一张矩阵对表,再用 C++20 的 concepts 把一个异步后端的形状约束出来。全卷收卷的两篇也都在这里:检测、分发、句柄的承载、分派的开销收进同一套接口,全子卷一路沿用的公共工具收编成 syskit 的四个头。

<ChapterNav variant="sub">
  <ChapterLink num="3" href="03-cross-async-io" desc="四个平台后端的差异摆开之后,C++ 的 concepts 能不能在编译期把一个异步后端约束出来:八维差异矩阵(epoll、kqueue 文档列、IOCP、io_uring,数字全部引用两侧五篇的实测存档)、五行 AsyncBackend concept 定成完成式形状、EpollBackend 用 EPOLLONESHOT 垫一层的适配与 IocpBackend 的 ReadFile 直录、统一 request 的 offset 字段把普通文件那一维收进类型、同一份 drive_one_round 两侧一字不改各跑两轮真实读、缺 take 的负例被两侧 GCC 16 在实例化之前拦下(要害行两侧一致)、string_view 过 printf %s 段错误的返工记录、kqueue 整列 FreeBSD 手册页口径如实标注,收在三个来源拼一个 completion 与 ch07 的接手处">跨平台异步 I/O 抽象</ChapterLink>
  <ChapterLink num="4" href="04-platform-abstraction" desc="同一份代码要活在 Linux 与 Windows 两个平台,差异这层活怎么收才立得住:预定义宏答为哪个目标编译(__GLIBC__ 与 _UCRT 把同一家 GCC 的两个 C 库区分开)、__has_include 查头不查能力(liburing.h 在只说明用户态库装了)、CMake 的平台身份与编译能力在裸 CXX=g++.exe 时分了叉(CMAKE_SYSTEM_NAME 认成 Linux 而 try_compile 的能力探测反而全对),死分支里种三处错 Linux 编译零警告通过而 Windows 侧六条 error 全现形(被裁分支零诊断的直接证据),四行一条约束的 ByteSource concept 把 #ifdef 退到选后端的一处 using、缺 read_some 的负例被两侧 GCC 16 点名,intptr_t 统一承载 fd 与 HANDLE、_open_osfhandle 真桥配出 CRT 的 fd 读通 26 字节(收上一篇留的 uintptr_t 口),虚表对模板最好档 0.94ns 与普通调用打平(GCC 16 投机去虚化的汇编在档)、最坏档 5.23ns,对 136-157ns 的系统调用锚占 1%-4%,分派成本不该是选型主因">平台抽象层设计:从 #ifdef 到 concepts</ChapterLink>
  <ChapterLink num="5" href="05-syskit-engineering" desc="一章一章攒下来的公共工具怎么收进一个两侧都编译得起来的库:两套近亲 net::UniqueFd/SysError 对本卷 unique_fd/sys_call 同处一个翻译单元做逐项对照(骨架同构 4 字节 nothrow 移动,差异在 reset 带参、成员 swap 与 is_swappable 双真的探测陷阱、noexcept 标注、EINTR 归宿),错误模型 40 字节带上下文的 SysError 对 16 字节可判等的 error_code 各有胜场,统一方案落在 syskit:: 蛇形加 error_code 双出口,networking 侧影响面逐个核过(三份文档四处落点,代码侧 01-modern-socket 五个文件加 lab0 脚手架的第二份拷贝与测试五处引用,02 与 03 两篇不引用),e2 把四件工具收编成 INTERFACE 库(四个头逐个标注收编自哪篇、双平台同一份 CMakeLists 零平台分支、冒烟测试两侧各编各跑全过,Windows 侧错误值 2 与 3 的语义分叉如实入册),e3 拿 if、generator expression、toolchain 文件三写法六场景量平台分支的生效时机(裸 CXX 覆盖下 if(UNIX) 把 POSIX 源塞给 g++.exe 构建当场失败、genex 展开证据在 ninja -v 的命令行 -D 里、make 把 C: 当目标分隔符的增量重编失败与换 Ninja 的解法),收卷前补一条基准纪律(Windows 侧 getpid 的 1.3ns 是假基准,真基准要换 GetProcessHandleCount 的 157ns)">syskit 工具库工程化</ChapterLink>
</ChapterNav>

矩阵的数字与四组实验的原始输出,都在仓库 `code/volumn_codes/vol8/systems-programming/cross-platform/` 下面各篇自己的存档目录里(03-cross-async-io、04-abstraction-design 与 05-syskit),您想亲手对表、补第三个后端,或者复现检测的三条路与 CMake 的六场景,材料是齐的。
