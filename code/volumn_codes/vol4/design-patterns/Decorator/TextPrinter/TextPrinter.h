#pragma once
#include <memory>
#include <print>
#include <string>

struct AbsTextPrinter {
  public:
    virtual ~AbsTextPrinter() = default;
    /**
     * @brief process only base IO Task
     *
     * @param text what to display
     */
    virtual void simple_print(const std::string& text) = 0;
};

struct PlainTextPrinter : AbsTextPrinter {
  public:
    virtual ~PlainTextPrinter() = default;
    /**
     * @brief process only base IO Task
     *
     * @param text what to display
     */
    virtual void simple_print(const std::string& text) override { std::print("{}", text); }
};

struct BaseDecorator : public AbsTextPrinter {
  protected:
    std::shared_ptr<AbsTextPrinter> printer_raw;

  public:
    explicit BaseDecorator(std::shared_ptr<AbsTextPrinter> ptr) : printer_raw(std::move(ptr)) {};
    virtual ~BaseDecorator() = default;

    virtual void simple_print(const std::string& text) = 0;
};

class QuoteDecorator : public BaseDecorator {
  public:
    explicit QuoteDecorator(std::shared_ptr<AbsTextPrinter> ptr) : BaseDecorator(ptr) {};
    void simple_print(const std::string& text) override {
        printer_raw->simple_print("\"" + text + "\"");
    }
};

class StarDecorator : public BaseDecorator {
  public:
    explicit StarDecorator(std::shared_ptr<AbsTextPrinter> ptr) : BaseDecorator(ptr) {};
    void simple_print(const std::string& text) override {
        printer_raw->simple_print("***" + text + "***");
    }
};
class UpperCaseDecorator : public BaseDecorator {
  public:
    explicit UpperCaseDecorator(std::shared_ptr<AbsTextPrinter> ptr) : BaseDecorator(ptr) {};
    void simple_print(const std::string& text) override {
        std::string result = text;
        std::transform(text.begin(), text.end(), result.begin(),
                       [](unsigned char ch) { return std::toupper(ch); });
        printer_raw->simple_print(text);
    }
};
