// decltype_auto_trailing_return.cpp - decltype(auto)和尾返回类型
#include <iostream>
#include <vector>
#include <string>
#include <utility>
#include <map>

// 尾返回类型语法（C++11）
template<typename T, typename U>
auto add_cxx11(T t, U u) -> decltype(t + u) {
    return t + u;
}

// 简化的返回类型推导（C++14）
template<typename T, typename U>
auto add_cxx14(T t, U u) {
    return t + u;
}

// decltype(auto) - 完美转发返回值
struct Container {
    std::vector<int> data;

    // 传统写法：需要显式指定返回类型
    std::vector<int>& get_data() {
        return data;
    }

    const std::vector<int>& get_data() const {
        return data;
    }

    // C++14：auto会丢弃引用
    auto get_data_by_auto() {
        return data;  // 返回std::vector<int>（拷贝！）
    }

    // C++14：decltype(auto)保留引用
    decltype(auto) get_data_by_decltype_auto() {
        return data;  // 返回std::vector<int>&
    }

    decltype(auto) get_data_by_decltype_auto_const() const {
        return data;  // 返回const std::vector<int>&
    }

    // 下标运算符：完美转发
    decltype(auto) operator[](size_t index) {
        return data[index];  // 返回int&
    }

    // const版本需要单独重载
    const int& operator[](size_t index) const {
        return data[index];  // 返回const int&
    }
};

// 演示括号的重要性
int global = 42;

decltype(auto) get_global_by_value() {
    return global;        // 返回int
}

decltype(auto) get_global_by_ref() {
    return (global);      // 返回int&（注意括号！）
}

// 模板函数完美转发
template<typename Container>
decltype(auto) get_first(Container&& c) {
    return c[0];  // 完美转发：如果是左值返回左值引用，右值返回右值
}

void demonstrate_trailing_return() {
    std::cout << "=== 尾返回类型演示 ===\n\n";

    std::cout << "C++11尾返回类型:\n";
    std::cout << "  add_cxx11(3, 4) = " << add_cxx11(3, 4) << '\n';
    std::cout << "  add_cxx11(3.5, 2) = " << add_cxx11(3.5, 2) << '\n';

    std::cout << "\nC++14自动返回类型:\n";
    std::cout << "  add_cxx14(3, 4) = " << add_cxx14(3, 4) << '\n';
    std::cout << "  add_cxx14(3.5, 2) = " << add_cxx14(3.5, 2) << '\n';

    // 不同类型相加
    std::string s1 = "Hello, ";
    const char* s2 = "World!";
    std::cout << "  add_cxx14(s1, s2) = " << add_cxx14(s1, s2) << '\n';
}

void demonstrate_decltype_auto() {
    std::cout << "\n=== decltype(auto)演示 ===\n\n";

    Container c;
    c.data = {1, 2, 3, 4, 5};

    std::cout << "1. 引用保留:\n";
    c.get_data_by_decltype_auto().push_back(6);
    std::cout << "  添加元素后的大小: " << c.data.size() << '\n';

    std::cout << "\n2. 下标访问:\n";
    c[0] = 100;
    std::cout << "  c[0] = " << c.data[0] << '\n';

    const Container cc;
    // cc[0] = 100;  // 错误：const版本返回const int&

    std::cout << "\n3. 括号的重要性:\n";
    std::cout << "  get_global_by_value() 返回 int\n";
    std::cout << "  get_global_by_ref() 返回 int&\n";

    decltype(auto) ref = get_global_by_ref();
    ref = 100;
    std::cout << "  通过引用修改后, global = " << global << '\n';
}

void demonstrate_perfect_forwarding() {
    std::cout << "\n=== 完美转发演示 ===\n\n";

    std::vector<int> v = {1, 2, 3};

    // 左值：返回引用
    get_first(v) = 100;
    std::cout << "1. 左值容器: v[0] = " << v[0] << '\n';

    // 右值：返回值
    auto x = get_first(std::vector<int>{10, 20, 30});
    std::cout << "2. 右值容器: x = " << x << '\n';
}

// 实际应用：配置解析器
class ConfigParser {
public:
    struct ConfigValue {
        std::string key;
        std::string value;
        bool found;
    };

    ConfigValue find_value(const std::string& key) {
        auto it = config_.find(key);
        if (it != config_.end()) {
            return {it->first, it->second, true};
        }
        return {"", "", false};
    }

    // 使用decltype(auto)避免拷贝
    decltype(auto) operator[](const std::string& key) {
        return config_[key];  // 返回std::string&
    }

    // const版本 - 使用find因为operator[]不是const的
    const std::string& operator[](const std::string& key) const {
        static const std::string empty;
        auto it = config_.find(key);
        if (it != config_.end()) {
            return it->second;
        }
        return empty;
    }

private:
    std::map<std::string, std::string> config_;
};

void demonstrate_real_world_usage() {
    std::cout << "\n=== 实际应用：配置解析器 ===\n\n";

    ConfigParser parser;
    parser["baudrate"] = "115200";
    parser["parity"] = "none";

    auto& baudrate = parser["baudrate"];  // 引用，避免拷贝
    baudrate = "9600";  // 直接修改

    std::cout << "配置值:\n";
    std::cout << "  baudrate: " << parser["baudrate"] << '\n';
    std::cout << "  parity: " << parser["parity"] << '\n';
}

int main() {
    demonstrate_trailing_return();
    demonstrate_decltype_auto();
    demonstrate_perfect_forwarding();
    demonstrate_real_world_usage();

    std::cout << "\n=== 演示结束 ===\n";
    return 0;
}
