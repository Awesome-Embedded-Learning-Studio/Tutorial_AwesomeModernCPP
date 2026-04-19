---
id: "050"
title: "CI 自动编译 code/ 下所有 CMake 项目"
category: automation
priority: P1
status: done
created: 2026-04-15
assignee: charliechen
depends_on: ["002"]
blocks: []
estimated_effort: medium
---

# CI 自动编译 code/ 下所有 CMake 项目

## 目标

建立 GitHub Actions 工作流，在每次 Pull Request 和推送到 main 分支时，自动编译 `code/` 目录下所有 CMake 项目。工作流需要分别处理两类编译目标：

1. **Host Examples**：使用 GCC/Clang 在主机上编译纯软件示例（如算法演示、单元测试）。
2. **STM32 Cross-Compile**：使用 `arm-none-eabi-gcc` 交叉编译 STM32 嵌入式项目，验证嵌入式代码的编译正确性。

这确保任何 PR 都不会破坏现有代码的编译，为项目建立持续集成的基础保障。

## 验收标准

- [ ] GitHub Actions 工作流文件 `.github/workflows/build-examples.yml` 已创建并可正常运行
- [ ] PR 触发时自动运行编译检查
- [ ] Host examples 编译 job 能自动发现 `code/` 下所有包含 `CMakeLists.txt` 的主机项目并逐一编译
- [ ] STM32 cross-compile job 使用 `arm-none-eabi-gcc` 工具链编译所有嵌入式项目
- [ ] 编译失败时 PR 检查状态为 failed，并输出清晰的错误日志
- [ ] `scripts/build_examples.py` 脚本可本地运行，支持 `--host` 和 `--stm32` 参数选择编译目标
- [ ] 工作流执行时间不超过 10 分钟
- [ ] README 中添加 build status 徽章

## 实施说明

### 工作流设计

```yaml
# .github/workflows/build-examples.yml
name: Build Examples

on:
  pull_request:
    paths:
      - 'code/**'
      - 'scripts/build_examples.py'
  push:
    branches: [main]
    paths:
      - 'code/**'
```

工作流包含两个并行 job：

1. **build-host-examples**
   - 运行环境：`ubuntu-latest`
   - 安装依赖：`cmake`, `gcc`, `g++`, `ninja`
   - 步骤：checkout → 安装工具 → 运行 `python3 scripts/build_examples.py --host`
   - 自动扫描 `code/` 下所有非嵌入式 CMake 项目

2. **build-stm32-examples**
   - 运行环境：`ubuntu-latest`
   - 安装依赖：`cmake`, `gcc-arm-none-eabi`, `ninja`
   - 通过 APT 或 ARM 官方 tarball 安装工具链
   - 步骤：checkout → 安装工具链 → 运行 `python3 scripts/build_examples.py --stm32`
   - STM32 项目通过 `toolchain-arm-none-eabi.cmake` 文件识别

### build_examples.py 脚本设计

脚本核心逻辑：

```python
def discover_cmake_projects(root_dir: str, target: str) -> list[Path]:
    """扫描目录，返回所有 CMake 项目路径。
    
    - target='host': 排除包含 toolchain-arm-none-eabi.cmake 的项目
    - target='stm32': 仅包含含 toolchain 文件的项目
    """
```

对每个项目执行：
1. 创建 `_build/` 构建目录
2. `cmake -B _build -G Ninja [toolchain flags]`
3. `cmake --build _build`
4. 捕获返回码和输出，汇总报告

输出格式：`[PASS/FAIL] path/to/project - 编译耗时 Xs`

### 工具链安装策略

ARM 工具链安装选项（推荐方案 1）：
1. **APT**: `sudo apt-get install gcc-arm-none-eabi`（版本可能较旧但稳定）
2. **ARM 官方 tarball**: 从 ARM 官网下载最新版，缓存到 GitHub Actions cache
3. **conda-forge**: `conda install -c conda-forge arm-none-eabi-gcc`

建议先尝试 APT 方案，若版本不满足需求再切换到 tarball + cache 方案。

## 涉及文件

- `.github/workflows/build-examples.yml` — GitHub Actions 工作流定义
- `scripts/build_examples.py` — 编译脚本（项目发现 + 批量编译 + 报告）

## 参考资料

- [GitHub Actions 官方文档](https://docs.github.com/en/actions)
- [ARM GNU Toolchain 下载](https://developer.arm.com/tools-and-software/open-source-software/developer-tools/gnu-toolchain)
- [CMake CI 最佳实践](https://cmake.org/cmake/help/latest/manual/cmake.1.html)
