#include <cstdio>
#include <utility>

class RecordingBuffer {
public:
    explicit RecordingBuffer(int n)
        : data_(new int[n]{}) {}

    ~RecordingBuffer() { delete[] data_; }

    int* data() { return data_; }

private:
    int* data_;
};

int main() {
    RecordingBuffer a(1024);
    RecordingBuffer b = std::move(a);
    std::printf("%d\n", b.data()[0]);
}
