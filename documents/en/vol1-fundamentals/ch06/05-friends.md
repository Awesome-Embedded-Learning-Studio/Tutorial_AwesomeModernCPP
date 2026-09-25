---
title: "Friends"
description: "Understand how friend functions and friend classes work, and master the legitimate use cases for friendship as well as the risks of overusing it"
chapter: 6
order: 5
difficulty: beginner
reading_time_minutes: 16
platform: host
prerequisites:
  - "static Members"
tags:
  - cpp-modern
  - host
  - beginner
  - 入门
  - 基础
cpp_standard: [11, 14, 17, 20]
translation:
  source: documents/vol1-fundamentals/ch06/05-friends.md
  source_hash: 3bda8a31dd87855b978a402b3cbd1f1c2d65e4d4fa2630a946a7c462751a9716
  translated_at: '2026-09-25T11:01:52+00:00'
  engine: anthropic
  token_count: 3000
---

# Friends: Granting Access to Private Members, On Purpose

Hey! My friend! Today we're introducing friend! Don't get the wrong idea—friend is actually a C++ keyword, haha! In the previous chapters we kept emphasizing encapsulation—`private` members are hidden inside the class, and external code can only manipulate objects through the `public` interface. But once in a while you'll run into a situation where some external function, or another class, genuinely needs to access private members, and the access is legitimate and unavoidable. C++ provides a dedicated mechanism for exactly this scenario: **`friend`**.

The essence of friendship is **targeted authorization**: the class's author deliberately declares, "I trust this function/class, and I allow it to see my private members." It is not tearing encapsulation down entirely (for that you could just make everything `public`); it grants access precisely, to specific named functions or classes. Next we'll take the three forms of friends—friend functions, friend classes, and friend member functions—apart one by one, and finish by discussing when you should use friends and when you shouldn't.

## Friend Functions: Authorizing an External Function

The friend function is the most basic form of friendship. We declare one inside the class with the `friend` keyword plus the declaration of an ordinary function:

```cpp
class Vector3D {
private:
    float x, y, z;
public:
    Vector3D(float x, float y, float z) : x(x), y(y), z(z) {}
    // Declare dot_product as a friend function
    friend float dot_product(const Vector3D& a, const Vector3D& b);
};

// Friend function definition—not a member function, no Vector3D:: needed
float dot_product(const Vector3D& a, const Vector3D& b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
```

There are a few points to get straight here. First, the `friend` declaration appears inside the class, but `dot_product` is **not** a member function of `Vector3D`. It is an ordinary global function that has simply been granted the privilege of accessing `Vector3D`'s private members. You call it just like any normal function: `dot_product(v1, v2)`, not `v1.dot_product(v2)`.

Second, the `friend` declaration can sit anywhere in the class—it makes no difference whether it lands in the `public`, `private`, or `protected` section; the effect is exactly the same. Conventionally we gather these declarations at the top or bottom of the class, kept apart from the member function declarations, so that "which external functions hold special privileges" is visible at a glance.

The most classic use case for friend functions is overloading `operator<<`, letting a custom type be written directly to a stream. The reason this scenario needs a friend is that the left operand of `operator<<` is a `std::ostream&`, not our class itself, so it can never be written as a member function of our class:

```cpp
class Point {
private:
    int x, y;

public:
    Point(int x, int y) : x(x), y(y) {}

    // Friend overload of operator<<
    friend std::ostream& operator<<(std::ostream& os, const Point& p);
};

std::ostream& operator<<(std::ostream& os, const Point& p)
{
    os << "(" << p.x << ", " << p.y << ")";
    return os;
}

// Now this works
Point p(3, 4);
std::cout << p << std::endl;  // Output: (3, 4)
```

We'll expand on the details of `operator<<` overloading in the next chapter; for now, you only need to understand why it must be a friend—its first parameter is a `std::ostream&`, not a `Point`, so this function cannot be written as a member function of `Point`.

## Friend Classes: Authorizing an Entire Class

If many member functions of one class need to access the private members of another class, declaring friend functions one by one gets tedious. That's when we can use `friend class` to authorize an entire class in one shot:

