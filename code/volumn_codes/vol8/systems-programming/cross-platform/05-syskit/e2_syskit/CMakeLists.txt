# e2_syskit —— syskit 骨架的最小可编译版(篇2 e2)
# INTERFACE 库(纯头文件)+ 冒烟测试;双平台同一份 CMakeLists,零平台分支,
# 平台差异全收在头文件的 #ifdef 里——这正是本篇的主张:分支退到边界。
cmake_minimum_required(VERSION 3.16)
project(syskit_skeleton CXX)

add_library(syskit INTERFACE)
target_include_directories(syskit INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(syskit INTERFACE cxx_std_23)

enable_testing()
add_executable(smoke tests/smoke.cpp)
target_link_libraries(smoke PRIVATE syskit)
add_test(NAME smoke COMMAND smoke)
