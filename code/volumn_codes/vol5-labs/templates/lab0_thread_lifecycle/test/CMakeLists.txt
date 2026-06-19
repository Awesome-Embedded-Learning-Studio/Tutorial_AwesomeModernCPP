# Lab 0 测试:每个 milestone 一个独立 executable。
# 学习者实现完某个 milestone 的组件,就能立刻跑对应测试,不必等全部写完。
# Catch2 由工程顶层(standalone guard)或 vol5-labs 顶层 FetchContent 拉取,这里只链接。
set(LAB0_TESTS
    test_milestone1
    test_milestone2
    test_milestone3
    test_milestone4
)

foreach(test ${LAB0_TESTS})
    add_executable(${test} ${test}.cpp)
    target_link_libraries(${test} PRIVATE lab0 Catch2::Catch2WithMain)
    target_include_directories(${test} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_options(${test} PRIVATE -Wall -Wextra -Wpedantic)

    # Debug 配置开 ThreadSanitizer:-DCMAKE_BUILD_TYPE=Debug 构建即在 TSan 下运行。
    # Catch2 没有 --tsan 运行参数,TSan 是编译期 -fsanitize=thread 开启的。
    target_compile_options(${test} PRIVATE
        $<$<CONFIG:Debug>:-fsanitize=thread -g -fno-omit-frame-pointer>)
    target_link_options(${test} PRIVATE
        $<$<CONFIG:Debug>:-fsanitize=thread>)

    add_test(NAME ${test} COMMAND ${test})
endforeach()
