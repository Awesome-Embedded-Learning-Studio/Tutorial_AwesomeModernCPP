// E3:两条共享途径对照
//   命名对象:无关进程靠 /dev/shm 里的名字相认(server/client 两个独立进程)
//   匿名映射:MAP_SHARED|MAP_ANONYMOUS,只在 fork 继承链上传递
//   另附 SCM_RIGHTS 传 fd 一句话演示(细讲留给 ch03 IPC 篇)
// 用法:
//   e3_two_ways anon     —— fork 前共享映射:子写父读;对照:fork 后各自新建匿名映射,不相通
//   e3_two_ways server   —— 创建 /lm03_two,写消息,等客户端回 ack(独立进程)
//   e3_two_ways client   —— 按名字打开 /lm03_two,读消息,回 ack(独立进程)
//   e3_two_ways fdpass   —— shm 的 fd 经 UNIX socket sendmsg(SCM_RIGHTS)传给子进程
// 编译:g++ -std=c++20 -Wall -Wextra -O2 -pthread e3_two_ways.cpp -o e3_two_ways
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <string_view>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

namespace {

constexpr const char* kName = "/lm03_two";
constexpr size_t kPage = 4096;

struct Chan {
    std::atomic<uint32_t> msg_ready; // server 写
    std::atomic<uint32_t> ack;       // client 写
    char text[128];
};

void run_anon() {
    std::printf("== 途径 A:匿名 MAP_SHARED|MAP_ANONYMOUS(fork 前映射,子进程继承)==\n");
    auto* pre = static_cast<unsigned long long*>(
        mmap(nullptr, kPage, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0));
    if (pre == MAP_FAILED) {
        perror("mmap");
        return;
    }
    // fork 后各自再建一个匿名共享映射(不相通对照组)
    auto* post = static_cast<unsigned long long*>(
        mmap(nullptr, kPage, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0));
    if (post == MAP_FAILED) {
        perror("mmap post");
        return;
    }

    pid_t pid = fork();
    if (pid == 0) {
        pre[0] = 0xC0DE; // 写 fork 之前的映射
        auto* own = static_cast<unsigned long long*>(
            mmap(nullptr, kPage, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0));
        own[0] = 0xDEAD; // 写 fork 之后自己新建的映射
        _exit(0);
    }
    int st = 0;
    waitpid(pid, &st, 0);
    std::printf("父进程读 fork 前的共享映射:0x%llX(子进程写的,继承链通了)\n",
                static_cast<unsigned long long>(pre[0]));
    std::printf(
        "父进程读 fork 后自己建的匿名映射:0x%llX(子进程那份是另一个实体,匿名没有名字无从相认)\n",
        static_cast<unsigned long long>(post[0]));
    munmap(pre, kPage);
    munmap(post, kPage);
}

bool wait_flag(std::atomic<uint32_t>& f, double timeout_s) {
    struct timespec ts{0, 50 * 1000 * 1000}; // 50ms 让一步
    for (int i = 0; i < static_cast<int>(timeout_s * 20); ++i) {
        if (f.load(std::memory_order_acquire) == 1)
            return true;
        nanosleep(&ts, nullptr);
    }
    return f.load(std::memory_order_acquire) == 1;
}

void run_server() {
    shm_unlink(kName);
    int fd = shm_open(kName, O_RDWR | O_CREAT | O_EXCL, 0600);
    if (fd < 0) {
        perror("shm_open");
        return;
    }
    if (ftruncate(fd, static_cast<off_t>(kPage)) != 0) {
        perror("ftruncate");
        return;
    }
    auto* ch = static_cast<Chan*>(mmap(nullptr, kPage, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0));
    if (ch == MAP_FAILED) {
        perror("mmap");
        return;
    }
    new (ch) Chan{};
    std::snprintf(ch->text, sizeof ch->text, "hello-from-server-pid-%d",
                  static_cast<int>(getpid()));
    ch->msg_ready.store(1, std::memory_order_release);
    std::printf("[server pid=%d] shm_open 建好 %s,写消息并置 msg_ready,等 ack(10s 超时)...\n",
                static_cast<int>(getpid()), kName);
    if (!wait_flag(ch->ack, 10.0)) {
        std::printf("[server] 等 ack 超时\n");
    } else {
        std::printf("[server] 收到 ack,消息送达;unlink 收尾\n");
    }
    munmap(ch, kPage);
    close(fd);
    shm_unlink(kName);
}

void run_client() {
    int fd = -1;
    for (int i = 0; i < 100; ++i) {      // server 可能还没建好,按名字重试
        fd = shm_open(kName, O_RDWR, 0); // 注意:不带 O_CREAT,纯按名字找
        if (fd >= 0)
            break;
        usleep(100 * 1000);
    }
    if (fd < 0) {
        std::printf("[client pid=%d] 按名字找不到 %s:%s\n", static_cast<int>(getpid()), kName,
                    std::strerror(errno));
        return;
    }
    auto* ch = static_cast<Chan*>(mmap(nullptr, kPage, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0));
    if (ch == MAP_FAILED) {
        perror("mmap");
        return;
    }
    if (!wait_flag(ch->msg_ready, 10.0)) {
        std::printf("[client] 等 msg_ready 超时\n");
        return;
    }
    std::printf("[client pid=%d] 与 server 无亲缘,靠 shm_open(\"%s\") 相认,读到 \"%s\",回 ack\n",
                static_cast<int>(getpid()), kName, ch->text);
    ch->ack.store(1, std::memory_order_release);
    munmap(ch, kPage);
    close(fd);
}

void run_fdpass() {
    std::printf("== 附:匿名/任意 shm 实体的 fd 可经 SCM_RIGHTS 跨进程传递(ch03 细讲)==\n");
    int sv[2];
    if (socketpair(AF_UNIX, SOCK_DGRAM, 0, sv) != 0) {
        perror("socketpair");
        return;
    }
    shm_unlink("/lm03_fdpass");
    int fd = shm_open("/lm03_fdpass", O_RDWR | O_CREAT | O_EXCL, 0600);
    if (fd < 0) {
        perror("shm_open");
        return;
    }
    ftruncate(fd, static_cast<off_t>(kPage));
    auto* p = static_cast<char*>(mmap(nullptr, kPage, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0));
    if (p == MAP_FAILED) {
        perror("mmap");
        return;
    }
    std::strcpy(p, "hello-via-fd");

    pid_t pid = fork();
    if (pid == 0) {
        close(sv[0]);
        char data = 0;
        struct iovec io{&data, 1};
        struct msghdr mh{};
        char ctl[CMSG_SPACE(sizeof(int))];
        mh.msg_iov = &io;
        mh.msg_iovlen = 1;
        mh.msg_control = ctl;
        mh.msg_controllen = sizeof ctl;
        if (recvmsg(sv[1], &mh, 0) < 0) {
            perror("recvmsg");
            _exit(1);
        }
        int got = -1;
        for (struct cmsghdr* c = CMSG_FIRSTHDR(&mh); c != nullptr; c = CMSG_NXTHDR(&mh, c)) {
            if (c->cmsg_level == SOL_SOCKET && c->cmsg_type == SCM_RIGHTS) {
                std::memcpy(&got, CMSG_DATA(c), sizeof got);
            }
        }
        auto* q = static_cast<char*>(mmap(nullptr, kPage, PROT_READ, MAP_SHARED, got, 0));
        if (q == MAP_FAILED) {
            perror("child mmap");
            _exit(1);
        }
        char link[128];
        char path[64];
        std::snprintf(path, sizeof path, "/proc/self/fd/%d", got);
        ssize_t n = readlink(path, link, sizeof link - 1);
        if (n > 0)
            link[n] = '\0';
        std::printf(
            "子进程 recvmsg 拿到重复出来的新 fd=%d(%s),mmap 后读到 \"%s\"——fd 传到即实体传到\n",
            got, n > 0 ? link : "?", q);
        std::fflush(stdout); // _exit 不冲 stdio 缓冲,手动冲
        _exit(0);
    }
    close(sv[1]);
    char data = 'x';
    struct iovec io{&data, 1};
    struct msghdr mh{};
    char ctl[CMSG_SPACE(sizeof(int))];
    mh.msg_iov = &io;
    mh.msg_iovlen = 1;
    mh.msg_control = ctl;
    mh.msg_controllen = sizeof ctl;
    struct cmsghdr* c = CMSG_FIRSTHDR(&mh);
    c->cmsg_level = SOL_SOCKET;
    c->cmsg_type = SCM_RIGHTS;
    c->cmsg_len = CMSG_LEN(sizeof(int));
    std::memcpy(CMSG_DATA(c), &fd, sizeof fd);
    if (sendmsg(sv[0], &mh, 0) < 0) {
        perror("sendmsg");
    }
    int st = 0;
    waitpid(pid, &st, 0);
    close(sv[0]);
    munmap(p, kPage);
    close(fd);
    shm_unlink("/lm03_fdpass");
}

} // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IOLBF, 0); // 输出接管道(tee)时按行冲,保证多进程输出有序
    if (argc < 2) {
        std::fprintf(stderr, "用法:e3_two_ways <anon|server|client|fdpass>\n");
        return 2;
    }
    const std::string_view cmd = argv[1];
    if (cmd == "anon")
        run_anon();
    else if (cmd == "server")
        run_server();
    else if (cmd == "client")
        run_client();
    else if (cmd == "fdpass")
        run_fdpass();
    else {
        std::fprintf(stderr, "未知子命令 %s\n", argv[1]);
        return 2;
    }
    return 0;
}
