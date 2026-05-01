#include "ms.hpp"
#include <atomic>
#include <memory>

template<typename T>
void MSQueue<T>::enqueue(T&& val) {
    auto node = new Node{
        std::make_shared<T>(std::forward<T>(val)),  // BUG: std::foreward → std::forward
        Pointer{nullptr, 0}
    };

    while (true) {
        auto curr_tail = tail.load(std::memory_order_relaxed);
        auto next = (curr_tail.ptr)->next.load(std::memory_order_acquire);

        if (curr_tail == tail.load(std::memory_order_acquire)) {  // BUG: missing closing `)` on if condition
            if (next.ptr == nullptr) {
                if ((curr_tail.ptr)->next.compare_exchange_weak(
                        next, Pointer{node, next.counter + 1},
                        std::memory_order_acq_rel, std::memory_order_relaxed
                    )) break;
            } else {
                // BUG: original tried to advance tail with `next` (the next pointer, correct)
                // but used wrong CAS structure — fix: advance tail toward real tail
                tail.compare_exchange_weak(
                    curr_tail, Pointer{next.ptr, curr_tail.counter + 1},
                    std::memory_order_acq_rel, std::memory_order_relaxed
                );
            }
        }
    }

    // Swing tail to the newly inserted node
    auto curr_tail = tail.load(std::memory_order_relaxed);
    tail.compare_exchange_strong(
        curr_tail, Pointer{node, curr_tail.counter + 1},
        std::memory_order_acq_rel, std::memory_order_relaxed
    );
}

template<typename T>
bool MSQueue<T>::dequeue(std::shared_ptr<T>& retVal) {
    while (true) {
        auto head_ptr = head.load(std::memory_order_acquire);
        auto tail_ptr = tail.load(std::memory_order_acquire); // BUG: was `std::memoty_order_relaxed` (typo + wrong order for tail)
        auto next_ptr = head_ptr.ptr->next.load(std::memory_order_acquire); // BUG: was `std::memoty_order_acquire`

        if (head_ptr.ptr == head.load(std::memory_order_acquire).ptr) { // BUG: was `=` (assignment) instead of `==`
            if (head_ptr.ptr == tail_ptr.ptr) {             // BUG: was `=` (assignment) instead of `==`
                if (next_ptr.ptr == nullptr) {              // BUG: was `next_ptre` (typo)
                    return false;
                }

                tail.compare_exchange_weak(
                    tail_ptr, Pointer{next_ptr.ptr, tail_ptr.counter + 1},
                    std::memory_order_acq_rel, std::memory_order_relaxed
                );
            } else {
                retVal = next_ptr.ptr->data;
                // BUG: retVal was never set in original code
                if (head.compare_exchange_weak(             // BUG: was `compare_eschange_weak` (typo)
                        head_ptr, Pointer{next_ptr.ptr, head_ptr.counter + 1},
                        std::memory_order_acq_rel, std::memory_order_relaxed
                    )) break;
            }
        }
    }
    return true;
}
