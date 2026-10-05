// e3_perm_space.cpp —— E3:权限三问 + 空间查询
//
//  [1] fs::permissions 读一位、加一位(owner_exec)、再去掉;前后用 ls 风格串对照
//  [2] 符号链接的 follow/nofollow:fs::status(跟目标) vs fs::symlink_status(链接自身,
//      Linux 上恒 0777 且无意义);悬空链接两边各是什么;再试 nofollow 改链接权限,内核认不认
//  [3] fs::space:capacity/available/free 与 df 对表;available(非特权)与 free(含 root 预留)差多少
//  [4] 失败路径:000 权限目录 -> directory_iterator 的 ec;exists 在 EACCES 与 ENOENT 下的差别
// 编译:g++ -std=c++20 -Wall -Wextra -O2 e3_perm_space.cpp -o e3_perm_space
#include <cerrno>
#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>
#include <unistd.h>

namespace fs = std::filesystem;
using fs::perms;

static const char* kScratch = "/home/charliechen/l04_scratch";

// perms 位集 -> "rwxr-x---" 风格
static std::string perm_string(perms p) {
    std::string s = "---------";
    auto bit = [&](perms m, char c, int i) {
        s[i] = (static_cast<unsigned>(p) & static_cast<unsigned>(m)) ? c : '-';
    };
    bit(perms::owner_read, 'r', 0);
    bit(perms::owner_write, 'w', 1);
    bit(perms::owner_exec, 'x', 2);
    bit(perms::group_read, 'r', 3);
    bit(perms::group_write, 'w', 4);
    bit(perms::group_exec, 'x', 5);
    bit(perms::others_read, 'r', 6);
    bit(perms::others_write, 'w', 7);
    bit(perms::others_exec, 'x', 8);
    char oct[8];
    std::snprintf(oct, sizeof oct, "%03o", static_cast<unsigned>(p) & 0777);
    return s + " (" + oct + ")";
}

