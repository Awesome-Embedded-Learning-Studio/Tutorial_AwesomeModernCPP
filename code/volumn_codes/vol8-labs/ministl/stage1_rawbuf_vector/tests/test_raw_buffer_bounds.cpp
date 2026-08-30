// tests/test_raw_buffer_bounds.cpp —— visit_at 的界外死法
//
// 照 Chromium 镜那派的语义:界外访问必须以 Check Crash 收场。
// 所以这个测试的"正确输出"是 abort + stderr 检查报告;
// 真打印出最后一行,说明检查没拦住,反而是失败。
#include <cstdio>

#include "tamcpp_ministl/raw_buffer.hpp"

int main() {
    tamcpp::ministl::RawBuffer<int> buf(4);
    buf.visit_at(4); // capacity 恰好 4,访问的是第 5 格
    std::puts("UNREACHABLE: bounds check did not fire");
    return 0;
}
