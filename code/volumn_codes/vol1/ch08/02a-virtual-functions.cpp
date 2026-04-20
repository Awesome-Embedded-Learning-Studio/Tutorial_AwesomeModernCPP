// 02a-virtual-functions.cpp
// 演示：没有 virtual 时基类指针的静态绑定
// 编译: g++ -Wall -Wextra -std=c++17 02a-virtual-functions.cpp -o 02a-virtual-functions

#include <cstdio>

class Shape {
public:
    void draw() const { printf("Shape::draw()\n"); }
};

class Circle : public Shape {
public:
    void draw() const { printf("Circle::draw()\n"); }
};

class Rectangle : public Shape {
public:
    void draw() const { printf("Rectangle::draw()\n"); }
};

int main() {
    Shape* shapes[3];
    shapes[0] = new Shape();
    shapes[1] = new Circle();
    shapes[2] = new Rectangle();

    for (int i = 0; i < 3; ++i) {
        shapes[i]->draw();
    }

    for (int i = 0; i < 3; ++i) {
        delete shapes[i];
    }
    return 0;
}
