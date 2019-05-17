#pragma once

#include <boost/asio/ip/udp.hpp>
#include <boost/intrusive/list.hpp>
#include "namespaces.hpp"

namespace asio_utp {

class udp_multiplexer_impl
    : public std::enable_shared_from_this<udp_multiplexer_impl>
{
public:
    using endpoint_type = asio::ip::udp::endpoint;

    using handler_type = std::function<void( const sys::error_code&
                                           , const endpoint_type&
                                           , const std::vector<uint8_t>&)>;

private:
    using intrusive_hook = boost::intrusive::list_base_hook
        <boost::intrusive::link_mode
            <boost::intrusive::auto_unlink>>;

public:
    struct recv_entry : intrusive_hook {
        handler_type handler;
    };

private:
    using recv_handlers = boost::intrusive::list
        < recv_entry
        , boost::intrusive::constant_time_size<false>>;

public:
    udp_multiplexer_impl(asio::ip::udp::socket);

    void start();

    template<typename ConstBufferSequence>
    std::size_t send_to( const ConstBufferSequence& buffers
                       , const endpoint_type& destination);

    void register_recv_handler(recv_entry&);

    endpoint_type local_endpoint() const {
        return _udp_socket.local_endpoint();
    }

    boost::asio::io_context::executor_type get_executor()
    {
        return _udp_socket.get_executor();
    }

    size_t available(sys::error_code&) const;

private:
    void start_receiving();
    void on_receive(const sys::error_code& ec, size_t size);

private:
    asio::ip::udp::socket _udp_socket;
    recv_handlers _recv_handlers;
    endpoint_type _rx_endpoint;
    std::vector<uint8_t> _rx_buffer;
};

inline udp_multiplexer_impl::udp_multiplexer_impl(asio::ip::udp::socket s)
    : _udp_socket(std::move(s))
{
    if (!_udp_socket.non_blocking()) {
        _udp_socket.non_blocking(true);
    }
}

inline void udp_multiplexer_impl::start()
{
    start_receiving();
}

inline void udp_multiplexer_impl::start_receiving()
{
    // TODO: Set the size of this buffer to be the maximum of what users
    // of this class require.
    _rx_buffer.resize(65536);

    _udp_socket.async_receive_from
        ( asio::buffer(_rx_buffer)
        , _rx_endpoint
        , [&, self = shared_from_this()]
          (const sys::error_code& ec, size_t size)
          {
              on_receive(ec, size);
          });
}

inline
void udp_multiplexer_impl::on_receive(const sys::error_code& ec, size_t size)
{
    if (ec) size = 0;

    _rx_buffer.resize(size);

    for (auto& e : _recv_handlers) {
        e.handler(ec, _rx_endpoint, _rx_buffer);
    }

    start_receiving();
}

template<typename ConstBufferSequence>
inline
std::size_t udp_multiplexer_impl::send_to( const ConstBufferSequence& buffers
                                         , const endpoint_type& destination)
{
    return _udp_socket.send_to(buffers, destination);
}

inline
size_t udp_multiplexer_impl::available(sys::error_code& ec) const
{
    return _udp_socket.available(ec);
}

} // asio_utp
