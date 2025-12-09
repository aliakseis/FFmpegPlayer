// ordered_scoped_token.hpp
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <stdexcept>

class OrderedScopedTokenGenerator {
public:
    OrderedScopedTokenGenerator() : ctrl_(std::make_shared<Control>()) {}

    struct Token;
    struct LockScope;

    // generate a new movable-only token
    Token generate();

private:
    struct Control {
        std::mutex mtx;
        std::condition_variable cv;
        std::atomic<uint64_t> next_index{0};   // next index allowed to enter critical section
        std::atomic<uint64_t> alloc_index{0};  // next index to assign
    };

    std::shared_ptr<Control> ctrl_;

public:
    // Movable-only token representing a one-time ordered gate
    struct Token {
        Token() = default;

        // non-copyable
        Token(const Token&) = delete;
        Token& operator=(const Token&) = delete;

        // movable
        Token(Token&&) noexcept = default;
        Token& operator=(Token&&) noexcept = default;

        ~Token() {
            // If token was never claimed (locked or consumed), consume it now so it doesn't block later tokens.
            // This destructor may block until it's this token's turn, then advance the counter.
            if (!state_) return;
            bool expected = false;
            if (!state_->claimed.compare_exchange_strong(expected, true)) {
                // already claimed/locked; nothing to do
                return;
            }

            // Wait until it's our turn, then advance to next and notify.
            std::unique_lock<std::mutex> lk(state_->ctrl->mtx);
            state_->ctrl->cv.wait(lk, [&]{ return state_->ctrl->next_index.load() == state_->index; });
            state_->ctrl->next_index.fetch_add(1);
            lk.unlock();
            state_->ctrl->cv.notify_all();
        }

        // Acquire the token and enter the critical section.
        // Returns a movable LockScope that holds the critical section until destroyed.
        // Throws std::runtime_error if token is invalid or already claimed.
        LockScope lock();

        // Try to acquire immediately. If successful returns LockScope; otherwise returns empty optional.
        // If token already claimed, returns empty.
        std::optional<LockScope> try_lock();

        // Query whether token has been claimed (locked or consumed)
        bool is_claimed() const {
            return state_ && state_->claimed.load();
        }

        explicit operator bool() const noexcept { return static_cast<bool>(state_); }

    private:
        friend class OrderedScopedTokenGenerator;
        struct State {
            std::shared_ptr<Control> ctrl;
            uint64_t index;
            std::atomic<bool> claimed{false}; // ensures token is used only once
        };

        explicit Token(std::shared_ptr<State> s) : state_(std::move(s)) {}
        std::shared_ptr<State> state_;
    };

    // RAII scope returned by Token::lock(); movable-only.
    struct LockScope {
        LockScope() = default;

        // non-copyable
        LockScope(const LockScope&) = delete;
        LockScope& operator=(const LockScope&) = delete;

        // movable
        LockScope(LockScope&& other) noexcept : state_(std::exchange(other.state_, {})), owns_(other.owns_) {
            other.owns_ = false;
        }
        LockScope& operator=(LockScope&& other) noexcept {
            if (this != &other) {
                // release current if owning
                release_if_needed();
                state_ = std::exchange(other.state_, {});
                owns_ = other.owns_;
                other.owns_ = false;
            }
            return *this;
        }

        ~LockScope() {
            release_if_needed();
        }

        // explicit release before destruction (optional)
        void release() {
            release_if_needed();
        }

        bool valid() const noexcept { return state_ && owns_; }

    private:
        friend struct Token;
        explicit LockScope(std::shared_ptr<Token::State> s) : state_(std::move(s)), owns_(true) {}

        void release_if_needed() {
            if (!state_ || !owns_) return;
            // advance next_index and notify
            std::lock_guard<std::mutex> lk(state_->ctrl->mtx);
            state_->ctrl->next_index.fetch_add(1);
            owns_ = false;
            state_->ctrl->cv.notify_all();
        }

        std::shared_ptr<Token::State> state_;
        bool owns_{false};
    };
};

// Implementation of generate, Token::lock, Token::try_lock

inline OrderedScopedTokenGenerator::Token OrderedScopedTokenGenerator::generate() {
    uint64_t idx = ctrl_->alloc_index.fetch_add(1);
    auto s = std::make_shared<Token::State>();
    s->ctrl = ctrl_;
    s->index = idx;
    s->claimed.store(false);
    return Token(s);
}

inline OrderedScopedTokenGenerator::LockScope OrderedScopedTokenGenerator::Token::lock() {
    if (!state_) throw std::runtime_error("Invalid token");
    // try to claim; only first caller may proceed
    bool expected = false;
    if (!state_->claimed.compare_exchange_strong(expected, true)) {
        throw std::runtime_error("Token already claimed");
    }

    // wait until it's our turn
    std::unique_lock<std::mutex> lk(state_->ctrl->mtx);
    state_->ctrl->cv.wait(lk, [&]{ return state_->ctrl->next_index.load() == state_->index; });

    // At this point we hold the right to enter the critical section.
    // Do NOT advance next_index here; LockScope destructor will advance when scope ends.
    return LockScope(state_);
}

inline std::optional<OrderedScopedTokenGenerator::LockScope> OrderedScopedTokenGenerator::Token::try_lock() {
    if (!state_) return std::nullopt;
    bool expected = false;
    if (!state_->claimed.compare_exchange_strong(expected, true)) {
        return std::nullopt; // already claimed
    }
    // check if it's our turn now
    uint64_t cur = state_->ctrl->next_index.load();
    if (cur != state_->index) {
        // Not our turn; we keep claimed=true to respect one-time semantics,
        // but try_lock fails to enter the critical section immediately.
        return std::nullopt;
    }
    // It's our turn; return LockScope (do not advance here)
    return LockScope(state_);
}
