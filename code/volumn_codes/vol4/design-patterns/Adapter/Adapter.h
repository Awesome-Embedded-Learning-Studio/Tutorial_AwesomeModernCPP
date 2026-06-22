#pragma once

#include <cstddef>
#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

// ============================================================================
// 适配器模式(Adapter)—— 把"两边接口对不上、又都不能改"的事后补救封装在一层
// 翻译里。本头文件按教学顺序给出:
//   1. 几何三件套 Point2D / Line / Rectangle(业务侧抽象,产出 Line)
//   2. OledDriver(Adaptee:只会画 Point,改不了)
//   3. LineToPointsAdapter(对象适配器:持有一份 Line 拷贝,展开成 Point)
//   4. Printer / LegacyLogger / LoggerAdapter(Target + Adaptee + 对象适配器)
//   5. BidirectionalAdapter(双向适配器:同时维护 Line 与 Point 两份数据)
//   6. CachedLineToPointsAdapter(缓存优化:按源数据地址记忆展开结果)
// 详见 documents/vol4-advanced/vol4-generics-patterns/05-adapter.md。
// ============================================================================

// ---------------------------------------------------------------------------
// 业务侧的几何抽象:Line 由两个 Point2D 组成,Rectangle 内部用四条 Line
// ---------------------------------------------------------------------------
struct Point2D {
    int x;
    int y;
};

struct Line {
    Point2D start;
    Point2D end;
};

class Rectangle {
  public:
    Rectangle(Point2D left_top, int width, int height) {
        Point2D rt{left_top.x + width, left_top.y};
        Point2D lb{left_top.x, left_top.y + height};
        Point2D rb{left_top.x + width, left_top.y + height};
        lines_ = {{left_top, rt}, {rt, rb}, {rb, lb}, {lb, left_top}};
    }
    const std::vector<Line>& lines() const { return lines_; }

  private:
    std::vector<Line> lines_;
};

// ---------------------------------------------------------------------------
// Adaptee:OLED 驱动只会画点,接口要的是 [begin, end) 的 Point 区间
// ---------------------------------------------------------------------------
class OledDriver {
  public:
    // Adaptee 侧的接口:只认 Point,不认 Line
    using ConstIter = std::vector<Point2D>::const_iterator;

    void draw_points(ConstIter begin, ConstIter end) {
        for (auto it = begin; it != end; ++it) {
            set_pixel(it->x, it->y);
        }
    }

  private:
    void set_pixel(int /*x*/, int /*y*/) {
        ++pixels_drawn_; // 这里用计数模拟"画了一个点"
    }

  public:
    int pixels_drawn_ = 0;
};

// ---------------------------------------------------------------------------
// 对象适配器一:持有一份 Line 的拷贝,构造时展开成 Point
// ---------------------------------------------------------------------------
class LineToPointsAdapter {
  public:
    using ConstIter = std::vector<Point2D>::const_iterator;

    explicit LineToPointsAdapter(const std::vector<Line>& lines) {
        points_.reserve(lines.size() * 2);
        for (const auto& l : lines) {
            points_.push_back(l.start);
            points_.push_back(l.end);
        }
    }

    // 对外暴露的"Target 接口":一对迭代器,正好喂给 OledDriver
    std::pair<ConstIter, ConstIter> points() const { return {points_.begin(), points_.end()}; }

  private:
    std::vector<Point2D> points_;
};

// ---------------------------------------------------------------------------
// 经典透明性示例:Target(Printer)期望 print(string),
// Adaptee(LegacyLogger)只会 write_line(const char*),签名对不上
// ---------------------------------------------------------------------------
class Printer {
  public:
    virtual ~Printer() = default;
    virtual void print(const std::string& msg) = 0;
};

// Adaptee:旧类,签名不兼容,而且改不了
class LegacyLogger {
  public:
    void write_line(const char* content) { std::cout << "[legacy] " << content << "\n"; }
};

// 对象适配器:实现 Target,内部持有 Adaptee
class LoggerAdapter : public Printer {
  public:
    explicit LoggerAdapter(std::unique_ptr<LegacyLogger> adaptee) : adaptee_(std::move(adaptee)) {}

    void print(const std::string& msg) override {
        adaptee_->write_line(msg.c_str()); // 翻译:std::string -> const char*
    }

  private:
    std::unique_ptr<LegacyLogger> adaptee_;
};

// ---------------------------------------------------------------------------
// 双向适配器:内部同时维护 lines_ 与 points_,两个方向都能导出
// ---------------------------------------------------------------------------
class BidirectionalAdapter {
  public:
    // 方向一:从 Line 进来,顺带展开成 Point
    explicit BidirectionalAdapter(std::vector<Line> lines) : lines_(std::move(lines)) {
        points_.reserve(lines_.size() * 2);
        for (const auto& l : lines_) {
            points_.push_back(l.start);
            points_.push_back(l.end);
        }
    }

    // 对外两个方向都能取:要 Line 给 Line,要 Point 给 Point
    const std::vector<Line>& lines() const { return lines_; }
    const std::vector<Point2D>& points() const { return points_; }

  private:
    std::vector<Line> lines_;
    std::vector<Point2D> points_;
};

// ---------------------------------------------------------------------------
// 缓存优化:按源数据地址当 key,第一次展开后记住结果,后续命中即跳过展开
// 注意:用裸指针当 key 要求源数据生命周期盖过缓存,内容变更不会被感知
// ---------------------------------------------------------------------------
class CachedLineToPointsAdapter {
  public:
    explicit CachedLineToPointsAdapter(std::vector<Line>* key) : key_(key) {}

    const std::vector<Point2D>& get_points() {
        auto found = cache_.find(key_);
        if (found != cache_.end()) {
            return found->second; // 命中缓存,跳过展开
        }
        // 未命中:第一次展开,并存入缓存
        ++expand_calls_;
        std::vector<Point2D> pts;
        pts.reserve(key_->size() * 2);
        for (const auto& l : *key_) {
            pts.push_back(l.start);
            pts.push_back(l.end);
        }
        return cache_[key_] = std::move(pts);
    }

    static std::size_t expand_calls_; // 统计真实展开次数(演示用)

  private:
    std::vector<Line>* key_;
    static inline std::unordered_map<std::vector<Line>*, std::vector<Point2D>> cache_;
};

inline std::size_t CachedLineToPointsAdapter::expand_calls_ = 0;