```cpp
class Matrix {
private:
    float data[3][3];

public:
    Matrix()  // Initialize as the identity matrix
    {
        for (int i = 0; i < 3; ++i) {
            for (int j = 0; j < 3; ++j) {
                data[i][j] = (i == j) ? 1.0f : 0.0f;
            }
        }
    }

    // Vector is a friend class of Matrix
    friend class Vector;
};

class Vector {
private:
    float x, y, z;

public:
    Vector(float x, float y, float z) : x(x), y(y), z(z) {}

    Vector transform(const Matrix& m)
    {
        // Vector's member functions can access Matrix's private members directly
        float nx = m.data[0][0] * x + m.data[0][1] * y + m.data[0][2] * z;
        float ny = m.data[1][0] * x + m.data[1][1] * y + m.data[1][2] * z;
        float nz = m.data[2][0] * x + m.data[2][1] * y + m.data[2][2] * z;
        return Vector(nx, ny, nz);
    }
};
```

`friend class Vector;` means **all** member functions of `Vector` can access `Matrix`'s private members. This is coarse-grained authorization, and we should use it with care—but there really are scenarios where two classes are related tightly enough to deserve this level of trust. Typical legitimate cases include the "container + iterator" pattern, and the close cooperation between math types like the pair above. The common trait: the two classes are **logically a single whole**, split into two classes only for code-organization reasons.

## Friend Member Functions: Authorizing Just One Member Function

If you feel that friend-class authorization is too broad, C++ offers finer-grained control: authorize only **one specific** member function of another class:

```cpp
class Vector;  // Forward declaration

class Matrix {
private:
    float data[3][3];
public:
    Matrix();
    // Authorize only this one member function: Vector::transform
    friend Vector Vector::transform(const Matrix& m);
};

class Vector {
private:
    float x, y, z;

public:
    Vector(float x, float y, float z) : x(x), y(y), z(z) {}

    Vector transform(const Matrix& m);
};
```

In theory this approach is the safest—the principle of least privilege, after all. In practice, though, friend member functions come with a headache-inducing dependency problem: when you declare `friend Vector Vector::transform(const Matrix&)`, the compiler must already have seen the complete definition of the `Vector` class, otherwise it cannot know that `transform` really is a member function of `Vector`. This forces us to arrange header include order carefully, and one wrong move drops you into circular dependencies. If you have three or four member functions to authorize, it's cleaner to just use a friend class.

## When to Use Friends

Friends are easy to abuse, so we owe ourselves a serious discussion of the boundaries.

**Scenarios where using friends is reasonable.** The most typical one is operator overloading—the `operator<<` from earlier is the best example. Tightly coupled implementation partners are also reasonable, such as a `Container` and its `Iterator`, or `Matrix` and `Vector`. In these cases the two classes already share implementation details anyway; using friends simply makes that fact explicit at the code level.

**Scenarios where friends should not be used.** If we just want to be lazy and skip designing a proper public interface, casually adding a `friend` so an external function can manipulate private data directly—this kind of friendship is harmful. Most scenarios that "need a friend" can actually be replaced by providing an appropriate access interface:

```cpp
// Not recommended: using a friend to bypass interface design
class SensorData {
    friend void serialize(const SensorData& data, uint8_t* buffer);
private:
    float values[100];
    int count;
};

// Recommended: provide a read-only interface, encapsulation stays intact
class SensorData {
private:
    float values[100];
    int count;
public:
    const float* data() const { return values; }
    int size() const { return count; }
};
```

We should remember three key properties of friendship, because they are often misunderstood. **Friendship is not inherited**: if `Base` is a friend of `X`, `Derived` (which inherits from `Base`) does not automatically become a friend of `X`. **Friendship is not transitive**: if `A` is a friend of `B`, and `B` is a friend of `C`, `A` does not automatically become a friend of `C`. **Friendship is one-way**: `A` being a friend of `B` means `A` can access `B`'s private members, but `B` cannot turn around and access `A`'s private members—unless `A` also declares `B` a friend. These three rules ensure that friend privileges never spread endlessly beyond what was declared.

A friend declaration is not a forward declaration of the function. Writing `friend void foo();` inside a class does make `foo` a friend of that class, but when we define the friend function outside the class, we must make sure an ordinary declaration of it (not the `friend` declaration) can be found before the call site. Otherwise, on some compilers you may hit a "function definition not found" linker error, especially when the friend function is defined in another `.cpp` file. The safest practice is to add one plain function declaration outside the class as well.

## Hands-On: friend_demo.cpp

