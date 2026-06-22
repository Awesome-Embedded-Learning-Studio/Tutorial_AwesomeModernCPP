#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

// 享元对象:只装内部状态(host + port)。
// 这部分状态不变、可复用,可以被多个连接方共享,
// 所以从对象里抠出来,全局只存一份。
class DbConfig {
  public:
    DbConfig(std::string host, int port) : host_(std::move(host)), port_(port) {}

    const std::string& host() const { return host_; }
    int port() const { return port_; }

    void show() const;

  private:
    std::string host_;
    int port_;
};

// 享元工厂:find-or-insert 共享池 + 一把 mutex 包住临界区。
// 同一个 (host, port) 只造一份,后面再来要直接把已有的那份还回去。
// 用 shared_ptr 是享元的共享所有权语义——工厂「管理」、调用方「使用」,
// 池子被清空后,调用方手里的引用仍能让对象存活。
class DbConfigFactory {
  public:
    std::shared_ptr<DbConfig> get(const std::string& host, int port);
    std::size_t size() const;

  private:
    mutable std::mutex mtx_;
    std::unordered_map<std::string, std::shared_ptr<DbConfig>> pool_;
};
