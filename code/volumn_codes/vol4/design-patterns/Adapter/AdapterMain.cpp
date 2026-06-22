#include "Adapter.h"

#include <iostream>
#include <memory>
#include <utility>

// 业务代码:只依赖 Target 抽象,完全不知道 LegacyLogger 存在
static void greet(Printer& p) {
    p.print("hello from adapter");
}

static void demo_object_adapter() {
    std::cout << "== 对象适配器一:Line -> Point ============================\n";
    Rectangle rect({0, 0}, 10, 5);
    LineToPointsAdapter adapter(rect.lines());

    OledDriver driver;
    auto [begin, end] = adapter.points();
    driver.draw_points(begin, end);

    std::cout << "pixels_drawn = " << driver.pixels_drawn_ << " (expect 8)\n\n";
}

static void demo_logger_adapter() {
    std::cout << "== 对象适配器二:Printer <-> LegacyLogger =================\n";
    auto logger = std::make_unique<LoggerAdapter>(std::make_unique<LegacyLogger>());
    greet(*logger);
    std::cout << "\n";
}

static void demo_bidirectional_adapter() {
    std::cout << "== 双向适配器:Line <-> Point ============================\n";
    Rectangle rect({0, 0}, 10, 5);
    BidirectionalAdapter bidi(rect.lines());

    std::cout << "bidi points = " << bidi.points().size() << " (expect 8)\n";
    std::cout << "bidi lines  = " << bidi.lines().size() << " (expect 4)\n\n";
}

static void demo_cached_adapter() {
    std::cout << "== 缓存适配器:连画五帧,展开只发生一次 ===================\n";
    Rectangle rect({0, 0}, 10, 5);
    auto lines = const_cast<std::vector<Line>&>(rect.lines());
    CachedLineToPointsAdapter cached(&lines);

    constexpr int kFrames = 5;
    for (int i = 0; i < kFrames; ++i) {
        const auto& points = cached.get_points();
        std::cout << "frame " << i << ": points = " << points.size() << "\n";
    }
    std::cout << "expand_calls = " << CachedLineToPointsAdapter::expand_calls_ << " (expect 1)\n\n";
}

int main() {
    demo_object_adapter();
    demo_logger_adapter();
    demo_bidirectional_adapter();
    demo_cached_adapter();
    return 0;
}
