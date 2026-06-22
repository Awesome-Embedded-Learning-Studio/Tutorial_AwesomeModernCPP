#pragma once

// 组合模式(Composite)工程示例,配套文章:
//   documents/vol4-advanced/vol4-generics-patterns/08-composite.md
//
// 文章走了两条路,本工程一并收录,顺序与文章一致:
//   第一条路:把同类对象聚合成数组
//     - Creature / CreatureAnalyzer(把散落的能力塞进 std::array,交给标准库算法)
//     - Analyzer<T, Getters...>(用 std::invoke + 变参模板把聚合做成通用工具)
//   第二条路:经典树形 Composite(GoF 的标准答案)
//     - Graphic / Circle / Group(Group 自己也是 Graphic,draw 递归)
//     - 采用「安全式」:add 只声明在 Group 上,叶子没有 add,编译期挡住错误
//
// 所有权用 std::unique_ptr 钉死,Group 析构时整棵子树自动回收。

#include <algorithm>
#include <array>
#include <cstddef>
#include <functional>
#include <iostream>
#include <memory>
#include <numeric>
#include <string>
#include <utility>
#include <vector>

// ============================================================================
// 第一条路:把同类对象聚合成数组
// ============================================================================

// 三个「本质上是同一类东西」的属性,一开始各自为政。
class Creature {
    int strength;
    int agility;
    int intelligence;

  public:
    Creature(int s, int a, int i) : strength(s), agility(a), intelligence(i) {}

    int get_strength() const { return strength; }
    int get_agility() const { return agility; }
    int get_intelligence() const { return intelligence; }
};

// 把三项能力在构造时一次性拷进 std::array,从此它们是一个整体。
// 数量是数量,单写一个 constexpr std::size_t kAbilityCount,
// 不要学老 C 把数量塞进 enum 的尾元素。
class CreatureAnalyzer {
  public:
    explicit CreatureAnalyzer(const Creature& c)
        : abilities_{c.get_strength(), c.get_agility(), c.get_intelligence()} {}

    int sum() const { return std::accumulate(abilities_.begin(), abilities_.end(), 0); }

    double avg() const { return sum() / static_cast<double>(kAbilityCount); }

    int max() const { return *std::max_element(abilities_.begin(), abilities_.end()); }

  private:
    static constexpr std::size_t kAbilityCount = 3;
    std::array<int, kAbilityCount> abilities_;
};

// 再进一步:让 Analyzer 自己挑字段。std::invoke 统一处理成员指针 / lambda /
// 任何能对 obj 调用的东西,把一包 getter 展开成数组。
template <typename T, typename... Getters> class Analyzer {
  public:
    static constexpr std::size_t N = sizeof...(Getters);
    std::array<int, N> vals;

    Analyzer(const T& obj, Getters... getters) : vals{std::invoke(getters, obj)...} {}

    int sum() const { return std::accumulate(vals.begin(), vals.end(), 0); }

    double avg() const { return sum() / static_cast<double>(N); }

    int max() const { return *std::max_element(vals.begin(), vals.end()); }
};

// ============================================================================
// 第二条路:经典树形 Composite(安全式)
// ============================================================================

// 安全式:基类 Graphic 只声明 draw,没有 add。
// 叶子没有 add,Group 才有 add,错误在编译期就被挡住。
class Graphic {
  public:
    virtual ~Graphic() = default;
    virtual void draw() const = 0;
    // 安全式:基类里没有 add
};

class Circle : public Graphic {
  public:
    explicit Circle(std::string name) : name_(std::move(name)) {}

    void draw() const override { std::cout << "  Circle[" << name_ << "] drawn\n"; }

  private:
    std::string name_;
};

// Group 自己也是一个 Graphic:draw 的实现就是遍历孩子、对每个孩子调 draw。
// 递归是隐式的——孩子若是 Group,虚分发会再次进入 Group::draw,层层展开。
// 用 vector<unique_ptr<Graphic>> 装孩子,所有权随 add 转交,
// Group 析构时整棵子树自动回收,一行手动 delete 都不用写。
class Group : public Graphic {
  public:
    explicit Group(std::string name) : name_(std::move(name)) {}

    void draw() const override {
        std::cout << "Group[" << name_ << "] (\n";
        for (const auto& child : children_) {
            child->draw();
        }
        std::cout << ") end Group[" << name_ << "]\n";
    }

    void add(std::unique_ptr<Graphic> g) { children_.push_back(std::move(g)); }

  private:
    std::string name_;
    std::vector<std::unique_ptr<Graphic>> children_;
};
