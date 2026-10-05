// seek_family.cpp —— lseek 的 whence 家族:SEEK_CUR/SEEK_END 的负偏移、管道上的 ESPIPE、
// 以及稀疏文件上的 SEEK_DATA/SEEK_HOLE 区间探测(《POSIX 文件 I/O》补课段 E3)
//
// 观察点(对照 seek_family.out):
//   a) 造稀疏文件:ftruncate 拉到 1 MiB,只在三处 pwrite 数据;
//      fstat 的 st_blocks*512 远小于 st_size —— 磁盘上没真占 1 MiB
//   b) SEEK_DATA/SEEK_HOLE 走完整个文件:data/hole 交替区间全部探测出来;
//      洞里读出来的是零页,不碰磁盘
//   c) ENXIO 的两种脸:全洞文件没有数据可找;从文件尾(及以后)出发找数据也没有
//   d) SEEK_END 负偏移:从尾部倒着数;SEEK_CUR 负偏移:游标回拨
//   e) 负的绝对位置被拒(EINVAL);越过文件尾的 seek 合法,文件在下次 write 才长大
//   f) 管道上 seek:五种 whence(SET/CUR/END/DATA/HOLE)全军覆没,errno=29(ESPIPE)
//
// SEEK_SET 的基本用法 L01 正文已讲过(偏移直接给、 whence 固定),这里只做增量,不重复。
#include "article.hpp"

#include <cstdio>
#include <cstring>

#include <sys/stat.h>
#include <sys/types.h>

namespace {

const char* sparse_path = "/home/charliechen/l01b_scratch/e3/sparse.bin";
const char* holeonly_path = "/home/charliechen/l01b_scratch/e3/holeonly.bin";
const char* tiny_path = "/home/charliechen/l01b_scratch/e3/tiny.txt";

void banner(const char* s) {
    std::printf("\n==== %s ====\n", s);
}

const char* whence_name(int w) {
    switch (w) {
        case SEEK_SET:
            return "SEEK_SET";
        case SEEK_CUR:
            return "SEEK_CUR";
        case SEEK_END:
            return "SEEK_END";
#ifdef SEEK_DATA
        case SEEK_DATA:
            return "SEEK_DATA";
#endif
#ifdef SEEK_HOLE
        case SEEK_HOLE:
            return "SEEK_HOLE";
#endif
        default:
            return "?";
    }
}

} // namespace

