# 顶层只把 Catch2 的 extras 加进了 MODULE_PATH，要用 catch_discover_tests
# 还得自己 include(Catch) 把命令加载进来。
include(Catch)

# 这个 Lab 会有好几个测试 target，依赖集合都一样
# （Catch2 + TAMCPP_TinyML）+ 同一套跨编译器 warning + 都要注册到 ctest。
# 封一层糖，每个测试一行起，不用反复抄，也避免手抄抄出 bug。
function(tamcpp_add_test name source)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE Catch2::Catch2WithMain TAMCPP_TinyML)
    tamcpp_target_warnings(${name})
    catch_discover_tests(${name})
endfunction()

tamcpp_add_test(smoke_catch2 smoke.cpp)
tamcpp_add_test(tensor_api tensor_api.cpp)
