#include <asio.hpp>
#include <chrono>
#include <fmt/format.h>

#define BENCH_KEY "asio_hiredis_bench_key"

asio::awaitable<void> sleep_for(asio::io_context& io, int ms) {
    auto duration = std::chrono::milliseconds(ms);
    asio::steady_timer timer(io, duration);
    co_await timer.async_wait(asio::use_awaitable);
}

static std::string count_to_str(std::size_t size) {
    double f_size = size;
    if (f_size < 1000) {
        return fmt::format("{0:.2f} ", f_size);
    } else if (f_size < 1000 * 1000) {
        return fmt::format("{0:.2f} K", f_size / 1000);
    } else if (f_size < 1000 * 1000 * 1000) {
        return fmt::format("{0:.2f} M", f_size / 1000 / 1000);
    } else {
        return fmt::format("{0:.2f} G", f_size / 1000 / 1000 / 1000);
    }
}
