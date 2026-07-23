#include <stdbool.h>
#include <stdio.h>

bool is_leap_year(int year) {
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

int month_day(int year, int month) {
    switch (month) {
        case 1:
        case 3:
        case 5:
        case 7:
        case 8:
        case 10:
        case 12:
            return 31;
        case 4:
        case 6:
        case 9:
        case 11:
            return 30;
        case 2:
            return is_leap_year(year) ? 29 : 28;
        default:
            return -1;
    }
}

int main(void) {
    printf("2024-2: %d (expect 29)\n", month_day(2024, 2));
    printf("2023-2: %d (expect 28)\n", month_day(2023, 2));
    printf("2000-2: %d (expect 29)\n", month_day(2000, 2));
    printf("1900-2: %d (expect 28)\n", month_day(1900, 2));
    printf("2024-7: %d (expect 31)\n", month_day(2024, 7));
    printf("2024-4: %d (expect 30)\n", month_day(2024, 4));
    printf("2024-13: %d (expect -1)\n", month_day(2024, 13));
    return 0;
}
