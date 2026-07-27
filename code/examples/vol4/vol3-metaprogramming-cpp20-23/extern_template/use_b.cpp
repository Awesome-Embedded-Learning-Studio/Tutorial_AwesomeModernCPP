#include "heavy_template.h"
#include <iostream>

// extern template 声明:Heavy<int> 别处已经显式实例化,这里别再生成代码
extern template struct Heavy<int>;

void use_b() {
    Heavy<int> h{99};
    std::cout << "use_b: " << h.compute(3) << "\n";
}
