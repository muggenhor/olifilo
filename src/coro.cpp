// SPDX-License-Identifier: GPL-3.0-or-later

#include <olifilo/expected.hpp>
#include <olifilo/io/errors.hpp>
#include <olifilo/io/poll.hpp>
#include <olifilo/io/types.hpp>

#include "logging-stuff.hpp"

#include <array>
#include <cassert>
#include <cerrno>
#include <chrono>
#include <coroutine>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <iterator>
#include <limits>
#include <optional>
#include <ranges>
#include <span>
#include <string_view>
#include <system_error>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

template<class... Ts>
struct overloaded : Ts... { using Ts::operator()...; };

namespace olifilo
{
// expected<T, std::error_code> wrappers for I/O syscalls
namespace io
{
expected<std::size_t> read(file_descriptor_handle fd, std::span<std::byte> buf) noexcept
{
  if (auto rv = ::read(fd, buf.data(), buf.size_bytes());
      rv == -1)
    return std::error_code(errno, std::system_category());
  else
    return static_cast<std::size_t>(rv);
}

expected<std::size_t> write(file_descriptor_handle fd, std::span<const std::byte> buf) noexcept
{
  if (auto rv = ::write(fd, buf.data(), buf.size_bytes());
      rv == -1)
    return std::error_code(errno, std::system_category());
  else
    return static_cast<std::size_t>(rv);
}

expected<std::span<std::byte>> read_some(file_descriptor_handle fd, std::span<std::byte> buf) noexcept
{
  if (auto rv = read(fd, buf);
      !rv)
    return rv.error();
  else
    return buf.first(*rv);
}

expected<std::span<const std::byte>> write_some(file_descriptor_handle fd, std::span<const std::byte> buf) noexcept
{
  if (auto rv = write(fd, buf);
      !rv)
    return rv.error();
  else
    return buf.subspan(*rv);
}

template <typename... Args>
expected<int> fcntl(file_descriptor_handle fd, int cmd, Args&&... args) noexcept
{
  if (auto rv = ::fcntl(fd, cmd, std::forward<Args>(args)...); rv == -1)
    return std::error_code(errno, std::system_category());
  else
    return rv;
}

expected<int> fcntl_get_file_status_flags(file_descriptor_handle fd) noexcept
{
  return fcntl(fd, F_GETFL);
}

expected<void> fcntl_set_file_status_flags(file_descriptor_handle fd, int flags) noexcept
{
  return fcntl(fd, F_SETFL, flags);
}

expected<file_descriptor_handle> socket(int domain, int type, int protocol = 0) noexcept
{
  if (file_descriptor_handle rv(::socket(domain, type, protocol)); !rv)
    return std::error_code(errno, std::system_category());
  else
    return rv;
}

expected<void> connect(file_descriptor_handle fd, const struct ::sockaddr* addr, ::socklen_t addrlen) noexcept
{
  if (auto rv = ::connect(fd, addr, addrlen); rv == -1)
    return std::error_code(errno, std::system_category());
  else
    return {};
}

enum class shutdown_how : int
{
  read = SHUT_RD,
  write = SHUT_WR,
  read_write = SHUT_RDWR,
};

expected<void> shutdown(file_descriptor_handle fd, shutdown_how how) noexcept
{
  if (auto rv = ::shutdown(fd, std::to_underlying(how)); rv == -1)
    return std::error_code(errno, std::system_category());
  else
    return {};
}

expected<unsigned> select(unsigned nfds, ::fd_set* readfds, ::fd_set* writefds, ::fd_set* exceptfds, struct ::timeval* timeout = nullptr) noexcept
{
  if (nfds > static_cast<unsigned>(std::numeric_limits<int>::max()))
    return std::make_error_code(std::errc::invalid_argument);

  if (nfds > FD_SETSIZE)
    return std::make_error_code(std::errc::bad_file_descriptor);

  if (auto rv = ::select(static_cast<int>(nfds), readfds, writefds, exceptfds, timeout); rv < 0)
    return std::error_code(errno, std::system_category());
  else
    return rv;
}

enum class sol_socket : int
{
  accept_connections = SO_ACCEPTCONN,
  broadcast = SO_BROADCAST,
  keep_alive = SO_KEEPALIVE,
  reuse_addr = SO_REUSEADDR,
  type = SO_TYPE, // RAW|STREAM|DGRAM
  error = SO_ERROR,
  receive_buffer_size = SO_RCVBUF,
  send_buffer_size = SO_SNDBUF,
  linger = SO_LINGER,
};

enum class sol_ip_tcp : int
{
  fastopen = TCP_FASTOPEN,
  fastopen_connect = TCP_FASTOPEN_CONNECT,
};

namespace detail
{
template <typename T>
concept Enum = std::is_enum<T>::value;

template <Enum Level>
struct socket_opt_level {};

template <>
struct socket_opt_level<sol_socket>
{
  static constexpr int level = SOL_SOCKET;
};

template <>
struct socket_opt_level<sol_ip_tcp>
{
  static constexpr int level = IPPROTO_TCP;
};

template <Enum auto Opt>
struct socket_opt
{
  static constexpr auto level = socket_opt_level<decltype(Opt)>::level;
  static constexpr auto name = std::to_underlying(Opt);
  // Defaulting to 'int' because almost every option is an int
  using type = int;
  using return_type = type;
};

template <>
struct socket_opt<sol_socket::error>
{
  static constexpr auto level = socket_opt_level<sol_socket>::level;
  static constexpr int name = std::to_underlying(sol_socket::error);
  using type = int;
  using return_type = std::error_code;

