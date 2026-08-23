cmake_minimum_required(VERSION 3.16)
project(f103_blink C CXX ASM)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 默认优化级别 RelWithDebInfo(-O2 -g):既能验证零开销,又能 GDB 单步
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

find_program(RENODE_BIN renode REQUIRED)

# add_blink_variant(name src)
# 声明一个闪灯变体:可执行文件 + hex/bin + run_<name>_in_renode 目标。
# 重构阶梯每一档调用一次,各档独立可编、可跑、可对照汇编。
function(add_blink_variant name src)
    add_executable(${name}.elf ${src} platform/startup.s)
    target_link_options(${name}.elf PRIVATE
        -T${CMAKE_CURRENT_SOURCE_DIR}/platform/stm32f103c8.ld
        -Wl,-Map=${CMAKE_BINARY_DIR}/${name}.map)
    add_custom_command(TARGET ${name}.elf POST_BUILD
        COMMAND ${CMAKE_OBJCOPY} -O ihex   $<TARGET_FILE:${name}.elf> ${CMAKE_BINARY_DIR}/${name}.hex
        COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${name}.elf> ${CMAKE_BINARY_DIR}/${name}.bin
        COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${name}.elf>)
    add_custom_target(run_${name}_in_renode
        COMMAND ${RENODE_BIN} --console --disable-xwt
                ${CMAKE_CURRENT_SOURCE_DIR}/${name}.resc -e quit
        DEPENDS ${name}.elf
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Running ${name}.elf in Renode simulator")
endfunction()

# --- 重构阶梯:每一档一个变体,逐档对照汇编验证零开销 ---
add_blink_variant(blink               src/main.c)                 # 第 1 档:裸机寄存器(C,基线)
add_blink_variant(blink_mmio          src/main_mmio.cpp)          # 第 2 档:mmio_reg 类型安全封装
add_blink_variant(blink_fields        src/main_fields.cpp)        # 第 3 档:位域 + enum class
add_blink_variant(blink_gpio_template src/main_gpio_template.cpp) # 第 4 档:模板 GPIO/LED
add_blink_variant(blink_cpp23         src/main_cpp23.cpp)         # 第 5 档:C++23 consteval/constinit/属性
add_blink_variant(blink_uart          src/main_uart.cpp)          # 加餐:闪灯 + printf 走 UART1(配合 showAnalyzer)

# TODO(真板到位):加 flash 目标(openocd -f interface/stlink.cfg -f target/stm32f1x.cfg ...)
# TODO(有可分离纯逻辑后,如消抖状态机/环形缓冲):加 host 单测目标(Catch2/doctest)
