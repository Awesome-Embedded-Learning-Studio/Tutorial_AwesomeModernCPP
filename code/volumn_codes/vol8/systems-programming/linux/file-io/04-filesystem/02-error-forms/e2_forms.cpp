// e2_forms.cpp —— E2:同一失败,抛异常版与 error_code 版的真实输出
//
//  [1] 打开不存在目录:directory_iterator 抛异常版 -> fs::filesystem_error 全字段
//  [2] 同一操作走 error_code 重载 -> ec.value()/message()/与 errc 比较
//  [3] 非成员函数(fs::exists 等)的 ec 版:ENOENT 对它们是不是「错误」?
//      exists/is_directory/status/file_size 逐个看 ec 是否被置位
//  [4] 对已删目录「再 ++」:it.increment(ec) 的非抛增量形态
// 编译:g++ -std=c++20 -Wall -Wextra -O2 e2_forms.cpp -o e2_forms
#include <cstdio>
#include <filesystem>
#include <system_error>

namespace fs = std::filesystem;

static const char* kMissing = "/home/charliechen/l04_scratch/e2/no_such_dir";

static void report_ec(const char* what, std::error_code ec, bool ret) {
    std::printf("  %-22s ret=%-6s ec.value()=%-3d ec.message()=\"%s\"\n", what,
                ret ? "true" : "false", ec.value(), ec.message().c_str());
}

int main() {
    // ---- [1] 抛异常形态 ----
    std::printf("== [1] directory_iterator(不存在目录)—— 抛异常版 ==\n");
    try {
        fs::directory_iterator it{kMissing}; // 无 ec 重载:失败即抛
        std::printf("  不该到这\n");
    } catch (const fs::filesystem_error& e) {
        std::printf("  caught std::filesystem::filesystem_error\n");
        std::printf("  what()       = \"%s\"\n", e.what());
        std::printf("  code().value()    = %d (ENOENT=%d)\n", e.code().value(), ENOENT);
        std::printf("  code().message()  = \"%s\"\n", e.code().message().c_str());
        std::printf("  code() == errc::no_such_file_or_directory : %s\n",
                    e.code() == std::errc::no_such_file_or_directory ? "true" : "false");
        std::printf("  path1()      = \"%s\"\n", e.path1().c_str());
    }

    // ---- [2] error_code 形态 ----
    std::printf("\n== [2] 同一操作,error_code 重载 ==\n");
    std::error_code ec;
    fs::directory_iterator it{kMissing, ec};
    std::printf("  构造返回后:ec.value()=%d (ENOENT=%d)\n", ec.value(), ENOENT);
    std::printf("  ec.message()=\"%s\"\n", ec.message().c_str());
    std::printf("  ec == {} 判失败:%s(ec 非默认值即失败,可直接当哨兵)\n",
                ec == std::error_code{} ? "false" : "true");
    std::printf("  失败时拿到的是收尾迭代器:%s\n",
                it == fs::directory_iterator{} ? "it == end(不抛,得空壳)" : "NO");

    // ---- [3] 非成员函数的 ec 版:ENOENT 算不算错误 ----
    std::printf("\n== [3] 非成员函数的 error_code 版,同一路径 %s ==\n", kMissing);
    ec.clear();
    bool b1 = fs::exists(kMissing, ec);
    report_ec("fs::exists", ec, b1);
    ec.clear();
    bool b2 = fs::is_directory(kMissing, ec);
    report_ec("fs::is_directory", ec, b2);
    ec.clear();
    fs::file_status st = fs::status(kMissing, ec);
    std::printf("  %-22s type=%d (not_found 枚举值 = %d) ec.value()=%d ec.message()=\"%s\"\n",
                "fs::status", static_cast<int>(st.type()),
                static_cast<int>(fs::file_type::not_found), ec.value(), ec.message().c_str());
    ec.clear();
    auto sz = fs::file_size(kMissing, ec);
    report_ec("fs::file_size", ec, sz != static_cast<std::uintmax_t>(-1));
    std::printf("    file_size 返回值 = %llu (uintmax_t(-1)=错误哨兵)\n",
                static_cast<unsigned long long>(sz));
    std::printf(
        "  同一种「文件不在」,四种态度:exists 连 ec 都清零(not_found 是答案);\n"
        "  is_directory 返 false 但 ec 留着 ENOENT;status 给 not_found 类型且 ec 带 ENOENT\n"
        "  (类型即答案,code 记原因);file_size 视为错误,返哨兵值。libstdc++ 的 exists()\n"
        "  内部:status 已知就 ec.clear() —— 这步是它跟 is_directory 分岔的地方。\n");

    // ---- [4] increment(ec):对收尾迭代器再 ++ 是 UB;换成「活目录被删」在 e2_vanish 做 ----
    std::printf("\n== [4] 增量的非抛形态(活例子见 e2_vanish)==\n");
    std::printf("  it.increment(ec) 与 ++it 对应;ec 版失败不抛,返回值仍是迭代器\n");
    return 0;
}
