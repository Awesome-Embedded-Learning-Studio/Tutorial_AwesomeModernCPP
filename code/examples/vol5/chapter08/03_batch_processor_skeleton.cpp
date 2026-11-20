// batch_processor_skeleton.cpp —— 接口骨架：形状参考，不是规定。实现从 8.2 起由您自己补
//
// 骨架原文是 batch_processor.hpp 加一个只包含头文件的翻译单元（tu_skeleton.cpp）：
//   g++ -std=c++20 -pthread -c tu_skeleton.cpp -o tu_skeleton.o
// 这里把两份合进一个 cpp，编译验证过的形状原样保留，main 只做 return 0。
#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

// 一项任务的输入与结果，普通数据，处理程序不问业务含义
struct Job {
  int input{};          // 交给任务的编号
  long long output{};   // 任务算完写回的结果
};

// 一条失败记录：哪一项、抛了什么消息
struct Failure {
  int input{};
  std::string reason{}; // 异常消息的拷贝，异常对象没了消息还在
};

class BatchProcessor {
public:
  explicit BatchProcessor(unsigned worker_count);  // 工作线程数量由调用方定
  ~BatchProcessor();

  BatchProcessor(const BatchProcessor&) = delete;
  BatchProcessor& operator=(const BatchProcessor&) = delete;

  void submit(Job job);           // 提交一项任务，排队等工作线程领走
  void request_stop() noexcept;   // 提出停止：还没开跑的不再做，在跑的跑完
  void wait();                    // 等全部收场：线程退出，计数站得住

  long long total() const;                // 成功结果的累加值
  std::size_t success_count() const;      // 成功的任务数
  std::size_t failed_count() const;       // 失败的任务数
  std::size_t skipped_count() const;      // 停止后没轮到做的任务数
  std::vector<Failure> failures() const;  // 失败清单，能对上号
};

// 骨架只验证编译，成员函数未实现，也没有代码引用它们。
// 这里用一个不取地址、不调用的标记，让符号不被链接器索要。
int main() {
  std::printf("skeleton compiles\n");
  return 0;
}
