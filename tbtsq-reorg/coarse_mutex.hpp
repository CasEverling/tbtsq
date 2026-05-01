#pragma once
#include "base_queue.hpp"

#include <queue>
#include <mutex>
#include <memory>

template<typename T>
class CoarseQueue : public MyQueue<T> {
private:
    std::mutex lk;
    std::queue<std::shared_ptr<T>> data;

public:
    void enqueue(T&& val) override {
        std::lock_guard<std::mutex> lg(lk);
        data.push(std::make_shared<T>(std::forward<T>(val)));
    }

    bool dequeue(std::shared_ptr<T>& retVal) override {
        std::lock_guard<std::mutex> lg(lk);
        if (data.empty()) return false;
        retVal = data.front();
        data.pop();
        return true;
    }
};
