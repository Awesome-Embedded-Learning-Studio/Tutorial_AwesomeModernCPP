/**
 * @file ex04_student_db.cpp
 * @brief 练习：学生成绩数据库
 *
 * 使用 map<string,int> 管理学生成绩。
 * 实现 add_student、get_score（使用 find 而非 []）、list_all、remove_student。
 */

#include <iostream>
#include <map>
#include <string>

/// @brief 添加学生成绩，若已存在则更新
/// @param db  学生数据库
/// @param name  学生姓名
/// @param score  成绩
void add_student(std::map<std::string, int>& db,
                 const std::string& name, int score)
{
    db[name] = score;
    std::cout << "  添加/更新: " << name << " -> " << score << "\n";
}

/// @brief 查询学生成绩（使用 find，不使用 []）
/// @param db  学生数据库
/// @param name  学生姓名
void get_score(const std::map<std::string, int>& db,
               const std::string& name)
{
    auto it = db.find(name);
    if (it != db.end()) {
        std::cout << "  查询: " << it->first << " 的成绩是 "
                  << it->second << "\n";
    } else {
        std::cout << "  查询: 未找到学生 \"" << name << "\"\n";
    }
}

/// @brief 列出所有学生及成绩
/// @param db  学生数据库
void list_all(const std::map<std::string, int>& db)
{
    if (db.empty()) {
        std::cout << "  （数据库为空）\n";
        return;
    }

    std::cout << "  共 " << db.size() << " 名学生:\n";
    for (const auto& [name, score] : db) {
        std::cout << "    " << name << " -> " << score << "\n";
    }
}

/// @brief 删除学生
/// @param db  学生数据库
/// @param name  学生姓名
void remove_student(std::map<std::string, int>& db,
                    const std::string& name)
{
    auto count = db.erase(name);
    if (count > 0) {
        std::cout << "  删除: 已移除学生 \"" << name << "\"\n";
    } else {
        std::cout << "  删除: 未找到学生 \"" << name << "\"\n";
    }
}

int main()
{
    std::cout << "===== 学生成绩数据库 =====\n\n";

    std::map<std::string, int> db;

    // 添加学生
    std::cout << "--- 添加学生 ---\n";
    add_student(db, "Alice", 92);
    add_student(db, "Bob", 85);
    add_student(db, "Charlie", 78);
    add_student(db, "Diana", 95);
    add_student(db, "Eve", 88);
    std::cout << "\n";

    // 列出所有
    std::cout << "--- 列出所有 ---\n";
    list_all(db);
    std::cout << "\n";

    // 查询（存在的和不存在的）
    std::cout << "--- 查询成绩 ---\n";
    get_score(db, "Alice");
    get_score(db, "Frank");
    std::cout << "\n";

    // 更新成绩
    std::cout << "--- 更新成绩 ---\n";
    add_student(db, "Bob", 90);
    get_score(db, "Bob");
    std::cout << "\n";

    // 删除学生
    std::cout << "--- 删除学生 ---\n";
    remove_student(db, "Charlie");
    remove_student(db, "Ghost");
    std::cout << "\n";

    // 再次列出
    std::cout << "--- 删除后的数据库 ---\n";
    list_all(db);
    std::cout << "\n";

    std::cout << "要点:\n";
    std::cout << "  1. 使用 find() 而非 [] 查询，避免意外插入\n";
    std::cout << "  2. map 按键的字典序自动排序\n";
    std::cout << "  3. C++17 结构化绑定简化遍历写法\n";

    return 0;
}
