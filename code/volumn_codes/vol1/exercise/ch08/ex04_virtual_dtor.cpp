/**
 * @file ex04_virtual_dtor.cpp
 * @brief 练习：虚析构函数的重要性
 *
 * 对比基类析构函数是否为 virtual 时的行为：
 * - 非虚析构：delete 基类指针时派生类析构函数被跳过
 * - 虚析构：正确调用完整析构链
 */

#include <iostream>
#include <memory>
#include <string>

// ----- 版本 A：非虚析构（有 Bug）-----
class BadBase {
private:
    std::string name_;

public:
    explicit BadBase(const std::string& name) : name_(name)
    {
        std::cout << "  BadBase(\"" << name_ << "\") 构造\n";
    }

    // 注意：析构函数不是 virtual！
    ~BadBase()
    {
        std::cout << "  BadBase(\"" << name_ << "\") 析构\n";
    }

    virtual void info() const
    {
        std::cout << "    BadBase: " << name_ << "\n";
    }
};

class BadDerived : public BadBase {
private:
    std::string extra_;

public:
    BadDerived(const std::string& name, const std::string& extra)
        : BadBase(name), extra_(extra)
    {
        std::cout << "  BadDerived(\"" << extra_ << "\") 构造\n";
    }

    ~BadDerived()
    {
        std::cout << "  BadDerived(\"" << extra_ << "\") 析构\n";
    }

    void info() const override
    {
        std::cout << "    BadDerived: " << extra_ << "\n";
    }
};

// ----- 版本 B：虚析构（正确）-----
class GoodBase {
private:
    std::string name_;

public:
    explicit GoodBase(const std::string& name) : name_(name)
    {
        std::cout << "  GoodBase(\"" << name_ << "\") 构造\n";
    }

    // 虚析构函数
    virtual ~GoodBase()
    {
        std::cout << "  GoodBase(\"" << name_ << "\") 析构\n";
    }

    virtual void info() const
    {
        std::cout << "    GoodBase: " << name_ << "\n";
    }
};

class GoodDerived : public GoodBase {
private:
    std::string extra_;

public:
    GoodDerived(const std::string& name, const std::string& extra)
        : GoodBase(name), extra_(extra)
    {
        std::cout << "  GoodDerived(\"" << extra_ << "\") 构造\n";
    }

    ~GoodDerived() override
    {
        std::cout << "  GoodDerived(\"" << extra_ << "\") 析构\n";
    }

    void info() const override
    {
        std::cout << "    GoodDerived: " << extra_ << "\n";
    }
};

// ============================================================
// main
// ============================================================
int main()
{
    std::cout << "===== 虚析构函数的重要性 =====\n\n";

    std::cout << "--- 版本 A：非虚析构（派生类析构被跳过）---\n";
    {
        BadBase* ptr = new BadDerived("base", "derived");
        ptr->info();  // 多态正常（虚函数）
        std::cout << "  执行 delete ptr...\n";
        delete ptr;   // Bug: BadDerived 析构不会被调用！
    }

    std::cout << "\n--- 版本 B：虚析构（完整析构链）---\n";
    {
        GoodBase* ptr = new GoodDerived("base", "derived");
        ptr->info();
        std::cout << "  执行 delete ptr...\n";
        delete ptr;   // 正确: 先析构 GoodDerived，再析构 GoodBase
    }

    std::cout << "\n--- 版本 B 用 unique_ptr（同样正确）---\n";
    {
        std::unique_ptr<GoodBase> uptr =
            std::make_unique<GoodDerived>("base2", "derived2");
        uptr->info();
        std::cout << "  离开作用域，unique_ptr 自动销毁...\n";
    }

    std::cout << "\n--- 总结 ---\n";
    std::cout << "  规则：只要类有虚函数，析构函数就必须是 virtual。\n";
    std::cout << "  否则通过基类指针 delete 派生对象会导致未定义行为。\n";

    return 0;
}
