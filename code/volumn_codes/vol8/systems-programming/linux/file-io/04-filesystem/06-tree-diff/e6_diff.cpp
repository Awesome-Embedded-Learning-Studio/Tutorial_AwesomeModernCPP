// e6_diff.cpp —— E6(实战雏形):两棵树的 diff 清单,不动内容只看元数据
//
// 判据:存在性 + size + mtime(fs::directory_entry::file_size / last_write_time)。
// 三类输出:新增(右有左无)/ 删除(左有右无)/ 修改(两边都有但 size 或 mtime 变了)。
// 目录级整删时,子孙条目折叠进父目录一条(与 diff -rq 的报告粒度对齐)。
// 用法:
//   ./e6_diff setup   # 建左右两棵对照树
//   ./e6_diff         # 出清单
// 编译:g++ -std=c++20 -Wall -Wextra -O2 e6_diff.cpp -o e6_diff
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using Clock = std::chrono::file_clock;

static const char* kLeft = "/home/charliechen/l04_scratch/e6_left";
static const char* kRight = "/home/charliechen/l04_scratch/e6_right";

struct Info {
    char type = '?'; // 'd' 目录 / 'f' 普通文件 / 'l' 链接
    std::uintmax_t size = 0;
    Clock::time_point mtime;
};

static void put(const fs::path& p, const char* text) {
    if (FILE* f = std::fopen(p.c_str(), "w")) {
        std::fputs(text, f);
        std::fclose(f);
    }
}

static void set_mtime(const fs::path& p, Clock::time_point t) {
    std::error_code ec;
    fs::last_write_time(p, t, ec);
}

static std::string fmt_time(Clock::time_point t) {
    auto sys = std::chrono::clock_cast<std::chrono::system_clock>(t);
    std::time_t sec = std::chrono::system_clock::to_time_t(sys);
    char buf[32];
    std::strftime(buf, sizeof buf, "%H:%M:%S", std::localtime(&sec));
    return buf;
}

static void setup() {
    fs::remove_all(kLeft);
    fs::remove_all(kRight);
    fs::create_directories(fs::path(kLeft) / "dir_old");
    fs::create_directories(fs::path(kRight) / "dir_new");

    put(fs::path(kLeft) / "keep.txt", "same\n");
    put(fs::path(kRight) / "keep.txt", "same\n"); // 内容一致,mtime 会差 1h(假阳性演示)

    put(fs::path(kLeft) / "stable.txt", "rock\n");
    put(fs::path(kRight) / "stable.txt", "rock\n"); // 内容一致,下面把 mtime 也钉成同一刻

    put(fs::path(kLeft) / "mod.txt", "12345");
    put(fs::path(kRight) / "mod.txt", "123456789"); // 内容变,size 也变

    put(fs::path(kLeft) / "samesize.txt", "AAAA");
    put(fs::path(kRight) / "samesize.txt", "BBBB"); // size 相同,内容换了

    put(fs::path(kLeft) / "only_left.txt", "bye\n"); // 右侧没有

    put(fs::path(kLeft) / "dir_old/nested.txt", "n\n");  // 整目录右侧消失
    put(fs::path(kRight) / "dir_new/fresh.txt", "hi\n"); // 整目录右侧新增
    put(fs::path(kRight) / "only_right.txt", "new\n");

    // 用 mtime 拉开时间戳:左侧统一拨慢 1 小时,右侧保持「现在」;
    // stable.txt 两边都钉到同一刻 now-2h —— 留一条真正「未变」的对照
    auto now = Clock::now();
    for (auto& e : fs::recursive_directory_iterator(kLeft))
        set_mtime(e.path(), now - std::chrono::hours(1));
    for (auto& e : fs::recursive_directory_iterator(kRight))
        set_mtime(e.path(), now);
    set_mtime(fs::path(kLeft) / "stable.txt", now - std::chrono::hours(2));
    set_mtime(fs::path(kRight) / "stable.txt", now - std::chrono::hours(2));
    std::printf("setup 完成:left=%s right=%s(左侧 mtime -1h;stable.txt 两侧同钉 -2h;"
                "keep.txt 内容相同但 mtime 差 1h —— 故意留的假阳性)\n",
                kLeft, kRight);
}

static std::map<std::string, Info> walk(const char* root) {
    std::map<std::string, Info> out;
    std::error_code ec;
    for (auto& e : fs::recursive_directory_iterator(
             root, fs::directory_options::skip_permission_denied, ec)) {
        Info info;
        info.type = e.is_directory(ec) ? 'd' : e.is_regular_file(ec) ? 'f' : 'l';
        if (info.type == 'f')
            info.size = e.file_size(ec);
        info.mtime = e.last_write_time(ec);
        out[fs::relative(e.path(), root).string()] = info;
    }
    return out;
}

// 折叠:若某条目的某个父目录也在集合里,整条略过(父目录一条顶全部)
static bool shadowed_by_dir(const std::string& rel, const std::map<std::string, Info>& side) {
    std::size_t pos = 0;
    while ((pos = rel.find('/', pos + 1)) != std::string::npos) {
        auto it = side.find(rel.substr(0, pos));
        if (it != side.end() && it->second.type == 'd')
            return true;
    }
    return false;
}

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "setup") {
        setup();
        return 0;
    }

    auto left = walk(kLeft), right = walk(kRight);
    std::printf("left %zu 条 / right %zu 条;判据 size+mtime,不读内容\n\n", left.size(),
                right.size());

    std::printf("== 新增(右有左无) ==\n");
    for (auto& [rel, ri] : right)
        if (!left.count(rel) && !shadowed_by_dir(rel, right))
            std::printf("  + %-22s %c size=%llu mtime=%s\n", rel.c_str(), ri.type,
                        static_cast<unsigned long long>(ri.size), fmt_time(ri.mtime).c_str());

    std::printf("== 删除(左有右无) ==\n");
    for (auto& [rel, li] : left)
        if (!right.count(rel) && !shadowed_by_dir(rel, left))
            std::printf("  - %-22s %c size=%llu mtime=%s\n", rel.c_str(), li.type,
                        static_cast<unsigned long long>(li.size), fmt_time(li.mtime).c_str());

    std::printf("== 修改(两边都有,元数据对不上) ==\n");
    for (auto& [rel, li] : left) {
        auto it = right.find(rel);
        if (it == right.end() || li.type != it->second.type)
            continue;
        bool size_diff = li.type == 'f' && li.size != it->second.size;
        bool mtime_diff = li.mtime != it->second.mtime;
        if (size_diff || mtime_diff)
            std::printf("  M %-22s %s%s(%llu->%llu 字节,%s->%s)\n", rel.c_str(),
                        size_diff ? "size " : "", mtime_diff ? "mtime " : "",
                        static_cast<unsigned long long>(li.size),
                        static_cast<unsigned long long>(it->second.size),
                        fmt_time(li.mtime).c_str(), fmt_time(it->second.mtime).c_str());
    }

    std::size_t same = 0;
    for (auto& [rel, li] : left) {
        auto it = right.find(rel);
        if (it != right.end() && li.type == it->second.type && li.size == it->second.size &&
            li.mtime == it->second.mtime)
            ++same;
    }
    std::printf("\n未变 %zu 条(stable.txt:内容+size+mtime 全同)。\n", same);
    std::printf("对照真 diff:diff -rq %s %s(在 shell 里跑,输出见 .out)\n", kLeft, kRight);
    return 0;
}
