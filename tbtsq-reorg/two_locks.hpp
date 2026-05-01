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

    alignas(64) std::mutex head_lock;
    alignas(64) std::mutex tail_lock;

public:
    TwoLocksQueue() {
        auto node = new Node{nullptr, nullptr};
        head = node;
        tail = node;
    }

    TwoLocksQueue(const TwoLocksQueue&) = delete;
    TwoLocksQueue(TwoLocksQueue&&)      = delete;
    TwoLocksQueue& operator=(const TwoLocksQueue&) = delete;

    ~TwoLocksQueue() {
        while (head != nullptr) {
            auto next = head->next;
            delete head;
            head = next;
        }
    }

    void enqueue(T&& val) override {
        auto node = new Node{
            std::make_shared<T>(std::forward<T>(val)), nullptr
        };
        std::lock_guard<std::mutex> lg(tail_lock);
        tail->next = node;
        tail = node;
    }

    bool dequeue(std::shared_ptr<T>& retVal) override {
        std::lock_guard<std::mutex> lg(head_lock);
        auto node     = head;
        auto new_head = node->next;
        if (new_head == nullptr) return false;
        retVal = new_head->data;
        head   = new_head;
        delete node;
        return true;
    }
};
