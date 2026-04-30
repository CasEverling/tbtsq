#include <memory>
#include <atomic>

template<typename T>
class MyQueue {
public:
    virtual void enqueue(T&& val);
    virtual bool dequeue(std::shared_ptr<T>& retVal);
};
