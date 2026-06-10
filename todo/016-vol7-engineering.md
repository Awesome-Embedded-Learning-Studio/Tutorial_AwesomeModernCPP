---
id: "016"
title: "卷七：工程实践 TODO"
category: content
priority: P1
status: active
created: 2026-06-10
updated: 2026-06-10
assignee: charliechen
depends_on:
  - "013-vol4-advanced-topics.md"   # Modules 语言特性在 vol4，工具配置在 vol7
  - "020-compilation-and-reference.md" # compilation/ 是链接器文章的前置
blocks:
  - "017-vol8-domains.md"           # vol8 嵌入式依赖 vol7 交叉编译内容
  - "021-projects.md"               # 贯穿式项目使用 vol7 工程实践
estimated_effort: large
---

# 卷七：工程实践 TODO

## 定位

卷七教一个 C++ 工程师从"能写代码"到"能交付项目"的全部实践能力：构建、测试、质量保障、调试、包管理、CI/CD。

**核心决策记录**：

- 2026-06-10：确认以**重建为主、旧素材选择性回收**的策略，不做增量维护
- 2026-06-10：CMake 深入挖掘——CMake 是事实标准，值得 9 篇覆盖
- 2026-06-10：测试主推 **Catch2 v3**（仓库 vol9 已在用），GoogleTest 对比性覆盖
- 2026-06-10：包管理**平铺对比**（FetchContent/CPM/vcpkg/Conan），严肃处理
- 2026-06-10：新增**调试专题**（GDB/LLDB、高级调试、性能分析）
- 2026-06-10：嵌入式边界方案 C——通用工程为主线，嵌入式案例保留在交叉编译和链接器两篇
- 2026-06-10：CMake 最低版本推荐 **3.28+**（支持完整 Preset + C++20 Modules）

## Current Assets

### 已有文档（9 篇，全部待重新定位）

| 文件 | 行数 | 处置方案 |
|------|------|---------|
| `01-cross-compilation-and-cmake.md` | 471 | **回收** → ch01-07，嵌入式案例保留，扩展 Preset + toolchain 组合 |
| `02-compiler-options.md` | 229 | **回收** → ch01-08，扩展 sanitizer 标志 |
| `03-linker-and-linker-scripts.md` | 442 | **回收** → ch01-09，顶部加 compilation/ 前置链接 |
| `01-file-copier-requirements-and-framework.md` | 199 | **迁移** → `projects/` |
| `02-file-copier-core-implementation.md` | 504 | **迁移** → `projects/` |
| `cpp-development-on-wsl.md` | 157 | **降级** → 附录或工具链笔记 |
| `cpp-modules-on-vs2026.md` | 88 | **合并** → ch01 C++20 Modules 段落，或交叉链接到 vol4 |
| `msvc-debugging-internals.md` | 137 | **回收素材** → ch04 调试文章 |
| `index.md` | 42 | **重写** |

### 已有代码

- `code/templates/cm_embedded.cmake`：嵌入式 Modern CMake 模板，需在 ch01 文章中引用
- `code/templates/linker_script.ld`：ARM 链接脚本模板，需在 ch01-09 中引用
- `code/stm32f1-tutorials/`：4 个 STM32F1 教程，ch01-07/09 的嵌入式案例来源
- **无专属 `code/vol7/` 目录**，需新建

### 仓库自身工程基础设施（可成为 ch06 案例）

- `.github/workflows/`：5 个 GitHub Actions workflow
- `.pre-commit-config.yaml`：pre-commit 配置
- `scripts/build.ts`：分卷并行构建（712 行 TypeScript）
- `scripts/validate_frontmatter.py`、`check_links.py`、`check_quality.py`
- `scripts/translate.py`：AI 翻译系统（1,123 行）
- `scripts/coverage.py`：翻译覆盖率追踪

### 英文翻译

当前 9 篇已全部翻译。重建后需运行 `scripts/translate.py` 重新翻译。

## 完整文章大纲

### ch01: 构建系统——CMake 深入（9 篇）

> 定位：从"会用 CMake"到"精通 Modern CMake"。CMake 3.28+ 为最低版本。

