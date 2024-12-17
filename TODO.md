* Read & review P2738R2
    - pay attention to incomplete types
* Implement async wait(futures...) generator like Python's [asyncio.as_completed](https://docs.python.org/3/library/asyncio-task.html#asyncio.as_completed)
* Clean up friend status
* Build & test on ESP32-C3 & ESP32-S3 with ESP-IDF >= 5.4 (has GCC 14 for C++23 deducing this)
* Allocator support via `promise_type::operator new`
* Track [address computation for coroutine frame differs between BasePromise and MostDerivedPromise](https://gcc.gnu.org/bugzilla/show_bug.cgi?id=118014)
    - having this fixed would allow earlier type erasure and as a result less instantiations of not-so-tiny template functions
