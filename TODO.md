* Read & review P2738R2
    - pay attention to incomplete types
* Implement async wait(futures...) generator like Python's [asyncio.as_completed](https://docs.python.org/3/library/asyncio-task.html#asyncio.as_completed)
* Clean up friend status
* Build & test on ESP32-C3 & ESP32-S3 with ESP-IDF >= 5.4 (has GCC 14 for C++23 deducing this)
* Allocator support via `promise_type::operator new`
* Track [address computation for coroutine frame differs between BasePromise and MostDerivedPromise](https://gcc.gnu.org/bugzilla/show_bug.cgi?id=118014)
    - having this fixed would allow earlier type erasure and as a result less instantiations of not-so-tiny template functions
    - example showing that GCC & Clang place overaligned promises at different offsets within coroutine frame than promises with `alignof(promise) <= alignof(void*)*2` while MSVC produces static offsets (independent of promise' alignment): https://godbolt.org/z/PbKaMP9cx
```cpp
#include <coroutine>

template <typename T>
struct checks
{
    static_assert(sizeof(std::coroutine_handle<T>) == sizeof(void*));
    static_assert(alignof(std::coroutine_handle<T>) == alignof(void*));

    static std::coroutine_handle<T> coro_from_promise(T& y)
    {
        return std::coroutine_handle<T>::from_promise(y);
    }

    static void* handle_coro(std::coroutine_handle<T> y)
    {
        return y.address();
    }

    static void* handle_coro(T& y)
    {
        return handle_coro(coro_from_promise(y));
    }

    static void resume_coro(T& y)
    {
        return coro_from_promise(y).resume();
    }

    static void destroy_coro(T& y)
    {
        return coro_from_promise(y).destroy();
    }
};

struct empty_t {};
struct overaligned
{
    alignas(256) char _[256];
};

template struct checks<empty_t>;
template struct checks<overaligned>;
```