| # | 文件名 | 标题 | 核心内容 | 来源 |
|---|--------|------|---------|------|
| 01 | `01-modern-cmake-project-structure.md` | Modern CMake 项目结构 | target-centric 范式、目录约定（src/app/tests/examples）、`cmake_minimum_required` 版本选择（推荐 3.28+）、CMake 4.0 迁移须知 | 新建 |
| 02 | `02-transitive-deps-and-genex.md` | 传递依赖与生成器表达式 | PUBLIC/PRIVATE/INTERFACE 语义、核心 genex（`$<CONFIG:>`, `$<COMPILE_FEATURES:>`, `$<BUILD_INTERFACE:>`, `$<IF:>`）、反模式清单 | 新建 |
| 03 | `03-cmake-presets.md` | CMake Preset 工程化 | CMakePresets.json 完整结构、hidden preset + 继承、条件过滤、CMakeUserPresets、CI 集成模式、IDE 统一体验（VS Code / CLion / VS） | 新建 |
| 04 | `04-fetchcontent-and-deps.md` | 依赖管理：FetchContent 与依赖提供者 | `FetchContent_MakeAvailable`（`_Populate` 已在 3.30 废弃）、`FIND_PACKAGE_ARGS`、`OVERRIDE_FIND_PACKAGE`、依赖去重、FetchContent vs ExternalProject 决策指南 | 新建 |
| 05 | `05-install-and-export.md` | 库的安装与分发 | `install(TARGETS ... EXPORT)`、`CMakePackageConfigHelpers`、`find_package` 工作原理（Config vs FindModule）、namespace target、CPS (Common Package Specification v0.14.1) 前瞻 | 新建 |
| 06 | `06-build-performance.md` | 构建性能优化 | Unity builds (`CMAKE_UNITY_BUILD`)、Precompiled Headers (`target_precompile_headers`)、ccache/sccache (`CMAKE_CXX_COMPILER_LAUNCHER`)、Ninja 生成器、各技术适用场景与冲突 | 新建 |
| 07 | `07-cross-compilation-and-toolchain.md` | 交叉编译与工具链文件 | toolchain file 最佳实践、CMAKE_FIND_ROOT_PATH 深入、sysroot、Preset + toolchain 组合、SuperBuild 模式、嵌入式案例（ARM Cortex-M） | **回收旧文** |
| 08 | `08-compiler-options-and-diagnostics.md` | 编译器选项与诊断 | 优化级别（-O0/-Og/-O2/-Os/-Oz）、runtime trim（-fno-exceptions/-fno-rtti）、sanitizer 编译标志（-fsanitize=）、CMake 中配置编译器选项的 target-centric 方式 | **回收旧文** |
| 09 | `09-linker-in-practice.md` | 链接器实践 | 链接器原理回顾、ARM 链接脚本实战、startup code、LTO。前置阅读：compilation/ 卷 | **回收旧文** |

### ch02: 测试——Catch2 体系（5 篇）

> 定位：从零到一的测试能力建设。主推 Catch2 v3.10+，GoogleTest 作为对比性覆盖。

| # | 文件名 | 标题 | 核心内容 |
|---|--------|------|---------|
| 01 | `01-test-framework-selection.md` | 测试框架选型 | Catch2 v3 / GoogleTest v1.17 / doctest v2.5 对比（API、编译速度、功能深度、C++ 最低标准）、选型决策树 |
| 02 | `02-catch2-in-practice.md` | Catch2 实战入门 | FetchContent 集成 Catch2 v3、TEST_CASE / SECTION / REQUIRE / CHECK、BDD 风格、`catch_discover_tests()`、CTest 集成 |
| 03 | `03-advanced-testing.md` | 高级测试技巧 | 参数化测试（GENERATE）、浮动点比较、自定义 matcher、测试 fixture、测试过滤与标签 |
| 04 | `04-mock-and-isolation.md` | Mock 与测试隔离 | 依赖注入模式、Trompeloeil 入门、接口 mock 实战、FakeIt 简述、GoogleMock 对比 |
| 05 | `05-coverage-and-embedded-testing.md` | Coverage 与嵌入式测试 | gcov/lcov 和 llvm-cov 工作流、CMake coverage 配置、host-side testing 策略、HAL mock 模式、嵌入式测试边界（Unity/CppUTest 何时使用） |

### ch03: 代码质量（3 篇）

> 定位：工程的"安全网"——在代码到达用户之前拦截问题。

| # | 文件名 | 标题 | 核心内容 |
|---|--------|------|---------|
| 01 | `01-static-analysis.md` | 静态分析 | clang-tidy 配置与规则选型（modernize-\*、bugprone-\*、cppcoreguidelines-\*）、`CMAKE_CXX_CLANG_TIDY` 集成、cppcheck 辅助、`.clang-tidy` 文件实践 |
| 02 | `02-sanitizers.md` | Sanitizer | ASan / UBSan / TSan 原理与配置、CMake 中启用 sanitizer 的 target-centric 方式、CI 中运行 sanitizer 构建。注意：GCC 不支持 MSan |
| 03 | `03-formatting-and-automation.md` | 代码格式化与自动化 | clang-format 配置、`.clang-format` 文件实践、pre-commit 框架集成、仓库自身的 `.pre-commit-config.yaml` 案例拆解 |

