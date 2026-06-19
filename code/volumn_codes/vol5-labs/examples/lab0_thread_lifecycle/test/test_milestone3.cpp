// Milestone 3: 参数生命周期修复
// 验证 worker 拿到独立的文件列表副本（值捕获/move），无悬空引用；
// 非整除分片也能覆盖所有文件；move-only 类型能安全传入线程。
// 通过本测试：审查并修正 include/lab0/file_scanner.h 中 scan() 的参数捕获。
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <filesystem>
#include <memory>

#include "lab0/file_scanner.h"
#include "lab0/joining_thread.h"
#include "test_helpers.h"

TEST_CASE("MS3: non-divisible sharding covers all files", "[lab0][milestone3]") {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "lab0_ms3_shard";
    fs::remove_all(dir);
    lab0::test::create_test_files(dir, 30);

    lab0::FileScanner scanner(dir, 8); // 30 / 8 非整除
    lab0::WorkerStats stats = scanner.scan();

    REQUIRE(stats.files_scanned == 30);
    fs::remove_all(dir);
}

TEST_CASE("MS3: prime file count covered by any worker count", "[lab0][milestone3]") {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "lab0_ms3_prime";
    fs::remove_all(dir);
    lab0::test::create_test_files(dir, 17); // 17 是素数，任何分片都非整除

    lab0::FileScanner scanner(dir, 4);
    lab0::WorkerStats stats = scanner.scan();

    REQUIRE(stats.files_scanned == 17);
    fs::remove_all(dir);
}

TEST_CASE("MS3: move-only argument passes safely into thread", "[lab0][milestone3]") {
    std::atomic<bool> processed{false};
    {
        auto ptr = std::make_unique<int>(42);
        // 关键：用 init-capture 把 unique_ptr move 进线程，生命周期由线程持有
        lab0::JoiningThread t([&processed, p = std::move(ptr)]() {
            if (p && *p == 42) {
                processed.store(true, std::memory_order_relaxed);
            }
        });
    }
    REQUIRE(processed.load());
}
