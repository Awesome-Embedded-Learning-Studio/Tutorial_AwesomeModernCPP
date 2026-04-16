# 代码示例目录

本目录包含现代 C++ 教程的所有代码示例。

## 目录结构

```
code/
├── examples/              # 通用教程代码示例
├── stm32f1-tutorials/     # STM32F103C8T6 实战工程
│   ├── 0_start_our_tutorial/
│   └── 1_led_control/
├── templates/              # 可复用的模板
│   ├── cm_embedded.cmake
│   └── linker_script.ld
└── project-setup/          # 项目创建脚本
    └── create_tutorial.sh
```

## 如何添加新示例

每个示例应包含以下文件：

```
XX_topic_name/
├── README.md           # 示例说明
├── main.cpp            # 主程序
├── CMakeLists.txt      # 构建配置
└── tests/              # 测试（可选）
    └── test_main.cpp
```

### 命名规范

- 目录名：`序号_简短英文描述`（如 `01_unique_ptr/`）
- 文件名：小写下划线分隔（如 `unique_ptr_demo.cpp`）

### CMake 模板

```cmake
cmake_minimum_required(VERSION 3.20)
project(example_name CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(example main.cpp)
```

## 编译示例

每个示例都可以独立编译：

```bash
cd examples/01_unique_ptr
mkdir build && cd build
cmake ..
cmake --build .
./example
```

## 代码规范

1. 所有示例必须能独立编译
2. 中文注释说明关键点
3. 充分利用现代 C++ 特性
4. 标注适用的 C++ 标准

## 许可证

本教程代码示例遵循项目主许可证。
