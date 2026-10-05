// E1:shm_open 命名对象全生命周期
// 观察点:名字空间在 /dev/shm、新对象尺寸为 0、O_EXCL 报 EEXIST、
//        unlink 后映射仍活(名字没了,对象活到最后一关)、unlink 后同名重建 → 新旧两个对象互不相干
// 附:mode 会被 umask 截断
// 编译:g++ -std=c++20 -Wall -Wextra -O2 e1_lifecycle.cpp -o e1_lifecycle
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {

const char* kName = "/lm03_demo";
constexpr size_t kSize = 64 * 1024;

void shell(const char* cmd) {
    std::printf("$ %s\n", cmd);
    std::fflush(stdout);
    if (std::system(cmd) != 0) {
        std::printf("  (命令返回非 0)\n");
    }
    std::fflush(stdout);
}

void dump_fd_link(int fd) {
    char path[64], target[256];
    std::snprintf(path, sizeof path, "/proc/self/fd/%d", fd);
    ssize_t n = ::readlink(path, target, sizeof target - 1);
    if (n < 0) {
        std::printf("  readlink(%s) 失败:%s\n", path, std::strerror(errno));
        return;
    }
    target[n] = '\0';
    std::printf("  readlink(%s) -> %s\n", path, target);
}

} // namespace

