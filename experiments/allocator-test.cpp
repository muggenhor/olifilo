// SPDX-License-Identifier: GPL-3.0-or-later

#include <coroutine>
#include <cstddef>
#include <format>
#include <iostream>
#include <iterator>
#include <memory>
#include <memory_resource>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>

struct myint
{
  myint(int y)
    : x(y)
  {
      std::format_to(std::ostreambuf_iterator(std::cerr), "myint(int={}):this={}\n", y, static_cast<const void*>(this));
  }

  myint(myint&& rhs)
    : x(rhs.x)
  {
      std::format_to(std::ostreambuf_iterator(std::cerr), "myint(myint&&.={}):this={}\n", rhs.x, static_cast<const void*>(this));
  }

  int x;
};

struct allocator_block
{
  alignas(__STDCPP_DEFAULT_NEW_ALIGNMENT__)
  std::byte data[__STDCPP_DEFAULT_NEW_ALIGNMENT__];

  static constexpr std::size_t blocks_for_bytes(std::size_t byte_count) noexcept
  {
    return (byte_count + sizeof(allocator_block) - 1) / sizeof(allocator_block);
  }
};


template <typename Allocator = std::allocator<allocator_block>>
struct promise;

template <typename Allocator = std::allocator<allocator_block>>
struct future
{
  using promise_type = promise<Allocator>;
  std::coroutine_handle<promise_type> handle;

  //using allocator_type = typename std::allocator_traits<Allocator>::template rebind_alloc<allocator_block>;

  future() = default;
  constexpr future(std::coroutine_handle<promise_type> handle) noexcept
    : handle(handle)
  {
  }

  constexpr future(future&& rhs) noexcept
    : handle(std::exchange(rhs.handle, {}))
  {
  }

  future& operator=(future&& rhs) noexcept
  {
    if (this != &rhs)
    {
      if (handle)
        handle.destroy();
      handle = std::exchange(rhs.handle, {});
    }
    return *this;
  }

  ~future()
  {
    if (handle)
      handle.destroy();
  }
};

template <typename Allocator>
struct promise
{
  constexpr std::suspend_never initial_suspend() const noexcept { return {}; }
  constexpr std::suspend_always final_suspend() const noexcept { return {}; }

  constexpr future<Allocator> get_return_object() noexcept { return {std::coroutine_handle<promise>::from_promise(*this)}; }
  constexpr void return_void() noexcept {}

  void unhandled_exception() { throw; }

  using allocator_type = typename std::allocator_traits<Allocator>::template rebind_alloc<allocator_block>;

  private:
  static constexpr std::size_t size_with_allocator(std::size_t size) noexcept
  {
    return size + alignof(allocator_type) - 1 + sizeof(allocator_type);
  }

  template <typename... Args>
  static constexpr decltype(auto) forward_last(Args&&... args) noexcept
  {
    // Comma expression to get the last
    return (static_cast<Args&&>(args), ...);
  }

  public:

  template <typename... Args>
  requires(std::is_convertible_v<decltype(forward_last(std::declval<Args&&>()...)), allocator_type>
        && !std::is_same_v<std::decay_t<decltype(forward_last(std::declval<Args&&>()...))>, std::nullptr_t>)
  void* operator new(std::size_t size, Args&&... args)
  {
    allocator_type alloc_(forward_last(std::forward<Args>(args)...));
    auto* const ptr = alloc_.allocate(allocator_block::blocks_for_bytes(size_with_allocator(size)));

    // store a copy of our allocator at the first aligned location within our storage
    allocator_type* const alloc_loc = reinterpret_cast<allocator_type*>(((reinterpret_cast<std::uintptr_t>(ptr) + size + alignof(allocator_type) - 1) / sizeof(allocator_type)) * sizeof(allocator_type));
    ::new (alloc_loc) allocator_type(std::move(alloc_));

    return ptr;
  }

  template <typename... Args>
  requires(!std::is_convertible_v<decltype(forward_last(std::declval<Args&&>()...)), allocator_type>
        || std::is_same_v<std::decay_t<decltype(forward_last(std::declval<Args&&>()...))>, std::nullptr_t>)
  void* operator new(std::size_t size, Args&&... args)
  {
    allocator_type alloc;
    return operator new(size, std::forward<Args>(args)..., alloc);
  }

  void operator delete(void* ptr, std::size_t size) noexcept
  {
    allocator_type* const alloc_loc = reinterpret_cast<allocator_type*>(((reinterpret_cast<std::uintptr_t>(ptr) + size + alignof(allocator_type) - 1) / sizeof(allocator_type)) * sizeof(allocator_type));

    allocator_type alloc_(std::move(*alloc_loc));
    alloc_loc->~allocator_type();

    alloc_.deallocate(static_cast<allocator_block*>(ptr), allocator_block::blocks_for_bytes(size_with_allocator(size)));
  }
};

future<std::pmr::polymorphic_allocator<>> hmm(myint, std::pmr::polymorphic_allocator<> alloc)
{
  co_return;
}

template <std::size_t N>
class stack_memory : public std::pmr::memory_resource
{
  public:
    constexpr stack_memory() noexcept = default;

    stack_memory(const stack_memory&) = delete;
    stack_memory(stack_memory&&) = delete;
    void operator=(const stack_memory&) = delete;
    void operator=(stack_memory&&) = delete;

  protected:
    void* do_allocate(std::size_t size, std::size_t alignment) override
    {
      void* p = &buf[allocated];
      std::size_t space = N - allocated;
      if (!std::align(alignment, size, p, space))
        throw std::bad_alloc();

      allocated = static_cast<std::byte*>(p) - buf + size;
      return p;
    }

    void do_deallocate(void* p, std::size_t bytes, std::size_t alignment) override
    {
    }

    bool do_is_equal(const std::pmr::memory_resource& rhs) const noexcept override
    {
      return this == &rhs;
    }

  private:
    std::size_t allocated = 0;
    alignas(__STDCPP_DEFAULT_NEW_ALIGNMENT__) std::byte buf[N];
};

int main(int argc, char** argv)
{
  stack_memory<4 << 10> mem;

  std::vector<future<std::pmr::polymorphic_allocator<>>, std::pmr::polymorphic_allocator<future<std::pmr::polymorphic_allocator<>>>> xs(std::pmr::polymorphic_allocator<>{&mem});
  xs.reserve(argc);
  for (int i = 0; i < argc; ++i)
    xs.push_back(hmm(i, xs.get_allocator()));
}
