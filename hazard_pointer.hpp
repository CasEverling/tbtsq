#include "my_queue.hpp"
#include <memory>
#include <atomic>
#include <vector>
    
template<typename T>
class MSHPQ {
public:
    struct alignas(64) Node{
        std::shared_ptr<T> data;
        std::atomic<Node*> next;
    };
   
    class Instance : public MyQueue {
    private:
        std::vector<Node*> retiredPointers;
        std::vector<Node*> readyPointers;

        MSHPQ* queue;
        size_t id;

        MSHPQ::Instance();

        void scan();
        void prepare_for_reintegration(Node* node);

    public:
        void enqueue(T&& val);
        bool dequeue(std::shared_ptr<T>& retVal);
    };

private:
    MSHPQ();
    
    std::vector<std::atomic<Node*>> hazardPointers;
    std::atomic<size_t> _users;
    size_t _max_users;

    std::atomic<Node*> _head;
    std::atomic<Node*> _tail;
    
    friend class MSHPQ<T>Instance;
    
public:
    static std::shared_ptr<MSHPQ<T>> create(size_t max_users);
    MSHPQ<T>::Instance instantiate();
}


