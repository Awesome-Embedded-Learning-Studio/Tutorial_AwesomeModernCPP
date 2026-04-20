/**
 * @file ex03_find_primes.cpp
 * @brief 练习：找素数
 *
 * 输入 N，使用嵌套循环和 break 打印 2 到 N 之间的所有素数。
 * 素数判断：如果 n 不能被 2 到 sqrt(n) 之间的任何数整除，则 n 是素数。
 */

#include <cmath>
#include <iostream>

bool is_prime(int n) {
    if (n < 2) {
        return false;
    }
    if (n == 2) {
        return true;
    }
    if (n % 2 == 0) {
        return false;
    }
    int limit = static_cast<int>(std::sqrt(n));
    for (int i = 3; i <= limit; i += 2) {
        if (n % i == 0) {
            // 找到因子，不是素数，提前退出
            break;
        }
        // 注意：这个 break 只跳出内层 if，不是 for 循环
        // 正确的写法应该直接 return false
    }
    // 修正：直接在循环中返回
    for (int i = 3; i <= limit; i += 2) {
        if (n % i == 0) {
            return false;  // 找到因子，不是素数
        }
    }
    return true;
}

int main() {
    int n = 0;
    std::cout << "请输入一个正整数 N: ";
    std::cin >> n;

    if (n < 2) {
        std::cout << "没有小于 2 的素数\n";
        return 0;
    }

    std::cout << "\n2 到 " << n << " 之间的素数：\n";

    int count = 0;
    for (int i = 2; i <= n; ++i) {
        bool prime = true;
        int limit = static_cast<int>(std::sqrt(i));

        for (int j = 2; j <= limit; ++j) {
            if (i % j == 0) {
                prime = false;
                break;  // 找到因子，不需要继续检查
            }
        }

        if (prime) {
            std::cout << i << " ";
            ++count;
            // 每行打印 10 个
            if (count % 10 == 0) {
                std::cout << '\n';
            }
        }
    }
    std::cout << "\n\n共找到 " << count << " 个素数\n";

    return 0;
}