### ch04: 调试（3 篇）

> 定位：工程必备能力——当代码行为不符预期时，系统性地找到原因。

| # | 文件名 | 标题 | 核心内容 |
|---|--------|------|---------|
| 01 | `01-gdb-and-lldb.md` | 调试基础：GDB 与 LLDB | 断点（条件/数据/函数）、watchpoint、栈帧与线程检查、CMake debug 构建配置（`-g` / `-g3` / DWARF）、GDB 前端（VS Code 集成） |
| 02 | `02-advanced-debugging.md` | 高级调试技术 | 远程调试（嵌入式 GDB + OpenOCD）、core dump 分析、post-mortem 调试、多线程调试技巧、rr (Mozilla record-replay) 简介 |
| 03 | `03-profiling-basics.md` | 性能分析入门 | perf 基础工作流、Valgrind / Callgrind / KCachegrind、Heaptrack、Google Benchmark + CMake 集成、Tracy 简介。深入分析交叉链接到 vol6 |

### ch05: 包管理（5 篇）

> 定位：严肃的平铺对比——2026 年 C++ 包管理是社区最热话题之一。

| # | 文件名 | 标题 | 核心内容 |
|---|--------|------|---------|
| 01 | `01-package-management-landscape.md` | C++ 包管理全景 | 依赖管理的核心问题（版本冲突、二进制缓存、传递依赖）、CPS 标准前瞻（v0.14.1，CMake 4.3+ 原生支持）、生态方向 |
| 02 | `02-fetchcontent-and-cpm.md` | FetchContent 与 CPM.cmake | FetchContent 最佳实践、CPM.cmake v0.40 的增强（`CPM_SOURCE_CACHE`、版本检查、`package-lock.cmake`）、适用场景：中小项目、零外部工具依赖 |
| 03 | `03-vcpkg-in-practice.md` | vcpkg 实战 | manifest mode（推荐）、toolchain file 集成、binary caching（NuGet + GitHub Packages）、自定义 registry、2,833+ ports 生态 |
| 04 | `04-conan2-in-practice.md` | Conan 2.x 实战 | CMakeDeps + CMakeToolchain 集成、lockfile 可复现构建、`tool_requires`、多构建系统支持、ConanCenter ~1,938 recipes 生态 |
| 05 | `05-package-manager-selection.md` | 包管理选型决策 | 四种方案对比矩阵（易用性/二进制缓存/版本冲突/CMake 集成/包数量/学习曲线）、决策树、混合使用模式 |

### ch06: CI/CD 与发布（4 篇）

> 定位：以真实项目为案例——仓库自身的 5 个 workflow 和 build.ts 是现成教材。

| # | 文件名 | 标题 | 核心内容 |
|---|--------|------|---------|
| 01 | `01-cicd-basics-and-github-actions.md` | CI/CD 基础与 GitHub Actions | GitHub Actions 工作流结构、CMake Preset 驱动的 CI、缓存策略（ccache、build cache）、矩阵构建（多编译器/多标准） |
| 02 | `02-real-project-ci-cd.md` | 以真实项目为例的 CI/CD | 拆解本仓库的 5 个 workflow + pre-commit 配置 + `scripts/build.ts` 分卷并行构建，展示"我们怎么做的" |
| 03 | `03-versioning-and-release.md` | 版本管理与发布流程 | semantic versioning、conventional commits、changelog 生成、git tag、release checklist、CPack 打包简介 |
| 04 | `04-doc-site-quality-loop.md` | 文档站质量闭环 | VitePress 构建与部署、链接检查自动化、翻译覆盖率追踪、发布前检查清单 |

## 卷间依赖与交叉链接

### 前置卷

