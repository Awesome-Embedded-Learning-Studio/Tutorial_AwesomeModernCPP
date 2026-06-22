#include "DbConfig.h"

#include <iostream>
#include <vector>

// 内部状态的展示:外部状态(连接次数、超时、事务状态)留在调用方那边,
// 用的时候传进来,不进享元——和棋子坐标是同一个套路。
void DbConfig::show() const {
    std::cout << "DbConfig: " << host_ << ":" << port_ << "\n";
}

std::shared_ptr<DbConfig> DbConfigFactory::get(const std::string& host, int port) {
    std::lock_guard<std::mutex> lk(mtx_); // 并发安全
    std::string key = host + ":" + std::to_string(port);
    auto it = pool_.find(key);
    if (it != pool_.end())
        return it->second;                             // 已有,直接复用
    auto cfg = std::make_shared<DbConfig>(host, port); // 没有才造一份
    pool_[key] = cfg;
    return cfg;
}

std::size_t DbConfigFactory::size() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return pool_.size();
}

static void test_basic_sharing() {
    std::cout << "== 基本共享 ==\n";
    DbConfigFactory factory;
    auto c1 = factory.get("127.0.0.1", 3306);
    auto c2 = factory.get("127.0.0.1", 3306);    // 和 c1 同一份
    auto c3 = factory.get("192.168.1.10", 5432); // 另一份

    c1->show();
    c2->show();
    c3->show();

    std::cout << "c1 和 c2 是同一份? " << std::boolalpha << (c1.get() == c2.get())
              << "\n" // 期待 true
              << "c1 和 c3 是同一份? " << std::boolalpha << (c1.get() == c3.get())
              << "\n" // 期待 false
              << "pool size = " << factory.size() << "\n"
              << "c1.use_count = " << c1.use_count() << "\n\n";
}

// 调用方手里的 shared_ptr 在工厂的池子被清空之后,
// 对象依然存活——共享所有权成立的地基。
static void test_outlives_pool() {
    std::cout << "== 外部引用比池子活得久 ==\n";
    std::shared_ptr<DbConfig> outer;
    {
        DbConfigFactory factory;
        outer = factory.get("10.0.0.7", 6379);
        std::cout << "池子活着时 outer.use_count = " << outer.use_count() << "\n";
        // factory 离开作用域,pool_ 析构,但 outer 还在
    }
    std::cout << "池子死后   outer.use_count = " << outer.use_count() << "\n"
              << "outer 还能用吗? ";
    outer->show();
    std::cout << "\n";
}

// 同一份内部状态,被画在三个不同的「外部状态」上。
// 外部状态(连接次数)作为参数传进来,用完即弃,不占享元一丁点内存。
static void test_extrinsic_state() {
    std::cout << "== 内部状态共享 + 外部状态传参 ==\n";
    DbConfigFactory factory;
    auto cfg = factory.get("127.0.0.1", 3306);

    // 三个连接方各用各的连接次数(外部状态),共享同一份 (host, port)
    for (int connections : {3, 12, 50}) {
        std::cout << "用 " << cfg->host() << ":" << cfg->port() << " 发起了 " << connections
                  << " 次连接\n";
    }
    std::cout << "但池子里 (host, port) 只有 " << factory.size() << " 份\n\n";
}

int main() {
    test_basic_sharing();
    test_outlives_pool();
    test_extrinsic_state();
    return 0;
}
