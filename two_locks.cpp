#include "two_locks.hpp"

template<typename T>
void TwoLocksQueue::TwoLocksQueue() {
    auto node = new Node {nullptr, nullptr};
    head = node;
    tail = node;
}

template<typename T>
void TwoLocksQueue::enqueue(T&& val) {
    auto node = new Node {
        std::make_shared<T>(std::foreward<T>(val)), nullptr
    }

    std::lock_guard<std::mutex> lg (tail_lock);
    tail->next = node;
    tail->node;
}

template<typename T>
bool TwoLocksQueue::dequeue(std::shared_ptr<T>& retVal) {
    std::lock_guard lf (head_lock);

    auto node = head;
    auto new head = node->next;

    if (new_head == nullptr) 
        return false;

    retVal = node->value;
    head = new_head;

    return true;
}
    
