#include "Proxy.h"

#include <iostream>
#include <string>
#include <vector>

// ---- 第一步:属性代理演示 ----
static void demo_property_proxy() {
    std::cout << "=== 1. Property Proxy ===\n";
    Property<int> port(8080);
    int v = port; // 隐式转 T -> getter
    std::cout << "read = " << v << "\n";
    port = 9090; // operator= -> setter
    std::cout << "after assign = " << port.raw() << "\n\n";
}

// ---- 第二步:虚拟代理演示(单线程懒加载 + 并发安全) ----
static void demo_virtual_proxy() {
    std::cout << "=== 2. Virtual Proxy (lazy load) ===\n";
    ImageProxy proxy("cat.png");
    std::cout << "before display, load_count = " << RealImage::load_count << "\n";
    proxy.display();
    proxy.display();
    std::cout << "after 2 displays, load_count = " << RealImage::load_count << " (expect 1)\n\n";

    // 并发安全演示:多个线程首次访问同一个代理,call_once 保证只构造一次
    std::cout << "--- concurrent display, count starts at " << RealImage::load_count << " ---\n";
    ImageProxy shared_proxy("dog.png");
    std::vector<std::thread> ts;
    for (int i = 0; i < 20; ++i) {
        ts.emplace_back([&shared_proxy] { shared_proxy.display(); });
    }
    for (auto& t : ts)
        t.join();
    std::cout << "after 20 threads, load_count = " << RealImage::load_count << " (expect 2)\n\n";
}

// ---- 第三步:保护代理演示 ----
static void demo_protection_proxy() {
    std::cout << "=== 3. Protection Proxy ===\n";
    Sensitive real;
    ProtectionProxy allowed(&real, /*allowed=*/true);
    ProtectionProxy denied(&real, /*allowed=*/false);

    std::cout << "allowed call: ";
    allowed.secret();

    std::cout << "denied call: ";
    try {
        denied.secret();
    } catch (const std::exception& e) {
        std::cout << "caught: " << e.what() << "\n";
    }
    std::cout << "\n";
}

// ---- 第四步:远程代理演示 ----
static void demo_remote_proxy() {
    std::cout << "=== 4. Remote Proxy ===\n";
    Transport transport;
    RemoteServiceProxy service(&transport);

    std::cout << "get_time() = " << service.get_time() << "\n";
    std::cout << "remote_compute(3, 4) = " << service.remote_compute(3, 4) << "\n\n";
}

// ---- 第五步:缓存代理演示 ----
static void demo_caching_proxy() {
    std::cout << "=== 5. Caching Proxy ===\n";
    // 真实对象:把被调用次数记下来,验证第二次命中缓存不再算
    class RealExpensive : public Expensive {
      public:
        int compute(int x) override {
            ++calls;
            return x * x;
        }
        int calls = 0;
    };

    RealExpensive real;
    CachingProxy cache(&real);

    std::cout << "compute(5) = " << cache.compute(5) << "\n";
    std::cout << "compute(5) = " << cache.compute(5) << " (cached)\n";
    std::cout << "compute(6) = " << cache.compute(6) << "\n";
    std::cout << "real.compute called " << real.calls << " times (expect 2)\n\n";
}

// ---- 第六步:同步代理演示 ----
static void demo_sync_proxy() {
    std::cout << "=== 6. Synchronization Proxy ===\n";
    // 非线程安全的真实对象:计数器裸自增,套上 SyncProxy 后多线程安全
    class UnsafeCounter : public SomeInterface {
      public:
        void op() override { ++count; }
        int count = 0;
    };

    UnsafeCounter real;
    SyncProxy synced(&real);

    std::vector<std::thread> ts;
    for (int i = 0; i < 1000; ++i) {
        ts.emplace_back([&synced] { synced.op(); });
    }
    for (auto& t : ts)
        t.join();
    std::cout << "after 1000 threads, counter = " << real.count << " (expect 1000)\n\n";
}

// ---- 第七步:COW 代理演示 ----
static void demo_cow_proxy() {
    std::cout << "=== 7. Copy-On-Write Proxy ===\n";
    CowString a("hello");
    CowString b = a; // 共享同一份底层

    std::cout << "after copy, a.str() = " << a.str() << ", b.str() = " << b.str() << " (shared)\n";

    b.append(" world"); // b 要写,先复制再改,不污染 a
    std::cout << "after b.append, a.str() = " << a.str() << ", b.str() = " << b.str()
              << " (forked)\n\n";
}

int main() {
    demo_property_proxy();
    demo_virtual_proxy();
    demo_protection_proxy();
    demo_remote_proxy();
    demo_caching_proxy();
    demo_sync_proxy();
    demo_cow_proxy();
    return 0;
}
