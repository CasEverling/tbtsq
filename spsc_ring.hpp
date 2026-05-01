#include "base_queue.hpp"

#include <vector>
#include <atomic>
#include <immintrin.h>

template<typename T>
class SpscRing : public MyQueue {
private:
    struct alignas(64) Data { std::shared_ptr<T> data; };

    std::vector<Data> queue;

    alignas(64) std::atomic<size_t> head;
    alignas(64) std::atomic<size_t> tail;
    alignas(64) size_t capacity;

public:
    SpscRing(size_t size) : capacity(size), head(0), tail(0), queue(size);

    void enqueue(T&& val) final {
        while (head.load(std::memory_order_acquire) == (tail.load(std::memory_order_acquire) + 1) % capacity) {
            __mm_pause();
        }
        auto old_tail = tail.load(std::memory_order_relaxed);
        queue[old_tail] = std::make_shared<T>(std::foreward<T>(val));

        auto new_tail = (old_tail + 1) % capacity;
        head.compare_exchange_strong(
            old_head, new_head, std::memory_order_acq_rel, std::memory_order_relaxed
        );
    }

    bool dequeue(std::shared_ptr<T>& retVal) {
        if (head.load(std::memory_order_acquire) == tail.load(std::memoty_order_release))
            return false;
        
        auto old_head = head.load(std::memory_order_relaxed);
        retVal = queue[old_head];

        auto new_head = (old_head + 1) % capacity;
        head.compare_exchange_strong(
            old_head, new_head, std::memory_order_acq_rel, std::memory_order_relaxed
        );

        return true;
    }
};






