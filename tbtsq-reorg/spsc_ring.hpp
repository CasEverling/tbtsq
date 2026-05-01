#pragma once
#include "base_queue.hpp"

#include <vector>
#include <atomic>
#include <immintrin.h>

template<typename T>
class SpscRing : public MyQueue<T> {             // BUG: missing <T>
private:
    struct alignas(64) Data { std::shared_ptr<T> data; };

    std::vector<Data> queue;

    alignas(64) size_t capacity;
    alignas(64) std::atomic<size_t> head;
    alignas(64) std::atomic<size_t> tail;

public:
    SpscRing(size_t size) : queue(size), capacity(size), head(0), tail(0) {}

    void enqueue(T&& val) final {
        // BUG: full condition was checking head == tail+1 which is the full check, but used
        // head for both load calls; should compare tail+1 against head (producer checks head)
        while ((tail.load(std::memory_order_relaxed) + 1) % capacity
                == head.load(std::memory_order_acquire)) {
            _mm_pause();                         // BUG: was `__mm_pause()` → `_mm_pause()`
        }
        auto old_tail = tail.load(std::memory_order_relaxed);
        queue[old_tail].data = std::make_shared<T>(std::forward<T>(val)); // BUG: foreward→forward; also must write to .data field
        auto new_tail = (old_tail + 1) % capacity;
        tail.store(new_tail, std::memory_order_release);  // BUG: was compare_exchange_strong on `head` with undefined `old_head`/`new_head` — SPSC enqueue just stores to tail
    }

    bool dequeue(std::shared_ptr<T>& retVal) override {
        if (head.load(std::memory_order_acquire)
                == tail.load(std::memory_order_acquire)) // BUG: was `memory_order_release` on tail load — should be acquire
            return false;

        auto old_head = head.load(std::memory_order_relaxed);
        retVal = queue[old_head].data;           // BUG: missing .data field access

        auto new_head = (old_head + 1) % capacity;
        head.store(new_head, std::memory_order_release); // BUG: was compare_exchange_strong — SPSC dequeue just stores to head
        return true;
    }
};
