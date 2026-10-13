// e1_backend_concept.cpp
// vol8 systems-programming ch04 跨平台篇的核心实验:
//   一个最小的完成式异步后端 concept(AsyncBackend: submit/wait/take 三件套),
//   两侧各写一个满足它的最小实现骨架,同一份受约束的泛型驱动代码跑通真实读路径。
//
//   Linux 侧后端 : EpollBackend —— epoll 是就绪式(Reactor),想满足完成式接口的约定,
//                  必须由适配层在就绪到来时亲手 read,把「就绪」翻译成「完成」。
//                  EPOLLONESHOT 把一次注册对齐成一次完成,重投用 MOD 再武装。
//   Win   侧后端 : IocpBackend —— IOCP 本身就是完成式(Proactor),submit 直接
//                  ReadFile+OVERLAPPED,wait 就是 GQCS,不需要翻译层。
//
// 编译: g++ -std=c++20 -O2 -Wall -Wextra [-D_FORTIFY_SOURCE=2]
//   Linux: g++ (GCC) 16.2.1 (WSL2 6.18.33.2)
//   Win  : MSYS2 UCRT64 g++ (GCC) 16.1.0 (Win11 26200)
// 同一份源文件两侧编译均零警告、同构输出。

#include <cerrno>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <list>
#include <optional>
#include <string_view>
#include <unordered_map>

// ---------------------------------------------------------------------------
// 平台无关部分: 完成事件 / 读请求 / concept / 受约束的泛型驱动
// ---------------------------------------------------------------------------

// 统一的完成事件。三个来源各出一半力气拼出这三个字段:
//   id    —— epoll 侧是适配层自派的编号; IOCP 侧藏在 OVERLAPPED 扩展里回来;
//            io_uring 侧就是 SQE 的 user_data 原样回来。
//   bytes —— epoll 侧是适配层亲手 read 的返回值; IOCP/io_uring 侧由内核填好带回。
//   error —— errno / GetLastError(), 0 为成功。
struct completion {
    std::uint64_t id;
    std::size_t bytes;
    int error;
};

// 统一的读请求。target 两侧含义不同(fd / HANDLE),用整数宽度承载。
// offset 对文件语义必填(io_uring READ 与 ReadFile+OVERLAPPED 都要显式偏移),
// 对流设备(管道/socket)忽略 —— epoll 侧只有流,这个字段用不上。
struct read_request {
    std::uint64_t id;
    std::uintptr_t target;
    void* buf;
    std::size_t len;
    unsigned long long offset;
};

// 最小的完成式后端接口约定。只约束三件套的形状,不管每个成员怎么实现。
template <typename B>
concept AsyncBackend = requires(B& b, typename B::request r, int timeout_ms) {
    typename B::completion;
    typename B::request;
    { b.submit(r) } -> std::same_as<bool>;                               // 交出请求
    { b.wait(timeout_ms) } -> std::same_as<int>;                         // 等完成进队
    { b.take() } -> std::same_as<std::optional<typename B::completion>>; // 逐个取走
};

// 受约束的泛型驱动: 这段代码两侧一字不改。
// 一次完整的「提交-等待-收割」,返回是否拿到成功完成。
template <AsyncBackend B> bool drive_one_round(B& backend, typename B::request r,
                                               std::size_t expect_bytes,
                                               std::string_view expect_data, std::string_view tag) {
    if (!backend.submit(r)) {
        std::printf("[%s] submit 失败\n", tag.data());
        return false;
    }
    int n = backend.wait(2000);
    auto c = backend.take();
    if (!c) {
        std::printf("[%s] wait=%d, take: 无完成\n", tag.data(), n);
        return false;
    }
    std::printf("[%s] backend=%s wait=%d take: id=%llu err=%d bytes=%zu data='%.*s'\n", tag.data(),
                B::name, n, static_cast<unsigned long long>(c->id), c->error, c->bytes,
                static_cast<int>(c->bytes), static_cast<const char*>(r.buf));
    return c->error == 0 && c->bytes == expect_bytes &&
           std::memcmp(r.buf, expect_data.data(), expect_bytes) == 0;
}

// ---------------------------------------------------------------------------
// Linux 侧: EpollBackend(就绪式到完成式的适配层)
// ---------------------------------------------------------------------------
#ifdef __linux__

