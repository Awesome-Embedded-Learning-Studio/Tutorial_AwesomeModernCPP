// custom_struct_binding.cpp - 自定义结构体的绑定
#include <iostream>
#include <array>
#include <type_traits>
#include <cstdint>
#include <string>

void basic_struct_binding() {
    std::cout << "=== 基本结构体绑定 ===\n\n";

    // 所有成员都是公有的结构体
    struct Point3D {
        double x, y, z;
    };

    Point3D p{1.1, 2.2, 3.3};

    // 结构化绑定
    auto [x, y, z] = p;

    std::cout << "Point3D解包:\n";
    std::cout << "  x = " << x << '\n';
    std::cout << "  y = " << y << '\n';
    std::cout << "  z = " << z << '\n';

    // 引用绑定
    auto& [rx, ry, rz] = p;
    rx = 10.0;
    std::cout << "\n修改rx后:\n";
    std::cout << "  p.x = " << p.x << '\n';
}

void complex_struct_binding() {
    std::cout << "\n=== 复杂结构体绑定 ===\n\n";

    struct SensorConfig {
        uint8_t sensor_id;
        uint16_t sample_rate;
        float threshold;
        bool enabled;
        char name[16];
    };

    SensorConfig config{
        5,           // sensor_id
        1000,        // sample_rate
        23.5f,       // threshold
        true,        // enabled
        "Temperature" // name
    };

    auto [id, rate, threshold, enabled, name] = config;

    std::cout << "SensorConfig解包:\n";
    std::cout << "  ID: " << +id << '\n';
    std::cout << "  Sample Rate: " << rate << " Hz\n";
    std::cout << "  Threshold: " << threshold << '\n';
    std::cout << "  Enabled: " << std::boolalpha << enabled << '\n';
    std::cout << "  Name: " << name << '\n';
}

void array_member_binding() {
    std::cout << "\n=== 数组成员绑定 ===\n\n";

    struct DataWithArray {
        int id;
        float values[3];
        char name[10];
    };

    DataWithArray data{
        42,
        {1.1f, 2.2f, 3.3f},
        "test"
    };

    auto [id, vals, name] = data;

    std::cout << "DataWithArray解包:\n";
    std::cout << "  ID: " << id << '\n';
    std::cout << "  Values: " << vals[0] << ", " << vals[1] << ", " << vals[2] << '\n';
    std::cout << "  Name: " << name << '\n';

    // 注意：vals是数组引用，不是指针
    static_assert(std::is_array_v<decltype(vals)>);
}

void nested_struct_binding() {
    std::cout << "\n=== 嵌套结构体 ===\n\n";

    struct Color {
        uint8_t r, g, b, a;
    };

    struct Pixel {
        int x, y;
        Color color;
    };

    Pixel pixel{100, 200, {255, 128, 64, 255}};

    // 外层解包
    auto [x, y, color] = pixel;

    std::cout << "Pixel外层:\n";
    std::cout << "  Position: (" << x << ", " << y << ")\n";

    // 内层解包
    auto [r, g, b, a] = color;
    std::cout << "  Color: RGBA(" << +r << ", " << +g << ", " << +b << ", " << +a << ")\n";
}

void binding_bit_fields() {
    std::cout << "\n=== 位域结构体 ===\n\n";

    struct Flags {
        unsigned int flag1 : 1;
        unsigned int flag2 : 1;
        unsigned int flag3 : 1;
        unsigned int reserved : 5;
    };

    Flags flags{1, 0, 1, 0};

    // 注意：位域的支持取决于编译器
    auto [f1, f2, f3, res] = flags;

    std::cout << "Flags位域:\n";
    std::cout << "  flag1: " << f1 << '\n';
    std::cout << "  flag2: " << f2 << '\n';
    std::cout << "  flag3: " << f3 << '\n';

    // 修改
    f2 = 1;
    std::cout << "\n修改f2后:\n";
    std::cout << "  flags.flag2: " << flags.flag2 << " (可能未改变，取决于实现)\n";
    std::cout << "  注意：位域绑定可能有意外行为\n";
}

void all_public_members_required() {
    std::cout << "\n=== 所有成员必须公有 ===\n\n";

    struct PublicStruct {
        int x, y;
    };

    struct PrivateStruct {
    private:
        int x, y;  // 私有成员
    };

    struct MixedStruct {
    public:
        int x;
    private:
        int y;  // 私有成员
    };

    PublicStruct ps{1, 2};
    auto [psx, psy] = ps;  // OK：所有成员公有

    std::cout << "PublicStruct: 可以绑定\n";

    // PrivateStruct pvs{1, 2};     // 错误：无法初始化私有成员
    // auto [pvx, pvy] = pvs;       // 错误：无法绑定私有成员

    // MixedStruct ms{1, 2};       // 错误：无法初始化私有成员
    // auto [msx, msy] = ms;       // 错误：无法绑定私有成员

    std::cout << "PrivateStruct/MixedStruct: 无法绑定（包含私有成员）\n";
}

void binding_with_static_members() {
    std::cout << "\n=== 静态成员不影响绑定 ===\n\n";

    // 注意：局部类不能有静态成员，所以在实际代码中
    // 静态成员需要在命名空间作用域中定义
    // 这里演示静态成员不参与结构化绑定的概念

    struct WithStatic {
        int x, y;
        // 静态成员必须在命名空间作用域中定义
        // static int count;
    };

    WithStatic ws{10, 20};

    // 静态成员不参与结构化绑定
    auto [x, y] = ws;  // 只绑定非静态成员

    std::cout << "WithStatic解包:\n";
    std::cout << "  x = " << x << '\n';
    std::cout << "  y = " << y << '\n';
    std::cout << "  说明：静态成员不参与结构化绑定\n";
}

void perfect_forwarding_with_structs() {
    std::cout << "\n=== 结构体与完美转发 ===\n\n";

    struct Config {
        int timeout_ms;
        int retry_count;
    };

    auto make_config = [](int timeout, int retry) {
        return Config{timeout, retry};
    };

    // 移动语义
    auto&& [timeout, retry] = make_config(5000, 3);
    std::cout << "Config:\n";
    std::cout << "  Timeout: " << timeout << " ms\n";
    std::cout << "  Retry: " << retry << " 次\n";
}

int main() {
    basic_struct_binding();
    complex_struct_binding();
    array_member_binding();
    nested_struct_binding();
    binding_bit_fields();
    all_public_members_required();
    binding_with_static_members();
    perfect_forwarding_with_structs();

    std::cout << "\n=== 演示结束 ===\n";

    std::cout << "\n总结:\n";
    std::cout << "1. 所有非静态成员必须是公有的\n";
    std::cout << "2. 静态成员不参与结构化绑定\n";
    std::cout << "3. 位域支持取决于编译器\n";
    std::cout << "4. 可以绑定嵌套结构体\n";
    std::cout << "5. 数组成员可以绑定\n";

    return 0;
}
