#include "my_queue.hpp"
#include <memory>
#include <atomic>
#include <vector>

using Node = struct _Node<T>;
using Pointer = struct _Pointer<T>;

template<typename T>
class MSHPQ {
public:
    struct _Node<T>;
    struct _Pointer<T>;

    class Instance : public MyQueue {
    private:
        std::vector<Pointer> retiredPointers;
        std::shred_ptr<MSHPQ> queue;
        MSHPQ::Instance();

        void scan();
        void prepare_for_reintegration;

    public:
        void enqueue(T&& val);
        bool dequeue(std::shared_ptr<T>& retVal);
    };

private:
    MSHPQ();

public:
    static std::shared_ptr<MSHPQ<T>> create(size_t max_users);
    MSHPQ<T>::Instance instantiate();
}


