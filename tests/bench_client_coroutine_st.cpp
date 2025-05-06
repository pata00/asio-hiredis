#include "helper.h"

#include <asio/as_tuple.hpp>
#include <asio/this_coro.hpp>
#include <fmt/format.h>
#include <shared_mutex>

#include "asio_hiredis.hpp"

constexpr auto use_nothrow_awaitable = asio::as_tuple(asio::use_awaitable);

bool quit = false;
void handle_sigint(int signum) {
    printf("Received SIGINT (Ctrl+C)\n");
    quit = true;
}

asio::awaitable<void> bench_client(asio::io_context& io) {
    auto client = ahedis::client::create(io);
    auto [status, err] = co_await client->async_connect("127.0.0.1", 6379, use_nothrow_awaitable);
    assert(status == 0);

    auto cmd = ahedis::command::create("incr %s", BENCH_KEY);

    while (!quit) {
        auto [reply] = co_await client->async_exec(cmd, use_nothrow_awaitable);
        assert(reply);
        assert(reply.is_integer());
    }

    co_await client->async_stop(asio::use_awaitable);
}

asio::awaitable<void> monitor_client(asio::io_context& io) {
    auto client = ahedis::client::create(io);
    auto [status, err] = co_await client->async_connect("127.0.0.1", 6379, use_nothrow_awaitable);
    assert(status == 0);

    auto cmd = ahedis::command::create("GET %s", BENCH_KEY);

    auto t0 = std::chrono::steady_clock::now();
    int last_bench_cnt = 0;
    while (!quit) {
        co_await sleep_for(io, 1000);
        auto t1 = std::chrono::steady_clock::now();

        auto [reply] = co_await client->async_exec(cmd, use_nothrow_awaitable);
        assert(reply);
        assert(reply.is_string());
        long long current_bench_cnt = std::atoll(reply.value<std::string_view>().data());

        const std::size_t avg_speed = current_bench_cnt * 1000 / std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
        const std::size_t cur_speed = (current_bench_cnt - last_bench_cnt);
        last_bench_cnt = current_bench_cnt;
        printf("avg: %s/s  cur: %s/s  total: %s\n", count_to_str(avg_speed).c_str(), count_to_str(cur_speed).c_str(), count_to_str(current_bench_cnt).c_str());
    }

    co_await client->async_stop(asio::use_awaitable);
}

asio::awaitable<void> async_main(int coroutine_count) {
    auto executor = co_await asio::this_coro::executor;
    auto& context = executor.context();
    auto* io_ptr = static_cast<asio::io_context*>(&context);
    auto& io = *io_ptr;

    // init redis
    {
        auto client = ahedis::client::create(io);
        auto [status, err] = co_await client->async_connect("127.0.0.1", 6379, asio::use_awaitable);
        assert(status == 0);

        auto cmd = ahedis::command::create("del %s", BENCH_KEY);
        auto [reply] = co_await client->async_exec(cmd, use_nothrow_awaitable);
        assert(reply);
        assert(reply.is_integer());

        co_await client->async_stop(asio::use_awaitable);
    }

    for (int i = 0; i < coroutine_count; ++i) {
        asio::co_spawn(io, bench_client(io), asio::detached);
    }
    asio::co_spawn(executor, monitor_client(io), asio::detached);
}

int main(int argc, char* argv[]) {
    int coroutine_count = 1;
    if (argc >= 2) {
        int val = std::atoi(argv[1]);
        if (val > 0) {
            coroutine_count = val;
        }
    }
    printf("run as coroutine_count:%d\n", coroutine_count);

    asio::io_context io;
    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT, handle_sigint);
    asio::co_spawn(io, async_main(coroutine_count), asio::detached);
    io.run();
    return 0;
}
