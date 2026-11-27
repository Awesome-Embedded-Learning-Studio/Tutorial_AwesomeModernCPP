---
id: "035"
title: "第三方库选型与领域重组大纲"
category: content
priority: P1
status: draft
created: 2026-11-26
assignee: charliechen
depends_on: []
blocks: []
estimated_effort: epic
---

# 第三方库选型与领域重组大纲

本大纲规划一件事：教程选进哪些第三方库，每个库用哪种讲法，放进哪一卷。选型看库的体量和维护状态。讲法分两种，体量小、结构清楚的库读源码，体量大的库讲架构加练 API。Qt 不在范围内，它的框架太大，原理讲不了，排除它的数据见「选型总表」。

方向是 2026-11-26 定的：计算机视觉、大模型、科学计算三个领域优先，先预备一到两个库。交叉语言要做。桌面 GUI 要做，挑轻量的。小型库里通用的都进，中型库选 spdlog 和 ETL。LVGL 由教程自己做 C++ 封装，顺带讲清 MCU 上的 GUI 是怎么跑起来的。具体选哪个库、放哪一卷，是本大纲的建议，都收在「待定事项」里，审完再落到各卷的 TODO。

证据来自 2026-11-25 到 11-26 的调查。翻了仓库里现有的提及和各子域的占位情况。读了各库的官方发布说明、README 和 OpenCV 的官方迁移指南。把候选库逐个克隆，数了源文件的个数和字节数，查了最近一次提交的日期。用 arm-none-eabi-g++ 实测了 Eigen 能不能编到 Cortex-M3 和 Cortex-M4。下面要引用的 OpenCV、ggml、LVGL 的结构和行号，都对着源码核过。

本文件是 2026-11-26 的第二版。和当天上一版相比，规模全部重数了，上一版有一批数偏小。OpenCV 一节改成以官方说明为准，上一版依据的那篇第三方文章有两处和官方对不上。Eigen 在卷四的位置从 T25 之后改到第三阶段末。ggml 明确分卷八和卷九两处。新增待定事项第 6、7 条。

## 数据口径

- 规模数的是源文件（`.h`、`.hpp`、`.cpp`、`.c`、`.cu`、`.inl` 等）的个数和字节数，排掉了测试、示例、文档、第三方这类目录。2026-11-26 全部按同一规则重数了一遍：每个库克隆默认分支，用 `git ls-tree -r -l` 取每个文件的字节数，Eigen 用 5.0.1 的源码包。单位是十进制的 MB。几个库的统计范围有取舍，写在总表下面的备注里。
- 上一版有一部分规模数取自一个包管理镜像的文件列表，那份列表不全，数出来偏小得多，ggml 那次数成 326 个文件 5.7MB。现在表里的数都是直接克隆仓库数的。
- 最近提交是默认分支上 `git log -1` 的作者日期，查于 2026-11-26。Eigen 的默认分支在 GitLab。
- 规模只反映体量。nlohmann/json 的单头发行版一个文件就有 4.9 万行，表里数的是它拆成多文件的那一版。

## Current Assets

- 第三方库在中文文档里都是顺带提及，没有专门的文章，也没有专门的子域。按整词匹配的文件数：Boost 47、Qt 27、Asio 19、fmt 16、vcpkg 8、Conan 3、spdlog 3、Eigen 4。LVGL、pybind11、nanobind、glaze、PEGTL 零提及。
- 包管理只在卷七 CMake 基础的 `03-find-package-and-cxx-standard.md` 讲了 `find_package`。`community/incoming/` 有一篇来稿 `why-cpp-package-manager-hard.md`。
- 卷八各子域的篇数：嵌入式 83、系统编程 57、AI 14（全部是 tiny_ml）、指针语义深潜 8、数据结构 6、网络 6。算法、数据存储、GUI 三个子域各只有一个占位的 index。
- 卷八 GUI 子域的 index 写着「重点是最小 GUI 框架设计而非 Qt 等成熟框架」，预计 12 篇，状态是规划中。`todo/017` 第 391 行起给它排的是 XCB 渲染基础，第 403 行的规划里写着要与 Qt、ImGui 对比。
- 卷八嵌入式的配套代码里有 ETL 的示例工程，在 `code/examples/chapter08/10_etl/`，三个 demo 文件。ETL 的旧文章 `embedded/05-etl.md` 已在 2026-08-24 随嵌入式教程的重建删除。
- 卷八 f103 主线用的板子是 Blue Pill 上的 STM32F103C8T6，环境搭建篇写的是 64KB Flash、20KB SRAM。工具链文件 `code/templates/cm_embedded.cmake` 里开着 `-fno-exceptions -fno-rtti`。仓库里的链接脚本模板 `code/templates/linker_script.ld` 写的是 256KB Flash、64KB RAM，ZerOS 的启动篇在讲它。
- `todo/017` 第 51 行已经给嵌入式 GUI 留了 `embedded-gui/` 子域，状态是待创建，第 418 到 432 行是它的规划。
- 卷九只有 Chromium 一条线，下面已有四个系列（OnceCallback、WeakPtr、flat_map、no_destructor）。`todo/018` 第 99 行起的候选表里已经有 spdlog、ETL、nlohmann/json、fmt。

