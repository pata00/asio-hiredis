#include "asio_hiredis/pool.h"

#include "asio/as_tuple.hpp"
#include "asio/steady_timer.hpp"

#include "asio_hiredis/client.h"

namespace ahedis {

    namespace pool_detail {
        static asio::awaitable<void> timeout(std::chrono::steady_clock::duration duration) {
            constexpr auto use_nothrow_awaitable = asio::as_tuple(asio::use_awaitable);
            asio::steady_timer timer(co_await asio::this_coro::executor);
            timer.expires_after(duration);
            co_await timer.async_wait(use_nothrow_awaitable);
        }

        static std::tuple<bool, std::string, int, std::string, std::string, int> parse_redis_uri(const std::string& redis_uri) {
            auto split_uri = [](const std::string& uri) {
                auto pos = uri.find("://");
                if (pos == std::string::npos) {
                    throw "invalid URI: no scheme";
                }

                auto type = uri.substr(0, pos);

                auto start = pos + 3;
                pos = uri.find("@", start);
                if (pos == std::string::npos) {
                    // No auth info.
                    return std::make_tuple(type, std::string{}, uri.substr(start));
                }

                auto auth = uri.substr(start, pos - start);

                return std::make_tuple(type, auth, uri.substr(pos + 1));
            };

            auto split_path = [](const std::string& path) -> std::tuple<std::string, int, std::string> {
                auto parameter_pos = path.rfind("?");
                std::string parameter_string;
                if (parameter_pos != std::string::npos) {
                    parameter_string = path.substr(parameter_pos + 1);
                }

                auto pos = path.rfind("/");
                if (pos != std::string::npos) {
                    // Might specified a db number.
                    try {
                        auto db = std::stoi(path.substr(pos + 1));

                        return std::make_tuple(path.substr(0, pos), db, parameter_string);
                    } catch (const std::exception&) {
                        // Not a db number, and it might be a path to unix domain socket.
                    }
                }

                // No db number specified, and use default one, i.e. 0.
                return std::make_tuple(path.substr(0, parameter_pos), 0, parameter_string);
            };

            bool flag;
            std::string host;
            int port = 6379;
            std::string user;
            std::string pass;
            int db = 0;

            try {
                auto const [type, user_and_pass, path] = split_uri(redis_uri);

                if (auto pos = user_and_pass.find(":"); pos != std::string::npos) {
                    user = user_and_pass.substr(0, pos);
                    pass = user_and_pass.substr(pos + 1);
                } else {
                    if (!user_and_pass.empty()) {
                        pass = user_and_pass;
                    }
                }

                auto const [host_and_port, db_value, parameter_string] = split_path(path);

                if (auto pos = host_and_port.find(":"); pos != std::string::npos) {
                    host = host_and_port.substr(0, pos);
                    port = std::atoi(host_and_port.substr(pos + 1).c_str());
                } else {
                    host = host_and_port;
                }

                if (host.empty()) {
                    throw "host is emtpy str";
                }

                db = db_value;

                if (db < 0) {
                    throw "db index err";
                }

                flag = true;

            } catch (const char* msg) {
                printf("parse_redis_uri exception:%s\n", msg);
                flag = false;
            }

            return std::tuple(flag, host, port, user, pass, db);
        }

