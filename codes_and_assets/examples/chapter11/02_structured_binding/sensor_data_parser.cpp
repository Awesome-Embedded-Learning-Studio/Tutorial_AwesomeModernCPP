// sensor_data_parser.cpp - 嵌入式传感器数据解析示例
#include <iostream>
#include <array>
#include <tuple>
#include <string>
#include <cstdint>

// 传感器数据结构
struct SensorReading {
    uint8_t sensor_id;
    float value;
    uint32_t timestamp;
    bool is_valid;
};

// 错误码
enum class ParseError {
    Ok,
    InvalidLength,
    ChecksumFailed,
    UnsupportedSensor
};

// 解析结果
struct ParseResult {
    SensorReading reading;
    ParseError error;
};

// 模拟传感器数据解析
ParseResult parse_sensor_data(const uint8_t* buffer, size_t size) {
    // 格式: [SENSOR_ID(1)] [VALUE_H(1)] [VALUE_L(1)] [TIMESTAMP(4)] [CRC(1)]
    if (size < 8) {
        return {SensorReading{}, ParseError::InvalidLength};
    }

    uint8_t sensor_id = buffer[0];
    uint16_t value_raw = (buffer[1] << 8) | buffer[2];
    float value = value_raw / 100.0f;  // 转换为实际值

    uint32_t timestamp = 0;
    for (int i = 0; i < 4; ++i) {
        timestamp = (timestamp << 8) | buffer[3 + i];
    }

    uint8_t received_crc = buffer[7];
    uint8_t calculated_crc = sensor_id ^ buffer[1] ^ buffer[2] ^ buffer[3] ^ buffer[4] ^ buffer[5] ^ buffer[6];

    if (received_crc != calculated_crc) {
        return {SensorReading{}, ParseError::ChecksumFailed};
    }

    return {
        {sensor_id, value, timestamp, true},
        ParseError::Ok
    };
}

// 配置解析
struct ConfigEntry {
    std::string key;
    std::string value;
    bool is_default;
};

ConfigEntry parse_config_line(const std::string& line) {
    auto pos = line.find('=');
    if (pos == std::string::npos || pos == 0) {
        return {"", "", true};  // 默认值
    }

    std::string key = line.substr(0, pos);
    std::string value = line.substr(pos + 1);

    // 去除空格
    auto trim = [](std::string& s) {
        size_t start = s.find_first_not_of(" \t");
        if (start != std::string::npos) {
            s.erase(0, start);
        }
        size_t end = s.find_last_not_of(" \t");
        if (end != std::string::npos && end + 1 < s.length()) {
            s.erase(end + 1);
        }
    };

    trim(key);
    trim(value);

    return {key, value, false};
}

// DMA传输状态
struct TransferStatus {
    bool completed;
    bool error;
    size_t bytes_transferred;
};

TransferStatus check_dma_transfer(uint32_t status_reg) {
    bool completed = (status_reg & 0x01) != 0;
    bool error = (status_reg & 0x02) != 0;
    size_t transferred = (status_reg >> 16) & 0xFFFF;

    return {completed, error, transferred};
}

void demonstrate_sensor_parsing() {
    std::cout << "=== 传感器数据解析 ===\n\n";

    // 模拟接收缓冲区
    std::array<uint8_t, 8> buffer = {
        0x05,           // Sensor ID = 5
        0x09, 0x74,     // Value = 2404 -> 24.04
        0x00, 0x12, 0x34, 0x56,  // Timestamp
        0x00            // CRC (假设正确)
    };

    auto [reading, error] = parse_sensor_data(buffer.data(), buffer.size());

    if (error == ParseError::Ok) {
        std::cout << "解析成功:\n";
        std::cout << "  传感器ID: " << +reading.sensor_id << '\n';
        std::cout << "  数值: " << reading.value << '\n';
        std::cout << "  时间戳: " << reading.timestamp << '\n';
    } else {
        std::cout << "解析失败: 错误码 " << static_cast<int>(error) << '\n';
    }

    // 错误情况
    std::cout << "\n错误情况演示:\n";
    std::array<uint8_t, 4> short_buffer = {1, 2, 3, 4};
    auto [reading2, error2] = parse_sensor_data(short_buffer.data(), short_buffer.size());

    std::cout << "  错误: ";
    switch (error2) {
        case ParseError::Ok: std::cout << "无"; break;
        case ParseError::InvalidLength: std::cout << "长度无效"; break;
        case ParseError::ChecksumFailed: std::cout << "校验和失败"; break;
        case ParseError::UnsupportedSensor: std::cout << "不支持的传感器"; break;
    }
    std::cout << '\n';
}

