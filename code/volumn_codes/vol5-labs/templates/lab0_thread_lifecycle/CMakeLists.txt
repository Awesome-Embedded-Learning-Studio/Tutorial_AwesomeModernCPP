cmake_minimum_required(VERSION 3.20)

# 本工程是 standalone:可以 cd 进来独立构建。
#   cmake -B build -DCMAKE_BUILD_TYPE=Debug
#   cmake --build build
# 被 vol5-labs 顶层 add_subdirectory 包含时也能工作(guard 跳过重复 FetchContent/enable_testing)。
project(lab0_thread_lifecycle LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 只在 standalone 构建时拉 Catch2 + 开 testing;
# 被顶层 add_subdirectory 包含时由顶层负责(复用顶层的 Catch2)。
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    include(FetchContent)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.7.1
    )
    FetchContent_MakeAvailable(Catch2)
    enable_testing()
endif()

# lab0 是 header-only INTERFACE 库,实现写在 include/lab0/*.h:
#   - templates/ 下是空实现(声明+TODO),初学者拷贝去补全
#   - examples/  下是参考实现(完整)
add_library(lab0 INTERFACE)
target_include_directories(lab0 INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(lab0 INTERFACE cxx_std_17)

add_subdirectory(test)
