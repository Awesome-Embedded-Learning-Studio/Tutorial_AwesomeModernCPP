// 05_conditionals.cpp
// 条件语句综合演示：if-else、switch、三元运算符、C++17 init-if

#include <iostream>

char grade_by_if(int score) {
    if (score >= 90) {
        return 'A';
    } else if (score >= 80) {
        return 'B';
    } else if (score >= 70) {
        return 'C';
    } else if (score >= 60) {
        return 'D';
    } else {
        return 'F';
    }
}

char grade_by_switch(int score) {
    switch (score / 10) {
        case 10:
        case 9:
            return 'A';
        case 8:
            return 'B';
        case 7:
            return 'C';
        case 6:
            return 'D';
        default:
            return 'F';
    }
}

int main() {
    // 用固定分数演示（方便在线运行）
    const int kScore = 85;

    std::cout << "成绩: " << kScore << std::endl;
    std::cout << "if-else 判定结果: " << grade_by_if(kScore) << std::endl;
    std::cout << "switch 判定结果:  " << grade_by_switch(kScore) << std::endl;
    std::cout << "是否及格: " << (kScore >= 60 ? "是" : "否") << std::endl;

    // C++17 带初始化器的 if
    if (int diff = kScore - 60; diff >= 0) {
        std::cout << "超过及格线 " << diff << " 分" << std::endl;
    } else {
        std::cout << "距离及格还差 " << -diff << " 分" << std::endl;
    }

    return 0;
}
