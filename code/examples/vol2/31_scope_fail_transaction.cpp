// scope_fail_transaction.cpp -- scope_fail 在事务中的应用：失败时自动回滚
// Standard: C++17（ScopeFail 模板见正文的定义）

#include <exception>
#include <iostream>
#include <stdexcept>
#include <type_traits>
#include <utility>

template <typename F> class ScopeFail {
  public:
    explicit ScopeFail(F&& func) noexcept
        : func_(std::move(func)), active_(true), uncaught_at_creation_(std::uncaught_exceptions()) {
    }

    ~ScopeFail() noexcept {
        if (active_ && std::uncaught_exceptions() > uncaught_at_creation_) {
            try {
                func_();
            } catch (...) {
                std::terminate();
            }
        }
    }

    ScopeFail(ScopeFail&& other) noexcept
        : func_(std::move(other.func_)), active_(other.active_),
          uncaught_at_creation_(other.uncaught_at_creation_) {
        other.active_ = false;
    }

    void dismiss() noexcept { active_ = false; }

    ScopeFail(const ScopeFail&) = delete;
    ScopeFail& operator=(const ScopeFail&) = delete;

  private:
    F func_;
    bool active_;
    int uncaught_at_creation_;
};

// make_scope_fail：辅助函数，免去手写 decltype 模板参数
template <typename F> ScopeFail<std::decay_t<F>> make_scope_fail(F&& func) noexcept {
    return ScopeFail<std::decay_t<F>>(std::forward<F>(func));
}

class DatabaseTransaction {
  public:
    void begin() { std::cout << "BEGIN TRANSACTION\n"; }
    void commit() { std::cout << "COMMIT\n"; }
    void rollback() { std::cout << "ROLLBACK\n"; }
};

void transfer_money(DatabaseTransaction& tx, int from, int to, int amount) {
    tx.begin();

    // 失败时自动回滚：异常传播时 ScopeFail 的析构触发 rollback
    auto on_fail = make_scope_fail([&tx]() noexcept { tx.rollback(); });

    if (amount <= 0) {
        throw std::invalid_argument("amount must be positive");
    }

    std::cout << "Transfer " << amount << " from " << from << " to " << to << "\n";
}

int main() {
    DatabaseTransaction tx;

    try {
        transfer_money(tx, 1001, 2002, -50);
    } catch (const std::exception& e) {
        std::cout << "捕获异常: " << e.what() << "\n";
    }
    return 0;
}
