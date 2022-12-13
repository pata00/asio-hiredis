# asio_hiredis
asio coroutine with hiredis async mode

## command
usage like hiredis command
```c++
auto cmd0 = asio_hiredis::command::create("get a");
auto cmd1 = asio_hiredis::command::create("set b 2");
auto cmd2 = asio_hiredis::command::create("set b %s", "2");
auto cmd3 = asio_hiredis::command::create("set b %b", "2", 1);
```
## result
all exec api return a std::shared_ptr<asio_hiredis::result> type
```c++
auto cmd = asio_hiredis::command::create("set a 1");
auto [res] = co_await client->async_exec(cmd1, use_nothrow_awaitable);
if (res){
  if(!res->has_error()){
    auto value = ret->as_T();     //T mean you type
  } else{
    auto err = ret->as_error();   // get error info
  }
}
else{
  //connection has broken, you should reset this client
}
```


## client
see [test_pool.cpp](./tests/test_pool.cpp)
low level api, can use without coroutine support.\
you should call: ```async_connect(IP, PORT)```,```async_exec(AUTH_CMD)```,```async_exec(SELECT_CMD)```by yourself before real data query
- callback mode
- coroutine mode

## pool
see [test_pool.cpp](./tests/test_pool.cpp)
high level api, need use with coroutine support. constructor ```asio_hiredis::pool``` with ```max_size = 1``` for instead ```asio_hiredis::client```.
- auto connect, reconnect, expand..
- support redis uri like ```tcp://[[username:]password@]host[:port][/db]```
- only coroutine mode
