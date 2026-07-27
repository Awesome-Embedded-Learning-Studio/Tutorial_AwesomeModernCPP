// 配套 07-template-instantiation-control.md「实测:机制怎么跑起来」
// extern template + 显式实例化的多文件演示。本目录 5 个文件协作:
//   heavy_template.h  模板定义(本文件)
//   use_a.cpp         走老办法,隐式实例化 Heavy<int>
//   use_b.cpp         用 extern template 抑制实例化
//   explicit_inst.cpp 集中显式实例化 Heavy<int>
//   main.cpp          串起来
//
// 编译运行:
//   g++ -std=c++20 -Wall -Wextra -c use_a.cpp use_b.cpp explicit_inst.cpp main.cpp
//   g++ use_a.o use_b.o explicit_inst.o main.o -o demo && ./demo
//
// 对照(去掉 explicit_inst.cpp,链接应失败,见正文「实测」节):
//   g++ -std=c++20 -Wall -Wextra -c use_b.cpp main.cpp
//   g++ use_b.o main.o -o demo_fail
#pragma once

// 一个适度「重」的模板:实例化会生成若干成员代码
template <typename T> struct Heavy {
    T value;

    explicit Heavy(T v) : value(v) {}

    T compute(T x) const {
        T acc = value;
        for (int i = 0; i < 10; ++i) {
            acc = acc * x + value;
        }
        return acc;
    }

    T scale(T factor) const { return value * factor; }

    T reset(T newv) {
        T old = value;
        value = newv;
        return old;
    }
};
