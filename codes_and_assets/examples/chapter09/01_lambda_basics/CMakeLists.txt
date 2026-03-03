cmake_minimum_required(VERSION 3.15)
project(chapter09_lambda_basics CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 避免输出路径重复
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

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
