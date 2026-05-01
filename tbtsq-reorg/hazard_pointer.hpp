#pragma once
#include "base_queue.hpp"
#include <memory>
#include <atomic>
#include <vector>
#include <algorithm>

#define _hp0 (queue->hazardPointers[3*id])
#define _hp1 (queue->hazardPointers[3*id + 1])
#define _hp2 (queue->hazardPointers[3*id + 2])

template<typename T>
class MSHPQ {
public:
    struct alignas(64) Node {
        std::shared_ptr<T>  data;
        std::atomic<Node*>  next;
        Node(std::shared_ptr<T> d, Node* n) : data(std::move(d)), next(n) {}
    };

    class Instance : public MyQueue<T> {
    private:
        std::vector<Node*>  retiredPointers;
        std::vector<Node*>  readyPointers;
        MSHPQ*              queue  = nullptr;
        size_t              id     = 0;

        friend class MSHPQ<T>;

        void prepare_for_reintegration(Node* node) {
            node->data = nullptr;
            node->next.store(nullptr, std::memory_order_relaxed);
            readyPointers.emplace_back(node);
        }

        void scan() {
            std::vector<Node*> plist;
            for (const auto& hp : queue->hazardPointers)
                plist.emplace_back(hp.load(std::memory_order_acquire));
            std::sort(plist.begin(), plist.end());

            std::vector<Node*> rlist;
            size_t i = 0, j = 0;
            auto len_ret = retiredPointers.size();
            auto len_pls = plist.size();

            for (; i < len_ret && j < len_pls; ) {
                if      (plist[j] > retiredPointers[i])  prepare_for_reintegration(retiredPointers[i++]);
                else if (plist[j] == retiredPointers[i]) rlist.emplace_back(retiredPointers[i++]);
                else                                     ++j;
            }
            for (; i < len_ret; ++i)
                prepare_for_reintegration(retiredPointers[i]);

            retiredPointers = std::move(rlist);
        }

    public:
        Instance() = default;

        void enqueue(T&& val) override {
            auto node = new Node{std::make_shared<T>(std::forward<T>(val)), nullptr};

            Node* thread_tail;
            while (true) {
                thread_tail = queue->_tail.load(std::memory_order_acquire);
                _hp1.store(thread_tail, std::memory_order_release);
                if (thread_tail != queue->_tail.load(std::memory_order_acquire)) continue;

                auto next = thread_tail->next.load(std::memory_order_relaxed);
                if (thread_tail != queue->_tail.load(std::memory_order_acquire)) continue;

                if (next != nullptr) {
                    queue->_tail.compare_exchange_weak(
                        thread_tail, next,
                        std::memory_order_acq_rel, std::memory_order_relaxed);
                    continue;
                }

                Node* expected = nullptr;
                if (thread_tail->next.compare_exchange_weak(
                        expected, node,
                        std::memory_order_acq_rel, std::memory_order_relaxed))
                    break;
            }

            queue->_tail.compare_exchange_strong(
                thread_tail, node,
                std::memory_order_acq_rel, std::memory_order_relaxed);

            _hp0.store(nullptr, std::memory_order_release);
            _hp1.store(nullptr, std::memory_order_release);
        }

        bool dequeue(std::shared_ptr<T>& retVal) override {
            Node* thread_head;
            Node* thread_next = nullptr;
            do {
                thread_head = queue->_head.load(std::memory_order_acquire);
                _hp0.store(thread_head, std::memory_order_release);
                if (thread_head != queue->_head.load(std::memory_order_acquire)) continue;

                auto thread_tail = queue->_tail.load(std::memory_order_acquire);
                thread_next      = thread_head->next.load(std::memory_order_relaxed);
                _hp1.store(thread_next, std::memory_order_release);

                if (thread_tail != queue->_tail.load(std::memory_order_acquire)) continue;
                if (thread_head != queue->_head.load(std::memory_order_acquire)) continue;

                if (thread_next == nullptr) return false;

                if (thread_head == thread_tail) {
                    queue->_tail.compare_exchange_weak(
                        thread_tail, thread_next,
                        std::memory_order_acq_rel, std::memory_order_relaxed);
                    continue;
                }

                retVal = thread_next->data;
            }
            while (!queue->_head.compare_exchange_weak(
                thread_head, thread_next,
                std::memory_order_acq_rel, std::memory_order_relaxed));

            retiredPointers.emplace_back(thread_head);
            if (retiredPointers.size() > 5) scan();
            return true;
        }
    };

private:
    explicit MSHPQ(size_t max_users)
        : hazardPointers(3 * max_users), _users(0), _max_users(max_users)
    {
        auto dummy = new Node{nullptr, nullptr};
        _head.store(dummy, std::memory_order_release);
        _tail.store(dummy, std::memory_order_release);
    }

    std::vector<std::atomic<Node*>> hazardPointers;
    std::atomic<size_t>             _users;
    size_t                          _max_users;
    std::atomic<Node*>              _head;
    std::atomic<Node*>              _tail;

public:
    static std::shared_ptr<MSHPQ<T>> create(size_t max_users) {
        return std::shared_ptr<MSHPQ<T>>(new MSHPQ<T>(max_users));
    }

    std::shared_ptr<Instance> instantiate() {
        auto q            = std::make_shared<Instance>();
        q->readyPointers  = std::vector<Node*>(10);
        for (int i = 0; i < 10; ++i)
            q->readyPointers[i] = new Node{nullptr, nullptr};
        q->retiredPointers = {};
        q->id              = _users.fetch_add(1, std::memory_order_relaxed);
        q->queue           = this;
        return q;
    }
};

#undef _hp0
#undef _hp1
#undef _hp2
