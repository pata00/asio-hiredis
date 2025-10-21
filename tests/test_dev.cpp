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
    auto cmd0 = ahedis::command::create("get a");
    auto [res] = co_await client->async_exec(cmd0, use_nothrow_awaitable);
    res.debug_print();

    auto cmd1 = ahedis::command::create("subscribe chl");
    // auto cmd2 = ahedis::command::create("subscribe chl2");

    auto [flag1] = co_await client->async_subscribe(cmd1, use_nothrow_awaitable);

    printf("flag1: %d\n", flag1);

    auto cmd2 = ahedis::command::create("psubscribe hehe.*");
    // auto cmd2 = ahedis::command::create("subscribe chl2");

    auto [flag2] = co_await client->async_subscribe(cmd2, use_nothrow_awaitable);

    printf("flag2: %d\n", flag2);

    // auto [flag2] = co_await client->async_subscribe(cmd2, use_nothrow_awaitable);

    // printf("flag2: %d\n", flag2);
    while (true) {
        auto [res] = co_await client->listen_msg(use_nothrow_awaitable);
        if (res.is_subscribe_result()) [[unlikely]] {
            auto [message_type, channel, value] = res.value<ahedis::result::SUBSCRIBE_RESULT_TYPE>();
            std::cout << "message_type:" << message_type << std::endl;
            std::cout << "channel:" << channel << std::endl;
            std::cout << "value:" << value << std::endl;
        } else if (res.is_psubscribe_result()) {
            auto [message_type, channel, value] = res.value<ahedis::result::PSUBSCRIBE_RESULT_TYPE>();
            std::cout << "message_type:" << message_type << std::endl;
            std::cout << "channel:" << channel << std::endl;
            std::cout << "value:" << value << std::endl;
        } else if (res.is_subscribe_message()) {
            auto [message_type, channel, message] = res.value<ahedis::result::SUBSCRIBE_MESSAGE_TYPE>();
            std::cout << "message_type:" << message_type << std::endl;
            std::cout << "channel:" << channel << std::endl;
            std::cout << "message:" << message << std::endl;
        } else if (res.is_psubscribe_message()) {
            auto [message_type, pattern, channel, message] = res.value<ahedis::result::PSUBSCRIBE_MESSAGE_TYPE>();
            std::cout << "message_type:" << message_type << std::endl;
            std::cout << "pattern:" << pattern << std::endl;
            std::cout << "channel:" << channel << std::endl;
            std::cout << "message:" << message << std::endl;
        } else {
            // error....
            res.debug_print();
        }
    }

    printf("before sleep\n");
    co_await sleep_for(client->io(), 1000);
    printf("after sleep\n");

    // if(res.has_error()) {
    //     printf("error: %s\n", res.as_error().data());
    //     co_return;
    // }

    // res.debug_print();
    // assert(!res.has_error());

    // auto const& tup = res.value<ahedis::result::SUBSCRIBE_RESULT_TYPE>();

    // for (auto const& e : res.value<ahedis::result::SUBSCRIBE_RESULT_TYPE>()) {
    //     //printf("channel: %s, message: %s\n", e.first.data(), e.second.data());
    // }
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