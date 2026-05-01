#pragma once
#include "base_queue.hpp"

#include <queue>
#include <mutex>
#include <memory>

template<typename T>
class CoarseQueue : public MyQueue<T> {          // BUG: was `MyQueue` (missing <T>)
private:                                          // BUG: was `priate:` (typo)
    std::mutex lk;
    std::queue<std::shared_ptr<T>> data;

public:
    void enqueue(T&& val) override;
    bool dequeue(std::shared_ptr<T>& retVal) override;
};
