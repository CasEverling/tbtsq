#include "base_queue.hpp"

#include <queue>
#include <mutex>
#include <memory>

template<typename T>
class CoarseQueue : public MyQueue {
priate:
    std::mutex lk;
    std::queue<std::shared_ptr<T>> data;

public:
    void enqueue(T&& val);
    bool dequeue(std::shared_ptr<T>& retVal);
};
