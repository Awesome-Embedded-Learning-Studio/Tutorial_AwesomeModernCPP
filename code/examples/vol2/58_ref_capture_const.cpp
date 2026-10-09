// 引用捕获的底层就是「闭包里存了个指针」,所以 operator() 是 const 的,
// 照样能改它指向的对象——跟 int* const p 一个道理:p 本身动不了,*p 随便改。
// 下面的 RefClosure 是把 [&sum](int) 的闭包类型手写「翻译」出来的等价物。
#include <iostream>

struct RefClosure {
    int& sum;
    void operator()(int value) const { sum += value; } // const 成员函数,照改不误
};

int main() {
    int sum = 0;

    auto accumulate = [&sum](int value) { sum += value; };
    accumulate(10);
    accumulate(20);
    accumulate(30);
    std::cout << "lambda:        sum = " << sum << " (expected 60)\n";

    sum = 0;
    RefClosure hand_written{sum};
    hand_written(10);
    hand_written(20);
    hand_written(30);
    std::cout << "hand-written:  sum = " << sum << " (expected 60)\n";

    int a = 0;
    double b = 0.0;
    auto one_ref = [&a] { return a; };
    auto two_refs = [&a, &b] { return a + b; };
    std::cout << "\nsizeof(one_ref)  = " << sizeof(one_ref) << " -- one pointer\n";
    std::cout << "sizeof(two_refs) = " << sizeof(two_refs) << " -- two pointers\n";
    std::cout << "sizeof(int*)     = " << sizeof(int*) << "\n";
    return 0;
}
