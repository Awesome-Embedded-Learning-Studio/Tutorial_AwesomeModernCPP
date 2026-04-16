---
title: "环境搭建（四）：WSL2 USB 透传，让 ST-Link 穿越虚拟化边界"
description: ""
tags:
  - beginner
  - cpp-modern
  - stm32f1
difficulty: beginner
platform: stm32f1
---
# 环境搭建（四）：WSL2 USB 透传，让 ST-Link 穿越虚拟化边界

## 前言：这是整个折腾路上最大的坑

如果你一路跟着前面的教程走过来，现在你的 WSL2 环境里已经有了 ARM 工具链，有了 OpenOCD，甚至可能已经编译出了你的第一个固件文件。当你兴冲冲地插上 ST-Link 调试器，准备把程序烧录到 STM32 里面时，现实会给你当头一棒——WSL2 根本看不到 USB 设备。

我现在正在经历这个阶段，lsusb 的输出里空空如也，不要说什么 ST-Link，连个鼠标都看不到。这不是你操作的问题，这是 WSL2 架构的先天缺陷。WSL2 用的是 Hyper-V 虚拟化技术，Linux 作为一个真正的虚拟机跑在 Windows 下面，但微软并没有把 USB 设备透传的功能做进去。你的 ST-Link 插在 Windows 的 USB 口上，被 Windows 驱动接管了，Linux 那边完全不知道它的存在。

这个问题困扰了我好几天。我在网上找各种资料，有人推荐用虚拟机方案，有人说直接放弃 WSL2 装原生 Ubuntu。但我不想放弃，因为 WSL2 的其他部分实在太方便了——和 Windows 文件系统集成、终端体验、包管理，这些都是原生 Linux 难以企及的。最终我找到了 usbipd-win 这个项目，它是微软官方维护的工具，专门用来解决 WSL2 的 USB 透传问题。

今天我们就把这个坑彻底填平，让 ST-Link 能够顺利地从 Windows 穿越到 WSL2，然后完成你的第一次 OpenOCD 烧录。

## WSL2 的 USB 问题，到底是怎么回事

让我们先理解清楚问题的根源。WSL2 虽然感觉上像是 Windows 里的一个 Linux 程序，但它实际上是一个完整的虚拟机。当你打开 WSL2 终端时，你是在和一个名为 "WSL" 的 Hyper-V 虚拟机交互。这个虚拟机有自己的内核、自己的内存管理、自己的设备树。

USB 设备在 PC 架构里是由主机控制器管理的，你的主板有好几个 USB 控制器，每个控制器下面挂着多个 USB 口。当一个 USB 设备插入时，控制器会给它分配一个地址，然后操作系统加载相应的驱动程序来和这个设备通信。问题在于 WSL2 的虚拟机里，USB 控制器是虚拟的，它连接不到物理的 USB 控制器，所以物理插入的设备对 WSL2 来说是不可见的。

Windows 主机能够看到你的 ST-Link，设备管理器里也正常识别了，但 WSL2 的 Linux 内核看不到。这就是为什么我们需要一个透传机制，把 Windows 看到的 USB 设备"借"给 WSL2 用。usbipd-win 就是做这个事情的，它实现了 USB/IP 协议，可以让 USB 设备通过网络协议栈从一台机器传输到另一台机器。在 WSL2 的场景下，就是从 Windows 传输到 WSL2 这个"虚拟机器"。

现在让我们开始配置。

## Windows 侧：安装和配置 usbipd-win

首先你要确保你用的是 WSL2 而不是 WSL1。WSL1 是个翻译层，它直接用 Windows 的内核，所以 USB 问题在 WSL1 里根本不存在——但 WSL1 也有很多其他限制，比如不支持 Docker，所以大部分人现在都用 WSL2。你可以在 PowerShell 里用 `wsl --list --verbose` 确认一下，如果你的版本是 1.x，那需要升级到 2。

接下来我们安装 usbipd-win。这个工具在微软的官方包管理器 winget 上有，安装非常简单。打开一个**管理员权限**的 PowerShell 终端，注意一定要管理员权限，因为 USB 设备的操作需要特权。执行：

```powershell
winget install usbipd
```

