#include "TextPrinter.h"
#include <iostream>
int main() {
    std::string text = "Hello, World!";

    std::cout << "原始输出：" << std::endl;
    auto plain = std::make_shared<PlainTextPrinter>();
    plain->simple_print(text);

    std::cout << "\n加上引号：" << std::endl;
    auto quoted = std::make_shared<QuoteDecorator>(plain);
    quoted->simple_print(text);

    std::cout << "\n加上星号 + 引号：" << std::endl;
    auto starredQuoted = std::make_shared<StarDecorator>(quoted);
    starredQuoted->simple_print(text);

    std::cout << "\n加上大写 + 星号 + 引号：" << std::endl;
    auto fullDecorated = std::make_shared<UpperCaseDecorator>(starredQuoted);
    fullDecorated->simple_print(text);

    return 0;
}
