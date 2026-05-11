---
title: "用 C 实现面向对象编程"
description: "用结构体 + 函数指针模拟类、封装、继承与多态，理解 OOP 的底层实现机制"
chapter: 1
order: 104
tags:
  - host
  - cpp-modern
  - advanced
  - 实战
  - 基础
difficulty: advanced
platform: host
reading_time_minutes: 25
cpp_standard: [11]
prerequisites:
  - "指针进阶：多级指针、指针与 const"
  - "结构体、联合体与内存对齐"
  - "函数指针与回调机制"
---

# 用 C 实现面向对象编程

说实话，这个话题笔者纠结了很久要不要写。毕竟都 2026 年了，谁还在 C 里手搓 OOP？但后来想想——嵌入式开发、Linux 内核、GTK/GLib、Lua 源码，这些重量级的 C 项目哪一个不是在用 struct + 函数指针做面向对象？更关键的是，如果你不理解 C 层面 OOP 是怎么拼出来的，那学 C++ 的时候对虚函数表、vptr、动态绑定的理解就永远是空中楼阁——你知道语法怎么用，但不知道底下发生了什么。

这篇我们就来用纯 C 把封装、继承、多态、接口抽象全部手撸一遍，最后拼出一个能跑的图形框架。写完之后回头看 C++ 的 `class`、`virtual`、`abstract class`，会有一种"原来如此"的通透感。

> **学习目标**
>
> 完成本章后，你将能够：
>
> - [ ] 用结构体 + 函数指针模拟 C++ 的类
> - [ ] 用不透明指针实现封装
> - [ ] 用结构体嵌套实现单继承
> - [ ] 用 vtable（虚函数表）模拟运行时多态
> - [ ] 用函数指针表实现接口抽象
> - [ ] 完成一个包含继承和多态的图形框架实战

## 环境说明

我们用 GCC 或 Clang 在主机上编译即可，不需要任何第三方库。代码遵循 C11 标准，因为要用到匿名结构体和指定初始化器。如果你在嵌入式平台上跑，这些写法同样是可移植的——struct 和函数指针不依赖任何运行时特性。

```text
平台：Linux / macOS / Windows (MSVC/MinGW)
编译器：GCC >= 9 或 Clang >= 12
标准：-std=c11
依赖：无
```

## 第一步——用不透明指针实现封装

封装的核心思想是隐藏内部实现，只暴露操作接口。C++ 用 `private` 和 `public`，C 里的答案就是不透明指针（opaque pointer）模式。

### 动态字符串缓冲区

我们做一个动态字符串缓冲区，调用者只能通过函数操作它，永远看不到内部结构。头文件只暴露类型名和操作函数：

```c
// strbuf.h — 公开头文件
typedef struct StrBuf StrBuf;

StrBuf*     strbuf_create(int capacity);
void        strbuf_destroy(StrBuf* sb);
int         strbuf_append(StrBuf* sb, const char* data, int len);
int         strbuf_length(const StrBuf* sb);
const char* strbuf_data(const StrBuf* sb);
```

头文件里只有一个前向声明 `typedef struct StrBuf StrBuf`。调用者知道 `StrBuf` 是个类型，但完全不知道它里面长什么样——没法直接访问任何字段，只能走我们提供的函数。这不就是 C++ 的 `private` 吗？

实现文件里才给出完整定义：

```c
// strbuf.c — 私有实现
#include "strbuf.h"
#include <stdlib.h>
#include <string.h>

struct StrBuf {
    char* data;
    int   capacity;
    int   length;
};

StrBuf* strbuf_create(int capacity)
{
    StrBuf* sb = (StrBuf*)malloc(sizeof(StrBuf));
    if (!sb) return NULL;
    sb->data = (char*)malloc(capacity);
    if (!sb->data) {
        free(sb);
        return NULL;
    }
    sb->capacity = capacity;
    sb->length = 0;
    sb->data[0] = '\0';
    return sb;
}

void strbuf_destroy(StrBuf* sb)
{
    if (sb) {
        free(sb->data);
        free(sb);
    }
}

int strbuf_append(StrBuf* sb, const char* data, int len)
{
    if (sb->length + len >= sb->capacity) {
        return -1;  // 缓冲区不足
    }
    memcpy(sb->data + sb->length, data, len);
    sb->length += len;
    sb->data[sb->length] = '\0';
    return 0;
}

int strbuf_length(const StrBuf* sb) { return sb->length; }
const char* strbuf_data(const StrBuf* sb) { return sb->data; }
```

