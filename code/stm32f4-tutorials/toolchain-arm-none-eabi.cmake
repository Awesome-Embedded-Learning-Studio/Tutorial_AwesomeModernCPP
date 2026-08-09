# STM32F407 交叉编译工具链文件(各工程共用)
#
# 用法:
#   cd code/stm32f4-tutorials/0_blink
#   cmake -B build -DCMAKE_TOOLCHAIN_FILE=../toolchain-arm-none-eabi.cmake
#   cmake --build build
#
# 抽成独立文件,避免旧 F1 那样把工具链内联进每个 CMakeLists、改一处要改四份。
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR cortex-m4)

set(CROSS_COMPILE arm-none-eabi-)
set(CMAKE_C_COMPILER   ${CROSS_COMPILE}gcc)
set(CMAKE_CXX_COMPILER ${CROSS_COMPILE}g++)
set(CMAKE_ASM_COMPILER ${CROSS_COMPILE}gcc)
set(CMAKE_OBJCOPY      ${CROSS_COMPILE}objcopy)
set(CMAKE_SIZE         ${CROSS_COMPILE}size)
set(CMAKE_READELF      ${CROSS_COMPILE}readelf)

# 裸机环境没有标准库运行时,try_compile 走链接会失败,强制按静态库探测
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# F407 = Cortex-M4F,单精度 FPU(fpv4-sp-d16),硬浮点 ABI
set(MCU_FLAGS "-mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb")
set(CMAKE_C_FLAGS_INIT   "${MCU_FLAGS}")
set(CMAKE_CXX_FLAGS_INIT "${MCU_FLAGS} -fno-exceptions -fno-rtti -fno-threadsafe-statics")
set(CMAKE_ASM_FLAGS_INIT "${MCU_FLAGS} -x assembler-with-cpp")
set(CMAKE_EXE_LINKER_FLAGS_INIT "${MCU_FLAGS} -nostdlib -Wl,--gc-sections")
