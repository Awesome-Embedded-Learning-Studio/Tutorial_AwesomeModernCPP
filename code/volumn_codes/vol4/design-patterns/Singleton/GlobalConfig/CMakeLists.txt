cmake_minimum_required(VERSION 3.10)
set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
project(Singleton2 VERSION 0.1.0 LANGUAGES C CXX)

add_executable(ConfigManager ConfigManager.cpp ConfigManager.h ConfigManager_main.cpp)

add_custom_command(
    TARGET ConfigManager PRE_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        ${CMAKE_SOURCE_DIR}/test_config.txt
        ${CMAKE_BINARY_DIR}/test_config.txt
    COMMENT "Copying test_config.txt to build directory"
)
