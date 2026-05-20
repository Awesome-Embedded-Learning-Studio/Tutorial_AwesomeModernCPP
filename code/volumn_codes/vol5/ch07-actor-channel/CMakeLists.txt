cmake_minimum_required(VERSION 3.20)
project(ch07_actor_channel VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

add_executable(01_actor_framework 01_actor_framework.cpp)
target_link_libraries(01_actor_framework PRIVATE Threads::Threads)

add_executable(02_channel_csp 02_channel_csp.cpp)
target_link_libraries(02_channel_csp PRIVATE Threads::Threads)
