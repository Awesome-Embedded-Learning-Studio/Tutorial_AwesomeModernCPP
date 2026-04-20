// specialize.cpp
#include <cstring>
#include <iostream>
#include <string>

/// @brief 通用打印器——直接输出值
template <typename T>
struct Printer {
    static void print(const T& value, const char* name = "")
    {
        if (name[0] != '\0') {
            std::cout << name << " = ";
        }
        std::cout << value << "\n";
    }
};

/// @brief bool 全特化——输出 "true" / "false"
template <>
struct Printer<bool> {
    static void print(bool value, const char* name = "")
    {
        if (name[0] != '\0') {
            std::cout << name << " = ";
        }
        std::cout << (value ? "true" : "false") << "\n";
    }
};

/// @brief const char* 全特化——安全打印字符串
template <>
struct Printer<const char*> {
    static void print(const char* value, const char* name = "")
    {
        if (name[0] != '\0') {
            std::cout << name << " = ";
        }
        std::cout << (value ? value : "(null)") << "\n";
    }
};

/// @brief 指针偏特化——打印解引用后的值
template <typename T>
struct Printer<T*> {
    static void print(T* ptr, const char* name = "")
    {
        if (name[0] != '\0') {
            std::cout << name << " = ";
        }
        if (ptr) {
            std::cout << "*";
            Printer<T>::print(*ptr);
        } else {
            std::cout << "(null)\n";
        }
    }
};

int main()
{
    // 通用版本
    Printer<int>::print(42, "int_val");
    Printer<double>::print(3.14, "double_val");
    Printer<std::string>::print(std::string("hello"), "str_val");

    std::cout << "\n";

    // bool 全特化
    Printer<bool>::print(true, "flag");
    Printer<bool>::print(false, "is_empty");

    std::cout << "\n";

    // const char* 全特化
    Printer<const char*>::print("world", "cstr");
    Printer<const char*>::print(nullptr, "null_str");

    std::cout << "\n";

    // 指针偏特化
    int x = 100;
    int* ptr = &x;
    int* null_ptr = nullptr;
    Printer<int*>::print(ptr, "int_ptr");
    Printer<int*>::print(null_ptr, "null_ptr");

    return 0;
}
