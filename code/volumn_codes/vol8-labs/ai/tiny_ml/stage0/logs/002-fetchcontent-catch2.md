# 002 · FetchContent 拉取 Catch2 机制

> 出处:`CMakeLists.txt` 第 14–22 行。
> 关联:handbook M0 第 259–266 行(骨架)、坑 1(第 332–336 行)。

## 原命令

```cmake
include(FetchContent)
FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.5.0)
FetchContent_MakeAvailable(Catch2)
list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
```

## 一句话定位

在 CMake **配置阶段(configure time)** 从 Git 拉 Catch2 源码,直接 `add_subdirectory` 同构编译进工程,无需预装/无 submodule/无 vcpkg。版本由 `GIT_TAG v3.5.0` 锁死。

## 三步机制

| 步骤 | 行为 | 何时下载 |
|------|------|----------|
| `FetchContent_Declare` | 只登记(name + 来源 + tag),写入 CMake 内部记录 | **不下载** |
| `FetchContent_MakeAvailable` | 首次配置时 clone + `add_subdirectory` + 定义 target(`Catch2::Catch2WithMain`) + 设 `<depname>_SOURCE_DIR` 变量 | **下载** |
| `list(APPEND CMAKE_MODULE_PATH .../extras)` | 把 Catch2 的辅助 `.cmake`(`Catch.cmake` 等,含 `catch_discover_tests`)挂进搜索路径,供后续 `include(Catch)` | — |

变量名规则:`<depname>_SOURCE_DIR` 用**全小写** depname → 这里是 `catch2_SOURCE_DIR`。

下载落点:`build/_deps/<name>-src/`(源码)、`-build/`(中间产物)。二次配置不重拉。不污染源码树。

## 实证(2026-06-30,/tmp 真实拉取 —— 失败,但日志坐实机制)

- `[1] Declare 之前` 与 `[2] Declare 之后` 两条 message 都打印 → 证明 **Declare 阶段磁盘无任何下载**。
- `[22%] Performing download step (git clone) for 'catch2-populate'` + `Cloning into 'catch2-src'` → 下载由 FetchContent 内部生成的 `catch2-populate` 子项目驱动 `git clone`,目标即 `catch2-src/`。
- 失败后 `_deps/` 有 `catch2-build`/`catch2-subbuild` 但**无 `catch2-src`** → 反向证明 `catch2-src/` 才是源码位置。

## 坑 1(实证命中):网络拉不下来

失败日志:`Failed to connect to github.com port 443 after 133656 ms`(重试 3 次)、`SSL_read: unexpected eof`。WSL 访问 github 不稳定。

**救急**(handbook 给的):手动浅克隆后用预放置目录替换自动下载:
```bash
git clone --depth 1 -b v3.5.0 https://github.com/catchorg/Catch2.git /tmp/catch2
cmake -S . -B build -DFETCHCONTENT_SOURCE_DIR_CATCH2=/tmp/catch2
```
变量名规则:`FETCHCONTENT_SOURCE_DIR_<大写DEPNAME>`,本质是替换 `GIT_REPOSITORY` 的下载源。
