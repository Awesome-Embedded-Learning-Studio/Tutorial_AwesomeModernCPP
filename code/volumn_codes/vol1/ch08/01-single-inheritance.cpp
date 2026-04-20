// inheritance.cpp
#include <iostream>
#include <string>

class Vehicle {
private:
    double speed_;

protected:
    std::string brand_;

public:
    Vehicle(const std::string& brand, double speed)
        : brand_(brand), speed_(speed)
    {
        std::cout << "  [Vehicle] constructed: " << brand_ << "\n";
    }

    ~Vehicle()
    {
        std::cout << "  [Vehicle] destroyed: " << brand_ << "\n";
    }

    double speed() const { return speed_; }
    const std::string& brand() const { return brand_; }

    void describe() const
    {
        std::cout << "  " << brand_ << " at " << speed_ << " km/h";
    }
};

class Car : public Vehicle {
private:
    int seats_;

public:
    Car(const std::string& brand, double speed, int seats)
        : Vehicle(brand, speed), seats_(seats)
    {
        std::cout << "  [Car] constructed: " << seats_ << " seats\n";
    }

    ~Car() { std::cout << "  [Car] destroyed\n"; }

    void describe() const
    {
        Vehicle::describe();
        std::cout << ", " << seats_ << " seats\n";
    }
};

class Truck : public Vehicle {
private:
    double payload_;

public:
    Truck(const std::string& brand, double speed, double payload)
        : Vehicle(brand, speed), payload_(payload)
    {
        std::cout << "  [Truck] constructed: " << payload_ << " tons\n";
    }

    ~Truck() { std::cout << "  [Truck] destroyed\n"; }

    void describe() const
    {
        Vehicle::describe();
        std::cout << ", " << payload_ << " tons\n";
    }
};

void show_vehicle(const Vehicle& v)   // 引用，不切片
{
    std::cout << "[ref] ";
    v.describe();
    std::cout << "\n";
}

void show_vehicle_sliced(Vehicle v)   // 值传递，切片！
{
    std::cout << "[val] ";
    v.describe();
    std::cout << "\n";
}

int main()
{
    std::cout << "=== 构造顺序 ===\n";
    Car car("Toyota", 120.0, 5);

    std::cout << "\n=== 按引用传递 ===\n";
    show_vehicle(car);

    std::cout << "\n=== 按值传递（切片）===\n";
    show_vehicle_sliced(car);

    std::cout << "\n=== 另一个派生类 ===\n";
    {
        Truck truck("Volvo", 90.0, 15.5);
        show_vehicle(truck);
    }

    std::cout << "\n=== 析构顺序 ===\n";
    return 0;
}
