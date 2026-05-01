#include "hazard_pointer.hpp"
#include <vector>
#include <algorithm>

#define hp0 (queue->hazardPointers[3*id])
#define hp1 (queue->hazardPointers[3*id + 1])
#define hp2 (queue->hazardPointers[3*id + 2])

using namespace std;

// ── DATA CENTER (MSHPQ<T>) ───────────────────────────────────────────────────

template<typename T>
MSHPQ<T>::MSHPQ(size_t max_users) {             // BUG: was `MSHPQ<T>::MSHPQ<T>` (ctor name must not repeat template args)
    _max_users = max_users;
    _users.store(0, std::memory_order_release);  // BUG: `_users.load(0,...)` → `.store(0,...)`; also `std::memtory_order` typo

    hazardPointers = std::vector<std::atomic<Node*>>(3 * max_users); // BUG: was `hazardPoionters` (typo) + wrong type (need atomic)

    auto dummy_node = new Node{nullptr, nullptr};

    _head.store(dummy_node, std::memory_order_release);
    _tail.store(dummy_node, std::memory_order_release);
}

template<typename T>
std::shared_ptr<MSHPQ<T>> MSHPQ<T>::create(size_t max_users) {
    // Can't use make_shared — constructor is private.
    return std::shared_ptr<MSHPQ<T>>(new MSHPQ<T>(max_users));
}

template<typename T>
std::shared_ptr<typename MSHPQ<T>::Instance> MSHPQ<T>::instantiate() {
    auto q = std::make_shared<Instance>();

    q->readyPointers = std::vector<Node*>(10);
    for (int i = 0; i < 10; i++) {
        q->readyPointers[i] = new Node{nullptr, nullptr}; // BUG: was `q->ready_pointers` (inconsistent name)
    }

    q->retiredPointers = std::vector<Node*>();
    q->id = _users.fetch_add(1, std::memory_order_relaxed); // BUG: fetch_add needs a value argument (1)
    q->queue = this;

    return q;
}

// ── INSTANCE (MSHPQ<T>::Instance) ────────────────────────────────────────────

template<typename T>
void MSHPQ<T>::Instance::prepare_for_reintegration(Node* node) {
    node->data = nullptr;
    node->next.store(nullptr, std::memory_order_relaxed);
    readyPointers.emplace_back(node);
}

template<typename T>
void MSHPQ<T>::Instance::scan() {
    auto plist = std::vector<Node*>();
    for (const auto& hp : queue->hazardPointers)
        plist.emplace_back(hp.load(std::memory_order_acquire));
    std::sort(plist.begin(), plist.end());       // BUG: was `p_list.begin()` (inconsistent name)

    auto len_ret = retiredPointers.size();
    auto len_pls = plist.size();
    size_t i{0};                                 // BUG: `0uz` is C++23; use plain 0
    size_t j{0};

    auto rlist = std::vector<Node*>();
    for (; i < len_ret && j < len_pls; ) {
        if (plist[j] > retiredPointers[i])       // BUG: was `>=` — should be `>` to mean "not in hazard list"
            prepare_for_reintegration(retiredPointers[i++]); // BUG: was `registeredPointers` (wrong name)

        else if (plist[j] == retiredPointers[i])
            rlist.emplace_back(retiredPointers[i++]); // BUG: was `registeredPointers` (wrong name)

        else j++;
    }

    for (; i < len_ret; i++)
        prepare_for_reintegration(retiredPointers[i]); // BUG: was `registeredPointers`

    retiredPointers = std::move(rlist);          // BUG: was `registeredPointers` (wrong name)
}

template<typename T>
void MSHPQ<T>::Instance::enqueue(T&& val) {
    auto node = new Node{
        std::make_shared<T>(std::forward<T>(val)), // BUG: `std::foreward` → `std::forward`
        nullptr
    };

    Node* thread_tail;
    while (true) {
        thread_tail = queue->_tail.load(std::memory_order_acquire); // BUG: was `queue->tail` (should be `_tail`)
        hp1.store(thread_tail, std::memory_order_release); // BUG: was `threa_tail` typo; also `std::memotry_order` typo

        if (thread_tail != queue->_tail.load(std::memory_order_acquire)) continue;

        auto next = thread_tail->next.load(std::memory_order_relaxed); // BUG: was `thread_tail.load(...)` — thread_tail is already a Node*

        if (thread_tail != queue->_tail.load(std::memory_order_acquire)) continue;

        if (next != nullptr) {
            queue->_tail.compare_exchange_weak(
                thread_tail, next,
                std::memory_order_acq_rel, std::memory_order_relaxed // BUG: `aqr_rel` → `acq_rel`
            );
            continue;
        }

        Node* expected_null = nullptr;
        if (thread_tail->next.compare_exchange_weak(
                expected_null, node,
                std::memory_order_acq_rel, std::memory_order_relaxed
        )) break;
    }

    queue->_tail.compare_exchange_strong(
        thread_tail, node,
        std::memory_order_acq_rel, std::memory_order_relaxed // BUG: `acr_rel` and `memoty_order` typos
    );

    hp0.store(nullptr, std::memory_order_release);
    hp1.store(nullptr, std::memory_order_release); // BUG: `memoty_order` typo
}

template<typename T>
bool MSHPQ<T>::Instance::dequeue(std::shared_ptr<T>& retVal) {
    Node* thread_head;
    Node* thread_next;
    do {
        thread_head = queue->_head.load(std::memory_order_acquire); // BUG: `queue->head` → `queue->_head`
        hp0.store(thread_head, std::memory_order_release);

        if (thread_head != queue->_head.load(std::memory_order_acquire)) continue;

        auto thread_tail = queue->_tail.load(std::memory_order_acquire);
        thread_next = thread_head->next.load(std::memory_order_relaxed); // BUG: `thread_head.load(...)` — thread_head is Node*
        hp1.store(thread_next, std::memory_order_release);

        if (thread_tail != queue->_tail.load(std::memory_order_acquire)) continue;
        if (thread_head != queue->_head.load(std::memory_order_acquire)) continue;

        if (thread_next == nullptr) return false;

        if (thread_head == thread_tail) {
            queue->_tail.load(std::memory_order_acquire)->next.compare_exchange_weak( // BUG: `queue->tail` → `queue->_tail`
                thread_next, thread_next,        // advance tail's next (no-op CAS to swing tail pointer below)
                std::memory_order_acq_rel, std::memory_order_relaxed
            );
            queue->_tail.compare_exchange_weak(
                thread_tail, thread_next,
                std::memory_order_acq_rel, std::memory_order_relaxed
            );
            continue;
        }

        retVal = thread_next->data;
    }
    while (!queue->_head.compare_exchange_weak(
        thread_head, thread_next,
        std::memory_order_acq_rel, std::memory_order_relaxed // BUG: `aqr_rel` → `acq_rel`
    ));

    retiredPointers.emplace_back(thread_head);
    if (retiredPointers.size() > 5) scan();     // BUG: `retiredPointers > 5` → `.size() > 5`

    return true;
}

template class MSHPQ<int>;
