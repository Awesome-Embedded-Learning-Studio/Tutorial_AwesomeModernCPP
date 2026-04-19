#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>
#include <stdalign.h>
#include <string.h>

// Compact frame with packed attribute
struct __attribute__((packed)) FrameHeader {
    uint8_t  start_flag;     // 0xAA
    uint8_t  type;
    uint16_t length;
    uint32_t timestamp;      // _Alignas(4) not compatible with packed
};

// Normal frame with alignment control
struct AlignedFrameHeader {
    uint8_t  start_flag;
    uint8_t  type;
    uint16_t length;
    alignas(4) uint32_t timestamp;
};

// Frame with flexible array member
typedef struct {
    struct AlignedFrameHeader header;
    uint16_t crc16;
    uint8_t  payload[];
} Frame;

void print_frame_layout(void)
{
    printf("=== Packed FrameHeader ===\n");
    printf("  sizeof: %zu\n", sizeof(struct FrameHeader));
    printf("  offsetof(start_flag): %zu\n", offsetof(struct FrameHeader, start_flag));
    printf("  offsetof(type):       %zu\n", offsetof(struct FrameHeader, type));
    printf("  offsetof(length):     %zu\n", offsetof(struct FrameHeader, length));
    printf("  offsetof(timestamp):  %zu\n", offsetof(struct FrameHeader, timestamp));

    printf("\n=== Aligned FrameHeader ===\n");
    printf("  sizeof: %zu\n", sizeof(struct AlignedFrameHeader));
    printf("  offsetof(start_flag): %zu\n", offsetof(struct AlignedFrameHeader, start_flag));
    printf("  offsetof(type):       %zu\n", offsetof(struct AlignedFrameHeader, type));
    printf("  offsetof(length):     %zu\n", offsetof(struct AlignedFrameHeader, length));
    printf("  offsetof(timestamp):  %zu\n", offsetof(struct AlignedFrameHeader, timestamp));

    printf("\n=== Frame (with FAM) ===\n");
    printf("  sizeof (no payload): %zu\n", sizeof(Frame));
    printf("  offsetof(crc16):     %zu\n", offsetof(Frame, crc16));
    printf("  offsetof(payload):   %zu\n", offsetof(Frame, payload));
}

Frame* create_frame(uint8_t type, const uint8_t* data, size_t data_len)
{
    size_t frame_size = sizeof(Frame) + data_len;
    Frame* f = (Frame*)malloc(frame_size);  // NOLINT
    if (!f) return NULL;

    f->header.start_flag = 0xAA;
    f->header.type = type;
    f->header.length = (uint16_t)data_len;
    f->header.timestamp = 12345678;
    f->crc16 = 0;  // Placeholder
    memcpy(f->payload, data, data_len);

    return f;
}

int main(void)
{
    print_frame_layout();

    uint8_t payload[] = {0x01, 0x02, 0x03, 0x04};
    Frame* f = create_frame(0x10, payload, sizeof(payload));
    if (f) {
        printf("\nCreated frame: type=0x%02X, length=%u, timestamp=%u\n",
               f->header.type, f->header.length, f->header.timestamp);
        printf("Payload bytes: ");
        for (size_t i = 0; i < f->header.length; i++) {
            printf("%02X ", f->payload[i]);
        }
        printf("\n");
        free(f);
    }

    return 0;
}
