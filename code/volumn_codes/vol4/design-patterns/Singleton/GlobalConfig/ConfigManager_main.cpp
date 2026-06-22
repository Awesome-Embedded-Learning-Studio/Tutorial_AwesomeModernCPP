#include "ConfigManager.h"
#include <exception>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
static constexpr int thread_count = 500;
void test_single_thread() {
    std::cout << "Single thread test:\n";

    auto& config = ConfigManager::instance();

    std::optional<std::string> host = config.get_value("host");
    if (host) {
        std::cout << "host = " << *host << '\n';
    } else {
        std::cout << "host not found\n";
    }

    std::optional<std::string> port = config.get_value("port");
    if (port) {
        std::cout << "port = " << *port << '\n';
    } else {
        std::cout << "port not found\n";
    }

    std::optional<std::string> username = config.get_value("username");
    if (username) {
        std::cout << "username = " << *username << '\n';
    } else {
        std::cout << "username not found\n";
    }

    std::optional<std::string> password = config.get_value("password");
    if (password) {
        std::cout << "password = " << *password << '\n';
    } else {
        std::cout << "password not found\n";
    }

    std::optional<std::string> nonExist = config.get_value("nonexistent_key");
    if (nonExist) {
        std::cout << "nonexistent_key = " << *nonExist << '\n';
    } else {
        std::cout << "nonexistent_key not found\n";
    }

    std::cout << "\n";
}

void test_multi_thread() {
    std::cout << "Multi-thread test:\n";

    auto worker = [](int id) {
        auto& config = ConfigManager::instance();
        auto value = config.get_value("username");
        if (value) {
            std::cout << "Thread " << id << ": port = " << *value << '\n';
        } else {
            std::cout << "Thread " << id << ": port not found\n";
        }
    };

    std::vector<std::thread> threads;

    for (int i = 0; i < thread_count; ++i) {
        threads.emplace_back(worker, i + 1);
    }

    for (auto& t : threads) {
        t.join();
    }

    std::cout << "\n";
}

int main() {

    auto& instance = ConfigManager::instance();
    try {
        instance.read_from_file("test_config.txt");
    } catch (const std::exception& e) {
        std::cerr << "Error Occurs: " << e.what();
        return -1;
    }

    test_single_thread();
    test_multi_thread();
    return 0;
}
