#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>

class ExpensiveResource {
  public:
    explicit ExpensiveResource(const std::string& key) : key_(key) {
        std::cout << "加载资源: " << key_ << "\n";
    }
    ~ExpensiveResource() { std::cout << "释放资源: " << key_ << "\n"; }
    const std::string& key() const { return key_; }

  private:
    std::string key_;
};

class ResourceCache {
  public:
    std::shared_ptr<ExpensiveResource> get(const std::string& key) {
        // 先尝试从缓存获取
        auto it = cache_.find(key);
        if (it != cache_.end()) {
            if (auto cached = it->second.lock()) {
                std::cout << "缓存命中: " << key << "\n";
                return cached;
            }
            // weak_ptr 已过期，从缓存中移除
            cache_.erase(it);
        }

        // 缓存未命中，加载资源
        auto resource = std::make_shared<ExpensiveResource>(key);
        cache_[key] = resource; // 存储 weak_ptr
        return resource;
    }

    void cleanup() {
        for (auto it = cache_.begin(); it != cache_.end();) {
            if (it->second.expired()) {
                it = cache_.erase(it);
            } else {
                ++it;
            }
        }
    }

    size_t size() const {
        size_t count = 0;
        for (const auto& [k, v] : cache_) {
            if (!v.expired())
                ++count;
        }
        return count;
    }

  private:
    std::unordered_map<std::string, std::weak_ptr<ExpensiveResource>> cache_;
};

void cache_demo() {
    ResourceCache cache;

    {
        auto r1 = cache.get("texture/player.png"); // 缓存未命中，加载
        auto r2 = cache.get("texture/player.png"); // 缓存命中

        std::cout << "缓存中的条目数: " << cache.size() << "\n"; // 1

        // r1 和 r2 离开作用域
    }

    std::cout << "资源已无人使用\n";
    std::cout << "缓存中的条目数: " << cache.size() << "\n"; // 0（weak_ptr 已过期）

    auto r3 = cache.get("texture/player.png"); // 需要重新加载
}

int main() {
    cache_demo();
    return 0;
}