  static constexpr return_type transform(type val) noexcept
  {
    return return_type(val, std::system_category());
  }
};

template <>
struct socket_opt<sol_socket::linger>
{
  static constexpr auto level = socket_opt_level<sol_socket>::level;
  static constexpr auto name = std::to_underlying(sol_socket::linger);
  using type = struct ::linger;
  using return_type = type;
};

template <>
struct socket_opt<sol_ip_tcp::fastopen_connect>
{
  static constexpr auto level = socket_opt_level<sol_ip_tcp>::level;
  static constexpr auto name = std::to_underlying(sol_ip_tcp::fastopen_connect);
  using type = int;
  using return_type = bool;
};
}  // namespace detail

expected<std::span<std::byte>> getsockopt(file_descriptor_handle fd, int level, int optname, std::span<std::byte> optval) noexcept
{
  ::socklen_t optlen = optval.size_bytes();
  if (auto rv = ::getsockopt(fd, level, optname, optval.data(), &optlen); rv == -1)
    return std::error_code(errno, std::system_category());
  else
    return optval.first(optlen);
}

template <detail::Enum auto optname>
expected<typename detail::socket_opt<optname>::return_type> getsockopt(file_descriptor_handle fd) noexcept
{
  using opt = detail::socket_opt<optname>;
  typename opt::type optval;
  if (auto rv = getsockopt(fd, opt::level, opt::name, as_writable_bytes(std::span(&optval, 1)));
      !rv)
    return unexpected(rv.error());
  else if (rv->size() != sizeof(optval))
    return unexpected(std::make_error_code(std::errc::invalid_argument));

  if constexpr (std::is_same_v<typename opt::type, typename opt::return_type>)
    return optval;
  else
    return opt::transform(std::move(optval));
}

expected<void> setsockopt(file_descriptor_handle fd, int level, int optname, std::span<const std::byte> optval) noexcept
{
  if (auto rv = ::setsockopt(fd, level, optname, optval.data(), optval.size_bytes()); rv == -1)
    return std::error_code(errno, std::system_category());
  else
    return {};
}

template <detail::Enum auto optname>
expected<void> setsockopt(file_descriptor_handle fd, typename detail::socket_opt<optname>::return_type const val) noexcept
{
  using opt = detail::socket_opt<optname>;
  const std::conditional_t<
      std::is_same_v<typename opt::type, typename opt::return_type>
    , const typename opt::type&
    , typename opt::type
    > optval(val);
  return setsockopt(fd, opt::level, opt::name, as_bytes(std::span(&optval, 1)));
}
}  // namespace io

template <typename T>
class future;

template <typename...>
struct is_future : std::false_type {};

template <typename T>
struct is_future<future<T>> : std::true_type {};

template <typename T>
inline constexpr bool is_future_v = is_future<T>::value;

class file_descriptor
{
  public:
    file_descriptor() = default;

    constexpr file_descriptor(io::file_descriptor_handle fd) noexcept
      : _fd(fd)
    {
    }

    virtual ~file_descriptor()
    {
      close();
    }

    file_descriptor(file_descriptor&& rhs) noexcept
      : _fd(rhs.release())
    {
    }

    file_descriptor& operator=(file_descriptor&& rhs) noexcept
    {
      if (&rhs != this)
      {
        close();
        _fd = rhs.release();
      }
      return *this;
    }

    void close() noexcept
    {
      if (_fd)
      {
        ::close(_fd);
        _fd = nullptr;
      }
    }

    constexpr explicit operator bool() const noexcept
    {
      return static_cast<bool>(_fd);
    }

    constexpr io::file_descriptor_handle handle() const noexcept
    {
      return _fd;
    }

    constexpr io::file_descriptor_handle release() noexcept
    {
      return std::exchange(_fd, nullptr);
    }

    future<std::span<std::byte>> read_some(std::span<std::byte> buf) noexcept;
    future<std::span<const std::byte>> write_some(std::span<const std::byte> buf) noexcept;

    future<std::span<std::byte>> read(std::span<std::byte> buf) noexcept;
    future<void> write(std::span<const std::byte> buf) noexcept;

  private:
    io::file_descriptor_handle _fd;
};

// used to register coroutines waiting for events and wait for all those events
class io_poll_context
{
  public:
    io_poll_context() = default;

    io_poll_context(io_poll_context&& rhs) = default;

    constexpr io_poll_context& operator=(io_poll_context&& rhs) noexcept
    {
      if (&rhs == this)
        return *this;

      for (auto& [_, event] : _polled_events)
      {
        event->wait_result = unexpected(std::make_error_code(std::errc::operation_canceled));
        // Don't know if this is necessary. But hopefully better than leaving them dangling or calling destroy()?
        _to_resume.emplace_back(event->waiter);
      }

      _polled_events = std::move(rhs._polled_events);
      _to_resume.insert(_to_resume.end(), begin(rhs._to_resume), end(rhs._to_resume));
      rhs._to_resume.clear();

      return *this;
    }

    void wait_for(io::poll::awaitable& event)
    {
      if (event.fd < 0 && event.fd >= FD_SETSIZE && !event.timeout)
      {
        // Because we're using select() which has a very limited range of acceptable file descriptors (usually [0:1024))
        event.wait_result = unexpected(std::make_error_code(std::errc::bad_file_descriptor));
        _to_resume.emplace_back(event.waiter);
        return;
      }

      if (event.timeout && *event.timeout < std::decay_t<decltype(*event.timeout)>::clock::now())
      {
        event.wait_result = unexpected(std::make_error_code(std::errc::timed_out));
        _to_resume.emplace_back(event.waiter);
        return;
      }

      _polled_events.emplace(event.fd, &event);
    }

