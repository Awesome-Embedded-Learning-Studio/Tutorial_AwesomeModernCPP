// cascading_demo.cpp
// this 指针与链式调用：StringBuilder + Config Builder

#include <cstdio>
#include <cstring>

class StringBuilder {
    char buffer_[256];
    std::size_t length_;

public:
    StringBuilder() : length_(0) { buffer_[0] = '\0'; }

    StringBuilder& append(const char* str)
    {
        while (*str && length_ < 255) {
            buffer_[length_++] = *str++;
        }
        buffer_[length_] = '\0';
        return *this;
    }

    StringBuilder& append_char(char c)
    {
        if (length_ < 255) {
            buffer_[length_++] = c;
            buffer_[length_] = '\0';
        }
        return *this;
    }

    // const 成员函数：只读取，不修改
    const char* c_str() const { return buffer_; }
    std::size_t length() const { return length_; }
};

class Config {
    char name_[64];
    int baudrate_;
    bool use_parity_;
    int timeout_ms_;

    // 私有构造，强制通过 Builder 创建
    Config(const char* name, int baud, bool parity, int timeout)
        : baudrate_(baud), use_parity_(parity), timeout_ms_(timeout)
    {
        std::strncpy(name_, name, 63);
        name_[63] = '\0';
    }

public:
    class Builder {
        char name_[64];
        int baudrate_;
        bool use_parity_;
        int timeout_ms_;

    public:
        Builder() : baudrate_(9600), use_parity_(false), timeout_ms_(1000)
        {
            name_[0] = '\0';
        }

        Builder& set_name(const char* name)
        {
            std::strncpy(name_, name, 63);
            name_[63] = '\0';
            return *this;
        }

        Builder& set_baudrate(int baud)
        {
            baudrate_ = baud;
            return *this;
        }

        Builder& set_parity(bool parity)
        {
            use_parity_ = parity;
            return *this;
        }

        Builder& set_timeout(int ms)
        {
            timeout_ms_ = ms;
            return *this;
        }

        Config build() const
        {
            return Config(name_, baudrate_, use_parity_, timeout_ms_);
        }
    };

    void print() const
    {
        std::printf("Config: name=%s, baud=%d, parity=%s, timeout=%dms\n",
                    name_, baudrate_,
                    use_parity_ ? "yes" : "no",
                    timeout_ms_);
    }
};

int main()
{
    // StringBuilder 链式调用
    StringBuilder sb;
    sb.append("Hello")
          .append(", ")
          .append("this ")
          .append("is ")
          .append("a ")
          .append("chain!")
          .append_char('\n');

    std::printf("--- StringBuilder ---\n");
    std::printf("%s", sb.c_str());
    std::printf("Total length: %zu\n\n", sb.length());

    // Config Builder 链式调用
    Config cfg = Config::Builder()
                     .set_name("UART1")
                     .set_baudrate(115200)
                     .set_parity(false)
                     .set_timeout(500)
                     .build();

    std::printf("--- Config Builder ---\n");
    cfg.print();

    return 0;
}
