/**
 * @file ex08_diamond_fix.cpp
 * @brief 练习：菱形继承与虚继承
 *
 * Device 基类含 device_id_。
 * Networkable 和 Monitorable 虚继承 Device。
 * SmartDevice 多重继承两者，device_id_ 不再歧义。
 */

#include <iostream>
#include <string>

// ----- 版本 A：非虚继承（产生歧义）-----
class BadDevice {
protected:
    int device_id_;

public:
    explicit BadDevice(int id) : device_id_(id)
    {
        std::cout << "  BadDevice(" << device_id_ << ") 构造\n";
    }

    virtual ~BadDevice() = default;

    int device_id() const { return device_id_; }
};

class BadNetworkable : public BadDevice {
public:
    explicit BadNetworkable(int id) : BadDevice(id)
    {
        std::cout << "  BadNetworkable 构造\n";
    }

    void connect() const
    {
        std::cout << "  Networkable#" << device_id_
                  << " 已连接\n";
    }
};

class BadMonitorable : public BadDevice {
public:
    explicit BadMonitorable(int id) : BadDevice(id)
    {
        std::cout << "  BadMonitorable 构造\n";
    }

    void check_status() const
    {
        std::cout << "  Monitorable#" << device_id_
                  << " 状态正常\n";
    }
};

// 两个 BadDevice 子对象 —— device_id 歧义
class BadSmartDevice : public BadNetworkable,
                       public BadMonitorable {
public:
    BadSmartDevice(int net_id, int mon_id)
        : BadNetworkable(net_id), BadMonitorable(mon_id) {}
};

// ----- 版本 B：虚继承（正确）-----
class Device {
protected:
    int device_id_;

public:
    explicit Device(int id) : device_id_(id)
    {
        std::cout << "  Device(" << device_id_ << ") 构造\n";
    }

    virtual ~Device() = default;

    int device_id() const { return device_id_; }
    void set_device_id(int id) { device_id_ = id; }
};

class Networkable : virtual public Device {
public:
    explicit Networkable(int id) : Device(id)
    {
        std::cout << "  Networkable 构造\n";
    }

    void connect() const
    {
        std::cout << "  Networkable#" << device_id_
                  << " 已连接\n";
    }
};

class Monitorable : virtual public Device {
public:
    explicit Monitorable(int id) : Device(id)
    {
        std::cout << "  Monitorable 构造\n";
    }

    void check_status() const
    {
        std::cout << "  Monitorable#" << device_id_
                  << " 状态正常\n";
    }
};

// 只有一个 Device 子对象 —— device_id 无歧义
class SmartDevice : public Networkable,
                    public Monitorable {
public:
    // 虚继承时，最底层的派生类负责初始化虚基类
    explicit SmartDevice(int id)
        : Device(id), Networkable(id), Monitorable(id)
    {
        std::cout << "  SmartDevice 构造\n";
    }

    void report() const
    {
        std::cout << "  SmartDevice#" << device_id()
                  << " 汇报:\n";
        connect();
        check_status();
    }
};

// ============================================================
// main
// ============================================================
int main()
{
    std::cout << "===== 菱形继承与虚继承 =====\n\n";

    std::cout << "--- 版本 A：非虚继承（歧义）---\n";
    {
        BadSmartDevice bad(1, 2);
        // 两个 device_id：必须显式指定路径
        std::cout << "  Networkable::device_id() = "
                  << bad.BadNetworkable::device_id() << "\n";
        std::cout << "  Monitorable::device_id() = "
                  << bad.BadMonitorable::device_id() << "\n";
        std::cout << "  问题: 同一个设备有两个不同的 id！\n";
    }

    std::cout << "\n--- 版本 B：虚继承（正确）---\n";
    {
        SmartDevice good(42);
        good.report();
        std::cout << "  device_id() = " << good.device_id()
                  << " (唯一，无歧义)\n";
        good.set_device_id(99);
        std::cout << "  修改后 device_id() = "
                  << good.device_id() << "\n";
    }

    std::cout << "\n--- 总结 ---\n";
    std::cout << "  菱形继承产生歧义时，使用 virtual 继承\n";
    std::cout << "  虚基类由最底层派生类负责初始化\n";
    std::cout << "  实际设计中应优先使用组合而非多重继承\n";

    return 0;
}
