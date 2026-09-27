// 18_class_templates.cpp
// 类模板实战演练：泛型栈 Stack（在线单文件版，把 stack.hpp 与 stack_demo.cpp 合在一起）

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

/// @brief 泛型栈，底层使用 std::vector 存储
/// @tparam T 元素类型
template <typename T> class Stack {
  public:
    /// @brief 将元素压入栈顶
    void push(const T& value) { data_.push_back(value); }

    /// @brief 弹出栈顶元素
    /// @throws std::out_of_range 栈为空时抛出异常
    void pop() {
        if (data_.empty()) {
            throw std::out_of_range("Stack::pop(): stack is empty");
        }
        data_.pop_back();
    }

    /// @brief 访问栈顶元素（可修改）
    /// @throws std::out_of_range 栈为空时抛出异常
    T& top() {
        if (data_.empty()) {
            throw std::out_of_range("Stack::top(): stack is empty");
        }
        return data_.back();
    }

    /// @brief 访问栈顶元素（只读）
    /// @throws std::out_of_range 栈为空时抛出异常
    const T& top() const {
        if (data_.empty()) {
            throw std::out_of_range("Stack::top(): stack is empty");
        }
        return data_.back();
    }

    /// @brief 判断栈是否为空
    bool empty() const { return data_.empty(); }

    /// @brief 返回栈中元素数量
    std::size_t size() const { return data_.size(); }

  private:
    std::vector<T> data_;
};

int main() {
    // --- Stack<int> ---
    std::cout << "=== Stack<int> ===\n";
    Stack<int> int_stack;
    int_stack.push(10);
    int_stack.push(20);
    int_stack.push(30);
    std::cout << "size: " << int_stack.size() << "\n";
    std::cout << "top:  " << int_stack.top() << "\n";
    int_stack.pop();
    std::cout << "after pop, top: " << int_stack.top() << "\n";
    std::cout << "empty: " << std::boolalpha << int_stack.empty() << "\n";

    // --- Stack<double> ---
    std::cout << "\n=== Stack<double> ===\n";
    Stack<double> dbl_stack;
    dbl_stack.push(3.14);
    dbl_stack.push(2.718);
    std::cout << "size: " << dbl_stack.size() << "\n";
    std::cout << "top:  " << dbl_stack.top() << "\n";
    dbl_stack.pop();
    std::cout << "after pop, top: " << dbl_stack.top() << "\n";

    // --- Stack<std::string> ---
    std::cout << "\n=== Stack<std::string> ===\n";
    Stack<std::string> str_stack;
    str_stack.push("hello");
    str_stack.push("world");
    str_stack.push("template");
    std::cout << "size: " << str_stack.size() << "\n";
    std::cout << "top:  " << str_stack.top() << "\n";
    str_stack.pop();
    std::cout << "after pop, top: " << str_stack.top() << "\n";

    // --- 异常测试 ---
    std::cout << "\n=== Exception test ===\n";
    Stack<int> empty_stack;
    try {
        empty_stack.pop();
    } catch (const std::out_of_range& e) {
        std::cout << "caught: " << e.what() << "\n";
    }

    return 0;
}
