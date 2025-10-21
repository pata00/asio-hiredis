#ifndef ASIO_HIREDIS_COMMAND_H
#define ASIO_HIREDIS_COMMAND_H
#pragma once

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstring>
#include <memory>
#include <ranges>
#include <unordered_set>
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

        // 判断是否为订阅命令（零拷贝 + 忽略大小写）
        bool is_subscribe_cmd() const {
            if (!data_ || length_ <= 0) {
                return false;
            }

            size_t pos = 0;
            size_t len = static_cast<size_t>(length_);

            // 跳过可能的前导空格
            while (pos < len && std::isspace(data_[pos])) {
                ++pos;
            }

            // 检查是否为RESP数组格式（以'*'开头）
            if (pos >= len || data_[pos] != '*') {
                return false;
            }
            ++pos;

            // 跳过数组长度部分，直到找到\r\n
            while (pos < len && data_[pos] != '\r') {
                ++pos;
            }
            // 跳过\r\n
            if (pos + 1 < len) {
                pos += 2;
            } else {
                return false;
            }

            // 第一个参数的格式应该是$N\r\n，其中N是字符串长度
            if (pos >= len || data_[pos] != '$') {
                return false;
            }
            ++pos;

            // 解析第一个参数（命令名称）的长度
            size_t cmd_len = 0;
            while (pos < len && data_[pos] != '\r') {
                if (data_[pos] >= '0' && data_[pos] <= '9') {
                    cmd_len = cmd_len * 10 + (data_[pos] - '0');
                }
                ++pos;
            }
            // 跳过\r\n
            if (pos + 1 < len) {
                pos += 2;
            } else {
                return false;
            }

            // 确保我们有足够的字符用于命令名称比较
            if (pos + cmd_len > len) {
                return false;
            }

            // 使用strncasecmp零拷贝地比较命令名称，忽略大小写
            // 检查所有可能的订阅命令
            if (cmd_len == 9 && strncasecmp(data_ + pos, "SUBSCRIBE", 9) == 0) {
                return true;
            }
            if (cmd_len == 10 && strncasecmp(data_ + pos, "PSUBSCRIBE", 10) == 0) {
                return true;
            }
            if (cmd_len == 11 && strncasecmp(data_ + pos, "UNSUBSCRIBE", 11) == 0) {
                return true;
            }
            if (cmd_len == 12 && strncasecmp(data_ + pos, "PUNSUBSCRIBE", 12) == 0) {
                return true;
            }

            return false;
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
