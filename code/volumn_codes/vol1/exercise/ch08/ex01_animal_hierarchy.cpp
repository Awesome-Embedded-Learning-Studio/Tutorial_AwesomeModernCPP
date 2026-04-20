/**
 * @file ex01_animal_hierarchy.cpp
 * @brief 练习：单继承与构造/析构顺序
 *
 * Animal 基类（private name_，protected sound_），
 * Dog 和 Cat 派生类。Dog 额外持有 breed_ 并提供 describe()。
 * 观察构造与析构的调用顺序。
 */

#include <iostream>
#include <string>

class Animal {
private:
    std::string name_;

protected:
    std::string sound_;

public:
    explicit Animal(const std::string& name, const std::string& sound)
        : name_(name), sound_(sound)
    {
        std::cout << "  Animal(\"" << name_
                  << "\") 构造\n";
    }

    virtual ~Animal()
    {
        std::cout << "  Animal(\"" << name_
                  << "\") 析构\n";
    }

    const std::string& name() const { return name_; }
    const std::string& sound() const { return sound_; }

    void speak() const
    {
        std::cout << name_ << " says: " << sound_ << "!\n";
    }
};

class Dog : public Animal {
private:
    std::string breed_;

public:
    Dog(const std::string& name, const std::string& breed)
        : Animal(name, "Woof"), breed_(breed)
    {
        std::cout << "    Dog(\"" << name
                  << "\", breed=\"" << breed_
                  << "\") 构造\n";
    }

    ~Dog() override
    {
        std::cout << "    Dog(\"" << name()
                  << "\", breed=\"" << breed_
                  << "\") 析构\n";
    }

    const std::string& breed() const { return breed_; }

    void describe() const
    {
        std::cout << name() << " is a " << breed_
                  << " and goes " << sound_ << ".\n";
    }
};

class Cat : public Animal {
public:
    explicit Cat(const std::string& name)
        : Animal(name, "Meow")
    {
        std::cout << "    Cat(\"" << name
                  << "\") 构造\n";
    }

    ~Cat() override
    {
        std::cout << "    Cat(\"" << name()
                  << "\") 析构\n";
    }

    void purr() const
    {
        std::cout << name() << " purrs: prrrr...\n";
    }
};

// ============================================================
// main
// ============================================================
int main()
{
    std::cout << "===== Animal 继承层次 =====\n\n";

    std::cout << "--- 创建 Dog ---\n";
    {
        Dog dog("Buddy", "Golden Retriever");
        dog.speak();
        dog.describe();
        std::cout << "\n  离开 Dog 作用域...\n";
    }

    std::cout << "\n--- 创建 Cat ---\n";
    {
        Cat cat("Whiskers");
        cat.speak();
        cat.purr();
        std::cout << "\n  离开 Cat 作用域...\n";
    }

    std::cout << "\n--- 构造/析构顺序总结 ---\n";
    std::cout << "构造顺序: 基类 Animal -> 派生类 (Dog/Cat)\n";
    std::cout << "析构顺序: 派生类 (Dog/Cat) -> 基类 Animal\n";

    return 0;
}
