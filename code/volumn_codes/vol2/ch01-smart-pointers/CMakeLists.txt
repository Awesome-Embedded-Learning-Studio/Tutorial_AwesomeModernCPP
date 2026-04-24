cmake_minimum_required(VERSION 3.20)
project(ch01_smart_pointers VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Threads REQUIRED)

add_executable(raii-deep-dive_1_stack_unwinding raii-deep-dive_1_stack_unwinding.cpp)
add_executable(raii-deep-dive_2_destructor_noexcept raii-deep-dive_2_destructor_noexcept.cpp)
add_executable(raii-deep-dive_3_edge_cases raii-deep-dive_3_edge_cases.cpp)
add_executable(raii-deep-dive_2 raii-deep-dive_2.cpp)
add_executable(unique-ptr_3 unique-ptr_3.cpp)
add_executable(unique-ptr_8 unique-ptr_8.cpp)
add_executable(custom-deleter_5 custom-deleter_5.cpp)
add_executable(shared_ptr_cache_benchmark shared_ptr_cache_benchmark.cpp)
target_link_libraries(shared_ptr_cache_benchmark PRIVATE Threads::Threads)

# Verification tests for unique_ptr article
add_executable(test_ebo_sizeof test_ebo_sizeof.cpp)
add_executable(test_vector_noexcept test_vector_noexcept.cpp)

# PIMPL pattern demonstration (split compilation)
add_executable(test_pimpl_pattern
    pimpl_widget.cpp
    pimpl_user.cpp
)

# Verification tests for shared_ptr article
add_executable(verify_shared_ptr_layout verify_shared_ptr_layout.cpp)
add_executable(verify_shared_ptr_performance verify_shared_ptr_performance.cpp)
target_link_libraries(verify_shared_ptr_performance PRIVATE Threads::Threads)
add_executable(verify_cache_contention verify_cache_contention.cpp)
target_link_libraries(verify_cache_contention PRIVATE Threads::Threads)

# Verification tests for weak_ptr article
add_executable(test_weak_ptr_atomicity test_weak_ptr_atomicity.cpp)
target_link_libraries(test_weak_ptr_atomicity PRIVATE Threads::Threads)
add_executable(test_weak_ptr_performance test_weak_ptr_performance.cpp)

# Verification tests for custom deleter article
add_executable(05-custom-deleter-sizeof 05-custom-deleter-sizeof.cpp)
add_executable(05-custom-deleter-benchmark 05-custom-deleter-benchmark.cpp)
add_executable(05-intrusive-ptr-demo 05-intrusive-ptr-demo.cpp)

# Verification tests for scope_guard article
add_executable(06-scope-guard-verification 06-scope-guard-verification.cpp)
