#include <asio/as_tuple.hpp>

#include "asio_hiredis.hpp"

asio::awaitable<void> test_client1(asio::io_context& io) {
    constexpr auto use_nothrow_awaitable = asio::as_tuple(asio::use_awaitable);

    auto client = ahedis::client::create(io);
    auto [status, err] = co_await client->async_connect("127.0.0.1", 6379, use_nothrow_awaitable);
    if (status != 0) {
        printf("test_client1 async_connect err = %s\n", err.c_str());
        co_return;
    }
    assert(status == 0);

    {
        auto cmd = ahedis::command::create("set a 1");
        auto [res] = co_await client->async_exec(cmd, use_nothrow_awaitable);
        assert(res);
        if (res.has_error()) {
            printf("test_client1 async_exec cmd err = %s\n", res.as_error().data());
            co_await client->async_stop(asio::use_awaitable);
            co_return;
        }
        assert(res.as_status() == "OK");
    }

    {
        auto cmd = ahedis::command::create("get a");
        auto [res] = co_await client->async_exec(cmd, use_nothrow_awaitable);
        assert(res);
        if (res.has_error()) {
            printf("test_client1 async_exec cmd err = %s\n", res.as_error().data());
            co_await client->async_stop(asio::use_awaitable);
            co_return;
        }
        assert(res.as_str() == "1");
    }

    {
        auto cmd = ahedis::command::create("publish chl 1");
        auto [res] = co_await client->async_exec(cmd, use_nothrow_awaitable);
        assert(res);
        if (res.has_error()) {
            printf("test_client1 async_exec cmd err = %s\n", res.as_error().data());
            co_await client->async_stop(asio::use_awaitable);
            co_return;
        }
        assert(res.as_longlong() >= 0);
    }

    co_await client->async_stop(asio::use_awaitable);
    co_return;
}

void test_client2(asio::io_context& io) {
    auto client = ahedis::client::create(io);

    client->async_connect("127.0.0.1", 6379, [client](int status, std::string err) {
        if (status != 0) {
            printf("test_client2 async_connect err = %s\n", err.c_str());
            return;
        }
        assert(status == 0);

        auto cmd1 = ahedis::command::create("set b 2");
        client->async_exec(cmd1, [client](const ahedis::result& res1) {
            assert(res1);
            if (res1.has_error()) {
                printf("test_client2 async_exec cmd1 err = %s\n", res1.as_error().data());
                client->async_stop([client](int status, const std::string& err) {
                    assert(status == 0);
                });
                return;
            }
            assert(res1.as_status() == "OK");

            auto cmd2 = ahedis::command::create("get b");
            client->async_exec(cmd2, [client](const ahedis::result& res2) {
                assert(res2);

                if (res2.has_error()) {
                    printf("test_client2 async_exec cmd2 err = %s\n", res2.as_error().data());
                    client->async_stop([client](int status, const std::string& err) {
                        assert(status == 0);
                    });
                    return;
                }
                assert(res2.as_str() == "2");

                client->async_stop([client](int status, const std::string& err) {
                    assert(status == 0);
                });
            });
        });
    });
}

int main() {
    try {
        asio::io_context io;
        signal(SIGPIPE, SIG_IGN);
        asio::co_spawn(io, test_client1(io), asio::detached);
        // test_client2(io);
        io.run();
    } catch (...) {
        assert(false);
    }
    return 0;
}