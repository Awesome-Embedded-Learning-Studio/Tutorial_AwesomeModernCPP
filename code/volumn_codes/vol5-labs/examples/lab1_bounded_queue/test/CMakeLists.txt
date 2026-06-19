# Lab 1 测试:每个 milestone 一个独立 executable。
# Catch2 由工程顶层(standalone guard)或 vol5-labs 顶层 FetchContent 拉取,这里只链接。
set(LAB1_TESTS
    test_milestone1
    test_milestone2
    test_milestone3
    test_milestone4
    test_milestone5
    test_milestone6
)

foreach(test ${LAB1_TESTS})
    add_executable(${test} ${test}.cpp)
    target_link_libraries(${test} PRIVATE lab1 Catch2::Catch2WithMain)
    target_include_directories(${test} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_options(${test} PRIVATE -Wall -Wextra -Wpedantic)

    # Debug 配置开 ThreadSanitizer:-DCMAKE_BUILD_TYPE=Debug 构建即在 TSan 下运行。
    target_compile_options(${test} PRIVATE
        $<$<CONFIG:Debug>:-fsanitize=thread -g -fno-omit-frame-pointer>)
    target_link_options(${test} PRIVATE
        $<$<CONFIG:Debug>:-fsanitize=thread>)

    add_test(NAME ${test} COMMAND ${test})
endforeach()
