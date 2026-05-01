#pragma once

#include "base_queue.hpp"

#include <vector>
#include <atomic>
#include <memory>
#include <immintrin.h>

template<typename T>
class VuykovQueue : public MyQueue<T> {
private:
    // Each slot in the ring buffer.
    // - `data`  : the stored item.
    // - `ready` : atomic_flag used as a single-bit sequence indicator.
    //             cleared  = slot is empty, ready to be written by a producer.
    //             set      = slot is full,  ready to be read  by a consumer.
    // alignas(64) keeps each Node on its own cache line, eliminating
    // false sharing between threads operating on adjacent slots.
    struct alignas(64) Node {
        std::shared_ptr<T>  data;
        std::atomic_flag    ready = ATOMIC_FLAG_INIT; // cleared = empty

        Node()                         = default;
        Node(const Node&)              = delete;
        Node& operator=(const Node&)   = delete;
        Node(Node&&)                   = delete;
        Node& operator=(Node&&)        = delete;
    };

    std::vector<Node> ring;

    alignas(64) const size_t capacity;
    alignas(64) std::atomic<size_t> tail;  // producer cursor
    alignas(64) std::atomic<size_t> head;  // consumer cursor

public:
    explicit VuykovQueue(size_t size)
        : ring(size), capacity(size), tail(0), head(0) {}

    VuykovQueue(const VuykovQueue&)            = delete;
    VuykovQueue& operator=(const VuykovQueue&) = delete;
    VuykovQueue(VuykovQueue&&)                 = delete;
    VuykovQueue& operator=(VuykovQueue&&)      = delete;

    ~VuykovQueue() override = default;

    void enqueue(T&& val) override {
        const size_t slot = tail.fetch_add(1, std::memory_order_relaxed) % capacity;
        Node& node = ring[slot];

        // Spin while the slot is still occupied (ready flag SET).
        // Back-pressures producers when the ring is full.
        while (node.ready.test(std::memory_order_acquire))
            _mm_pause();

        // Move the value in — no copy, safe for move-only types.
        node.data = std::make_shared<T>(std::move(val));

        // Publish: flag transitions cleared → set.
        node.ready.test_and_set(std::memory_order_release);
    }

    bool dequeue(std::shared_ptr<T>& retVal) override {
        const size_t slot = head.fetch_add(1, std::memory_order_relaxed) % capacity;
        Node& node = ring[slot];

        // Non-blocking: slot empty → undo cursor advance and report miss.
        if (!node.ready.test(std::memory_order_acquire)) {
            head.fetch_sub(1, std::memory_order_relaxed);
            return false;
        }

        // Transfer ownership directly — no ref-count bump.
        retVal    = std::move(node.data);
        node.data = nullptr;

        // Unpublish: flag transitions set → cleared, slot writable again.
        node.ready.clear(std::memory_order_release);
        return true;
    }
};
