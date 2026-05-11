# Environment Setup (Part 4): WSL2 USB Passthrough, Making ST-Link Cross the Virtualization Boundary

## Preface: The Biggest Pitfall on This Journey

If you have been following the previous tutorials, your WSL2 environment now has the ARM toolchain, OpenOCD, and perhaps even your first compiled firmware file. When you eagerly plug in the ST-Link debug probe, ready to flash the program to the STM32, reality hits you hard—WSL2 cannot see USB devices at all.

I am currently going through this phase myself, where the `lsusb` output is completely empty. Let alone an ST-Link, you cannot even see a mouse. This is not a user error; it is an inherent limitation of the WSL2 architecture. WSL2 uses Hyper-V virtualization technology, and Linux runs as a true virtual machine under Windows, but Microsoft did not implement USB device passthrough. Your ST-Link is plugged into a Windows USB port, claimed by Windows drivers, and the Linux side is completely unaware of its existence.

This problem plagued me for days. I searched online for all sorts of resources; some recommended using a virtual machine approach, while others suggested giving up on WSL2 entirely and installing native Ubuntu. But I did not want to give up, because the rest of WSL2 is simply too convenient—its integration with the Windows file system, terminal experience, and package management are all hard to match with native Linux. Eventually, I found the usbipd-win project, a tool officially maintained by Microsoft specifically designed to solve the WSL2 USB passthrough problem.

Today, we will fill this pitfall once and for all, allowing the ST-Link to smoothly cross from Windows into WSL2, and then complete your first OpenOCD flash.

## The WSL2 USB Problem: What Is Actually Happening

Let us first understand the root of the problem. Although WSL2 feels like a Linux program inside Windows, it is actually a complete virtual machine. When you open a WSL2 terminal, you are interacting with a Hyper-V virtual machine named "WSL". This virtual machine has its own kernel, its own memory management, and its own device tree.

In the PC architecture, USB devices are managed by host controllers. Your motherboard has several USB controllers, with multiple USB ports under each controller. When a USB device is plugged in, the controller assigns it an address, and the operating system loads the corresponding driver to communicate with the device. The problem is that inside the WSL2 virtual machine, the USB controller is virtualized and cannot connect to the physical USB controllers, making physically plugged-in devices invisible to WSL2.

The Windows host can see your ST-Link, and Device Manager recognizes it normally, but the WSL2 Linux kernel cannot see it. This is why we need a passthrough mechanism to "lend" the USB devices seen by Windows to WSL2. usbipd-win does exactly this; it implements the USB/IP protocol, which allows USB devices to be transmitted from one machine to another over the network protocol stack. In the WSL2 scenario, this means transmitting from Windows to the "virtual machine" that is WSL2.

Now let us start configuring.

## Windows Side: Installing and Configuring usbipd-win

First, make sure you are using WSL2 and not WSL1. WSL1 is a translation layer that directly uses the Windows kernel, so the USB problem does not exist in WSL1 at all—but WSL1 has many other limitations, such as lack of Docker support, which is why most people use WSL2 now. You can verify this in PowerShell with `wsl --status`, and if your version is 1.x, you need to upgrade to 2.

Next, we install usbipd-win. This tool is available on Microsoft's official package manager winget, making installation very simple. Open a **PowerShell terminal with administrator privileges**—note that administrator privileges are mandatory because USB device operations require elevated rights. Run:

```powershell
winget install usbipd
```

After installation, you should be able to use the `usbipd` command. Now let us check which USB devices are on the system:

```powershell
usbipd list
```

This command will list all USB devices, and you will see a long list including your mouse, keyboard, webcam, and so on. Each device has a BUSID, in a format like "1-5" or "2-3". Your ST-Link should also be in the list, likely shown as "STMicroelectronics ST-LINK..." or a similar name. Remember its BUSID; for example, mine shows as "1-8".

Next, you need to bind this device to usbipd-win. Binding is a one-time operation that tells Windows this device can be passthrough in the future. After binding, the device will disappear from Windows Device Manager, its driver will be unloaded, and usbipd-win will take over. Run the bind command:

```powershell
usbipd bind --busid 1-8
```

Replace `BUSID` with the actual BUSID you see. If successful, you will see a confirmation message. The device has now disappeared from Windows' view; you can verify this in Device Manager, where the ST-Link entry should be gone.

But WSL2 still cannot see the device at this point, because binding is just preparation. You also need to "attach" the device to WSL2. This attach operation must be done every time you restart WSL2 or re-plug the device. Let us run:

```powershell
usbipd attach --wsl --busid 1-8
```

