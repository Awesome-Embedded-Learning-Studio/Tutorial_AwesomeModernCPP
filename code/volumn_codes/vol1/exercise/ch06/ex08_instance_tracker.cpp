/**
 * @file ex08_instance_tracker.cpp
 * @brief 练习：TrackedObject 实例追踪
 *
 * 使用静态成员追踪类的存活实例数和总创建数。
 * 构造时递增 active_count 和 total_created，
 * 析构时递减 active_count。
 */

#include <iostream>

class TrackedObject {
private:
    static int active_count_;
    static int total_created_;
    int id_;

public:
    explicit TrackedObject(int id) : id_(id) {
        ++active_count_;
        ++total_created_;
        std::cout << "  构造 #" << id_
                  << " (active=" << active_count_
                  << ", total=" << total_created_ << ")\n";
    }

    ~TrackedObject() {
        --active_count_;
        std::cout << "  析构 #" << id_
                  << " (active=" << active_count_
                  << ", total=" << total_created_ << ")\n";
    }

    static int active_count() { return active_count_; }
    static int total_created() { return total_created_; }

    int id() const { return id_; }
};

// 静态成员定义
int TrackedObject::active_count_ = 0;
int TrackedObject::total_created_ = 0;

int main() {
    std::cout << "===== TrackedObject 实例追踪 =====\n\n";

    std::cout << "初始状态:\n";
    std::cout << "  active_count = " << TrackedObject::active_count() << "\n";
    std::cout << "  total_created = " << TrackedObject::total_created() << "\n\n";

    // 创建 5 个对象
    std::cout << "创建 5 个对象:\n";
    TrackedObject* objs[5];
    for (int i = 0; i < 5; ++i) {
        objs[i] = new TrackedObject(i + 1);
    }

    std::cout << "\n创建后:\n";
    std::cout << "  active_count = " << TrackedObject::active_count() << "\n";
    std::cout << "  total_created = " << TrackedObject::total_created() << "\n\n";

    // 销毁 3 个对象
    std::cout << "销毁 3 个对象 (#1, #3, #5):\n";
    delete objs[0];
    delete objs[2];
    delete objs[4];

    std::cout << "\n销毁后:\n";
    std::cout << "  active_count = " << TrackedObject::active_count() << "\n";
    std::cout << "  total_created = " << TrackedObject::total_created() << "\n\n";

    // 验证
    bool active_ok = (TrackedObject::active_count() == 2);
    bool total_ok = (TrackedObject::total_created() == 5);

    std::cout << "验证:\n";
    std::cout << "  active_count == 2: "
              << (active_ok ? "通过" : "失败") << "\n";
    std::cout << "  total_created == 5: "
              << (total_ok ? "通过" : "失败") << "\n\n";

    // 释放剩余对象
    std::cout << "释放剩余对象:\n";
    delete objs[1];
    delete objs[3];

    std::cout << "\n全部释放后:\n";
    std::cout << "  active_count = " << TrackedObject::active_count() << "\n";
    std::cout << "  total_created = " << TrackedObject::total_created() << "\n";

    return 0;
}