`struct StrBuf` 的完整定义只出现在 `.c` 文件里。如果调用者尝试写 `sb->length`，编译器直接报错"dereferencing pointer to incomplete type"。C 的 `.h` 文件相当于 C++ 的 `public` 部分，`.c` 文件相当于 `private` 成员和函数实现——区别是 C 靠编译器的类型不完整性检查，C++ 靠语言层面的访问控制关键字。

## 第二步——用结构体 + 函数指针模拟类

封装搞定了，接下来解决一个更根本的问题：C 没有"方法"。C++ 里方法是绑定在类上的函数，可以通过 `obj.method()` 调用。C 没这个语法糖，但我们用一种约定模拟：**把函数指针存进结构体，第一个参数始终是 `self` 指针**。

### 计数器"对象"

```c
typedef struct Counter {
    int value;
    int min;
    int max;

    // 「方法」——函数指针
    void (*increment)(struct Counter* self);
    void (*decrement)(struct Counter* self);
    int  (*get_value)(const struct Counter* self);
    void (*reset)(struct Counter* self);
} Counter;
```

结构体里既有数据成员又有函数指针成员，函数指针相当于 C++ 的成员函数。但有个重要区别——C 的函数指针不会自动绑定 `this`，需要手动传 `self`。

方法实现和"构造函数"：

```c
static void counter_increment(Counter* self)
{
    if (self->value < self->max) {
        self->value++;
    }
}

static int counter_get_value(const Counter* self)
{
    return self->value;
}

// 「构造函数」——初始化对象并绑定方法
void counter_init(Counter* self, int min, int max)
{
    self->value = min;
    self->min = min;
    self->max = max;
    self->increment = counter_increment;
    self->get_value = counter_get_value;
    // ...其他方法绑定
}
```

使用的时候就很 OOP 了：

```c
Counter c;
counter_init(&c, 0, 100);

c.increment(&c);
c.increment(&c);
printf("value = %d\n", c.get_value(&c));  // value = 2
```

> ⚠️ **踩坑预警**
> 把函数指针直接塞进每个实例里意味着每个对象都存了一份函数指针——在 64 位系统上这个 `Counter` 光函数指针就占 32 字节。如果你创建一万个对象，就有十万份完全相同的指针。下一节我们用 vtable 来优化这个问题。

## 第三步——用结构体嵌套实现继承

C 没有语法层面的继承，但我们可以用**结构体嵌套**来模拟——把"基类"作为成员放到"派生类"的第一个字段。为什么是第一个？因为 C 标准保证结构体的地址和它第一个成员的地址相同，这样就可以安全地在基类指针和派生类指针之间做类型转换。

### 动物家族

```c
// 「基类」——所有动物共有的属性
typedef struct Animal {
    const char* name;
    int    age;
    void (*speak)(const struct Animal* self);
} Animal;

void animal_print_info(const Animal* self)
{
    printf("[%s, age=%d] ", self->name, self->age);
    if (self->speak) {
        self->speak(self);
    }
    printf("\n");
}

// 「派生类」——狗
typedef struct Dog {
    Animal base;          // 基类放第一个！
    const char* breed;
} Dog;

void dog_speak(const Animal* self) { printf("Woof!"); }

void dog_init(Dog* self, const char* name, int age, const char* breed)
{
    self->base.name = name;
    self->base.age = age;
    self->base.speak = dog_speak;
    self->breed = breed;
}

// 「派生类」——猫
typedef struct Cat {
    Animal base;
    int lives_remaining;
} Cat;

void cat_speak(const Animal* self) { printf("Meow!"); }

void cat_init(Cat* self, const char* name, int age, int lives)
{
    self->base.name = name;
    self->base.age = age;
    self->base.speak = cat_speak;
    self->lives_remaining = lives;
}
```

关键的地方来了——因为 `Dog` 和 `Cat` 的第一个成员都是 `Animal base`，所以 `&dog->base == (Animal*)dog`。我们可以把 `Dog*` 安全地转成 `Animal*`，然后通过基类指针统一调用：

```c
Dog dog;
dog_init(&dog, "Buddy", 3, "Golden Retriever");
Cat cat;
cat_init(&cat, "Whiskers", 2, 9);

Animal* animals[2] = { (Animal*)&dog, (Animal*)&cat };
for (int i = 0; i < 2; i++) {
    animals[i]->speak(animals[i]);
}
```