void demonstrate_config_parsing() {
    std::cout << "\n=== 配置解析 ===\n\n";

    std::array<std::string, 5> config_lines = {
        "baudrate = 115200",
        "parity = none",
        "stop_bits = 1",
        "invalid line",
        "timeout=5000"
    };

    std::cout << "解析配置:\n";
    for (const auto& line : config_lines) {
        auto [key, value, is_default] = parse_config_line(line);

        if (!is_default && !key.empty()) {
            std::cout << "  " << key << " = " << value << '\n';
        } else if (is_default) {
            std::cout << "  跳过无效行: \"" << line << "\"\n";
        }
    }
}

void demonstrate_dma_status() {
    std::cout << "\n=== DMA传输状态 ===\n\n";

    // 模拟DMA状态寄存器
    // bit 0: 传输完成
    // bit 1: 传输错误
    // bit 16-31: 传输字节数
    uint32_t dma_status = 0x00040103;  // 完成状态，1030字节

    auto [completed, error, transferred] = check_dma_transfer(dma_status);

    std::cout << "DMA状态:\n";
    std::cout << "  完成: " << std::boolalpha << completed << '\n';
    std::cout << "  错误: " << error << '\n';
    std::cout << "  传输字节: " << transferred << '\n';
}

// GPIO状态
struct GPIO_Status {
    bool pin0 : 1;
    bool pin1 : 1;
    bool pin2 : 1;
    bool pin3 : 1;
    bool pin4 : 1;
    bool pin5 : 1;
    bool pin6 : 1;
    bool pin7 : 1;
};

GPIO_Status read_gpio_status(uint16_t port) {
    return {
        static_cast<bool>((port >> 0) & 1),
        static_cast<bool>((port >> 1) & 1),
        static_cast<bool>((port >> 2) & 1),
        static_cast<bool>((port >> 3) & 1),
        static_cast<bool>((port >> 4) & 1),
        static_cast<bool>((port >> 5) & 1),
        static_cast<bool>((port >> 6) & 1),
        static_cast<bool>((port >> 7) & 1)
    };
}

void demonstrate_gpio_status() {
    std::cout << "\n=== GPIO状态读取 ===\n\n";

    uint16_t gpio_input = 0b10101010;

    auto [p0, p1, p2, p3, p4, p5, p6, p7] = read_gpio_status(gpio_input);

    std::cout << "GPIO引脚状态:\n";
    std::cout << "  PIN0: " << p0 << '\n';
    std::cout << "  PIN1: " << p1 << '\n';
    std::cout << "  PIN2: " << p2 << '\n';
    std::cout << "  PIN3: " << p3 << '\n';
    std::cout << "  PIN4: " << p4 << '\n';
    std::cout << "  PIN5: " << p5 << '\n';
    std::cout << "  PIN6: " << p6 << '\n';
    std::cout << "  PIN7: " << p7 << '\n';

    // 检查特定引脚
    if (p0 || p1) {
        std::cout << "\n触发中断: PIN0或PIN1有信号\n";
    }
}

// UART帧状态
struct UART_FrameStatus {
    bool parity_error;
    bool framing_error;
    bool noise_error;
    bool overrun_error;
    uint8_t received_data;
};

UART_FrameStatus parse_uart_status(uint32_t isr, uint32_t rdr) {
    return {
        (isr & 0x01) != 0,  // 奇偶校验错误
        (isr & 0x02) != 0,  // 帧错误
        (isr & 0x04) != 0,  // 噪声错误
        (isr & 0x08) != 0,  // 过载错误
        static_cast<uint8_t>(rdr & 0xFF)
    };
}

void demonstrate_uart_status() {
    std::cout << "\n=== UART状态解析 ===\n\n";

    uint32_t uart_isr = 0x00;  // 无错误
    uint32_t uart_rdr = 0x42;  // 接收数据 'B'

    auto [parity, framing, noise, overrun, data] = parse_uart_status(uart_isr, uart_rdr);

    if (!parity && !framing && !noise && !overrun) {
        std::cout << "接收数据: " << static_cast<char>(data) << " (0x" << std::hex << +data << std::dec << ")\n";
    }

    // 模拟错误
    uart_isr = 0x03;  // 奇偶和帧错误
    auto [p2, f2, n2, o2, d2] = parse_uart_status(uart_isr, uart_rdr);

    std::cout << "\n错误状态:\n";
    std::cout << "  奇偶错误: " << p2 << '\n';
    std::cout << "  帧错误: " << f2 << '\n';
    std::cout << "  噪声错误: " << n2 << '\n';
    std::cout << "  过载错误: " << o2 << '\n';
}

int main() {
    demonstrate_sensor_parsing();
    demonstrate_config_parsing();
    demonstrate_dma_status();
    demonstrate_gpio_status();
    demonstrate_uart_status();

    std::cout << "\n=== 演示结束 ===\n";

    std::cout << "\n嵌入式应用总结:\n";
    std::cout << "1. 传感器数据解析：解包ID、值、时间戳\n";
    std::cout << "2. 配置解析：key-value对处理\n";
    std::cout << "3. 状态寄存器解包：DMA、GPIO、UART状态\n";
    std::cout << "4. 错误处理：结构化绑定让错误检查更直观\n";

    return 0;
}