## Gaps

- 没有地方讲怎么把一个第三方库引进工程。FetchContent、CPM、vcpkg、Conan 各有提及，没有一篇把它们放在一起讲清楚什么时候用哪个。
- 小库没有样本。卷九的定位是读源码学设计，但 Chromium 的体量读者上手不了，中间缺几个一个下午能读完的库。
- 大模型推理只有 tiny_ml 这个手搓的玩具，没有工业级的对照。
- 桌面 GUI 和嵌入式 GUI 都是空位。
- `todo/017` 第 35 行和第 81 行把交叉语言写成不再规划。

## Scope

- 在范围内：「选型总表」里 19 个库的去留和归位，卷八新增子域的设立，对 `todo/013`、`016`、`017`、`018` 的改动建议。
- 不在范围内：Qt。Boost 提及 47 处，这轮不单独讲。游戏中间件（SDL3、EnTT）、机器人（ROS 2）、密码学（Botan、OpenSSL 3）、脚本引擎（QuickJS）这几类调查时列过，这轮不选。
- 各库的文章怎么写不在本大纲，审完后分到各卷的 TODO。
- 各卷 TODO 文件自身和仓库现状的出入，不在本大纲里逐条修。只记会碰到本大纲改动的那些，见「对现有 TODO 的改动」。
- C++26 和 CppCon 2026 的内容更新不在本大纲，要点收在「范围外」，各有归属。

## 选型总表

按体量从小到大排。

| 库 | 规模 | 领域 | 最近提交 | 处理 |
|---|---|---|---|---|
| scope_guard | 1 文件，420 行 | 通用小库 | 2026-11-15 | 进，已定 |
| tl::expected | 1 文件，95KB | 通用小库 | 2025-09-01 | 进，已定 |
| magic_enum | 10 文件，0.2MB | 通用小库 | 2026-11-25 | 进，已定 |
| CLI11 | 29 文件，0.5MB | 通用小库 | 2026-11-24 | 进，已定 |
| fmt | 20 文件，0.7MB | 通用小库 | 2026-11-24 | 进，已定 |
| nanobind | 76 文件，0.9MB | 交叉语言 | 2026-11-01 | 进，已定 |
| spdlog | 115 文件，1.0MB | 中型通用 | 2026-11-25 | 进，已定 |
| sol2 | 117 文件，1.1MB | 交叉语言 | 2025-03-07 | 进，已定，注意 20 个月没提交 |
| Nuklear | 1 文件，1.2MB | 桌面 GUI | 2026-11-26 | 备选 |
| pybind11 | 73 文件，1.2MB | 交叉语言 | 2026-11-24 | 进，已定 |
| nlohmann/json | 79 文件，2.3MB | 通用小库 | 2026-11-23 | 进，已定 |
| Nana | 217 文件，2.6MB | 桌面 GUI | 2026-11-04 | 备选 |
| Dear ImGui | 11 文件，4.0MB | 桌面 GUI | 2026-11-24 | 建议选它 |
| llama.cpp | 356 文件，7.1MB | 大模型 | 2026-11-26 | 优先 |
| Eigen | 361 文件，7.1MB | 科学计算 | 2026-11-22 | 优先，建议先预备 |
| ETL | 443 文件，7.7MB | 中型通用，嵌入式 | 2026-11-20 | 进，已定 |
| ggml | 990 文件，22.5MB | 大模型 | 2026-11-26 | 优先，建议先预备 |
| LVGL | 917 文件，23.4MB | 嵌入式 GUI | 2026-11-25 | 进，已定，做 C++ 封装 |
| OpenCV 5.x | 1701 文件，34.4MB | 计算机视觉 | 2026-11-26 | 优先 |
| QtBase | 4069 文件，77.7MB | GUI | 2026-11-26 | 排除，已定 |

备注：

