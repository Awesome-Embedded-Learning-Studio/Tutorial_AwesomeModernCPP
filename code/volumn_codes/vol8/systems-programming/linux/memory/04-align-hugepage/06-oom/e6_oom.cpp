// E6: OOM Killer —— 只读观察 + 安全余量内的占用/回收链演示,绝不真触发系统 OOM
// 安全规则:先读 MemAvailable,只 touch 它的 60%,全程随时打印 MemAvailable,
// 用完立刻 free。绝不把系统逼到墙角。
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <string>
#include <unistd.h>

static long meminfo_kb(const char* key) {
    std::ifstream f("/proc/meminfo");
    std::string line;
    while (std::getline(f, line))
        if (line.rfind(key, 0) == 0) {
            long v = 0;
            std::sscanf(line.c_str() + std::strlen(key), " %ld", &v);
            return v;
        }
    return -1;
}

static int self_int(const char* what) {
    std::ifstream f(std::string("/proc/self/") + what);
    int v = -1;
    f >> v;
    return v;
}

static void status_vm(const char* stage) {
    std::ifstream f("/proc/self/status");
    std::string line;
    std::string out;
    while (std::getline(f, line))
        if (line.rfind("VmRSS:", 0) == 0 || line.rfind("VmSize:", 0) == 0)
            out += "    " + line + "\n";
    std::printf("  [%s]\n%s    MemAvailable=%ld kB   oom_score=%d\n", stage, out.c_str(),
                meminfo_kb("MemAvailable:"), self_int("oom_score"));
}

static bool write_self(const char* file, const char* val) {
    std::string path = std::string("/proc/self/") + file;
    int fd = open(path.c_str(), O_WRONLY);
    if (fd < 0) {
        std::printf("  open(%s, W) -> errno=%d (%s)\n", path.c_str(), errno, std::strerror(errno));
        return false;
    }
    ssize_t n = write(fd, val, std::strlen(val));
    close(fd);
    std::printf("  write(%s, \"%s\") -> %zd %s\n", file, val, n, n > 0 ? "成功" : "失败");
    return n > 0;
}

int main() {
    std::printf("==== ① 只读观察 ====\n");
    std::printf("  /proc/self/oom_score     = %d (0-1000,越大越先被杀)\n", self_int("oom_score"));
    std::printf("  /proc/self/oom_score_adj = %d (基线 0)\n", self_int("oom_score_adj"));
    std::printf("  本机 MemTotal=%ld kB MemAvailable=%ld kB\n\n", meminfo_kb("MemTotal:"),
                meminfo_kb("MemAvailable:"));

    std::printf("==== ② oom_score_adj 可写性 ====\n");
    write_self("oom_score_adj", "250"); // 自己进程内写正值:非特权允许
    std::printf("  读回 oom_score_adj = %d\n", self_int("oom_score_adj"));
    write_self("oom_score_adj", "0"); // 恢复
    std::printf("  恢复后 oom_score_adj = %d\n", self_int("oom_score_adj"));
    errno = 0;
    bool neg = write_self("oom_score_adj", "-1000"); // 降到 0 以下需要 CAP_SYS_RESOURCE
    if (!neg)
        std::printf(
            "  负值写入被拒(errno=%d %s)—— 无特权只能把自己调得更\"该杀\",不能调得更\"免死\"\n",
            errno, std::strerror(errno));
    write_self("oom_score_adj", "0");

    std::printf("\n==== ③ 安全余量内的占用/回收链 ====\n");
    long avail = meminfo_kb("MemAvailable:");
    long target_kb = avail * 60 / 100; // 只碰 MemAvailable 的 60%
    std::printf("  MemAvailable=%ld kB -> 目标 touch=%ld kB(60%%),calloc+每页实写\n", avail,
                target_kb);
    status_vm("touch 前基线");

    size_t bytes = static_cast<size_t>(target_kb) * 1024;
    unsigned char* p = static_cast<unsigned char*>(std::calloc(bytes, 1));
    if (!p) {
        std::perror("calloc");
        return 1;
    }
    for (size_t i = 0; i < bytes; i += 4096)
        p[i] = 0xAA; // 首触,逼内核给物理页
    status_vm("touch 后:RSS 涨到目标量级,MemAvailable 同步下降");

    std::free(p);
    status_vm("free 后:RSS 落回,MemAvailable 回升 —— 占用/回收链条闭合");

    std::printf("\n口径:malloc/calloc 只记账虚拟;touch 才占物理;free 后 RSS 立落(大块走 mmap)。\n"
                "OOM 只发生在物理+swap 耗尽且无页可回收时,由内核按 oom_score 挑牺牲者。\n"
                "本实验刻意只动 MemAvailable 的 60%%,不做逼近上限的危险演示。\n");
    return 0;
}
