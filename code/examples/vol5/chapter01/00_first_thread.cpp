#include <print>
#include <thread>

void finish_download() {
    std::println("Download finished");
}

int main() {
    std::thread worker(finish_download);
    std::println("List scrolled");
    worker.join();
    std::println("Showing the completion message");
}