    std::error_code run_one()
    {
      ////std::string_view func_name(__PRETTY_FUNCTION__);
      ////func_name = func_name.substr(func_name.find("run_one"));

      unsigned i = 0;
      while (!_to_resume.empty())
      {
        auto waiter = _to_resume.front();
        _to_resume.pop_front();
        ////std::format_to(std::ostreambuf_iterator(std::cout), "{:>7} {:4}: {:128.128}[{}]resume(waiter={}))\n", ts(), __LINE__, func_name, i++, waiter.address());
        waiter.resume();
      }

      if (_polled_events.empty())
        return {};

      fd_set readfds, writefds, exceptfds;
      FD_ZERO(&readfds);
      FD_ZERO(&writefds);
      FD_ZERO(&exceptfds);
      unsigned nfds = 0;
      decltype(_polled_events.begin()->second->timeout) timeout;

      ////const auto now = std::decay_t<decltype(*timeout)>::clock::now();
      i = 0;
      for (const auto& [fd, handler] : _polled_events)
      {
        ////std::format_to(std::ostreambuf_iterator(std::cout), "{:>7} {:4}: {:128.128}[{}](event@{}=({}, fd={}, timeout={}, waiter={}))\n", ts(), __LINE__, func_name, i++, static_cast<const void*>(handler), handler->events, handler->fd, handler->timeout.transform([&] (auto time) { return std::chrono::duration_cast<std::chrono::microseconds>(time - now); }), handler->waiter.address());

        if (handler->timeout)
        {
          if (timeout)
            timeout = std::min(*timeout, *handler->timeout);
          else
            timeout = *handler->timeout;
        }

        if (!fd)
          continue;

        assert(fd < FD_SETSIZE);
        if (std::to_underlying(handler->events & io::poll_event::read))
        {
          FD_SET(fd, &readfds);
          nfds = std::max(nfds, static_cast<unsigned>(fd + 1));
        }
        if (std::to_underlying(handler->events & io::poll_event::write))
        {
          FD_SET(fd, &writefds);
          nfds = std::max(nfds, static_cast<unsigned>(fd + 1));
        }
        if (std::to_underlying(handler->events & io::poll_event::priority))
        {
          FD_SET(fd, &exceptfds);
          nfds = std::max(nfds, static_cast<unsigned>(fd + 1));
        }
      }

      struct ::timeval tv;
      const auto tvp = [&]() -> decltype(&tv) {
        if (!timeout)
          return nullptr;
        const auto now = std::decay_t<decltype(*timeout)>::clock::now();
        if (*timeout < now)
        {
          tv.tv_sec = 0;
          tv.tv_usec = 0;
        }
        else
        {
          const auto time_left = std::chrono::duration_cast<std::chrono::microseconds>(*timeout - now);
          tv.tv_sec = time_left.count() / 1000000L;
          tv.tv_usec = time_left.count() % 1000000L;
        }
        return &tv;
      }();

      if (const auto r = io::select(nfds, nfds ? &readfds : nullptr, nfds ? &writefds : nullptr, nfds ? &exceptfds : nullptr, tvp); !r)
      {
        return r.error();
      }
      else
      {
        const auto now = timeout
          ? std::decay_t<decltype(*timeout)>::clock::now()
          : std::decay_t<decltype(*timeout)>();

        const auto last = _polled_events.end();
        unsigned idx = 0;
        for (auto i = _polled_events.begin(),
              next = (i != last) ? std::next(i) : last;
            i != last;
            i = (next != last) ? next++ : next)
        {
          const auto fd = i->first;
          auto& handler = *i->second;
          idx++;

          if (*r == 0) // timeout occurred
          {
            if (!handler.timeout || now < *handler.timeout)
              continue;

            ////std::format_to(std::ostreambuf_iterator(std::cout), "{:>7} {:4}: {:128.128}[{}](event@{}=({}, fd={}, timeout={}, waiter={}))\n", ts(), __LINE__, func_name, idx - 1, static_cast<const void*>(&handler), handler.events, handler.fd, handler.timeout.transform([&] (auto time) { return std::chrono::duration_cast<std::chrono::microseconds>(time - now); }), handler.waiter.address());
            handler.wait_result = unexpected(std::make_error_code(std::errc::timed_out));
            _to_resume.emplace_back(std::exchange(handler.waiter, nullptr));
            _polled_events.erase(i);

            continue;
          }

          if (!fd)
            continue;

          if (!(std::to_underlying(handler.events & io::poll_event::read    ) && FD_ISSET(fd, &readfds))
           && !(std::to_underlying(handler.events & io::poll_event::write   ) && FD_ISSET(fd, &writefds))
           && !(std::to_underlying(handler.events & io::poll_event::priority) && FD_ISSET(fd, &exceptfds)))
            continue;

          ////std::format_to(std::ostreambuf_iterator(std::cout), "{:>7} {:4}: {:128.128}[{}](event@{}=({}, fd={}, timeout={}, waiter={}))\n", ts(), __LINE__, func_name, idx - 1, static_cast<const void*>(&handler), handler.events, handler.fd, handler.timeout.transform([&] (auto time) { return std::chrono::duration_cast<std::chrono::microseconds>(time - now); }), handler.waiter.address());
          handler.wait_result.emplace(); // no polling error (may be an error event but that's for checking downstream)
          _to_resume.emplace_back(std::exchange(handler.waiter, nullptr));
          _polled_events.erase(i);
        }
      }

      i = 0;
      while (!_to_resume.empty())
      {
        auto waiter = std::move(_to_resume.front());
        _to_resume.pop_front();
        ////std::format_to(std::ostreambuf_iterator(std::cout), "{:>7} {:4}: {:128.128}[{}]resume(waiter={}))\n", ts(), __LINE__, func_name, i++, waiter.address());
        waiter.resume();
      }

      return {};
    }