int main() {
    fs::path work = fs::path(kScratch) / "e3";
    fs::create_directories(work);

    // ---- [1] 读 / 加 / 去 ----
    fs::path f = work / "script.sh";
    {
        FILE* fp = std::fopen(f.c_str(), "w");
        std::fputs("#!/bin/sh\n", fp);
        std::fclose(fp);
    }
    std::printf("== [1] fs::permissions 三步 ==\n");
    std::printf("  初始            : %s\n", perm_string(fs::status(f).permissions()).c_str());
    fs::permissions(f, perms::owner_exec, fs::perm_options::add);
    std::printf("  add owner_exec  : %s\n", perm_string(fs::status(f).permissions()).c_str());
    std::printf("  ::access(X_OK)  : %s\n", ::access(f.c_str(), X_OK) == 0 ? "可执行" : "不可执行");
    fs::permissions(f, perms::owner_exec, fs::perm_options::remove);
    std::printf("  remove owner_exec: %s\n", perm_string(fs::status(f).permissions()).c_str());

    // ---- [2] follow / nofollow ----
    std::printf("\n== [2] 符号链接:fs::status(跟随) vs fs::symlink_status(不跟随) ==\n");
    fs::path target = work / "real.txt";
    {
        FILE* fp = std::fopen(target.c_str(), "w");
        std::fputs("body", fp);
        std::fclose(fp);
    }
    fs::path link = work / "link_to_real";
    fs::create_symlink("real.txt", link);
    fs::path dangling = work / "link_to_nowhere";
    fs::create_symlink("nowhere.txt", dangling);

    std::printf("  status(link)          type=%d perms=%s   <- 跟随,看到的是 real.txt\n",
                static_cast<int>(fs::status(link).type()),
                perm_string(fs::status(link).permissions()).c_str());
    std::printf("  symlink_status(link)  type=%d perms=%s   <- 链接自身,Linux 恒 0777\n",
                static_cast<int>(fs::symlink_status(link).type()),
                perm_string(fs::symlink_status(link).permissions()).c_str());
    std::printf("  status(dangling)      type=%d (not_found=%d)  <- 跟随悬空链接 = 文件不存在\n",
                static_cast<int>(fs::status(dangling).type()),
                static_cast<int>(fs::file_type::not_found));
    std::printf("  symlink_status(dangling) type=%d (symlink=%d)  <- 不跟随,链接本身在\n",
                static_cast<int>(fs::symlink_status(dangling).type()),
                static_cast<int>(fs::file_type::symlink));

    std::error_code ec;
    fs::permissions(link, perms::owner_write, fs::perm_options::remove | fs::perm_options::nofollow,
                    ec);
    std::printf("  nofollow 改链接自身权限:ec=%d (\"%s\")  <- Linux 对 lchmod 的态度\n", ec.value(),
                ec.message().c_str());
    std::printf("  目标 real.txt 权限有没有被误伤:%s\n",
                perm_string(fs::status(target).permissions()).c_str());
    fs::permissions(link, perms::owner_write, fs::perm_options::remove); // 默认跟随
    std::printf("  默认(跟随)remove owner_write 后目标权限:%s\n",
                perm_string(fs::status(target).permissions()).c_str());

    // ---- [3] fs::space ----
    std::printf("\n== [3] fs::space(\"%s\") ==\n", kScratch);
    fs::space_info sp = fs::space(kScratch, ec);
    std::printf("  capacity  = %llu (%.2f GiB)\n", static_cast<unsigned long long>(sp.capacity),
                sp.capacity / 1073741824.0);
    std::printf("  free      = %llu (%.2f GiB)   <- 块总量减已用(含 root 预留)\n",
                static_cast<unsigned long long>(sp.free), sp.free / 1073741824.0);
    std::printf("  available = %llu (%.2f GiB)   <- 非特权进程能用到的\n",
                static_cast<unsigned long long>(sp.available), sp.available / 1073741824.0);
    std::printf("  free - available = %llu (%.2f%%) <- ext4 root 预留的痕迹\n",
                static_cast<unsigned long long>(sp.free - sp.available),
                100.0 * (sp.free - sp.available) / sp.capacity);
    ec.clear();
    fs::space_info bad = fs::space("/home/charliechen/l04_scratch/e3/no_such", ec);
    std::printf("  space(不存在路径): ec=%d (\"%s\"), 字段=%llu(哨兵 uintmax_t(-1))\n", ec.value(),
                ec.message().c_str(), static_cast<unsigned long long>(bad.capacity));

    // ---- [4] EACCES:000 目录 ----
    std::printf("\n== [4] 无权限目录 ==\n");
    fs::path denied = work / "denied";
    fs::create_directories(denied);
    {
        FILE* fp = std::fopen((denied / "inside.txt").c_str(), "w");
        std::fclose(fp);
    }
    fs::permissions(denied, perms::none, fs::perm_options::replace); // chmod 000(属主照样被挡)
    ec.clear();
    fs::directory_iterator di{denied, ec};
    std::printf("  directory_iterator(denied, ec): ec.value()=%d (EACCES=%d) message=\"%s\"\n",
                ec.value(), EACCES, ec.message().c_str());
    ec.clear();
    bool e1 = fs::exists(denied / "no_such_file", ec); // 穿不进去 vs 根本没有
    std::printf("  exists(denied/no_such_file) = %s, ec=%d(\"%s\")  <- EACCES 是错误\n",
                e1 ? "true" : "false", ec.value(), ec.message().c_str());
    ec.clear();
    bool e2 = fs::exists("/no_such_root_file", ec);
    std::printf("  exists(/no_such_root_file) = %s, ec=%d(\"%s\")  <- ENOENT 是答案\n",
                e2 ? "true" : "false", ec.value(), ec.message().c_str());
    // 收尾:属主随时可以改回权限
    fs::permissions(denied, perms::owner_all, fs::perm_options::replace);
    fs::remove_all(denied);
    std::printf("  (收尾:属主 chmod 回 0700 后删除,000 挡不住 chmod)\n");
    return 0;
}
