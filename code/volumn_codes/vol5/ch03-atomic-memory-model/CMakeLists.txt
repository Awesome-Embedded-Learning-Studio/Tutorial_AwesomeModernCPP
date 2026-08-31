cmake_minimum_required(VERSION 3.20)
project(ch03_atomic_memory_model VERSION 0.0.1 LANGUAGES CXX)

# MSVC:按 UTF-8 读源码与执行字符集(源文件是 UTF-8 无 BOM,含中文注释/字符串,
# 不显式声明会被 cl 按系统代码页误读,C2001 常量中有换行符)
add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 可运行的可执行文件（含 main 函数）
add_executable(atomic_default_ctor_verify atomic_default_ctor_verify.cpp)

# 汇编验证文件（无 main 函数，仅编译为 .s 文件用于指令检查）
# 使用方式：cmake --build build --target asm_check
set(ASM_OUTPUT_DIR ${CMAKE_BINARY_DIR}/asm)
file(MAKE_DIRECTORY ${ASM_OUTPUT_DIR})

function(add_asm_target SRC_FILE)
    get_filename_component(BASE ${SRC_FILE} NAME_WE)
    if(MSVC)
        # MSVC:cl /c 只编译不链接,/Fa 把汇编清单写成 .asm
        set(ASM_OUTPUT ${ASM_OUTPUT_DIR}/${BASE}.asm)
        add_custom_command(
            OUTPUT ${ASM_OUTPUT}
            COMMAND ${CMAKE_CXX_COMPILER} /nologo /std:c++20 /O2 /c
                    /Fa${ASM_OUTPUT} /Fo${ASM_OUTPUT_DIR}/${BASE}.obj
                    ${CMAKE_CURRENT_SOURCE_DIR}/${SRC_FILE}
            DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${SRC_FILE}
            COMMENT "Generating assembly: ${BASE}.asm"
        )
    else()
        set(ASM_OUTPUT ${ASM_OUTPUT_DIR}/${BASE}.s)
        add_custom_command(
            OUTPUT ${ASM_OUTPUT}
            COMMAND ${CMAKE_CXX_COMPILER} -std=c++20 -O2 -S -o ${ASM_OUTPUT} ${CMAKE_CURRENT_SOURCE_DIR}/${SRC_FILE}
            DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${SRC_FILE}
            COMMENT "Generating assembly: ${BASE}.s"
        )
    endif()
    add_custom_target(gen_${BASE} DEPENDS ${ASM_OUTPUT})
endfunction()

add_asm_target(atomic_cas_weak_strong_asm.cpp)
add_asm_target(fence_x86_instructions.cpp)
add_asm_target(memory_order_store_x86.cpp)

add_custom_target(asm_check
    DEPENDS gen_atomic_cas_weak_strong_asm gen_fence_x86_instructions gen_memory_order_store_x86
    COMMENT "All assembly files generated in: ${ASM_OUTPUT_DIR}/"
)
