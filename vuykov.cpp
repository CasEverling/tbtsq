#include "vuykov.hpp"

template<typename T>
void VuykovQueue<T>::enqueue(T&& val) {
    const size_t slot = tail.fetch_add(1, std::memory_order_relaxed) % capacity;
    Node& node = ring[slot];

    while (node.ready.test(std::memory_order_acquire)) {
        _mm_pause();
    }

    node.data = std::make_shared<T>(std::move(val));

    node.ready.test_and_set(std::memory_order_release);
}

template<typename T>
bool VuykovQueue<T>::dequeue(std::shared_ptr<T>& retVal) {
    const size_t slot = head.fetch_add(1, std::memory_order_relaxed) % capacity;
    Node& node = ring[slot];

    if (!node.ready.test(std::memory_order_acquire)) {
        head.fetch_sub(1, std::memory_order_relaxed);
        return false;
    }

    retVal     = std::move(node.data);
    node.data  = nullptr;

    node.ready.clear(std::memory_order_release);

    return true;
}
