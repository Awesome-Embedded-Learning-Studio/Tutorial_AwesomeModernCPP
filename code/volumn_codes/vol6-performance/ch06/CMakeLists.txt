cmake_minimum_required(VERSION 3.20)
project(vol6_ch06_cpp_abstraction_cost LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)  # abstraction_sizeof 用 std::span(C++20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 06-01 虚函数与去虚拟化:与 ch04/virtual_devirt.cpp 共用,本目录不重复
# 06-02 异常的零成本模型
add_executable(exception_cost exception_cost.cpp)
target_compile_options(exception_cost PRIVATE -O2 -Wall -Wextra -Wpedantic)

# 06-03 std::function 的 SBO
add_executable(function_sbo function_sbo.cpp)
target_compile_options(function_sbo PRIVATE -O2 -Wall -Wextra -Wpedantic)

# 06-04 成本速查表 sizeof
add_executable(abstraction_sizeof abstraction_sizeof.cpp)
target_compile_options(abstraction_sizeof PRIVATE -O2 -Wall -Wextra -Wpedantic)

# 06-05 RVO/NRVO/move。默认 -O2;对照版见 README(-fno-elide-constructors)
add_executable(rvo_move rvo_move.cpp)
target_compile_options(rvo_move PRIVATE -O2 -Wall -Wextra -Wpedantic)
