#pragma once

// 桥接模式:把「抽象」(Abstraction) 和「实现」(Implementor) 拆成两条独立的继承链,
// 让抽象里持有一个实现接口的引用,用这个引用把两条链「桥接」起来。
//
// 本头文件演示文章的第一部分:经典的「形状 x 后端」二维分离。
// - DrawingAPI:实现接口(Implementor),只描述「画的能力」,不管形状。
// - OpenGLApi / DirectXApi:具体实现(ConcreteImplementor),真正的后端。
// - Shape:抽象(Abstraction),持有实现接口,自己不画,委托给后端。
// - Circle:精化抽象(RefinedAbstraction),只负责自己的几何。

#include <iostream>
#include <memory>
#include <utility>

// Implementor(实现接口):只描述「画的能力」,不管形状
struct DrawingAPI {
    virtual ~DrawingAPI() = default;
    virtual void draw_circle(double x, double y, double r) = 0;
};

// ConcreteImplementor:真正的后端实现
struct OpenGLApi : DrawingAPI {
    void draw_circle(double x, double y, double r) override {
        std::cout << "[OpenGL]  绘制圆 中心(" << x << "," << y << ") 半径 " << r << "\n";
    }
};

struct DirectXApi : DrawingAPI {
    void draw_circle(double x, double y, double r) override {
        std::cout << "[DirectX] 绘制圆 中心(" << x << "," << y << ") 半径 " << r << "\n";
    }
};

// Abstraction:持有实现接口,自己不画,委托给后端
class Shape {
  public:
    explicit Shape(std::unique_ptr<DrawingAPI> api) : api_(std::move(api)) {}
    virtual ~Shape() = default;
    virtual void draw() = 0;

  protected:
    std::unique_ptr<DrawingAPI> api_;
};

// RefinedAbstraction:具体形状,只负责自己的几何
class Circle : public Shape {
  public:
    Circle(double x, double y, double r, std::unique_ptr<DrawingAPI> api)
        : Shape(std::move(api)), x_(x), y_(y), r_(r) {}
    void draw() override { api_->draw_circle(x_, y_, r_); }

  private:
    double x_, y_, r_;
};