- scope_guard 数的是 Neargye 的那个仓库。
- Dear ImGui 只数了仓库根目录的核心文件，`backends/` 和 `misc/` 没算。
- ggml 数的是 `src/` 整个目录，含各后端。其中 CPU 后端约 4.5MB，CUDA、SYCL、OpenCL 各 2MB 上下，Metal 和 Vulkan 各 1.5MB 上下。核心部分是 `src/` 顶层的源文件加 `include/` 的头文件，三万二千多行。llama.cpp 数的是 `src/` 和 `common/`，没含 ggml 子模块，两者相加约 29.6MB。
- Eigen 只数 `Eigen/`。`unsupported/Eigen` 另有 168 个文件、约 2.3MB。
- LVGL 的 `src/` 含驱动、字体和内嵌的 thorvg。
- Nuklear 只有 `nuklear.h` 一个文件，约 3.2 万行。
- OpenCV 数的是 `modules/`。
- QtBase 数的是 `src/`，是 Dear ImGui 核心的近 20 倍。

「已定」是 2026-11-26 定的方向。「建议」和「备选」等审。

## 按领域重组

### 通用小库

scope_guard、tl::expected、magic_enum、CLI11、fmt、nlohmann/json 放卷九，当读源码的最小样本。

tl::expected 是 `std::expected` 的前身，fmt 是 `std::format` 的前身，读一遍能看清标准库那些设计从哪来。magic_enum 是 C++26 静态反射落地之前的枚举名反射，和卷四已有的 `06-static-reflection-basics.md` 对着读。

tl::expected 从 2025 年 9 月起没有提交。它的功能已经完整，对应的东西也进了 C++23，这是完成态。

它们和 `todo/018` 已有的候选表怎么并，见待定事项第 7 条。

### spdlog 与 ETL

spdlog 放卷七工程实践，日志是工程基础设施。它的 README 写明是纯头文件库，格式化基于 fmt，有可选的异步模式，加一个日志目标只需要实现 sink 接口里的一个函数。讲 sink 接口、格式化和异步模式，讲 sink 的时候顺带读 `include/spdlog/sinks/sink.h` 和 `logger.h`。

ETL 放卷八嵌入式，接现有的内存管理那几篇，从 `10_etl/` 那三个 demo 新写。它的 README 说整库不碰堆，所有非侵入式容器的容量在编译期定死，最低支持 C++03，CI 尽可能覆盖 C++11 到 C++26。讲无堆分配的容器怎么实现，和 C++26 的 `std::inplace_vector` 对照着读。

### Eigen

Eigen 5.0.0 发布于 2025-09-30（官方 CHANGELOG）。5.0 是最后一个支持 C++14 的大版本，之后的版本要求 C++17。拿 C++11 去编，`Macros.h` 第 749 行直接报 `#error Eigen requires at least c++14 support.`。版本号从这一版起改成语义化版本。LGPL 许可的代码全部移除。

能不能上 MCU 做了实测。工具是 arm-none-eabi-g++ 16.2.0，目标是 Cortex-M3（软浮点）和 Cortex-M4（`-mfpu=fpv4-sp-d16 -mfloat-abi=hard`），都带 `-Os -fno-exceptions -fno-rtti -Wall -Wextra`，异常开关和卷八工具链文件里的一致。结果如下：

- 固定大小的 `Matrix3f`、`Vector3f`，做乘法、求逆、行列式。C++14 和 C++17 都编过，零警告，对象文件 7.2KB 到 8.0KB。M3 多出几个 `__aeabi_f*` 软浮点符号，其余只依赖 `__assert_func` 和 `memset`，不碰堆。
- 动态大小的 `MatrixXf` 加 `partialPivLu().solve()`。也编过，对象文件 78KB 到 82KB，未定义符号里有 `operator new`、`malloc`、`free` 和 `__cxa_guard_*`，也就是要用堆。
- 对照组不关异常和 RTTI（Cortex-M4，C++17）。固定大小的对象文件多约 2KB，多出一个栈展开符号。动态大小的多 22KB，多出 `__cxa_throw`、`std::bad_alloc`、`__gxx_personality_v0`。

所以嵌入式上用固定大小矩阵，关掉异常和 RTTI 照样能编。Eigen 只在定义了 `__ARM_NEON` 时才开 NEON 向量化（`ConfigureVectorization.h` 第 395 行），Cortex-M3 和 M4 没有 NEON，不用额外去关。

建议把它当卷四模板线里的一个案例，放第三阶段末，T19（Type Erasure）和 T21（Tag Dispatch 与 if constexpr 分派）之后，`todo/013` 第 184 到 189 行的综合项目表里加一行。不放在 T25 后面。T25 讲的是模板编程的未来，Eigen 是现在的工业库。表达式模板用到的工具，T07 和 T08 的包展开与折叠表达式、T11 的 `if constexpr`、T13 的 `constexpr`、T14 到 T16 的 Concepts，第二阶段就都齐了。读者读完这些再看 Eigen 的 `MatrixBase` 和表达式类，能看见这些工具凑在一起是什么样。

