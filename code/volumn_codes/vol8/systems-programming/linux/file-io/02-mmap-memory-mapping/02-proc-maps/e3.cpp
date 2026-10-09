// E3: /proc/self/maps 观察法
// 同一文件建两个视图:MAP_SHARED(offset 0)与 MAP_PRIVATE(offset 8 KiB),
// 按路径过滤打印自己那两行;再 munmap 掉 SHARED,证明「映射建立只多一行」。
// 顺带指认 [heap] 与 [stack]。全程单线程,maps 内容静止,可放心逐行读。
#include "article.hpp"

#include <fstream>
#include <print>
#include <string>
#include <vector>

namespace {

struct map_line {
    std::string whole;
    std::uintptr_t lo = 0, hi = 0;
};

std::vector<map_line> read_maps()
{
    std::vector<map_line> out;
    std::ifstream f{"/proc/self/maps"};
    std::string line;
    while (std::getline(f, line)) {
        map_line m;
        m.lo = std::strtoull(line.c_str(), nullptr, 16);
        const auto dash = line.find('-');
        m.hi = std::strtoull(line.c_str() + dash + 1, nullptr, 16);
        m.whole = line;
        out.push_back(std::move(m));
    }
    return out;
}

void print_matching(const std::vector<map_line>& maps, const std::string& needle,
                    const char* label)
{
    for (const auto& m : maps) {
        if (m.whole.find(needle) != std::string::npos) {
            std::print("{}{}\n", label, m.whole);
        }
    }
}

} // namespace

int main()
{
    const char* path = "/tmp/l02_exps/e3_proc_maps/e3data.bin";
    {
        unique_fd w{sys_call("open", ::open, path, O_WRONLY | O_CREAT | O_TRUNC, 0666)};
        std::vector<char> blob(64 * 1024, 'E');
        sys_call("write", ::write, w.get(), blob.data(), blob.size());
    }
    const std::string needle = "e3data.bin";

    auto before = read_maps();
    std::print("maps lines total: {} (before any mmap of the file)\n", before.size());
    print_matching(before, needle, "  file-related: ");

    unique_fd fd{sys_call("open", ::open, path, O_RDWR)};
    const std::size_t page = static_cast<std::size_t>(::sysconf(_SC_PAGE_SIZE));

    mapped_region shared(fd, 2 * page, PROT_READ | PROT_WRITE, MAP_SHARED, 0);
    mapped_region priv(fd, 2 * page, PROT_READ | PROT_WRITE, MAP_PRIVATE,
                       static_cast<off_t>(2 * page)); // 偏移 8 KiB,落在文件的第二个视图
    std::print("mapped SHARED 2 pages at 0x{:x} (offset 0)\n",
               reinterpret_cast<std::uintptr_t>(shared.data()));
    std::print("mapped PRIVATE 2 pages at 0x{:x} (offset 8K)\n",
               reinterpret_cast<std::uintptr_t>(priv.data()));

    // 对照组:ext4 上的文件,tmpfs 的 00:xx 设备号 vs 真块设备号
    const char* extpath = "/home/charliechen/l02_scratch/e3ext.bin";
    {
        unique_fd w{sys_call("open", ::open, extpath, O_WRONLY | O_CREAT | O_TRUNC, 0666)};
        std::vector<char> blob(8192, 'X');
        sys_call("write", ::write, w.get(), blob.data(), blob.size());
    }
    unique_fd efd{sys_call("open", ::open, extpath, O_RDONLY)};
    mapped_region extview(efd, 2 * page, PROT_READ, MAP_PRIVATE, 0);

    auto after = read_maps();
    std::print("maps lines total: {} (after 3 mmaps, +{})\n", after.size(),
               after.size() - before.size());
    print_matching(after, needle, "  file-related: ");
    print_matching(after, "e3ext.bin", "  ext4-backed : ");

    std::print("\n[same file, anonymous/bss side of the world]\n");
    print_matching(after, "[heap]",  "  heap  : ");
    print_matching(after, "[stack]", "  stack : ");

    shared.reset(); // munmap 掉 SHARED,PRIVATE/ext4 留着
    auto last = read_maps();
    std::print("\nafter munmap(SHARED): maps lines total: {} ({}), SHARED line gone, PRIVATE stays\n",
               last.size(), static_cast<long>(last.size()) - static_cast<long>(after.size()));
    print_matching(last, needle, "  file-related: ");
    (void)extview;
    return 0;
}
