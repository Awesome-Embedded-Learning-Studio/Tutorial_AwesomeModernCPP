// friend_demo.cpp
#include <array>
#include <cstdio>

class Vector;

class Matrix {
private:
    std::array<std::array<float, 3>, 3> data;
public:
    Matrix() : data{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}} {}
    void set(int row, int col, float value) { data[row][col] = value; }
    void print() const
    {
        for (int i = 0; i < 3; ++i)
            std::printf("| %.2f %.2f %.2f |\n",
                        data[i][0], data[i][1], data[i][2]);
    }
    // 授权 Vector 访问私有成员
    friend class Vector;
};

class Vector {
private:
    std::array<float, 3> v;
public:
    Vector(float x, float y, float z) : v{x, y, z} {}
    // 友元权限：直接访问 Matrix 内部数组
    Vector transform(const Matrix& m) const
    {
        float nx = m.data[0][0] * v[0] + m.data[0][1] * v[1] + m.data[0][2] * v[2];
        float ny = m.data[1][0] * v[0] + m.data[1][1] * v[1] + m.data[1][2] * v[2];
        float nz = m.data[2][0] * v[0] + m.data[2][1] * v[1] + m.data[2][2] * v[2];
        return Vector(nx, ny, nz);
    }
    void print() const
    { std::printf("(%.2f, %.2f, %.2f)\n", v[0], v[1], v[2]); }
};

int main()
{
    Matrix m;
    m.set(0, 0, 2.0f);
    m.set(1, 1, 3.0f);
    m.set(2, 2, 0.5f);
    Vector v(1.0f, 2.0f, 4.0f);
    Vector result = v.transform(m);
    std::printf("Matrix:\n");
    m.print();
    std::printf("Vector:  ");
    v.print();
    std::printf("Result:  ");
    result.print();
    return 0;
}
