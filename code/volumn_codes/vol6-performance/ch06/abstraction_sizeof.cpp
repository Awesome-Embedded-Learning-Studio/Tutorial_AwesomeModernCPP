#include <cstdio>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>
int main() {
    printf("sizeof:\n");
    printf("  int                              = %zu\n", sizeof(int));
    printf("  std::optional<int>               = %zu  (+1 bool+padding,有无值标记)\n",
           sizeof(std::optional<int>));
    printf("  std::variant<int,double>         = %zu\n", sizeof(std::variant<int, double>));
    printf("  std::variant<int,char,double,str>= %zu  (index + 最大备选 + padding)\n",
           sizeof(std::variant<int, char, double, std::string>));
    printf("  std::span<int>                   = %zu  (指针+长度,零所有权)\n",
           sizeof(std::span<int>));
    printf("  std::string_view                 = %zu  (指针+长度,不保证 \\0)\n",
           sizeof(std::string_view));
    printf("  std::shared_ptr<int>             = %zu  (2 指针:对象+控制块)\n",
           sizeof(std::shared_ptr<int>));
    printf("  std::unique_ptr<int>             = %zu  (1 指针)\n", sizeof(std::unique_ptr<int>));
    printf("  std::string                      = %zu  (含 SSO 缓冲)\n", sizeof(std::string));
    printf("  std::vector<int>                 = %zu  (3 指针)\n", sizeof(std::vector<int>));
    return 0;
}
