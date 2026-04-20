/**
 * @file ex09_chained_config.cpp
 * @brief 练习：实现链式配置器
 *
 * Config 类通过 set_width / set_height 等方法返回 *this 的引用，
 * 支持链式调用：config.set_width(800).set_height(600).print()。
 */

#include <iostream>
#include <string>

class Config {
private:
    int width_;
    int height_;
    std::string title_;
    bool fullscreen_;

public:
    // 默认构造函数
    Config()
        : width_(800)
        , height_(600)
        , title_("Untitled")
        , fullscreen_(false) {}

    // 链式 setter：返回 Config& 以支持连续调用
    Config& set_width(int w) {
        width_ = w;
        return *this;
    }

    Config& set_height(int h) {
        height_ = h;
        return *this;
    }

    Config& set_title(const std::string& t) {
        title_ = t;
        return *this;
    }

    Config& set_fullscreen(bool f) {
        fullscreen_ = f;
        return *this;
    }

    // 打印当前配置
    void print() const {
        std::cout << "Config {\n"
                  << "  width  = " << width_ << "\n"
                  << "  height = " << height_ << "\n"
                  << "  title  = \"" << title_ << "\"\n"
                  << "  fullscreen = " << (fullscreen_ ? "true" : "false")
                  << "\n}\n";
    }

    // Getter
    int width() const { return width_; }
    int height() const { return height_; }
};

int main() {
    std::cout << "===== 链式配置器 =====\n\n";

    // 默认配置
    Config default_config;
    std::cout << "默认配置:\n";
    default_config.print();
    std::cout << "\n";

    // 链式调用
    Config game_config;
    game_config.set_width(1920)
               .set_height(1080)
               .set_title("My Game")
               .set_fullscreen(true);

    std::cout << "游戏配置 (链式调用):\n";
    game_config.print();
    std::cout << "\n";

    // 部分修改也可以链式
    Config window_config;
    window_config.set_title("Hello Window").set_width(640);
    std::cout << "窗口配置 (部分设置):\n";
    window_config.print();

    return 0;
}
