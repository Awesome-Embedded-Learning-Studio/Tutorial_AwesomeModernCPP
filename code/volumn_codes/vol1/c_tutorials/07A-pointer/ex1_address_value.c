#include <stdio.h>

int main(void) {

    int value_int = 0;
    double value_double = 0.0;
    char value_char = '0';

    printf("(int) value:%d         address:%p    size:%zu\n", value_int, (void*)&value_int,
           sizeof(int));
    printf("(double) value:%.2f    address:%p    size:%zu\n", value_double, (void*)&value_double,
           sizeof(double));
    printf("(char) value:%d        address:%p    size:%zu\n", value_char, (void*)&value_char,
           sizeof(char));
    return 0;
}
