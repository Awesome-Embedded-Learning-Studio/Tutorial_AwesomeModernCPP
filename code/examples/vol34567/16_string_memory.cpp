// Standard: C++23
// string 内存深入：SSO 观察 + resize_and_overwrite 缓冲复用
#include <algorithm>
#include <cstring>
#include <iostream>
#include <string>

// 判断 string 的 data() 是否落在对象内联缓冲里（SSO 的标志）
bool points_inside_object(const std::string& s) {
    const char* obj = reinterpret_cast<const char*>(&s);
    return s.data() >= obj && s.data() < obj + sizeof(std::string);
}

// 模拟一个 C API：向 buf 最多写 n 字节，返回实际写入数
std::size_t fake_read(char* buf, std::size_t n) {
    static const char msg[] = "hello";
    std::size_t len = std::min(n, sizeof(msg) - 1);
    std::memcpy(buf, msg, len);
    return len;
}

int main() {
    std::cout << "sizeof(std::string) = " << sizeof(std::string) << '\n';

    std::string short_s = "hi";  // 很可能走 SSO
    std::string long_s(64, 'x'); // 超过 SSO 阈值，出堆
    std::cout << "short_s.data() 在对象内? " << points_inside_object(short_s) << "（SSO）\n";
    std::cout << "long_s.data()  在对象内? " << points_inside_object(long_s) << "（出堆）\n";

    std::cout << "\n== resize() 旧写法：先把 64 字符全部值初始化（清零），再截断 ==\n";
    std::string old_buf;
    old_buf.resize(64);
    std::size_t got = fake_read(old_buf.data(), old_buf.size());
    old_buf.resize(got);
    std::cout << "old: '" << old_buf << "' (len=" << old_buf.size() << ")\n";

    std::cout << "\n== resize_and_overwrite (C++23)：不清零多余字符，回调报告实际长度 ==\n";
    std::string buf;
    buf.resize_and_overwrite(64, [](char* p, std::size_t n) noexcept {
        return fake_read(p, n); // 只写实际字节，返回新长度（r ∈ [0, n]）
    });
    std::cout << "new: '" << buf << "' (len=" << buf.size() << ")\n";
    return 0;
}
