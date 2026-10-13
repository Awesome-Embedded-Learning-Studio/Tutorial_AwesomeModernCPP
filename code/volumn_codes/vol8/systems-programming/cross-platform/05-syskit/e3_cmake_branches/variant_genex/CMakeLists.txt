# 写法二:generator expression,生成期展开
# 生效时机:生成构建系统的时刻(configure 之后、编译之前)
# 要害一:configure 阶段 message 打印它,看到的只能是原文——它此时还没展开
# 要害二:PLATFORM_ID 问的仍是 CMAKE_SYSTEM_NAME,身份认错它照样被骗
cmake_minimum_required(VERSION 3.16)
project(cmake_branch_genex CXX)
add_executable(demo main.cpp)
message(STATUS "[genex-variant] raw text at configure time: $<PLATFORM_ID:Windows>")
message(STATUS "[genex-variant] CMAKE_SYSTEM_NAME=${CMAKE_SYSTEM_NAME}")
target_compile_definitions(demo PRIVATE
    $<$<PLATFORM_ID:Windows>:GENEX_SAYS_WINDOWS=1>
    $<$<PLATFORM_ID:Linux>:GENEX_SAYS_LINUX=1>
)
