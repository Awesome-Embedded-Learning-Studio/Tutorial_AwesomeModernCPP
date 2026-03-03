// packet_parsing.cpp - 实际嵌入式场景：协议包解析
#include <span>
#include <cstdint>
#include <iostream>

// 模拟 CRC 校验函数
bool crc_check(const uint8_t* data, size_t length) {
    // 简化：假设 CRC 总是正确
    (void)data;
    (void)length;
    return true;
}

// 解析数据包
void recv_packet(std::span<uint8_t> buffer) {
    std::cout << "Received " << buffer.size() << " bytes\n";

    // 检查最小包长度
    if (buffer.size() < 4) {
        std::cout << "Packet too short\n";
        return;
    }

    // 解析头部（前4字节）
    auto header = buffer.first(4);
    uint8_t msg_id = header[0];
    uint8_t flags = header[1];
    uint16_t length = header[2] | (header[3] << 8);

    std::cout << "Header: ID=" << int(msg_id)
              << ", Flags=" << int(flags)
              << ", Length=" << length << '\n';

    // 检查长度字段是否合法
    if (buffer.size() < 4 + length) {
        std::cout << "Length field exceeds buffer size\n";
        return;
    }

    // 提取 payload
    auto payload = buffer.subspan(4, length);
    std::cout << "Payload: ";
    for (auto b : payload) {
        std::cout << std::hex << int(b) << ' ';
    }
    std::cout << std::dec << '\n';

    // CRC 校验（把 payload 当作字节流）
    auto bytes = std::as_bytes(payload);
    if (crc_check(static_cast<const uint8_t*>(bytes.data()), bytes.size())) {
        std::cout << "CRC: OK\n";
    } else {
        std::cout << "CRC: FAILED\n";
    }
}

int main() {
    // 模拟接收到的数据包
    // 格式: [MSG_ID][FLAGS][LEN_L][LEN_H][PAYLOAD...]
    std::array<uint8_t, 16> packet = {
        0x01,        // Message ID
        0x00,        // Flags
        0x08, 0x00,  // Length = 8
        // Payload (8 bytes)
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
        // Extra data (should be ignored)
        0xFF, 0xFF, 0xFF, 0xFF
    };

    std::cout << "=== Parsing valid packet ===\n";
    recv_packet(packet);

    // 测试短包
    std::array<uint8_t, 2> short_packet = {0x01, 0x00};
    std::cout << "\n=== Parsing short packet ===\n";
    recv_packet(short_packet);

    // 测试长度字段不匹配
    std::array<uint8_t, 10> bad_length_packet = {
        0x01, 0x00, 0x20, 0x00,  // Claims length=32
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06
    };
    std::cout << "\n=== Parsing packet with bad length ===\n";
    recv_packet(bad_length_packet);

    return 0;
}
