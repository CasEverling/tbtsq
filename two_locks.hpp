#pragma once

#include "base_queue.hpp"

#include <mutex>
#include <memory>

template<typename T>
class TwoLocksQueue : public MyQueue<T> {        
private:
    struct Node {
        std::shared_ptr<T> data;
        Node* next;
    };                                          

    alignas(64) Node* head;
    alignas(64) Node* tail;

    std::mutex head_lock;
    std::mutex tail_lock;

public:
    TwoLocksQueue();
    TwoLocksQueue(TwoLocksQueue&)  = delete;
    TwoLocksQueue(TwoLocksQueue&&) = default;
    TwoLocksQueue& operator=(TwoLocksQueue&) = delete;  
    ~TwoLocksQueue();

    void enqueue(T&& val) override;
    bool dequeue(std::shared_ptr<T>& retVal) override;
};
