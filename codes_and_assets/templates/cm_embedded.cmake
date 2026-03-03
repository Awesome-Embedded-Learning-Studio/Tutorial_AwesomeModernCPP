cmake_minimum_required(VERSION 3.20)
project(EmbeddedExample CXX)

# ==================== C++ 标准配置 ====================
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ==================== 编译选项 ====================
option(BUILD_SHARED_LIBS "Build shared libraries" OFF)
option(ENABLE_TESTS "Build tests" OFF)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")

# ==================== 嵌入式特定选项 ====================
option(ENABLE_EXCEPTIONS "Enable C++ exceptions" OFF)
option(ENABLE_RTTI "Enable RTTI" OFF)
option(ENABLE_LTO "Enable Link Time Optimization" OFF)

# 应用嵌入式选项
if(NOT ENABLE_EXCEPTIONS)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-exceptions")
endif()

if(NOT ENABLE_RTTI)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-rtti")
endif()

if(ENABLE_LTO)
    set(CMAKE_CXX_FLAGS "${CXX_FLAGS} -flto")
    set(CMAKE_CXX_LINKER_FLAGS "${CMAKE_CXX_LINKER_FLAGS} -flto")
endif()

# ==================== 输出目录 ====================
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# ==================== 示例可执行文件 ====================
add_executable(example main.cpp)

# ==================== 测试支持 ====================
if(ENABLE_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ==================== 安装配置（可选）====================
install(TARGETS example DESTINATION bin)

# ==================== 打印配置信息 ====================
message(STATUS "")
message(STATUS "========================================")
message(STATUS "  Embedded Example Configuration")
message(STATUS "========================================")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Exceptions: ${ENABLE_EXCEPTIONS}")
message(STATUS "RTTI: ${ENABLE_RTTI}")
message(STATUS "LTO: ${ENABLE_LTO}")
message(STATUS "========================================")
message(STATUS "")
