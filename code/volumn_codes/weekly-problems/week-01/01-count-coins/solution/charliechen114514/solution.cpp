int helper(int total, int coin) {
    if (total == 0) {
        return 1;
    }
    if (total < 0 || coin == 0) {
        return 0;
    }
    int next;
    switch (coin) {
        case 1:
            next = 5;
            break;
        case 5:
            next = 10;
            break;
        case 10:
            next = 25;
            break;
        default:
            next = 0;
            break; // 25 已是最大面值,没有"下一个"
    }
    return helper(total - coin, coin) + helper(total, next);
}

int count_coins(int total) {
    return helper(total, 1);
}