安装完成后，你应该可以用 `usbipd` 命令了。现在先查看一下系统中有哪些 USB 设备：

```powershell
usbipd list
```

这个命令会列出所有 USB 设备，你会看到长长的列表，包括你的鼠标、键盘、摄像头等等。每个设备都有一个 BUSID，格式是类似 "1-5" 或 "2-3" 这样的。你的 ST-Link 应该也在列表里，可能显示为 "STMicroelectronics ST-LINK..." 或者类似的名称。记住它的 BUSID，比如我的显示为 "1-8"。

接下来你需要把这个设备绑定到 usbipd-win。绑定是一个只需要做一次的操作，它告诉 Windows 这个设备以后可以被透传。绑定之后，设备在 Windows 设备管理器里会消失，它的驱动会被卸载，转而由 usbipd-win 接管。执行绑定命令：

```powershell
usbipd bind --busid 1-8
```

把 `1-8` 替换成你实际看到的 BUSID。如果成功，你会看到确认信息。现在设备已经从 Windows 的视野里消失了，你可以在设备管理器里确认一下，ST-Link 那一项应该已经不见了。

但这时候 WSL2 还看不到设备，因为绑定只是准备工作，你还需要把设备"附接"到 WSL2。这个 attach 操作是每次重启 WSL2 或者重新插拔设备后都需要做的。让我们执行：

```powershell
usbipd attach --wsl --busid 1-8
```

这个命令会把设备通过 USB/IP 协议传输到 WSL2。`--wsl` 参数指定目标是我们默认的 WSL 发行版。现在设备应该已经出现在 WSL2 里了。

bind 和 attach 的区别很重要，bind 是一次性操作，告诉 Windows "这个设备以后可以被透传"，而 attach 是每次都要做的，相当于"我现在把这个设备连接到 WSL2"。你重启电脑后 bind 状态会保留，但 attach 会丢失，需要重新执行。

## Linux 侧：验证设备透传

现在回到你的 WSL2 终端。你可以用 `lsusb` 命令查看 USB 设备列表：

```bash
lsusb | grep -i stlink
```

如果一切顺利，你应该看到类似这样的输出：

```text
Bus 001 Device 005: ID 0483:3748 STMicroelectronics ST-LINK/V2
```

或者可能是 `0483:374b`，这取决于你的 ST-Link 版本。V2 版本是 3748，V2-1 是 374b，但这对 OpenOCD 来说区别不大，它两个都支持。

设备号信息在这行输出里很重要：`Bus 001 Device 005` 意味着这个设备在 `/dev/bus/usb/001/005`。这个设备节点文件是我们后续要用来访问 ST-Link 的接口。

现在我们让 WSL2 能够访问这个设备。在原生 Linux 系统里，你通常会配置 udev 规则，让系统自动给 USB 设备设置正确的权限。但在 WSL2 里，udev 默认是不工作的——WSL2 启动时会跳过 udev 的服务启动，这导致 udev 规则根本不会生效。这是 WSL2 的另一个坑。

你可以尝试创建 udev 规则文件 `/etc/udev/rules.d/49-stlinkv2.rules`，内容是：

```text
# STM32 ST-LINK/V2
SUBSYSTEM=="usb", ATTR{idVendor}=="0483", ATTR{idProduct}=="3748", MODE="0666"
# STM32 ST-LINK/V2-1
SUBSYSTEM=="usb", ATTR{idVendor}=="0483", ATTR{idProduct}=="374b", MODE="0666"
```

然后在原生 Ubuntu 上你需要 `sudo udevadm control --reload-rules && sudo udevadm trigger` 来重新加载规则。但在 WSL2 里，这些命令可能不会有任何效果，因为 udev 服务根本没在跑。

所以我们需要另一个办法：手动修改设备权限。

## WSL 下的权限处理：那个令人崩溃的 LIBUSB_ERROR_ACCESS

当你第一次尝试用 OpenOCD 连接 ST-Link 时，你很可能会遇到 `LIBUSB_ERROR_ACCESS` 错误。这个错误的意思很明确：OpenOCD 没有权限访问 `/dev/bus/usb/001/005` 这个设备文件。

解决方法很简单粗暴，用 sudo 修改权限：

```bash
sudo chmod 666 /dev/bus/usb/001/005
```