    std::error_code run()
    {
      while (!empty())
      {
        if (auto error = run_one())
          return error;
      }

      return {};
    }

    constexpr bool empty() const noexcept
    {
      return _polled_events.empty() && _to_resume.empty();
    }

  private:
    std::unordered_multimap<io::file_descriptor_handle, io::poll::awaitable*> _polled_events;
    std::deque<std::coroutine_handle<>> _to_resume;
};

template <typename T>
class promise;

template <typename T>
class future
{
  public:
    using value_type = expected<T>;
    using promise_type = promise<T>;

    future(future&& rhs) noexcept
      : handle(std::exchange(rhs.handle, nullptr))
    {
    }

    future& operator=(future&& rhs) noexcept
    {
      if (&rhs != this)
      {
        destroy();
        handle = std::exchange(rhs.handle, nullptr);
      }

      return *this;
    }

    ~future()
    {
      destroy();
    }

    constexpr explicit operator bool() const noexcept
    {
      return static_cast<bool>(handle);
    }

    bool done() const
    {
      return handle && handle.done();
    }

    void destroy()
    {
      if (handle)
      {
        handle.destroy();
        handle = nullptr;
      }
    }

    expected<T> get(io_poll_context& executor) noexcept
    {
      ////std::string_view func_name(__PRETTY_FUNCTION__);
      ////func_name = func_name.substr(func_name.find("get"));

      assert(handle);

      auto& promise = handle.promise();

      unsigned i = 0;

      while (!handle.done())
      {
        unsigned j = 0;

        assert(!promise.events.empty() || !executor.empty());

        ////const auto now = std::decay_t<decltype(*promise.events.front()->timeout)>::clock::now();
        for (const auto event : promise.events)
        {
          ////std::format_to(std::ostreambuf_iterator(std::cout), "{:>7} {:4}: {:128.128}[{},{}](root={}, events@{}, event@{}=({}, fd={}, timeout={}, waiter={}))\n", ts(), __LINE__, func_name, i++, j++, handle.address(), static_cast<const void*>(&promise.events), static_cast<const void*>(event), event->events, event->fd, event->timeout.transform([&] (auto time) { return std::chrono::duration_cast<std::chrono::microseconds>(time - now); }), event->waiter.address());
          executor.wait_for(*event);
        }
        promise.events.clear();

        if (auto err = executor.run_one(); err)
          return unexpected(err);
      }

      assert(promise.returned_value);
      expected<T> rv(std::move(*promise.returned_value));
      destroy();
      return rv;
    }

    expected<T> get() noexcept
    {
      io_poll_context ctx;
      return get(ctx);
    }

    constexpr bool await_ready() const noexcept
    {
      return !handle || handle.done();
    }

    void await_suspend(std::coroutine_handle<> suspended)
    {
      auto& promise = handle.promise();
      assert(promise.waits_on_me == nullptr && "only a single coroutine should have a future for this coroutine to co_await on!");
      promise.waits_on_me = suspended;
    }

    constexpr expected<T> await_resume() noexcept
    {
      assert(handle);
      assert(handle.done());
      auto& promise = handle.promise();

      assert(promise.returned_value);
      expected<T> rv(std::move(*promise.returned_value));
      destroy();
      return rv;
    }

  private:
    template <typename U>
    friend class promise;
    template <typename... Ts>
    friend future<std::tuple<expected<Ts>...>> when_all(future<Ts>... futures) noexcept;
    template <std::forward_iterator I, std::sentinel_for<I> S>
    requires(is_future_v<typename std::iterator_traits<I>::value_type>)
    friend future<std::vector<typename std::iterator_traits<I>::value_type::value_type>> when_all(I first, S last) noexcept;

    constexpr future(std::coroutine_handle<promise<T>> handle) noexcept
      : handle(handle)
    {
    }

    std::coroutine_handle<promise<T>> handle;
};

namespace detail
{
struct promise_wait_callgraph
{
  promise_wait_callgraph* root_caller;
  std::deque<promise_wait_callgraph*> callees;
  std::coroutine_handle<> waits_on_me;
  std::deque<io::poll::awaitable*> events;

  constexpr promise_wait_callgraph() noexcept
    : root_caller(this)
  {
  }

  ~promise_wait_callgraph()
  {
    std::erase(root_caller->callees, this);
  }

  promise_wait_callgraph(promise_wait_callgraph&&) = delete;
  promise_wait_callgraph& operator=(promise_wait_callgraph&&) = delete;
};

constexpr void push_back(std::coroutine_handle<promise_wait_callgraph> waiter, io::poll::awaitable& event) noexcept
{
  waiter.promise().root_caller->events.push_back(&event);
}
}

class my_current_promise
{
  private:
    my_current_promise() = default;

    // Only friends are allowed to know the promise they run in
    template <typename... Ts>
    friend future<std::tuple<expected<Ts>...>> when_all(future<Ts>... futures) noexcept;
    template <std::forward_iterator I, std::sentinel_for<I> S>
    requires(is_future_v<typename std::iterator_traits<I>::value_type>)
    friend future<std::vector<typename std::iterator_traits<I>::value_type::value_type>> when_all(I first, S last) noexcept;
};

template <typename T>
class promise : private detail::promise_wait_callgraph
{
  public:
    future<T> get_return_object() { return future<T>(std::coroutine_handle<promise>::from_promise(*this)); }
    constexpr std::suspend_never initial_suspend() noexcept { return {}; }
    struct final_resume_waiter
    {
      std::coroutine_handle<> waiter;

      constexpr bool await_ready() const noexcept
      {
        return false;
      }

      constexpr void await_resume() const noexcept
      {
      }

