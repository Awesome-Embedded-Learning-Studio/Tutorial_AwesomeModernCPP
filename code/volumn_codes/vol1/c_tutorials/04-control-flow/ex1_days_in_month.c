#include <stdio.h>

int days_in_month(int month, int is_leap_year)
{
    switch (month) {
    case 1: case 3: case 5: case 7: case 8: case 10: case 12:
        return 31;
    case 4: case 6: case 9: case 11:
        return 30;
    case 2:
        return is_leap_year ? 29 : 28;
    default:
        return -1;
    }
}

int main(void)
{
    const char* kMonthNames[] = {
        "", "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };

    for (int m = 1; m <= 12; m++) {
        printf("%-4s: %2d days (normal), %2d days (leap)\n",
               kMonthNames[m],
               days_in_month(m, 0),
               days_in_month(m, 1));
    }

    return 0;
}