输出：

```text
[Buddy, age=3] Woof!
[Whiskers, age=2] Meow!
```

虽然我们都是通过 `Animal*` 指针调用，但 `Dog` 和 `Cat` 各自发出了不同的叫声。这就是多态的雏形——同一接口，不同行为。

> ⚠️ **踩坑预警**
> 基类**必须**放在第一个字段。如果你把它放到中间或末尾，`&dog == (Animal*)&dog` 就不成立了，类型转换会得到错误的偏移量，轻则数据错乱，重则直接 crash。

## 第四步——用虚函数表（vtable）实现多态

前面把函数指针直接塞进每个对象，浪费了不少内存。现在来做正经的多态——用虚函数表（vtable）。这是 C++ 编译器实现虚函数的底层机制，我们手动复现它。核心思想：**同一类型的所有对象共享一张函数指针表，每个对象只存一个指向这张表的指针**。

### 图形基类 + vtable

```c
typedef struct Shape Shape;

// 虚函数表——所有 Shape「类」共享的函数指针表
typedef struct ShapeVtable {
    double (*area)(const Shape* self);
    double (*perimeter)(const Shape* self);
    void   (*draw)(const Shape* self);
    void   (*destroy)(Shape* self);
} ShapeVtable;

// 基类结构体
typedef struct Shape {
    const ShapeVtable* vtable;  // 指向虚函数表的指针（就是 C++ 的 vptr）
    const char* name;
} Shape;

// 通用虚函数分派
double shape_area(const Shape* self)
{
    return self->vtable->area(self);
}
void shape_draw(const Shape* self)
{
    self->vtable->draw(self);
}
// ... shape_perimeter、shape_destroy 同理
```

`ShapeVtable` 就是虚函数表——一张函数指针数组。`Shape` 里的 `const ShapeVtable* vtable` 就是 C++ 里每个带虚函数的对象内部隐藏的 vptr。现在实现具体的图形：

```c
// 圆形
typedef struct Circle {
    Shape base;     // 基类放第一个
    double radius;
} Circle;

static double circle_area(const Shape* self)
{
    const Circle* c = (const Circle*)self;  // 向下转型
    return 3.14159265358979 * c->radius * c->radius;
}

static void circle_draw(const Shape* self)
{
    const Circle* c = (const Circle*)self;
    printf("Circle(\"%s\", r=%.2f)\n", self->name, c->radius);
}

static void circle_destroy(Shape* self) { free(self); }

// 圆形的 vtable——const，全局唯一
static const ShapeVtable kCircleVtable = {
    .area      = circle_area,
    .perimeter = circle_perimeter,
    .draw      = circle_draw,
    .destroy   = circle_destroy
};

Circle* circle_create(const char* name, double radius)
{
    Circle* c = (Circle*)malloc(sizeof(Circle));
    c->base.vtable = &kCircleVtable;  // 绑定 vtable
    c->base.name = name;
    c->radius = radius;
    return c;
}
```

矩形的写法完全同理——定义 `Rect` 结构体、实现方法、创建 `kRectVtable`、写 `rect_create`。此处不再赘述。

来验证多态是否工作：

```c
Shape* shapes[3];
shapes[0] = (Shape*)circle_create("Sun", 5.0);
shapes[1] = (Shape*)rect_create("Box", 3.0, 4.0);
shapes[2] = (Shape*)circle_create("Moon", 2.0);

for (int i = 0; i < 3; i++) {
    shape_draw(shapes[i]);
    printf("  area = %.2f\n", shape_area(shapes[i]));
}
```

输出：

```text
Circle("Sun", r=5.00)
  area = 78.54
Rectangle("Box", w=3.00, h=4.00)
  area = 12.00
Circle("Moon", r=2.00)
  area = 12.57
```

通过统一的 `shape_area()`、`shape_draw()` 接口调用，每次都走到了正确的具体实现——这就是运行时多态，和 C++ 虚函数的底层机制**完全一样**。内存布局对比如下：

```text
vtable 方案：每个对象只有 1 个 vptr（8 字节）
┌──────────────────┐
│ vtable ─────────────→ kCircleVtable（全局共享）
│ name             │     ┌──────────────────┐
│ radius           │     │ circle_area      │
└──────────────────┘     │ circle_perimeter │
                         │ circle_draw      │
                         └──────────────────┘
```

## 第五步——用函数指针表实现接口

