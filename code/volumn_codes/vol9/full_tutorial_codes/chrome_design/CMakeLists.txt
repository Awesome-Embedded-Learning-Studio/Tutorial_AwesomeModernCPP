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
    12_intrusive_refcount
    13_atomic_flag
    14_sequence_checker
)

# C++20 示例（需要 concepts / lambda pack expansion）
set(CPP20_EXAMPLES
    05_lambda_advanced
    09_concepts_requires
    15_concepts_requires
    16_weak_ptr_skeleton
    17_weak_ptr_factory
    18_bind_weakptr_cancel
    19_flat_map_basic
    20_lookup_vs_shift_perf
    21_sorted_unique_tag
    22_bulk_build_extract
    23_no_destructor_basic
    24_placement_new_and_storage
    25_static_assert_gatekeep
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

# threading support:原子/序列检查示例需要 pthread
find_package(Threads REQUIRED)
target_link_libraries(03_atomic_memory_order PRIVATE Threads::Threads)
target_link_libraries(12_intrusive_refcount PRIVATE Threads::Threads)
target_link_libraries(13_atomic_flag PRIVATE Threads::Threads)
target_link_libraries(14_sequence_checker PRIVATE Threads::Threads)
target_link_libraries(16_weak_ptr_skeleton PRIVATE Threads::Threads)
target_link_libraries(17_weak_ptr_factory PRIVATE Threads::Threads)
target_link_libraries(18_bind_weakptr_cancel PRIVATE Threads::Threads)
target_link_libraries(19_flat_map_basic PRIVATE Threads::Threads)
target_link_libraries(20_lookup_vs_shift_perf PRIVATE Threads::Threads)
target_link_libraries(21_sorted_unique_tag PRIVATE Threads::Threads)
target_link_libraries(22_bulk_build_extract PRIVATE Threads::Threads)
