---
chapter: 14
difficulty: beginner
order: 4
platform: stm32f1
reading_time_minutes: 14
tags:
- beginner
- cpp-modern
- stm32f1
title: 'Environment Setup (Part 4): WSL2 USB Passthrough, Bridging ST-Link Across
  the Virtualization Boundary'
description: ''
translation:
  source: documents/vol8-domains/embedded/00-env-setup/04-wsl2-usb.md
  source_hash: 6069d87f3edf059f964d09abfed044a6acdcf7c79ac718ce4f774f31bd5958c3
  translated_at: '2026-06-16T04:08:53.126993+00:00'
  engine: anthropic
  token_count: 2021
---
# Environment Setup (Part 4): WSL2 USB Passthrough, Making ST-Link Cross the Virtualization Boundary

## Preface: The Biggest Hurdle in This Journey

If you have followed the previous tutorials, your WSL2 environment should now have the ARM toolchain and OpenOCD installed, and you might have even compiled your first firmware file. When you eagerly plug in the ST-Link debug probe, ready to flash the program into the STM32, reality will hit you hard—WSL2 simply cannot see the USB device.

I am currently going through this stage myself; the output of `lsusb` is completely empty. Not to mention the ST-Link, I can't even see a mouse. This isn't an error on your part; it's an inherent architectural limitation of WSL2. WSL2 uses Hyper-V virtualization technology, where Linux runs as a true virtual machine underneath Windows. However, Microsoft did not implement USB device passthrough functionality. Your ST-Link is plugged into a Windows USB port and is managed by Windows drivers, while the Linux side is completely unaware of its existence.

This problem plagued me for several days. I searched for various solutions online; some recommended using a virtual machine, while others suggested giving up on WSL2 entirely and installing native Ubuntu. But I didn't want to give up, because the other parts of WSL2 are simply too convenient—the file system integration with Windows, the terminal experience, and package management are all hard to match with native Linux. Eventually, I found the `usbipd-win` project, a tool officially maintained by Microsoft specifically designed to solve WSL2 USB passthrough issues.

Today, we will fill this pit once and for all, allowing the ST-Link to successfully traverse from Windows to WSL2, and then complete your first OpenOCD flash.

## The WSL2 USB Problem: What Exactly is Happening?

Let's first understand the root of the problem clearly. Although WSL2 feels like a Linux program inside Windows, it is actually a complete virtual machine. When you open a WSL2 terminal, you are interacting with a Hyper-V virtual machine named "WSL". This virtual machine has its own kernel, memory management, and device tree.

In PC architecture, USB devices are managed by host controllers. Your motherboard has several USB controllers, and each controller has multiple USB ports attached. When a USB device is inserted, the controller assigns it an address, and the operating system loads the corresponding driver to communicate with the device. The problem is that inside the WSL2 virtual machine, the USB controllers are virtual; they cannot connect to physical USB controllers, so physically inserted devices are invisible to WSL2.

The Windows host can see your ST-Link, and Device Manager recognizes it normally, but the WSL2 Linux kernel cannot see it. This is why we need a passthrough mechanism to "lend" the USB device seen by Windows to WSL2. `usbipd-win` does exactly this; it implements the USB/IP protocol, which allows USB devices to be transmitted from one machine to another via the network protocol stack. In the context of WSL2, this means transmitting from Windows to the WSL2 "virtual machine".

Now let's start the configuration.

## Windows Side: Installing and Configuring usbipd-win

First, ensure you are using WSL2 and not WSL1. WSL1 is a translation layer that uses the Windows kernel directly, so USB issues don't exist there—but WSL1 has many other limitations, such as lack of Docker support, so most people use WSL2 now. You can check this in PowerShell using `wsl --list --verbose`. If your version is 1.x, you need to upgrade to 2.

Next, we install `usbipd-win`. This tool is available on Microsoft's official package manager, `winget`, making installation very simple. Open a PowerShell terminal with **Administrator privileges**—note that administrator privileges are mandatory because USB device operations require elevated rights. Execute:

```powershell
winget install usbipd
```

After installation, you should be able to use the `usbipd` command. Now, let's check which USB devices are in the system:

```powershell
usbipd list
```

This command will list all USB devices. You will see a long list, including your mouse, keyboard, webcam, etc. Each device has a BUSID, formatted like "1-5" or "2-3". Your ST-Link should also be in the list, possibly displayed as "STMicroelectronics ST-LINK..." or similar. Remember its BUSID; for example, mine shows as "1-8".

Next, you need to bind this device to `usbipd-win`. Binding is a one-time operation that tells Windows this device can be passed through in the future. After binding, the device will disappear from Windows Device Manager, its driver will be unloaded, and `usbipd-win` will take over. Execute the bind command:

```powershell
usbipd bind --busid <BUSID>
```

