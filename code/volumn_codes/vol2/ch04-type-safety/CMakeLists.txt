cmake_minimum_required(VERSION 3.20)
project(ch04_type_safety VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 01-enum-class (uses C++20 using enum)
add_executable(enum_class_basics enum_class_basics.cpp)
set_target_properties(enum_class_basics PROPERTIES CXX_STANDARD 20)

# 02-strong-types
add_executable(strong_types strong_types.cpp)

# 03-variant
add_executable(variant_basics variant_basics.cpp)

# 04-optional
add_executable(optional_demo optional_demo.cpp)

# 05-any
add_executable(any_basics any_basics.cpp)

# ==================== 验证代码 ====================
# 用于验证文章中的技术断言

# 验证 01-enum-class.md
add_executable(verify_01_enum_class 01-enum-class-verification.cpp)
target_compile_options(verify_01_enum_class PRIVATE
    -Wall -Wextra -Wswitch -Wswitch-enum
)

# 验证 02-strong-types.md
add_executable(verify_02_strong_types 02-strong-types-verification.cpp)
target_compile_options(verify_02_strong_types PRIVATE -Wall -Wextra -O2)

# 验证 03-variant.md
add_executable(verify_03_variant 03-variant-verification.cpp)
target_compile_options(verify_03_variant PRIVATE -Wall -Wextra)

# 验证 04-optional.md
add_executable(verify_04_optional 04-optional-verification.cpp)
target_compile_options(verify_04_optional PRIVATE -Wall -Wextra)

# 验证 05-any.md
add_executable(verify_05_any 05-any-verification.cpp)
target_compile_options(verify_05_any PRIVATE -Wall -Wextra)