        static asio::awaitable<std::shared_ptr<ahedis::client>> connect_and_init(asio::io_context& io, const std::string& ip, int port, const std::string& user,
                                                                                 const std::string& pass, int db) {
            constexpr auto use_nothrow_awaitable = asio::as_tuple(asio::use_awaitable);
            auto new_cli = ahedis::client::create(io);
            // ASIO_HIREDIS_POOL_DEBUG("connect_and_init begin async_connect = %d \n", new_cli->debug_object_id_);
            auto [code, msg] = co_await new_cli->async_connect(ip.c_str(), port, use_nothrow_awaitable);

            if (code != 0) {
                ASIO_HIREDIS_POOL_DEBUG("connect_and_init connect failed code = %d, msg = %s, wait 1s\n", code, msg.c_str());
                co_return std::shared_ptr<ahedis::client>();
            }

            if (!pass.empty()) {
                std::shared_ptr<ahedis::command> auth_cmd;
                if (!user.empty()) {
                    auth_cmd = ahedis::command::create("AUTH %b %b", user.data(), user.size(), pass.data(), pass.size());
                } else {
                    auth_cmd = ahedis::command::create("AUTH %b", pass.data(), pass.size());
                }

                auto [auth_reply] = co_await new_cli->async_exec(auth_cmd, use_nothrow_awaitable);
                if (!auth_reply || auth_reply.has_error()) {
                    ASIO_HIREDIS_POOL_DEBUG("connect_and_init auth failed: %s\n", auth_reply->as_error().data());
                    co_return std::shared_ptr<ahedis::client>();
                }
                assert(auth_reply.as_status() == "OK");
            }

            if (db != 0) {
                auto select_cmd = ahedis::command::create("SELECT %d", db);
                auto [select_reply] = co_await new_cli->async_exec(select_cmd, use_nothrow_awaitable);
                if (!select_reply || select_reply.has_error()) {
                    ASIO_HIREDIS_POOL_DEBUG("connect_and_init select failed: %s\n", select_reply->as_error().data());
                    co_return std::shared_ptr<ahedis::client>();
                }
                assert(select_reply.as_status() == "OK");
            }
            co_return new_cli;
        }

    } // namespace pool_detail

    pool::pool(asio::io_context& io, std::string_view ip, int port, std::string_view user, std::string_view pass, int db, int min_size, int max_size)
        : config_ip_(ip)
        , config_user_(user)
        , config_pass_(pass)
        , config_port_(port)
        , config_db_(db)
        , config_min_size_(min_size)
        , config_max_size_(max_size)
        , io_(io)
        , strand_(io) {
        assert(0 <= config_min_size_ && config_min_size_ <= config_max_size_);
        ASIO_HIREDIS_POOL_DEBUG("asio_hiredis_pool::asio_hiredis_pool %p\n", this);
        m_pool.reserve(max_size);
        m_idle_ids.reserve(max_size);

        // return;
        // TODO: quit this coroutine
        // asio::co_spawn(
        //     io_,
        //     [this]() -> asio::awaitable<void> {
        //         auto timeout = [](int sec) -> asio::awaitable<void> {
        //             asio::steady_timer timer(co_await asio::this_coro::executor);
        //             timer.expires_after(std::chrono::seconds(sec));
        //             co_await timer.async_wait(asio::use_awaitable);
        //         };

        //         while (true) {
        //             co_await timeout(1);
        //             if ((int)m_idle_ids.size() > (int)config_min_size_) {
        //                 for (auto e : m_idle_ids) {
        //                 }
        //             }
        //         }

        //         co_return;
        //     },
        //     asio::detached);
    }

    pool* pool::create(asio::io_context& io, const std::string& uri, int min_size, int max_size) {
        auto [status, host, port, user, pass, db] = pool_detail::parse_redis_uri(uri);
        assert(status);
        return new pool(io, host, port, user, pass, db, min_size, max_size);
    }

    // asio::awaitable<void> pool::init() {
    //     for (int i = 0; i < config_min_size_; ++i) {
    //         co_await atomic_expand();
    //     }
    //     co_return;
    // }

    asio::awaitable<void> pool::shutdown() {
        auto lk = std::lock_guard(m_mutex);
        for (auto client : m_pool) {
            co_await client->async_stop(asio::use_awaitable);
        }
        co_return;
    }

    // asio::awaitable<std::shared_ptr<client>> pool::get_client() {
    //     while (m_pool.empty()) {
    //         auto new_client = std::make_shared<client>(*this);
    //         auto [connect_status] = co_await new_client->async_connect(config_ip_.c_str(), config_port_, use_nothrow_awaitable);
    //         if (connect_status == 0) {
    //             m_pool.push_back(std::move(new_client));
    //             break;
    //         } else {
    //             co_await timeout(1s);
    //         }
    //     }
    //     co_return m_pool.front();
    // }

