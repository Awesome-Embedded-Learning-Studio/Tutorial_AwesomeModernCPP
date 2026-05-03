cmake_minimum_required(VERSION 3.20)
project(chrome_design_prerequisites LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# C++17 示例（无需 C++20/23 特性）
set(CPP17_EXAMPLES
    01_move_semantics
    02_smart_pointers
    03_atomic_memory_order
    04_lambda_basics
    06_type_traits
    07_function_type_specialization
    08_invoke
)

# C++20 示例（需要 concepts / lambda pack expansion）
set(CPP20_EXAMPLES
    05_lambda_advanced
    09_concepts_requires
)

# C++23 示例（需要 move_only_function / deducing this）
set(CPP23_EXAMPLES
    10_move_only_function
    11_deducing_this
)

# C++17 targets
foreach(name ${CPP17_EXAMPLES})
    add_executable(${name} ${name}.cpp)
    target_compile_options(${name} PRIVATE -Wall -Wextra -Wpedantic)
    set_target_properties(${name} PROPERTIES CXX_STANDARD 17)
endforeach()

# C++20 targets
foreach(name ${CPP20_EXAMPLES})
    add_executable(${name} ${name}.cpp)
    target_compile_options(${name} PRIVATE -Wall -Wextra -Wpedantic)
    set_target_properties(${name} PROPERTIES CXX_STANDARD 20)
endforeach()

# C++23 targets
foreach(name ${CPP23_EXAMPLES})
    add_executable(${name} ${name}.cpp)
    target_compile_options(${name} PRIVATE -Wall -Wextra -Wpedantic)
    set_target_properties(${name} PROPERTIES CXX_STANDARD 23)
endforeach()

# threading support for atomic example
find_package(Threads REQUIRED)
target_link_libraries(03_atomic_memory_order PRIVATE Threads::Threads)