卷四的结构本身在动。013 写着四个子目录「0 篇正文」，实际子目录里已经有 48 篇文章在站上（共 52 个 md，4 个是 index）。其中 `10-fixed-vector.md`、`05-type-safe-any.md` 正是 013 列为待做的两个综合项目，`06-static-reflection-basics.md` 和 T24 重叠。Eigen 加进去之前，这批文章的去留要先定。

### ggml 与 llama.cpp

tiny_ml 是手搓的玩具推理器，ggml 是同一件事的工业级版本，对照着讲。讲推理原理的篇目放卷八 AI 子域，接 tiny_ml。读源码的部分放卷九，当 Chromium 之后的第二条大线。

ggml 有独立仓库，但它的 README 写明核心改动要提到 llama.cpp 去，那边才是开发的主仓库。读源码以 llama.cpp 仓库里的 `ggml/` 为准。

ggml 是 C 写的。README 的自我介绍是零依赖、跨平台、带 SIMD 内核、支持 CPU、GPU、NPU 和浏览器后端、运行期不做内存分配。整个 `src/` 是 22.5MB，大部分是各后端。核心是 `src/` 顶层的源文件加 `include/` 的头文件，三万二千多行。切入的顺序和位置如下，行号是 2026-11-26 的 master：

1. `include/ggml.h`（2862 行）。`enum ggml_type` 在第 218 行，`enum ggml_op` 在第 324 行，`struct ggml_init_params` 在第 511 行，`struct ggml_tensor` 在第 519 行，`ggml_init` 和 `ggml_new_tensor` 的声明在第 655 行起。这一步认识张量和内存池的接口。
2. `src/ggml.c`（8257 行）。`struct ggml_object` 在第 975 行，`struct ggml_context` 在第 992 行，`ggml_init` 的实现在第 1627 行，`ggml_new_tensor` 在第 1879 行。看一块连续的 buffer 怎么被切成对象。`struct ggml_cgraph` 的定义在 `src/ggml-impl.h`（862 行）第 348 行。
3. `include/ggml-alloc.h`（87 行）和 `src/ggml-alloc.c`（1171 行）。计算图里的张量内存怎么分配。
4. `include/ggml-backend.h`（480 行）和 `src/ggml-backend.cpp`（3074 行）。后端接口，以及 `ggml_backend_sched` 怎么把一张图分给多个后端。
5. `src/ggml-quants.c`（5772 行），对照 `enum ggml_type` 里的 Q4_0、Q8_0、K 系列、IQ 系列和 MXFP4。量化格式。
6. `include/gguf.h`（211 行）和 `src/gguf.cpp`（1742 行）。模型文件格式，说明在 `docs/gguf.md`。
7. 后端先读 `src/ggml-cpu/ggml-cpu.c`（3988 行）。

入门示例用仓库自带的 `examples/simple`，有 `simple-ctx.cpp` 和 `simple-backend.cpp` 两个。

llama.cpp 是 C++ 写的。`src/` 里以 `llama-` 开头的源文件有 63 个，模型加载在 `llama-model-loader.cpp`，词表在 `llama-vocab.cpp`，KV cache 在 `llama-kv-cache.cpp` 和它的几个变体里，语法约束在 `llama-grammar.cpp`。采样在 `common/sampling.cpp`。`src/` 加 `common/` 是 7.1MB。先读 ggml，再读 llama.cpp。

### OpenCV

当前稳定版是 5.1.0，4.x 在 Homebrew 里已标成弃用（查于 2026-11-26）。`5.x` 分支头文件里的版本宏已经是 5.2.0-dev。

5.0 发布于 2026 年 6 月。下面的变化取自官方的发布说明和迁移指南（`opencv/opencv` 的 wiki），并且对着 5.x 分支的源码核过：

