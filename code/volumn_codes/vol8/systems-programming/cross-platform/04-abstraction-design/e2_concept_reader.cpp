// e2_concept_reader.cpp —— 篇1 e2 丙场:concepts 约束版 file_reader(编译期多态)
//
// 与甲场同一件事的另一种收法:#ifdef 不再散落在函数体内,而是退到"选后端"
// 的一处边界;两个后端各自的实现文件里干干净净,公共的形状用 concept 声明。
// 受约束的用户代码 drain 是模板:每个后端实例化各自的版本(可内联,无虚表),
// 这就是"静默多态"——调用点写法与运行期多态无异,分派发生在编译期。
//
// 接口形状参照 cross-platform/03 的 AsyncBackend(完成式):read_some 交回
// 实际读到的字节数,0 表示到尾;错误用 errno/GetLastError 原样带出。
// 本篇把同一思路从异步后端扩到普通资源——这是 03 篇留下的口,咱们来收。
//
// 编译口径:同甲场。负例(缺 read_some 的类型)在 e2_concept_negative.cpp。
#include <cstdio>
#include <fstream>
#include <string>

#ifdef _WIN32
#    define WIN32_LEAN_AND_MEAN
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    include <windows.h>
#else
#    include <fcntl.h>
#    include <unistd.h>
#endif

// ---- 公共形状:五行 concept,一个"字节源"的约定 ----
#include <concepts>
#include <cstddef>

template <class S>
concept ByteSource = requires(S& s, void* buf, std::size_t n) {
    { s.read_some(buf, n) } -> std::same_as<std::size_t>; // 读到多少交回多少,0=到尾
};

// ---- 后端一:内存源,两侧通用(第二个被同等检查的后端) ----
class MemSource {
  public:
    explicit MemSource(const std::string& data) : data_(data) {}
    std::size_t read_some(void* buf, std::size_t n) {
        const std::size_t take = (pos_ + n > data_.size()) ? data_.size() - pos_ : n;
        __builtin_memcpy(buf, data_.data() + pos_, take);
        pos_ += take;
        return take;
    }

  private:
    std::string data_;
    std::size_t pos_ = 0;
};

// ---- 后端二:平台源。实现内部零 #ifdef;#ifdef 只活在"选谁"的边界上 ----
#ifdef _WIN32
class Win32HandleSource {
  public:
    explicit Win32HandleSource(const char* path) {
        wchar_t wpath[512];
        ::MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, 512);
        h_ = ::CreateFileW(wpath, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    }
    ~Win32HandleSource() {
        if (h_ != INVALID_HANDLE_VALUE)
            ::CloseHandle(h_);
    }
    Win32HandleSource(const Win32HandleSource&) = delete;
    Win32HandleSource& operator=(const Win32HandleSource&) = delete;
    bool valid() const { return h_ != INVALID_HANDLE_VALUE; }
    std::size_t read_some(void* buf, std::size_t n) {
        DWORD got = 0;
        if (!::ReadFile(h_, buf, static_cast<DWORD>(n), &got, nullptr))
            return 0; // 失败按 0 交回;错误细节的教学完整版归思维基石,这里只示意
        return got;
    }

  private:
    HANDLE h_ = INVALID_HANDLE_VALUE;
};
using NativeSource = Win32HandleSource;
#else
class PosixFdSource {
  public:
    explicit PosixFdSource(const char* path) : fd_(::open(path, O_RDONLY)) {}
    ~PosixFdSource() {
        if (fd_ >= 0)
            ::close(fd_);
    }
    PosixFdSource(const PosixFdSource&) = delete;
    PosixFdSource& operator=(const PosixFdSource&) = delete;
    bool valid() const { return fd_ >= 0; }
    std::size_t read_some(void* buf, std::size_t n) {
        for (;;) {
            ssize_t r = ::read(fd_, buf, n);
            if (r < 0) {
                if (errno == EINTR)
                    continue;
                return 0;
            }
            return static_cast<std::size_t>(r);
        }
    }

  private:
    int fd_ = -1;
};
using NativeSource = PosixFdSource;
#endif

// ---- 用户代码:一份,所有后端通用。这就是受约束的静默多态 ----
template <ByteSource S> static std::string drain(S& src, std::size_t limit) {
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

// 后端被实例化即被检查:static_assert 让"每个后端都满足约定"成为编译期的明面
static_assert(ByteSource<MemSource>);
static_assert(ByteSource<NativeSource>);

int main(int argc, char** argv) {
    const char* path = (argc > 1) ? argv[1] : "e2_data.txt";
    const std::string payload = "hello from concept dispatch\n";
    {
        std::ofstream f(path, std::ios::binary);
        f << payload;
    }

    MemSource mem{payload};
    const std::string a = drain(mem, 4096);

    NativeSource nat{path};
    const std::string b = nat.valid() ? drain(nat, 4096) : std::string{};

#ifdef _WIN32
    std::printf("[concept] backends={Mem,Win32} mem_len=%zu nat_len=%zu match=%s/%s\n", a.size(),
                b.size(), a == payload ? "yes" : "NO", b == payload ? "yes" : "NO");
#else
    std::printf("[concept] backends={Mem,POSIX} mem_len=%zu nat_len=%zu match=%s/%s\n", a.size(),
                b.size(), a == payload ? "yes" : "NO", b == payload ? "yes" : "NO");
#endif
    return (a == payload && b == payload) ? 0 : 1;
}
