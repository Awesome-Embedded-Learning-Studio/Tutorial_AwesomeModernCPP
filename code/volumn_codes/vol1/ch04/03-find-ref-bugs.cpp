// 03-find-ref-bugs.cpp
// 练习：找出以下代码中与引用相关的所有问题
// 提示：行 A、行 B、行 C 各有错误，取消注释后会编译失败

int& get_value()
{
    int x = 42;
    return x;  // BUG: 返回局部变量的引用（悬空引用，运行时未定义行为）
}

void process(int& ref) { ref += 10; }

int main()
{
    int& r = get_value();  // 悬空引用：绑定了已销毁的局部变量

    // int& uninit;          // 行 A：编译错误——引用必须在声明时初始化
    int a = 10;
    int& ref = a;
    int b = 20;
    // ref = &b;             // 行 B：编译错误——不能把 int* 赋给 int
    // process(5);           // 行 C：编译错误——不能把右值绑定到非 const 引用
}
