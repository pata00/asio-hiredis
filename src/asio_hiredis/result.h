#ifndef ASIO_HIREDIS_RESULT_H
#define ASIO_HIREDIS_RESULT_H
#pragma once

#include <cassert>
#include <iostream>
#include <string_view>
#include <vector>

#include "hiredis.h"

namespace ahedis {

    class result {
      public:
        using SUBSCRIBE_RESULT_TYPE = std::tuple<std::string_view, std::string_view, long long>;
        using SUBSCRIBE_MESSAGE_TYPE = std::tuple<std::string_view, std::string_view, std::string_view>;

        using PSUBSCRIBE_RESULT_TYPE = SUBSCRIBE_RESULT_TYPE;
        using PSUBSCRIBE_MESSAGE_TYPE = std::tuple<std::string_view, std::string_view, std::string_view, std::string_view>;

        result() noexcept
            : reply_(nullptr) {
        }
        result(redisReply* reply)
            : reply_(reply) {
        }

        result(const result&) = delete;
        result& operator=(const result&) = delete;

        result(result&& other) noexcept {
            reply_ = other.reply_;
            other.reply_ = nullptr;
        }

        result& operator=(result&&) = delete;

        ~result() {
            if (reply_) {
                freeReplyObject(reply_);
            }
        }

        operator bool() const {
            return reply_ != nullptr;
        }

        bool has_error() const {
            return !reply_ || reply_->type == REDIS_REPLY_ERROR;
        }

        std::string_view as_status() const {
            assert(reply_->type == REDIS_REPLY_STATUS);
            return std::string_view(reply_->str, reply_->len);
        }

        std::string_view as_error() const {
            assert(reply_->type == REDIS_REPLY_ERROR);
            return std::string_view(reply_->str, reply_->len);
        }

        bool is_nil() const {
            return reply_->type == REDIS_REPLY_NIL;
        }

        bool is_array() const {
            return reply_->type == REDIS_REPLY_ARRAY;
        }

        bool is_integer() const {
            return reply_->type == REDIS_REPLY_INTEGER;
        }

        bool is_string() const {
            return reply_->type == REDIS_REPLY_STRING;
        }

        bool is_subscribe_result() const {
            if (!reply_ || reply_->type != REDIS_REPLY_ARRAY || reply_->elements != 3) {
                return false;
            }

            // 检查第一个元素是否为字符串类型的操作名
            if (reply_->element[0]->type != REDIS_REPLY_STRING || reply_->element[1]->type != REDIS_REPLY_STRING ||
                reply_->element[2]->type != REDIS_REPLY_INTEGER) {
                return false;
            }

            // 检查操作名是否为订阅相关的操作
            std::string_view operation(reply_->element[0]->str, reply_->element[0]->len);
            return operation == "subscribe" || operation == "unsubscribe";
        }

        bool is_subscribe_message() const {
            if (!reply_ || reply_->type != REDIS_REPLY_ARRAY || reply_->elements != 3) {
                return false;
            }

            // 检查所有三个元素都是字符串类型
            if (reply_->element[0]->type != REDIS_REPLY_STRING || reply_->element[1]->type != REDIS_REPLY_STRING ||
                reply_->element[2]->type != REDIS_REPLY_STRING) {
                return false;
            }

            // 检查消息类型是否为实际的消息
            std::string_view message_type(reply_->element[0]->str, reply_->element[0]->len);
            return message_type == "message";
        }

        bool is_psubscribe_result() const {
            if (!reply_ || reply_->type != REDIS_REPLY_ARRAY || reply_->elements != 3) {
                return false;
            }

            // 检查第一个元素是否为字符串类型的操作名
            if (reply_->element[0]->type != REDIS_REPLY_STRING || reply_->element[1]->type != REDIS_REPLY_STRING ||
                reply_->element[2]->type != REDIS_REPLY_INTEGER) {
                return false;
            }

            // 检查操作名是否为订阅相关的操作
            std::string_view operation(reply_->element[0]->str, reply_->element[0]->len);
            return operation == "psubscribe" || operation == "punsubscribe";
        }

        bool is_psubscribe_message() const {
            if (!reply_ || reply_->type != REDIS_REPLY_ARRAY || reply_->elements != 4) {
                return false;
            }

            // 检查所有四个元素都是字符串类型
            if (reply_->element[0]->type != REDIS_REPLY_STRING || reply_->element[1]->type != REDIS_REPLY_STRING ||
                reply_->element[2]->type != REDIS_REPLY_STRING || reply_->element[3]->type != REDIS_REPLY_STRING) {
                return false;
            }

            // 检查消息类型是否为实际的消息
            std::string_view message_type(reply_->element[0]->str, reply_->element[0]->len);
            return message_type == "pmessage";
        }

        void debug_print() const {
            auto _print = [](redisReply* reply) {
                if (reply->type == REDIS_REPLY_ERROR) {
                    std::cout << "error:" << std::string_view(reply->str, reply->len) << std::endl;
                } else if (reply->type == REDIS_REPLY_STATUS) {
                    std::cout << "status:" << std::string_view(reply->str, reply->len) << std::endl;
                } else if (reply->type == REDIS_REPLY_STRING) {
                    std::cout << "str:" << std::string_view(reply->str, reply->len) << std::endl;
                } else if (reply->type == REDIS_REPLY_INTEGER) {
                    std::cout << "long long :" << reply->integer << std::endl;
                } else if (reply->type == REDIS_REPLY_DOUBLE) {
                    std::cout << "long long :" << reply->dval << std::endl;
                } else if (reply->type == REDIS_REPLY_NIL) {
                    std::cout << "nil" << std::endl;
                } else {
                    std::cout << "not support type:" << reply->type << std::endl;
                }
            };

            assert(*this);

            if (reply_->type == REDIS_REPLY_ARRAY) {
                std::cout << "result is array:" << std::endl;
                for (size_t i = 0; i < reply_->elements; ++i) {
                    std::cout << "[" << i << "]: ";
                    _print(reply_->element[i]);
                }
            } else {
                std::cout << "result is single:" << std::endl;
                _print(reply_);
            }
        }

