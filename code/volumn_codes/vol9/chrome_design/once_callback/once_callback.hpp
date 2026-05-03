/**
 * @file once_callback.hpp
 * @author Charliechen114514 (chengh1922@mails.jlu.edu.cn)
 * @brief Once Callback single file
 * @version 0.1
 * @date 2026-05-02
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once
#include "cancel_token/cancel_token.hpp"
#include <functional>

namespace tamcpp::chrome {

/*
 *  OK, we request the newest gcc to finish the issue
 *  update your gcc to try new feature!
 */
static_assert(__cpp_lib_move_only_function >= 202110L);

/**
 * @brief
 *
 * @tparam filled here the function, promise to callback once
 */
template <typename FuncSignature> class OnceCallback;

template <typename F, typename T>
concept not_the_same_t = !std::is_same_v<std::decay_t<F>, T>;

template <typename ReturnType, typename... FuncArgs>
class OnceCallback<ReturnType(FuncArgs...)> // Specialization of Functional like
{
  public:
    using FuncSig = ReturnType(FuncArgs...);

  private:
    enum class Status {
        kEmpty,   // Null when construction with no lambda or func specified
        kValid,   // validate for usage
        kConsumed // Has been callbacked
    } status_ = Status::kEmpty;

    std::move_only_function<FuncSig> func_;
    std::shared_ptr<CancelableToken> token_;

    /**
     * @brief Disabled the copy
     *
     */
    OnceCallback(const OnceCallback&) = delete;
    OnceCallback& operator=(const OnceCallback&) = delete;

  public:
    template <typename Functor>
        requires not_the_same_t<Functor, OnceCallback>
    explicit OnceCallback(Functor&& function)
        : status_(Status::kValid), func_(std::move(function)) {}

    explicit OnceCallback() = default;

    OnceCallback(OnceCallback&& other) noexcept
        : status_(other.status_), func_(std::move(other.func_)), token_(std::move(other.token_)) {
        other.status_ = Status::kEmpty;
    }
    OnceCallback& operator=(OnceCallback&& other) noexcept {
        if (this != &other) {
            status_ = other.status_;
            func_ = std::move(other.func_);
            token_ = std::move(other.token_);
            other.status_ = Status::kEmpty;
        }
        return *this;
    }

    void set_token(std::shared_ptr<CancelableToken> token) { token_ = std::move(token); }

    template <typename Next> auto then(Next&& next) &&;

    /**
     * @brief
     *
     * @tparam deducing this features, no need to be filled
     * @param self
     * @param args
     * @return ReturnType
     */
    template <typename Self> auto run(this Self&& self, FuncArgs&&... args) -> ReturnType;

    /**
     * @brief Check Issue
     *
     * @return true
     * @return false
     */
    [[nodiscard]] bool is_cancelled() const noexcept {
        if (status_ != Status::kValid) return true;
        if (token_ && !token_->is_valid()) return true;
        return false;
    }
    [[nodiscard]] bool maybe_valid() const noexcept { return !is_cancelled(); }
    [[nodiscard]] bool is_null() const noexcept { return status_ == Status::kEmpty; }
    explicit operator bool() const noexcept { return !is_null() && !is_cancelled(); }

  private:
    ReturnType impl_run(FuncArgs... args);
};

template <typename Signature, typename F, typename... BoundArgs>
auto bind_once(F&& funtor, BoundArgs&&... args);

} // namespace tamcpp::chrome

#include "once_callback_impl.hpp"
