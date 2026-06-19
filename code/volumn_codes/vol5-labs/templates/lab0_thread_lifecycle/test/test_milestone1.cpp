// Milestone 1: 并行任务分发
// 验证 FileScanner 用裸 std::thread 把目录分片扫描，统计正确。
// 通过本测试：补全 include/lab0/file_scanner.h 的 scan() MS1 实现。
#include <catch2/catch_test_macros.hpp>

#include <filesystem>

#include "lab0/file_scanner.h"
#include "test_helpers.h"

TEST_CASE("MS1: scan collects all files", "[lab0][milestone1]") {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "lab0_ms1_basic";
    fs::remove_all(dir);
    lab0::test::create_test_files(dir, 20);

    lab0::FileScanner scanner(dir, 4);
    lab0::WorkerStats stats = scanner.scan();

    REQUIRE(stats.files_scanned == 20);
    fs::remove_all(dir);
}

TEST_CASE("MS1: empty directory does not crash", "[lab0][milestone1]") {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "lab0_ms1_empty";
    fs::remove_all(dir);
    fs::create_directories(dir);

    lab0::FileScanner scanner(dir, 4);
    lab0::WorkerStats stats = scanner.scan();

    REQUIRE(stats.files_scanned == 0);
    REQUIRE(stats.total_bytes == 0);
    fs::remove_all(dir);
}

TEST_CASE("MS1: total byte count is correct", "[lab0][milestone1]") {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "lab0_ms1_bytes";
    fs::remove_all(dir);
    lab0::test::create_test_files(dir, 10); // 每个 100 字节

    lab0::FileScanner scanner(dir, 4);
    lab0::WorkerStats stats = scanner.scan();

    REQUIRE(stats.total_bytes == 10 * 100);
    fs::remove_all(dir);
}
