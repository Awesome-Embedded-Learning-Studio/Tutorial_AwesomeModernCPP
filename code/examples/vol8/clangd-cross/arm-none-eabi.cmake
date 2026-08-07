# STM32F1 (Cortex-M3) 交叉编译 toolchain 文件片段
# 用法:cmake -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=arm-none-eabi.cmake
#
# 详见 documents/vol8-domains/embedded/00-env-setup/06-clangd-for-cross-compilation.md

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR cortex-m3)

# 指定交叉编译器(CMake 会把绝对路径写进 compile_commands.json)
set(CMAKE_C_COMPILER arm-none-eabi-gcc)
set(CMAKE_CXX_COMPILER arm-none-eabi-g++)

# 裸机环境跳过 try_compile 的运行检查,否则 ARM 可执行文件在本机跑不了
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# Cortex-M3 + Thumb 指令集,这些 flag 会随 compile_commands.json 透传给 clangd
set(MCU_FLAGS "-mcpu=cortex-m3 -mthumb")
set(CMAKE_C_FLAGS_INIT   "${MCU_FLAGS}")
set(CMAKE_CXX_FLAGS_INIT "${MCU_FLAGS}")

# 工程根 CMakeLists.txt 里也开 ON;这里再开一次保险
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
