// ordered_scoped_token.h
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <unordered_set>

class OrderedScopedTokenGenerator {
public:
    OrderedScopedTokenGenerator() : ctrl_(std::make_shared<Control>()) {}

    struct LockScope;

private:
    struct Control {
        std::mutex mtx;
        std::condition_variable cv;
        uint64_t next_index{ 0 };   // next index allowed to enter critical section
        uint64_t alloc_index{ 0 };  // next index to assign
        // indices that have been consumed (either released or discarded) but are > next_index
        std::unordered_set<uint64_t> consumed;
    };

    std::shared_ptr<Control> ctrl_;

    // helper: mark index consumed and advance next_index while possible
    static void consume_and_advance(const std::shared_ptr<Control>& ctrl, uint64_t index) {
        std::lock_guard<std::mutex> lk(ctrl->mtx);
        if (index < ctrl->next_index) {
            // already passed
            return;
        }
        if (index == ctrl->next_index) {
            ++ctrl->next_index;
            // advance while contiguous indices are present in consumed set
            while (true) {
                auto it = ctrl->consumed.find(ctrl->next_index);
                if (it == ctrl->consumed.end()) break;
                ctrl->consumed.erase(it);
                ++ctrl->next_index;
            }
        }
        else {
            // index > next_index: remember it for later coalescing
            ctrl->consumed.insert(index);
        }
        ctrl->cv.notify_all();
    }

public:
    // Movable-only token representing a one-time ordered gate
    struct Token {
        Token() = default;

        // non-copyable
        Token(const Token&) = delete;
        Token& operator=(const Token&) = delete;

        // movable
        Token(Token&& other) noexcept = default;
        Token& operator=(Token&& other) noexcept = default;

        // Acquire the token and enter the critical section.
        // Returns a movable LockScope that holds the critical section until destroyed.
        // Throws std::runtime_error if token is invalid or already claimed.
        LockScope lock() {
            if (!state_) 
                throw std::runtime_error("Invalid token");

            std::unique_lock<std::mutex> lk(state_->ctrl->mtx);
            state_->ctrl->cv.wait(lk, [&] { return state_->ctrl->next_index == state_->index; });
            // Do NOT advance next_index here; LockScope destructor will advance when scope ends.
            return LockScope(std::move(state_));
        }

        // Try to acquire immediately. If successful returns LockScope; otherwise returns empty optional.
        // If token already claimed, returns empty.
        LockScope try_lock()
        {
            if (!state_)
                throw std::runtime_error("Invalid token");

            std::lock_guard<std::mutex> lk(state_->ctrl->mtx);

            if (state_->ctrl->next_index != state_->index)
                return {};

            return LockScope(std::move(state_));
        }

        // Query whether token has been claimed (locked or consumed)
        bool is_claimed() const {
            return !state_;
        }

        explicit operator bool() const noexcept { return static_cast<bool>(state_); }

    private:
        friend class OrderedScopedTokenGenerator;
        struct State {
            std::shared_ptr<Control> ctrl;
            uint64_t index;
            ~State() {
                consume_and_advance(ctrl, index);
            }
        };

        explicit Token(std::unique_ptr<State> s) : state_(std::move(s)) {}
        std::unique_ptr<State> state_;
    };

    // RAII scope returned by Token::lock(); movable-only.
    struct LockScope {

        // non-copyable
        LockScope(const LockScope&) = delete;
        LockScope& operator=(const LockScope&) = delete;

        // movable
        LockScope(LockScope&& other) noexcept = default;
        LockScope& operator=(LockScope&& other) noexcept = default;

        ~LockScope() {
            release_if_needed();
        }

        // explicit release before destruction (optional)
        void release() {
            release_if_needed();
        }

        bool valid() const noexcept { return static_cast<bool>(state_); }

    private:
        friend struct Token;
        LockScope() = default;
        explicit LockScope(std::unique_ptr<Token::State> s) : state_(std::move(s)) {}

        void release_if_needed() {
            state_.reset();
        }

        std::unique_ptr<Token::State> state_;
    };

    // generate a new movable-only token
    Token generate() {
        uint64_t idx;
        {
            // allocate index under mutex to avoid ABA with consumed set (not strictly necessary but simpler)
            std::lock_guard<std::mutex> lk(ctrl_->mtx);
            idx = ctrl_->alloc_index++;
        }
        auto s = std::make_unique<Token::State>();
        s->ctrl = ctrl_;
        s->index = idx;
        return Token(std::move(s));
    }
};
