// e2_concept_negative.cpp —— 篇1 e2 丁场:不满足 ByteSource 的类型,编译期被拦
//
// 与 cross-platform/03 的 e2 负例同构,资源换成了字节源:ShortSource 提供
// read_fixed(名字不满足约定),没有 read_some。两处检查:
//   static_assert(!ByteSource<ShortSource>) —— concept 如实回答"不满足"
//   drain(shorty, 64)                        —— 受约束模板拒绝实例化,诊断点名
// 预期:第一处编过,第二处编不过(-c 只编译不链接,要的就是诊断)。
#include <concepts>
#include <cstddef>
#include <cstdio>
#include <string>

template <class S>
concept ByteSource = requires(S& s, void* buf, std::size_t n) {
    { s.read_some(buf, n) } -> std::same_as<std::size_t>;
};

class ShortSource {
  public:
    std::size_t read_fixed(void*, std::size_t) { return 0; } // 名字不对题
};

static_assert(!ByteSource<ShortSource>,
              "ShortSource 不满足 ByteSource —— 缺 read_some,编译期就该现形");

template <ByteSource S> std::string drain(S& src, std::size_t limit) {
    std::string out;
    char buf[256];
    while (out.size() < limit) {
        std::size_t n = src.read_some(buf, sizeof buf);
        if (n == 0)
            break;
        out.append(buf, n);
    }
    return out;
}

int main() {
    ShortSource shorty;
    std::string s = drain(shorty, 64); // 这一行注定编不过
    std::printf("%zu\n", s.size());
    return 0;
}
