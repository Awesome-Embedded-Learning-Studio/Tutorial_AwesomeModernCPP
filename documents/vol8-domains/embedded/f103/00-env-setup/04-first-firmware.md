---
title: "第一个自己的固件：往库里加 target"
description: "看了三篇理论，这一篇全程动手：照抄 01_blinky 起自己的 00_my_blinky，注册进构建系统，亲历 sim target 撞名和漏注册两颗雷的真报错，再踩第三颗不响的——半改名让全绿的仿真跑起别人的固件；CMakeLists 逐段解剖，最后改一行 HAL_Delay 让效果在采样判据下可见——外加一个 200 比 500 还省 4 字节的指令编码彩蛋"
chapter: 0
order: 4
tags:
  - stm32f1
  - beginner
  - 嵌入式
  - CMake
difficulty: beginner
platform: stm32f1
reading_time_minutes: 11
related:
  - "为什么是 C++,凭什么?"
  - "Renode 观测课:没有板子,谁说了算"
---

# "看了三篇了，一行都没让我写呢"

那很抱歉了，我相信您抱着手机，或者是您就打开虚拟机或者是WSL的时候，已经蓄势待发。这一篇需要您来动手！

> 从这一篇开始，请您放下您的手机，还请到达您的电脑前，打开WSL，或者任何您喜欢的发行版，我们准备深呼吸，干活！

## 工程嘛，抄起手

感谢我的工作的导师，当时我在优化项目的CSS Parser的时候，就让我抄Google的Blink CSS。“抄不丢人，抄都抄不好，说不过去了”。不是所有的东西都要零帧起手。但是动手，还是有抓手才好动。我们的想法是，没必要完全从0写。您可以先尝试改动并且理解代码~

咱们把 `01_blinky` 整个拷一份，起名 `00_my_blinky`——排最前面，`ls` 的时候一眼就是您自己的：

```bash
cd libestdx
cp -r examples/01_blinky examples/00_my_blinky
```

进去把三处名字改掉：`CMakeLists.txt` 和 `renode.resc` 里的 `01_blinky`/`blinky` 全换成 `00_my_blinky`/`my_blinky`，`main.cpp` 开头的注释改成您自己的话。然后是关键的一步，让构建系统知道它存在。打开 `examples/CMakeLists.txt`，现在的内容长这样：

```cmake
add_subdirectory(01_blinky)
add_subdirectory(02_gpio)
add_subdirectory(03_led)
add_subdirectory(04_button)
```

在第一行前面加上您的：

```cmake
add_subdirectory(00_my_blinky)
```

趁手热，咱们把这个目录结构看明白。这套工程是三层，最上面的是**顶层编排**（根 `CMakeLists.txt`，定标准、挂子目录）、**家族包**（`include/libestdx/boards/stm32f1/`，把官方 HAL 编成 `hal` 静态库——"轮子只讲不造"的那部分住这）、**固件**（`examples/` 下每个目录一个可执行目标，消费 hal）。您刚才这一拷一加，就是往第三层里添了一位新住户。

## 哈哈哈哈被骗了兄弟

好了请你跑一下。。。

```bash
cmake -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/arch/stm32f103c8t6.cmake
```

啪！是CMake说不可以！笔者把真文贴给您：

```text
CMake Error at examples/01_blinky/CMakeLists.txt:23 (add_custom_target):
  add_custom_target cannot create target "sim" because another target with
  the same name already exists.  The existing target is a custom target
  created in source directory
  ".../libestdx/examples/00_my_blinky".
```

遇到报错不要紧，重要的是读懂它，他说——`cannot create target "sim" because another target with the same name already exists`，仔细读一下，不仔细的话就去问豆包大人，deepseek大人，还是别的大人，都行。我建议你自己读一下。

