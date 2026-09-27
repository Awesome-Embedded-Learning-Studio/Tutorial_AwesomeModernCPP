#include <chrono>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

class Timer {
  public:
    Timer() : start_(std::chrono::high_resolution_clock::now()) {}

    double elapsed_ms() const {
        auto end = std::chrono::high_resolution_clock::now();
        return std::chrono::duration<double, std::milli>(end - start_).count();
    }

  private:
    std::chrono::high_resolution_clock::time_point start_;
};

#include <cctype>

// 版本一：const string& 参数
int count_digits_v1(const std::string& s) {
    int count = 0;
    for (char c : s) {
        if (std::isdigit(static_cast<unsigned char>(c))) {
            ++count;
        }
    }
    return count;
}

// 版本二：string_view 参数
int count_digits_v2(std::string_view sv) {
    int count = 0;
    for (char c : sv) {
        if (std::isdigit(static_cast<unsigned char>(c))) {
            ++count;
        }
    }
    return count;
}

void bench_param_passing() {
    constexpr int kCalls = 1000000;
    std::string str_data = "abc123def456ghi789jkl012mno345";
    const char* c_data = "abc123def456ghi789jkl012mno345";

    // 测试 1：传 std::string 给 const string& 参数
    {
        Timer t;
        volatile int sink = 0;
        for (int i = 0; i < kCalls; ++i) {
            sink += count_digits_v1(str_data);
        }
        std::cout << "const string& + string arg: " << t.elapsed_ms() << " ms\n";
    }

    // 测试 2：传 const char* 给 const string& 参数（需要临时构造）
    {
        Timer t;
        volatile int sink = 0;
        for (int i = 0; i < kCalls; ++i) {
            sink += count_digits_v1(c_data); // 隐式构造临时 string
        }
        std::cout << "const string& + char* arg:  " << t.elapsed_ms() << " ms\n";
    }

    // 测试 3：传 std::string 给 string_view 参数
    {
        Timer t;
        volatile int sink = 0;
        for (int i = 0; i < kCalls; ++i) {
            sink += count_digits_v2(str_data);
        }
        std::cout << "string_view   + string arg: " << t.elapsed_ms() << " ms\n";
    }

    // 测试 4：传 const char* 给 string_view 参数
    {
        Timer t;
        volatile int sink = 0;
        for (int i = 0; i < kCalls; ++i) {
            sink += count_digits_v2(c_data);
        }
        std::cout << "string_view   + char* arg:  " << t.elapsed_ms() << " ms\n";
    }
}

int main() {
    bench_param_passing();
    return 0;
}
