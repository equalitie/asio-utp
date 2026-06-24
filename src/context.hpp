#pragma once

#include <boost/asio/ip/udp.hpp>
#include "namespaces.hpp"
#include "util.hpp"
#include "socket_impl.hpp"
#include "intrusive_list.hpp"
#include "udp_multiplexer_impl.hpp"
#include "id.hpp"

#include <utp.h>
#include <asio_utp/socket.hpp>

namespace asio_utp {

class context : public std::enable_shared_from_this<context> {
public:
    using endpoint_type = asio::ip::udp::endpoint;
    using socket_type = asio::ip::udp::socket;
    using executor_type = socket_type::executor_type;

public:
    context(std::shared_ptr<udp_multiplexer_impl>);

    utp_context* get_libutp_context() const { return _utp_ctx; }

    endpoint_type local_endpoint() const { return _local_endpoint; }

    executor_type get_executor();

    ~context();

    void increment_outstanding_ops(const char* dbg);
    void decrement_outstanding_ops(const char* dbg);
    void increment_completed_ops(const char* dbg);
    void decrement_completed_ops(const char* dbg);

    void register_socket(std::shared_ptr<socket_impl>);

    void add_accepting_socket(socket_impl& s) {
        _accepting_sockets.push_back(s);
    }

    MultiplexerId id() const {
        return _id;
    }

    void close();

private:
    void start_receiving();
    void stop_receiving();

    void on_read( const sys::error_code& ec
                , const endpoint_type& ep
                , const uint8_t* data
                , size_t size);

    static uint64 callback_log(utp_callback_arguments*);
    static uint64 callback_sendto(utp_callback_arguments*);
    static uint64 callback_on_error(utp_callback_arguments*);
    static uint64 callback_on_state_change(utp_callback_arguments*);
    static uint64 callback_on_read(utp_callback_arguments*);
    static uint64 callback_on_firewall(utp_callback_arguments*);
    static uint64 callback_on_accept(utp_callback_arguments*);

private:
    executor_type _exec;
    endpoint_type _local_endpoint;
    std::weak_ptr<udp_multiplexer_impl> _multiplexer;
    udp_multiplexer_impl::recv_entry _recv_handle;
    utp_context* _utp_ctx;

    // Registered sockets are all those that use `this` context.
    std::vector<std::shared_ptr<socket_impl>> _registered_sockets;
    intrusive::list<socket_impl, &socket_impl::_accept_hook> _accepting_sockets;

    struct ticker_type;
    std::shared_ptr<ticker_type> _ticker;

    // Number of operation started but their handler have
    // not yet been put onto the execution queue.
    size_t _outstanding_op_count = 0;
    // Number of operations waiting on the execution queue.
    size_t _completed_op_count = 0;
    MultiplexerId _id;

#if ASIO_UTP_DEBUG_LOGGING
    bool _debug = true;
#else
    bool _debug = false;
#endif
};

} // namespace
