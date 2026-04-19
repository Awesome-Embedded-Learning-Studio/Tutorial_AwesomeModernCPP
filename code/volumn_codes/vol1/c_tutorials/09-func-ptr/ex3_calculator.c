#include <stdio.h>
#include <stdlib.h>

typedef int (*BinaryOp)(int, int);

static int op_add(int a, int b) { return a + b; }
static int op_sub(int a, int b) { return a - b; }
static int op_mul(int a, int b) { return a * b; }
static int op_div(int a, int b) { return b != 0 ? a / b : 0; }
static int op_mod(int a, int b) { return b != 0 ? a % b : 0; }

typedef struct {
    char symbol;
    BinaryOp op;
} OpEntry;

int main(void)
{
    OpEntry ops[] = {
        {'+', op_add}, {'-', op_sub}, {'*', op_mul},
        {'/', op_div}, {'%', op_mod},
    };
    int op_count = sizeof(ops) / sizeof(ops[0]);

    int a = 17, b = 5;
    printf("a=%d, b=%d\n", a, b);
    for (int i = 0; i < op_count; i++) {
        printf("%d %c %d = %d\n", a, ops[i].symbol, b, ops[i].op(a, b));
    }

    return 0;
}
