# e1_cmake_probe —— 篇1 e1 第 3 路:CMake 的配置期平台检测
#
# 配置期发生在 cmake 生成构建系统之前,它知道的比预处理器多(编译器探测、
# try_compile 试编),也少(默认不知道交叉目标,得靠 toolchain 文件声明)。
# 这个工程只做检测、打报告,顺带 configure_file 出一张 config.h。
cmake_minimum_required(VERSION 3.16)
project(platform_probe CXX)

include(CheckIncludeFileCXX)

message(STATUS "[probe] CMAKE_SYSTEM_NAME = ${CMAKE_SYSTEM_NAME}")
message(STATUS "[probe] CMAKE_SYSTEM_VERSION = ${CMAKE_SYSTEM_VERSION}")
message(STATUS "[probe] CMAKE_CXX_COMPILER = ${CMAKE_CXX_COMPILER}")
message(STATUS "[probe] WIN32=${WIN32} UNIX=${UNIX} MSVC=${MSVC} MINGW=${MINGW} MSYS=${MSYS} CYGWIN=${CYGWIN}")
message(STATUS "[probe] CMAKE_SIZEOF_VOID_P = ${CMAKE_SIZEOF_VOID_P}")

# check_include_file_cxx 内部走一次 try_compile:头在不在,编一把才知道
check_include_file_cxx("sys/epoll.h"   HAVE_SYS_EPOLL_H)
check_include_file_cxx("sys/event.h"   HAVE_SYS_EVENT_H)
check_include_file_cxx("liburing.h"    HAVE_LIBURING_H)
check_include_file_cxx("windows.h"     HAVE_WINDOWS_H)
message(STATUS "[probe] HAVE_SYS_EPOLL_H=${HAVE_SYS_EPOLL_H} HAVE_SYS_EVENT_H=${HAVE_SYS_EVENT_H} HAVE_LIBURING_H=${HAVE_LIBURING_H} HAVE_WINDOWS_H=${HAVE_WINDOWS_H}")

configure_file(config.h.in config.h)
add_executable(probe_main probe_main.cpp)
target_include_directories(probe_main PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_compile_features(probe_main PRIVATE cxx_std_20)
