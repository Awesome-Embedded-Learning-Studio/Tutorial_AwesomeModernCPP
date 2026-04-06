cmake_minimum_required(VERSION 3.22)

# ── 工具链（必须在 project() 之前）──────────────────────────────────────────
set(CMAKE_SYSTEM_NAME      Generic)
set(CMAKE_SYSTEM_PROCESSOR ARM)

set(CMAKE_C_COMPILER       arm-none-eabi-gcc)
set(CMAKE_CXX_COMPILER     arm-none-eabi-g++)
set(CMAKE_ASM_COMPILER     arm-none-eabi-gcc)
set(CMAKE_OBJCOPY          arm-none-eabi-objcopy)
set(CMAKE_SIZE             arm-none-eabi-size)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# 避免 CMake 做链接测试时报错
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# ── 项目定义 ────────────────────────────────────────────────────────────────
project(stm32_demo C CXX ASM)
set(CMAKE_C_STANDARD   23)
set(CMAKE_CXX_STANDARD 23)

# ── HAL / CMSIS 路径 ────────────────────────────────────────────────────────
set(STM32F1_ROOT ${CMAKE_SOURCE_DIR}/../../../third_party/STM32F1/Drivers)
set(CMSIS_TMPL   ${STM32F1_ROOT}/CMSIS/Device/ST/STM32F1xx/Source/Templates)

# ── 源文件 ──────────────────────────────────────────────────────────────────
file(GLOB HAL_SRC
    ${STM32F1_ROOT}/STM32F1xx_HAL_Driver/Src/*.c
)

list(FILTER HAL_SRC EXCLUDE REGEX ".*_template\\.c$")

add_compile_options(
    -mcpu=cortex-m3
    -mthumb
    -O2
    -Wall
    -Wextra
    -Wno-missing-field-initializers
    -ffunction-sections
    -fdata-sections
    -DUSE_HAL_DRIVER
    -DSTM32F103xB
)

add_compile_options(
    $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>
    $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>
)

# ── 子目录 ──────────────────────────────────────────────────────────────────
add_subdirectory(system)

# ── 可执行目标 ───────────────────────────────────────────────────────────────
add_executable(${PROJECT_NAME}.elf
    main.cpp
    ${CMSIS_TMPL}/system_stm32f1xx.c
    ${CMSIS_TMPL}/gcc/startup_stm32f103xb.s
    ${HAL_SRC}
)

target_link_libraries(${PROJECT_NAME}.elf PRIVATE
    -Wl,--whole-archive
    system
    -Wl,--no-whole-archive
)

# ── 链接选项 ─────────────────────────────────────────────────────────────────
target_link_options(${PROJECT_NAME}.elf PRIVATE
    -mcpu=cortex-m3
    -mthumb
    -T${CMAKE_SOURCE_DIR}/STM32F103C8TX_FLASH.ld
    -nostartfiles
    -specs=nano.specs
    -specs=nosys.specs
    -Wl,--gc-sections
    -Wl,-Map=${PROJECT_NAME}.map
)

# ── 构建后处理：生成 .bin 并打印 size ────────────────────────────────────────
add_custom_command(TARGET ${PROJECT_NAME}.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY}
            -O binary
            $<TARGET_FILE:${PROJECT_NAME}.elf>
            ${PROJECT_NAME}.bin
    COMMAND ${CMAKE_SIZE}
            --format=berkeley
            $<TARGET_FILE:${PROJECT_NAME}.elf>
    COMMENT "Generating ${PROJECT_NAME}.bin"
)

# ── 烧录目标：make flash ─────────────────────────────────────────────────────
add_custom_target(flash
    COMMAND openocd
            -f interface/stlink.cfg
            -f target/stm32f1x.cfg
            -c "program ${PROJECT_NAME}.bin verify reset exit 0x08000000"
    DEPENDS ${PROJECT_NAME}.elf
    COMMENT "Flashing ${PROJECT_NAME}.bin via OpenOCD"
)

# ── 擦除目标：make erase ─────────────────────────────────────────────────────
add_custom_target(erase
    COMMAND openocd
            -f interface/stlink.cfg
            -f target/stm32f1x.cfg
            -c "init; halt; stm32f1x mass_erase 0; exit"
    COMMENT "Erasing STM32 Flash"
)
