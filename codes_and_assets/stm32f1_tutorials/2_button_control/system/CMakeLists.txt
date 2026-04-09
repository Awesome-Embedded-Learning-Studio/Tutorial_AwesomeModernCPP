add_library(system STATIC
    hal_mock.c
    syscall.c
    clock.cpp
)

target_include_directories(system PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}                                         
    ${STM32F1_ROOT}/CMSIS/Include
    ${STM32F1_ROOT}/CMSIS/Device/ST/STM32F1xx/Include
    ${STM32F1_ROOT}/STM32F1xx_HAL_Driver/Inc
)
