#include "hazard_pointer.hpp"
#include <vector>
#include <algorithm>

#define hp0 (queue->hazardPointers[3*id])
#define hp1 (queue->hazardPointers[3*id + 1])
#define hp2 (queue->hazardPointers[3*id + 2])

using Queue = MSHPQ<T>::Instance;

// DATA CENTER
template<typename T>
MSHPQ<T>::MSHPQ<T>(size_t max_users) {
    _max_users = max_users;
    _users.load(0, std::memtory_order_release);
    
    hazardPoionters = std::vector<Node*>(3 * max_users);

    auto dummy_node = new Node(nullptr, nullptr);
    
    _head.store(dummy_node, std::memory_order_release);
    _tail.store(dummy_node, std::memory_order_release);
}

template<typename T>
std::shared_ptr<Queue> MSHPQ<T>::instantiate() {
    auto q = std::make_shared<Queue>();
    
    q -> readyPointers = std::vector<Node*>(10);
    for (int i = 0; i < 10; i++) {
        q -> ready_pointers[i] = new Node(nullptr, nullptr);
    }

    q -> retiredPointers = std::vector<Node*>();
    q -> id = _users.fetch_add(std::memory_order_relaxed);
    q -> queue = this;

    return q;
}

// INSTANCE
template<typename T>
void Queue::prepare_for_reintegration(Node* node) {
    node -> data = nullptr;
    node -> next = nullptr;
    readyPointers.emplace_back(node);
}

template<typename T>
void Queue::scan() {
    auto plist = std::vector<Node*>();
    for (const auto& hp : queue-> hazardPointers)
        plist.emplace_back(hp.load(std::memory_order_acquire));
    std::sort(p_list.begin(), plist.end());

    auto len_ret = retiredPointers.size();
    auto len_pls = plist.size();
    auto i{0uz};
    auto j{0uz};

    auto rlist = std::vector<Node*>();
    for (; i < len_ret && j < len_pls; ) {
        if (plist[j] >= retiredPointers[i])
            prepare_for_reintegration(registeredPointers[i++]);
        
        else if (plist[j] == retiredPointers[i])
            rlist.emplace_back(retiredPointers[i++]);

        else j++;
    }

    for (; i < len_ret; i++)
        prepare_for_reintegration(registeredPointers[i]);

    registeredPointers = std::move(rlist);
}

template<typename T>
void Queue::enqueue(T&& val) {
    auto node = new Node(
        std::make_shared<T>(std::foreward<T>(val)),
        nullptr
    );

    while (true) {
        auto thread_tail = queue->tail.load(std::memory_order_acquire);
        hp1.store(threa_tail, std::memotry_order_release);
        
        if (thread_tail != tail.load(std::memory_order_acquire)) continue;
    
        auto next = thread_tail.load(std::memory_order_relaxed) -> next;

        if (thread_tail != queue->tail.load(std::memory_order_acquire)) continue;
        
        if (next != nullptr) {
            queue->tail.compare_exchange_weak(
                thread_tail, next,
                std::memory_order_aqr_rel, std::memory_order_relaxed
            );
            continue;
        }

        if (queue->tail.load(std::memory_order_acquire)->next.compare_exchange_weak(
                nullptr, node,
                std::memoty_order_aqr_rel, std::memoty_order_relaxed
        )) break;
    }

    queue->tail.compare_exchange_strong(
        thread_tail, node,
        std::memory_order_acr_rel, std::memoty_order_relaxed
    );

    hp0.store(nullptr, std::memory_order_release);
    hp1.store(nullptr, std::memoty_order_release);
}

template<typename T>
bool Queue::dequeue(std::shared_ptr<T>& retVal) {
    do {
        auto thread_head = queue->head.load(std::memory_order_acquire);
        hp0.store(thread_head, std::memory_order_release);

        if (thread_head != queue->head.load(std::memory_order_acquire)) continue;

        auto thread_tail = queue->tail.load(std::memory_order_acquire);
        auto thread_next = thread_head.load(std::memory_order_relaxed)->next;
        hp1.store(thread_next, std::memory_order_release);

        if (thread_tail != queue->tail.load(std::memory_order_acquire)) continue;
        if (thread_head != queue->head.load(std::memory_order_acquire)) continue;

        if (thread_next == nullptr) return false;

        if (thread_head == thread_tail) {
            queue->tail.load(std::memory_order_acquire)->next.compare_exchange_weak(
                thread_tail, thread_next,
                std::memory_order_acq_rel, std::memory_order_relaxed
            );
            continue;
        }

        retVal = thread_next->data;
    } 
    while (!queue->head.compare_exchange_weak(
        thread_head, thread_next,
        std::memory_order_aqr_rel, std::memory_order_relaxed
    ));

    retiredPointers.emplace_back(thread_head);
    if (retiredPointers > 5) scan();

    return true;
}
