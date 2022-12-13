#include <vector>
#include "asio_hiredis/command.h"


int main(){

    auto cmd1 = ahedis::command::create("set a 1");

    auto cmd2 = ahedis::command::create("set a 2");

    auto cmd3(std::move(cmd1));

    auto cmd4(std::move(cmd1));

    std::vector<std::shared_ptr<ahedis::command>> test_v;

    test_v.emplace_back(std::move(ahedis::command::create("set a 3")));

    auto v2 = std::move(test_v);

    return 0;
}