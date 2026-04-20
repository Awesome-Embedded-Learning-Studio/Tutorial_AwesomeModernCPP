cmake_minimum_required(VERSION 3.20)
project(Vol1Ch04 VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 01-pointer-basics
add_executable(01-address-demo 01-address-demo.cpp)
add_executable(01-pointer-types 01-pointer-types.cpp)
add_executable(01-track-pointers 01-track-pointers.cpp)
add_executable(01-fix-pointer-bugs 01-fix-pointer-bugs.cpp)

# 02-pointer-arithmetic
add_executable(02-array-decay 02-array-decay.cpp)
add_executable(02-pointer-step 02-pointer-step.cpp)
add_executable(02-array-traversal 02-array-traversal.cpp)
add_executable(02-cstring-ops 02-cstring-ops.cpp)
add_executable(02-ptr-arith-demo 02-ptr-arith-demo.cpp)

# 03-references
add_executable(03-references-demo 03-references-demo.cpp)
add_executable(03-find-ref-bugs 03-find-ref-bugs.cpp)

# 04-smart-ptr-preview
add_executable(04-raw-vs-smart 04-raw-vs-smart.cpp)
add_executable(04-unique-ptr-demo 04-unique-ptr-demo.cpp)
