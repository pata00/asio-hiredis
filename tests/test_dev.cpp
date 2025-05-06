#include "asio_hiredis.hpp"
#include "helper.h"

constexpr auto use_nothrow_awaitable = asio::as_tuple(asio::use_awaitable);

bool quit = false;
void handle_sigint(int signum) {
    printf("Received SIGINT (Ctrl+C)\n");
    quit = true;
    // 在这里可以执行一些清理操作
}

asio::awaitable<void> load_redis_ticks(std::shared_ptr<ahedis::client>& client) {
    auto cmd = ahedis::command::create("HGETALL ALL_TICKS");
    auto [res] = co_await client->async_exec(cmd, use_nothrow_awaitable);
    assert(!res.has_error());
    for (auto const& [k, v] : res.value<std::vector<std::pair<std::string_view, std::string_view>>>()) {
    }
    // res.debug_print();
    co_return;
}

asio::awaitable<void> async_main(asio::io_context& io) {
    auto client = ahedis::client::create(io);
    auto [status, err] = co_await client->async_connect("192.168.2.12", 6379, use_nothrow_awaitable);
    assert(status == 0);

    co_await load_redis_ticks(client);

    while (!quit) {
        co_await sleep_for(io, 1000);
        break;
    }

    printf("before stop\n");
    co_await client->async_stop(asio::use_awaitable);
    printf("after stop\n");
}

int main(int argc, char* argv[]) {
    asio::io_context io;
    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT, handle_sigint);
    asio::co_spawn(io, async_main(io), asio::detached);
    io.run();
    return 0;
}