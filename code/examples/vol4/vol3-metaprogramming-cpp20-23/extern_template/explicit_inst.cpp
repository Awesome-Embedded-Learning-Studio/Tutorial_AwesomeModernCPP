#include "heavy_template.h"

// 显式实例化定义:在这里实例化 Heavy<int> 的全部成员,
// 供 use_b 等用了 extern template 声明的翻译单元链接
template struct Heavy<int>;