Replace `<BUSID>` with the actual BUSID you see. If successful, you will see a confirmation message. Now the device has disappeared from Windows' view; you can confirm this in Device Manager, and the ST-Link entry should be gone.

However, WSL2 still cannot see the device at this point because binding is just preparation. You also need to "attach" the device to WSL2. This attach operation must be done every time you restart WSL2 or re-plug the device. Let's execute:

```powershell
usbipd attach --wsl --busid <BUSID>
```

This command transmits the device to WSL2 via the USB/IP protocol. The `--wsl` parameter specifies the target as our default WSL distribution. The device should now appear inside WSL2.

The distinction between bind and attach is important: bind is a one-time operation telling Windows "this device can be passed through," while attach must be done every time, equivalent to "I am now connecting this device to WSL2". After a computer restart, the bind state persists, but the attach state is lost and needs to be re-executed.

## Linux Side: Verifying Device Passthrough

Now return to your WSL2 terminal. You can use the `lsusb` command to view the list of USB devices:

```bash
lsusb
```

If everything goes well, you should see output similar to this:

```text
Bus 001 Device 005: ID 0483:3748 STMicroelectronics ST-LINK/V2
```

Or it might be `374b`, depending on your ST-Link version. Version V2 is `3748`, V2-1 is `374b`, but this makes little difference to OpenOCD as it supports both.

The device number information is crucial in this line of output: `Device 005` means this device is `/dev/bus/usb/001/005`. This device node file is the interface we will use later to access the ST-Link.

Now we need to enable WSL2 to access this device. In a native Linux system, you would typically configure udev rules to let the system automatically set correct permissions for USB devices. But in WSL2, udev does not work by default—WSL2 skips udev service startup during boot, causing udev rules to not take effect at all. This is another pitfall of WSL2.

You can try creating a udev rules file `/etc/udev/rules.d/99-stlink.rules` with the content:

```text
SUBSYSTEM=="usb", ATTR{idVendor}=="0483", ATTR{idProduct}=="3748", MODE="0666"
```

Then on native Ubuntu, you would need `sudo udevadm control --reload-rules` to reload the rules. But in WSL2, these commands might have no effect because the udev service isn't running at all.

So we need another approach: manually modifying device permissions.

## Permission Handling in WSL: That Frustrating LIBUSB_ERROR_ACCESS

When you try to connect to the ST-Link with OpenOCD for the first time, you will likely encounter a `LIBUSB_ERROR_ACCESS` error. The meaning of this error is clear: OpenOCD does not have permission to access the `/dev/bus/usb/xxx/yyy` device file.

The solution is simple and crude: use sudo to modify permissions:

```bash
sudo chmod 666 /dev/bus/usb/001/005
```

The problem is that every time you re-attach the USB device, the device number might change. Sometimes the ST-Link is Device 005, and the next time you restart WSL2 it might be Device 006. So manually typing commands is tedious; we need an automation script.

I wrote a simple Bash script that automatically finds the ST-Link device node and modifies permissions:

```bash
#!/bin/bash
BUS=$(lsusb | grep "STMicroelectronics ST-LINK" | awk '{print $2}')
DEVICE=$(lsusb | grep "STMicroelectronics ST-LINK" | awk '{print $4}' | cut -c 1-3)
sudo chmod 666 /dev/bus/usb/$BUS/$DEVICE
```

This script works by using `grep` to find the ST-Link line, then using `awk` to extract the bus number (second column) and the device number (first three characters of the fourth column). The `cut -c 1-3` trick is because the device number in `lsusb` output is followed by a colon, like "005:", and we only want the first three characters.

You can put this script in your `~/bin` directory, add execute permissions with `chmod +x`, and run it after re-attaching the USB device. Alternatively, you can add it to an alias in your `.bashrc` or `.zshrc`, like `alias fixstlink='sudo ~/bin/fix_stlink.sh'`, so in the future you only need to type `fixstlink`.

## OpenOCD Flashing in Action: Witness the Miracle

Now that the device is passed through and permissions are set, we can start flashing the firmware for real. OpenOCD's configuration file system is very flexible. You need to specify two configuration files: one is the interface configuration (describing which debug probe you use), and the other is the target configuration (describing which chip you are flashing).

For ST-Link V2 and STM32F103C8T6, the configuration files are:

- `interface/stlink.cfg` — ST-Link debug probe interface
- `target/stm32f1x.cfg` — STM32F1 series chips

OpenOCD will automatically search its configuration file directory, usually under `/usr/share/openocd/scripts`, so you don't need to write the full path.

The most basic manual flashing command looks like this:

```bash
openocd -f interface/stlink.cfg -f target/stm32f1x.cfg -c "program firmware.bin verify reset exit 0x08000000"
```

Let me explain the parts of this command. The `-f` parameter specifies the configuration files; here we specified two. The `-c` parameter executes OpenOCD commands directly on the command line instead of using a configuration file.

