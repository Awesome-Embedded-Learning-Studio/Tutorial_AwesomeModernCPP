// 验证代码：[[no_unique_address]] 的实际效果
// 验证空类优化和地址相同性

struct Empty {
    void foo() {}
};

// 不使用属性
struct Container1 {
    Empty e;
    int x;
};

// 使用属性
struct Container2 {
    [[no_unique_address]] Empty e;
    int x;
};

// 多个空成员
struct Container3 {
    [[no_unique_address]] Empty e1;
    [[no_unique_address]] Empty e2;
    int x;
};

struct Test1 {
    [[no_unique_address]] Empty e1;
    [[no_unique_address]] Empty e2;
};

struct Test2 {
    [[no_unique_address]] Empty e1;
    [[no_unique_address]] Empty e2;
    int x;
};

struct Test3 {
    [[no_unique_address]] Empty e1;
    int x;
    [[no_unique_address]] Empty e2;
};

// 非空类
struct NotEmpty {
    int data;
};

struct Container4 {
    [[no_unique_address]] NotEmpty e;
    int x;
};

#include <cstdio>

int main() {
    printf("sizeof(Empty) = %zu\n", sizeof(Empty));
    printf("sizeof(Container1) = %zu\n", sizeof(Container1));
    printf("sizeof(Container2) = %zu\n", sizeof(Container2));
    printf("sizeof(Container3) = %zu\n", sizeof(Container3));
    printf("sizeof(Container4) = %zu\n", sizeof(Container4));
    printf("sizeof(Test1) = %zu\n", sizeof(Test1));
    printf("sizeof(Test2) = %zu\n", sizeof(Test2));
    printf("sizeof(Test3) = %zu\n", sizeof(Test3));

    // 测试地址是否相同
    Container3 c3;
    Test1 t1;
    Test2 t2;
    Test3 t3;

    printf("\nContainer3: &e1=%p, &e2=%p, equal? %d\n",
           (void*)&c3.e1, (void*)&c3.e2, &c3.e1 == &c3.e2);
    printf("Test1: &e1=%p, &e2=%p, diff=%ld, equal=%d\n",
           (void*)&t1.e1, (void*)&t1.e2,
           (char*)&t1.e2 - (char*)&t1.e1, &t1.e1 == &t1.e2);
    printf("Test2: &e1=%p, &e2=%p, &x=%p, diff e1-e2=%ld, equal=%d\n",
           (void*)&t2.e1, (void*)&t2.e2, (void*)&t2.x,
           (char*)&t2.e2 - (char*)&t2.e1, &t2.e1 == &t2.e2);
    printf("Test3: &e1=%p, &e2=%p, &x=%p, diff e1-e2=%ld, equal=%d\n",
           (void*)&t3.e1, (void*)&t3.e2, (void*)&t3.x,
           (char*)&t3.e2 - (char*)&t3.e1, &t3.e1 == &t3.e2);

    return 0;
}

/*
编译命令（GCC 15.2.1）：
g++ -std=c++20 test_no_unique_address.cpp -o test_no_unique_addr
./test_no_unique_addr

测试结果：
sizeof(Empty) = 1
sizeof(Container1) = 8  （没有属性，有对齐）
sizeof(Container2) = 4  （有属性，优化掉空类）
sizeof(Container3) = 4  （多个空成员也优化）
sizeof(Container4) = 8  （非空类不优化）

地址测试结果（GCC 15.2.1）：
- Test1（只有两个空成员）：&e1 和 &e2 不同，相差 1 字节
- Test2（e1, e2, x）：e1 和 x 地址相同！e2 和 e1 相差 1 字节
- Test3（e1, x, e2）：e1 和 x 地址相同！e2 单独占 1 字节

重要发现：
1. 文章中说 "同一类型的多个 [[no_unique_address]] 成员可能共享同一地址"
   在 GCC 15.2.1 上，它们不一定共享地址，但第一个空成员可能和后续
   非空成员共享地址（如 Test2 中 e1 和 x 地址相同）。

2. 文章中的警告是正确的，但具体行为依赖编译器实现。

3. sizeof 的优化效果是确定且显著的。
*/
