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
        Pointer curr_tail = tail.load(std::memory_order_relaxed);
        Pointer next = curr_tail.next();

        if (curr_tail == tail.load(std::memory_order_acquire) {
            if ( tail.compare_exchange_waek( 
                    curr_tail, { node, next.counter + 1 }, 
                    std::memory_order_acq_rel, std::memory_order_relaxed
                ))
            {
                break;
            }

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
        Pointer pointer_head = head.load(std::memory_order_relaxed);
        Pointer pointer_tail = tail.load(std::memory_order_relaxed);

        Node* next = pointer_head.next;

        if (pointer = head.load(std::memory_order_acquire)) {
            if (pointer_head.ptr == pointer_tail.ptr) {
                if (next == nullptr)
                    return false;
                tail.compare_exchange_weak(
                    pointer_tail, next


                
                