    asio::awaitable<std::unique_ptr<conn>> pool::get_conn() {
        using namespace std::literals::chrono_literals;

        std::unique_ptr<conn> ret;

        for (;;) {
            if (!m_idle_ids.empty()) {
                auto lk = std::lock_guard(m_mutex);
                if (!m_idle_ids.empty()) {
                    ret = std::make_unique<conn>(*this, *m_idle_ids.begin());
                    break;
                }
            }

            // without strand
            if (m_pending_cnt >= config_max_size_) {
                ASIO_HIREDIS_POOL_DEBUG("all conn is busy without strand..., wait for 10 ms\n");
                co_await pool_detail::timeout(10ms);
                continue;
            }

            co_await asio::post(asio::bind_executor(strand_, asio::use_awaitable));

            // with strand
            if (m_pending_cnt >= config_max_size_) {
                ASIO_HIREDIS_POOL_DEBUG("all conn is busy with strand..., wait for 10 ms\n");
                co_await pool_detail::timeout(10ms);
                continue;
            }

            co_await atomic_expand();
        }

        co_return ret;
    }

    asio::awaitable<void> pool::reset_client(int id) {
        using namespace std::literals::chrono_literals;

        for (;;) {
            auto new_cli = co_await pool_detail::connect_and_init(io_, config_ip_, config_port_, config_user_, config_pass_, config_db_);
            if (!new_cli) {
                ASIO_HIREDIS_POOL_DEBUG("reset_client id = %d failed, wait 1s\n", id);
                co_await pool_detail::timeout(1s);
                continue;
            }

            m_pool.at(id) = std::move(new_cli);
            ASIO_HIREDIS_POOL_DEBUG("reset_client id = %d succeed\n", id);
            break;
        }
        co_return;
    }

    asio::awaitable<void> pool::atomic_expand() {
        using namespace std::literals::chrono_literals;

        int cnt = ++m_pending_cnt;
        assert(cnt <= config_max_size_);

        for (;;) {
            auto new_cli = co_await pool_detail::connect_and_init(io_, config_ip_, config_port_, config_user_, config_pass_, config_db_);
            if (!new_cli) {
                ASIO_HIREDIS_POOL_DEBUG("atomic_expand cnt = %d failed, wait 1s\n", cnt);
                co_await pool_detail::timeout(1s);
                continue;
            }

            auto lk = std::lock_guard(m_mutex);
            m_idle_ids.emplace(m_pool.size());
            m_pool.emplace_back(std::move(new_cli));
            ASIO_HIREDIS_POOL_DEBUG("atomic_expand cnt = %d, id = %d succeed\n", cnt, (int)m_pool.size() - 1);
            break;
        }

        co_return;
    }

    asio::awaitable<result> pool::async_exec(std::shared_ptr<ahedis::command> cmd) {
        constexpr auto use_nothrow_awaitable = asio::as_tuple(asio::use_awaitable);
        ASIO_HIREDIS_POOL_DEBUG("begin to query\n");
        for (;;) {
            auto conn = co_await get_conn();
            // 在同一个上下文中,co_await并不会导致协程切换,所以得强制strand切换一轮,测试用
            //  co_await asio::post(asio::bind_executor(conn->get()->strand(), asio::use_awaitable));

            if (!conn->get()->has_flag(client::status_flag::ev_connected)) {
                co_await conn->reset();
                continue;
            }
            ASIO_HIREDIS_CLIENT_DEBUG(conn->get()->debug_object_id_, "get conn %d\n", conn->id());
            auto [res] = co_await conn->get()->async_exec(cmd, use_nothrow_awaitable);
            ASIO_HIREDIS_POOL_DEBUG("end to query %d\n", conn->id());
            co_return std::move(res);
        }
    }

    asio::awaitable<bool> pool::test_ping() {
        auto cmd = ahedis::command::create("ping");
        auto res = co_await async_exec(cmd);
        bool ret = false;
        if (res && !res.has_error()) {
            if (res.as_status() == "PONG") {
                ret = true;
            }
        }
        co_return ret;
    }

} // namespace ahedis