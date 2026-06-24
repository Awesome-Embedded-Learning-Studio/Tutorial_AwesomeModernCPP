// Standard: C++20
// <random> 的分布演示：mt19937 引擎喂给三种分布，各跑一百万个样本看统计量。
// normal(0,1) 看均值/标准差 + 直方图（钟形），bernoulli(0.7) 看 true 占比，
// uniform_real(0,1) 看均值是否逼近 0.5——分布对象自动处理好取模偏差，这是 rand()%N 做不到的。
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

int main() {
    std::mt19937 eng(2024);

    // 正态分布: 均值 0, 标准差 1
    std::normal_distribution<double> norm(0.0, 1.0);
    constexpr int N = 1000000;
    double sum = 0, sum2 = 0;
    std::vector<long long> hist(10, 0); // [-5, 5) 分 10 桶，每桶宽 1.0
    for (int i = 0; i < N; ++i) {
        double x = norm(eng);
        sum += x;
        sum2 += x * x;
        int b = int(x + 5.0);
        if (b >= 0 && b < 10)
            ++hist[b];
    }
    double mean = sum / N;
    double stddev = std::sqrt(sum2 / N - mean * mean);
    std::printf("normal(0,1) %d 样本: 均值=%.4f 标准差=%.4f\n", N, mean, stddev);
    std::printf("直方图(每桶宽1.0, [-5,5)):\n");
    for (int i = 0; i < 10; ++i) {
        std::printf("  [%+.0f,%+.0f) %lld\n", i - 5.0, i - 4.0, hist[i]);
    }

    // 伯努利: p=0.7
    std::bernoulli_distribution bern(0.7);
    long long trues = 0;
    for (int i = 0; i < N; ++i)
        if (bern(eng))
            ++trues;
    std::printf("bernoulli(0.7) %d 样本: true 占比=%.4f\n", N, double(trues) / N);

    // 均匀实数: [0, 1)
    std::uniform_real_distribution<double> ureal(0.0, 1.0);
    double usum = 0;
    for (int i = 0; i < N; ++i)
        usum += ureal(eng);
    std::printf("uniform_real(0,1) %d 样本: 均值=%.4f (期望 0.5)\n", N, usum / N);

    return 0;
}