#    include <fcntl.h>
#    include <sys/epoll.h>
#    include <unistd.h>
// errno 已在平台无关区引入 <cerrno>

class EpollBackend {
  public:
    using completion = ::completion;
    using request = ::read_request;
    static constexpr char name[] = "epoll";

    EpollBackend() : epfd_(::epoll_create1(EPOLL_CLOEXEC)) {}
    ~EpollBackend() {
        if (epfd_ >= 0)
            ::close(epfd_);
    }
    EpollBackend(const EpollBackend&) = delete;
    EpollBackend& operator=(const EpollBackend&) = delete;

    // 武装一次兴趣。ONESHOT: 一个事件之后兴趣自动解除,
    // 「一次 submit 恰好对应一次完成」的语义就立在它上面。
    bool submit(request r) {
        ::epoll_event ev{};
        ev.events = EPOLLIN | EPOLLONESHOT;
        ev.data.u64 = r.target;
        int op = armed_.count(r.target) ? EPOLL_CTL_MOD : EPOLL_CTL_ADD;
        if (::epoll_ctl(epfd_, op, static_cast<int>(r.target), &ev) != 0)
            return false;
        armed_[r.target] = true;
        inflight_[r.target] = r;
        return true;
    }

    // 就绪到来时,适配层亲手搬运,把「可读了」翻译成「读到了」。
    int wait(int timeout_ms) {
        ::epoll_event evs[8];
        int n = ::epoll_wait(epfd_, evs, 8, timeout_ms);
        int got = 0;
        for (int i = 0; i < n; ++i) {
            auto it = inflight_.find(evs[i].data.u64);
            if (it == inflight_.end())
                continue; // 已撤单的兴趣,跳过
            request r = it->second;
            completion c{r.id, 0, 0};
            ssize_t nb = ::read(static_cast<int>(r.target), r.buf, r.len);
            if (nb < 0) {
                c.error = errno;
            } else {
                c.bytes = static_cast<std::size_t>(nb);
            }
            done_.push_back(c);
            ++got;
        }
        return got;
    }

    std::optional<completion> take() {
        if (done_.empty())
            return std::nullopt;
        completion c = done_.front();
        done_.pop_front();
        return c;
    }

  private:
    int epfd_;
    std::unordered_map<std::uintptr_t, bool> armed_;
    std::unordered_map<std::uintptr_t, request> inflight_;
    std::deque<completion> done_;
};

static_assert(AsyncBackend<EpollBackend>,
              "EpollBackend 必须满足 AsyncBackend —— 适配层把就绪式掰成完成式");

