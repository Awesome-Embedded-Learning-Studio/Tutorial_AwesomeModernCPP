// multi_inherit.cpp
// 编译：g++ -Wall -Wextra -std=c++17 -o multi_inherit multi_inherit.cpp

#include <cstdio>
#include <string>

// --- 接口多继承 ---
class Drawable {
public:
    virtual ~Drawable() = default;
    virtual void draw() const = 0;
};

class Serializable {
public:
    virtual ~Serializable() = default;
    virtual std::string serialize() const = 0;
};

class Shape : public Drawable, public Serializable {
protected:
    std::string name_;
public:
    explicit Shape(std::string name) : name_(std::move(name)) {}
};

class Circle : public Shape {
    double radius_;
public:
    explicit Circle(double r) : Shape("Circle"), radius_(r) {}
    void draw() const override {
        printf("[Draw] %s (r=%.2f)\n", name_.c_str(), radius_);
    }
    std::string serialize() const override {
        return "{\"type\":\"circle\",\"r\":" + std::to_string(radius_) + "}";
    }
};

class Rectangle : public Shape {
    double w_, h_;
public:
    Rectangle(double w, double h) : Shape("Rect"), w_(w), h_(h) {}
    void draw() const override {
        printf("[Draw] %s (%.2fx%.2f)\n", name_.c_str(), w_, h_);
    }
    std::string serialize() const override {
        return "{\"type\":\"rect\",\"w\":" + std::to_string(w_) +
               ",\"h\":" + std::to_string(h_) + "}";
    }
};

// --- 菱形继承（非虚） ---
class Component {
public:
    int version;
    Component() : version(1) { printf("  Component()\n"); }
};

class Renderer : public Component {
public:
    Renderer() { printf("  Renderer()\n"); }
    void render() { printf("  Render (v=%d)\n", version); }
};

class EventHandler : public Component {
public:
    EventHandler() { printf("  EventHandler()\n"); }
    void click() { printf("  Click (v=%d)\n", version); }
};

class Widget : public Renderer, public EventHandler {
public:
    Widget() { printf("  Widget()\n"); }
};

// --- 菱形继承（虚继承） ---
class VComponent {
public:
    int version;
    VComponent() : version(1) { printf("  VComponent()\n"); }
};

class VRenderer : virtual public VComponent {
public:
    VRenderer() { printf("  VRenderer()\n"); }
    void render() { printf("  Render (v=%d)\n", version); }
};

class VEventHandler : virtual public VComponent {
public:
    VEventHandler() { printf("  VEventHandler()\n"); }
    void click() { printf("  Click (v=%d)\n", version); }
};

class VWidget : public VRenderer, public VEventHandler {
public:
    VWidget() : VComponent(), VRenderer(), VEventHandler() {
        printf("  VWidget()\n");
    }
};

int main()
{
    printf("=== Interface Multi-Inheritance ===\n");
    Circle c(5.0);
    Rectangle r(3.0, 4.0);
    Drawable* drawables[] = {&c, &r};
    for (auto* d : drawables) { d->draw(); }

    printf("\n=== Diamond (no virtual) ===\n");
    Widget w;
    w.Renderer::version = 2;
    w.EventHandler::version = 3;
    printf("  sizeof(Widget) = %zu\n", sizeof(Widget));

    printf("\n=== Diamond (virtual) ===\n");
    VWidget vw;
    vw.version = 42;  // OK！只有一份
    printf("  sizeof(VWidget) = %zu\n", sizeof(VWidget));

    return 0;
}
