#pragma once

#include <mutex>
#include <memory>
#include <cstdlib>
#include <cassert>

namespace crossbow {

/**
 * @brief A mock mutex for disabling locking in the singleton
 * 
 * This class implements the mutex concept with empty methods.
 * This can be used to disable synchronization in the singleton
 * holder.
 */
struct no_locking {
    void lock() {}
    void unlock() {}
    bool try_lock() { return true; }
};

template<typename T>
struct create_static {
    static constexpr bool supports_recreation = false;
    union max_align {
        char t_[sizeof(T)];
        short int short_int_;
        long int long_int_;
        float float_;
        double double_;
        long double longDouble_;
        struct Test;
        int Test::* pMember_;
        int (Test::*pMemberFn_)(int);
    };
    
    static T* create()
    {
        static max_align static_memory_;
        return new (&static_memory_) T;
    }
    
    static void destroy(T* ptr)
    {
        ptr->~T();
    }
};

template<typename T>
struct create_using_new {
    static constexpr bool supports_recreation = true;
    static T* create()
    {
        return new T;
    };
    
    static void destroy(T* ptr)
    {
        delete ptr;
    }
};

template<typename T>
struct create_using_malloc
{
    static constexpr bool supports_recreation = true;
    static T* create()
    {
        void* p = std::malloc(sizeof(T));
        if (!p) return nullptr;
        return new (p) T;
    }
    
    static void destroy(T *ptr)
    {
        ptr->~T();
        free(ptr);
    }
};

template<class T, class allocator>
struct create_using
{
    static constexpr bool supports_recreation = true;
    static allocator alloc_;
    
    static T* create()
    {
        T* p = alloc_.allocate(1);
        if (!p) return nullptr;
        alloc_.construct(p);
        return p;
    };
    
    static void destroy(T* ptr)
    {
        alloc_.destroy(ptr);
        alloc_.deallocate(ptr, 1);
    }
};

template<typename T>
struct default_lifetime
{
    static void schedule_destruction(T*, void (*func)())
    {
        std::atexit(func);
    }
    
    static void on_dead_ref()
    {
        throw std::logic_error("Dead reference detected");
    }
};

template<typename T>
struct phoenix_lifetime {
    static void schedule_destruction(T*, void (*func)())
    {
        std::atexit(func);
    }
    
    static void on_dead_ref()
    {
    }
};

template<typename T>
struct infinite_lifetime
{
    static void schedule_destruction(T*, void (*)()) {}
    static void on_dead_ref() {}
};

template<typename T>
struct lifetime_traits
{
    static constexpr bool supports_recreation = true;
};

template<typename T>
struct lifetime_traits<infinite_lifetime<T>>
{
    static constexpr bool supports_recreation = false;
};

template<typename T>
struct lifetime_traits<default_lifetime<T>>
{
    static constexpr bool supports_recreation = false;
};

template<
    typename Type,
    typename Create = create_static<Type>,
    typename LifetimePolicy = default_lifetime<Type>,
    typename Mutex = std::mutex>
class singleton {
public:
    typedef Type value_type;
    typedef Type* pointer;
    typedef const Type* const_pointer;
    typedef const Type& const_reference;
    typedef Type& reference;
private:
    static bool destroyed_;
    static pointer instance_;
    static Mutex mutex_;
    
    static void destroy() {
        if(destroyed_) return;
        Create::destroy(instance_);
        instance_ = nullptr;
        destroyed_ = true;
    }
public:
    static reference instance() {
        static_assert(Create::supports_recreation || !lifetime_traits<LifetimePolicy>::supports_recreation,
                      "The creation policy does not support instance recreation, while the lifetime does support it.");
        if (!instance_) {
            std::lock_guard<Mutex> l(mutex_);
            if (!instance_) {
                if (destroyed_) {
                    destroyed_ = false;
                    LifetimePolicy::on_dead_ref();
                }
                instance_ = Create::create();
                LifetimePolicy::schedule_destruction(instance_, &destroy);
            }
        }
        return *instance_;
    }
    /**
     * WARNING: DO NOT EXECUTE THIS MULTITHREADED!!!
     */
    static void destroy_instance() {
        if (instance_) {
            std::lock_guard<Mutex> l(mutex_);
            destroy();
        }
    }
public:
    pointer operator-> ()
    {
        if (!instance_) { instance(); }
        return instance_;
    }
    
    reference operator* ()
    {
        if (!instance_) { instance(); }
        return *instance_;
    }
    
    const_pointer operator-> () const
    {
        if (!instance_) { instance(); }
        return instance_;
    }
    
    const_reference operator*() const
    {
        if (!instance_) { instance(); }
        return *instance_;
    }
};

template<typename T, typename C, typename L, typename M>
bool singleton<T,C,L,M>::destroyed_ = false;

template<typename T, typename C, typename L, typename M>
typename singleton<T,C,L,M>::pointer singleton<T,C,L,M>::instance_ = nullptr;

template<typename T, typename C, typename L, typename M>
M singleton<T,C,L,M>::mutex_;

} // namespace crossbow