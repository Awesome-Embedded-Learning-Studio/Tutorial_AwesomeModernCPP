# 卷五练习手册 · 工程脚手架

vol5 并发练习手册的可运行代码工程。每个 Lab 有两份:

- **`templates/<lab>/`** — 空实现骨架(声明 + TODO),给初学者**拷贝**去做练习。
- **`examples/<lab>/`** — 参考实现(完整),供卡住时对照;顶层 CMake 编译它做验证。

两份各自是 **standalone 工程**(自己的 CMakeLists + Catch2),能独立打开/构建,IDE/clangd 友好。

## 目录结构

```text
vol5-labs/
├── CMakeLists.txt                          # 顶层:FetchContent Catch2 + 编译 examples/ 做验证
├── README.md                               # 本文件
├── templates/
│   └── lab0_thread_lifecycle/              # 模板(空实现,初学者拷贝)
│       ├── CMakeLists.txt                  #   standalone(FetchContent + Catch2)
│       ├── include/lab0/                   #   ← 你拷贝后在这里补全实现
│       │   ├── file_info.h                 #      数据结构(已给全,不用改)
│       │   ├── worker_stats.h              #      数据结构(已给全,不用改)
│       │   ├── joining_thread.h            #      Milestone 2 实现
│       │   └── file_scanner.h              #      Milestone 1/3/4 实现
│       └── test/                           #   教程提供的测试(不用改,除非补边界测试)
└── examples/
    └── lab0_thread_lifecycle/              # 参考实现(完整,被顶层编译验证)
        ├── CMakeLists.txt                  #   standalone
        ├── include/lab0/                   #   完整实现
        └── test/
```

依赖管理用 **FetchContent**(不再用 CPM),每个 standalone 工程自己拉 Catch2 v3.7.1,无需额外文件。

## 开始一个 Lab(初学者)

```bash
# 1. 拷贝模板去做(也可以直接在 templates/ 里改)
cp -r code/volumn_codes/vol5-labs/templates/lab0_thread_lifecycle /tmp/my-lab0
cd /tmp/my-lab0

# 2. 构建(首次 FetchContent 拉 Catch2,需联网)
cmake -B build -DCMAKE_BUILD_TYPE=Debug     # Debug 默认开 ThreadSanitizer
cmake --build build
```

第一次构建会停在链接阶段,报 `undefined reference to lab0::FileScanner::scan()` — 这是**故意的**:`file_scanner.h` / `joining_thread.h` 只有声明没有实现,链接器在提醒你"该动手了"。这就是 TDD 式练习的起点。

按对应 Lab 的 handbook(如 `documents/vol5-concurrency/exercises/00-thread-lifecycle.md`)的 Milestone 顺序实现 `include/lab0/*.h`,每完成一个 milestone 跑对应测试,变绿即通过。

## 跑测试

```bash
ctest --test-dir build --output-on-failure                # 全部
./build/test/test_milestone1                               # 单个 milestone
./build/test/test_milestone2 "[lab0][milestone2]"          # Catch2 标签过滤
```

> TSan 不需要额外参数 — Debug 构建已经在编译期通过 `-fsanitize=thread` 开启,直接运行测试即在 TSan 下。Release 构建不开 sanitizer。**注意:Catch2 没有 `--tsan` 这种运行参数,TSan 是编译期选项。**

## 卡住了?

打开 `examples/lab0_thread_lifecycle/` 看参考实现(作者的实现,不一定最优,欢迎 Issue/PR 改进)。但**先自己挣扎一会儿** — 直接抄参考实现会丢掉 dogfooding 的价值。

## 作者 / CI:一键验证参考实现

```bash
cd code/volumn_codes/vol5-labs
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

顶层编译 `examples/` 下所有参考实现。`templates/` 不编译(空实现,给初学者的)。

## dogfooding 反馈

你是这本手册的一号测试用户。**遇到任何卡点都记下来回报**,据此迭代 handbook 和测试。需要反馈的典型情况:指引不清 / 测试红得不明白 / 踩坑没预警到 / 命令跑不通 / 指引与代码矛盾。

```text
卡点位置:Milestone N / 文件 / 行
现象:……(报错信息 / 实际行为)
期望:……(你觉得应该怎样)
```
