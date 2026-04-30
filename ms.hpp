#pragma once

#include "base_queue.hpp"
#include <atomic>
#include <memory>

template<typename T>
class MSQueue : public MyQueue {
private:
    struct alignas(64) Pointer {
        Node* ptr;
        size_t counter;
    };

    struct alignas(64) Node {
        std::shared_ptr<T> data;
        Pointer<T> next;
    };
    
    Pointer head;
    Pointer tail;

public:
    MSQueue<T>() : 
        head({ new Node( nullptr, { nullptr, 0 } ), 0 }), 
        tail(head);

    MSQueue<T>(MSQueue<T>&) = default;
    MSQueue<T>(MSQueue<T>&&) = default;
    MSQueue<T> operator= (MSQueue&) = default;
    ~MSQueue<T>() = default;

    void enqueue(T&& val);
    bool dequeue(std::shared_ptr<T>& retVal);
};
    


