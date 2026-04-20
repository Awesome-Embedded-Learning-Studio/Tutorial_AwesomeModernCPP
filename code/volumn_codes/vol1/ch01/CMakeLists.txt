cmake_minimum_required(VERSION 3.20)
project(Vol1Ch01 VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# basic-types
add_executable(fixed-width-sizes fixed-width-sizes.cpp)
add_executable(numeric-limits numeric-limits.cpp)
add_executable(float-precision float-precision.cpp)
add_executable(char-types char-types.cpp)
add_executable(bool-arithmetic bool-arithmetic.cpp)
add_executable(builtin-type-sizes builtin-type-sizes.cpp)
add_executable(integer-type-sizes integer-type-sizes.cpp)

# type-conversion
add_executable(implicit-conversion implicit-conversion.cpp)
add_executable(explicit-cast explicit-cast.cpp)
add_executable(float-comparison float-comparison.cpp)
add_executable(conversion-overview conversion-overview.cpp)
add_executable(integer-division integer-division.cpp)
add_executable(celsius-to-fahrenheit celsius-to-fahrenheit.cpp)

# const-basics
add_executable(const-and-constexpr const-and-constexpr.cpp)

# value-categories
add_executable(pass-by-value-vs-ref pass-by-value-vs-ref.cpp)
add_executable(lvalue-rvalue-demo lvalue-rvalue-demo.cpp)
add_executable(dangling-reference dangling-reference.cpp)