int main() {
    shm_unlink(kName); // 干净起点:残留就删,第一次跑时报 ENOENT 属预期,忽略
    std::printf("== E1 shm_open 命名对象全生命周期 ==\n\n");
    shell("ls -l /dev/shm");

    // [1] 首次创建
    errno = 0;
    int fd = shm_open(kName, O_RDWR | O_CREAT | O_EXCL, 0600);
    if (fd < 0) {
        std::printf("shm_open 创建失败:%s\n", std::strerror(errno));
        return 1;
    }
    std::printf("[1] shm_open(\"%s\", O_CREAT|O_EXCL|O_RDWR, 0600) 成功,fd=%d\n", kName, fd);

    struct stat st{};
    if (fstat(fd, &st) != 0) {
        perror("fstat");
        return 1;
    }
    std::printf("    刚创建时 fstat:size=%lld mode=%04o(新对象尺寸为 0,不 ftruncate 直接 mmap 会 "
                "EINVAL/长度 0)\n",
                static_cast<long long>(st.st_size), st.st_mode & 0777);

    // [2] 定尺寸
    if (ftruncate(fd, static_cast<off_t>(kSize)) != 0) {
        perror("ftruncate");
        return 1;
    }
    if (fstat(fd, &st) != 0) {
        perror("fstat");
        return 1;
    }
    std::printf("[2] ftruncate(fd, %zu) 后:size=%lld\n", kSize, static_cast<long long>(st.st_size));

    // [3] 映射 + 写入第一代内容
    void* p = mmap(nullptr, kSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (p == MAP_FAILED) {
        perror("mmap");
        return 1;
    }
    auto* gen1 = static_cast<unsigned long long*>(p);
    gen1[0] = 0x1111ULL;
    std::strcpy(reinterpret_cast<char*>(&gen1[2]), "generation-1");
    std::printf("[3] mmap MAP_SHARED 完成,写入第一代标记 0x%llx + 文本 \"%s\"\n",
                static_cast<unsigned long long>(gen1[0]), reinterpret_cast<char*>(&gen1[2]));
    shell("ls -l /dev/shm");

    // [4] 名字被占用:不 unlink 再来一次 O_CREAT|O_EXCL → EEXIST
    errno = 0;
    int fd2 = shm_open(kName, O_RDWR | O_CREAT | O_EXCL, 0600);
    std::printf("[4] 未 unlink,再次 shm_open 同名 O_CREAT|O_EXCL → 返回 %d,errno=%d (%s)\n", fd2,
                errno, std::strerror(errno));
    if (fd2 >= 0)
        close(fd2);

    // [5] 不带 O_CREAT 打开:同一个对象,第二个映射能看到第一代内容
    errno = 0;
    int fd3 = shm_open(kName, O_RDWR, 0);
    if (fd3 < 0) {
        std::printf("[5] shm_open(name, O_RDWR) 失败:%s\n", std::strerror(errno));
        return 1;
    }
    void* p3 = mmap(nullptr, kSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd3, 0);
    if (p3 == MAP_FAILED) {
        perror("mmap fd3");
        return 1;
    }
    auto* view2 = static_cast<unsigned long long*>(p3);
    std::printf("[5] 第二个进程视角:shm_open(name, O_RDWR) 不带 O_CREAT 也成功(fd=%d),映射后读到 "
                "0x%llx \"%s\"——同一实体\n",
                fd3, static_cast<unsigned long long>(view2[0]), reinterpret_cast<char*>(&view2[2]));
    munmap(p3, kSize);
    close(fd3);

    // [6] unlink:名字消失
    if (shm_unlink(kName) != 0) {
        perror("shm_unlink");
        return 1;
    }
    std::printf("[6] shm_unlink(\"%s\") 返回 0,名字没了\n", kName);
    dump_fd_link(fd);
    shell("ls -l /dev/shm");

    // [7] unlink 后已映射的视图仍有效
    std::printf("[7] unlink 后旧映射:仍读到 0x%llx \"%s\"\n",
                static_cast<unsigned long long>(gen1[0]), reinterpret_cast<char*>(&gen1[2]));
    gen1[1] = 42;
    std::printf("    继续写 gen1[1]=%llu 也成功——unlink 只删名字,实体活到所有映射/fd 关闭\n",
                static_cast<unsigned long long>(gen1[1]));

    // [8] 名字已释放:同名 O_CREAT|O_EXCL 现在能成功,新旧对象互不相干
    errno = 0;
    int fd4 = shm_open(kName, O_RDWR | O_CREAT | O_EXCL, 0600);
    if (fd4 < 0) {
        std::printf("[8] unlink 后同名重建失败:%s\n", std::strerror(errno));
        return 1;
    }
    if (ftruncate(fd4, static_cast<off_t>(kSize)) != 0) {
        perror("ftruncate fd4");
        return 1;
    }
    void* q = mmap(nullptr, kSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd4, 0);
    if (q == MAP_FAILED) {
        perror("mmap fd4");
        return 1;
    }
    auto* gen2 = static_cast<unsigned long long*>(q);
    gen2[0] = 0x2222ULL;
    std::strcpy(reinterpret_cast<char*>(&gen2[2]), "generation-2");
    std::printf("[8] unlink 后同名 O_CREAT|O_EXCL 重建成功(fd=%d),写入 0x2222 \"generation-2\"\n",
                fd4);
    std::printf("    同一时刻旧映射看到的还是 0x%llx \"%s\"——同名的两个对象互不相干,证明 unlink "
                "后重建的是新实体\n",
                static_cast<unsigned long long>(gen1[0]), reinterpret_cast<char*>(&gen1[2]));

    // 收尾
    munmap(q, kSize);
    close(fd4);
    shm_unlink(kName);
    munmap(p, kSize);
    close(fd);
    std::printf("\n[9] 收尾:新对象 unlink、旧映射 munmap,进程正常退出\n");

    // 附:umask 截断 mode
    std::printf("\n== 附:shm_open 的 mode 会被进程 umask 截断 ==\n");
    mode_t old = umask(0022);
    int fa = shm_open("/lm03_umask022", O_RDWR | O_CREAT | O_EXCL, 0666);
    umask(0000);
    int fb = shm_open("/lm03_umask000", O_RDWR | O_CREAT | O_EXCL, 0666);
    umask(old);
    struct stat sa{}, sb{};
    if (fstat(fa, &sa) == 0 && fstat(fb, &sb) == 0) {
        std::printf("都请求 0666:umask=0022 时实际落盘 %04o;umask=0000 时实际 %04o\n",
                    sa.st_mode & 0777, sb.st_mode & 0777);
    }
    shell("ls -l /dev/shm");
    close(fa);
    close(fb);
    shm_unlink("/lm03_umask022");
    shm_unlink("/lm03_umask000");
    std::printf("(umask 演示对象已清理)\n");
    return 0;
}
