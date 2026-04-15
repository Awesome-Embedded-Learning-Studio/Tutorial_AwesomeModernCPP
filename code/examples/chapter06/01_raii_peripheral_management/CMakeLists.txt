cmake_minimum_required(VERSION 3.20)
project(chapter06_01_raii_peripheral_management CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 嵌入式友好选项
option(BUILD_SHARED_LIBS "Build shared libraries" OFF)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")

# 为每个示例创建可执行文件
add_executable(gpio_raii gpio_raii.cpp)
add_executable(spi_transaction spi_transaction.cpp)
add_executable(dma_channel dma_channel.cpp)
add_executable(scope_guard scope_guard.cpp)
