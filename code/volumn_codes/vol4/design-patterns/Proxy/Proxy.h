#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>

// ============================================================================
// 第一步:属性代理 —— 把字段读写变成可拦截的操作
// 对应文章「第一步」,Property<T> 模板,默认走直接存值,可注入自定义 getter/setter
// ============================================================================
template <typename T> class Property {
  public:
    using Getter = std::function<T()>;
    using Setter = std::function<void(const T&)>;

    // 默认走「直接存值」的实现
    Property() : getter_([this] { return value_; }), setter_([this](const T& v) { value_ = v; }) {}

    explicit Property(const T& v) : Property() { value_ = v; }

    // 注入自定义的 getter/setter —— 校验、通知、日志都挂在这里
    Property(Getter g, Setter s) : getter_(std::move(g)), setter_(std::move(s)) {}

    operator T() const { return getter_(); } // 读:走 getter
    Property& operator=(const T& v) {        // 写:走 setter
        setter_(v);
        return *this;
    }

    const T& raw() const { return value_; } // 供演示观察内部值

  private:
    mutable T value_{}; // mutable:getter_ 在 const 方法里也能改它
    Getter getter_;
    Setter setter_;
};

// ============================================================================
// 第二步:虚拟代理 —— 把昂贵的创建推迟到最后一刻
// 对应文章「第二步」,Image / RealImage / ImageProxy
// 并发坑用 std::call_once 修掉(文章「踩坑预警」给出的正解)
// ============================================================================
struct Image {
    virtual void display() = 0;
    virtual ~Image() = default;
};

// 真实图片:构造时就要从磁盘加载,很慢
class RealImage : public Image {
  public:
    explicit RealImage(const std::string& f) : filename_(f) {
        load_from_disk();
        ++load_count;
    }
    void display() override { std::cout << "Displaying " << filename_ << "\n"; }

    static std::atomic<int> load_count;

  private:
    void load_from_disk() { std::cout << "Loading " << filename_ << " from disk (expensive)\n"; }
    std::string filename_;
};
inline std::atomic<int> RealImage::load_count{0};

// 虚拟代理:第一次 display() 才构造真实对象;并发安全用 call_once
class ImageProxy : public Image {
  public:
    explicit ImageProxy(const std::string& f) : filename_(f) {}
    void display() override {
        std::call_once(flag_, [this] { real_ = std::make_unique<RealImage>(filename_); });
        real_->display();
    }

  private:
    std::string filename_;
    std::unique_ptr<RealImage> real_;
    std::once_flag flag_;
};

// ============================================================================
// 第三步:保护代理 —— 把「谁能做什么」从业务里剥出来
// 对应文章「第三步」,Sensitive / ProtectionProxy
// ============================================================================
class Sensitive {
  public:
    void secret() { std::cout << "secret data\n"; }
};

class ProtectionProxy {
  public:
    ProtectionProxy(Sensitive* s, bool allowed) : s_(s), allowed_(allowed) {}
    void secret() {
        if (!allowed_)
            throw std::runtime_error("access denied");
        s_->secret();
    }

  private:
    Sensitive* s_;
    bool allowed_;
};

// ============================================================================
// 第四步:远程代理 —— 把网络细节藏在本地
// 对应文章「第四步」,Transport / RemoteService / RemoteServiceProxy
// ============================================================================
// 模拟一个传输层(真实场景是 socket / HTTP / gRPC)
class Transport {
  public:
    std::string send_request(const std::string& req) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50)); // 模拟网络延迟
        if (req == "get_time")
            return "2025-09-29T12:00:00Z";
        if (req.rfind("compute:", 0) == 0)
            return "result:" + req.substr(8);
        throw std::runtime_error("unknown request");
    }
};

// 远程服务的本地抽象
struct RemoteService {
    virtual std::string get_time() = 0;
    virtual int remote_compute(int x, int y) = 0;
    virtual ~RemoteService() = default;
};

// 远程代理:把方法调用翻译成 transport 请求
class RemoteServiceProxy : public RemoteService {
  public:
    explicit RemoteServiceProxy(Transport* t) : transport_(t) {}

    std::string get_time() override { return transport_->send_request("get_time"); }

    int remote_compute(int x, int y) override {
        std::string req = "compute:" + std::to_string(x) + "," + std::to_string(y);
        transport_->send_request(req); // 真实场景会解析 "result:..."
        return x + y;
    }

  private:
    Transport* transport_;
};

// ============================================================================
// 第五步:缓存代理 —— 把重复计算的结果存起来
// 对应文章「第五步」,Expensive / CachingProxy
// ============================================================================
class Expensive {
  public:
    virtual int compute(int x) = 0;
    virtual ~Expensive() = default;
};

class CachingProxy : public Expensive {
  public:
    explicit CachingProxy(Expensive* r) : real_(r) {}

    int compute(int x) override {
        if (auto it = cache_.find(x); it != cache_.end()) {
            return it->second; // 命中缓存,直接返回
        }
        int result = real_->compute(x); // 没命中,才算
        cache_[x] = result;
        return result;
    }

  private:
    Expensive* real_;
    std::unordered_map<int, int> cache_;
};

// ============================================================================
// 第六步:同步代理 —— 给非线程安全对象套一把锁
// 对应文章「第六步」,SomeInterface / SyncProxy
// ============================================================================
class SomeInterface {
  public:
    virtual void op() = 0;
    virtual ~SomeInterface() = default;
};

class SyncProxy : public SomeInterface {
  public:
    explicit SyncProxy(SomeInterface* r) : real_(r) {}
    void op() override {
        std::lock_guard<std::mutex> lk(mtx_); // 进方法先锁
        real_->op();                          // 真正干活
    } // 离开自动解锁

  private:
    SomeInterface* real_;
    std::mutex mtx_;
};

// ============================================================================
// 第七步:写时复制(COW)—— 共享读、写时复制
// 对应文章「第七步」,CowString,用 use_count() 判独占(老黄历 unique() 已弃用)
// ============================================================================
class CowString {
  public:
    CowString() : data_(std::make_shared<std::string>()) {}
    CowString(const std::string& s) : data_(std::make_shared<std::string>(s)) {}

    // 读:直接返回 const 引用,多个 CowString 共享同一份
    const std::string& str() const { return *data_; }

    // 写:先确保独占(必要时复制),再改
    void append(const std::string& s) {
        ensure_unique();
        data_->append(s);
    }

  private:
    void ensure_unique() {
        if (data_.use_count() > 1) {                       // 不独占
            data_ = std::make_shared<std::string>(*data_); // 复制一份
        }
    }
    std::shared_ptr<std::string> data_;
};
