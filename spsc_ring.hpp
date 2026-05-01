#pragma once
#include "base_queue.hpp"

#include <vector>
#include <atomic>
#include <immintrin.h>

template<typename T>
class SpscRing : public MyQueue<T> {            
private:
    struct alignas(64) Data { std::shared_ptr<T> data; };

    std::vector<Data> queue;

    alignas(64) std::atomic<size_t> head;
    alignas(64) std::atomic<size_t> tail;
    alignas(64) size_t capacity;

public:
    SpscRing(size_t size) : capacity(size), head(0), tail(0), queue(size) {}  

    void enqueue(T&& val) final {
        while ((tail.load(std::memory_order_relaxed) + 1) % capacity
                == head.load(std::memory_order_acquire)) {
            _mm_pause();                    
        }
        auto old_tail = tail.load(std::memory_order_relaxed);
        queue[old_tail].data = std::make_shared<T>(std::forward<T>(val)); 
        auto new_tail = (old_tail + 1) % capacity;
        tail.store(new_tail, std::memory_order_release);  
    }

    bool dequeue(std::shared_ptr<T>& retVal) override {
        if (head.load(std::memory_order_acquire)
                == tail.load(std::memory_order_acquire)) 
            return false;

        auto old_head = head.load(std::memory_order_relaxed);
        retVal = queue[old_head].data;        

        auto new_head = (old_head + 1) % capacity;
        head.store(new_head, std::memory_order_release); 
        return true;
    }
};
