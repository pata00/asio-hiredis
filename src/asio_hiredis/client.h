
#ifndef ASIO_HIREDIS_CLIENT_H
#define ASIO_HIREDIS_CLIENT_H
#pragma once

#include <cstdarg>
#include <deque>
#include <sstream>

#include <chrono>
#include <iostream>
#include <memory>
#include <optional>
#include <variant>
#include <vector>

#include "asio.hpp"
#include "asio/experimental/awaitable_operators.hpp"
#include "asio/experimental/concurrent_channel.hpp"
#include "hiredis.h"
#if HIREDIS_MAJOR >= 1 && HIREDIS_MINOR >= 1
#include "async.h"
#else
#error "need hiredis version >= 1.1.0"
#endif

#include "command.h"
#include "result.h"

// #define ENABLE_ASIO_HIREDIS_CLIENT_DEBUG
#ifdef ENABLE_ASIO_HIREDIS_CLIENT_DEBUG

#define ASIO_HIREDIS_CLIENT_DEBUG(id, fmt, ...)                                                \
    std::printf("ASIO_HIREDIS_CLIENT_DEBUG:[%d] [%d] " fmt, id, (int)gettid(), ##__VA_ARGS__); \
    std::fflush(stdout)
#else
#define ASIO_HIREDIS_CLIENT_DEBUG(...)
#endif

namespace ahedis {
    class client : public std::enable_shared_from_this<client> {
        using connect_resume_cb = asio::any_completion_handler<void(int, std::string)>;
        using disconnect_resume_cb = asio::any_completion_handler<void(int, std::string)>;
        using exec_resume_cb = asio::any_completion_handler<void(ahedis::result)>;
        using subscribe_msg_queue = asio::experimental::concurrent_channel<void(asio::error_code, ahedis::result)>;

      public:
        enum status_flag : uint32_t {
            init = 0,
            ev_enable_connect = 0x1,
            ev_enable_disconnect = 0x2,
            ev_enable_read = 0x4,
            ev_enable_write = 0x8,
            ev_connected = 0x10,
            ev_in_reading = 0x20,
            ev_in_writing = 0x40,
            ev_enable_connect_timeout = 0x80,
            ev_enable_command_timeout = 0x100
        };

        template <typename... Args>
        static std::shared_ptr<client> create(Args&&... args) {
            return std::shared_ptr<client>(new client(std::forward<Args>(args)...));
        }

        client(const client&) = delete;
        client& operator=(const client&) = delete;
        client(client&& other) = delete;
        ~client() {
            ASIO_HIREDIS_CLIENT_DEBUG(debug_object_id_, "destructor \n");
        }

        asio::io_context& io() {
            return io_;
        }

        asio::io_context::strand& strand() {
            return strand_;
        }

        bool has_flag(status_flag flag) const {
            return status_bits_ & flag;
        }

        void set_flag(status_flag flag, bool enable) {
            if (enable) {
                status_bits_ = status_bits_ | flag;
            } else {
                status_bits_ = status_bits_ & ~flag;
            }
        }

        bool can_close() const {
            return !connect_cb_ && !disconnect_cb_ && execute_resume_cb_queue_.empty() &&
                   std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - last_use_time_).count() > 120;
        }

        // https://stackoverflow.com/questions/69280674/co-await-custom-awaiter-in-boost-asio-coroutine
        // https://stackoverflow.com/questions/66215701/boost-awaitable-write-into-a-socket-and-await-particular-response
        // https://github.com/chriskohlhoff/asio/issues/795
        // https://github.com/chriskohlhoff/talking-async/blob/master/episode2/step_5.cpp
        template <typename CompletionToken>
        auto async_connect(const char* ip, int port, CompletionToken&& token) {
            return asio::async_initiate<CompletionToken, void(int, std::string)>(
                [this, self = shared_from_this(), ip, port](auto handler) {
                    asio::post(strand_, [this, self = std::move(self), ip, port, handler = std::move(handler)]() mutable {
                        ASIO_HIREDIS_CLIENT_DEBUG(debug_object_id_, "async_connect initiate\n");

                        assert(status_bits_ == status_flag::init);
                        assert(ac_ == nullptr);
                        set_flag(status_flag::ev_enable_connect, true);

                        redisOptions options;
                        memset(&options, 0, sizeof(options));
                        struct timeval con_tv;
                        con_tv.tv_sec = 20;
                        con_tv.tv_usec = 0;
                        options.connect_timeout = &con_tv;
                        struct timeval cmd_tv;
                        cmd_tv.tv_sec = 5;
                        cmd_tv.tv_usec = 0;
                        options.command_timeout = &cmd_tv;
                        options.privdata = static_cast<void*>(this);
                        REDIS_OPTIONS_SET_TCP(&options, ip, port);
                        options.options |= REDIS_OPT_NOAUTOFREEREPLIES;

                        // options.options |= REDIS_OPT_NOAUTOFREE;
                        ac_ = redisAsyncConnectWithOptions(&options);
                        assert(ac_ != nullptr);

                        if (ac_->c.fd == REDIS_INVALID_FD) {
                            std::move(handler)(-2, std::string("hiredis return REDIS_INVALID_FD, because:") + ac_->c.errstr);
                            return;
                        }

                        asio::error_code ec;
                        socket_.assign(asio::ip::tcp::v4(), ac_->c.fd, ec);
                        if (ec) {
                            ASIO_HIREDIS_CLIENT_DEBUG(debug_object_id_, "async_connect initiate fd = %d failed:%d %s\n", ac_->c.fd, ec.value(),
                                                      ec.message().c_str());
                            std::move(handler)(-3, std::string("asio socket assign return error:") + std::to_string(ec.value()) + " msg:" + ec.message());
                            return;
                        }

                        ASIO_HIREDIS_CLIENT_DEBUG(debug_object_id_, "async_connect initiate fd = %d\n", ac_->c.fd);

                        ac_->ev.addRead = [](void* privdata) {
                            auto this_ = static_cast<ahedis::client*>(privdata);
                            ASIO_HIREDIS_CLIENT_DEBUG(this_->debug_object_id_, "addRead\n");
                            asio::post(this_->strand(), [self = this_->shared_from_this()] {
                                self->set_flag(status_flag::ev_enable_read, true);
                                self->run();
                            });
                        };

                        ac_->ev.delRead = [](void* privdata) {
                            auto this_ = static_cast<ahedis::client*>(privdata);
                            ASIO_HIREDIS_CLIENT_DEBUG(this_->debug_object_id_, "delRead\n");
                            asio::post(this_->strand(), [self = this_->shared_from_this()] {
                                assert(self->has_flag(status_flag::ev_enable_read));
                                self->set_flag(status_flag::ev_enable_read, false);
                            });
                        };

                        ac_->ev.addWrite = [](void* privdata) {
                            auto this_ = static_cast<ahedis::client*>(privdata);
                            ASIO_HIREDIS_CLIENT_DEBUG(this_->debug_object_id_, "addWrite\n");
                            asio::post(this_->strand(), [self = this_->shared_from_this()] {
                                bool need_strand = self->has_flag(status_flag::ev_enable_connect);
                                self->set_flag(status_flag::ev_enable_write, true);
                                self->run();
                            });
                        };
                        ac_->ev.delWrite = [](void* privdata) {
                            auto this_ = static_cast<ahedis::client*>(privdata);
                            ASIO_HIREDIS_CLIENT_DEBUG(this_->debug_object_id_, "delWrite\n");
                            asio::post(this_->strand(), [self = this_->shared_from_this()] {
                                self->set_flag(status_flag::ev_enable_write, false);
                            });
                        };
                        ac_->ev.cleanup = [](void* privdata) {
                            auto this_ = static_cast<ahedis::client*>(privdata);
                            ASIO_HIREDIS_CLIENT_DEBUG(this_->debug_object_id_, "cleanup\n");
                            asio::post(this_->strand(), [self = this_->shared_from_this()] {
                                self->set_flag(status_flag::ev_enable_write, false);
                                self->set_flag(status_flag::ev_enable_read, false);
                            });
                        };
                        ac_->ev.scheduleTimer = [](void* privdata, struct timeval tv) {
                            auto this_ = static_cast<ahedis::client*>(privdata);
                            ASIO_HIREDIS_CLIENT_DEBUG(this_->debug_object_id_, "scheduleTimer %d %d\n", (int)tv.tv_sec, (int)tv.tv_usec);
                            assert(tv.tv_sec != 0 || tv.tv_usec != 0);
                            asio::post(this_->strand(), [self = this_->shared_from_this(), tv] {
                                if (self->has_flag(status_flag::ev_enable_connect)) {
                                    if (!self->has_flag(status_flag::ev_enable_connect_timeout)) {
                                        self->set_flag(status_flag::ev_enable_connect_timeout, true);
                                        self->reset_deadline(tv);
                                        self->watchdog();
                                    } else {
                                        self->flush_deadline(tv);
                                    }
                                } else {
                                    assert(self->has_flag(status_flag::ev_connected));
                                    if (!self->has_flag(status_flag::ev_enable_command_timeout)) {
                                        self->set_flag(status_flag::ev_enable_command_timeout, true);
                                        self->reset_deadline(tv);
                                        self->watchdog();
                                    } else {
                                        self->flush_deadline(tv);
                                    }
                                }
                            });
                        };

                        ac_->ev.data = static_cast<void*>(this);
                        assert(ac_->c.privdata == static_cast<void*>(this)); // passed by redisOptions

                        assert(!connect_cb_);
                        connect_cb_ = std::make_shared<connect_resume_cb>([handler = std::move(handler)](int code, std::string msg) mutable {
                            std::move(handler)(code, std::move(msg));
                        });

                        redisAsyncSetConnectCallback(ac_, [](const redisAsyncContext* c, int status) {
                            auto this_ = static_cast<ahedis::client*>(c->ev.data);
                            ASIO_HIREDIS_CLIENT_DEBUG(this_->debug_object_id_, "async_connect ConnectCallback\n");
                            this_->on_connect_cb(status);
                        });

                        redisAsyncSetDisconnectCallback(ac_, [](const redisAsyncContext* c, int status) {
                            auto this_ = static_cast<ahedis::client*>(c->ev.data);
                            ASIO_HIREDIS_CLIENT_DEBUG(this_->debug_object_id_, "async_connect DisconnectCallback\n");
                            this_->on_disconnect_cb(status);
                        });

                        run();
                    });
                },
                token);
        }

        template <typename CompletionToken>
        auto async_exec(std::shared_ptr<ahedis::command> cmd, CompletionToken&& token) {
            return asio::async_initiate<CompletionToken, void(ahedis::result)>(
                [this, self = shared_from_this(), cmd = std::move(cmd)](auto handler) {
                    asio::post(strand_, [this, self = std::move(self), cmd = std::move(cmd), handler = std::move(handler)]() mutable {
                        ASIO_HIREDIS_CLIENT_DEBUG(debug_object_id_, "async_exec initiate\n");
                        if (!has_flag(status_flag::ev_connected)) {
                            ASIO_HIREDIS_CLIENT_DEBUG(debug_object_id_, "start query but conn status error:%x...\n", status_bits_);
                            ahedis::result arg(nullptr);
                            std::move(handler)(std::move(arg));
                            return;
                        }

                        execute_resume_cb_queue_.emplace_back(std::make_shared<exec_resume_cb>([handler = std::move(handler)](ahedis::result arg) mutable {
                            std::move(handler)(std::move(arg));
                        }));

                        ASIO_HIREDIS_CLIENT_DEBUG(debug_object_id_, "start real query...\n");
                        last_use_time_ = std::chrono::steady_clock::now();
                        void* privdata = static_cast<void*>(this);
                        assert(privdata != nullptr);

                        auto ret = redisAsyncFormattedCommand(
                            ac_,
                            [](redisAsyncContext* c, void* r, void* privdata) {
                                auto this_ = static_cast<ahedis::client*>(privdata);
                                this_->on_exec_cb(r);
                            },
                            privdata, cmd->data(), cmd->length());

                        if (ret != REDIS_OK) {
                            ASIO_HIREDIS_CLIENT_DEBUG(debug_object_id_, "start real query failed\n");
                            ahedis::result arg(nullptr);
                            auto handler_ptr = std::move(execute_resume_cb_queue_.front());
                            execute_resume_cb_queue_.pop_front();
                            std::move (*handler_ptr)(std::move(arg));
                            return;
                        }

                        run();
                    });
                },
                token);
        }

        // 一个正在做 SUBSCRIBE 的 hiredis 客户端（连接）不能同时执行普通的 GET / SET / HGETALL 等命令。
        template <typename CompletionToken>
        auto async_subscribe(std::shared_ptr<ahedis::command> cmd, CompletionToken&& token) {
            assert(cmd->is_subscribe_cmd());
            assert(execute_resume_cb_queue_.empty());
            return asio::async_initiate<CompletionToken, void(bool)>(
                [this, self = shared_from_this(), cmd = std::move(cmd)](auto handler) {
                    asio::post(strand_, [this, self = std::move(self), cmd = std::move(cmd), handler = std::move(handler)]() mutable {
                        ASIO_HIREDIS_CLIENT_DEBUG(debug_object_id_, "async_subscribe initiate\n");
                        if (!has_flag(status_flag::ev_connected)) {
                            ASIO_HIREDIS_CLIENT_DEBUG(debug_object_id_, "start subscribe but conn status error:%x...\n", status_bits_);
                            std::move(handler)(false);
                            return;
                        }

                        // assert(!subscribe_resume_cb_);
                        // subscribe_resume_cb_ = std::make_shared<exec_resume_cb>([handler = std::move(handler)](ahedis::result arg) mutable {
                        //     std::move(handler)(std::move(arg));
                        // });

                        ASIO_HIREDIS_CLIENT_DEBUG(debug_object_id_, "start real subscribe...\n");
                        last_use_time_ = std::chrono::steady_clock::now();
                        void* privdata = static_cast<void*>(this);
                        assert(privdata != nullptr);

                        auto ret = redisAsyncFormattedCommand(
                            ac_,
                            [](redisAsyncContext* c, void* r, void* privdata) {
                                auto this_ = static_cast<ahedis::client*>(privdata);
                                this_->on_subscribe_cb(r);
                            },
                            privdata, cmd->data(), cmd->length());

                        if (ret != REDIS_OK) {
                            ASIO_HIREDIS_CLIENT_DEBUG(debug_object_id_, "start real subscribe failed\n");
                            std::move(handler)(false);
                        } else {
                            ASIO_HIREDIS_CLIENT_DEBUG(debug_object_id_, "start real subscribe success\n");
                            std::move(handler)(true);
                        }
                    });
                },
                token);
        }

        // 监听订阅消息，支持回调和co_await方式调用
        template <typename CompletionToken>
        auto listen_msg(CompletionToken&& token) {
            return asio::async_initiate<CompletionToken, void(ahedis::result)>(
                [this, self = shared_from_this()](auto handler) {
                    // 从消息队列中异步接收消息
                    sub_msgs.async_receive([this, handler = std::move(handler)](asio::error_code ec, ahedis::result result) mutable {
                        if (ec) {
                            // 如果有错误，创建一个错误的result
                            ahedis::result error_result(nullptr);
                            std::move(handler)(std::move(error_result));
                        } else {
                            ASIO_HIREDIS_CLIENT_DEBUG(debug_object_id_, "listen_msg cb0\n");
                            std::move(handler)(std::move(result));
                            ASIO_HIREDIS_CLIENT_DEBUG(debug_object_id_, "listen_msg cb1\n");
                        }
                    });
                },
                token);
        }

        template <typename CompletionToken>
        auto async_stop(CompletionToken&& token) {
            return asio::async_initiate<CompletionToken, void(int, std::string)>(
                [this, self = shared_from_this()](auto handler) {
                    asio::post(strand_, [this, self = std::move(self), handler = std::move(handler)]() mutable {
                        ASIO_HIREDIS_CLIENT_DEBUG(debug_object_id_, "async_stop initiate\n");
                        assert(!has_flag(status_flag::ev_enable_disconnect));
                        set_flag(status_flag::ev_enable_disconnect, true);
                        assert(ac_ != nullptr);
                        ASIO_HIREDIS_CLIENT_DEBUG(debug_object_id_, "async_stop initiate2\n");

                        if (!has_flag(status_flag::ev_connected) && !has_flag(status_flag::ev_enable_connect)) {
                            ASIO_HIREDIS_CLIENT_DEBUG(debug_object_id_, "async_stop but aleady disconnected...\n");
                            std::string msg("async_stop but aleady disconnected");
                            std::move(handler)(0, std::move(msg));
                            ASIO_HIREDIS_CLIENT_DEBUG(debug_object_id_, "async_stop but aleady disconnected...2\n");
                            return;
                        }

                        disconnect_cb_ = std::make_shared<disconnect_resume_cb>([handler = std::move(handler)](int code, std::string msg) mutable {
                            printf("will call disconnect_cb_...\n");
                            std::move(handler)(code, std::move(msg));
                        });
                        ASIO_HIREDIS_CLIENT_DEBUG(debug_object_id_, "async_stop initiate3\n");

                        stop();
                    });
                },
                token);
        }

        void strand_run() {
            asio::post(strand_, [this, self = shared_from_this()] {
                run();
            });
        }

        void reset_deadline(const timeval& tv) {
            deadline_ = std::chrono::steady_clock::now() + std::chrono::seconds(tv.tv_sec) + std::chrono::microseconds(tv.tv_usec);
        }

        void flush_deadline(const timeval& tv) {
            deadline_ = std::max(deadline_, std::chrono::steady_clock::now() + std::chrono::seconds(tv.tv_sec) + std::chrono::microseconds(tv.tv_usec));
        }

        void watchdog() {
            auto trigger_clock = deadline_;
            timer_.expires_at(deadline_);
            // ASIO_HIREDIS_CLIENT_DEBUG(debug_object_id_, "watchdog wait...%d\n",(int)std::chrono::duration_cast<std::chrono::seconds>
            // (trigger_clock.time_since_epoch()).count());
            timer_.async_wait(asio::bind_executor(strand_, [this, self = shared_from_this(), trigger_clock](std::error_code ec) {
                if (ec) {
                    if (ec.value() != asio::error::operation_aborted) {
                        ASIO_HIREDIS_CLIENT_DEBUG(debug_object_id_, "watchdog error...%d %s\n", ec.value(), ec.message().c_str());
                    }
                    return;
                }

                if (deadline_ > trigger_clock) {
                    // deadline has flushded
                    watchdog();
                } else {
                    ASIO_HIREDIS_CLIENT_DEBUG(debug_object_id_, "watchdog triggered\n");
                    redisAsyncHandleTimeout(ac_);
                }
            }));
        }

      private:
        //
        client(asio::io_context& io)
            : io_(io)
            , strand_(io)
            , socket_(io)
            , timer_(io)
            , ac_(nullptr)
            , status_bits_(status_flag::init)
            , last_use_time_(std::chrono::steady_clock::now())
            , sub_msgs(io, 1024 * 16) {
#ifdef ENABLE_ASIO_HIREDIS_CLIENT_DEBUG
            debug_object_id_ = ++s_current;
#endif
            ASIO_HIREDIS_CLIENT_DEBUG(debug_object_id_, "constructor\n");
        }

        void run() {
            if (has_flag(status_flag::ev_enable_read) && !has_flag(status_flag::ev_in_reading)) {
                set_flag(status_flag::ev_in_reading, true);
                ASIO_HIREDIS_CLIENT_DEBUG(debug_object_id_, "query read...\n");
                socket_.async_wait(asio::ip::tcp::socket::wait_read, asio::bind_executor(strand_, [this, self = shared_from_this()](asio::error_code ec) {
                                       ASIO_HIREDIS_CLIENT_DEBUG(debug_object_id_, "start read...\n");
                                       assert(has_flag(status_flag::ev_in_reading));
                                       if (ec) {
                                           ASIO_HIREDIS_CLIENT_DEBUG(debug_object_id_, "end read error %d, %s\n", ec.value(), ec.message().c_str());
                                           set_flag(status_flag::ev_in_reading, false);
                                           return;
                                       }
                                       redisAsyncHandleRead(ac_); // 内部会回调查询结果, 断开连接回调
                                       set_flag(status_flag::ev_in_reading, false);
                                       ASIO_HIREDIS_CLIENT_DEBUG(debug_object_id_, "end read...\n");
                                       strand_run();
                                   }));
                ASIO_HIREDIS_CLIENT_DEBUG(debug_object_id_, "query read end...\n");
            }

            if (has_flag(status_flag::ev_enable_write) && !has_flag(status_flag::ev_in_writing)) {
                set_flag(status_flag::ev_in_writing, true);
                ASIO_HIREDIS_CLIENT_DEBUG(debug_object_id_, "query write...\n");
                socket_.async_wait(asio::ip::tcp::socket::wait_write, asio::bind_executor(strand_, [this, self = shared_from_this()](asio::error_code ec) {
                                       ASIO_HIREDIS_CLIENT_DEBUG(debug_object_id_, "start write...\n");
                                       assert(has_flag(status_flag::ev_in_writing));
                                       if (ec) {
                                           ASIO_HIREDIS_CLIENT_DEBUG(debug_object_id_, "end write error %d %s\n", ec.value(), ec.message().c_str());
                                           set_flag(status_flag::ev_in_writing, false);
                                           return;
                                       }
                                       redisAsyncHandleWrite(ac_); // 内部可能会回调connectcallbak
                                       set_flag(status_flag::ev_in_writing, false);
                                       ASIO_HIREDIS_CLIENT_DEBUG(debug_object_id_, "end write...\n");
                                       strand_run();
                                   }));
                ASIO_HIREDIS_CLIENT_DEBUG(debug_object_id_, "query write end...\n");
            }
        }

        void stop() {
            printf("before socket cancel...\n");
            socket_.cancel();
            printf("stop....");
            redisAsyncDisconnect(ac_); // 内部会调用 cleanup， 然后再调用disconnectcb
            connect_cb_.reset();
            disconnect_cb_.reset();
            printf("here\n");
        }

        void on_connect_cb(int status) {
            std::string msg;
            if (status != REDIS_OK) {
                socket_.release();
                timer_.cancel();
                msg = std::string(ac_->errstr);
            }

            assert(has_flag(status_flag::ev_enable_connect));
            set_flag(status_flag::ev_enable_connect, false);

            assert(!has_flag(status_flag::ev_connected));
            if (status == REDIS_OK) {
                set_flag(status_flag::ev_connected, true);

#ifdef ENABLE_ASIO_HIREDIS_CLIENT_DEBUG
                int current_cnt = ++s_connected_cnt;
                ASIO_HIREDIS_CLIENT_DEBUG(debug_object_id_, "asio::on_connect_cb cnt: %d\n", current_cnt);
                if (current_cnt == 3) {
                    // exit(0);
                }
#endif

            } else {
                ASIO_HIREDIS_CLIENT_DEBUG(debug_object_id_, "asio::on_connect_cb Error: %s\n", msg.c_str());
            }

            if (connect_cb_) {
                asio::post(strand_, [handler_ptr = std::move(connect_cb_), status, msg = std::move(msg)] {
                    std::move (*handler_ptr)(status, std::move(msg));
                });
            }

            // TODO 是否只有异常断开会触发此错误
            if (status == REDIS_OK) {
                socket_.async_wait(asio::ip::tcp::socket::wait_error, asio::bind_executor(strand_, [this, self = shared_from_this()](asio::error_code ec) {
                                       (void)this;
                                       ASIO_HIREDIS_CLIENT_DEBUG(debug_object_id_, "END===========wait_error %d, %s\n", ec.value(), ec.message().c_str());
                                       // assert(false);
                                   }));
            }
        }

        void on_disconnect_cb(int status) {
            assert(has_flag(status_flag::ev_connected));
            set_flag(status_flag::ev_connected, false);
            try {
                socket_.release();
            } catch (...) {
                // for windows socket release error
                // https://github.com/chriskohlhoff/asio/issues/1097
            }
            timer_.cancel();

            std::string msg;
            if (status != REDIS_OK) {
                msg = std::string(ac_->errstr);
            }

            if (status == REDIS_OK) { // 主动关闭
                assert(has_flag(status_flag::ev_enable_disconnect));
                set_flag(status_flag::ev_enable_disconnect, false);
            } else {
                ASIO_HIREDIS_CLIENT_DEBUG(debug_object_id_, "asio::on_disconnect_cb Error: %s\n", msg.c_str());
            }

            if (disconnect_cb_) {
                asio::post(strand_, [handler_ptr = std::move(disconnect_cb_), status, msg = std::move(msg)] {
                    std::move (*handler_ptr)(status, std::move(msg));
                });
            }
        }

        void on_exec_cb(void* r) {

            ASIO_HIREDIS_CLIENT_DEBUG(debug_object_id_, "query cb with return :%p\n", r);
            assert(!execute_resume_cb_queue_.empty()); // not support subcribe

            auto handler_ptr = std::move(execute_resume_cb_queue_.front());
            execute_resume_cb_queue_.pop_front();

            asio::post(strand_, [handler_ptr = std::move(handler_ptr), r] {
                ahedis::result arg(static_cast<redisReply*>(r));
                std::move (*handler_ptr)(std::move(arg));
            });
        }

        // 在这个回调中把结果存储起来
        void on_subscribe_cb(void* r) {
            ASIO_HIREDIS_CLIENT_DEBUG(debug_object_id_, "on_subscribe_cb:%p\n", r);
            ahedis::result arg(static_cast<redisReply*>(r));
            bool flag = sub_msgs.try_send(asio::error_code{}, std::move(arg));
            assert(flag);
            // if (arg) {
            //     arg.debug_print();
            // }
            // ASIO_HIREDIS_CLIENT_DEBUG(debug_object_id_, "subcribe cb with return :%p\n", r);
            // assert(subscribe_resume_cb_ != nullptr);

            // auto& handler_ptr = subscribe_resume_cb_;

            // asio::post(strand_, [handler_ptr, r] {
            //     ahedis::result arg(static_cast<redisReply*>(r));
            //     (*handler_ptr)(std::move(arg));

            //     // ahedis::result arg2(static_cast<redisReply*>(nullptr));
            //     // (*handler_ptr)(std::move(arg2));
            // });
        }

#ifdef ENABLE_ASIO_HIREDIS_CLIENT_DEBUG
      public:
        int debug_object_id_;
        static inline std::atomic<int> s_current = 0;
        static inline std::atomic<int> s_connected_cnt = 0;
#endif

      private:
        asio::io_context& io_;
        asio::io_context::strand strand_;
        asio::ip::tcp::socket socket_;
        asio::steady_timer timer_;
        std::chrono::steady_clock::time_point deadline_;
        redisAsyncContext* ac_;
        uint32_t status_bits_;
        std::chrono::steady_clock::time_point last_use_time_;
        std::shared_ptr<connect_resume_cb> connect_cb_;
        std::shared_ptr<disconnect_resume_cb> disconnect_cb_;
        std::deque<std::shared_ptr<exec_resume_cb>> execute_resume_cb_queue_;
        subscribe_msg_queue sub_msgs;
    };

#ifdef ENABLE_ASIO_HIREDIS_CLIENT_DEBUG
    static const int pos = (size_t)&((client*)0)->debug_object_id_;
    constexpr const int base_size = sizeof(std::shared_ptr<client>);
    static_assert(pos == base_size);
    static_assert(base_size == 16);
#endif

} // namespace ahedis

#endif