- 最低标准是 C++17，默认也按 C++17 构建。`cmake/OpenCVDetectCXXCompiler.cmake` 第 207 到 209 行设了 `CMAKE_CXX_STANDARD 17`，并且关掉了 GNU 扩展。编译器最低版本是 GCC 8、Clang 9、MSVC 2017。Python 2 不再支持。
- 1.x 时代的 C API 全部删除，`core_c.h`、`types_c.h`、`imgproc_c.h` 都没了。`CV_8U`、`CV_32F` 这些宏还在。
- 许可证是 Apache 2.0（仓库根目录的 `LICENSE`）。
- OpenVX 的支持删了。G-API、经典的 ML 模块、Haar 和 HOG 检测器都移到了 `opencv_contrib`。约一半的 C++ 示例被删。
- 模块重组。`calib3d` 拆成 `geometry`、`calib`、`stereo`、`ptcloud` 四个，`features2d` 改名 `features`。`opencv2/calib3d.hpp` 这个头文件还在，为了向后兼容，C++ 这边不用改 include。
- core 里加了 `CV_16BF`、`CV_32U`、`CV_64U`、`CV_64S`、`CV_Bool` 五种类型。`MatShape` 取代 `MatSize`，直接嵌在 `Mat` 里，不做额外的动态分配。`std::vector` 包成 `Mat` 现在是真正的一维数组。
- DNN 换了新的推理引擎，和经典引擎并存。官方给的数字是覆盖 ONNX 规范的 64% 以上，4.x 不到 13%。新引擎带分词器、注意力层、KV cache，能端到端跑视觉语言模型，示例在 `samples/dnn/vlm_inference.py`。新引擎目前只在 CPU 上跑，GPU 要用经典引擎，或者构建时链进 ONNX Runtime。Darknet 和 Caffe 的解析器删了。
- 图像变形（`warpAffine`、`warpPerspective`、`remap`）重写了插值，官方说提速 10% 到 300% 以上。`findContours` 换成了更快的 TRUCO 算法。文字渲染换成了基于 TrueType 的引擎，旧的 `FONT_HERSHEY_*` 还能用，外观变了。
- USAC 成了所有鲁棒估计的默认后端。

HAL 目录下有 `riscv-rvv`（core、dnn、features2d、imgproc 四组）和 `carotene` 等，universal intrinsics 里有 16 位浮点类型 `v_float16`。

有两处要留意。一是 DNN 引擎选择的 API 还在变。5.0 的迁移指南写的是 `ENGINE_CLASSIC`，5.2.0-dev 的 `dnn.hpp` 里是 `ENGINE_AUTO`、`ENGINE_OPENCV`、`ENGINE_ORT`。写这一段时按当时的版本核。二是迁移指南里「Running VLMs」一节还标着待补。

上一版依据的那篇第三方作者的文章，有两处和官方对不上。ONNX 覆盖率它写 80% 以上，官方是 64% 以上。引擎枚举它写的是 `ENGINE_NEW` 和 `ENGINE_LEGACY`，官方的发布说明和头文件里都没有这两个名字。本大纲不再引它。

这个体量不适合读源码，用讲架构加练 API 的讲法，放卷八新建的计算机视觉子域。切入点是 `cv::Mat` 的内存模型（`UMatData` 里的引用计数加数据布局，`mat.hpp` 第 705 到 732 行、`addref()` 在第 1869 行）、`Ptr<T>`、HAL 抽象层（同一套 API 下面按平台换 SIMD 实现）。

它和 Eigen 有交集。`WITH_EIGEN` 打开时，geometry 模块的 USAC 求解器（`usac/` 下的 essential、homography、fundamental、dls、pnp 五个 solver）、`sqpnp.cpp` 和 core 的 `lapack.cpp` 会用 Eigen。这个选项默认在非交叉编译时才开，交叉编译要手动打开。两条线可以互链。

### 桌面 GUI

候选有 Dear ImGui、Nana、Nuklear。

Dear ImGui 的 README 把自己叫「bloat-free graphical user interface library for C++」。讲原理的那一节说，它尽量不让用户去保存和同步重复的界面状态，这比传统的保留模式界面更不容易出错。它输出顶点缓冲和绘制命令列表，不直接碰图形状态，渲染由应用自己做。核心只有 11 个文件，4.0MB。README 的 Gallery 里列了 Tracy（性能分析器）、ImHex（十六进制编辑器）、RemedyBG（调试器）这些用它做的项目。2026-10-15 的 master 构建是 1.93.0。

建议选它。原理集中在「界面每帧由应用代码重新提交，库只产出绘制数据」这一件事上，不用先讲保留模式那套对象模型。

它的代价是用 C++ 写的，风格偏 C，当不了现代 C++ 的范例。

更看重现代 C++ 风格的话，Nana 可以换上来。它的 README 自称「C++ standard-like GUI library」，用法是创建 `form` 对象、`show()`、进 `nana::exec()` 的事件循环，属于保留模式。它的 `master` 分支现在是 1.8 的预发布，要 C++17，上一个版本 1.7 在 `nana-1.7` 分支。2026-11-04 还有提交。

Nuklear 是单头的 C89 库，公共领域许可，也是即时模式，README 说它连标准库都可以不依赖。

`todo/017` 给桌面 GUI 这个子域排的是 XCB 渲染基础（阶段 A，里程碑是 MiniDraw 渲染库），接着是六篇保留模式的核心概念（阶段 B，第 401 到 408 行），手搓 MiniGUI 另立在项目页。建议把 Dear ImGui 插在阶段 A 和阶段 B 之间：先在 XCB 上有了一个窗口和一套绘制，再用 ImGui 讲它怎么在窗口里画界面，阶段 B 顺延，手搓框架留给项目页。

