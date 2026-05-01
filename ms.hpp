#pragma once

#include "base_queue.hpp"
#include <atomic>
#include <memory>

template<typename T>
class MSQueue : public MyQueue<T> {              // BUG: missing <T>
private:
    struct Node;

    struct Pointer {
        Node* ptr;
        size_t counter;
    };

    struct alignas(64) Node {
        std::shared_ptr<T> data;
        std::atomic<Pointer> next;               // BUG: was `std::atomic<Pointer<T>>` — Pointer is not a template
    };

    std::atomic<Pointer> head;
    std::atomic<Pointer> tail;                   // BUG: was `atd::atomiv<Pointer>` (typos)

public:
    MSQueue() :
        head(Pointer{ new Node{ nullptr, Pointer{nullptr, 0} }, 0 }),
        tail(head.load())                        // BUG: constructor had `;` instead of `{}`-body closing `}`
    {}

    MSQueue(const MSQueue&) = delete;
    MSQueue(MSQueue&&)      = delete;
    MSQueue& operator=(const MSQueue&) = delete; // BUG: was missing return type &
    ~MSQueue() = default;

    void enqueue(T&& val) override;
    bool dequeue(std::shared_ptr<T>& retVal) override;
};
