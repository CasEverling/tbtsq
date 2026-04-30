#pragma once

#include "base_queue.hpp"

#include <mutex>
#include <memory>

template<typename T>
class TwoLocksQueue : public MyQueue {
private:
    struct Node {
        std::shared_ptr<T> data;
        Node* next;
    }

    alignas(64) Node* head;
    alignas(64) Node* tail;

    std::mutex head_lock;
    std::mutex tail_lock;
public:
    TwoLocksQueue();
    TwoLocksQueue(TwoLocksQueue&)  = default;
    TwoLocksQueue(TwoLocksQueue&&) = default;
    TwoLocksQueue operator= (TwoLocksQueue&) = default;
    ~TwoLocksQueue();

    void enqueue(T&& val);
    bool dequeue(std::shared_ptr<T>& retVal);
};
