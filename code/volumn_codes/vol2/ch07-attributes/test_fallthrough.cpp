// 验证代码：空 case 是否需要 [[fallthrough]]
// 验证编译器是否对空 case 发出警告

#include <cstdio>

void test_empty_case(int x) {
    switch (x) {
        case 1:
        case 2:  // 空 case 贯穿
            printf("1 or 2\n");
            break;
        case 3:
            printf("3\n");
        case 4:  // 非空 case 贯穿，应该警告
            printf("4\n");
            break;
        default:
            printf("other\n");
    }
}

int main() {
    test_empty_case(1);
    test_empty_case(3);
    return 0;
}

/*
编译命令：
g++ -std=c++17 -Wimplicit-fallthrough test_fallthrough.cpp

测试结果：
只有 case 3 -> case 4 的非空贯穿产生警告。
空 case（case 1 -> case 2）不产生警告。

结论：文章断言正确 - 编译器不对空 case 发出警告。
*/
