#include "asio_hiredis.hpp"

void test_client1(asio::io_context& io) {
    auto client = ahedis::client::create(io);

    client->async_connect("127.0.0.1", 6379, [client](int status, const std::string& err) {
        if (status != 0) {
            printf("test_client2 async_connect err = %s\n", err.c_str());
            return;
        }
        assert(status == 0);

        auto cmd1 = ahedis::command::create("blpop abc 1");
        client->async_exec(cmd1, [client](const ahedis::result& res1) {
            if (!res1) {
                return;
            }
            res1.debug_print();
            assert(res1.is_nil());
            // client->async_stop([client](int status, const std::string& err) {
            //     assert(status == 0);
            // });
            return;

            // if (res1->has_error()) {
            //     printf("test_client2 async_exec cmd1 err = %s\n", res1->as_error().data());
            //     client->async_stop([client](int status, const std::string& err) {
            //         assert(status == 0);
            //     });
            //     return;
            // }
            // assert(res1->as_status() == "OK");
        });
    });
}

int main(int argc, char* argv[]) {
    try {
        asio::io_context io;
        signal(SIGPIPE, SIG_IGN);
        test_client1(io);
        // test_client2(io);
        io.run();
    } catch (...) {
        assert(false);
    }
}