### 嵌入式 GUI：LVGL 与 C++ 封装

LVGL 是纯 C 写的，MIT 许可，当前版本 9.6.0。官方没有 C++ 封装，`src/` 下 67 个 `.cpp` 文件都属于内嵌的 thorvg 矢量图形引擎。第三方的 lvglpp 在 2026-10-11 还有提交，但它基于 LVGL 8.3。8.x 的类型名在 9.6 的源码里已经一个不剩：`lv_btn_t`、`lv_disp_t`、`lv_indev_drv_t`、`lv_img_t` 的出现次数都是 0，对应的是 `lv_button_t`、`lv_display_t`、`lv_indev_t`、`lv_image_t`。自己封装这条路没有现成的能照搬。

lvglpp 的 README 把封装要讲的问题都实际遇到了，可以直接当素材：

- 所有权。LVGL 自己记父子关系，删父对象时连子对象一起删。包装类析构时不能把还挂在树上的对象删掉。lvglpp 用 `owns_ptr` 标志和 `release_ptr()` 处理，底层是带自定义删除器的 `unique_ptr`。
- 回调。LVGL 的回调是 C 函数指针加 `void* user_data`。lvglpp 把 C++ 对象的指针存进 `user_data`，回调里再取回来。代价是占掉了 `user_data`，每次回调要重新包一层临时对象。
- 线程。官方文档在 `docs/src/integration/overview.mdx` 里写明 LVGL is not thread-safe，`lv_timer_handler()` 和别的线程里的 LVGL 调用要互斥。
- 开销。lvglpp 每个包装对象多 12 字节。

板子的事查过了。卷八 f103 主线用的是 Blue Pill 上的 STM32F103C8T6，环境搭建篇写的是 64KB Flash、20KB SRAM。LVGL 给了两套数。README 说最低 32KB RAM、128KB Flash，典型界面约 100KB RAM、200 到 300KB Flash。`docs/src/introduction/requirements.mdx` 说最核心的组件 64KB Flash 就够，推荐 180KB 以上，栈 2KB 以上、推荐 8KB，堆 2KB 以上、用很多小部件时推荐 48KB。C8T6 按第一套数差得远。按第二套数 Flash 只是擦着最低线，20KB 的 SRAM 要装下栈、堆，还要留出显示缓冲，余量很小。`lv_conf_template.h` 里 `LV_MEM_SIZE` 的默认值是 65536，LVGL 默认给自己开 64KB 的内存池。所以 C8T6 不适合跑 LVGL。

板端那步不能落在 C8T6 上，f407 的线还空着。LVGL 官方的 STM32 接入指南（`docs/src/integration/chip_vendors/stm32/`）说，板上的 LCD 由 LTDC 驱动就用它的 LTDC 驱动，没有就按 README 里手动移植的写法，自己提供 flush 回调和触摸读取回调。所以板端不卡在有没有 LTDC 上。f103 那条线已经是 Renode 模拟器先行。LVGL 在 PC 上走 SDL 模拟器，做法上是一致的，它自带 SDL、X11、Wayland 驱动。

和 `todo/017` 原有规划（第 418 到 432 行）的对应关系：阶段 A 的三篇（OLED 驱动、TFT LCD 驱动、触摸输入）留着，正好是 LVGL 要的显示和输入驱动层，对应它的 flush 回调和 indev read 回调。阶段 B 的 04（轻量 Widget 系统）和 05（布局与事件分发）就是本大纲建议换成 LVGL 封装的部分。06 的仪表盘 Demo 留着，当封装的验收。

### 交叉语言

pybind11、nanobind、sol2 都进，放卷八新建的语言互操作子域。

pybind11 的 README 自称轻量的纯头文件库，在 C++ 和 Python 两个方向上暴露类型，master 已经是 3.x。nanobind 自称「tiny and efficient」，语法和 pybind11 几乎一样，号称开销更低。sol2 是纯头文件的 Lua 绑定，支持 Lua 5.1 以后的所有版本，含 LuaJIT。

讲法从 C ABI 开始：`extern "C"`、不透明指针、回调函数指针、用返回值传错误码。这是所有绑定的地基，和编译与链接卷直接接上。然后讲 pybind11 怎么在这之上把 C++ 类暴露给 Python，nanobind 是同一件事的另一种做法。sol2 换了目标语言，原理一样。

sol2 从 2025 年 3 月起没有提交，写它的时候要标明版本。

## 待定事项

