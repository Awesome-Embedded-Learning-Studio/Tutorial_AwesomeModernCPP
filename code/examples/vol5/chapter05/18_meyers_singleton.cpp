// meyers_singleton.cpp —— 块级 static 的并发初始化由标准担保
// （正文片段的补全版：Config 沿用 dclp.cpp 的定义，main 演示取到的是同一个对象）
#include <cstdio>

struct Config { int id; int value; };

Config& config() {
  static Config inst{1, 42};   // 并发初始化由标准担保
  return inst;
}

int main() {
  Config& a = config();
  Config& b = config();
  std::printf("两次取到同一个对象=%d，id=%d value=%d\n",
              &a == &b ? 1 : 0, a.id, a.value);
  return 0;
}
