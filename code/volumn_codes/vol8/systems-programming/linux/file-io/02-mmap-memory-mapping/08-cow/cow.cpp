// 复核:cow(文章「MAP_PRIVATE」一节)——同一进程两个映射当面对质
#include "article.hpp"

#include <cstdio>
#include <cstring>
#include <print>
#include <string>

namespace {

std::string peek_file(const unique_fd& fd) // pread 16 字节的小封装
{
    char buf[16] {};
    sys_call("pread", ::pread, fd.get(), buf, sizeof buf, 0);
    return std::string{buf, sizeof buf};
}

} // namespace

int main()
{
    const char* path = "/tmp/l02_exps/recheck/cow.bin";
    {
        unique_fd w{sys_call("open", ::open, path, O_WRONLY | O_CREAT | O_TRUNC, 0666)};
        sys_call("write", ::write, w.get(), "AAAABBBBCCCCDDDD", 16);
    }
    unique_fd fd{sys_call("open", ::open, path, O_RDWR)};

    mapped_region priv(fd, 16, PROT_READ | PROT_WRITE, MAP_PRIVATE);
    std::memcpy(priv.data(), "XXXX", 4); // 写的是 COW 出来的私有副本
    std::printf("MAP_PRIVATE view : %.4s... (just wrote)\n", priv.data());
    std::printf("file via pread   : %s\n", peek_file(fd).c_str());

    mapped_region shared(fd, 16, PROT_READ | PROT_WRITE, MAP_SHARED);
    std::print("MAP_SHARED view  : {:.16s} (sees the ORIGINAL)\n",
               reinterpret_cast<const char*>(shared.data())); // reinterpret 转成 const char*,截断交给格式串

    std::memcpy(shared.data() + 8, "YYYY", 4); // SHARED:直接改页缓存
    sys_call("msync", ::msync, shared.data(), shared.size(), MS_SYNC); // 写回并等待
    std::printf("after msync file : %s\n", peek_file(fd).c_str());
    return 0;
}