1. **先预备哪两个库。** 建议 Eigen 和 ggml。Eigen 7.1MB 纯头文件，ggml 的核心三万多行、其余是后端，两个都零外部依赖，各自接得上现有的线。OpenCV 排第三，体量是 Eigen 的近五倍，5.0 发布才半年，DNN 引擎选择的 API 还在变。
2. **桌面 GUI 选哪个。** 建议 Dear ImGui，Nana 备选。
3. **交叉语言是否重启。** 建议重启。`todo/017` 第 35 行和第 81 行要相应改掉。
4. **LVGL 的开发环境，和 embedded-gui 子域原有的规划怎么并。** C8T6 跑不了 LVGL，f407 的线还空着。建议先在 PC 模拟器上讲封装，封装写完再往板端搬，板端那步等 f407 的线起来。`todo/017` 第 78 行和第 418 到 432 行给 embedded-gui 排的是自己写轻量 Widget 框架，和给 LVGL 做 C++ 封装是两条路。建议阶段 A 的显示和触摸三篇留着当驱动层，阶段 B 的 04、05 换成 LVGL 封装，06 的仪表盘留着当验收，手搓 Widget 框架并进项目页的手搓 MiniGUI。
5. **卷八新增子域。** 建议在卷八加科学计算、计算机视觉、语言互操作这几个子域，嵌入式 GUI 沿用 `todo/017` 已有的 embedded-gui 这一条。桌面 GUI 留在现有的 gui-graphics。
6. **Eigen 在卷四放哪。** 建议当第三阶段末的案例，T19、T21 之后，不挂在 T25 后面，理由见 Eigen 一节。前提是卷四四个子目录里已有的 48 篇先定去留。
7. **小库放卷九，和 `todo/018` 已有的候选表怎么并。** 018 第 99 行起的候选表和本大纲有三处不一样。spdlog 在第一梯队（第 107 行），还排在第一批的第 3 项（第 143 行），本大纲放卷七。ETL 在第三批（第 153 行），本大纲放卷八。nlohmann/json 是补充候选，理由是教学增量不如第一梯队（第 117 行），本大纲把它列进通用小库。建议小库以本大纲的总表为准，018 的候选表改成引用它。spdlog 和 ETL 各留一处，用法篇在卷七和卷八，卷九不再单列。另外 018 要求每个项目走 full 加 hands_on 双路径（第 21、161 行），每个主题产出五样东西（第 159 行），对 420 行的 scope_guard 太重。建议给小库开一条轻量格式，一篇读源码的笔记加一个能编译的最小示例，双路径留给 Chromium 和 ggml。总表说小库排在 Chromium 之前，指的是读者的阅读顺序，018 自己的生产顺序是另一回事。
8. **优先级。** frontmatter 里的 P1 是建议值。

## 对现有 TODO 的改动

审完之后执行，现在不动。只列和本大纲直接相关的。这几份文件里别的和仓库现状对不上的地方，改的时候会碰到，一并记下。

- `todo/017-vol8-domains.md`
  - 第 35 行和第 81 行的 interop 改成重启。领域体系表（第 43 到 53 行）加一行，后面加一节。这张表还漏了已有的 `ai/` 和 `data-structure/`，列着的 `design-patterns/` 和 `embedded-gui/` 目录还不存在，一起改。
  - gui-graphics（第 391 到 408 行）：阶段 A 和阶段 B 之间插 Dear ImGui。
  - embedded-gui（第 418 到 432 行）：按待定事项第 4 条改。第 425 行的 TFT LCD 那篇写的是 LTDC、FSMC 和 DMA2D，这几个外设哪款芯片才有，本大纲没核。
  - 新增科学计算、计算机视觉两个子域的条目。嵌入式 GUI 沿用 embedded-gui，语言互操作算 interop 那一行。
  - ETL：第 201 行「保留为独立参考」改成新写，D1 的口径跟着改，嵌入式路线的站点表里给 ETL 一个位置。
  - 这份文件的嵌入式远期规划路线（第 100 到 193 行）描述的还是老版外设链。f103 现在是八个站（env-setup、led、button、uart、time、i2c、patterns、f103-to-f407），Renode 模拟器先行，在 HAL 之上写应用层。ZerOS 那条线已经有 39 个 md，约 1.1 万行，文件里一个字没提。改它的时候绕不开这些。
- `todo/013-vol4-advanced-topics.md`：综合项目表（第 184 到 189 行）加一行 Eigen 表达式模板的案例，第三阶段末。前提是四个子目录里已有的 48 篇先定去留，013 现在写着它们是「0 篇正文」。
- `todo/016-vol7-engineering.md`：加 spdlog。包管理不用另起一篇，第 127 行和第 83 行已经规划了两篇，把 CPS 的现状更新进去，第 178 行那条待核验销掉。
- `todo/018-vol9-open-source-study.md`：小库按总表进候选池，开一条轻量格式，ggml 当第二条大线，spdlog 和 ETL 不再单列。它的 Current Assets（第 23 行起）只写了 OnceCallback，实际已经有四个系列、62 个 md，改的时候一起更新。
- `todo/README.md`：文件列表补上 `032` 到 `035`。状态图例里没有 draft。旧 TODO 映射里有三个去向指向不存在的文件（第 69、73、83 行的 `020-compilation-and-reference.md` 和 `030-community.md`），顺手修。