int main() {
    EpollBackend backend;

    int fds[2];
    if (::pipe2(fds, O_NONBLOCK | O_CLOEXEC) != 0) {
        std::perror("pipe2");
        return 1;
    }

    unsigned char buf[16] = {};
    bool ok = true;

    ::ssize_t unused = ::write(fds[1], "hello", 5);
    (void)unused;
    read_request r1{1, static_cast<std::uintptr_t>(fds[0]), buf, 5, 0};
    ok = drive_one_round(backend, r1, 5, std::string_view("hello", 5), "轮1") && ok;

    // 第二轮: ONESHOT 之后兴趣已解除,重投走 MOD 再武装。
    unused = ::write(fds[1], "cross", 5);
    (void)unused;
    read_request r2{2, static_cast<std::uintptr_t>(fds[0]), buf, 5, 0};
    ok = drive_one_round(backend, r2, 5, std::string_view("cross", 5), "轮2") && ok;

    ::close(fds[0]);
    ::close(fds[1]);
    std::printf("%s: 两轮完成事件与数据全部对上\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}

#endif // __linux__

// ---------------------------------------------------------------------------
// Windows 侧: IocpBackend(完成式本体,不需要翻译)
// ---------------------------------------------------------------------------
#ifdef _WIN32

#    include <windows.h>

class IocpBackend {
  public:
    using completion = ::completion;
    using request = ::read_request;
    static constexpr char name[] = "iocp";

    IocpBackend() : port_(::CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0)) {}
    ~IocpBackend() {
        if (port_)
            ::CloseHandle(port_);
    }
    IocpBackend(const IocpBackend&) = delete;
    IocpBackend& operator=(const IocpBackend&) = delete;

    // 句柄挂端口。key 是句柄级的,一发一发的心思不靠它,靠 OVERLAPPED 扩展。
    bool attach(std::uintptr_t handle, std::uint64_t key) {
        return ::CreateIoCompletionPort(reinterpret_cast<HANDLE>(handle), port_,
                                        static_cast<ULONG_PTR>(key), 0) == port_;
    }

    bool submit(request r) {
        // OVERLAPPED 的生命周期必须覆盖在途全程,完成取走前不许动它。
        inflight_.push_back(ovx{});
        ovx& x = inflight_.back();
        x.ov.Offset = static_cast<DWORD>(r.offset & 0xFFFFFFFFull);
        x.ov.OffsetHigh = static_cast<DWORD>(r.offset >> 32);
        x.id = r.id;
        BOOL ok = ::ReadFile(reinterpret_cast<HANDLE>(r.target), r.buf, static_cast<DWORD>(r.len),
                             nullptr, &x.ov);
        // 句柄带 FILE_FLAG_OVERLAPPED: FALSE+ERROR_IO_PENDING 是正常在途;
        // TRUE(缓存命中等同步完成)也会照常投完成包,GQCS 照样收得到。
        if (!ok && ::GetLastError() != ERROR_IO_PENDING) {
            inflight_.pop_back();
            return false;
        }
        return true;
    }

    int wait(int timeout_ms) {
        DWORD bytes = 0;
        ULONG_PTR key = 0;
        LPOVERLAPPED pov = nullptr;
        BOOL ok =
            ::GetQueuedCompletionStatus(port_, &bytes, &key, &pov, static_cast<DWORD>(timeout_ms));
        if (pov == nullptr)
            return 0; // 超时,没有包
        completion c{0, static_cast<std::size_t>(bytes), 0};
        if (!ok)
            c.error = static_cast<int>(::GetLastError());
        auto it = find_by_ov(pov);
        if (it != inflight_.end()) {
            c.id = it->id;
            inflight_.erase(it); // 完成取走,OVERLAPPED 这才可以回收
        }
        done_.push_back(c);
        return 1;
    }

    std::optional<completion> take() {
        if (done_.empty())
            return std::nullopt;
        completion c = done_.front();
        done_.pop_front();
        return c;
    }

  private:
    struct ovx {
        OVERLAPPED ov;
        std::uint64_t id;
    };
    std::list<ovx>::iterator find_by_ov(OVERLAPPED* pov) {
        for (auto it = inflight_.begin(); it != inflight_.end(); ++it)
            if (&it->ov == pov)
                return it;
        return inflight_.end();
    }

    HANDLE port_;
    std::list<ovx> inflight_;
    std::deque<completion> done_;
};

static_assert(AsyncBackend<IocpBackend>,
              "IocpBackend 必须满足 AsyncBackend —— 完成式本体直录 submit/wait/take");

int main() {
    IocpBackend backend;

    // 造一个数据文件,内容 20 字节: "hello cross-platform"
    const wchar_t* path = L"e1_iocp_data.txt";
    HANDLE w = ::CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                             nullptr);
    if (w == INVALID_HANDLE_VALUE) {
        std::printf("造数据文件失败 gle=%lu\n", ::GetLastError());
        return 1;
    }
    DWORD wr = 0;
    ::WriteFile(w, "hello cross-platform", 20, &wr, nullptr);
    ::CloseHandle(w);

    HANDLE h = ::CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                             FILE_FLAG_OVERLAPPED, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        std::printf("开异步句柄失败 gle=%lu\n", ::GetLastError());
        return 1;
    }
    if (!backend.attach(reinterpret_cast<std::uintptr_t>(h), 7)) {
        std::printf("挂端口失败 gle=%lu\n", ::GetLastError());
        return 1;
    }

    unsigned char buf[16] = {};
    bool ok = true;

    read_request r1{1, reinterpret_cast<std::uintptr_t>(h), buf, 5, 0};
    ok = drive_one_round(backend, r1, 5, std::string_view("hello", 5), "轮1") && ok;

    // 第二轮: 换偏移 6 再读 5 字节,文件语义下偏移是请求自带的书签。
    read_request r2{2, reinterpret_cast<std::uintptr_t>(h), buf, 5, 6};
    ok = drive_one_round(backend, r2, 5, std::string_view("cross", 5), "轮2") && ok;

    ::CloseHandle(h);
    ::DeleteFileW(path);
    std::printf("%s: 两轮完成事件与数据全部对上\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}

#endif // _WIN32
