// e5_paths.cpp —— E5:path 拼接与化简的坑(纯词法,不碰文件系统)
//
//  [1] operator/:末尾斜杠、空串、绝对路径右侧(整体顶掉左侧)、「./」原样拼进去
//  [2] lexically_normal:./a/../b、a/b/..、a/../..、a//b///c;词法化简,不查盘
//  [3] path::value_type 与 c_str():Linux 上是 char(const char*),
//      Windows 上是 wchar_t —— 镜像篇的对拍点,本侧把 Linux 行为钉死
//  [4] 迭代 path:根目录条目、相对路径、末尾斜杠产生的空元素
// 编译:g++ -std=c++20 -Wall -Wextra -O2 e5_paths.cpp -o e5_paths
#include <cstdio>
#include <filesystem>
#include <type_traits>

namespace fs = std::filesystem;

static void show(const char* tag, const fs::path& p) {
    std::printf("  %-28s -> \"%s\"\n", tag, p.c_str());
}

int main() {
    // ---- [1] operator/ ----
    std::printf("== [1] operator/ 的四种脾气 ==\n");
    show("fs::path(\"base\") / \"leaf\"", fs::path("base") / "leaf");
    show("fs::path(\"base/\") / \"leaf\"", fs::path("base/") / "leaf");
    show("fs::path(\"base//\") / \"leaf\"", fs::path("base//") / "leaf");
    show("fs::path(\"base\") / \"\"", fs::path("base") / "");
    show("fs::path(\"base\") / \"/abs\"", fs::path("base") / "/abs");     // 坑:绝对路径顶掉左侧
    show("fs::path(\"base\") / \"./leaf\"", fs::path("base") / "./leaf"); // 不化简
    fs::path p = "root";
    p /= "sub"; // /= 就地版
    p /= "file.txt";
    show("p = \"root\"; p/=\"sub\"; p/=\"file.txt\"", p);

    // ---- [2] lexically_normal ----
    std::printf("\n== [2] lexically_normal(纯词法) ==\n");
    show("\"./a/../b\"", fs::path("./a/../b").lexically_normal());
    show("\"a/b/..\"", fs::path("a/b/..").lexically_normal());
    show("\"a/../..\"", fs::path("a/../..").lexically_normal());
    show("\"../b\"", fs::path("../b").lexically_normal());
    show("\"a//b///c\"", fs::path("a//b///c").lexically_normal());
    show("\"a/./b\"", fs::path("a/./b").lexically_normal());
    show("\"/../../a\"", fs::path("/../../a").lexically_normal()); // 根目录之下的 .. 被钳在根
    show("\"./\"", fs::path("./").lexically_normal());
    show("\"\"", fs::path("").lexically_normal());
    std::printf("  (注意 \"./a/../b\" -> \"b\":中间目录被 a/../ 抵消;不查盘,"
                "\"a/../b\" 里的 a 是不是目录、存不存在,一概不管)\n");

    // ---- [3] value_type / c_str ----
    std::printf("\n== [3] path::value_type 与 c_str()(Linux 侧) ==\n");
    static_assert(std::is_same_v<fs::path::value_type, char>, "Linux 上 path 的字符类型是 char");
    static_assert(std::is_same_v<decltype(fs::path{}.c_str()), const char*>,
                  "c_str() 在 Linux 上退化为 const char*");
    std::printf("  sizeof(path::value_type) = %zu\n", sizeof(fs::path::value_type));
    std::printf("  string_type 即 std::string:%s\n",
                std::is_same_v<fs::path::string_type, std::string> ? "yes" : "no");
    fs::path bin = "/usr/bin/env";
    std::printf("  printf(\"%%s\", p.c_str()) = %s(可直接喂 C API)\n", bin.c_str());
    std::printf("  (Windows 侧 value_type 是 wchar_t,c_str() 是 const wchar_t*;"
                "窄字符 C API 需显式转换(如 string())或改用宽字符 API —— 镜像篇对拍点)\n");

    // ---- [4] 迭代元素 ----
    std::printf("\n== [4] 迭代 path 的元素 ==\n");
    auto dump = [](const char* s) {
        std::printf("  \"%s\" ->", s);
        for (auto& elem : fs::path(s))
            std::printf(" [%s]", elem.c_str());
        std::printf("\n");
    };
    dump("/usr/local/include");
    dump("a/b/c");
    dump("a/b/"); // 末尾斜杠 -> 尾部空元素
    dump("/");
    dump(".");
    dump("..");
    dump("/abs/path/../x");
    return 0;
}