| 卷 | 关系 | 链接方式 |
|----|------|---------|
| **compilation/** | 前置——ch01-09 链接器文章假设读者已有编译链接基础 | ch01-09 顶部加"前置阅读"链接 |
| **vol2（现代特性）** | 前置——CMake/编译器文章引用 C++11-20 特性 | 自然引用 |
| **vol4（高级主题）** | 双向——Modules 语言特性在 vol4，工具配置在 vol7 | ch01 中 Modules 段落交叉链接到 vol4 |

### 后续卷

| 卷 | 关系 | 链接方式 |
|----|------|---------|
| **vol5（并发）** | ch05 ch08 的 TSan/benchmark 依赖 vol7 的工具基础 | vol5 ch08 引用 vol7 ch03/ch04 |
| **vol6（性能）** | ch04-03 性能分析入门 → vol6 深入 | 交叉链接 |
| **vol8（嵌入式）** | 强依赖——vol8 嵌入式章节直接链接 vol7 交叉编译 | ch01-07/09 保留嵌入式案例 |
| **vol9（开源项目）** | vol9 使用 Catch2/CPM/CMake——vol7 提供教程化内容 | 自然引用 |
| **projects/** | 贯穿式项目使用 vol7 工程实践 | 自然引用 |

### 重叠主题的处理

| 主题 | 涉及卷 | 处理 |
|------|--------|------|
| Modules 语言特性 | vol4 | vol4 讲语义，vol7 讲 CMake 工具配置 |
| 编译器优化原理 | vol6 | vol6 讲"编译器怎么优化"，vol7 讲"怎么配置编译器选项" |
| 链接器库与符号 | compilation/ | compilation/ 讲库/符号机制，vol7 讲嵌入式链接脚本实践 |
| 并发测试 | vol5 ch08 | vol5 是并发特化，vol7 是通用测试方法论 |
| 嵌入式工程准备 | vol8 | vol7 讲通用交叉编译，vol8 讲 STM32 板级细节 |

## 需要标准状态核验的内容

以下内容涉及时间敏感的标准或工具版本，写作时需要核验：

- `needs standard-status verification`：CPS (Common Package Specification) 的版本和 CMake 支持状态
- `needs standard-status verification`：C++20 Modules 在 CMake 4.x 中的成熟度
- `needs standard-status verification`：vcpkg 和 Conan 的当前版本和 API
- `needs standard-status verification`：Catch2 v3 的最新版本和集成方式
- `needs standard-status verification`：各 sanitizer 在 GCC/Clang 最新版上的支持情况

## 写作顺序建议

### 第一批：ch01 构建系统（核心基础）

1. ch01-01: Modern CMake 项目结构（建立 target-centric 基础范式）
2. ch01-02: 传递依赖与生成器表达式
3. ch01-03: CMake Preset 工程化
4. ch01-04: FetchContent 与依赖提供者
5. ch01-08: 编译器选项与诊断（回收旧文）

### 第二批：ch02 测试 + ch03 代码质量

6. ch02-01: 测试框架选型
7. ch02-02: Catch2 实战入门
8. ch03-02: Sanitizer（与 ch01-08 编译器选项自然衔接）
9. ch02-04: Mock 与测试隔离
10. ch02-05: Coverage 与嵌入式测试

### 第三批：ch04 调试 + ch05 包管理 + ch01 收尾

11. ch04-01: GDB 与 LLDB
12. ch04-03: 性能分析入门
13. ch01-05: 库的安装与分发
14. ch05-01: 包管理全景
15. ch05-02–05: 各包管理方案实战与选型

### 第四批：ch06 CI/CD + ch01 深度文章

16. ch01-06: 构建性能优化
17. ch01-07: 交叉编译与工具链文件（回收旧文）
18. ch01-09: 链接器实践（回收旧文）
19. ch06-01–04: CI/CD 与发布

### 每篇完成后

- 创建配套 `code/vol7/` 下的 CMake 示例工程
- 更新 vol7 `index.md`
- 运行 `scripts/translate.py` 生成英文翻译
- 运行 frontmatter 校验和链接检查

## Acceptance

- [ ] 卷七形成 CMake → testing → quality → debugging → packaging → CI/CD 的工程闭环
- [ ] 29 篇文章全部有配套 code 示例
- [ ] 仓库自身的 CI/scripts 被纳入 ch06 案例文章
- [ ] 嵌入式工程准备和 vol8 互链，不重复维护
- [ ] 旧文章完成回收/迁移/降级
- [ ] 英文翻译同步更新
- [ ] 所有交叉链接与 compilation/、vol4、vol5、vol6、vol8 对齐

## Key References

### 调研来源

- CMake 4.3.3 官方文档（cmake.org）
- Craig Scott, "Professional CMake: A Practical Guide"（第 22 版）
- CppCon 2025: "CMake Doesn't Have to Be Painful" (Bret Brown)
- CppCon 2025: "The Evolution of CMake: 25 Years" (Bill Hoffman)
- C++Now 2025: "Effective CTest" (Daniel Pfeifer)
- C++Now 2025: "CPS in CMake" (Bill Hoffman)
- Catch2 v3.10.0 GitHub + 官方文档
- GoogleTest v1.17.0 GitHub + 官方文档
- doctest v2.5.2 GitHub
- vcpkg 2026.06.01 registry（2,833+ ports）
- Conan 2.29.0 changelog
- CPM.cmake v0.40.3 GitHub
- CPS v0.14.1 spec（cps-org.github.io）
- WG21 P2656R2（C++ Ecosystem IS）
