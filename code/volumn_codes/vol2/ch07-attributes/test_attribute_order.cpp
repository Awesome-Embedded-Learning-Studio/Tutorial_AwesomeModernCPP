// 验证代码：多个属性的顺序和组合方式
// 验证 [[attr1, attr2]] 和 [[attr1]] [[attr2]] 效果是否相同

#include <cstdio>

// 方式1：写在一起
[[nodiscard, deprecated("Use new_func()")]] int func1() {
    return 1;
}

// 方式2：分开写
[[nodiscard]] [[deprecated("Use new_func()")]] int func2() {
    return 2;
}

// 方式3：反向顺序
[[deprecated("Use new_func()")]] [[nodiscard]] int func3() {
    return 3;
}

int main() {
    func1();  // 应该产生两个警告：nodiscard 和 deprecated
    func2();  // 应该产生两个警告
    func3();  // 应该产生两个警告
    return 0;
}

/*
编译命令：
g++ -std=c++17 test_attribute_order.cpp -Wdeprecated

测试结果：
所有三种方式都产生了相同的警告：
- ignoring return value, declared with attribute 'nodiscard'
- is deprecated: Use new_func()

结论：[[attr1, attr2]] 和 [[attr1]] [[attr2]] 效果完全相同。
顺序也不影响效果。
*/