在我们的`01_blinky` 的构建脚本里有个叫 `sim` 的自定义 target（就是上一篇那条 `--target sim` 一条龙），您照抄出来的 `00_my_blinky` 里也有一个 `sim`——而 **CMake 的 target 名是整个工程全局唯一的**，撞了就配置失败。解法非常滴直接：把您那份里的三个 target 改名，`sim`→`my_sim`、`flash`→`my_flash`、`erase`→`my_erase`（**请记得顺手把 `DEPENDS` 那行也对上，要不然会出现诡异的问题，当然，使用CMake变量语法解决，也是一种更妙的方式，这里就不偏题讲CMake了**）。

## 跑通它

重新配置、构建：

```bash
cmake -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/arch/stm32f103c8t6.cmake
cmake --build build --target my_blinky
```

构建尾部的真输出：

```text
[13/13] Linking CXX executable examples/00_my_blinky/my_blinky; objcopy -> bin, report size
   text    data     bss     dec     hex filename
   5500      12       4    5516    158c .../my_blinky
```

呱！他没哭，就是吐的是自己的体重，虽然如果是真的婴儿那就得吓死了。咱们的话是高兴一下——咱们的第一个固件出生了，5500 字节。跟 `01_blinky` 一模一样的体重，因为 `main.cpp` 还没动过，它现在就是 blinky 的双胞胎。打开 `00_my_blinky/renode.resc`，在文件末尾加上一个采样宏：

```text
macro sample
"""
    pause
    emulation RunFor "0.25"
    sysbus ReadDoubleWord 0x4001100C
    (这两句再重复七次)
    start
"""
```

咱们注意宏里两处细节：`pause` 打头是必须的——机器正在跑的时候 `RunFor` 会拒绝执行；结尾的 `start` 让机器采完样继续跑。宏是随脚本一起加载的，所以加了它要重新起模拟器：

```bash
cmake --build build --target my_sim
```

机器停在 monitor 提示符后，咱们一句命令换八个采样点：

```text
(machine-0) runMacro $sample
```

然后我们就有了下面的数字一字排开：

```text
0x00002000  0x00002000  0x00000000  0x00000000
0x00002000  0x00002000  0x00000000  0x00000000
```

`0x2000` 和 `0x0000` 各占四个、成对交替（半周期 500ms），行为确认，它真的在闪。判据的完整原理（为什么 250ms、混叠怎么坑人）上一篇已经讲清楚了；从这一篇起，它就是您 resc 里随叫随到的一个宏。LLM在我让他干杂活的时候，他骄傲的说：**自己的固件，自己配剧本**。我看这个意思不错，留这里了。

要是忘了往 `examples/CMakeLists.txt` 里加那行 `add_subdirectory`呢？构建直接这么报：

```text
ninja: error: unknown target 'my_blinky', did you mean 'blinky'?
```

ninja 还挺客气，会给您猜一个。看到 `unknown target`，先检查注册，再检查拼写。

## 您的固件里都写了什么

趁热打铁，咱们把 `00_my_blinky/CMakeLists.txt` 逐段读一遍——它就是"一个固件"的完整定义：

```cmake
add_executable(my_blinky
    main.cpp
    stm32f1xx_it.c # 中断服务程序:哪些 handler 存在由这份固件决定
    syscalls.c     # newlib 桩
)
```

咱们一段一段看。第一段声明可执行目标，注意那两个 C 文件：`stm32f1xx_it.c` 和 `syscalls.c` **住在固件这边而不是库里**。中断向量表和 newlib 对这些符号的引用在链接后期才出现，放库里会被"没人引用就不拉"的规则丢掉，谁拥有谁负责。接着链接：

```cmake
target_link_libraries(my_blinky PRIVATE hal)
target_link_options(my_blinky PRIVATE
    -T${CMAKE_SOURCE_DIR}/cmake/arch/stm32f103c8t6.ld
    -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/my_blinky.map
)
```

`hal` 就是家族包编出来的静态库；链接脚本 `-T` 指定内存布局（64K Flash / 20K SRAM，代码住哪、数据住哪全由它说了算）；`-Map` 让链接器多吐一份地图文件，哪个函数占多少字节都记在里面——这份 `.map` 咱们在后面"二进制分析"的主题里是大主角。最后是收尾加工：

