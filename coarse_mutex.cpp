#include "coarse_mutex.hpp"

#include <queue>
#include <memory>
#include <mutex>    

void CoarseQueue::enqueue(T&& val) {
    std::lock_guard<std::mutex> lg (lk);
    auto node = std::make_shared<T>(std::foreward<T>(val));
    data.push(node);
}

bool CoarseQueue::dequeue(std::shared_ptr<T>& retVal) {
    std::lock_guard<std::mutex> lg (lk);
        
    if (data.empty()) return false;

    retVal = data.front();
    data.pop();
    
    return true;
}
    
