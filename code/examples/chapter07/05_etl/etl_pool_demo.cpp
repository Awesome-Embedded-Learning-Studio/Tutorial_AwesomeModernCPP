// etl_pool_demo.cpp - ETL 对象池示例
// 注意：此示例需要 ETL 库支持
// https://github.com/ETLCPP/etl

#include <iostream>
#include <cstddef>
#include <array>
#include <new>

// 模拟 ETL object_pool 接口（用于演示）
template<typename T, size_t N>
class EtlPoolDemo {
    struct Node {
        Node* next;
        alignas(T) char storage[sizeof(T)];
    };

    std::array<Node, N> pool_;
    Node* free_list_;

public:
    EtlPoolDemo() {
        // 初始化空闲链表
        free_list_ = &pool_[0];
        for (size_t i = 0; i < N - 1; ++i) {
            pool_[i].next = &pool_[i + 1];
        }
        pool_[N - 1].next = nullptr;
    }

    // 分配对象（调用构造函数）
    template<typename... Args>
    T* create(Args&&... args) {
        if (!free_list_) return nullptr;

        Node* node = free_list_;
        free_list_ = free_list_->next;

        return new (node->storage) T(std::forward<Args>(args)...);
    }

    // 释放对象（调用析构函数）
    void destroy(T* obj) {
        if (!obj) return;

        obj->~T();

        Node* node = reinterpret_cast<Node*>(obj);
        node->next = free_list_;
        free_list_ = node;
    }

    size_t capacity() const { return N; }
};

// 示例对象
struct Sensor {
    int id;
    const char* name;
    int value;

    Sensor(int i, const char* n) : id(i), name(n), value(0) {}

    void read() {
        value = id * 42;  // 模拟读取
    }

    void display() const {
        std::cout << "  Sensor " << id << " (" << name
                  << "): " << value << '\n';
    }
};

int main() {
    std::cout << "=== ETL Object Pool Demo ===\n\n";

    // 创建容量为 5 的传感器对象池
    // etl::object_pool<Sensor, 5> sensor_pool;  // 使用真实 ETL 时的写法
    EtlPoolDemo<Sensor, 5> sensor_pool;

    std::cout << "Pool capacity: " << sensor_pool.capacity() << "\n\n";

    // 分配传感器对象
    std::cout << "=== Creating sensors ===\n";
    Sensor* s1 = sensor_pool.create(1, "Temperature");
    Sensor* s2 = sensor_pool.create(2, "Humidity");
    Sensor* s3 = sensor_pool.create(3, "Pressure");
    Sensor* s4 = sensor_pool.create(4, "Light");
    Sensor* s5 = sensor_pool.create(5, "Motion");

    // 尝试分配第 6 个（应该失败）
    Sensor* s6 = sensor_pool.create(6, "Sound");
    if (!s6) {
        std::cout << "Failed to create Sensor 6 (pool exhausted)\n";
    }

    // 使用传感器
    std::cout << "\n=== Reading sensors ===\n";
    s1->read();
    s2->read();
    s3->read();
    s4->read();
    s5->read();

    std::cout << "\n=== Sensor values ===\n";
    s1->display();
    s2->display();
    s3->display();
    s4->display();
    s5->display();

    // 释放一些传感器
    std::cout << "\n=== Releasing some sensors ===\n";
    sensor_pool.destroy(s2);
    sensor_pool.destroy(s4);
    std::cout << "Released Sensors 2 and 4\n";

    // 现在可以重新分配
    std::cout << "\n=== Creating new sensors ===\n";
    Sensor* s7 = sensor_pool.create(7, "Voltage");
    if (s7) {
        s7->read();
        s7->display();
    }

    Sensor* s8 = sensor_pool.create(8, "Current");
    if (s8) {
        s8->read();
        s8->display();
    }

    // 清理
    std::cout << "\n=== Cleanup ===\n";
    sensor_pool.destroy(s1);
    sensor_pool.destroy(s3);
    sensor_pool.destroy(s5);
    sensor_pool.destroy(s7);
    sensor_pool.destroy(s8);

    std::cout << "\n=== Key benefits ===\n";
    std::cout << "- Pre-allocated memory, no fragmentation\n";
    std::cout << "- O(1) allocation and deallocation\n";
    std::cout << "- Objects constructed on-demand\n";
    std::cout << "- Perfect for fixed-size object collections\n";

    return 0;
}
