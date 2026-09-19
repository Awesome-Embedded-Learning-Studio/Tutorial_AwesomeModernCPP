#include <algorithm>
#include <cstring>
#include <span>

// Modern C++ 版:缓冲区长度随 std::span 走,「正好写满 n 格」写进参数类型;
// memcpy 抄真字符、memset 补 '\0',两步各干各的。
void copy_n(std::span<char> dst, const char* src) {
    const std::size_t take = std::min(std::strlen(src), dst.size());
    std::memcpy(dst.data(), src, take);                      // 有多少真字符抄多少
    std::memset(dst.data() + take, '\0', dst.size() - take); // 不足的格子补 '\0' 凑满
}

// 判题签名(题目约定的 C 风格原型):一行转调 span 版
void copy_n(char dst[], char src[], int n) {
    copy_n(std::span<char>(dst, n), src);
}
