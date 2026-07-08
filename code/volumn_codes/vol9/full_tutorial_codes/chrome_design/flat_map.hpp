// flat_map.hpp —— Chromium 风格 flat_map/flat_tree 的教学版完整实现
// 对应:flat_map 实战(二)~(五) + 设计指南(二)
// 设计要点(每个决策见 articles):
//   - flat_tree<Key,GetKeyFromValue,KeyCompare,Container> 是唯一实现核心
//   - flat_map = flat_tree 子类(GetFirst 提 pair.first);flat_set = flat_tree 别名(std::identity)
//   - 有序不变量:构造期 sort_and_unique + 插入期 lower_bound
//   - sorted_unique_t tag dispatch 跳过 sort_and_unique,配 DCHECK 诚实契约
//   - 透明比较(std::less<> 默认)+ [[no_unique_address]] EBO 空比较器
// 编译:g++/clang++ -std=c++20

#pragma once

#include <algorithm>
#include <cassert>
#include <functional>
#include <ranges>
#include <tuple>
#include <utility>
#include <vector>

namespace tamcpp::chrome {

// sorted_unique tag(跳过 sort_and_unique 的诚实契约)—— 放 chrome 顶层,using namespace 可见
struct sorted_unique_t {};
inline constexpr sorted_unique_t sorted_unique{};

namespace internal {

template <class Key, class GetKeyFromValue, class KeyCompare, class Container> class flat_tree {
  public:
    using key_type = Key;
    using key_compare = KeyCompare;
    using value_type = typename Container::value_type;
    using container_type = Container;
    using iterator = typename Container::iterator;
    using const_iterator = typename Container::const_iterator;
    using size_type = typename Container::size_type;

    // —— 构造 ——
    flat_tree() = default;
    explicit flat_tree(const KeyCompare& c) : comp_(c) {}

    template <class InputIt>
    flat_tree(InputIt first, InputIt last, const KeyCompare& c = KeyCompare())
        : body_(first, last), comp_(c) {
        sort_and_unique();
    }
    flat_tree(Container body, const KeyCompare& c = KeyCompare())
        : body_(std::move(body)), comp_(c) {
        sort_and_unique();
    }
    // sorted_unique 构造:跳过排序,只 debug 校验
    template <class InputIt>
    flat_tree(sorted_unique_t, InputIt first, InputIt last, const KeyCompare& c = KeyCompare())
        : body_(first, last), comp_(c) {
        assert(is_sorted_unique() && "sorted_unique 构造要求输入确实有序无重复");
    }
    flat_tree(sorted_unique_t, Container body, const KeyCompare& c = KeyCompare())
        : body_(std::move(body)), comp_(c) {
        assert(is_sorted_unique() && "sorted_unique 构造要求输入确实有序无重复");
    }
    // initializer_list 构造(支持 flat_map<...> m{{k,v},...})
    flat_tree(std::initializer_list<value_type> il, const KeyCompare& c = KeyCompare())
        : body_(il.begin(), il.end()), comp_(c) {
        sort_and_unique();
    }
    flat_tree(sorted_unique_t, std::initializer_list<value_type> il,
              const KeyCompare& c = KeyCompare())
        : body_(il.begin(), il.end()), comp_(c) {
        assert(is_sorted_unique() && "sorted_unique 构造要求输入确实有序无重复");
    }

    // —— 查找 O(log n) ——(const 与非 const 两套,operator[]/at/insert_or_assign 需非 const)
    iterator lower_bound(const Key& key) {
        return std::lower_bound(body_.begin(), body_.end(), key,
                                [this](const value_type& v, const Key& k) { return less(v, k); });
    }
    const_iterator lower_bound(const Key& key) const {
        return std::lower_bound(body_.begin(), body_.end(), key,
                                [this](const value_type& v, const Key& k) { return less(v, k); });
    }
    iterator find(const Key& key) {
        auto it = lower_bound(key);
        if (it != body_.end() && !less(key, *it))
            return it;
        return body_.end();
    }
    const_iterator find(const Key& key) const {
        auto it = lower_bound(key);
        if (it != body_.end() && !less(key, *it))
            return it;
        return body_.end();
    }
    bool contains(const Key& key) const { return find(key) != body_.end(); }
    size_type count(const Key& key) const { return contains(key) ? 1 : 0; }

    // —— 插入 O(n) shift ——
    std::pair<iterator, bool> insert(value_type v) {
        auto it = std::lower_bound(
            body_.begin(), body_.end(), v,
            [this](const value_type& a, const value_type& b) { return less(a, b); });
        if (it != body_.end() && !less(v, *it))
            return {it, false};
        return {body_.emplace(it, std::move(v)), true};
    }

    iterator erase(const_iterator pos) { return body_.erase(pos); }
    size_type erase(const Key& key) {
        auto it = find(key);
        if (it == body_.end())
            return 0;
        body_.erase(it);
        return 1;
    }

    // —— 批量重建 ——
    container_type extract() && { return std::exchange(body_, Container{}); }
    void replace(container_type body) {
        body_ = std::move(body);
        assert(is_sorted_unique() && "replace 要求新数据有序无重复");
    }

    // —— 通用接口 ——
    size_type size() const { return body_.size(); }
    bool empty() const { return body_.empty(); }
    iterator begin() { return body_.begin(); }
    iterator end() { return body_.end(); }
    const_iterator begin() const { return body_.begin(); }
    const_iterator end() const { return body_.end(); }
    const value_type& front() const { return body_.front(); }

  protected:
    Container body_;
    [[no_unique_address]] KeyCompare comp_;

    // value-vs-value / key-vs-value 比较(异构)。value 走提取器,裸 key 原样过。
    template <typename A, typename B> bool less(const A& a, const B& b) const {
        GetKeyFromValue ext;
        return comp_(extract_key(ext, a), extract_key(ext, b));
    }
    template <typename Ext, typename V> static const auto& extract_key(Ext& ext, const V& v) {
        if constexpr (std::is_same_v<std::decay_t<V>, value_type>)
            return ext(v);
        else
            return v;
    }

    void sort_and_unique() {
        std::stable_sort(body_.begin(), body_.end(),
                         [this](const value_type& a, const value_type& b) { return less(a, b); });
        body_.erase(std::unique(body_.begin(), body_.end(),
                                [this](const value_type& a, const value_type& b) {
                                    return !less(a, b) && !less(b, a);
                                }),
                    body_.end());
    }
    bool is_sorted_unique() const {
        for (size_type i = 1; i < body_.size(); ++i)
            if (!less(body_[i - 1], body_[i]))
                return false;
        return true;
    }
};

} // namespace internal

// flat_map 的 key 提取器:pair.first
struct GetFirst {
    template <class K, class V> constexpr const K& operator()(const std::pair<K, V>& p) const {
        return p.first;
    }
};

template <class Key, class Mapped, class Compare = std::less<>,
          class Container = std::vector<std::pair<Key, Mapped>>>
class flat_map : public internal::flat_tree<Key, GetFirst, Compare, Container> {
    using base = internal::flat_tree<Key, GetFirst, Compare, Container>;

