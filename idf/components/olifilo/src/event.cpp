// SPDX-License-Identifier: GPL-3.0-or-later

#include <olifilo/idf/event.hpp>

#include <algorithm>
#include <cstring>
#include <iterator>

#include <olifilo/dynarray.hpp>
#include <olifilo/idf/errors.hpp>

#include <esp_event.h>
#include <esp_log.h>
#include <esp_vfs.h>
#include <esp_vfs_ops.h>

namespace olifilo::esp
{
static constexpr char TAG[] = "olifilo::esp::event";

std::array<events::fd_context, 5> events::contexts;

expected<void> event_subscription_default::destroy() noexcept
{
  if (!_subscription)
    return {unexpect, make_error_code(std::errc::invalid_argument)};

  return {unexpect, esp_event_handler_instance_unregister(_event_base, _event_id, _subscription), error_category()};
}

expected<event_subscription_default> event_subscription_default::create(
    ::esp_event_base_t event_base
  , std::int32_t event_id
  , ::esp_event_handler_t event_handler
  , void* event_handler_arg
  ) noexcept
{
  ::esp_event_handler_instance_t subscription;
  if (const auto status = ::esp_event_handler_instance_register(
        event_base
      , event_id
      , event_handler
      , event_handler_arg
      , &subscription
      ); status != ESP_OK)
    return {unexpect, status, error_category()};
  return event_subscription_default(event_base, event_id, subscription);
}

struct events::fd_waiter
{
  int                     fd      = -1;
  ::fd_set*               readers = nullptr;
  ::fd_set*               errors  = nullptr;
  ::esp_vfs_select_sem_t  waker;

