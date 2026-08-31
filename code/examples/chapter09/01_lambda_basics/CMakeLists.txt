cmake_minimum_required(VERSION 3.15)
project(chapter09_lambda_basics CXX)

# MSVC:按 UTF-8 读源码与执行字符集(源文件是 UTF-8 无 BOM,含中文注释/字符串,
# 不显式声明会被 cl 按系统代码页误读,C2001 常量中有换行符)
add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 避免输出路径重复
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# MSVC 线用 /W4 对齐 GCC 线警告水位(不开 /WX,能跑优先);必须在 add_executable 之前
add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/W4>)
add_executable(basic_syntax basic_syntax.cpp)
add_executable(type_deduction type_deduction.cpp)
add_executable(algorithm_args algorithm_args.cpp)
add_executable(capture_basics capture_basics.cpp)
add_executable(generic_lambda generic_lambda.cpp)
add_executable(event_handler event_handler.cpp)
add_executable(config_builder config_builder.cpp)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(basic_syntax PRIVATE -Wall -Wextra -Werror)
    target_compile_options(type_deduction PRIVATE -Wall -Wextra -Werror)
    target_compile_options(algorithm_args PRIVATE -Wall -Wextra -Werror)
    target_compile_options(capture_basics PRIVATE -Wall -Wextra -Werror)
    target_compile_options(generic_lambda PRIVATE -Wall -Wextra -Werror)
    target_compile_options(event_handler PRIVATE -Wall -Wextra -Werror)
    target_compile_options(config_builder PRIVATE -Wall -Wextra -Werror)
endif()