但问题在于每次你重新 attach USB 设备后，设备号可能会变。有时候 ST-Link 是 Device 005，下次重启 WSL2 后可能变成 Device 006。所以手动输命令很麻烦，我们需要一个自动化脚本。

我写了一个简单的 `fix_stlink.sh` 脚本，它会自动找到 ST-Link 的设备节点并修改权限：

```bash
#!/bin/bash
# 自动修复 ST-Link 权限的脚本

# 用 lsusb 找到 ST-Link 设备，提取总线号和设备号，我这边是类似ST-Link，建议你自己lsusb先看看再修一下这个脚本
BUSDEV=$(lsusb | grep -i stlink | awk '{print "/dev/bus/usb/"$2"/"substr($4,1,3)}')

if [ -z "$BUSDEV" ]; then
    echo "没有找到 ST-Link 设备，请先在 Windows 侧执行 usbipd attach"
    exit 1
fi

echo "找到 ST-Link 设备: $BUSDEV"
sudo chmod 666 $BUSDEV
echo "权限已设置为 666"
```

这个脚本的工作原理是：用 `lsusb | grep -i stlink` 找到 ST-Link 那一行，然后用 awk 提取总线号（第二列）和设备号（第四列的前三个字符）。`substr($4,1,3)` 这个技巧是因为 lsusb 输出的设备号后面带个冒号，比如 "005:"，我们只取前三个字符。

你可以把这个脚本放在 `~/bin/` 目录下，添加执行权限 `chmod +x ~/bin/fix_stlink.sh`，然后每次重新 attach USB 设备后运行一下。或者你可以把它加到你的 `.bashrc` 或 `.zshrc` 里的某个 alias，比如 `alias fix-stlink='~/bin/fix_stlink.sh'`，这样以后只需要输 `fix-stlink` 就行了。

## OpenOCD 烧录实战：见证奇迹的时刻

现在设备透传了，权限也设置了，我们可以开始真正烧录固件了。OpenOCD 的配置文件系统非常灵活，你需要指定两个配置文件：一个是接口配置（interface），描述你用的什么调试器；另一个是目标配置（target），描述你要烧录什么芯片。

对于 ST-Link V2 和 STM32F103C8T6，配置文件分别是：

- `interface/stlink.cfg` — ST-Link 调试器接口
- `target/stm32f1x.cfg` — STM32F1 系列芯片

OpenOCD 会自动搜索它的配置文件目录，通常在 `/usr/share/openocd/scripts/` 下，所以你不需要写完整路径。

最基本的手动烧录命令是这样：

```bash
openocd -f interface/stlink.cfg -f target/stm32f1x.cfg \
        -c "program firmware.bin verify reset exit 0x08000000"
```

让我解释一下这个命令的各个部分。`-f` 参数指定配置文件，这里我们指定了两个。`-c` 参数是直接在命令行执行 OpenOCD 的命令，而不是用配置文件里的。

`program firmware.bin` 告诉 OpenOCD 烧录名为 `firmware.bin` 的二进制文件。`verify` 表示烧录后自动校验，确保数据写入正确。`reset` 会在烧录完成后复位芯片，让它从头开始执行新程序。`exit` 告诉 OpenOCD 做完这些就退出，而不是继续监听 GDB 连接。最后的 `0x08000000` 是 STM32F103 的 Flash 起始地址，这是 ARM Cortex-M 系列的标准地址。

如果你需要完全擦除芯片后再烧录（比如你之前烧过大程序，现在要烧个小程序，不擦除的话可能有残留数据），可以加个 `erase` 命令：

```bash
openocd -f interface/stlink.cfg -f target/stm32f1x.cfg \
        -c "flash erase_address 0x08000000 0x20000" \
        -c "program firmware.bin verify reset exit 0x08000000"
```

`flash erase_address 0x08000000 0x20000` 会擦除从 0x08000000 开始的 128KB Flash（STM32F103C8T6 的总容量）。`0x20000` 是十六进制，换算成十进制正好是 131072 字节 = 128KB。

在实际项目里，你不会每次都手动打这么长的命令。用 CMake 的 flash 目标会更方便：

```bash
cmake --build build --target flash
```

