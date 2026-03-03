# 代码示例目录

本目录包含《现代嵌入式C++教程》的所有代码示例。

## 目录结构

```
codes_and_assets/
├── examples/              # 代码示例
│   ├── chapter02/          # 零开销抽象、内联、constexpr、CRTP
│   ├── chapter03/          # 初始化列表、移动语义、RVO/NRVO
│   ├── chapter05/          # 内存管理策略
│   ├── chapter06/          # 智能指针与 RAII
│   ├── chapter07/          # 容器与视图
│   ├── chapter08/          # 类型安全工具
│   └── chapter09/          # Lambda 与函数式编程
└── templates/             # 可复用的模板
    ├── cm_embedded.cmake  # 嵌入式 CMake 模板
    └── linker_script.ld   # 链接器脚本模板
```

## 如何添加新示例

每个示例应包含以下文件：

```
XX_topic_name/
├── README.md           # 示例说明
├── main.cpp            # 主程序（或按主题拆分多个 .cpp）
├── CMakeLists.txt      # 构建配置
└── tests/              # 测试（可选）
    └── test_main.cpp
```

### 命名规范

- 目录名：`序号_简短英文描述` (如 `01_raii_gpio/`)
- 文件名：小写下划线分隔 (如 `raii_gpio_example.cpp`)

### CMake 模板

```cmake
cmake_minimum_required(VERSION 3.20)
project(example_name CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")

add_executable(example main.cpp)

# 嵌入式常用选项（可选）
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-exceptions -fno-rtti")
```

## 编译示例

每个示例都可以独立编译：

```bash
cd examples/chapter02/01_zero_overhead
mkdir build && cd build
cmake ..
make
./example
```

## 嵌入式注意事项

1. **C++ 标准**：所有示例使用 C++20
2. **编译器**：GCC 10+ 或 Clang 10+
3. **异常处理**：部分示例禁用异常（嵌入式常见）
4. **RTTI**：部分示例禁用 RTTI
5. **目标平台**：示例代码假设交叉编译环境

## 代码规范

1. **可编译**：所有示例必须能独立编译
2. **注释清晰**：中文注释说明关键点
3. **C++20 特性**：充分利用现代 C++ 特性
4. **嵌入式考虑**：标注是否适用裸机/Linux
5. **类型安全**：优先使用强类型和抽象

## 许可证

本教程代码示例遵循项目主许可证。
