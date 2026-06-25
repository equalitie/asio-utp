#pragma once

#include <boost/asio/ip/udp.hpp>
#include "namespaces.hpp"
#include "weak_from_this.hpp"
#include "intrusive_list.hpp"
#include "id.hpp"
#include <asio_utp/log.hpp>
#include <asio_utp/detail/signal.hpp>
#include <asio_utp/udp_socket.hpp>
#include <boost/system/system_error.hpp>
#include <iostream>
#include <sstream>

namespace asio_utp {

class context;

class udp_multiplexer_impl
    : public std::enable_shared_from_this<udp_multiplexer_impl>
{
public:
    using endpoint_type = asio::ip::udp::endpoint;

    using handler_type = std::function<void( const sys::error_code&
                                           , const endpoint_type&
                                           , const uint8_t*
                                           , size_t)>;

    struct recv_entry {
        intrusive::list_hook hook;
        std::weak_ptr<udp_multiplexer_impl> multiplexer;
        handler_type handler;

        void unlink();

        ~recv_entry();
    };

    using on_send_to_handler = void(
        const std::vector<boost::asio::const_buffer>&,
        size_t,
        const endpoint_type&,
        boost::system::error_code
    );
    using on_send_to_connection = Signal<on_send_to_handler>::Connection;

private:
    using recv_handlers = intrusive::list<recv_entry, &recv_entry::hook>;

public:
    static std::shared_ptr<udp_multiplexer_impl> create(std::unique_ptr<abstract_udp_socket>);

    std::size_t send_to( const std::vector<asio::const_buffer>&
                       , const endpoint_type& destination
                       , asio::socket_base::message_flags
                       , sys::error_code&);

    template< typename WriteHandler>
    void async_send_to( const std::vector<asio::const_buffer>&
                      , const endpoint_type&
                      , WriteHandler&&);

    void register_recv_handler(recv_entry&);

    on_send_to_connection on_send_to(std::function<on_send_to_handler> handler);

    endpoint_type local_endpoint() const {
        return _udp_socket->local_endpoint();
    }

    asio::any_io_executor get_executor() {
        return _udp_socket->get_executor();
    }

    bool is_open() const { return _udp_socket->is_open(); }

    size_t available(sys::error_code&) const;

    ~udp_multiplexer_impl();

    context& get_context() { return *_context; }

    udp_multiplexer_impl(std::unique_ptr<abstract_udp_socket>);

    MultiplexerId id() const {
        return _id;
    }

    void on_recv_entry_unlinked();

private:
    void start_receiving();
    void flush_handlers(const sys::error_code& ec, size_t size);

public:
    // For debugging only
    static
    std::string to_hex(uint8_t*, size_t);

private:
    struct State {
        endpoint_type rx_endpoint;
        std::vector<uint8_t> rx_buffer;

        State() : rx_buffer(65537) {}
    };

    std::unique_ptr<abstract_udp_socket> _udp_socket;

    // Anyone wishing to receive raw UDP packets adds a handler into this
    // intrusive list. Zero or one entry will be from the `context` and zero or
    // more entries will come from the user facing `udp_multiplexer`.
    recv_handlers _recv_handlers;
    Signal<on_send_to_handler> _send_to_signal;
    std::shared_ptr<State> _state;
    // Shared with `socket_impl`.
    std::shared_ptr<context> _context;
    bool _is_receiving = false;
    MultiplexerId _id;
    bool _debug = false;
};

template< typename WriteHandler>
inline
void udp_multiplexer_impl::async_send_to( const std::vector<asio::const_buffer>& buffers
                                        , const endpoint_type& dst
                                        , WriteHandler&& h)
{
    _udp_socket->async_send_to(buffers, dst, [
        &buffers,
        &dst,
        h = std::forward<WriteHandler>(h),
        wself = asio_utp::weak_from_this(this)
    ] (const sys::error_code& ec, std::size_t bytes_transferred) mutable {
        if (auto self = wself.lock()) {
            self->_send_to_signal(buffers, bytes_transferred, dst, ec);
            h(ec, bytes_transferred);
        } else {
            h(asio::error::operation_aborted, 0);
        }
    });
}

} // asio_utp namespace
