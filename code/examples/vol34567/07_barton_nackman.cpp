// Barton-Nackman trick & friend injection demo
#include <cmath>
#include <compare>
#include <cstdint>
#include <iostream>

// Simple Point with friend-injected operators
template <typename T> class Point {
    T x_, y_;

  public:
    constexpr Point() : x_(0), y_(0) {}
    constexpr Point(T x, T y) : x_(x), y_(y) {}

    constexpr T x() const { return x_; }
    constexpr T y() const { return y_; }

    // Friend injection: these are non-template functions
    // injected into the enclosing namespace, found via ADL
    friend bool operator==(const Point& a, const Point& b) { return a.x_ == b.x_ && a.y_ == b.y_; }
    friend bool operator!=(const Point& a, const Point& b) { return !(a == b); }

    // Arithmetic operators via friend injection
    friend Point operator+(const Point& a, const Point& b) { return {a.x_ + b.x_, a.y_ + b.y_}; }
    friend Point operator-(const Point& a, const Point& b) { return {a.x_ - b.x_, a.y_ - b.y_}; }
    friend Point operator*(const Point& p, T s) { return {p.x_ * s, p.y_ * s}; }

    // C++20 spaceship operator (also friend-injected)
    friend auto operator<=>(const Point& a, const Point& b) {
        if (auto cmp = a.x_ <=> b.x_; cmp != 0)
            return cmp;
        return a.y_ <=> b.y_;
    }

    // Stream output via friend injection
    friend std::ostream& operator<<(std::ostream& os, const Point& p) {
        return os << '(' << p.x_ << ", " << p.y_ << ')';
    }

    // Utility methods
    [[nodiscard]] constexpr double distance_from_origin() const {
        return std::hypot(static_cast<double>(x_), static_cast<double>(y_));
    }
    [[nodiscard]] constexpr T dot(const Point& other) const {
        return x_ * other.x_ + y_ * other.y_;
    }
};

int main() {
    Point<int> p1{3, 4};
    Point<int> p2{1, 2};

    // Comparison via ADL (friend injection)
    std::cout << std::boolalpha;
    std::cout << "p1 == p1: " << (p1 == p1) << "\n";
    std::cout << "p1 != p2: " << (p1 != p2) << "\n";
    std::cout << "p1 > p2:  " << (p1 > p2) << "\n"; // uses <=>

    // Arithmetic via ADL
    auto p3 = p1 + p2;
    auto p4 = p1 * 2;
    std::cout << "p1 + p2 = " << p3 << "\n";
    std::cout << "p1 * 2  = " << p4 << "\n";

    // Distance
    std::cout << "p1 distance from origin: " << p1.distance_from_origin() << "\n";
    std::cout << "p1 . p2 = " << p1.dot(p2) << "\n";

    return 0;
}
