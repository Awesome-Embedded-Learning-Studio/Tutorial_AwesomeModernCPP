// E5: 可见性与耐久性分开看(数据文件在 ext4 上,/tmp 是 tmpfs 不能用)
// 128 MiB 文件 + MAP_SHARED:每页写 1 字节制造 32768 个脏页,
// /proc/meminfo 的 Dirty 应涨约 131072 kB;此时另一个 fd 的 pread 已能读到新值
// (可见 ≠ 已写回);msync(MS_SYNC) 后 Dirty 落回基线。
// 再补一段:只写 1 MiB 不 msync,Dirty 小幅回涨,pread 依旧立刻可见。
#include "article.hpp"

#include <chrono>
#include <fstream>
#include <print>
#include <string>

namespace {

unsigned long meminfo_kb(const char* key) // 读 /proc/meminfo 某字段的 kB 值
{
    std::ifstream f{"/proc/meminfo"};
    std::string name;
    unsigned long kb = 0;
    while (f >> name >> kb) {
        if (name == key) {
            return kb;
        }
        f.ignore(256, '\n');
    }
    return 0;
}

void report(const char* when)
{
    std::print("{:<26} Dirty = {:>8} kB, Writeback = {:>6} kB\n",
               when, meminfo_kb("Dirty:"), meminfo_kb("Writeback:"));
}

} // namespace

int main()
{
    const char* path = "/home/charliechen/l02_scratch/dirty.bin";
    constexpr std::size_t kLen = 128u << 20; // 128 MiB
    const std::size_t page = static_cast<std::size_t>(::sysconf(_SC_PAGE_SIZE));
    const std::size_t pages = kLen / page;

    unique_fd fd{sys_call("open", ::open, path, O_RDWR | O_CREAT | O_TRUNC, 0666)};
    sys_call("ftruncate", ::ftruncate, fd.get(), static_cast<off_t>(kLen));
    sys_call("fsync", ::fsync, fd.get());       // 文件尺寸元数据先落盘,起点干净
    unique_fd fd2{sys_call("open", ::open, path, O_RDONLY)}; // 独立的第二个 fd,只读旁观

    report("baseline");

    mapped_region region(fd, kLen, PROT_READ | PROT_WRITE, MAP_SHARED);

    unsigned char before = 0;
    sys_call("pread-hole", ::pread, fd2.get(), &before, 1,
             static_cast<off_t>(12345 * page)); // 洞里读出来是 0
    std::print("pread byte @page 12345 before write : {} (sparse hole reads as zero)\n",
               static_cast<int>(before));

    auto t0 = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < pages; ++i) {
        region.data()[i * page] = static_cast<unsigned char>(i); // 每页摸 1 字节,页页变脏
    }
    auto t1 = std::chrono::steady_clock::now();
    std::print("wrote 1 byte x {} pages in {:.1f} ms\n", pages,
               std::chrono::duration<double, std::milli>(t1 - t0).count());

    report("after dirtying");
    unsigned char seen = 0;
    sys_call("pread-1", ::pread, fd2.get(), &seen, 1, static_cast<off_t>(page)); // 页 1,值应为 1
    std::print("pread byte @page 1 via 2nd fd now : {} (expected 1, page index & 0xFF)\n",
               static_cast<int>(seen));
    sys_call("pread-2", ::pread, fd2.get(), &seen, 1,
             static_cast<off_t>(12345 * page));
    std::print("pread byte @page 12345 now        : {} (expected {}, i.e. 12345 & 0xFF)\n",
               static_cast<int>(seen), 12345 & 0xFF);

    auto t2 = std::chrono::steady_clock::now();
    sys_call("msync", ::msync, region.data(), region.size(), MS_SYNC);
    auto t3 = std::chrono::steady_clock::now();
    std::print("msync(MS_SYNC) over 128 MiB       : {:.1f} ms\n",
               std::chrono::duration<double, std::milli>(t3 - t2).count());
    report("after msync(MS_SYNC)");

    // 日常形态:只写 1 MiB、不 msync —— Dirty 小幅回涨,pread 立刻可见
    constexpr std::size_t kSmall = 1u << 20;
    for (std::size_t off = 0; off < kSmall; off += page) {
        region.data()[off] = 0xAB;
    }
    report("after +1 MiB, no msync");
    sys_call("pread-3", ::pread, fd2.get(), &seen, 1, 4096);
    std::print("pread byte @page 1 now            : {} (0xAB = 171, visible immediately)\n",
               static_cast<int>(seen));
    return 0; // ~mapped_region -> munmap;dirty.bin 留给 shell 清理
}
