// ScopeGuard with success/fail branches
// 需要 C++17 的 std::uncaught_exceptions()

#include <utility>
#include <cstdio>
#include <exception>

// ========== 基础 ScopeExit (始终执行) ==========

template <typename F>
class ScopeExit {
public:
    explicit ScopeExit(F&& f) noexcept
        : func_(std::move(f)), active_(true) {}

    ~ScopeExit() noexcept {
        if (active_) {
            func_();
        }
    }

    ScopeExit(const ScopeExit&) = delete;
    ScopeExit& operator=(const ScopeExit&) = delete;

    ScopeExit(ScopeExit&& other) noexcept
        : func_(std::move(other.func_)), active_(other.active_) {
        other.active_ = false;
    }

    void dismiss() noexcept { active_ = false; }

private:
    F func_;
    bool active_;
};

template <typename F>
ScopeExit<typename std::decay<F>::type> make_scope_exit(F&& f) {
    return ScopeExit<typename std::decay<F>::type>(std::forward<F>(f));
}

// ========== ScopeSuccess (仅无异常时执行) ==========

template <typename F>
class ScopeSuccess {
public:
    explicit ScopeSuccess(F&& f) noexcept
        : func_(std::move(f))
        , active_(true)
        , uncaught_at_construction_(std::uncaught_exceptions()) {}

    ~ScopeSuccess() noexcept {
        if (active_ && std::uncaught_exceptions() == uncaught_at_construction_) {
            func_();
        }
    }

    ScopeSuccess(const ScopeSuccess&) = delete;
    ScopeSuccess& operator=(const ScopeSuccess&) = delete;

    ScopeSuccess(ScopeSuccess&& other) noexcept
        : func_(std::move(other.func_))
        , active_(other.active_)
        , uncaught_at_construction_(other.uncaught_at_construction_) {
        other.active_ = false;
    }

    void dismiss() noexcept { active_ = false; }

private:
    F func_;
    bool active_;
    int uncaught_at_construction_;
};

template <typename F>
ScopeSuccess<typename std::decay<F>::type> make_scope_success(F&& f) {
    return ScopeSuccess<typename std::decay<F>::type>(std::forward<F>(f));
}

// ========== ScopeFail (仅异常时执行) ==========

template <typename F>
class ScopeFail {
public:
    explicit ScopeFail(F&& f) noexcept
        : func_(std::move(f))
        , active_(true)
        , uncaught_at_construction_(std::uncaught_exceptions()) {}

    ~ScopeFail() noexcept {
        if (active_ && std::uncaught_exceptions() > uncaught_at_construction_) {
            func_();
        }
    }

    ScopeFail(const ScopeFail&) = delete;
    ScopeFail& operator=(const ScopeFail&) = delete;

    ScopeFail(ScopeFail&& other) noexcept
        : func_(std::move(other.func_))
        , active_(other.active_)
        , uncaught_at_construction_(other.uncaught_at_construction_) {
        other.active_ = false;
    }

    void dismiss() noexcept { active_ = false; }

private:
    F func_;
    bool active_;
    int uncaught_at_construction_;
};

template <typename F>
ScopeFail<typename std::decay<F>::type> make_scope_fail(F&& f) {
    return ScopeFail<typename std::decay<F>::type>(std::forward<F>(f));
}

// ========== 使用示例 ==========

// 模拟事务管理器
class Transaction {
public:
    void begin() {
        printf("[Transaction] BEGIN\n");
    }

    void commit() {
        printf("[Transaction] COMMIT\n");
    }

    void rollback() {
        printf("[Transaction] ROLLBACK\n");
    }
};

void success_fail_example() {
    printf("=== ScopeSuccess / ScopeFail Example ===\n");

    Transaction tx;

    tx.begin();

    auto on_success = make_scope_success([&]{ tx.commit(); });
    auto on_fail = make_scope_fail([&]{ tx.rollback(); });

    printf("Transaction in progress...\n");

    // 模拟成功（没有抛出异常）
    // on_success 会执行，on_fail 不会
}

void with_exception_example() {
    printf("\n=== With Exception Example ===\n");

    Transaction tx;

    tx.begin();

    auto on_success = make_scope_success([&]{ tx.commit(); });
    auto on_fail = make_scope_fail([&]{ tx.rollback(); });

    printf("About to throw exception...\n");

    // 注意：在实际编译时需要启用异常
    // throw std::runtime_error("Something went wrong");

    // 如果异常被抛出：
    // - on_success 不会执行（因为 uncaught_exceptions 增加）
    // - on_fail 会执行
}

// 数据库操作示例
struct Database {
    void connect() { printf("[DB] Connected\n"); }
    void disconnect() { printf("[DB] Disconnected\n"); }
    void begin_tx() { printf("[DB] TX BEGIN\n"); }
    void commit_tx() { printf("[DB] TX COMMIT\n"); }
    void rollback_tx() { printf("[DB] TX ROLLBACK\n"); }
};

void database_operation(Database& db) {
    printf("\n=== Database Operation Example ===\n");

    db.connect();
    auto disconnect = make_scope_exit([&]{ db.disconnect(); });

    db.begin_tx();

    auto on_success = make_scope_success([&]{ db.commit_tx(); });
    auto on_fail = make_scope_fail([&]{ db.rollback_tx(); });

    printf("Executing query...\n");

    // 如果这里抛出异常，on_fail 会执行回滚
    // 否则 on_success 会执行提交
}

// 文件写入示例
struct FileWriter {
    void open() { printf("[File] Opened\n"); }
    void close() { printf("[File] Closed\n"); }
    void write(const char* data) { printf("[File] Writing: %s\n", data); }
    void commit() { printf("[File] Committing changes\n"); }
    void revert() { printf("[File] Reverting changes\n"); }
};

void file_write_example() {
    printf("\n=== File Write Example ===\n");

    FileWriter writer;

    writer.open();
    auto close = make_scope_exit([&]{ writer.close(); });

    auto on_success = make_scope_success([&]{ writer.commit(); });
    auto on_fail = make_scope_fail([&]{ writer.revert(); });

    writer.write("Header data");
    writer.write("Body data");
    writer.write("Footer data");

    printf("Write completed successfully\n");
}

// 状态恢复示例
class SystemState {
public:
    void save() { printf("[State] Saving current state\n"); }
    void restore() { printf("[State] Restoring saved state\n"); }
    void clear_saved() { printf("[State] Clearing saved state\n"); }
};

void risky_operation(SystemState& sys) {
    printf("\n=== Risky Operation Example ===\n");

    sys.save();

    auto on_fail = make_scope_fail([&]{ sys.restore(); });
    auto on_success = make_scope_success([&]{ sys.clear_saved(); });

    printf("Performing risky operation...\n");

    // 如果操作失败抛异常，状态会自动恢复
    // 如果成功，清除保存的状态
}

int main() {
    success_fail_example();
    with_exception_example();

    Database db;
    database_operation(db);

    file_write_example();

    SystemState sys;
    risky_operation(sys);

    printf("\n=== All Examples Complete ===\n");
    printf("\nNote: ScopeSuccess and ScopeFail require:\n");
    printf("  1. C++17 or later for std::uncaught_exceptions()\n");
    printf("  2. Exceptions enabled in compiler\n");
    printf("For embedded systems with -fno-exceptions, use ScopeExit only.\n");

    return 0;
}
