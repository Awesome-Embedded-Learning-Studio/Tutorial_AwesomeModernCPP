/**
 * @file ex03_polymorphic_doc.cpp
 * @brief 练习：多态文档系统
 *
 * Document 抽象基类，含纯虚 print() 和虚析构函数。
 * TextDocument、ImageDocument、PdfDocument 三种派生。
 * 使用 vector<unique_ptr<Document>> 统一管理，遍历调用 print()。
 */

#include <iostream>
#include <memory>
#include <string>
#include <vector>

class Document {
private:
    std::string title_;

public:
    explicit Document(const std::string& title) : title_(title) {}
    virtual ~Document() = default;

    const std::string& title() const { return title_; }

    virtual void print() const = 0;
};

class TextDocument : public Document {
private:
    int line_count_;

public:
    TextDocument(const std::string& title, int lines)
        : Document(title), line_count_(lines) {}

    void print() const override
    {
        std::cout << "  [Text] \"" << title()
                  << "\" (" << line_count_ << " lines)\n";
    }
};

class ImageDocument : public Document {
private:
    int width_;
    int height_;

public:
    ImageDocument(const std::string& title, int w, int h)
        : Document(title), width_(w), height_(h) {}

    void print() const override
    {
        std::cout << "  [Image] \"" << title()
                  << "\" (" << width_ << "x" << height_ << ")\n";
    }
};

class PdfDocument : public Document {
private:
    int page_count_;

public:
    PdfDocument(const std::string& title, int pages)
        : Document(title), page_count_(pages) {}

    void print() const override
    {
        std::cout << "  [PDF] \"" << title()
                  << "\" (" << page_count_ << " pages)\n";
    }
};

// ============================================================
// main
// ============================================================
int main()
{
    std::cout << "===== 多态文档系统 =====\n\n";

    std::vector<std::unique_ptr<Document>> docs;
    docs.push_back(std::make_unique<TextDocument>(
        "README", 120));
    docs.push_back(std::make_unique<ImageDocument>(
        "screenshot", 1920, 1080));
    docs.push_back(std::make_unique<PdfDocument>(
        "report", 42));
    docs.push_back(std::make_unique<TextDocument>(
        "notes", 15));
    docs.push_back(std::make_unique<PdfDocument>(
        "thesis", 256));

    std::cout << "文档列表:\n";
    for (const auto& doc : docs) {
        doc->print();
    }

    std::cout << "\n共 " << docs.size() << " 个文档，全部正确多态分派。\n";

    return 0;
}