      constexpr std::coroutine_handle<> await_suspend(std::coroutine_handle<> suspended) noexcept
      {
        return std::exchange(waiter, nullptr);
      }
    };
    constexpr final_resume_waiter final_suspend() noexcept
    {
      auto waiter = std::exchange(waits_on_me, nullptr);
      if (!waiter)
        waiter = std::noop_coroutine();
      return {waiter};
    }
    constexpr void unhandled_exception() noexcept
    {
      if constexpr (is_expected_with_std_error_code_v<T>)
      {
        try
        {
          throw;
        }
        catch (std::bad_expected_access<std::error_code>& exc)
        {
          returned_value = exc.error();
        }
        catch (std::system_error& exc)
        {
          returned_value = exc.code();
        }
      }
      else
      {
        std::terminate();
      }
    }

    constexpr void return_value(expected<T>&& v) noexcept(std::is_nothrow_move_constructible_v<T>)
    {
      // NOTE: MUST use assignment (instead of .emplace()) to be safe in case of self-assignment
      returned_value = std::move(v);
    }

    template <typename U>
    constexpr future<U>&& await_transform(future<U>&& fut) noexcept
    {
      ////std::string_view func_name(__PRETTY_FUNCTION__);
      ////func_name = func_name.substr(func_name.find("await_transform"));

      if (detail::promise_wait_callgraph* const callee_promise = fut.handle ? &fut.handle.promise() : nullptr)
      {
        if (std::ranges::find(callees, callee_promise) == callees.end())
          callees.push_back(callee_promise);

        auto& events_queue = root_caller->events;

        for (std::size_t i = 0; i < callees.size(); ++i)
        {
          // Recurse into grand children
          {
            auto grand_children = std::move(callees[i]->callees);
            callees.insert(callees.end(), begin(grand_children), end(grand_children));
          }

          const auto& child = callees[i];

          child->root_caller = this;
          ////std::format_to(std::ostreambuf_iterator(std::cout), "{:>7} {:4}: {:128.128}[{}](child={}, child->root={}\n", ts(), __LINE__, func_name, i, std::coroutine_handle<promise>::from_promise(*reinterpret_cast<promise*>(child)).address(), std::coroutine_handle<promise>::from_promise(*reinterpret_cast<promise*>(child->root_caller)).address());

          // Steal all events from all child futures
          if (events_queue.empty())
          {
            events_queue = std::move(child->events);
          }
          else
          {
            // move entire container to ensure memory is recovered at scope exit
            auto moved_events = std::move(child->events);
            events_queue.insert(events_queue.end(), begin(moved_events), end(moved_events));
          }
        }
      }
      ////std::format_to(std::ostreambuf_iterator(std::cout), "{:>7} {:4}: {:128.128}(root={}, me={})\n", ts(), __LINE__, func_name, std::coroutine_handle<promise>::from_promise(*static_cast<promise*>(root_caller)).address(), std::coroutine_handle<promise>::from_promise(*this).address());
      return std::move(fut);
    }

    template <typename MaybeAwaitable>
    requires(!std::is_same_v<std::decay_t<MaybeAwaitable>, io::poll>)
    constexpr MaybeAwaitable&& await_transform(MaybeAwaitable&& obj) noexcept
    {
      return std::forward<MaybeAwaitable>(obj);
    }

    constexpr auto await_transform(io::poll&& obj) noexcept
    {
      ////std::string_view func_name(__PRETTY_FUNCTION__);
      ////func_name = func_name.substr(func_name.find("await_transform"));
      ////std::format_to(std::ostreambuf_iterator(std::cout), "{:>7} {:4}: {:128.128}(root={}, me={})\n", ts(), __LINE__, func_name, std::coroutine_handle<promise>::from_promise(*static_cast<promise*>(root_caller)).address(), std::coroutine_handle<promise>::from_promise(*this).address());
      return io::poll::awaitable(std::move(obj), std::coroutine_handle<detail::promise_wait_callgraph>::from_promise(*this));
    }

    struct promise_retriever
    {
      promise& p;

      constexpr bool await_ready() const noexcept
      {
        return true;
      }

      constexpr auto await_suspend(std::coroutine_handle<> suspended) noexcept
      {
        return suspended;
      }

      constexpr promise& await_resume() const noexcept
      {
        return p;
      }
    };

    constexpr auto await_transform(my_current_promise&&) noexcept
    {
      return promise_retriever{*this};
    }

    template <typename U>
    friend class promise;
    friend class future<T>;
    template <typename... Ts>
    friend future<std::tuple<expected<Ts>...>> when_all(future<Ts>... futures) noexcept;
    template <std::forward_iterator I, std::sentinel_for<I> S>
    requires(is_future_v<typename std::iterator_traits<I>::value_type>)
    friend future<std::vector<typename std::iterator_traits<I>::value_type::value_type>> when_all(I first, S last) noexcept;