  public:
    using mapped_type = Mapped;
    using key_type = typename base::key_type;
    using value_type = typename base::value_type;
    using iterator = typename base::iterator;
    using const_iterator = typename base::const_iterator;

    using base::base; // 继承 flat_tree 的构造/查找/插入

    mapped_type& operator[](const Key& key) {
        auto it = this->lower_bound(key);
        if (it == this->end() || this->less(key, *it)) {
            it = this->insert(value_type{key, mapped_type{}}).first;
        }
        return it->second;
    }

    mapped_type& at(const Key& key) {
        auto it = this->find(key);
        assert(it != this->end() && "flat_map::at key out of range");
        return const_cast<mapped_type&>(it->second);
    }
    const mapped_type& at(const Key& key) const {
        auto it = this->find(key);
        assert(it != this->end() && "flat_map::at key out of range");
        return it->second;
    }

    template <class M> std::pair<iterator, bool> insert_or_assign(const Key& key, M&& obj) {
        auto it = this->lower_bound(key);
        if (it != this->end() && !this->less(key, *it)) {
            it->second = std::forward<M>(obj); // 覆写(依赖 pair<K,V> 非 const)
            return {it, false};
        }
        return this->insert(value_type{key, mapped_type(std::forward<M>(obj))});
    }

    template <class... Args> std::pair<iterator, bool> try_emplace(const Key& key, Args&&... args) {
        auto it = this->lower_bound(key);
        if (it != this->end() && !this->less(key, *it))
            return {it, false};
        return this->insert(value_type{std::piecewise_construct, std::forward_as_tuple(key),
                                       std::forward_as_tuple(std::forward<Args>(args)...)});
    }
};

// flat_set:flat_tree 别名,key=value,std::identity 提取
template <class Key, class Compare = std::less<>, class Container = std::vector<Key>>
using flat_set = internal::flat_tree<Key, std::identity, Compare, Container>;

} // namespace tamcpp::chrome
