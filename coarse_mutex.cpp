#include "coarse_mutex.hpp"

#include <queue>
#include <memory>
#include <mutex>

template<typename T>
void CoarseQueue<T>::enqueue(T&& val) {
    std::lock_guard<std::mutex> lg(lk);
    auto node = std::make_shared<T>(std::forward<T>(val));  // BUG: std::foreward → std::forward
    data.push(node);
}

template<typename T>
bool CoarseQueue<T>::dequeue(std::shared_ptr<T>& retVal) {
    std::lock_guard<std::mutex> lg(lk);

    if (data.empty()) return false;

    retVal = data.front();
    data.pop();

    return true;
}