int main() {
    banner("a) 造一个稀疏文件:ftruncate 1 MiB + 三段 pwrite");
    {
        int fd = sys_call("open", ::open, sparse_path, O_RDWR | O_CREAT | O_TRUNC, 0666);
        sys_call("ftruncate", ::ftruncate, fd, 1L << 20);      // 先把尺寸拉到 1 MiB,中间不写
        sys_call("pwrite", ::pwrite, fd, "DATA@4K!", 8, 4096); // 8 字节小段
        char band[4096];
        std::memset(band, 'B', sizeof band);
        sys_call("pwrite", ::pwrite, fd, band, sizeof band, 68 * 1024); // 一整页的 B
        std::memset(band, 'Z', sizeof band);
        // 尾部 256 字节 Z,离 EOF 整整留一个块(4 KiB)——让文件以洞收尾,走出 ENXIO 分支
        sys_call("pwrite", ::pwrite, fd, band, 256, (1L << 20) - 4096 - 256);
        struct stat st{};
        sys_call("fstat", ::fstat, fd, &st);
        std::printf("st_size     = %ld 字节(1 MiB)\n", (long)st.st_size);
        std::printf("st_blocks   = %ld × 512 = %ld 字节  <- 磁盘实占,st_size 只是「账面」\n",
                    (long)st.st_blocks, (long)st.st_blocks * 512);

        banner("b) SEEK_DATA/SEEK_HOLE:把 data/hole 交替区间全部走一遍");
        // 算法:从 0 出发,SEEK_DATA 找下一段数据(ENXIO=后面全洞),
        //        再 SEEK_HOLE 找这段数据的结尾;循环到 EOF
        long pos = 0;
        long size = (long)st.st_size;
        while (pos < size) {
            off_t data = ::lseek(fd, pos, SEEK_DATA);
            if (data == -1 && errno == ENXIO) {
                std::printf("hole  [%7ld, %7ld)  长度 %7ld(尾部全洞,SEEK_DATA 报 ENXIO)\n", pos,
                            size, size - pos);
                break;
            }
            if (data > pos)
                std::printf("hole  [%7ld, %7ld)  长度 %7ld\n", pos, (long)data, (long)data - pos);
            off_t hole = sys_call("lseek(SEEK_HOLE)", ::lseek, fd, data, SEEK_HOLE);
            std::printf("data  [%7ld, %7ld)  长度 %7ld\n", (long)data, (long)hole,
                        (long)hole - (long)data);
            pos = (long)hole;
        }
        // 洞读出来是什么:从偏移 2048 读 8 字节,应为全零
        char zeros[8];
        ssize_t n = sys_call("pread", ::pread, fd, zeros, 8, 2048);
        std::printf("洞里 pread(2048, 8) = %ld 字节:", (long)n);
        for (ssize_t i = 0; i < n; ++i)
            std::printf(" %02x", (unsigned char)zeros[i]);
        std::printf("  <- 零页,内核现造的,不占磁盘\n");
        std::printf(
            "细看两处:8 字节的 pwrite 报成了 4096 字节的 data,尾部 256 字节 Z 同样被撑到块边界\n");
        std::printf("          —— SEEK_DATA/SEEK_HOLE 的精度是文件系统块(这里 4 "
                    "KiB),小于一个块的洞根本不存在\n");

        banner("c) ENXIO 的两种脸");
        int hf = sys_call("open", ::open, holeonly_path, O_RDWR | O_CREAT | O_TRUNC, 0666);
        sys_call("ftruncate", ::ftruncate, hf, 8192); // 纯洞文件:有尺寸,没数据
        off_t r = ::lseek(hf, 0, SEEK_DATA);
        std::printf("全洞文件 lseek(0, SEEK_DATA)      = %ld, errno=%d(%s)\n", (long)r, errno,
                    errno == ENXIO ? "ENXIO" : std::strerror(errno));
        r = ::lseek(fd, (off_t)size, SEEK_DATA); // 从文件尾出发
        std::printf("从文件尾 lseek(%ld, SEEK_DATA)  = %ld, errno=%d(%s)\n", size, (long)r, errno,
                    errno == ENXIO ? "ENXIO" : std::strerror(errno));
        ::close(hf);
        ::close(fd);
    }

    banner("d) SEEK_END/SEEK_CUR 的负偏移:倒着数、游标回拨");
    {
        int fd = sys_call("open", ::open, tiny_path, O_RDWR | O_CREAT | O_TRUNC, 0666);
        sys_call("write", ::write, fd, "0123456789", 10);
        char buf[8];
        off_t p = sys_call("lseek", ::lseek, fd, -4, SEEK_END); // 尾部倒数第 4
        ssize_t n = sys_call("read", ::read, fd, buf, 4);
        std::printf("lseek(-4, SEEK_END) = %ld, read = \"%.*s\"\n", (long)p, (int)n, buf);
        sys_call("lseek", ::lseek, fd, 0, SEEK_SET);
        n = sys_call("read", ::read, fd, buf, 2); // 游标到 2
        std::printf("先读 %ld 字节(\"%.*s\"),游标在 2;lseek(-1, SEEK_CUR) = %ld,", (long)n, (int)n,
                    buf, (long)sys_call("lseek", ::lseek, fd, -1, SEEK_CUR));
        n = sys_call("read", ::read, fd, buf, 2);
        std::printf("接着读 \"%.*s\"  <- 回拨一格重读了下标 1\n", (int)n, buf);

        banner("e) 负的绝对位置被拒;越过文件尾合法,write 之前文件不长大");
        off_t bad = ::lseek(fd, -1, SEEK_SET);
        std::printf("lseek(-1, SEEK_SET)  = %ld, errno=%d(%s)  <- 结果为负,直接拒\n", (long)bad,
                    errno, errno == EINVAL ? "EINVAL" : std::strerror(errno));
        off_t far_ = sys_call("lseek", ::lseek, fd, 100, SEEK_END); // 越过尾 100 字节
        struct stat st{};
        sys_call("fstat", ::fstat, fd, &st);
        std::printf("lseek(+100, SEEK_END)= %ld,此时 st_size 仍是 %ld  <- 洞已定位,字节未落盘\n",
                    (long)far_, (long)st.st_size);
        ::close(fd);
    }

    banner("f) 管道上 seek:ESPIPE");
    {
        int fds[2];
        sys_call("pipe", ::pipe, fds);
        const int whences[] = {SEEK_SET, SEEK_CUR, SEEK_END, SEEK_DATA, SEEK_HOLE};
        for (int w : whences) {
            off_t r = ::lseek(fds[0], 0, w);
            std::printf("lseek(读端, 0, %-9s) = %3ld, errno=%d", whence_name(w), (long)r, errno);
            if (r == -1)
                std::printf("(%s)", errno == ESPIPE ? "ESPIPE" : std::strerror(errno));
            std::printf("\n");
        }
        std::printf("管道是流,没有「位置」可言——想定位的都回去找普通文件\n");
        ::close(fds[0]);
        ::close(fds[1]);
    }
    return 0;
}
