#include "AreaCal.h"
#include <memory>
#include <print>
#include <vector>
int main() {
    std::vector<std::unique_ptr<Shape>> shapes;
    shapes.emplace_back(std::make_unique<Circle>(3.0));
    shapes.emplace_back(std::make_unique<Rectangle>(4.0, 5.0));
    shapes.emplace_back(std::make_unique<Triangle>(6.0, 2.0));

    AreaCalculatorVisitor calculator;
    for (auto& shape : shapes) {
        shape->accept(calculator);
    }

    std::println("Total area: {}", calculator.total_area);
    return 0;
}
