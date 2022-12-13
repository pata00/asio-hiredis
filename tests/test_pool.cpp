#include <asio/as_tuple.hpp>

#include "asio_hiredis.hpp"

constexpr auto use_nothrow_awaitable = asio::as_tuple(asio::use_awaitable);

static asio::awaitable<void> timeout(std::chrono::steady_clock::duration duration) {
    asio::steady_timer timer(co_await asio::this_coro::executor);
    timer.expires_after(duration);
    co_await timer.async_wait(use_nothrow_awaitable);
}

asio::awaitable<void> test_exec(asio::io_context& io, ahedis::pool& pool) {
    auto cmd1 = ahedis::command::create("set a 1");
    auto res1 = co_await pool.async_exec(cmd1);
    assert(res1.as_status() == "OK");

    auto cmd2 = ahedis::command::create("get a");
    auto res2 = co_await pool.async_exec(cmd2);
    assert(res2.as_str() == "1");
}

asio::awaitable<void> test_pool_conn(asio::io_context& io, ahedis::pool& pool) {
    auto conn = co_await pool.get_conn();
    auto [res1] = co_await conn->get()->async_exec(ahedis::command::create("set a 1"), use_nothrow_awaitable);
    assert(res1.as_status() == "OK");
    auto [res2] = co_await conn->get()->async_exec(ahedis::command::create("get a"), use_nothrow_awaitable);
    assert(res2.as_str() == "1");
}

asio::awaitable<void> shutdown(asio::io_context& io, ahedis::pool& pool) {
    co_await timeout(std::chrono::seconds(1));
    co_await pool.shutdown();
}

int main() {
    try {
        asio::io_context io;
        signal(SIGPIPE, SIG_IGN);
        // tcp://[[username:]password@]host[:port][/db]
        std::unique_ptr<ahedis::pool> pool_ptr(ahedis::pool::create(io, "tcp://127.0.0.1:6379/0", 0, 10));
        auto& pool = *pool_ptr;
        // asio_hiredis::pool pool(io, "127.0.0.1", 6379, "", "", 0, 0, 10);
        asio::co_spawn(io, test_exec(io, pool), asio::detached);
        asio::co_spawn(io, shutdown(io, pool), asio::detached);
        io.run();
    } catch (...) {
        assert(false);
    }
    return 0;
}