Now let's look at a complete example: `Matrix` and `Vector` cooperating through a friend relationship to perform matrix-vector multiplication.

```cpp
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
    // Grant Vector access to private members
    friend class Vector;
};

class Vector {
private:
    std::array<float, 3> v;
public:
    Vector(float x, float y, float z) : v{x, y, z} {}
    // Friend privileges: direct access to Matrix's internal array
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
```

Compile and run:

```bash
g++ -std=c++17 -Wall -Wextra -o friend_demo friend_demo.cpp
./friend_demo
```

Expected output:

```text
Matrix:
| 2.00 0.00 0.00 |
| 0.00 3.00 0.00 |
| 0.00 0.00 0.50 |
Vector:  (1.00, 2.00, 4.00)
Result:  (2.00, 6.00, 2.00)
```

In this example, `Vector::transform` directly accesses the private array `Matrix::data`. Without friendship, we would have to provide a `float get(int, int) const` access interface—it's not that this is impossible, but in performance-sensitive settings like a math library, one less layer of indirection means tighter loops and friendlier cache behavior.

## Exercises

### Exercise 1: Implementing operator<< as a Friend

Implement a friend function `operator<<` for the `Student` class below, so that `std::cout << student;` can directly output the student's information.

```cpp
class Student {
private:
    int id;
    float score;
    std::string name;

public:
    Student(int id, float score, const std::string& name)
        : id(id), score(score), name(name) {}

    // Add the friend declaration here
};

// Implement operator<< here
```

To verify: create a few `Student` objects, output their information with `std::cout`, and confirm the format is correct.

::: details Reference Answer

```cpp
#include <iostream>
#include <string>

class Student {
 private:
    int id;
    float score;
    std::string name;

 public:
    Student(int id, float score, const std::string& name)
        : id(id), score(score), name(name) {}

    friend std::ostream& operator<<(std::ostream& os, const Student& student);
};

std::ostream& operator<<(std::ostream& os, const Student& student) {
    os << "学生ID:" << student.id << ","
       << "姓名：" << student.name << ","
       << "成绩：" << student.score;
    return os;
}

int main() {
    // Create several Student objects
    Student student1(1, 95.5f, "小明");
    Student student2(2, 88.0f, "小红");
    Student student3(3, 76.5f, "杰");

    // Output student information to verify operator<<
    std::cout << student1 << std::endl;
    std::cout << student2 << std::endl;
    std::cout << student3 << std::endl;

    return 0;
}
```

Compile and run:

```bash
g++ -std=c++17 -Wall -Wextra main.cpp -o main && ./main
```

Result:

```text
学生ID:1,姓名：小明,成绩：95.5
学生ID:2,姓名：小红,成绩：88
学生ID:3,姓名：杰,成绩：76.5
```

:::

### Exercise 2: Designing a Container-Iterator Friend Pair

Implement an `IntBuffer` container and an `IntBufferIterator` iterator. `IntBuffer` stores its data internally in a fixed-size `int` array, and `IntBufferIterator` uses friend privileges to access that array and complete the traversal. Outside code must not be able to access `IntBuffer`'s internal array directly. Hint: `IntBuffer` declares `friend class IntBufferIterator;`, and the iterator holds a pointer to the container.

::: details Reference Answer

```cpp
#include <array>
#include <cstddef>
#include <iostream>

class IntBufferIterator;

class IntBuffer {
 private:
  std::array<int, 4> data{};

 public:
  IntBuffer() {
    for (std::size_t i = 0; i < data.size(); ++i) {
      data[i] = static_cast<int>(i * 2);
    }
  }

  friend class IntBufferIterator;
};

class IntBufferIterator {
 private:
  const IntBuffer* buffer = nullptr;
  std::size_t index = 0;

 public:
  explicit IntBufferIterator(const IntBuffer& buffer) : buffer(&buffer) {}

  bool hasNext() const { return index < buffer->data.size(); }

  int next() {
    int value = buffer->data[index];
    ++index;
    return value;
  }
};

int main() {
  IntBuffer buffer;
  IntBufferIterator iterator{buffer};

  while (iterator.hasNext()) {
    std::cout << iterator.next() << std::endl;
  }
}
```

Compile and run:

```bash
g++ -std=c++17 -Wall -Wextra main.cpp -o main && ./main
```

Result:

```text
0
2
4
6
```

:::
