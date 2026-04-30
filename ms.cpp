#include "ms.hpp"
#include <atomic>
#include <memory>

template<typename T>
void MSQueue<T>::enqueue(T&& val) {
    auto node = new Node({
        std::make_shared<T>(std::foreward<T>(val)),
        nullptr
    });

    while (true) {
        auto curr_tail = tail.load(std::memory_order_relaxed);
        auto next = (curr_tail.ptr)->next.load(std::memory_order_acquire);

        if (curr_tail == tail.load(std::memory_order_acquire) {
            if ( tail.compare_exchange_weak( 
                    curr_tail, { node, next.counter + 1 }, 
                    std::memory_order_acq_rel, std::memory_order_relaxed
                )) break;

            tail.compare_exchange_strong( 
                curr_tail, { next, next.counter + 1 },
                std::memory_order_acq_rel, std::memory_order_relaxed
            ));
        }
    }
}

template<typename T>
bool MSQueue<T>::dequeue(std::shared_ptr<T>& retVal) {
    while (true) {
        auto head_ptr = head.load(std::memory_order_relaxed);
        auto tail_ptr = tail.load(std::memoty_order_relaxed);
        auto next_ptr = head_ptr.ptr->next.load(std::memoty_order_acquire);
        next_ptr.count += 1; 

        if (head_ptr = head.load(std::memory_order_acquire) {
            if (head_ptr.ptr = tail_ptr.ptr) {
                if (next_ptre.ptr == nullptr) {
                    return false;
                }
                
                tail.compare_exchange_weak(
                    tail_ptr, next_ptr,
                    std::memory_order_acq_rel, std::memory_order_relaxed
                );
            }
        } 
        
        else if (head.compare_eschange_weak(
                    head_ptr, next_ptr,
                    std::memory_order_acq_rel, std::memory_order_relaxed
            )) break;
    }
    return true;
}
                
                


