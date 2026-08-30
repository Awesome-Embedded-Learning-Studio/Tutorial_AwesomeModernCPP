# 验收:黑盒接口测试 + 一个"必须以 Check Crash 收场"的死法测试
tamcpp_add_exec(test_raw_buffer test_raw_buffer.cpp)
add_test(NAME raw_buffer.interface COMMAND test_raw_buffer)

tamcpp_add_exec(test_vector test_vector.cpp)
add_test(NAME vector.interface COMMAND test_vector)

tamcpp_add_exec(test_raw_buffer_bounds test_raw_buffer_bounds.cpp)
# ctest 见到信号打死的子进程直接判负、不看输出正则,
# 所以"期望崩溃"的用例交给裁判脚本,由它验证两件事:
# 崩溃报告在场,且程序真的死了(详见 expect_check_death.sh)。
add_test(NAME raw_buffer.visit_at_oob
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/expect_check_death.sh
            $<TARGET_FILE:test_raw_buffer_bounds>)

tamcpp_add_exec(test_vector_visit_oob test_vector_visit_oob.cpp)
# 界必须是 size 不是 capacity(见该文件注释)
add_test(NAME vector.visit_at_oob
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/expect_check_death.sh
            $<TARGET_FILE:test_vector_visit_oob>)