`program firmware.bin` tells OpenOCD to flash the binary file named `firmware.bin`. `verify` means automatically verify after flashing to ensure data is written correctly. `reset` resets the chip after flashing completes so it starts executing the new program from the beginning. `exit` tells OpenOCD to quit after doing this instead of continuing to listen for GDB connections. Finally, `0x08000000` is the Flash start address for the STM32F103, which is the standard address for the ARM Cortex-M series.

If you need to fully erase the chip before flashing (for example, you previously flashed a large program and now want to flash a smaller one; without erasing, there might be residual data), you can add an `erase` command:

```bash
openocd -f interface/stlink.cfg -f target/stm32f1x.cfg -c "flash erase_address 0x08000000 0x20000" -c "program firmware.bin verify reset exit"
```

`flash erase_address 0x08000000 0x20000` will erase 128KB of Flash starting from 0x08000000 (the total capacity of STM32F103C8T6). `0x20000` is hexadecimal, which converts to exactly 131072 bytes = 128KB.

In actual projects, you won't manually type such long commands every time. Using the flash target in CMake is more convenient:

```bash
cmake --build build --target flash
```

This will find the generated firmware file in the `build` directory and automatically call OpenOCD to flash it. This assumes you have configured the flash target in CMakeLists.txt beforehand; you can refer to previous tutorials for details.

## Troubleshooting Common Errors: When Flashing Fails

During this process, you may encounter various errors. Let me summarize the most common ones and their solutions.

`LIBUSB_ERROR_ACCESS` is the most common one, indicating that OpenOCD does not have permission to access the USB device. The solution is to re-run the `fix_stlink.sh` script, or manually `chmod` that device node. If you re-attached the USB device, the device number might have changed, so you need to set permissions again.

`Error: open failed` is a more generic error, usually meaning OpenOCD cannot find the USB device at all. The first step is to confirm whether the device was successfully passed through to WSL2; check with `lsusb`. If you don't see the device, go back to the Windows side and re-execute `usbipd attach --wsl --busid <BUSID>`. If the device is there but OpenOCD still reports an error, it might be a permission issue; continue troubleshooting according to the `LIBUSB_ERROR_ACCESS` flow.

`Error: unable to find a matching device` usually means OpenOCD's configuration file does not match the actual hardware. For example, you are actually using an STM32F4 series chip, but the configuration file specifies `target/stm32f1x.cfg`, or you are using a J-Link debug probe but the configuration file specifies `interface/stlink.cfg`. Check if your hardware model matches the configuration file.

Another situation is where WSL2 cannot see any USB devices at all, and the output of `lsusb` is empty. In this case, `usbipd-win` might not be working correctly, or the WSL2 kernel modules might not be loaded. You can use `lsmod | grep usbip` in WSL2 to check if USB/IP related modules are loaded. If not loaded, you can try `sudo modprobe usbip_core`, but usually the WSL2 kernel configuration should include these modules by default.

## Concise Guide for Native Ubuntu Users

If you are using native Ubuntu Linux (not WSL2), congratulations, things are much simpler. You don't need `usbipd-win` because your Linux kernel can access USB devices directly. You only need to configure udev rules to let the system automatically set correct permissions for the ST-Link.

Create a `/etc/udev/rules.d/99-stlink.rules` file with the content:

```text
SUBSYSTEM=="usb", ATTR{idVendor}=="0483", ATTR{idProduct}=="3748", MODE="0666"
SUBSYSTEM=="usb", ATTR{idVendor}=="0483", ATTR{idProduct}=="374b", MODE="0666"
```

Then reload the udev rules:

```bash
sudo udevadm control --reload-rules && sudo udevadm trigger
```

Unplug and replug the ST-Link, and udev will automatically apply the new rules. After that, your normal user account can access the device directly without sudo or manual permission modification every time. Native Linux's udev system works very well, which is an advantage over WSL2.

## Conclusion: The Cost of Cross-Platform

After struggling through WSL2 USB passthrough, you should now be able to complete the full STM32 development workflow in the WSL2 environment: editing code, compiling firmware, and flashing chips, all happening within a unified environment. Although the `usbipd-win` attach operation is a bit tedious, once you write it into a small script or PowerShell function, daily use is quite convenient.

The WSL2 solution is essentially a compromise—it gives you a near-native Linux development experience on Windows, but the cost is having to take some detours in certain areas. USB passthrough is just one of them; later you may also encounter issues with serial port passthrough, network configuration, etc. But the good news is that these problems have solutions, and once configured, subsequent usage is smooth.

The next article will enter the realm of real embedded development: starting from blinking an LED, we will step-by-step explore STM32 peripheral programming. You will see how modern C++ makes embedded code more concise and safer. For now, get your development environment completely sorted out and master the flashing toolchain; we will soon be able to start writing real code.
