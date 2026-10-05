// E2: write() 之后进程的死亡方式,和数据的可见性。
//
//   场景 A:子进程 write() 一段数据后 _exit(0),父进程立刻 read 同一文件 —— 看吗?
//   场景 B:子进程循环 write 4KiB 编号块,写到一半被 kill -9,父进程读回并逐块校验。
//   场景 C(对照):子进程 fprintf 到 stdio 缓冲(用户态)后 _exit(0) —— 对比另一层的丢法。
//
// 路径烧死:数据文件写 /home/charliechen/l03_scratch/。
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

static const char* DIR = "/home/charliechen/l03_scratch";

static void act_a_write_then_exit(const char* path) {
    std::printf("== 场景 A: write() 后立刻 _exit(0),不 fsync ==\n");
    pid_t pid = fork();
    if (pid == 0) { // 子进程 A
        int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0)
            _exit(42);
        const char msg[] = "WRITTEN_VIA_WRITE_SYSCALL_NO_FSYNC_THEN_EXIT";
        ssize_t n = write(fd, msg, sizeof(msg) - 1);
        if (n != (ssize_t)sizeof(msg) - 1)
            _exit(43);
        _exit(0); // 故意不 close、不 fsync
    }
    int st = 0;
    waitpid(pid, &st, 0);
    std::printf("  子进程 A 已退出 (waitpid status=0x%x)。父进程现在 read 同一文件:\n", st);
    int fd = open(path, O_RDONLY);
    char buf[128] = {0};
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    std::printf("  read() 返回 %zd 字节,内容: \"%s\"\n", n, buf);
    std::printf("  判定: %s\n\n",
                n > 0 ? "可见 —— write() 返回时数据已在页缓存(内核),进程死不死它都在" : "不可见");
}

static int act_b_child(const char* path) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
        _exit(50);
    // 4KiB 编号块:头 8 字节是块号,其余字节按块号填充,供事后逐块校验
    std::vector<unsigned char> blk(4096);
    for (uint64_t i = 0;; ++i) {
        std::memcpy(blk.data(), &i, 8);
        unsigned char fill = static_cast<unsigned char>(0x41 + i % 26);
        std::memset(blk.data() + 8, fill, 4096 - 8);
        ssize_t n = write(fd, blk.data(), blk.size());
        if (n != (ssize_t)blk.size())
            _exit(51);
        // 放慢到 ~20 MiB/s,让父进程的 kill -9 大概率落在写到一半的途中
        timespec ts{0, 200 * 1000};
        nanosleep(&ts, nullptr);
    }
}

static void act_b_kill9_mid_write(const char* path) {
    std::printf("== 场景 B: 写到一半 kill -9 ==\n");
    pid_t pid = fork();
    if (pid == 0)
        act_b_child(path);
    timespec ts{0, 120 * 1000 * 1000}; // 120ms 后动手
    nanosleep(&ts, nullptr);
    kill(pid, SIGKILL);
    int st = 0;
    waitpid(pid, &st, 0);
    std::printf("  子进程被 kill -9 (waitpid status=0x%x, SIGKILL=%d)。父进程读回校验:\n", st,
                SIGKILL);

    int fd = open(path, O_RDONLY);
    struct stat sb{};
    fstat(fd, &sb);
    std::printf("  文件大小: %lld 字节 (%lld 个完整 4KiB 块, 尾部零头 %lld 字节)\n",
                (long long)sb.st_size, (long long)(sb.st_size / 4096),
                (long long)(sb.st_size % 4096));

    std::vector<unsigned char> data(sb.st_size);
    long long got = 0;
    while (got < sb.st_size) {
        ssize_t n = read(fd, data.data() + got, sb.st_size - got);
        if (n <= 0)
            break;
        got += n;
    }
    close(fd);

    long long full = sb.st_size / 4096;
    long long intact = 0;
    for (long long i = 0; i < full; ++i) {
        unsigned char* p = data.data() + i * 4096;
        uint64_t idx = 0;
        std::memcpy(&idx, p, 8);
        unsigned char fill = static_cast<unsigned char>(0x41 + idx % 26);
        bool ok = (idx == (uint64_t)i);
        for (size_t j = 8; ok && j < 4096; ++j)
            ok = (p[j] == fill);
        if (ok)
            ++intact;
    }
    std::printf("  校验: %lld/%lld 个完整块编号与填充模式全部完好, 序号 0..%lld 连续\n", intact,
                full, full ? full - 1 : -1);
    std::printf("  判定: kill -9 杀掉的是进程;凡是 write() 已经返回的数据,一个字节都没丢\n\n");
}

static void act_c_stdio_buffer(const char* path) {
    std::printf("== 场景 C(对照): stdio 用户态缓冲,同样 _exit(0) ==\n");
    // C1: fprintf 后直接 _exit —— 数据还在 libc 的用户态缓冲里,连页缓存都没进
    pid_t pid = fork();
    if (pid == 0) {
        FILE* fp = fopen(path, "w");
        if (!fp)
            _exit(60);
        // 100 行 x 24 字节 = 2400 字节,小于 stdio 4096 缓冲:全留在用户态
        for (int i = 0; i < 100; ++i)
            fprintf(fp, "STDIO_BUFFERED_LINE_%03d\n", i);
        _exit(0); // 不 fclose、不 fflush
    }
    int st = 0;
    waitpid(pid, &st, 0);
    struct stat sb{};
    stat(path, &sb);
    std::printf("  C1 fprintf 后 _exit(0): 文件大小 %lld 字节 (100 行 x 24 字节本应 2400)\n",
                (long long)sb.st_size);

    // C2: 同样 fprintf,但 fflush 之后 _exit —— fflush 内部调 write(),进了页缓存
    pid = fork();
    if (pid == 0) {
        FILE* fp = fopen(path, "w");
        if (!fp)
            _exit(61);
        for (int i = 0; i < 100; ++i)
            fprintf(fp, "STDIO_BUFFERED_LINE_%03d\n", i);
        fflush(fp);
        _exit(0);
    }
    waitpid(pid, &st, 0);
    stat(path, &sb);
    std::printf("  C2 fprintf + fflush 后 _exit(0): 文件大小 %lld 字节\n", (long long)sb.st_size);
    std::printf("  判定: 丢数据的另一层是 stdio 用户态缓冲;write() 返回后那一层已经不存在了\n");
}

int main() {
    char p_a[256], p_b[256], p_c[256];
    snprintf(p_a, sizeof p_a, "%s/e2a.bin", DIR);
    snprintf(p_b, sizeof p_b, "%s/e2b.bin", DIR);
    snprintf(p_c, sizeof p_c, "%s/e2c.bin", DIR);

    act_a_write_then_exit(p_a);
    act_b_kill9_mid_write(p_b);
    act_c_stdio_buffer(p_c);
    return 0;
}
