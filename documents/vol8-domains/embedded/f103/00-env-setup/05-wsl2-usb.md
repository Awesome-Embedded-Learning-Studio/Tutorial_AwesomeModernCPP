---
title: "WSL2 USB 透传(想用实际板子再看)"
description: "把 ST-Link 从 Windows 穿过虚拟化边界送进 WSL2:usbipd-win 的 bind 与 attach、设备权限的自动化,以及 OpenOCD 烧录实战"
chapter: 0
order: 5
tags:
  - stm32f1
  - beginner
  - 入门
  - 工具链
difficulty: beginner
platform: stm32f1
reading_time_minutes: 6
prerequisites:
  - "CMake 配置:从零构建 STM32 构建系统"
related:
  - "调试:从 printf 到 GDB"
---

# WSL2 USB 透传(想用实际板子再看)

这篇是选修课。模拟器主线的朋友可以整篇跳过,不影响任何后续内容;手头有 Blue Pill 和 ST-Link、想把固件烧进实际板子的朋友,这篇是您的路。原生 Linux 用户也请直接跳到文末的简明指南,WSL2 的弯路您不用走。

如果您决定走实际板子路线,又恰好在 WSL2 里开发,那笔者可以提前打个预防针:这是整个环境搭建路上最大的一坑。兴冲冲插上 ST-Link,`lsusb` 的输出里空空如也,别说 ST-Link,连个鼠标都看不到。问题不在您的操作,根源是 WSL2 架构的先天缺陷。

## 问题到底出在哪

WSL2 感觉上像 Windows 里的一个 Linux 程序,实际是一台完整的 Hyper-V 虚拟机:自己的内核、自己的内存管理、自己的设备树。USB 设备在 PC 架构里由主机控制器管理,咱们每插一个设备,操作系统就加载驱动接管它。而 WSL2 虚拟机里的 USB 控制器是虚拟的,连不到物理控制器,所以物理插入的设备对 WSL2 完全不可见——Windows 那边设备管理器里 ST-Link 好好的,Linux 这边一无所知。

解法是 usbipd-win,微软官方维护的工具,实现 USB/IP 协议,把 Windows 看到的 USB 设备"借"给 WSL2。它也是笔者试过一圈方案(虚拟机直通、放弃 WSL2 装双系统)之后,唯一坚持下来的。

## Windows 侧:usbipd-win

先确认您用的是 WSL2 而不是 WSL1(PowerShell 里 `wsl --list --verbose`,版本 1.x 需要升级)。然后开一个**管理员权限**的 PowerShell:

```powershell
winget install usbipd
```

装好后看看系统里有哪些 USB 设备:

```powershell
usbipd list
```

长长的列表里有您的鼠标键盘摄像头,每个设备带一个 BUSID(形如 "1-8")。找到 ST-Link(显示为 STMicroelectronics ST-LINK 之类),记住它的 BUSID。

接下来咱们先做 bind。**bind 是一次性的**:

```powershell
usbipd bind --busid 1-8
```

它告诉 Windows"这个设备以后允许被透传",执行后您会看到设备在 Windows 设备管理器里消失,重启后 bind 状态保留。**attach 则是每次都要重做的**:

```powershell
usbipd attach --wsl --busid 1-8
```

它把设备真正接到 WSL2 上,但重启电脑或重新插拔后失效,需要重跑。咱们可以把 attach 写进一个 PowerShell 函数,日常一键。

## Linux 侧:验证与权限

回到 WSL2 终端验证:

```bash
lsusb | grep -i stlink
```

看到类似输出就通了:

```text
Bus 001 Device 005: ID 0483:3748 STMicroelectronics ST-LINK/V2
```

`0483:3748` 是 ST-Link V2,`374b` 是 V2-1,OpenOCD 都支持。这行输出里的 `Bus 001 Device 005` 告诉咱们,设备节点在 `/dev/bus/usb/001/005`——马上要用到。

然后是权限,WSL2 在这里又坑了一道。原生 Linux 靠 udev 规则自动设权限,但 WSL2 启动时跳过 udev 服务,规则形同虚设。OpenOCD 第一次连设备,您十有八九会收获 `LIBUSB_ERROR_ACCESS`。解法简单粗暴:

