#ifndef ASIO_HIREDIS_COMMAND_H
#define ASIO_HIREDIS_COMMAND_H
#pragma once

#include <cassert>
#include <memory>
#include <utility>

#include "hiredis.h"

namespace ahedis {

    class command {
      public:
        template <typename... Args>
        static std::shared_ptr<command> create(const char* format, Args&&... args) {
            char* data = nullptr;
            auto length = redisFormatCommand(&data, format, std::forward<Args>(args)...);
            assert(length > 0);
            return std::make_shared<command>(command(data, length));
        }

        ~command() {
            if (data_) {
                redisFreeCommand(data_);
            }
        }

        command(const command&) = delete;

        command(command&& other) noexcept {
            data_ = other.data_;
            length_ = other.length_;
            other.data_ = nullptr;
            other.length_ = 0;
        }

        command& operator=(const command&) = delete;

        command& operator=(command&& other) = delete;

        // command& operator=(command&& other) noexcept {
        //     std::swap(data_, other.data_);
        //     std::swap(length_, other.length_);
        //     return *this;
        // }

        char* data() const {
            return data_;
        }

        int length() const {
            return length_;
        }

      private:
        command(char* data, int length)
            : data_(data)
            , length_(length) {
        }

      private:
        char* data_;
        int length_;
    };

    // asio::awaitable<long long> lpush(const std::string_view& key, const std::string_view& val) {
    //     constexpr auto use_nothrow_awaitable = asio::experimental::as_tuple(asio::use_awaitable);
    //     asio_hiredis::command cmd("LPUSH %b %b", key.data(), key.size(), val.data(), val.size());
    //     auto [replay] = co_await async_exec(cmd, use_nothrow_awaitable);
    //     // if(replay->)

    //     co_return 0;
    // }

    // asio::awaitable<OptionalStringPair> brpop(const std::string_view& key, long long timeout) {
    //     constexpr auto use_nothrow_awaitable = asio::experimental::as_tuple(asio::use_awaitable);
    //     asio_hiredis::command cmd("BRPOP %b %lld", key.data(), key.size(), timeout);
    //     auto [replay] = co_await async_exec(cmd, use_nothrow_awaitable);
    //     // if(replay->)

    //     co_return std::nullopt;
    // }

    // asio::awaitable<bool> expire(const std::string_view& key, long long ttl) {
    //     constexpr auto use_nothrow_awaitable = asio::experimental::as_tuple(asio::use_awaitable);
    //     asio_hiredis::command cmd("EXPIRE %b %lld", key.data(), key.size(), ttl);
    //     auto [replay] = co_await async_exec(cmd, use_nothrow_awaitable);
    //     // if(replay->)

    //     co_return true;
    // }

    // asio::awaitable<bool> setex(const std::string_view& key, long long ttl, const std::string_view& val) {
    //     constexpr auto use_nothrow_awaitable = asio::experimental::as_tuple(asio::use_awaitable);
    //     asio_hiredis::command cmd("SETEX %b %lld %b", key.data(), key.size(), ttl, val.data(), val.size());
    //     auto [replay] = co_await async_exec(cmd, use_nothrow_awaitable);
    //     // if(replay->)

    //     co_return true;
    // }

} // namespace ahedis
#endif
