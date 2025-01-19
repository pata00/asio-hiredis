#include <asio/as_tuple.hpp>
#include <fmt/format.h>
#include <shared_mutex>

#include "asio_hiredis.hpp"

static std::string count_to_str(std::size_t size) {
    double f_size = size;
    // printf("fsize = %f\n", f_size);
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

static std::atomic<std::size_t> total_size = 0;
static std::atomic<std::size_t> cur_size = 0; // current 1s
static std::chrono::steady_clock::time_point t0;
static std::chrono::steady_clock::time_point t1;
static std::shared_mutex t1_rwlock;
static std::vector<int> run_statics;

asio::awaitable<void> bench_client(asio::io_context& io, int coro_id) {
    constexpr auto use_nothrow_awaitable = asio::as_tuple(asio::use_awaitable);

    auto client = ahedis::client::create(io);
    auto [status, err] = co_await client->async_connect("127.0.0.1", 6379, asio::use_awaitable);
    assert(status == 0);

    // delete last value
    {
        auto cmd = ahedis::command::create("del BENCH_CLIENT_CB_CNT");
        auto [reply] = co_await client->async_exec(cmd, use_nothrow_awaitable);
        assert(reply);
        assert(reply.is_integer());
    }

    auto cmd = ahedis::command::create("incr BENCH_CLIENT_CB_CNT");
    if (coro_id == 0) {
        t0 = std::chrono::steady_clock::now();
        t1 = t0;
    }

    int finished_cnt = 0;

    while (true) {
        auto [reply] = co_await client->async_exec(cmd, use_nothrow_awaitable);
        assert(reply);
        assert(reply.as_longlong() == ++finished_cnt);
        run_statics.at(coro_id)++;
        cur_size++;

        auto t2 = std::chrono::steady_clock::now();

        t1_rwlock.lock_shared();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1);
        t1_rwlock.unlock_shared();
        if (ms.count() >= 1000) {
            auto total_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t0);
            if (total_ms.count() >= 10000) {
                break;
            }
            t1_rwlock.lock();
            t1 = t2;
            t1_rwlock.unlock();
            total_size += cur_size;
            const std::size_t cur_speed = cur_size * 1000 / ms.count();
            const std::size_t avg_speed = total_size * 1000 / std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
            std::size_t print_total_size = total_size;
            cur_size = 0;
            printf("avg: %s/s  cur: %s/s  total: %s\n", count_to_str(avg_speed).c_str(), count_to_str(cur_speed).c_str(),
                   count_to_str(print_total_size).c_str());
        }
    }

    co_await client->async_stop(asio::use_awaitable);
}

int main(int argc, char* argv[]) {
    int thread_count = 1;
    int coroutine_count = 1;

    if (argc >= 2) {
        int val = std::atoi(argv[1]);
        if (val > 0) {
            thread_count = val;
        }
    }

    if (argc >= 3) {
        int val = std::atoi(argv[2]);
        if (val > 0) {
            coroutine_count = val;
        }
    }

    printf("run as thread_count:%d coroutine_count:%d\n", thread_count, coroutine_count);

    asio::io_context io(thread_count);
    signal(SIGPIPE, SIG_IGN);

    run_statics.resize(coroutine_count);
    for (int i = 0; i < coroutine_count; ++i) {
        asio::co_spawn(io, bench_client(io, i), asio::detached);
    }

    std::vector<std::thread> work_threads;
    work_threads.reserve(thread_count);

    for (int i = 0; i < thread_count; ++i) {
        work_threads.emplace_back([&io, i] {
            try {
                io.run();
            } catch (...) {
                assert(false);
            }
        });
    }

    for (auto& t : work_threads) {
        t.join();
    }

    for (std::size_t i = 0; i < run_statics.size(); ++i) {
        printf("run_statics [%02d] %d\n", (int)i, run_statics.at(i));
    }

    return 0;
}
