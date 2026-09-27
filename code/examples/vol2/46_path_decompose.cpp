#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

void decompose_path(const fs::path& p) {
    std::cout << "原始路径:     " << p << "\n";
    std::cout << "root_name:    " << p.root_name() << "\n";
    std::cout << "root_dir:     " << p.root_directory() << "\n";
    std::cout << "root_path:    " << p.root_path() << "\n";
    std::cout << "relative_path:" << p.relative_path() << "\n";
    std::cout << "parent_path:  " << p.parent_path() << "\n";
    std::cout << "filename:     " << p.filename() << "\n";
    std::cout << "stem:         " << p.stem() << "\n";
    std::cout << "extension:    " << p.extension() << "\n";
    std::cout << "------\n";
}

int main() {
    decompose_path("/usr/local/bin/gcc");
    decompose_path("/home/user/report.pdf");
    decompose_path("config.ini");
    decompose_path("/tmp/archive.tar.gz");
    return 0;
}