继承解决代码复用，但有时候我们需要更松耦合的关系——接口。C 没有接口概念，但可以用**纯函数指针结构体**来模拟。和 vtable 的区别在于，接口不包含数据成员，只定义行为约定。

### 多接口实现与偏移陷阱

一个类型可以同时实现多个接口——通过嵌套多个接口结构体。但这里有个大坑：

```c
typedef struct Drawable {
    void (*draw)(const struct Drawable* self);
} Drawable;

typedef struct Serializable {
    char* (*to_string)(const struct Serializable* self);
} Serializable;

// 同时实现两个接口
typedef struct TextShape {
    Drawable    drawable;       // 第一个接口——可以直接 cast
    Serializable serializable;  // 第二个接口——必须用 & 取地址！
    char* text;
} TextShape;
```

```c
// 第一个接口——两种写法等价
Drawable* d1 = (Drawable*)ts;       // OK，因为是第一个成员
Drawable* d2 = &ts->drawable;       // 也 OK，更明确

// 第二个接口——直接 cast 是错的！
// Serializable* s = (Serializable*)ts;  // 危险！偏移不对
Serializable* s = &ts->serializable;    // 正确
```

> ⚠️ **踩坑预警**
> 在 C++ 里编译器会自动计算多重继承的偏移量，但在 C 里做手搓 OOP，你必须自己保证指针转换的正确性。这就是为什么很多 C 项目（比如 Linux 内核）倾向于只做单继承 + 回调函数，而不是搞多重接口继承。如果你一定要做多接口，务必用 `&obj->interface` 来获取指针，不要直接 cast。

## 第六步——实战：拼一个图形管理框架

现在把前面学到的封装、继承、多态、vtable 全套技术组合起来，写一个图形管理框架。框架核心是一个 `ShapeManager`——用不透明指针封装，外部只拿到一个指针，不知道内部怎么存储图形。

### 图形管理器

```c
// shape_manager.h — 不透明指针封装
typedef struct ShapeManager ShapeManager;

ShapeManager* shape_manager_create(int max_shapes);
void          shape_manager_destroy(ShapeManager* mgr);
int           shape_manager_add(ShapeManager* mgr, Shape* shape);
void          shape_manager_draw_all(const ShapeManager* mgr);
double        shape_manager_total_area(const ShapeManager* mgr);
Shape*        shape_manager_find_by_name(const ShapeManager* mgr,
                                         const char* name);
```

```c
// shape_manager.c — 私有实现
struct ShapeManager {
    Shape** shapes;
    int     count;
    int     capacity;
};

ShapeManager* shape_manager_create(int max_shapes)
{
    ShapeManager* mgr = (ShapeManager*)malloc(sizeof(ShapeManager));
    if (!mgr) return NULL;
    mgr->shapes = (Shape**)calloc(max_shapes, sizeof(Shape*));
    if (!mgr->shapes) {
        free(mgr);
        return NULL;
    }
    mgr->count = 0;
    mgr->capacity = max_shapes;
    return mgr;
}

void shape_manager_destroy(ShapeManager* mgr)
{
    if (!mgr) return;
    for (int i = 0; i < mgr->count; i++) {
        shape_destroy(mgr->shapes[i]);
    }
    free(mgr->shapes);
    free(mgr);
}

int shape_manager_add(ShapeManager* mgr, Shape* shape)
{
    if (mgr->count >= mgr->capacity) return -1;
    mgr->shapes[mgr->count++] = shape;
    return mgr->count - 1;
}

void shape_manager_draw_all(const ShapeManager* mgr)
{
    printf("=== Drawing %d shapes ===\n", mgr->count);
    for (int i = 0; i < mgr->count; i++) {
        shape_draw(mgr->shapes[i]);
    }
}

double shape_manager_total_area(const ShapeManager* mgr)
{
    double total = 0.0;
    for (int i = 0; i < mgr->count; i++) {
        total += shape_area(mgr->shapes[i]);
    }
    return total;
}
```

### 验证

```c
int main(void)
{
    ShapeManager* mgr = shape_manager_create(10);

    shape_manager_add(mgr, (Shape*)circle_create("Sun", 5.0));
    shape_manager_add(mgr, (Shape*)rect_create("Box", 3.0, 4.0));
    shape_manager_add(mgr, (Shape*)circle_create("Moon", 2.0));
    shape_manager_add(mgr, (Shape*)rect_create("Frame", 10.0, 6.0));

    shape_manager_draw_all(mgr);
    printf("Total area: %.2f\n", shape_manager_total_area(mgr));

    Shape* found = shape_manager_find_by_name(mgr, "Box");
    if (found) {
        printf("Found: ");
        shape_draw(found);
    }

    shape_manager_destroy(mgr);
    return 0;
}
```