  ~fd_waiter()
  {
    if (fd < 0)
      return;

    auto&& context = contexts[fd];
    std::scoped_lock _(context.lock);
    erase(context.waiters, this);
  }
};

events::fd_context::~fd_context()
{
  std::allocator<std::byte> alloc;
  waiters.destroy(alloc);
}

void events::fd_context::receive(this events::fd_context* self, esp_event_base_t event_base, std::int32_t event_id, void* event_data) noexcept
{
  ESP_LOGD(TAG, "fd[%zd@%p]::receive: received event %s:%ld(%p) [open=%u,max_size=%zu]"
      , self - contexts.begin(), self, event_base, event_id, event_data, self->opened, self->event_data_size);

  assert(self != nullptr);
  std::scoped_lock _(self->lock);
  if (!self->opened)
    return;

  const auto event_size = sizeof(::esp_event_base_t) + sizeof(std::int32_t) + self->event_data_size;
  assert(self->queue.size() % event_size == 0);
  self->queue.reserve(self->queue.size() + event_size);
  std::back_insert_iterator out(self->queue);
  out = std::ranges::copy(as_bytes(std::span(&event_base, 1)), out).out;
  out = std::ranges::copy(as_bytes(std::span(&event_id, 1)), out).out;
  if (event_data)
    out = std::ranges::copy(std::span(static_cast<const std::byte*>(event_data), self->event_data_size), out).out;
  else
    self->queue.resize(self->queue.size() + self->event_data_size);
  ESP_LOGD(TAG, "fd[%zd@%p]::receive: event queue size: %zu"
      , self - contexts.begin(), self, self->queue.size());
  assert(self->queue.size() % event_size == 0);
  assert(!self->queue.empty());

  for (auto& waiter : self->waiters)
    esp_vfs_select_triggered(waiter->waker);
}

expected<int> events::init() noexcept
{
  using fd_waiters_t = dynarray<fd_waiter>;

  static constexpr ::esp_vfs_select_ops_t select_ops = {
      .start_select = [](
            int                  nfds
          , fd_set*              readfds
          , fd_set*              writefds
          , fd_set*              exceptfds
          , esp_vfs_select_sem_t waker
          , void**               driver_data) noexcept -> esp_err_t
        {
          nfds = std::min(nfds, static_cast<int>(events::contexts.size()));

          // Count fds to allocate for
          std::size_t count = 0;
          bool should_awake = false;
          for (int fd = 0; fd < nfds; ++fd)
          {
            // event fds are never writable
            // thus everything in writefds is always 'ready' (to fail with ENOSYS)
            if (FD_ISSET(fd, writefds))
            {
              should_awake = true;
              break;
            }
            auto&& context = events::contexts[fd];
            if (FD_ISSET(fd, readfds))
            {
              std::scoped_lock _(context.lock);
              if (!context.queue.empty() || !context.opened)
              {
                should_awake = true;
                break;
              }
              ++count;
            }
            if (FD_ISSET(fd, exceptfds))
            {
              if (!context.opened)
              {
                should_awake = true;
                break;
              }
              ++count;
            }
          }

          fd_waiters_t fd_waiters;
          if (!should_awake)
          {
            if (auto r = fd_waiters_t::create(count); r)
              fd_waiters = *std::move(r);
            else if (&r.error().category() == &error_category())
              return r.error().value();
            else if (r.error() == std::errc::invalid_argument)
              return ESP_ERR_INVALID_ARG;
            else if (r.error() == std::errc::not_enough_memory)
              return ESP_ERR_NO_MEM;
            else
              return ESP_FAIL;
          }

          auto fd_waiter = fd_waiters.begin();
          for (int fd = 0; fd < nfds; ++fd)
          {
            if (!FD_ISSET(fd, readfds) && !FD_ISSET(fd, exceptfds))
              continue;

            auto&& context = events::contexts[fd];
            std::scoped_lock _(context.lock);

            if (!context.opened)
              continue;

            ::fd_set* const check_read  = FD_ISSET(fd, readfds  ) ? readfds   : nullptr;
            ::fd_set* const check_error = FD_ISSET(fd, exceptfds) ? exceptfds : nullptr;
            FD_CLR(fd, exceptfds);
            if (check_read
             && context.queue.empty())
                FD_CLR(fd, check_read);

            if (should_awake)
              // avoid allocating more.
              // we already know we're awaking select: no need for the bookkeeping!
              continue;

            assert(fd_waiter != fd_waiters.end());
            fd_waiter->fd       = fd;
            fd_waiter->readers  = check_read;
            fd_waiter->errors   = check_error;
            fd_waiter->waker    = waker;

            std::allocator<std::byte> alloc;
            if (auto r = context.waiters.push_back(fd_waiter++, alloc); r)
              ;
            else if (&r.error().category() == &error_category())
              return r.error().value();
            else if (r.error() == std::errc::invalid_argument)
              return ESP_ERR_INVALID_ARG;
            else if (r.error() == std::errc::not_enough_memory)
              return ESP_ERR_NO_MEM;
            else
              return ESP_FAIL;
          }

          if (should_awake)
            esp_vfs_select_triggered(waker);
          else
            *driver_data = fd_waiters.release();

          return ESP_OK;
        },
      .end_select = [](void* driver_data) noexcept -> esp_err_t
        {
          // resume ownership of waiter list we released in start_select
          fd_waiters_t fd_waiters(driver_data);
          for (auto& fd_waiter : fd_waiters)
          {
            auto&& context = events::contexts[fd_waiter.fd];
            std::scoped_lock _(context.lock);

            if (context.opened)
            {
              if (fd_waiter.readers && !context.queue.empty())
                FD_SET(fd_waiter.fd, fd_waiter.readers);
            }
            else
            {
              if (fd_waiter.errors)
                FD_SET(fd_waiter.fd, fd_waiter.errors);
            }
          }

          return ESP_OK;
        },
  };
  static constexpr ::esp_vfs_fs_ops_t vfs = {
      .read = [](int fd, void *data, size_t size) noexcept -> ssize_t
        {
          if (fd < 0 || fd >= events::contexts.size())
          {
            errno = EBADF;
            return -1;
          }

          auto&& context = events::contexts[fd];
          if (data == nullptr)
          {
            errno = EFAULT;
            return -1;
          }

          std::scoped_lock _(context.lock);
          ESP_LOGD(TAG, "read(fd=%d@%p [open=%u], dest=<%p,%zu>): event queue size: %zu"
              , fd, &context, context.opened, data, size, context.queue.size());
          if (!context.opened)
          {
            errno = EBADF;
            return -1;
          }
          if (context.queue.empty())
          {
            errno = EAGAIN;
            return -1;
          }
          const auto event_size = sizeof(::esp_event_base_t) + sizeof(std::int32_t) + context.event_data_size;
          if (size < event_size)
          {
            errno = EMSGSIZE;
            return -1;
          }

          assert(context.queue.size() % event_size == 0);
          std::memcpy(data, context.queue.data(), event_size);
          context.queue.erase(context.queue.begin(), context.queue.begin() + event_size);
          return event_size;
        },
      .close = [](int fd) noexcept -> int
        {
          if (fd < 0 || fd >= events::contexts.size())
          {
            errno = EBADF;
            return -1;
          }

          auto&& context = events::contexts[fd];
          std::scoped_lock _(context.lock);
          if (!std::exchange(context.opened, false))
          {
            errno = EBADF;
            return -1;
          }

          context.queue.clear();
          context.subscriptions.clear();
          for (auto& waiter : context.waiters)
            esp_vfs_select_triggered(waiter->waker);

          return 0;
        },
      .select = &select_ops,
  };

  static ::esp_vfs_id_t vfs_id = -1;
  if (vfs_id != -1)
    return {std::in_place, vfs_id};

  static std::mutex once;
  std::scoped_lock _(once);

  if (vfs_id != -1)
    return {std::in_place, vfs_id};

  // ESP_ERR_INVALID_STATE is returned when a subsystem is already initialized
  if (const auto status = ::esp_event_loop_create_default(); status != ESP_OK && status != ESP_ERR_INVALID_STATE)
    return {olifilo::unexpect, status, error_category()};

  if (const auto status = esp_vfs_register_fs_with_id(&vfs, ESP_VFS_FLAG_STATIC, nullptr, &vfs_id); status != ESP_OK)
  {
    vfs_id = -1;
    return {olifilo::unexpect, status, error_category()};
  }

  return {std::in_place, vfs_id};
}

expected<io::file_descriptor> events::subscribe(std::initializer_list<std::tuple<::esp_event_base_t, std::int32_t>> events, std::size_t event_data_size) noexcept
{
  const auto vfs = init();
  if (!vfs)
    return {unexpect, vfs.error()};

  for (int fd = 0; fd < contexts.size(); ++fd)
  {
    auto&& context = contexts[fd];
    std::scoped_lock _(context.lock);
    if (context.opened || !context.waiters.empty())
      continue;

    context.subscriptions.clear();
    context.subscriptions.reserve(events.size());

    for (auto&& [event_base, event_id] : events)
    {
      ESP_LOGD(TAG, "[local_fd=%d] subscribing to %s:%ld (max_size=%zu)"
          , fd, event_base, event_id, event_data_size);
      if (auto subscription = event_subscription_default::create(
            event_base
          , event_id
          // cast only changes first param from 'fd_context*' to 'void*' (the value it receives is &context below: fd_context*)
          , reinterpret_cast<void (*)(void*, ::esp_event_base_t, std::int32_t, void*) noexcept>(&fd_context::receive)
          , &context); !subscription)
      {
        context.subscriptions.clear();
        return subscription.error();
      }
      else
      {
        context.subscriptions.push_back(*std::move(subscription));
      }
    }

    int global_fd;
    if (const auto status = esp_vfs_register_fd_with_local_fd(
          *vfs
        , fd
        , /*permanent=*/false
        , &global_fd
        ); status == ESP_ERR_NO_MEM)
    {
      context.subscriptions.clear();
      return {unexpect, static_cast<int>(std::errc::too_many_files_open), std::system_category()};
    }
    else if (status != ESP_OK)
    {
      context.subscriptions.clear();
      return {unexpect, status, error_category()};
    }

    context.event_data_size = event_data_size;
    context.opened = true;
    return {std::in_place, io::file_descriptor_handle(global_fd)};
  }

  return {unexpect, make_error_code(std::errc::too_many_files_open)};
}
}  // namespace olifilo::esp
