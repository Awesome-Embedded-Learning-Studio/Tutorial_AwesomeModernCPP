#include <iostream>
#include <string>

struct Session {
    explicit Session(std::string tag) : tag_(std::move(tag)) {
        std::cout << "Session(" << tag_ << ") 构造\n";
    }
    ~Session() { std::cout << "~Session(" << tag_ << ") 析构\n"; }
    const std::string& tag() const { return tag_; }

  private:
    std::string tag_;
};

// 甲方：造出对象，裸指针交出去，觉得自己只是「转交」
Session* build_session() {
    return new Session("无主");
}

// 乙方：拿到指针用一下，觉得「不是我 new 的，不归我删」
void use_session(Session* s) {
    std::cout << "用了一下 Session " << s->tag() << "\n";
}

int main() {
    Session* s = build_session(); // 交接发生，但没有任何一方声明负责
    use_session(s);
    std::cout << "main 结束\n";
    return 0; // 到这里也没有 delete
}
