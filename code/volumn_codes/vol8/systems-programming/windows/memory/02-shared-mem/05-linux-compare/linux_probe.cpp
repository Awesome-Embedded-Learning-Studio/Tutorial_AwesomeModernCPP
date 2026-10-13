// linux_probe.cpp —— E5 对照表的 Linux 侧证据:shm_open 的名字/生命周期/对齐/撞名
//
// 编译(WSL2 本机 g++,与 Windows 侧同一台物理机):
//   g++ -std=c++20 -O2 -Wall -Wextra linux_probe.cpp -o linux_probe
// 运行:
//   ./linux_probe
//
// 观察点(每一行都对应 Windows 侧的某个实验,进对照表):
//   (1) 名字住在哪:/dev/shm/sysprog_e5_<pid> 是一个真文件,stat 得到;
//       Windows 的名字住在内核对象命名空间,文件系统里看不见
//   (2) 撞名语义:shm_open(O_CREAT|O_EXCL) 第二次 → -1 EEXIST;不带 O_EXCL 的第二次
//       → 拿到同一对象的另一个 fd,毫无表示 —— "是不是我新建的"要靠 O_EXCL 问;
//       Windows 是 CreateFileMappingW 也能打开,但 GetLastError 会答 183
//   (3) 大小谁定:ftruncate 显式定尺寸(Windows 在 CreateFileMapping 的参数里定)
//   (4) 偏移对齐:mmap offset=4096+8(不是页的倍数)→ -1 EINVAL;
//       Windows MapViewOfFile 的 offset 要 64KB 对齐(err=1132),粗 16 倍
//   (5) 生命周期:shm_unlink 是显式的,与引用无关 —— unlink 后名字立刻没了
//       (再 open 得 ENOENT),但 mmap 还在、照常读写,对象活到 munmap;
//       Windows 没有 unlink,名字跟着最后一个句柄隐式消亡(E1/E2 实测)

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

int main() {
    long page = sysconf(_SC_PAGESIZE);
    char name[64], path[96];
    snprintf(name, sizeof name, "/sysprog_e5_%d", (int)getpid());
    snprintf(path, sizeof path, "/dev/shm/sysprog_e5_%d", (int)getpid());
    printf("[Linux 侧] pid=%d 页大小=%ld 名字=%s(POSIX shm 名字空间)\n", (int)getpid(), page, name);

    // (1) 创建 + 名字是个真文件
    int fd = shm_open(name, O_CREAT | O_RDWR | O_EXCL, 0600);
    printf("  shm_open(O_CREAT|O_RDWR|O_EXCL) -> fd=%d errno=%d(%s)\n", fd, errno, strerror(errno));
    struct stat st{};
    int sr = stat(path, &st);
    printf("  stat(%s) -> %d size=%lld —— 名字就是 /dev/shm 里一个真文件\n", path, sr,
           (long long)st.st_size);

    // (2) 撞名:O_EXCL 的第二次 vs 不带 O_EXCL 的第二次
    errno = 0;
    int fd2 = shm_open(name, O_CREAT | O_RDWR | O_EXCL, 0600);
    printf("  shm_open(第二次,带 O_EXCL)  -> fd=%d errno=%d(%s)\n", fd2, errno, strerror(errno));
    errno = 0;
    int fd3 = shm_open(name, O_RDWR, 0600);
    printf(
        "  shm_open(第二次,不带 O_EXCL)-> fd=%d errno=%d —— 同一对象另一个 fd,没有\"已存在\"信号\n",
        fd3, errno);
    close(fd3);

    // (3) 大小:ftruncate 显式定
    ftruncate(fd, 256 * 1024);
    stat(path, &st);
    printf("  ftruncate(256KiB) 后 stat size=%lld —— 大小是 ftruncate 显式给的"
           "(Windows 在 CreateFileMapping 参数里给)\n",
           (long long)st.st_size);
    unsigned char* p =
        (unsigned char*)mmap(nullptr, 256 * 1024, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    printf("  mmap(MAP_SHARED) -> %p\n", p);

    // (4) 偏移对齐:offset 不是页的倍数
    errno = 0;
    void* mis = mmap(nullptr, 8192, PROT_READ | PROT_WRITE, MAP_SHARED, fd, page + 8);
    printf("  mmap(offset=页+8) -> %p errno=%d(%s) —— offset 必须按页对齐(Windows 要 64KB)\n", mis,
           errno, strerror(errno));

    // (5) unlink 显式摘名,映射续命
    memset(p, 0xA5, 4096);
    errno = 0;
    int ur = shm_unlink(name);
    printf("  shm_unlink(%s) -> %d%s 名字显式摘除,与还有多少映射/多少 fd 无关\n", name, ur,
           ur == 0 ? "" : "(errno 只在 -1 时才有意义)");
    errno = 0;
    int fd4 = shm_open(name, O_RDWR, 0600);
    printf("  unlink 后再 shm_open -> fd=%d errno=%d(%s) —— 名字没了\n", fd4, errno,
           strerror(errno));
    unsigned ok = 0;
    for (int i = 0; i < 4096; ++i) {
        ok += (p[i] == 0xA5);
    }
    memset(p, 0x5A, 4096);
    printf("  unlink 后旧映射读写:回读 %u/4096 相符,再写 4096 字节也成功 —— 对象活到 munmap\n", ok);
    munmap(p, 256 * 1024);
    close(fd);
    printf("[Linux 侧] 完\n");
    return 0;
}
