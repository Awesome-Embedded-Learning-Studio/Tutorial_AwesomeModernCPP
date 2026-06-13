---
title: Development Environment Setup
description: From toolchains, project structure, and CMake to WSL2 USB passthrough
  and GDB debugging, we build a complete scaffold for STM32 development.
platform: stm32f1
tags:
- cpp-modern
- intermediate
- stm32f1
translation:
  source: documents/vol8-domains/embedded/00-env-setup/index.md
  source_hash: ee52fb9205ca4bce0366c90b1e5d0a6877d33edf895f850ce9c89f11213610cd
  translated_at: '2026-06-13T11:53:33.570080+00:00'
  engine: anthropic
  token_count: 140
---
# Development Environment Setup

> From the cross-compilation toolchain to a complete GDB debugging environment, we lay a solid foundation for STM32 development—all future hands-on projects will stand on this setup.

## Toolchain and Project Structure

- [Part 1: Building the STM32 Toolchain from Scratch](01-toolchain-setup.md) — Cross-compilation principles and installation guide
- [Part 2: Project Structure](02-project-structure.md) — HAL library acquisition, startup file pitfalls, and directory setup
- [CMake Configuration](03-cmake-configuration.md) — Building an STM32 build system from scratch

## WSL2 and Debugging

- [Part 4: WSL2 USB Passthrough](04-wsl2-usb.md) — Making ST-Link cross virtualization boundaries
- [Part 5: Advanced Debugging](05-debugging-guide.md) — From printf to a complete GDB debugging environment
