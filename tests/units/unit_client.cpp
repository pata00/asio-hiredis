#include <vector>

#include "asio_hiredis/client.h"

int main(){

    asio::io_context io;

    auto client = ahedis::client::create(io);

    // client->async_connect("127.0.0.1", 6379, [&client](int status, std::string error) {
    //     auto cmd = ahedis::command::create("set a 1");
    //     client->async_exec(cmd, [](ahedis::result r) {

    //     });
    // });

    io.run();

    return 0;
}