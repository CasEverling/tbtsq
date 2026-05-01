#pragma once
#include "base_queue.hpp"
#include <atomic>
#include <memory>

template<typename T>
class MSQueue : public MyQueue<T> {
private:
    struct Node;

    // alignas(16) required for lock-free __int128 CAS (cmpxchg16b) on x86-64.
    // Must NOT be alignas(64) — that triggers __atomic_load_16 ABI issues.
    struct alignas(16) Pointer {
        Node*  ptr;
        size_t counter;

        bool operator==(const Pointer& o) const noexcept {
            return ptr == o.ptr && counter == o.counter;
        }
    };

    struct alignas(64) Node {
        std::shared_ptr<T>   data;
        std::atomic<Pointer> next;

        Node(std::shared_ptr<T> d, Pointer n) : data(std::move(d)), next(n) {}
    };

    alignas(64) std::atomic<Pointer> head;
    alignas(64) std::atomic<Pointer> tail;

public:
    MSQueue() {
        auto dummy = new Node{nullptr, Pointer{nullptr, 0}};
        Pointer p{dummy, 0};
        head.store(p, std::memory_order_relaxed);
        tail.store(p, std::memory_order_relaxed);
    }

    MSQueue(const MSQueue&)            = delete;
    MSQueue(MSQueue&&)                 = delete;
    MSQueue& operator=(const MSQueue&) = delete;
    ~MSQueue()                         = default;

    void enqueue(T&& val) override {
        auto node = new Node{
            std::make_shared<T>(std::forward<T>(val)),
            Pointer{nullptr, 0}
        };

        while (true) {
            auto curr_tail = tail.load(std::memory_order_acquire);
            auto next      = curr_tail.ptr->next.load(std::memory_order_acquire);

            if (curr_tail == tail.load(std::memory_order_acquire)) {
                if (next.ptr == nullptr) {
                    Pointer new_next{node, next.counter + 1};
                    if (curr_tail.ptr->next.compare_exchange_weak(
                            next, new_next,
                            std::memory_order_acq_rel,
                            std::memory_order_relaxed))
                        break;
                } else {
                    Pointer new_tail{next.ptr, curr_tail.counter + 1};
                    tail.compare_exchange_weak(
                        curr_tail, new_tail,
                        std::memory_order_acq_rel,
                        std::memory_order_relaxed);
                }
            }
        }

        auto curr_tail = tail.load(std::memory_order_relaxed);
        Pointer new_tail{node, curr_tail.counter + 1};
        tail.compare_exchange_strong(
            curr_tail, new_tail,
            std::memory_order_acq_rel,
            std::memory_order_relaxed);
    }

    bool dequeue(std::shared_ptr<T>& retVal) override {
        while (true) {
            auto head_ptr = head.load(std::memory_order_acquire);
            auto tail_ptr = tail.load(std::memory_order_acquire);
            auto next_ptr = head_ptr.ptr->next.load(std::memory_order_acquire);

            if (head_ptr == head.load(std::memory_order_acquire)) {
                if (head_ptr.ptr == tail_ptr.ptr) {
                    if (next_ptr.ptr == nullptr) return false;
                    Pointer new_tail{next_ptr.ptr, tail_ptr.counter + 1};
                    tail.compare_exchange_weak(
                        tail_ptr, new_tail,
                        std::memory_order_acq_rel,
                        std::memory_order_relaxed);
                } else {
                    retVal = next_ptr.ptr->data;
                    Pointer new_head{next_ptr.ptr, head_ptr.counter + 1};
                    if (head.compare_exchange_weak(
                            head_ptr, new_head,
                            std::memory_order_acq_rel,
                            std::memory_order_relaxed))
                        break;
                }
            }
        }
        return true;
    }
};