## 顺带查到的三件事

这三件不属于选型，调查时看到的，各有归属。

- CMake 4.3 把 CPS（Common Package Specification，一份 JSON 描述一个包，让不同的构建系统和包管理器读同一种格式）从实验性转成正式支持，Kitware 的公告发在 2026-03-19。`install(PACKAGE_INFO)` 导出，`find_package` 直接读 `.cps` 文件，传递依赖的处理写进了规范，target 的命名空间强制等于包名。限制是不支持 generator expression，规范本身还是 0.14 的预发布。Conan 2.34 的 changelog 里至少八条提到 CPS，`CMakeConfigDeps` 支持 CPS 和 CMake 之间的来回转换、CPS 动态库，以及 CMake 4.4 起的 `dyld_requires`，这个生成器的状态是 experimental。vcpkg 这次没查到相关说明。`todo/016` 已经在两篇规划的文章里提到 CPS（第 83、127 行），第 178 行把它的状态列为待核验，上面这些就是核过的结果。
- Boost 1.92 给大量库加了 C++20 modules，`find_package` 开始支持 header-only 组件。归卷四和卷七的 Modules 内容。
- vcpkg 在 2026 年 5 月的版本里移除了 cppcoro。卷五 `chapter07-coroutines/04-task-type.md` 第 347 行和 `cookbook/b-topics/09-channel-csp.md` 第 780 行把它当延伸参考，不用删，补一句它长期没维护。

## 范围外

C++26 和 CppCon 2026 的调查是同一次做的，不在这个大纲里，要点和归属记在这里，免得丢。

- `documents/cpp-reference/index.md` 第 184 到 199 行的 C++26 段落过时。C++26 已于 2026-03-28 定稿，段落还写着正在制定中，表格只列了 6 项。归参考卡的维护。
- 卷六 `04-05-simd.md` 第 111 到 113 行的展望写的是 `<experimental/simd>`，GCC 16 的 libstdc++ 里已经有 `std::simd`，cppreference 的支持表标的是部分实现。
- `todo/019` 候选池里 Contracts 和 Reflection 两场标着的 needs standard-status verification 可以摘掉。
- `todo/013` 的 T23 到 T25 前置条件齐了，GCC 16.1 实现了 Pack Indexing、Reflection、Contracts 和 Expansion statements。`#embed` 在仓库里零提及。卷四四个子目录里已经有一篇 `06-static-reflection-basics.md`，和 T24 重叠。
- `std::execution`、`<hazard_pointer>`、`<rcu>`、`<linalg>` 进了标准。cppreference 的 C++26 支持表里这四项的编译器列是空的，GCC 16 的发布说明也没提。查的是镜像站，可能滞后。参考卡的 C++26 段落写着 GCC 16 对 linalg 有部分实现，和这张表对不上，改参考卡时一起核。
- Profiles 冲着 C++29 去，P2786 已经从 C++26 移除，这两样不写。
- GCC 16 的默认标准从 gnu++17 改成 gnu++20，`code/` 下 190 个 CMakeLists 有 29 个没写 `CXX_STANDARD`。归卷七和各卷代码的维护。
- CppCon 2026 的笔记候选，材料在 github.com/CppCon/CppCon2026，归 `todo/019`。

## Acceptance

- [x] 调查项收尾（2026-11-26）。规模全部按同一口径重数。OpenCV 5.0 的变化对着官方发布说明、迁移指南和 5.x 源码核过。C8T6 的内存和 LVGL 的官方需求对过。Eigen 用 arm-none-eabi-g++ 在 Cortex-M3 和 Cortex-M4 上实测编过。ggml 的切入位置对着源码定了。Conan 对 CPS 的支持在 changelog 里核过。LVGL 的版本、类型改名、线程说明查了官方仓库和文档。
- [ ] 「待定事项」逐条有定论，本文件从 draft 转成 active，或者内容并回各卷的 TODO 之后关闭
- [ ] 按定论改 `todo/017`、`todo/013`、`todo/016`、`todo/018`
- [ ] `todo/README.md` 的文件列表补上 `032` 到 `035`
- [ ] 写 OpenCV DNN 引擎选择那一段时，按当时的版本核枚举名
- [ ] 给 LVGL 定板端型号时，查那个型号的数据手册
