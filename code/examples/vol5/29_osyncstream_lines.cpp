// osyncstream 整行交付：四个写线程各打三轮，每一行都是囫囵个儿，行与行的次序随调度抖动
#include <iostream>
#include <syncstream>
#include <thread>
#include <vector>

int main() {
    std::vector<std::jthread> writers;
    for (int id = 0; id != 4; ++id) {
        writers.emplace_back([id] {
            for (int r = 0; r != 3; ++r) {
                std::osyncstream(std::cout) << "writer " << id << " round " << r << '\n';
            }
        });
    }
    for (auto& t : writers) {
        t.join();
    }
    return 0;
}