```bash
sudo chmod 666 /dev/bus/usb/001/005
```

麻烦在于重新 attach 后设备号会变(005 变 006 之类),手动追着改太累。咱们写个小脚本自动找:

```bash
#!/bin/bash
# 自动修复 ST-Link 权限
BUSDEV=$(lsusb | grep -i stlink | awk '{print "/dev/bus/usb/"$2"/"substr($4,1,3)}')

if [ -z "$BUSDEV" ]; then
    echo "没有找到 ST-Link 设备,请先在 Windows 侧执行 usbipd attach"
    exit 1
fi

echo "找到 ST-Link 设备: $BUSDEV"
sudo chmod 666 $BUSDEV
echo "权限已设置为 666"
```

`substr($4,1,3)` 是因为 lsusb 输出的设备号带着冒号("005:"),只取前三个字符。仓库配套工程里的 `chmod_usb.sh` 就是它,您每次 attach 后跑一下,或者挂个 alias。

## OpenOCD 烧录

咱们要配的文件就两个:接口配置说"用什么调试器",目标配置说"烧什么芯片"。ST-Link V2 + F103C8T6 的组合是 `interface/stlink.cfg` + `target/stm32f1x.cfg`(OpenOCD 自动搜索 `/usr/share/openocd/scripts/`,不用写全路径)。手动烧录:

```bash
openocd -f interface/stlink.cfg -f target/stm32f1x.cfg \
        -c "program build/stm32_demo.bin verify reset exit 0x08000000"
```

咱们逐个看:`program` 烧录,`verify` 烧完校验,`reset` 复位芯片让它从头跑新程序,`exit` 做完就退出,结尾的 `0x08000000` 是 Flash 起始地址。需要先擦除的话(C8T6 的 Flash 总量是 **64KB**,十六进制 0x10000):

```bash
openocd -f interface/stlink.cfg -f target/stm32f1x.cfg \
        -c "flash erase_address 0x08000000 0x10000" \
        -c "program build/stm32_demo.bin verify reset exit 0x08000000"
```

日常咱们别手敲,上一篇配好的 CMake 目标一条命令完事:

```bash
cmake --build build --target flash
```

::: warning 网上抄来的命令先核对 Flash 容量
不少教程的擦除命令写着 `0x20000`(128KB)——那是 F103CB 的容量,咱们手上的 C8T6 只有 64KB。多擦不存在的区域,OpenOCD 轻则警告重则报错。
:::

## 排错

`LIBUSB_ERROR_ACCESS`:权限,重跑上面的脚本。

`Error: open failed`:您先跑 `lsusb | grep -i stlink` 确认设备透传了没;没有就回 Windows 侧重新 attach。

`Error: unable to find a matching device`:配置和硬件不匹配——调试器明明是 J-Link,咱们却用了 stlink.cfg;或者芯片是 F4,却用了 stm32f1x.cfg。

`lsusb` 输出为空:usbipd 或内核模块的问题,咱们用 `lsmod | grep usbip` 看模块加载没,必要时 `sudo modprobe vhci-hcd`。

## 原生 Ubuntu 用户的简明指南

原生 Linux 内核直接管理 USB,不需要 usbipd,您只要配好 udev 规则。创建 `/etc/udev/rules.d/49-stlinkv2.rules`:

```text
# STM32 ST-LINK/V2
SUBSYSTEM=="usb", ATTR{idVendor}=="0483", ATTR{idProduct}=="3748", MODE="0666", TAG+="uaccess"
# STM32 ST-LINK/V2-1
SUBSYSTEM=="usb", ATTR{idVendor}=="0483", ATTR{idProduct}=="374b", MODE="0666", TAG+="uaccess"
```

重载规则、拔插一次 ST-Link:

```bash
sudo udevadm control --reload-rules
sudo udevadm trigger
```

之后普通用户直接访问,没有 sudo 也没有权限脚本。对比咱们在 WSL2 里折腾的这一路,这是原生 Linux 实打实的省心之处。

到这,实际板子路线的烧录环节通了。下一篇咱们讲调试——那篇的开头是模拟器路线:GDB 不需要任何硬件,人人可练;实际板子的 OpenOCD 调试放在后半,和这篇正好衔接。