  private:
    std::optional<expected<T>> returned_value;
};

template <typename... Ts>
future<std::tuple<expected<Ts>...>> when_all(future<Ts>... futures) noexcept
{
  ////std::string_view func_name(__PRETTY_FUNCTION__);
  ////func_name = func_name.substr(func_name.find("when_all"));

  auto& my_promise = co_await my_current_promise();
  assert(my_promise.events.empty());
  assert(my_promise.root_caller == &my_promise);

  // Force promise.await_transform(await-expr) to be executed for all futures *before* suspending execution of *this* coroutine when invoking co_await.
  // Unfortunately whether the co_await pack expansion executes in this order or once per future just before suspending for each future is implementation-defined. So we need this hack...
  (my_promise.await_transform(std::move(futures)), ...);

  // Now allow this future's .get() to handle the actual I/O multiplexing
  co_return std::tuple<expected<Ts>...>((co_await futures)...);
}

template <std::forward_iterator I, std::sentinel_for<I> S>
requires(is_future_v<typename std::iterator_traits<I>::value_type>)
future<std::vector<typename std::iterator_traits<I>::value_type::value_type>> when_all(I first, S last) noexcept
{
  ////std::string_view func_name(__PRETTY_FUNCTION__);
  ////func_name = func_name.substr(func_name.find("when_all"));

  std::vector<typename std::iterator_traits<I>::value_type::value_type> rv;

  auto& my_promise = co_await my_current_promise();

  // Force promise.await_transform(await-expr) to be executed for all futures *before* suspending execution of *this* coroutine when invoking co_await.
  std::size_t count = 0;
  for (auto i = first; i != last; ++i, ++count)
    *i = my_promise.await_transform(std::move(*i));

  rv.reserve(count);
  // Now allow this future's .get() to handle the actual I/O multiplexing while collecting the results
  for (; first != last; ++first)
    rv.emplace_back(co_await *first);

  co_return rv;
}

template <std::ranges::forward_range R>
requires(std::is_rvalue_reference_v<R&&>)
auto when_all(R&& futures) noexcept
{
  using std::ranges::begin;
  using std::ranges::end;
  return when_all(begin(futures), end(futures));
}

future<void> sleep_until(io::poll::timeout_clock::time_point time) noexcept
{
  ////auto timeout = time - io::poll::timeout_clock::now();
  ////std::format_to(std::ostreambuf_iterator(std::cout), "{:>7} {:4}: {:128.128}({}@{})\n", ts(), __LINE__, "sleep_until", std::chrono::duration_cast<std::chrono::milliseconds>(timeout), std::chrono::duration_cast<std::chrono::milliseconds>(time.time_since_epoch()));

  if (auto r = co_await io::poll(time);
      !r && r.error() != std::errc::timed_out)
    co_return r;
  else
    co_return {};
}

future<void> sleep(io::poll::timeout_clock::duration time) noexcept
{
  return sleep_until(time + io::poll::timeout_clock::now());
}

inline future<std::span<std::byte>> file_descriptor::read_some(std::span<std::byte> buf) noexcept
{
  const auto fd = handle();
  ////std::format_to(std::ostreambuf_iterator(std::cout), "{:>7} {:4}: {:128.128}(fd={}, buf=<size={}>)\n", ts(), __LINE__, "file_descriptor::read_some", fd, buf.size());

  if (auto rv = io::read_some(fd, buf);
      rv || rv.error() != io::error::operation_not_ready)
    co_return rv;

  co_return (
      co_await io::poll(fd, io::poll_event::read)
    ).and_then([=] { return io::read_some(fd, buf); });
}

inline future<std::span<const std::byte>> file_descriptor::write_some(std::span<const std::byte> buf) noexcept
{
  const auto fd = handle();
  ////std::format_to(std::ostreambuf_iterator(std::cout), "{:>7} {:4}: {:128.128}(fd={}, buf=<size={}>)\n", ts(), __LINE__, "file_descriptor::write_some", fd, buf.size());

  if (auto rv = io::write_some(fd, buf);
      rv || rv.error() != io::error::operation_not_ready)
    co_return rv;

  co_return (
      co_await io::poll(fd, io::poll_event::write)
    ).and_then([=] { return io::write_some(fd, buf); });
}

inline future<std::span<std::byte>> file_descriptor::read(std::span<std::byte> const buf) noexcept
{
  const auto fd = handle();
  ////std::format_to(std::ostreambuf_iterator(std::cout), "{:>7} {:4}: {:128.128}(fd={}, buf=<size={}>)\n", ts(), __LINE__, "file_descriptor::read", fd, buf.size());
  std::size_t read_so_far = 0;

  if (auto rv = io::read(fd, buf);
      !rv && rv.error() != io::error::operation_not_ready)
    co_return rv.error();
  else if (rv)
    read_so_far += *rv;

  while (read_so_far < buf.size())
  {
    if (auto wait = co_await io::poll(fd, io::poll_event::read); !wait)
      co_return wait.error();

    if (auto rv = io::read(fd, buf.subspan(read_so_far)); !rv)
      co_return rv.error();
    else if (*rv == 0)
      co_return buf.first(read_so_far);
    else
      read_so_far += *rv;
  }

  co_return buf;
}

inline future<void> file_descriptor::write(std::span<const std::byte> buf) noexcept
{
  const auto fd = handle();
  ////std::format_to(std::ostreambuf_iterator(std::cout), "{:>7} {:4}: {:128.128}(fd={}, buf=<size={}>)\n", ts(), __LINE__, "file_descriptor::write", fd, buf.size());

  if (auto rv = io::write_some(fd, buf);
      !rv && rv.error() != io::error::operation_not_ready)
    co_return rv;
  else if (rv)
    buf = *rv;

  while (!buf.empty())
  {
    if (auto wait = co_await io::poll(fd, io::poll_event::write); !wait)
      co_return wait;

    if (auto rv = io::write_some(fd, buf); !rv)
      co_return rv;
    else
      buf = *rv;
  }

  co_return {};
}

class socket_descriptor : public file_descriptor
{
  public:
    using file_descriptor::file_descriptor;
};

class stream_socket : public socket_descriptor
{
  public:
    using socket_descriptor::socket_descriptor;

