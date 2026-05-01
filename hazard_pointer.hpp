#pragma once
#include "base_queue.hpp"
#include <memory>
#include <atomic>
#include <vector>

template<typename T>
class MSHPQ {
public:
    struct alignas(64) Node {
        std::shared_ptr<T> data;
        std::atomic<Node*> next;
    };

    class Instance : public MyQueue<T> {
    private:
        std::vector<Node*> retiredPointers;
        std::vector<Node*> readyPointers;

        MSHPQ* queue;
        size_t id;

        void scan();
        void prepare_for_reintegration(Node* node);

        // MSHPQ<T>::instantiate() constructs and populates Instance
        friend class MSHPQ<T>;

    public:
        Instance() = default;

        void enqueue(T&& val) override;
        bool dequeue(std::shared_ptr<T>& retVal) override;
    };

private:
    explicit MSHPQ(size_t max_users);

    std::vector<std::atomic<Node*>> hazardPointers;
    std::atomic<size_t> _users;
    size_t _max_users;

    std::atomic<Node*> _head;
    std::atomic<Node*> _tail;

public:
    static std::shared_ptr<MSHPQ<T>> create(size_t max_users);
    std::shared_ptr<Instance> instantiate();
};