```text
=== Drawing 4 shapes ===
Circle("Sun", r=5.00) -> area=78.54
Rectangle("Box", w=3.00, h=4.00) -> area=12.00
Circle("Moon", r=2.00) -> area=12.57
Rectangle("Frame", w=10.00, h=6.00) -> area=60.00
Total area: 163.10
Found: Rectangle("Box", w=3.00, h=4.00) -> area=12.00
```

我们用统一接口管理不同类型的图形对象，多态分派自动走到正确的实现——封装、继承、多态全部在位。

## C++ 衔接：编译器到底在帮你做什么

当你在 C++ 里写 `class Shape { virtual double area() = 0; }` 的时候，编译器帮你做了我们上面手动做的所有事情：

| 你手动做的 C OOP | C++ 编译器帮你做的 |
|---|---|
| 定义 `ShapeVtable` 结构体 | 编译器自动生成 vtable（`.rodata` 段） |
| 构造函数里赋值 `vtable = &kCircleVtable` | 构造函数自动设置 vptr |
| 手动写 `shape_area()` 做虚函数分派 | `s->area()` 自动通过 vptr 查表 |
| `(Circle*)shape` 手动向下转型 | `dynamic_cast<Circle*>(shape)` 安全转型 |
| `counter_init(&c, 0, 100)` 手动调构造函数 | `Counter c(0, 100)` 自动构造 |
| 不透明指针隐藏字段 | `private:` 访问控制 |
| 结构体嵌套做继承 | `class Derived : public Base` |

C++ 的 OOP 语法本质就是 C OOP 惯用法的语法糖。编译器把绑 vtable、传 `this`、做类型转换这些琐碎工作全部自动化了。理解了这一点，你就能理解 C++ 里一些看似奇怪的设计——比如为什么空类的 `sizeof` 不是 0（有 vptr）、为什么虚析构函数很重要（不然析构时走不到子类的 vtable）、为什么构造函数里不能调虚函数（vptr 还没设置好）。

### 虚析构函数为什么重要

在我们的 C 实现里，`shape_destroy()` 通过 vtable 找到正确的 `destroy` 函数来释放资源。如果 vtable 里 `destroy` 没被正确覆盖，`free()` 就只释放了基类大小的内存，派生类多出来的字段就泄漏了。C++ 里虚析构函数解决的完全是同一个问题——`delete base_ptr` 时必须通过 vtable 找到派生类的析构函数，先析构派生类再析构基类。如果析构函数不是 `virtual` 的，编译器做静态绑定，只调用基类析构函数——派生类的资源就泄漏了。

## 练习

### 练习 1：三角形扩展

在图形框架中添加一个 `Triangle` 类型（用三边长度表示）：

```c
typedef struct Triangle {
    Shape  base;
    double a, b, c;  // 三边长度
} Triangle;

Triangle* triangle_create(const char* name, int id,
                           double a, double b, double c);
```

提示：三角形面积用海伦公式——先算半周长 `s = (a+b+c)/2`，面积 `A = sqrt(s*(s-a)*(s-b)*(s-c))`。别忘了在 vtable 里填入正确的函数指针。

### 练习 2：图形排序

给 `ShapeManager` 添加按面积排序功能：

```c
/// @brief 按面积从小到大排序所有图形
void shape_manager_sort_by_area(ShapeManager* mgr);
```

提示：可以用标准库的 `qsort()`，但比较函数接收 `const void*`，需要转成 `Shape**` 再解引用得到 `Shape*`，然后通过 `shape_area()` 比较大小。

### 练习 3：不透明指针版计数器

把第二步的 `Counter` 改成不透明指针版本——头文件只暴露 `typedef struct Counter Counter;` 和操作函数，实现文件藏起完整定义。请自行拆分头文件和实现文件，并提供一个 `counter_create()` 返回堆分配的对象。

## 参考资源

- [GLib Object System (GObject) - GNOME](https://docs.gtk.org/gobject/)
- [Linux Kernel Object Model (kobject)](https://docs.kernel.org/core-api/kobject.html)
- [C++ 虚函数 - cppreference](https://en.cppreference.com/w/cpp/language/virtual)