      public:
        template <typename T>
        T value() const;

      private:
        redisReply* reply_;
    };

    template <>
    inline std::string_view result::value<std::string_view>() const {
        assert(reply_->type == REDIS_REPLY_STRING);
        return std::string_view(reply_->str, reply_->len);
    }

    template <>
    inline long long result::value<long long>() const {
        assert(reply_->type == REDIS_REPLY_INTEGER);
        return reply_->integer;
    }

    template <>
    inline std::vector<std::string_view> result::value<std::vector<std::string_view>>() const {
        assert(reply_->type == REDIS_REPLY_ARRAY);
        std::vector<std::string_view> ret;
        ret.reserve(reply_->elements);

        for (size_t i = 0; i < reply_->elements; ++i) {
            auto element = reply_->element[i];
            assert(element->type == REDIS_REPLY_STRING);
            ret.emplace_back(std::string_view(element->str, element->len));
        }
        return ret;
    }

    template <>
    inline std::pair<std::string_view, std::string_view> result::value<std::pair<std::string_view, std::string_view>>() const {
        assert(reply_->type == REDIS_REPLY_ARRAY);
        assert(reply_->elements == 2);
        assert(reply_->element[0]->type == REDIS_REPLY_STRING);
        assert(reply_->element[1]->type == REDIS_REPLY_STRING);
        return {std::string_view(reply_->element[0]->str, reply_->element[0]->len), std::string_view(reply_->element[1]->str, reply_->element[1]->len)};
    }

    template <>
    inline std::vector<std::pair<std::string_view, std::string_view>> result::value<std::vector<std::pair<std::string_view, std::string_view>>>() const {
        assert(reply_->type == REDIS_REPLY_ARRAY);
        std::vector<std::pair<std::string_view, std::string_view>> ret;
        ret.reserve(reply_->elements);

        for (size_t i = 0; i < reply_->elements; i += 2) {
            auto key_element = reply_->element[i];
            auto value_element = reply_->element[i + 1];
            assert(key_element->type == REDIS_REPLY_STRING);
            assert(value_element->type == REDIS_REPLY_STRING);
            ret.emplace_back(std::string_view(key_element->str, key_element->len), std::string_view(value_element->str, value_element->len));
        }
        return ret;
    }

    template <>
    inline result::SUBSCRIBE_RESULT_TYPE result::value<result::SUBSCRIBE_RESULT_TYPE>() const {
        assert(reply_->type == REDIS_REPLY_ARRAY);
        assert(reply_->elements == 3);
        assert(reply_->element[0]->type == REDIS_REPLY_STRING);
        assert(reply_->element[1]->type == REDIS_REPLY_STRING);
        assert(reply_->element[2]->type == REDIS_REPLY_INTEGER);

        result::SUBSCRIBE_RESULT_TYPE ret;

        std::get<0>(ret) = std::string_view(reply_->element[0]->str, reply_->element[0]->len);
        std::get<1>(ret) = std::string_view(reply_->element[1]->str, reply_->element[1]->len);
        std::get<2>(ret) = reply_->element[2]->integer;

        return ret;
    }

    template <>
    inline result::SUBSCRIBE_MESSAGE_TYPE result::value<result::SUBSCRIBE_MESSAGE_TYPE>() const {
        assert(reply_->type == REDIS_REPLY_ARRAY);
        assert(reply_->elements == 3);
        assert(reply_->element[0]->type == REDIS_REPLY_STRING);
        assert(reply_->element[1]->type == REDIS_REPLY_STRING);
        assert(reply_->element[2]->type == REDIS_REPLY_STRING);

        result::SUBSCRIBE_MESSAGE_TYPE ret;

        std::get<0>(ret) = std::string_view(reply_->element[0]->str, reply_->element[0]->len);
        std::get<1>(ret) = std::string_view(reply_->element[1]->str, reply_->element[1]->len);
        std::get<2>(ret) = std::string_view(reply_->element[2]->str, reply_->element[2]->len);

        return ret;
    }

    template <>
    inline result::PSUBSCRIBE_MESSAGE_TYPE result::value<result::PSUBSCRIBE_MESSAGE_TYPE>() const {
        assert(reply_->type == REDIS_REPLY_ARRAY);
        assert(reply_->elements == 4);
        assert(reply_->element[0]->type == REDIS_REPLY_STRING);
        assert(reply_->element[1]->type == REDIS_REPLY_STRING);
        assert(reply_->element[2]->type == REDIS_REPLY_STRING);
        assert(reply_->element[3]->type == REDIS_REPLY_STRING);

        result::PSUBSCRIBE_MESSAGE_TYPE ret;

        std::get<0>(ret) = std::string_view(reply_->element[0]->str, reply_->element[0]->len);
        std::get<1>(ret) = std::string_view(reply_->element[1]->str, reply_->element[1]->len);
        std::get<2>(ret) = std::string_view(reply_->element[2]->str, reply_->element[2]->len);
        std::get<3>(ret) = std::string_view(reply_->element[3]->str, reply_->element[3]->len);

        return ret;
    }

} // namespace ahedis

#endif