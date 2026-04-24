#include <vector>
#include <string>
#include <iostream>

struct ValidationError {
    std::string field;
    std::string message;
};

struct ValidationReport {
    std::vector<ValidationError> errors;

    void add(std::string field, std::string message) {
        errors.push_back({std::move(field), std::move(message)});
    }

    bool ok() const { return errors.empty(); }

    void print() const {
        for (const auto& e : errors) {
            std::cerr << "  - " << e.field << ": " << e.message << "\n";
        }
    }
};

void validate_form(const std::string& name,
                   const std::string& email,
                   int age,
                   ValidationReport& report) {
    if (name.empty()) report.add("name", "Name cannot be empty");
    if (name.size() > 100) report.add("name", "Name too long");

    if (email.find('@') == std::string::npos) {
        report.add("email", "Invalid email format");
    }

    if (age < 0 || age > 200) report.add("age", "Age out of range");
}

int main() {
    ValidationReport report;
    validate_form("", "invalid", -1, report);

    if (!report.ok()) {
        std::cerr << "Validation failed:\n";
        report.print();
    }
}
