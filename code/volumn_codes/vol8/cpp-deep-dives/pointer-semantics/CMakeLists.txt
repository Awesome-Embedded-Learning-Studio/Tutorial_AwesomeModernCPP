cmake_minimum_required(VERSION 3.14)
project(pointer-semantics-demo LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(COMMON_FLAGS -Wall -Wextra -Wpedantic -g)

# 文章 01：Borrowed<T> 和 ObserverPtr<T>
add_executable(test_borrowed_observer test_borrowed_observer.cpp)
target_compile_options(test_borrowed_observer PRIVATE ${COMMON_FLAGS})

# 文章 02：UnsafeWeakPtr UB 演示
add_executable(test_unsafe_ub test_unsafe_weak_ptr_ub.cpp)
target_compile_options(test_unsafe_ub PRIVATE ${COMMON_FLAGS})

# 文章 02：UnsafeWeakPtr UB 演示（ASan 版本）
add_executable(test_unsafe_ub_asan test_unsafe_weak_ptr_ub.cpp)
target_compile_options(test_unsafe_ub_asan PRIVATE ${COMMON_FLAGS} -fsanitize=address,undefined)
target_link_options(test_unsafe_ub_asan PRIVATE -fsanitize=address,undefined)

# 文章 03：SimpleWeakPtr 安全失效检测
add_executable(test_simple_wp test_simple_weak_ptr.cpp)
target_compile_options(test_simple_wp PRIVATE ${COMMON_FLAGS})

# 文章 04：Chrome-like WeakPtr 生命周期验证
add_executable(test_lifetime test_lifetime_verification.cpp)
target_compile_options(test_lifetime PRIVATE ${COMMON_FLAGS})
