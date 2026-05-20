cmake_minimum_required(VERSION 3.20)
project(ch05_future_task_threadpool VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(jthread_move_semantics jthread_move_semantics.cpp)
