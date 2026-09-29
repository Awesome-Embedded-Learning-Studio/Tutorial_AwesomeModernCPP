// barrier 多轮汇合：完成函数替全队聚合，各轮 round_total = 10 / 20，两轮合计 30
#include <array>
#include <barrier>
#include <iostream>
#include <syncstream>
#include <thread>
#include <vector>

int main() {
    // barrier：完成函数在全队放行之前替全队聚合
    constexpr int n = 4;
    constexpr int rounds = 2;
    std::array<int, n> partial{};
    int round_total = 0;
    long grand_total = 0;
    std::barrier sync_point(n, [&]() noexcept {
        round_total = 0;
        for (int x : partial) {
            round_total += x;
        }
        grand_total += round_total;
    });
    std::vector<std::jthread> workers;
    for (int id = 0; id != n; ++id) {
        workers.emplace_back([&, id] {
            for (int r = 0; r != rounds; ++r) {
                partial[id] = (id + 1) * (r + 1); // 本轮贡献
                sync_point.arrive_and_wait();     // 到齐后完成函数先跑，然后全队放行
                std::osyncstream(std::cout) << "第 " << r + 1 << " 轮：worker " << id
                                            << " 看到 round_total = " << round_total << '\n';
            }
        });
    }
    for (auto& t : workers) {
        t.join();
    }
    std::cout << "两轮合计 grand_total = " << grand_total << "（期望 30）\n";
    return 0;
}
