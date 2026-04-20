/**
 * @file ex12_query_builder.cpp
 * @brief 练习：QueryBuilder 链式调用
 *
 * 实现简易 SQL 查询构建器，通过链式方法调用拼接 SQL 语句：
 * select("cols").from("table").where("cond").build()
 * 返回 "SELECT cols FROM table WHERE cond"
 */

#include <iostream>
#include <string>

class QueryBuilder {
private:
    std::string select_clause_;
    std::string from_clause_;
    std::string where_clause_;

public:
    QueryBuilder() = default;

    // 设置 SELECT 子句
    QueryBuilder& select(const std::string& columns) {
        select_clause_ = columns;
        return *this;
    }

    // 设置 FROM 子句
    QueryBuilder& from(const std::string& table) {
        from_clause_ = table;
        return *this;
    }

    // 设置 WHERE 子句
    QueryBuilder& where(const std::string& condition) {
        where_clause_ = condition;
        return *this;
    }

    // 构建最终 SQL 语句
    std::string build() const {
        std::string sql = "SELECT " + select_clause_;
        if (!from_clause_.empty()) {
            sql += " FROM " + from_clause_;
        }
        if (!where_clause_.empty()) {
            sql += " WHERE " + where_clause_;
        }
        return sql;
    }

    // 重置构建器状态
    QueryBuilder& reset() {
        select_clause_.clear();
        from_clause_.clear();
        where_clause_.clear();
        return *this;
    }
};

int main() {
    std::cout << "===== QueryBuilder 链式调用 =====\n\n";

    // 基本用法
    QueryBuilder builder;
    std::string sql1 = builder.select("id, name, score")
                               .from("students")
                               .where("score > 90")
                               .build();
    std::cout << "查询 1:\n  " << sql1 << "\n\n";

    // 无 WHERE 条件
    builder.reset();
    std::string sql2 = builder.select("*")
                               .from("products")
                               .build();
    std::cout << "查询 2 (无条件):\n  " << sql2 << "\n\n";

    // 复杂条件
    builder.reset();
    std::string sql3 = builder.select("name, email")
                               .from("users")
                               .where("age >= 18 AND active = 1")
                               .build();
    std::cout << "查询 3 (复杂条件):\n  " << sql3 << "\n\n";

    // 单次构建
    std::string sql4 = QueryBuilder()
                           .select("count(*)")
                           .from("orders")
                           .where("status = 'pending'")
                           .build();
    std::cout << "查询 4 (临时对象):\n  " << sql4 << "\n\n";

    // 验证基本用例
    builder.reset();
    std::string expected = "SELECT cols FROM table WHERE cond";
    std::string actual = builder.select("cols")
                                .from("table")
                                .where("cond")
                                .build();
    bool ok = (actual == expected);
    std::cout << "验证:\n";
    std::cout << "  期望: \"" << expected << "\"\n";
    std::cout << "  实际: \"" << actual << "\"\n";
    std::cout << "  结果: " << (ok ? "通过" : "失败") << "\n\n";

    std::cout << "要点:\n";
    std::cout << "  链式方法返回 *this 引用\n";
    std::cout << "  build() 在最后一步完成拼接\n";
    std::cout << "  Builder 模式将复杂构造过程分步完成\n";

    return 0;
}
