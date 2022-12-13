#ifndef ASIO_HIREDIS_RESULT_H
#define ASIO_HIREDIS_RESULT_H
#pragma once

#include <cassert>
#include <iostream>
#include <string_view>

#include "hiredis.h"

namespace ahedis {

    class result {
      public:
        result(redisReply* reply)
            : reply_(reply) {
        }

        result(const result&) = delete;
        result& operator=(const result&) = delete;

        result(result&& other) {
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

        std::string_view as_str() const {
            assert(reply_->type == REDIS_REPLY_STRING);
            return std::string_view(reply_->str, reply_->len);
        }

        long long as_longlong() const {
            assert(reply_->type == REDIS_REPLY_INTEGER);
            return reply_->integer;
        }

        std::pair<std::string_view, std::string_view> as_pair_str() const {
            assert(reply_->type == REDIS_REPLY_ARRAY);
            assert(reply_->elements == 2);
            return {std::string_view(reply_->element[0]->str, reply_->element[0]->len), std::string_view(reply_->element[1]->str, reply_->element[1]->len)};
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
        redisReply* reply_;
    };
} // namespace ahedis

#endif