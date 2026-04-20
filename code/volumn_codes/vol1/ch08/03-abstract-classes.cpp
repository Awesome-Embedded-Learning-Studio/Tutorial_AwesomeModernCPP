// 03-abstract-classes.cpp
// 演示：抽象类、接口、序列化器框架
// 编译: g++ -Wall -Wextra -std=c++17 03-abstract-classes.cpp -o 03-abstract-classes

#include <iostream>
#include <string>

/// @brief 序列化器接口——支持不同格式的数据输出
class ISerializer {
public:
    virtual ~ISerializer() = default;
    virtual void begin_object(const std::string& name) = 0;
    virtual void end_object() = 0;
    virtual void write_field(const std::string& key,
                             const std::string& value) = 0;
    virtual std::string result() const = 0;
};

class JSONSerializer : public ISerializer {
    std::string output_;
    int depth_ = 0;
public:
    void begin_object(const std::string& name) override {
        if (depth_ > 0) output_ += "\"" + name + "\": ";
        output_ += "{\n";
        ++depth_;
    }
    void end_object() override { --depth_; output_ += "}\n"; }
    void write_field(const std::string& key,
                     const std::string& value) override {
        output_ += "  \"" + key + "\": \"" + value + "\",\n";
    }
    std::string result() const override { return output_; }
};

class XMLSerializer : public ISerializer {
    std::string output_;
    std::string current_tag_;
public:
    void begin_object(const std::string& name) override {
        current_tag_ = name;
        output_ += "<" + name + ">\n";
    }
    void end_object() override { output_ += "</" + current_tag_ + ">\n"; }
    void write_field(const std::string& key,
                     const std::string& value) override {
        output_ += "  <" + key + ">" + value + "</" + key + ">\n";
    }
    std::string result() const override { return output_; }
};

void serialize_sensor_data(ISerializer& serializer) {
    serializer.begin_object("sensor");
    serializer.write_field("id", "TEMP-001");
    serializer.write_field("value", "23.5");
    serializer.write_field("unit", "celsius");
    serializer.end_object();
}

int main() {
    JSONSerializer json;
    XMLSerializer xml;
    serialize_sensor_data(json);
    serialize_sensor_data(xml);
    std::cout << "=== JSON ===\n" << json.result() << "\n";
    std::cout << "=== XML ===\n" << xml.result() << std::endl;
    return 0;
}
