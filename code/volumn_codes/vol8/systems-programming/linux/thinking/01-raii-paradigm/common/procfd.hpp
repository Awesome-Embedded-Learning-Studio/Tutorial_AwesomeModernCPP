#pragma once
// common/procfd.hpp —— 读 /proc/self/fd 的公共小工具
//
// list_open_fds():列出当前进程打开的全部 fd(升序)。
// 两个坑都按 file-io/01 篇 exp5 的实测处理:
//   * opendir("/proc/self/fd") 自己要占一个 fd(那个槽位不算进结果)
//   * 目录项里有 "." 和 ".."(跳过)

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <dirent.h>
#include <vector>

inline std::vector<int> list_open_fds() {
    std::vector<int> fds;
    DIR* d = ::opendir("/proc/self/fd");
    if (d == nullptr) {
        std::perror("opendir(/proc/self/fd)");
        std::exit(1);
    }
    const int own = ::dirfd(d);
    while (struct dirent* e = ::readdir(d)) {
        if (e->d_name[0] == '.') {
            continue;
        }
        const int n = std::atoi(e->d_name);
        if (n != own) {
            fds.push_back(n);
        }
    }
    ::closedir(d);
    std::sort(fds.begin(), fds.end());
    return fds;
}

// 全量打印(短表)或首尾摘要(长表)
inline void print_fds(const char* tag, const std::vector<int>& v) {
    std::printf("%s (%zu):", tag, v.size());
    if (v.size() <= 20) {
        for (int fd : v) {
            std::printf(" %d", fd);
        }
    } else {
        for (std::size_t i = 0; i < 8; ++i) {
            std::printf(" %d", v[i]);
        }
        std::printf(" ... %d %d %d %d", v[v.size() - 4], v[v.size() - 3], v[v.size() - 2],
                    v[v.size() - 1]);
    }
    std::printf("\n");
}
