#include "two_locks.hpp"

template<typename T>
TwoLocksQueue<T>::TwoLocksQueue() {              // BUG: was `void TwoLocksQueue::TwoLocksQueue()` (wrong return type + missing <T>)
    auto node = new Node{nullptr, nullptr};
    head = node;
    tail = node;
}

template<typename T>
void TwoLocksQueue<T>::enqueue(T&& val) {        // BUG: was `TwoLocksQueue::enqueue` (missing <T>)
    auto node = new Node{
        std::make_shared<T>(std::forward<T>(val)), nullptr  // BUG: std::foreward → std::forward
    };                                           // BUG: missing semicolon after Node{...}

    std::lock_guard<std::mutex> lg(tail_lock);
    tail->next = node;
    tail = node;                                 // BUG: was `tail->node` (nonsense; should advance tail pointer)
}

template<typename T>
bool TwoLocksQueue<T>::dequeue(std::shared_ptr<T>& retVal) {  // BUG: missing <T>
    std::lock_guard<std::mutex> lf(head_lock);

    auto node = head;
    auto new_head = node->next;                  // BUG: was `auto new head` (space in identifier)

    if (new_head == nullptr)
        return false;

    retVal = new_head->data;                     // BUG: was `node->value` (wrong node + wrong field name)
    head = new_head;

    delete node;                                 // BUG: dummy node leaks without this
    return true;
}

template<typename T>
TwoLocksQueue<T>::~TwoLocksQueue() {
    while (head != nullptr) {
        auto next = head->next;
        delete head;
        head = next;
    }
}
