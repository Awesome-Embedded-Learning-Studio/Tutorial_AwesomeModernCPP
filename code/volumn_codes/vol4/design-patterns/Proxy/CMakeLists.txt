cmake_minimum_required(VERSION 3.10)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
project(Proxy VERSION 0.1.0 LANGUAGES C CXX)

add_executable(ProxyMain Proxy.h ProxyMain.cpp)
find_package(Threads REQUIRED)
target_link_libraries(ProxyMain PRIVATE Threads::Threads)