This command transmits the device to WSL2 via the USB/IP protocol. The `--wsl` parameter specifies that the target is our default WSL distribution. The device should now appear in WSL2.

The difference between bind and attach is important: bind is a one-time operation that tells Windows "this device can be passthrough in the future," while attach must be done each time, equivalent to "I am now connecting this device to WSL2." After you restart your computer, the bind state is preserved, but the attach is lost and needs to be re-executed.

## Linux Side: Verifying Device Passthrough

Now go back to your WSL2 terminal. You can use the `lsusb` command to view the USB device list:

```bash
lsusb | grep -i stlink
```

If all goes well, you should see output similar to this:

```text
Bus 001 Device 005: ID 0483:3748 STMicroelectronics ST-LINK/V2
```

Or it might be `374b`, depending on your ST-Link version. The V2 version is 3748, and V2-1 is 374b, but this makes little difference to OpenOCD, as it supports both.

The device number information in this line of output is important: `Device 005` means this device is at `/dev/bus/usb/BUS/005`. This device node file is the interface we will use later to access the ST-Link.

Now we need to let WSL2 access this device. On a native Linux system, you would typically configure udev rules to let the system automatically set the correct permissions for USB devices. But in WSL2, udev does not work by default—WSL2 skips the udev service startup, which means udev rules never take effect. This is another pitfall of WSL2.

You can try creating a udev rules file `/etc/udev/rules.d/99-stlink.rules` with the following content:

```text
# STM32 ST-LINK/V2
SUBSYSTEM=="usb", ATTR{idVendor}=="0483", ATTR{idProduct}=="3748", MODE="0666"
# STM32 ST-LINK/V2-1
SUBSYSTEM=="usb", ATTR{idVendor}=="0483", ATTR{idProduct}=="374b", MODE="0666"
```

Then on native Ubuntu, you would need `sudo udevadm control --reload-rules` to reload the rules. But in WSL2, these commands might not have any effect because the udev service is not running at all.

So we need another approach: manually modifying device permissions.

## Permission Handling in WSL: The Infuriating LIBUSB_ERROR_ACCESS

When you first try to connect to the ST-Link with OpenOCD, you will very likely encounter the `LIBUSB_ERROR_ACCESS` error. The meaning of this error is clear: OpenOCD does not have permission to access the `/dev/bus/usb/...` device file.

The solution is simple and brute-force: use sudo to modify the permissions:

```bash
sudo chmod 666 /dev/bus/usb/001/005
```

But the problem is that every time you re-attach the USB device, the device number might change. Sometimes the ST-Link is Device 005, and the next time you restart WSL2, it might become Device 006. So typing the command manually is tedious, and we need an automation script.

I wrote a simple `fix-stlink.sh` script that automatically finds the ST-Link device node and modifies its permissions:

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

The working principle of this script is: it uses `lsusb` to find the ST-Link line, then uses awk to extract the bus number (second column) and device number (the first three characters of the fourth column). The `substr($4, 1, 3)` trick is there because the device number in the lsusb output is followed by a colon, such as "005:", and we only want the first three characters.

You can put this script in the `~/bin` directory, add execute permission with `chmod +x`, and run it after re-attaching the USB device each time. Or you can add it as an alias in your `.bashrc` or `.zshrc`, such as `alias fix-stlink='sudo ~/bin/fix-stlink.sh'`, so that in the future you only need to type `fix-stlink`.

## OpenOCD Flashing in Action: The Moment of Truth

Now that the device is passthrough and the permissions are set, we can start actually flashing the firmware. OpenOCD's configuration file system is very flexible. You need to specify two configuration files: one is the interface configuration, describing which debug probe you are using; the other is the target configuration, describing which chip you are flashing.

For ST-Link V2 and STM32F103C8T6, the configuration files are:

- `interface/stlink.cfg` — ST-Link debug probe interface
- `target/stm32f1x.cfg` — STM32F1 series chips

OpenOCD will automatically search its configuration file directory, usually under `/usr/share/openocd/scripts`, so you do not need to write the full path.

The most basic manual flash command looks like this:

```bash
openocd -f interface/stlink.cfg -f target/stm32f1x.cfg \
        -c "program firmware.bin verify reset exit 0x08000000"
```

Let me explain the parts of this command. The `-f` parameter specifies the configuration files; here we specified two. The `-c` parameter executes OpenOCD commands directly on the command line, rather than using those in a configuration file.

`program firmware.bin` tells OpenOCD to flash the binary file named `firmware.bin`. `verify` automatically verifies after flashing to ensure the data was written correctly. `reset` resets the chip after flashing is complete, making it start executing the new program from the beginning. `shutdown` tells OpenOCD to exit after doing all this, rather than continuing to listen for GDB connections. Finally, `0x08000000` is the Flash start address of the STM32F103, which is the standard address for the ARM Cortex-M series.

