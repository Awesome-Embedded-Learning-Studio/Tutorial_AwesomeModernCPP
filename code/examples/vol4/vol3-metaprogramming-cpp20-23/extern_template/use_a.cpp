#include "heavy_template.h"
#include <iostream>

// 这个翻译单元正常隐式实例化 Heavy<int>(用到时编译器自动生成)
void use_a() {
    Heavy<int> h{42};
    std::cout << "use_a: " << h.compute(2) << "\n";
}