```cmake
add_custom_command(TARGET my_blinky POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:my_blinky> my_blinky.bin
    COMMAND ${CMAKE_SIZE} --format=berkeley $<TARGET_FILE:my_blinky>
    COMMENT "objcopy -> bin, report size"
)
```

咱们接着看收尾这段：链接出来的 ELF 不能直接烧，`objcopy` 剥成裸二进制 `my_blinky.bin`，`size` 打印体积，就是您刚才看到的那张三列表。再往下 `my_sim` 那段您已经在撞名雷里认识它了：起一个无头 Renode 加载这份 resc，`WORKING_DIRECTORY` 固定在仓库根（为什么，02 篇的 `@` 路径讲过）。

## 骗你的，根本没跑你那个（这也算是抄工程的一个小惩罚

前面的问题好歹都是大喇叭——CMake 配置直接红给您看，ninja 还客客气气帮您猜。真正的坑在后面：下面这个是一个字都不报啊，全程绿，跑的还是别人的固件。

场景是这样：改名的时候手抖，`add_executable(my_blinky` 改了，但 POST_BUILD 里那两个 `$<TARGET_FILE:blinky>`、`my_sim` 的 `DEPENDS blinky` 漏了，还是旧名。您猜 CMake 报什么？什么都不报。咱们前面说过 target 名全局唯一，撞名会配置失败

这个机制反过来也会送你个大的：`blinky` 这个名字在 `01_blinky` 那里**找得到**，于是您固件里漏改的引用静默解析到别人的 target 上。您的 `objcopy` 拷出来的是 01 的 ELF，您的 `my_flash` 烧进板子的是 01 的固件，一路上没有任何人喊一声。

还没完。`renode.resc` 里的 `$bin` 是手写的文件路径，target 改名之后这条路就悬空了——而 ninja 从不清理孤儿产物，改名之前编出来的旧 ELF 还好端端躺在 `build/` 里，LoadELF 每次都成功。好在日志说——

```text
[4/5] Linking CXX executable examples/00_my_blinky/blinky; objcopy -> bin, report size
   4716      12       4    4732    127c .../build/examples/00_my_blinky/blinky
[4/5] cd ... && renode --console --disable-xwt -e include @.../examples/01_blinky/renode.resc
...
(monitor) include @.../examples/01_blinky/renode.resc
11:38:54.6108 [INFO] sysbus: Loaded SVD: ... Name: STM32F103. Description: STM32F103.
11:38:54.6335 [INFO] sysbus: Loading block of 4716 bytes length at 0x8000000.
```

笔者：欸我超！不对不对！所以的话，麻烦各位一定看日志，绿了，现象对了，也不是OK。

## 改一行，让判据看见它

双胞胎不算您的固件，改点东西。咱们把 `main.cpp` 里两个 `HAL_Delay(500)` 都改成 `HAL_Delay(200)`——半周期从 500 毫秒变 200 毫秒。重新构建、Renode 采样，还是 250ms 间隔，真输出：

```text
0x00000000  0x00002000  0x00000000  0x00000000
0x00002000  0x00000000  0x00002000  0x00000000
```

对比一下改之前：500ms 版本是成对出现（两个采样落进同一个半周期），200ms 版本不再成对（半周期比采样间隔还短，读数几乎每次都在跳）。**您改了一行代码，观测模式整个变了形**——这就是上一课判据的意义：固件的任何行为变化，在采样序列上都留痕。

还有个礼物送给爱抠细节的您：改完之后 size 从 5500 掉到了 5496，省了 4 字节。为什么？`200` 摆得进 16 位 `movs` 指令的立即数（0 到 255），`500` 摆不进，只能用 32 位的 `mov.w`——两处延时各长出 2 字节，就是这 4 字节的差距。一行代码的改动，反汇编里明码标价。