If you need to fully erase the chip before flashing (for example, if you previously flashed a large program and now want to flash a smaller one, where residual data might remain without erasing), you can add an `erase` command:

```bash
openocd -f interface/stlink.cfg -f target/stm32f1x.cfg \
        -c "flash erase_address 0x08000000 0x20000" \
        -c "program firmware.bin verify reset exit 0x08000000"
```

`erase` will erase 128KB of Flash starting from 0x08000000 (the total capacity of the STM32F103C8T6). `0x20000` is in hexadecimal, which converts to exactly 131072 bytes = 128KB in decimal.

In real projects, you will not type such a long command manually every time. Using CMake's flash target is more convenient:

```bash
cmake --build build --target flash
```

This will find the generated firmware file in the `build` directory and automatically invoke OpenOCD to flash it. The prerequisite is that you have configured the flash target in CMakeLists.txt beforehand; you can refer to the previous tutorials for details.

## Common Error Troubleshooting: When Flashing Fails

During this process, you may encounter various errors. Let me summarize the most common ones and their corresponding solutions.

`LIBUSB_ERROR_ACCESS` is the most common one, meaning OpenOCD does not have permission to access the USB device. The solution is to re-run the `fix-stlink.sh` script, or manually `chmod 666` that device node. If you re-attached the USB device, the device number might have changed, so you need to set the permissions again.

`Error: open failed` is a more generic error, usually meaning OpenOCD cannot find the USB device at all. The first step here is to confirm whether the device was successfully passthrough to WSL2; check with `lsusb`. If you cannot see the device, go back to the Windows side and re-execute `usbipd attach`. If the device is there but OpenOCD still reports an error, it might be a permission issue, so continue troubleshooting following the LIBUSB_ERROR_ACCESS flow.

`target not found` usually means OpenOCD's configuration files do not match the actual hardware. For example, if you are actually using an STM32F4 series chip but the configuration file says `stm32f1x.cfg`, or if you are using a J-Link debug probe but the configuration file says `stlink.cfg`. Check whether your hardware model matches the configuration files.

Another situation is when WSL2 cannot see any USB devices at all, and the `lsusb` output is empty. This might be because usbipd-win is not working correctly, or the WSL2 kernel modules are not loaded. You can use `lsmod | grep usbip` in WSL2 to check whether the USB/IP related modules are loaded. If they are not loaded, you can try `sudo modprobe usbip-core`, but usually the WSL2 kernel configuration should already include these modules.

## A Brief Guide for Native Ubuntu Users

If you are using native Ubuntu Linux (not WSL2), congratulations, things are much simpler. You do not need usbipd-win because your Linux kernel can directly access USB devices. You only need to configure udev rules to let the system automatically set the correct permissions for the ST-Link.

Create the `/etc/udev/rules.d/99-stlink.rules` file with the following content:

```text
# STM32 ST-LINK/V2
SUBSYSTEM=="usb", ATTR{idVendor}=="0483", ATTR{idProduct}=="3748", MODE="0666", TAG+="uaccess"
# STM32 ST-LINK/V2-1
SUBSYSTEM=="usb", ATTR{idVendor}=="0483", ATTR{idProduct}=="374b", MODE="0666", TAG+="uaccess"
```

Then reload the udev rules:

```bash
sudo udevadm control --reload-rules
sudo udevadm trigger
```

Unplug and re-plug the ST-Link, and udev will automatically apply the new rules. After that, your regular user account can directly access the device without needing sudo or manually modifying permissions each time. The native Linux udev system works very well; this is one of its advantages over WSL2.

## Conclusion: The Price of Cross-Platform

After wrestling with WSL2's USB passthrough, you should now be able to complete the entire STM32 development workflow in the WSL2 environment: editing code, compiling firmware, and flashing the chip, all within a unified environment. Although the usbipd-win attach operation is a bit tedious, once you write it into a small script or PowerShell function, daily use is quite convenient.

The WSL2 approach is essentially a compromise—it gives you a development experience close to native Linux on Windows, but the price is having to take some detours in certain areas. USB passthrough is just one of them; later you might also encounter issues like serial device passthrough and network configuration. But the good news is that all these pitfalls have solutions, and once configured, subsequent usage is smooth.

In the next article, we will dive into real embedded development: starting with blinking an LED, we will step by step explore STM32 peripheral programming. You will see how modern C++ makes embedded code cleaner and safer. For now, get your development environment fully set up, practice using the flashing toolchain, and we will soon start writing real code.
