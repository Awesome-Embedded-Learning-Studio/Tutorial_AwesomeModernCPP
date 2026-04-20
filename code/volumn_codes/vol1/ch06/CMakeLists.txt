cmake_minimum_required(VERSION 3.20)
project(Vol1Ch06 VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(point point.cpp)
add_executable(constructors constructors.cpp)
add_executable(ctor_dtor_order ctor_dtor_order.cpp)
add_executable(raii_demo raii_demo.cpp)
add_executable(static_demo static_demo.cpp)
add_executable(friend_demo friend_demo.cpp)
add_executable(cascading_demo cascading_demo.cpp)
