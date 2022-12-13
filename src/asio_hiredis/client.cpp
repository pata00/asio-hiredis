#include "asio_hiredis/client.h"

#include "async.h"
#include "hiredis.h"

namespace ahedis {

#ifdef ENABLE_ASIO_HIREDIS_CLIENT_DEBUG
    std::atomic<int> client::s_current = 0;
    std::atomic<int> client::s_connected_cnt = 0;
#endif

    void client::run() {
        if (has_flag(status_flag::ev_enable_read) && !has_flag(status_flag::ev_in_reading)) {
            set_flag(status_flag::ev_in_reading, true);
            ASIO_HIREDIS_CLIENT_DEBUG(debug_object_id_, "query read...\n");
            socket_.async_wait(asio::ip::tcp::socket::wait_read, asio::bind_executor(strand_, [this, self = shared_from_this()](asio::error_code ec) {
                                   ASIO_HIREDIS_CLIENT_DEBUG(debug_object_id_, "start read...\n");
                                   assert(has_flag(status_flag::ev_in_reading));
                                   if (ec) {
                                       ASIO_HIREDIS_CLIENT_DEBUG(debug_object_id_, "end read error %d, %s\n", ec.value(), ec.message().c_str());
                                       set_flag(status_flag::ev_in_reading, false);
                                       return;
                                   }
                                   redisAsyncHandleRead(ac_); // 内部会回调查询结果, 断开连接回调
                                   set_flag(status_flag::ev_in_reading, false);
                                   ASIO_HIREDIS_CLIENT_DEBUG(debug_object_id_, "end read...\n");
                                   strand_run();
                               }));
            ASIO_HIREDIS_CLIENT_DEBUG(debug_object_id_, "query read end...\n");
        }

        if (has_flag(status_flag::ev_enable_write) && !has_flag(status_flag::ev_in_writing)) {
            set_flag(status_flag::ev_in_writing, true);
            ASIO_HIREDIS_CLIENT_DEBUG(debug_object_id_, "query write...\n");
            socket_.async_wait(asio::ip::tcp::socket::wait_write, asio::bind_executor(strand_, [this, self = shared_from_this()](asio::error_code ec) {
                                   ASIO_HIREDIS_CLIENT_DEBUG(debug_object_id_, "start write...\n");
                                   assert(has_flag(status_flag::ev_in_writing));
                                   if (ec) {
                                       ASIO_HIREDIS_CLIENT_DEBUG(debug_object_id_, "end write error %d %s\n", ec.value(), ec.message().c_str());
                                       set_flag(status_flag::ev_in_writing, false);
                                       return;
                                   }
                                   redisAsyncHandleWrite(ac_); // 内部可能会回调connectcallbak
                                   set_flag(status_flag::ev_in_writing, false);
                                   ASIO_HIREDIS_CLIENT_DEBUG(debug_object_id_, "end write...\n");
                                   strand_run();
                               }));
            ASIO_HIREDIS_CLIENT_DEBUG(debug_object_id_, "query write end...\n");
        }
    }

    void client::stop() {
        socket_.cancel();
        redisAsyncDisconnect(ac_); // 内部会调用 cleanup， 然后再调用disconnectcb
        connect_cb_.reset();
        disconnect_cb_.reset();
    }

    void client::on_connect_cb(int status) {
        std::string msg;
        if (status != REDIS_OK) {
            socket_.release();
            timer_.cancel();
            msg = std::string(ac_->errstr);
        }

        assert(has_flag(status_flag::ev_enable_connect));
        set_flag(status_flag::ev_enable_connect, false);

        assert(!has_flag(status_flag::ev_connected));
        if (status == REDIS_OK) {
            set_flag(status_flag::ev_connected, true);

#ifdef ENABLE_ASIO_HIREDIS_CLIENT_DEBUG
            int current_cnt = ++s_connected_cnt;
            ASIO_HIREDIS_CLIENT_DEBUG(debug_object_id_, "asio::on_connect_cb cnt: %d\n", current_cnt);
            if (current_cnt == 3) {
                // exit(0);
            }
#endif

        } else {
            ASIO_HIREDIS_CLIENT_DEBUG(debug_object_id_, "asio::on_connect_cb Error: %s\n", msg.c_str());
        }

        if (connect_cb_) {
            asio::post(strand_, [handler_ptr = std::move(connect_cb_), status, msg = std::move(msg)] {
                std::move (*handler_ptr)(status, std::move(msg));
            });
        }

        // TODO 是否只有异常断开会触发此错误
        if (status == REDIS_OK) {
            socket_.async_wait(asio::ip::tcp::socket::wait_error, asio::bind_executor(strand_, [this, self = shared_from_this()](asio::error_code ec) {
                                   ASIO_HIREDIS_CLIENT_DEBUG(debug_object_id_, "END===========wait_error %d, %s\n", ec.value(), ec.message().c_str());
                                   // assert(false);
                               }));
        }
    }

    void client::on_disconnect_cb(int status) {
        assert(has_flag(status_flag::ev_connected));
        set_flag(status_flag::ev_connected, false);
        socket_.release();
        timer_.cancel();

        std::string msg;
        if (status != REDIS_OK) {
            msg = std::string(ac_->errstr);
        }

        if (status == REDIS_OK) { // 主动关闭
            assert(has_flag(status_flag::ev_enable_disconnect));
            set_flag(status_flag::ev_enable_disconnect, false);
        } else {
            ASIO_HIREDIS_CLIENT_DEBUG(debug_object_id_, "asio::on_disconnect_cb Error: %s\n", msg.c_str());
        }

        if (disconnect_cb_) {
            asio::post(strand_, [handler_ptr = std::move(disconnect_cb_), status, msg = std::move(msg)] {
                std::move (*handler_ptr)(status, std::move(msg));
            });
        }
    }

    void client::on_exec_cb(void* r) {
        ASIO_HIREDIS_CLIENT_DEBUG(debug_object_id_, "query cb with return :%p\n", r);
        assert(!execute_resume_cb_queue_.empty()); // not support subcribe

        auto handler_ptr = std::move(execute_resume_cb_queue_.front());
        execute_resume_cb_queue_.pop_front();

        asio::post(strand_, [handler_ptr = std::move(handler_ptr), r] {
            ahedis::result arg(static_cast<redisReply*>(r));
            std::move (*handler_ptr)(std::move(arg));
        });
    }

} // namespace ahedis
