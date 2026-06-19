// Milestone 4: 线程局部统计与汇总
// 验证 FileScanner 用每 worker 的局部 WorkerStats 统计、主线程汇总后，
// 结果与单线程逐一扫描完全一致（含扩展名分布）；压力测试在 TSan 下无 data race。
// 通过本测试：把 scan() 里的全局 atomic 换成局部 WorkerStats + 主线程汇总。
//
// 关于 thread_local：本场景每 worker 只执行一次扫描，普通局部变量与
// thread_local 在行为上等价；thread_local 的价值在"同一线程多次进入同一函数
// 时复用/累积状态"。本测试不要求必须用 thread_local 关键字——只要统计正确即可。
#include <catch2/catch_test_macros.hpp>

#include <filesystem>

#include "lab0/file_scanner.h"
#include "test_helpers.h"

TEST_CASE("MS4: multi-threaded stats match single-threaded baseline", "[lab0][milestone4]") {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "lab0_ms4";
    fs::remove_all(dir);
    lab0::test::create_test_files(dir, 10, ".cpp");
    lab0::test::create_test_files(dir, 5, ".h");
    lab0::test::create_test_files(dir, 3, ".txt");

    // 单线程"正确答案"
    lab0::WorkerStats expected;
    for (const auto& entry : fs::recursive_directory_iterator(dir)) {
        if (entry.is_regular_file()) {
            expected.files_scanned++;
            expected.total_bytes += entry.file_size();
            expected.ext_counts[entry.path().extension().string()]++;
        }
    }

    // 多线程扫描
    lab0::FileScanner scanner(dir, 4);
    lab0::WorkerStats actual = scanner.scan();

    REQUIRE(actual.files_scanned == expected.files_scanned);
    REQUIRE(actual.total_bytes == expected.total_bytes);
    REQUIRE(actual.ext_counts[".cpp"] == 10);
    REQUIRE(actual.ext_counts[".h"] == 5);
    REQUIRE(actual.ext_counts[".txt"] == 3);

    fs::remove_all(dir);
}

TEST_CASE("MS4: stress test clean under TSan", "[lab0][milestone4]") {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "lab0_ms4_stress";
    fs::remove_all(dir);
    lab0::test::create_test_files(dir, 200);

    lab0::FileScanner scanner(dir, 8);
    lab0::WorkerStats stats = scanner.scan();

    REQUIRE(stats.files_scanned == 200);
    // Debug 构建已在编译期开 -fsanitize=thread，本测试运行期间应无任何 data race 报告。
    fs::remove_all(dir);
}
