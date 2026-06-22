#include "logger.h"
#include <string>
#include <thread>
#include <vector>
static constexpr unsigned int TEST_TIME = 50;

void test_functions(const std::string messages) {
    auto& logger_instance = SimpleLogger::instance();
    logger_instance.log_messages(messages);
}

int main() {

    std::vector<std::thread> test_threads;

    for (int i = 0; i < TEST_TIME; i++) {
        std::string result = "Hello from Times: " + std::to_string(i);
        test_threads.emplace_back(test_functions, result);
    }

    for (auto& each : test_threads) {
        each.join();
    }
}
