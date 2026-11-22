// stop_source 与 stop_token 的状态查询:两次 request_stop 只有一次为真
#include <print>
#include <stop_token>

int main() {
    std::stop_source source;
    std::stop_token token = source.get_token();

    std::println("before: source.stop_possible()={} token.stop_possible()={}",
                 source.stop_possible(), token.stop_possible());
    std::println("before: source.stop_requested()={} token.stop_requested()={}",
                 source.stop_requested(), token.stop_requested());
    std::println("request_stop() #1 -> {}", source.request_stop());
    std::println("request_stop() #2 -> {}", source.request_stop());
    std::println("after:  token.stop_requested()={}", token.stop_requested());

    std::stop_token empty;
    std::println("default token: stop_possible()={} stop_requested()={}", empty.stop_possible(),
                 empty.stop_requested());

    std::stop_source silent(std::nostopstate);
    std::println("nostopstate source: stop_possible()={} request_stop() -> {}",
                 silent.stop_possible(), silent.request_stop());
}
