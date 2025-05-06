#include <asio/as_tuple.hpp>
#include <fmt/format.h>
#include <shared_mutex>

#include "asio_hiredis.hpp"
#include "helper.h"

constexpr auto use_nothrow_awaitable = asio::as_tuple(asio::use_awaitable);

bool quit = false;
void handle_sigint(int signum) {
    printf("Received SIGINT (Ctrl+C)\n");
    quit = true;
    // 在这里可以执行一些清理操作
}

asio::awaitable<void> bench_client(asio::io_context& io) {
    auto client = ahedis::client::create(io);
    auto [status, err] = co_await client->async_connect("127.0.0.1", 6379, use_nothrow_awaitable);
    assert(status == 0);

    // delete last value
    {
        auto cmd = ahedis::command::create("del %s", BENCH_KEY);
        auto [reply] = co_await client->async_exec(cmd, use_nothrow_awaitable);
        // assert(reply);
        // assert(reply.is_integer());
    }

    auto cmd = ahedis::command::create("incr %s", BENCH_KEY);

    int querying_cnt = 0;
    long long finished_cnt = 0ll;

    while (!quit) {
        if (querying_cnt < 4096) {
            client->async_exec(cmd, [&querying_cnt, &finished_cnt](const ahedis::result& reply) {
                assert(reply);
                assert(reply.value<long long>() == ++finished_cnt);
                --querying_cnt;
            });
            ++querying_cnt;

        } else {
            co_await sleep_for(client->io(), 1);
            continue;
        }
    }

    while (querying_cnt != 0) {
        // co_await asio::post(client->strand(), asio::use_awaitable); 无法实现等待flush，因为时间并不挂在strand上，只是回调那一刻才出现在strand上
        co_await sleep_for(client->io(), 100);
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

// bench result:
// avg: 1.22 M/s  cur: 1.23 M/s  total: 342.88 M
int main(int argc, char* argv[]) {
    asio::io_context io;
    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT, handle_sigint);
    asio::co_spawn(io, bench_client(io), asio::detached);
    asio::co_spawn(io, monitor_client(io), asio::detached);
    io.run();
    return 0;
}