    static expected<stream_socket> create(int domain, int protocol = 0) noexcept
    {
      ////std::format_to(std::ostreambuf_iterator(std::cout), "{:>7} {:4}: {:.128}\n", ts(), __LINE__, "stream_socket::create");

      constexpr int sock_open_non_block = 0
#if __linux__ || __FreeBSD__ || __NetBSD__ || __OpenBSD__
        // Some OSs allow us to create non-blocking sockets with a single syscall
        | SOCK_NONBLOCK
#endif
      ;

      return io::socket(domain, SOCK_STREAM | sock_open_non_block, protocol)
        .transform([] (auto fd) { return stream_socket(fd); })
        .and_then([] (auto sock) {
          if constexpr (!sock_open_non_block)
            return io::fcntl_get_file_status_flags(sock.handle())
              .and_then([&] (auto flags) { return io::fcntl_set_file_status_flags(sock.handle(), flags | O_NONBLOCK); })
              .transform([&] { return std::move(sock); })
              ;
          else
            return expected<stream_socket>(std::move(sock));
        })
      ;
    }

    future<void> connect(const ::sockaddr* addr, std::size_t addrlen) noexcept
    {
      ////std::format_to(std::ostreambuf_iterator(std::cout), "{:>7} {:4}: {:.128}\n", ts(), __LINE__, "stream_socket::connect");

      const auto fd = handle();

      if (auto rv = io::connect(fd, addr, addrlen);
          rv || rv.error() != io::error::operation_not_ready)
        co_return rv;

      if (auto wait = co_await io::poll(handle(), io::poll_event::write); !wait)
        co_return wait;

      if (auto connect_result = io::getsockopt<io::sol_socket::error>(handle());
          !connect_result || *connect_result)
        co_return connect_result ? *connect_result : connect_result.error();

      co_return {};
    }

    expected<void> shutdown(io::shutdown_how how) noexcept
    {
      return io::shutdown(handle(), how);
    }

  private:
};
}  // namespace olifilo

class mqtt
{
  public:
    template <std::output_iterator<std::byte> Out>
    Out serialize_remaining_length(Out out, std::uint32_t value)
    {
      *out++ = static_cast<std::byte>(value & 0x7f);

      value >>= 7;
      while (value)
      {
        *out++ = static_cast<std::byte>((value & 0x7f) | 0x80);
        value >>= 7;
      }

      return out;
    }

    enum class packet_t : std::uint8_t
    {
      connect     =  1,
      connack     =  2,
      publish     =  3,
      puback      =  4,
      pubrec      =  5,
      pubrel      =  6,
      pubcomp     =  7,
      subscribe   =  8,
      suback      =  9,
      unsubscribe = 10,
      unsuback    = 11,
      pingreq     = 12,
      pingresp    = 13,
      disconnect  = 14,
    };

    std::chrono::duration<std::uint16_t> keep_alive{15};

    static olifilo::future<mqtt> connect(const char* ipv6, uint16_t port, std::uint8_t id) noexcept
    {
      mqtt con;
      {
        sockaddr_in6 addr {
          .sin6_family = AF_INET6,
          .sin6_port = htons(port),
        };
        if (auto r = inet_pton(addr.sin6_family, ipv6, &addr.sin6_addr);
            r == -1)
          co_return std::error_code(errno, std::system_category());
        else if (r == 0)
          co_return std::make_error_code(std::errc::invalid_argument);

        if (auto r = olifilo::stream_socket::create(addr.sin6_family))
          con._sock = std::move(*r);
        else
          co_return r.error();

        if (auto r = co_await con._sock.connect(reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)); !r)
          co_return r.error();
      }

      con.keep_alive = decltype(con.keep_alive)(con.keep_alive.count() << (id & 1));

      std::uint8_t connect_pkt[27];
      connect_pkt[0] = std::to_underlying(packet_t::connect) << 4;
      connect_pkt[1] = 25;

      // protocol name
      connect_pkt[2] = 0;
      connect_pkt[3] = 4;
      connect_pkt[4] = 'M';
      connect_pkt[5] = 'Q';
      connect_pkt[6] = 'T';
      connect_pkt[7] = 'T';

      // protocol level
      connect_pkt[8] = 4;

      // connect flags
      connect_pkt[9] = 0x02 /* want clean session */;

      // keep alive (seconds, 16 bit big endian)
      connect_pkt[10] = con.keep_alive.count() >> 8;
      connect_pkt[11] = con.keep_alive.count() & 0xff;

      // client ID
      connect_pkt[12] = 0;
      connect_pkt[13] = 13;
      connect_pkt[14] = 'c';
      connect_pkt[15] = 'p';
      connect_pkt[16] = 'p';
      connect_pkt[17] = '2' + (id / 10 % 10);
      connect_pkt[18] = '0' + (id % 10);
      connect_pkt[19] = 'c';
      connect_pkt[20] = 'o';
      connect_pkt[21] = 'r';
      connect_pkt[22] = 'o';
      connect_pkt[23] = 'm';
      connect_pkt[24] = 'q';
      connect_pkt[25] = 't';
      connect_pkt[26] = 't';

      // send CONNECT command
      if (auto r = co_await con._sock.write(as_bytes(std::span(connect_pkt)));
          !r)
        co_return r.error();

      std::memset(connect_pkt, 0, sizeof(connect_pkt));

#if 1
      if (auto r = co_await olifilo::io::poll(con._sock.handle(), olifilo::io::poll_event::read); !r)
        co_return r.error();
#endif

      // expect CONNACK
      auto ack_pkt = co_await con._sock.read(as_writable_bytes(std::span(connect_pkt, 4)));
      if (!ack_pkt)
        co_return ack_pkt.error();
      else if (ack_pkt->size() != 4)
        co_return std::make_error_code(std::errc::connection_aborted);

      if (static_cast<packet_t>(static_cast<std::uint8_t>((*ack_pkt)[0]) >> 4) != packet_t::connack) // Check CONNACK message type
        co_return std::make_error_code(std::errc::bad_message);

