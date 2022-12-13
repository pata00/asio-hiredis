#ifndef ASIO_HIREDIS_POOL_H
#define ASIO_HIREDIS_POOL_H
#pragma once

#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "asio.hpp"

#include "asio_hiredis/client.h"
#include "asio_hiredis/command.h"
#include "asio_hiredis/result.h"

// #define ENABLE_ASIO_HIREDIS_POOL_DEBUG
#ifdef ENABLE_ASIO_HIREDIS_POOL_DEBUG

#define ASIO_HIREDIS_POOL_DEBUG(fmt, ...)                                            \
    std::printf("ASIO_HIREDIS_POOL_DEBUG:[%d]  " fmt, (int)gettid(), ##__VA_ARGS__); \
    std::fflush(stdout)
#else
#define ASIO_HIREDIS_POOL_DEBUG(...)
#endif

namespace ahedis {
    class conn;

    class pool {
        friend class client;
        friend class conn;

      public:
        pool(asio::io_context& io, std::string_view ip, int port, std::string_view user, std::string_view pass, int db, int min_size, int max_size);

        ~pool() {
            ASIO_HIREDIS_POOL_DEBUG("asio_hiredis_pool::~asio_hiredis_pool %p\n", this);
        }

        static pool* create(asio::io_context& io, const std::string& uri, int min_size, int max_size);

        asio::awaitable<void> init();

        asio::awaitable<void> shutdown();

        asio::awaitable<std::unique_ptr<conn>> get_conn();

        asio::awaitable<void> reset_client(int id);

        asio::awaitable<void> atomic_expand();

        asio::awaitable<result> async_exec(std::shared_ptr<ahedis::command> cmd);

        asio::awaitable<bool> test_ping();

        asio::io_context& io() {
            return io_;
        }

        asio::io_context::strand& strand() {
            return strand_;
        }

      private:
        const std::string config_ip_;
        const std::string config_user_;
        const std::string config_pass_;
        const int config_port_;
        const int config_db_;
        const int config_min_size_;
        const int config_max_size_;
        asio::io_context& io_;
        asio::io_context::strand strand_;
        std::vector<std::shared_ptr<client>> m_pool;
        std::unordered_set<uint32_t> m_idle_ids;
        std::recursive_mutex m_mutex;
        std::atomic<int> m_pending_cnt;
    };

    class conn {
      public:
        conn(pool& pool, uint32_t idx)
            : m_from_pool(pool)
            , m_idx(idx) {
            ASIO_HIREDIS_POOL_DEBUG("hold conn %d\n", idx);
            auto lk = std::lock_guard(m_from_pool.m_mutex);
            assert(m_from_pool.m_idle_ids.find(m_idx) != m_from_pool.m_idle_ids.end());
            m_from_pool.m_idle_ids.erase(m_idx);
        }

        ~conn() {
            ASIO_HIREDIS_POOL_DEBUG("free conn %d\n", m_idx);
            auto lk = std::lock_guard(m_from_pool.m_mutex);
            assert(m_from_pool.m_idle_ids.find(m_idx) == m_from_pool.m_idle_ids.end());
            m_from_pool.m_idle_ids.emplace(m_idx);
        }

        conn(const conn&) = delete;
        conn& operator=(conn&) = delete;

        uint32_t id() const {
            return m_idx;
        }

        std::shared_ptr<client>& get() {
            auto lk = std::lock_guard(m_from_pool.m_mutex); // TODO, dose this need lock?
            return m_from_pool.m_pool[m_idx];
        }

        asio::awaitable<void> reset() {
            co_await m_from_pool.reset_client(m_idx);
        }

        asio::awaitable<result> async_exec(std::shared_ptr<ahedis::command> cmd) {
            constexpr auto use_nothrow_awaitable = asio::as_tuple(asio::use_awaitable);
            auto [res] = co_await get()->async_exec(cmd, use_nothrow_awaitable);
            co_return std::move(res);
        }

      private:
        pool& m_from_pool;
        const uint32_t m_idx;
    };

} // namespace ahedis
#endif