这会在 `build/` 目录下找到生成的固件文件，自动调用 OpenOCD 烧录。前提是你之前在 CMakeLists.txt 里配置好了 flash 目标，具体可以参考前面的教程。

## 常见错误排查：当烧录失败时

在这个过程中你可能会遇到各种错误，让我总结一下最常见的几种和对应的解决方案。

`LIBUSB_ERROR_ACCESS` 是最常见的一个，表示 OpenOCD 没有权限访问 USB 设备。解决方法就是重新跑一下 `fix_stlink.sh` 脚本，或者手动 `sudo chmod 666` 那个设备节点。如果你重新 attach 了 USB 设备，设备号可能变了，所以需要重新设置权限。

`Error: open failed` 这个错误比较笼统，通常意味着 OpenOCD 根本找不到 USB 设备。这时候第一步是确认设备是否成功透传到 WSL2，用 `lsusb | grep -i stlink` 检查一下。如果看不到设备，回到 Windows 侧重新执行 `usbipd attach --wsl --busid X-X`。如果设备在那里但 OpenOCD 还是报错，可能是权限问题，继续按 LIBUSB_ERROR_ACCESS 的流程排查。

`Error: unable to find a matching device` 通常意味着 OpenOCD 的配置文件和实际硬件不匹配。比如你用的实际上是 STM32F4 系列芯片，但配置文件写的是 `stm32f1x.cfg`，或者你用的是 J-Link 调试器但配置文件写的是 `stlink.cfg`。检查一下你的硬件型号和配置文件是否对应。

还有一种情况是 WSL2 完全看不到任何 USB 设备，`lsusb` 的输出为空。这时候可能是 usbipd-win 没有正确工作，或者 WSL2 的内核模块没有加载。你可以用 `lsmod | grep usbip` 在 WSL2 里检查 USB/IP 相关模块是否加载。如果没有加载，可以尝试 `sudo modprobe vhci-hcd`，但通常 WSL2 的内核配置应该已经包含了这些模块。

## 原生 Ubuntu 用户的简明指南

如果你用的是原生 Ubuntu Linux（不是 WSL2），恭喜你，事情要简单得多。你不需要 usbipd-win，因为你的 Linux 内核可以直接访问 USB 设备。你只需要配置 udev 规则，让系统自动给 ST-Link 设置正确的权限。

创建 `/etc/udev/rules.d/49-stlinkv2.rules` 文件，内容是：

```text
# STM32 ST-LINK/V2
SUBSYSTEM=="usb", ATTR{idVendor}=="0483", ATTR{idProduct}=="3748", MODE="0666", TAG+="uaccess"
# STM32 ST-LINK/V2-1
SUBSYSTEM=="usb", ATTR{idVendor}=="0483", ATTR{idProduct}=="374b", MODE="0666", TAG+="uaccess"
```

然后重新加载 udev 规则：

```bash
sudo udevadm control --reload-rules
sudo udevadm trigger
```

拔插一下 ST-Link，udev 会自动应用新规则。之后你的普通用户账号就能直接访问设备了，不需要 sudo 也不需要每次手动修改权限。原生 Linux 的 udev 系统工作得很完善，这是它相比 WSL2 的一个优势。

## 结语：跨平台的代价

折腾完 WSL2 的 USB 透传，你现在应该能够在 WSL2 环境里完成完整的 STM32 开发流程了：编辑代码、编译固件、烧录芯片，一切都在一个统一的环境里进行。虽然 usbipd-win 的 attach 操作有点繁琐，但把它写进一个小脚本或者 PowerShell 函数后，日常使用也还算方便。

WSL2 这个方案本质上是个折中——它让你在 Windows 上获得接近原生 Linux 的开发体验，但代价是需要在某些地方绕些弯路。USB 透传只是其中之一，后面你可能还会遇到串口设备透传、网络配置等问题。但好消息是，这些坑都有解决方案，而且一旦配置好了，后续的使用就顺畅了。

下一篇文章我们会进入真正的嵌入式开发：从点灯开始，一步步探索 STM32 的外设编程。你会看到现代 C++ 如何让嵌入式代码变得更简洁、更安全。现在先把你的开发环境彻底打通，烧录工具链练熟，我们很快就可以开始写真正的代码了。
