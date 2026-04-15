// 空基类优化 (EBO) 示例

#include <iostream>
#include <cstdint>

// ==================== 空基类 ====================
class EmptyBase {
public:
    void func() { std::cout << "EmptyBase::func()" << std::endl; }
};

// ==================== 不使用 EBO 的继承 ====================
class Derived_Bad : private EmptyBase {
private:
    int data_;
public:
    Derived_Bad(int val) : data_(val) {}
    int get_data() const { return data_; }
};

// ==================== 使用 EBO 的继承 ====================
template<typename Base>
class Derived_Good : private Base {
private:
    int data_;
public:
    Derived_Good(int val) : data_(val) {}
    int get_data() const { return data_; }
    void call_base() { Base::func(); }
};

// ==================== 多个空基类 ====================
class Empty1 {
public:
    void f1() { std::cout << "Empty1::f1()" << std::endl; }
};

class Empty2 {
public:
    void f2() { std::cout << "Empty2::f2()" << std::endl; }
};

class Empty3 {
public:
    void f3() { std::cout << "Empty3::f3()" << std::endl; }
};

// 继承多个空基类
class MultiEmpty : public Empty1, public Empty2, public Empty3 {
private:
    int data_;
public:
    MultiEmpty(int val) : data_(val) {}
    int get_data() const { return data_; }
};

// ==================== 对齐示例 ====================
struct alignas(16) AlignedEmpty {
    void func() {}
};

class WithAlignedEmpty : public AlignedEmpty {
private:
    int data_;
public:
    WithAlignedEmpty(int val) : data_(val) {}
    int get_data() const { return data_; }
};

// ==================== 演示主函数 ====================
int main() {
    std::cout << "=== 空基类优化 (EBO) 示例 ===" << std::endl;

    // 1. 基本 EBO
    std::cout << "\n--- 基本 EBO ---" << std::endl;
    std::cout << "sizeof(EmptyBase): " << sizeof(EmptyBase) << std::endl;
    std::cout << "sizeof(Derived_Bad): " << sizeof(Derived_Bad) << std::endl;
    std::cout << "sizeof(Derived_Good<EmptyBase>): " << sizeof(Derived_Good<EmptyBase>) << std::endl;

    // 2. 多个空基类
    std::cout << "\n--- 多个空基类 ---" << std::endl;
    std::cout << "sizeof(Empty1): " << sizeof(Empty1) << std::endl;
    std::cout << "sizeof(Empty2): " << sizeof(Empty2) << std::endl;
    std::cout << "sizeof(Empty3): " << sizeof(Empty3) << std::endl;
    std::cout << "sizeof(MultiEmpty): " << sizeof(MultiEmpty) << std::endl;

    // 3. 带对齐的空基类
    std::cout << "\n--- 带对齐的空基类 ---" << std::endl;
    std::cout << "alignof(AlignedEmpty): " << alignof(AlignedEmpty) << std::endl;
    std::cout << "sizeof(AlignedEmpty): " << sizeof(AlignedEmpty) << std::endl;
    std::cout << "sizeof(WithAlignedEmpty): " << sizeof(WithAlignedEmpty) << std::endl;

    std::cout << "\n关键点：" << std::endl;
    std::cout << "1. 空基类优化 (EBO) 让空基类不占用派生类空间" << std::endl;
    std::cout << "2. 标准 C++ 编译器默认启用 EBO" << std::endl;
    std::cout << "3. 多个空基类可以共享同一地址空间" << std::endl;
    std::cout << "4. 带对齐的空基类可能影响 EBO 效果" << std::endl;
    std::cout << "5. EBO 是模板设计中的重要优化技术" << std::endl;

    return 0;
}
