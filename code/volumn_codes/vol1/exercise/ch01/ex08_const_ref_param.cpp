/**
 * @file ex08_const_ref_param.cpp
 * @brief 练习：写一个使用 const 引用参数的函数
 *
 * 实现 print_sum(const int& a, const int& b)，演示 const 引用参数的使用。
 * 注释说明何时 const T& 比直接传 T 更有利。
 */

#include <iostream>
#include <string>

// 使用 const 引用接收 int 参数
// 对于 int 这样的基本类型，传值和传 const 引用的性能差异极小
// 但 const 引用可以避免不必要的拷贝，并且保证不会意外修改参数
void print_sum(const int& a, const int& b) {
    std::cout << a << " + " << b << " = " << (a + b) << '\n';
}

// 对于大型对象（如 std::string），const 引用的优势非常明显：
// - 避免拷贝整个字符串
// - 保证函数不会修改原始字符串
void print_string_info(const std::string& str) {
    std::cout << "字符串 \"" << str << "\" 长度为 " << str.size() << '\n';
}

// 返回字符串中单词数的函数（同样使用 const 引用）
int count_words(const std::string& text) {
    if (text.empty()) {
        return 0;
    }
    int count = 0;
    bool in_word = false;
    for (char ch : text) {
        if (ch == ' ' || ch == '\t' || ch == '\n') {
            in_word = false;
        } else if (!in_word) {
            in_word = true;
            ++count;
        }
    }
    return count;
}

int main() {
    // 基本类型的 const 引用
    int x = 10;
    int y = 20;
    print_sum(x, y);
    print_sum(5, 7);  // const 引用可以绑定到右值

    // 大型对象的 const 引用
    std::string message = "Hello, Modern C++ const references!";
    print_string_info(message);
    print_string_info("临时字符串也可以");  // const 引用绑定到临时对象

    std::string sentence = "The quick brown fox jumps over the lazy dog";
    std::cout << "\"" << sentence << "\" 中有 "
              << count_words(sentence) << " 个单词\n";

    // ===== 何时使用 const T& vs T =====
    std::cout << "\n===== 何时使用 const T& vs T =====\n";
    std::cout << "基本类型（int, double, 指针等）：传值即可，开销相同\n";
    std::cout << "大型对象（string, vector, 自定义类）：使用 const T&\n";
    std::cout << "经验法则：sizeof(T) > 16 字节或 T 有非平凡拷贝时用 const T&\n";

    return 0;
}