      if (static_cast<std::uint8_t>((*ack_pkt)[1]) != 2) // variable length header portion must be exactly 2 bytes
        co_return std::make_error_code(std::errc::bad_message);

      if (static_cast<std::uint8_t>((*ack_pkt)[2]) & 0x01 != 0) // session-present flag must be unset (i.e. we MUST NOT have a server-side session)
        co_return std::make_error_code(std::errc::bad_message);

      const auto connect_return_code = static_cast<std::uint8_t>((*ack_pkt)[3]);
      if (connect_return_code != 0)
        co_return std::error_code(connect_return_code, std::generic_category() /* mqtt::error_category() */);

      co_return con;
    }

    olifilo::future<void> disconnect() noexcept
    {
      char disconnect_pkt[2];
      disconnect_pkt[0] = std::to_underlying(packet_t::disconnect) << 4;
      disconnect_pkt[1] = 0;

      // send DISCONNECT command
      if (auto r = co_await this->_sock.write(as_bytes(std::span(disconnect_pkt)));
          !r)
        co_return r;

      if (auto r = this->_sock.shutdown(olifilo::io::shutdown_how::write); !r)
        co_return r;

#if 1
      if (auto r = co_await olifilo::io::poll(this->_sock.handle(), olifilo::io::poll_event::read); !r)
        co_return r;
#endif

      if (auto r = co_await this->_sock.read_some(as_writable_bytes(std::span(disconnect_pkt, 1)));
          !r)
        co_return r;
      else if (!r->empty())
        co_return std::make_error_code(std::errc::bad_message);

      this->_sock.close();
      co_return {};
    }

    olifilo::future<void> ping() noexcept
    {
      char ping_pkt[2];
      ping_pkt[0] = std::to_underlying(packet_t::pingreq) << 4;
      ping_pkt[1] = 0;

      // send PINGREQ command
      if (auto r = co_await this->_sock.write(as_bytes(std::span(ping_pkt)));
          !r)
        co_return r;

#if 1
      if (auto r = co_await olifilo::io::poll(this->_sock.handle(), olifilo::io::poll_event::read); !r)
        co_return r;
#endif

      // expect PINGRESP
      auto ack_pkt = co_await this->_sock.read(as_writable_bytes(std::span(ping_pkt)));
      if (!ack_pkt)
        co_return ack_pkt;
      else if (ack_pkt->size() != 2)
        co_return std::make_error_code(std::errc::connection_aborted);

      if (static_cast<packet_t>(static_cast<std::uint8_t>((*ack_pkt)[0]) >> 4) != packet_t::pingresp) // Check PINGRESP message type
        co_return std::make_error_code(std::errc::bad_message);

      if (static_cast<std::uint8_t>((*ack_pkt)[1]) != 0) // variable length header portion must be empty
        co_return std::make_error_code(std::errc::bad_message);

      co_return {};
    }

  private:
    olifilo::stream_socket _sock;
};

olifilo::future<void> do_mqtt(std::uint8_t id) noexcept
{
  using namespace std::literals::chrono_literals;

  ////std::format_to(std::ostreambuf_iterator(std::cout), "{:>7} {:4}: {:128.128}({})\n", ts(), __LINE__, "do_mqtt", id);

  auto r = co_await mqtt::connect("fdce:1234:5678::1", 1883, id);
  ////std::format_to(std::ostreambuf_iterator(std::cout), "{:>7} {:4}: {:128.128}({}) r = {}\n", ts(), __LINE__, "do_mqtt", id, static_cast<bool>(r));
  if (!r)
    co_return r;

  using clock = olifilo::io::poll::timeout_clock;
  const auto keep_alive_wait_time = std::chrono::duration_cast<clock::duration>(r->keep_alive) * 3 / 4;
  const auto start = clock::now() - ts();
  constexpr auto run_time = 120s;

  clock::time_point now;
  while ((now = clock::now()) - start < run_time)
  {
    const auto sleep_time = keep_alive_wait_time - (now - start) % keep_alive_wait_time;
    auto err = co_await olifilo::sleep_until(now + sleep_time);
    ////std::format_to(std::ostreambuf_iterator(std::cout), "{:>7} {:4}: {:128.128}({}) err = {}\n", ts(), __LINE__, "do_mqtt", id, (err ? std::error_code() : err.error()).message());
    if (!err)
      co_return err;

    if (clock::now() - start >= run_time)
      break;

    err = co_await r->ping();
    ////std::format_to(std::ostreambuf_iterator(std::cout), "{:>7} {:4}: {:128.128}({}) err = {}\n", ts(), __LINE__, "do_mqtt", id, (err ? std::error_code() : err.error()).message());
    if (!err)
      co_return err;
  }

  auto err = co_await r->disconnect();
  ////std::format_to(std::ostreambuf_iterator(std::cout), "{:>7} {:4}: {:128.128}({}) err = {}\n", ts(), __LINE__, "do_mqtt", id, (err ? std::error_code() : err.error()).message());
  co_return err;
}

int main()
{
  using olifilo::when_all;

#if 0
  if (auto r = do_mqtt(0).get();
      !r)
    throw std::system_error(r.error());
#else
  if (auto [r1, r2, rs] = when_all(
        do_mqtt(1)
      , do_mqtt(2)
      , when_all(std::array{
          do_mqtt(3),
          do_mqtt(4),
        })
      ).get().value();
      !r1 || !r2 || !rs)
  {
    throw std::system_error(!r1 ? r1.error() : !r2 ? r2.error() : rs.error());
  }
  else
  {
    for (auto& ri : *rs)
    {
      if (!ri)
        throw std::system_error(ri.error());
    }
  }
#endif
}
