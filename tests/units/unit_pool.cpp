#include <vector>

#include "asio_hiredis/pool.h"
#include "asio_hiredis/pool.cpp"
#include "asio_hiredis/client.cpp"

int main(){

    asio::io_context io;

    ahedis::pool pool(io, "127.0.0.1", 6379, "", "", 0, 0, 10);


    return 0;
}