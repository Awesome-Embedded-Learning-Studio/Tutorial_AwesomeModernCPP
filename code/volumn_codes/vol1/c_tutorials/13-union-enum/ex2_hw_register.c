#include <stdio.h>
#include <stdint.h>

typedef struct {
    uint32_t enable          : 1;   // bit 0
    uint32_t interrupt_enable : 1;   // bit 1
    uint32_t dma_enable      : 1;   // bit 2
    uint32_t mode            : 3;   // bits 5:3
    uint32_t speed           : 4;   // bits 9:6
    uint32_t reserved        : 22;  // bits 31:10
} ControlRegBits;

typedef union {
    ControlRegBits bits;
    uint32_t       raw;
} ControlReg;

void print_register(const ControlReg* reg)
{
    printf("Register raw: 0x%08X\n", reg->raw);
    printf("  enable:           %u\n", reg->bits.enable);
    printf("  interrupt_enable: %u\n", reg->bits.interrupt_enable);
    printf("  dma_enable:       %u\n", reg->bits.dma_enable);
    printf("  mode:             %u\n", reg->bits.mode);
    printf("  speed:            %u\n", reg->bits.speed);
}

void set_mode(ControlReg* reg, uint32_t mode)
{
    reg->bits.mode = mode & 0x7;
}

int main(void)
{
    ControlReg reg = {0};
    reg.bits.enable = 1;
    reg.bits.interrupt_enable = 1;
    reg.bits.mode = 5;
    reg.bits.speed = 12;

    print_register(&reg);

    printf("\nSetting mode to 3:\n");
    set_mode(&reg, 3);
    print_register(&reg);

    printf("\nWriting raw value 0x00000305:\n");
    reg.raw = 0x00000305;
    print_register(&reg);

    return 0;
}
