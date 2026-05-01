#pragma once
#include <memory>
#include <atomic>

template<typename T>
class MyQueue {
public:
    virtual void enqueue(T&& val) = 0;
    virtual bool dequeue(std::shared_ptr<T>& retVal) = 0;
    virtual ~MyQueue() = default;
};
