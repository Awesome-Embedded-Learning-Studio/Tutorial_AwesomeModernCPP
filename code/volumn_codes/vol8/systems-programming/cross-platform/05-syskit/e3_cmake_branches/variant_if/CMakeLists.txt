# 写法一:if(WIN32)/if(UNIX) 配置期分支
# 生效时机:configure 阶段,CMAKE_SYSTEM_NAME 定了它就定了
# 要害:它信的是 CMake 的平台身份。身份认错(裸 CC 覆盖 g++.exe 时被当成
# Linux),分支就选错,错误要到编译期才现形
cmake_minimum_required(VERSION 3.16)
project(cmake_branch_if CXX)
add_executable(demo main.cpp)
if(WIN32)
    message(STATUS "[if-variant] branch = WIN32  (CMAKE_SYSTEM_NAME=${CMAKE_SYSTEM_NAME})")
    target_compile_definitions(demo PRIVATE SYSKIT_PLATFORM_WIN32=1)
elseif(UNIX)
    message(STATUS "[if-variant] branch = UNIX  (CMAKE_SYSTEM_NAME=${CMAKE_SYSTEM_NAME})")
    target_compile_definitions(demo PRIVATE SYSKIT_PLATFORM_POSIX=1)
endif()
