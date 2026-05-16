#include <iostream>
#include <vector>
#include <memory>
#include <concepts>

class Shape {
public:
    virtual ~Shape() = default;
    virtual void draw() const = 0;
};

class Circle : public Shape {
public:
    void draw() const override { std::cout << "OOP: drawing circle\n"; }
};

class Rectangle : public Shape {
public:
    void draw() const override { std::cout << "OOP: drawing rectangle\n"; }
};

void draw_all_oop(const std::vector<std::unique_ptr<Shape>>& shapes) {
    for (const auto& s : shapes) s->draw();
}

struct Triangle {
    void draw() const { std::cout << "Generic: drawing triangle\n"; }
};

struct Star {
    void draw() const { std::cout << "Generic: drawing star\n"; }
};

template <typename T>
concept Drawable = requires(const T& t) { { t.draw() }; };

template <std::ranges::range R>
    requires Drawable<std::ranges::range_value_t<R>>
void draw_all_generic(const R& items) {
    for (const auto& item : items) item.draw();
}

int main() {
    std::cout << "=== OOP ===\n";
    std::vector<std::unique_ptr<Shape>> oop_shapes;
    oop_shapes.push_back(std::make_unique<Circle>());
    oop_shapes.push_back(std::make_unique<Rectangle>());
    draw_all_oop(oop_shapes);

    std::cout << "\n=== Generic ===\n";
    std::vector<Triangle> triangles{Triangle{}, Triangle{}};
    std::vector<Star> stars{Star{}};
    draw_all_generic(triangles);
    draw_all_generic(stars);

    std::cout << "\n=== Generic with OOP types ===\n";
    std::vector<Circle> circles{Circle{}, Circle{}};
    draw_all_generic(circles);
    return 0;
}
